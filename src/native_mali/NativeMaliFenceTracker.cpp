/* SPDX-License-Identifier: GPL-2.0-only
 *
 * Fence bookkeeping for the NativeMali ABI. See NativeMaliFenceTracker.h.
 */
#include "NativeMaliFenceTracker.h"

NativeMaliFenceTracker::NativeMaliFenceTracker() {
    buckets_.fill(kEmpty);
}

size_t NativeMaliFenceTracker::hash_fence(uint64_t fence) {
    /* SplitMix64 finalizer for well-distributed open addressing. */
    uint64_t x = fence;
    x ^= x >> 30;
    x *= UINT64_C(0xbf58476d1ce4e5b9);
    x ^= x >> 27;
    x *= UINT64_C(0x94d049bb133111eb);
    x ^= x >> 31;
    return static_cast<size_t>(x) & (kHashBuckets - 1);
}

size_t NativeMaliFenceTracker::find_index(uint64_t fence) const {
    if (fence == 0 || count_ == 0) {
        return kCapacity;
    }
    size_t bucket = hash_fence(fence);
    for (size_t step = 0; step < kHashBuckets; ++step) {
        const uint16_t slot = buckets_[bucket];
        if (slot == kEmpty) {
            return kCapacity;
        }
        if (slot != kTombstone && entries_[slot].fence == fence) {
            return slot;
        }
        bucket = (bucket + 1) & (kHashBuckets - 1);
    }
    return kCapacity;
}

void NativeMaliFenceTracker::hash_insert(uint64_t fence, uint16_t index) {
    if (tombstones_ > kHashBuckets / 4) {
        hash_rebuild();
    }
    size_t bucket = hash_fence(fence);
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

void NativeMaliFenceTracker::hash_rebuild() {
    buckets_.fill(kEmpty);
    tombstones_ = 0;
    for (size_t index = 0; index < count_; ++index) {
        size_t bucket = hash_fence(entries_[index].fence);
        while (buckets_[bucket] != kEmpty) {
            bucket = (bucket + 1) & (kHashBuckets - 1);
        }
        buckets_[bucket] = static_cast<uint16_t>(index);
    }
}

bool NativeMaliFenceTracker::track(uint64_t fence, uint64_t dep_fence) {
    if (fence == 0) {
        return false;
    }
    if (find_index(fence) != kCapacity) {
        return false;
    }
    if (count_ == kCapacity) {
        return false;
    }
    const size_t slot = count_;
    entries_[slot].fence = fence;
    entries_[slot].dep_fence = (dep_fence == fence) ? 0 : dep_fence;
    entries_[slot].order = ++next_order_;
    entries_[slot].state = NativeMaliFenceState::Pending;
    ++count_;
    ++pending_count_;
    hash_insert(fence, static_cast<uint16_t>(slot));
    return true;
}

bool NativeMaliFenceTracker::resolve(uint64_t fence) {
    const size_t index = find_index(fence);
    if (index == kCapacity || entries_[index].state != NativeMaliFenceState::Pending) {
        return false;
    }
    entries_[index].state = NativeMaliFenceState::Signaled;
    --pending_count_;
    return true;
}

size_t NativeMaliFenceTracker::resolve_chain(uint64_t fence, uint64_t *resolved_out,
                                             size_t max_out) {
    const size_t first = find_index(fence);
    if (first == kCapacity || entries_[first].state != NativeMaliFenceState::Pending) {
        return 0;
    }
    size_t resolved = 0;
    size_t current = first;
    for (size_t hops = 0; hops < kCapacity && current != kCapacity; ++hops) {
        Entry &entry = entries_[current];
        if (entry.state != NativeMaliFenceState::Pending) {
            break;
        }
        entry.state = NativeMaliFenceState::Signaled;
        --pending_count_;
        if (resolved_out != nullptr && resolved < max_out) {
            resolved_out[resolved] = entry.fence;
        }
        ++resolved;
        const uint64_t dep = entry.dep_fence;
        entry.dep_fence = 0;
        current = (dep != 0) ? find_index(dep) : kCapacity;
    }
    return resolved;
}

size_t NativeMaliFenceTracker::lose_all() {
    size_t lost = 0;
    for (size_t index = 0; index < count_; ++index) {
        if (entries_[index].state == NativeMaliFenceState::Pending) {
            entries_[index].state = NativeMaliFenceState::Lost;
            ++lost;
        }
    }
    pending_count_ = 0;
    return lost;
}

bool NativeMaliFenceTracker::forget_at(size_t index) {
    if (index >= count_) {
        return false;
    }
    const uint64_t removed_fence = entries_[index].fence;
    if (entries_[index].state == NativeMaliFenceState::Pending) {
        --pending_count_;
    }

    /* Mark the removed fence's hash bucket as a tombstone. */
    size_t bucket = hash_fence(removed_fence);
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
        const uint64_t moved_fence = entries_[index].fence;
        size_t mb = hash_fence(moved_fence);
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
    entries_[last] = Entry{};
    --count_;
    if (tombstones_ > kHashBuckets / 4) {
        hash_rebuild();
    }
    return true;
}

bool NativeMaliFenceTracker::forget(uint64_t fence) {
    const size_t index = find_index(fence);
    if (index == kCapacity) {
        return false;
    }
    return forget_at(index);
}

bool NativeMaliFenceTracker::evict_oldest_finished() {
    size_t oldest_signaled = kCapacity;
    uint64_t oldest_signaled_order = UINT64_MAX;
    size_t oldest_lost = kCapacity;
    uint64_t oldest_lost_order = UINT64_MAX;

    for (size_t index = 0; index < count_; ++index) {
        const Entry &entry = entries_[index];
        if (entry.state == NativeMaliFenceState::Signaled) {
            if (entry.order < oldest_signaled_order) {
                oldest_signaled_order = entry.order;
                oldest_signaled = index;
            }
        } else if (entry.state == NativeMaliFenceState::Lost) {
            if (entry.order < oldest_lost_order) {
                oldest_lost_order = entry.order;
                oldest_lost = index;
            }
        }
    }
    if (oldest_signaled != kCapacity) {
        return forget_at(oldest_signaled);
    }
    if (oldest_lost != kCapacity) {
        return forget_at(oldest_lost);
    }
    return false;
}

uint64_t NativeMaliFenceTracker::oldest_pending() const {
    uint64_t best_fence = 0;
    uint64_t best_order = UINT64_MAX;
    for (size_t index = 0; index < count_; ++index) {
        const Entry &entry = entries_[index];
        if (entry.state == NativeMaliFenceState::Pending && entry.order < best_order) {
            best_order = entry.order;
            best_fence = entry.fence;
        }
    }
    return best_fence;
}

NativeMaliFenceState NativeMaliFenceTracker::state(uint64_t fence) const {
    const size_t index = find_index(fence);
    if (index == kCapacity) {
        return NativeMaliFenceState::Unknown;
    }
    return entries_[index].state;
}
