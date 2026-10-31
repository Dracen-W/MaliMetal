/* SPDX-License-Identifier: GPL-2.0-only
 *
 * Userspace IOVA allocator. See NativeMaliIovaAllocator.h.
 *
 * The policy is deliberately boring: a sorted free list, first fit, merge on
 * release. It is not a performance allocator; it is one that can be explained
 * and tested, which matters more while the kernel side is still being proven.
 */
#include "NativeMaliIovaAllocator.h"

#include <algorithm>

NativeMaliIovaAllocator::NativeMaliIovaAllocator(uint64_t base, uint64_t size,
                                                 uint64_t page_bytes)
    : base_(base), size_(size), page_bytes_(page_bytes) {
    if (page_bytes_ == 0 || base_ == 0 || size_ == 0) {
        return;
    }
    if (!aligned(base_, page_bytes_) || !aligned(size_, page_bytes_)) {
        return;
    }
    if (base_ > UINT64_MAX - size_) {
        return;
    }
    free_ranges_.push_back({base_, base_ + size_});
    valid_ = true;
}

bool NativeMaliIovaAllocator::overlaps_allocation(uint64_t start, uint64_t end) const {
    for (const auto &allocation : allocations_) {
        const uint64_t other_end = allocation.iova + allocation.bytes;
        if (start < other_end && allocation.iova < end) {
            return true;
        }
    }
    return false;
}

void NativeMaliIovaAllocator::insert_free(uint64_t start, uint64_t end) {
    if (start >= end) {
        return;
    }
    /* Sorted insert, then merge with neighbours. release_all() and normal frees
     * both come through here, so the list stays canonical. */
    auto position = std::lower_bound(
        free_ranges_.begin(), free_ranges_.end(), start,
        [](const Range &range, uint64_t value) { return range.start < value; });
    position = free_ranges_.insert(position, Range{start, end});
    if (position != free_ranges_.begin()) {
        auto previous = position - 1;
        if (previous->end == position->start) {
            previous->end = position->end;
            free_ranges_.erase(position);
            position = previous;
        }
    }
    auto next = position + 1;
    if (next != free_ranges_.end() && position->end == next->start) {
        position->end = next->end;
        free_ranges_.erase(next);
    }
}

bool NativeMaliIovaAllocator::reserve(uint64_t bytes, uint64_t *iova) {
    if (!valid_ || iova == nullptr || bytes == 0 || !aligned(bytes, page_bytes_)) {
        return false;
    }
    for (auto it = free_ranges_.begin(); it != free_ranges_.end(); ++it) {
        if (it->bytes() < bytes) {
            continue;
        }
        const uint64_t start = it->start;
        const uint64_t end = start + bytes;
        if (end > it->end) { /* cannot happen after the size check; keep it exact */
            return false;
        }
        if (overlaps_allocation(start, end)) {
            continue;
        }
        if (end == it->end) {
            free_ranges_.erase(it);
        } else {
            it->start = end;
        }
        allocations_.push_back({start, bytes});
        *iova = start;
        return true;
    }
    return false;
}

bool NativeMaliIovaAllocator::free(uint64_t iova, uint64_t bytes, uint64_t guard_fence) {
    if (!valid_ || bytes == 0 || !aligned(iova, page_bytes_) || !aligned(bytes, page_bytes_)) {
        return false;
    }
    auto it = std::find_if(
        allocations_.begin(), allocations_.end(),
        [iova, bytes](const Allocation &allocation) {
            return allocation.iova == iova && allocation.bytes == bytes;
        });
    if (it == allocations_.end()) {
        return false;
    }
    allocations_.erase(it);
    if (guard_fence == 0) {
        insert_free(iova, iova + bytes);
    } else {
        quarantine_.push_back({iova, bytes, guard_fence});
    }
    return true;
}

size_t NativeMaliIovaAllocator::release(uint64_t guard_fence) {
    size_t released = 0;
    for (auto it = quarantine_.begin(); it != quarantine_.end();) {
        if (it->guard == guard_fence) {
            insert_free(it->iova, it->iova + it->bytes);
            it = quarantine_.erase(it);
            ++released;
        } else {
            ++it;
        }
    }
    return released;
}

size_t NativeMaliIovaAllocator::release_all() {
    const size_t released = quarantine_.size();
    for (const auto &entry : quarantine_) {
        insert_free(entry.iova, entry.iova + entry.bytes);
    }
    quarantine_.clear();
    return released;
}

uint64_t NativeMaliIovaAllocator::allocated_bytes() const {
    uint64_t total = 0;
    for (const auto &allocation : allocations_) {
        total += allocation.bytes;
    }
    return total;
}

uint64_t NativeMaliIovaAllocator::quarantined_bytes() const {
    uint64_t total = 0;
    for (const auto &entry : quarantine_) {
        total += entry.bytes;
    }
    return total;
}

uint64_t NativeMaliIovaAllocator::free_bytes() const {
    uint64_t total = 0;
    for (const auto &range : free_ranges_) {
        total += range.bytes();
    }
    return total;
}

uint64_t NativeMaliIovaAllocator::largest_free_bytes() const {
    uint64_t largest = 0;
    for (const auto &range : free_ranges_) {
        largest = std::max(largest, range.bytes());
    }
    return largest;
}
