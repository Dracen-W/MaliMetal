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

NativeMaliBo *NativeMaliBoTable::mutable_find(uint64_t handle) {
    for (size_t index = 0; index < count_; ++index) {
        if (entries_[index].handle == handle) {
            return &entries_[index];
        }
    }
    return nullptr;
}

const NativeMaliBo *NativeMaliBoTable::find(uint64_t handle) const {
    return const_cast<NativeMaliBoTable *>(this)->mutable_find(handle);
}

bool NativeMaliBoTable::add(uint64_t handle, uint64_t bytes) {
    if (handle == 0 || bytes == 0 || !aligned(bytes)) {
        return false;
    }
    if (bytes > NATIVE_MALI_ABI_MAX_BO_BYTES) {
        return false;
    }
    if (mutable_find(handle) != nullptr) {
        return false; /* a handle is unique for the life of the client */
    }
    if (count_ == kCapacity) {
        return false;
    }
    NativeMaliBo record{};
    record.handle = handle;
    record.bytes = bytes;
    entries_[count_++] = record;
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
    for (size_t index = 0; index < count_; ++index) {
        if (entries_[index].handle != handle) {
            continue;
        }
        if (entries_[index].mapped) {
            if (iova == nullptr) {
                return false;
            }
            *iova = entries_[index].iova;
        } else if (iova != nullptr) {
            *iova = 0;
        }
        entries_[index] = entries_[count_ - 1];
        --count_;
        return true;
    }
    return false;
}
