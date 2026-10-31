/* SPDX-License-Identifier: GPL-2.0-only
 *
 * Fence bookkeeping for the NativeMali ABI. See NativeMaliFenceTracker.h.
 */
#include "NativeMaliFenceTracker.h"

NativeMaliFenceTracker::Entry *NativeMaliFenceTracker::mutable_find(uint64_t fence) {
    for (size_t index = 0; index < count_; ++index) {
        if (entries_[index].fence == fence) {
            return &entries_[index];
        }
    }
    return nullptr;
}

bool NativeMaliFenceTracker::track(uint64_t fence) {
    if (fence == 0) {
        return false;
    }
    if (mutable_find(fence) != nullptr) {
        return false;
    }
    if (count_ == kCapacity) {
        return false;
    }
    entries_[count_].fence = fence;
    entries_[count_].state = NativeMaliFenceState::Pending;
    ++count_;
    return true;
}

bool NativeMaliFenceTracker::resolve(uint64_t fence) {
    Entry *entry = mutable_find(fence);
    if (entry == nullptr || entry->state != NativeMaliFenceState::Pending) {
        return false;
    }
    entry->state = NativeMaliFenceState::Signaled;
    return true;
}

size_t NativeMaliFenceTracker::lose_all() {
    size_t lost = 0;
    for (size_t index = 0; index < count_; ++index) {
        if (entries_[index].state == NativeMaliFenceState::Pending) {
            entries_[index].state = NativeMaliFenceState::Lost;
            ++lost;
        }
    }
    return lost;
}

bool NativeMaliFenceTracker::forget(uint64_t fence) {
    for (size_t index = 0; index < count_; ++index) {
        if (entries_[index].fence != fence) {
            continue;
        }
        entries_[index] = entries_[count_ - 1];
        --count_;
        return true;
    }
    return false;
}

NativeMaliFenceState NativeMaliFenceTracker::state(uint64_t fence) const {
    for (size_t index = 0; index < count_; ++index) {
        if (entries_[index].fence == fence) {
            return entries_[index].state;
        }
    }
    return NativeMaliFenceState::Unknown;
}

size_t NativeMaliFenceTracker::pending_count() const {
    size_t pending = 0;
    for (size_t index = 0; index < count_; ++index) {
        if (entries_[index].state == NativeMaliFenceState::Pending) {
            ++pending;
        }
    }
    return pending;
}
