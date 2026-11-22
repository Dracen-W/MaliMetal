/* SPDX-License-Identifier: GPL-2.0-only
 *
 * Buffer records for the NativeMali ABI. See NativeMaliBoTable.h.
 */
#include "NativeMaliBoTable.h"

#include "NativeMaliABI.h"

namespace {

bool aligned(uint64_t value) {
    return (value % NativeMaliAbiPageBytes) == 0;
}

} /* namespace */

NativeMaliBoTable::NativeMaliBoTable() {
    buckets_.fill(kEmpty);
}

size_t NativeMaliBoTable::hash_handle(uint64_t handle) {
    uint64_t x = handle;
    x ^= x >> 30;
    x *= UINT64_C(0xbf58476d1ce4e5b9);
    x ^= x >> 27;
    x *= UINT64_C(0x94d049bb133111eb);
    x ^= x >> 31;
    return static_cast<size_t>(x) & (kHashBuckets - 1);
}

size_t NativeMaliBoTable::find_index(uint64_t handle) const {
    if (handle == 0 || count_ == 0) {
        return kCapacity;
    }
    size_t bucket = hash_handle(handle);
    for (size_t step = 0; step < kHashBuckets; ++step) {
        const uint16_t slot = buckets_[bucket];
        if (slot == kEmpty) {
            return kCapacity;
        }
        if (slot != kTombstone && entries_[slot].handle == handle) {
            return slot;
        }
        bucket = (bucket + 1) & (kHashBuckets - 1);
    }
    return kCapacity;
}

void NativeMaliBoTable::hash_insert(uint64_t handle, uint16_t index) {
    if (tombstones_ > kHashBuckets / 4) {
        hash_rebuild();
    }
    size_t bucket = hash_handle(handle);
    for (size_t step = 0; step < kHashBuckets; ++step) {
        const uint16_t slot = buckets_[bucket];
        if (slot == kEmpty || slot == kTombstone) {
            if (slot == kTombstone) {
                --tombstones_;
            }
            buckets_[bucket] = index;
            return;
        }
        bucket = (bucket + 1) & (kHashBuckets - 1);
    }
}

void NativeMaliBoTable::hash_rebuild() {
    buckets_.fill(kEmpty);
    tombstones_ = 0;
    for (size_t index = 0; index < count_; ++index) {
        size_t bucket = hash_handle(entries_[index].handle);
        while (buckets_[bucket] != kEmpty) {
            bucket = (bucket + 1) & (kHashBuckets - 1);
        }
        buckets_[bucket] = static_cast<uint16_t>(index);
    }
}

NativeMaliBo *NativeMaliBoTable::mutable_find(uint64_t handle) {
    const size_t index = find_index(handle);
    return index == kCapacity ? nullptr : &entries_[index];
}

const NativeMaliBo *NativeMaliBoTable::find(uint64_t handle) const {
    const size_t index = find_index(handle);
    return index == kCapacity ? nullptr : &entries_[index];
}

bool NativeMaliBoTable::add(uint64_t handle, uint64_t bytes) {
    if (handle == 0 || bytes == 0 || !aligned(bytes)) {
        return false;
    }
    if (bytes > NATIVE_MALI_ABI_MAX_BO_BYTES) {
        return false;
    }
    if (find_index(handle) != kCapacity) {
        return false; /* a handle is unique for the life of the client */
    }
    if (count_ == kCapacity) {
        return false;
    }
    NativeMaliBo record{};
    record.handle = handle;
    record.bytes = bytes;
    const size_t slot = count_++;
    entries_[slot] = record;
    hash_insert(handle, static_cast<uint16_t>(slot));
    return true;
}

bool NativeMaliBoTable::set_mapping(uint64_t handle, uint64_t iova, uint32_t permissions) {
    NativeMaliBo *record = mutable_find(handle);
    if (record == nullptr || record->mapped) {
        return false;
    }
    if (iova == 0 || !aligned(iova)) {
        return false;
    }
    if ((permissions & NativeMaliBoRead) == 0) {
        /* The header says read is always required: panfrost maps IOMMU_READ. */
        return false;
    }
    if ((permissions & ~(NativeMaliBoRead | NativeMaliBoWrite)) != 0) {
        return false; /* unknown permission bit: refuse, do not mask */
    }
    record->mapped = true;
    record->iova = iova;
    record->permissions = permissions;
    return true;
}

bool NativeMaliBoTable::clear_mapping(uint64_t handle, uint64_t *iova) {
    NativeMaliBo *record = mutable_find(handle);
    if (record == nullptr || !record->mapped || iova == nullptr) {
        return false;
    }
    *iova = record->iova;
    record->mapped = false;
    record->iova = 0;
    record->permissions = 0;
    return true;
}

bool NativeMaliBoTable::set_cpu_token(uint64_t handle, uint32_t token) {
    NativeMaliBo *record = mutable_find(handle);
    if (record == nullptr || record->cpu_mapped || token == 0) {
        return false;
    }
    record->cpu_mapped = true;
    record->cpu_token = token;
    return true;
}

bool NativeMaliBoTable::clear_cpu_token(uint64_t handle, uint32_t token) {
    NativeMaliBo *record = mutable_find(handle);
    if (record == nullptr || !record->cpu_mapped || record->cpu_token != token) {
        return false;
    }
    record->cpu_mapped = false;
    record->cpu_token = 0;
    return true;
}

bool NativeMaliBoTable::remove(uint64_t handle, uint64_t *iova) {
    const size_t index = find_index(handle);
    if (index == kCapacity) {
        return false;
    }
    if (entries_[index].mapped) {
        if (iova == nullptr) {
            return false;
        }
        *iova = entries_[index].iova;
    } else if (iova != nullptr) {
        *iova = 0;
    }

    size_t bucket = hash_handle(handle);
    for (size_t step = 0; step < kHashBuckets; ++step) {
        const uint16_t slot = buckets_[bucket];
        if (slot == kEmpty) {
            break;
        }
        if (slot == index) {
            buckets_[bucket] = kTombstone;
            ++tombstones_;
            break;
        }
        bucket = (bucket + 1) & (kHashBuckets - 1);
    }

    const size_t last = count_ - 1;
    if (index != last) {
        entries_[index] = entries_[last];
        const uint64_t moved_handle = entries_[index].handle;
        size_t mb = hash_handle(moved_handle);
        for (size_t step = 0; step < kHashBuckets; ++step) {
            const uint16_t slot = buckets_[mb];
            if (slot == kEmpty) {
                break;
            }
            if (slot == last) {
                buckets_[mb] = static_cast<uint16_t>(index);
                break;
            }
            mb = (mb + 1) & (kHashBuckets - 1);
        }
    }
    entries_[last] = NativeMaliBo{};
    --count_;
    if (tombstones_ > kHashBuckets / 4) {
        hash_rebuild();
    }
    return true;
}
