/* SPDX-License-Identifier: GPL-2.0-only
 *
 * The userspace IOVA allocator for the NativeMali ABI.
 *
 * NativeMaliMemory::map() takes the address; the MMU has no allocator of its
 * own (see Mali-G52 docs and NativeMaliABI.h). PanVK expects the driver to
 * supply one, so this is the driver's allocator, and it is pure logic: no
 * kernel call, no hardware, host-tested.
 *
 * Rules taken from the ABI, not invented:
 *   - 4 KiB granularity (NativeMaliAbiPageBytes);
 *   - an IOVA of zero is not a valid mapping (NativeMaliAbiBoMap.iova is
 *     "nonzero");
 *   - ranges must not overlap;
 *   - a range freed while a job that may still read it is in flight is
 *     quarantined until that fence completes, then released. A device reset
 *     releases everything, because the GPU can no longer reach the old state.
 */
#ifndef MALIMETAL_NATIVE_MALI_IOVA_ALLOCATOR_H
#define MALIMETAL_NATIVE_MALI_IOVA_ALLOCATOR_H

#include <stddef.h>
#include <stdint.h>

#include <vector>

class NativeMaliIovaAllocator {
public:
    /* `base` must be page aligned and nonzero; `size` a multiple of the page
     * size. The usable range is what Query reports, not the whole address
     * space. */
    NativeMaliIovaAllocator(uint64_t base, uint64_t size, uint64_t page_bytes = 4096);

    bool valid() const { return valid_; }

    /* First fit. Returns false for zero, unaligned or oversized requests. */
    bool reserve(uint64_t bytes, uint64_t *iova);

    /* Free an exact allocation. A zero guard releases the range immediately;
     * a nonzero guard keeps it out of circulation until release(guard) or
     * release_all(). Returns false when no allocation matches exactly. */
    bool free(uint64_t iova, uint64_t bytes, uint64_t guard_fence = 0);

    /* Release quarantined ranges guarded by this exact fence. Fences are
     * opaque, so this matches by equality, never by order. Returns how many
     * ranges were released. */
    size_t release(uint64_t guard_fence);

    /* Device reset: every quarantined range is safe to reuse. */
    size_t release_all();

    uint64_t base() const { return base_; }
    uint64_t size() const { return size_; }
    uint64_t page_bytes() const { return page_bytes_; }
    uint64_t allocated_bytes() const;
    uint64_t quarantined_bytes() const;
    uint64_t free_bytes() const;
    uint64_t largest_free_bytes() const;
    size_t allocation_count() const { return allocations_.size(); }
    size_t free_range_count() const { return free_ranges_.size(); }
    size_t quarantine_count() const { return quarantine_.size(); }

private:
    struct Range {
        uint64_t start;
        uint64_t end; /* exclusive */
        uint64_t bytes() const { return end - start; }
    };
    struct Allocation {
        uint64_t iova;
        uint64_t bytes;
    };
    struct Quarantined {
        uint64_t iova;
        uint64_t bytes;
        uint64_t guard;
    };

    static bool aligned(uint64_t value, uint64_t alignment) {
        return alignment != 0 && (value % alignment) == 0;
    }
    void insert_free(uint64_t start, uint64_t end);
    bool overlaps_allocation(uint64_t start, uint64_t end) const;

    uint64_t base_ = 0;
    uint64_t size_ = 0;
    uint64_t page_bytes_ = 4096;
    bool valid_ = false;
    std::vector<Range> free_ranges_;
    std::vector<Allocation> allocations_;
    std::vector<Quarantined> quarantine_;
};

#endif /* MALIMETAL_NATIVE_MALI_IOVA_ALLOCATOR_H */
