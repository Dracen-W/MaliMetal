/* SPDX-License-Identifier: GPL-2.0-only
 *
 * Host test for the userspace IOVA allocator: alignment, first fit, merging,
 * exact free, and the quarantine that keeps a range away from the GPU until the
 * fence that may still reference it has completed.
 */
#include "NativeMaliIovaAllocator.h"

#include <cstdio>

namespace {

int failures = 0;

void expect(bool condition, const char *what) {
    if (!condition) {
        std::fprintf(stderr, "FAIL %s\n", what);
        ++failures;
    }
}

constexpr uint64_t kPage = 4096;

} /* namespace */

int main() {
    /* Construction is refused for values that cannot describe a usable range. */
    expect(!NativeMaliIovaAllocator(0, 0x100000).valid(), "base 0 refused (iova is nonzero)");
    expect(!NativeMaliIovaAllocator(0x1000, 0).valid(), "zero size refused");
    expect(!NativeMaliIovaAllocator(0x1800, 0x1000).valid(), "unaligned base refused");
    expect(!NativeMaliIovaAllocator(0x1000, 0x1800).valid(), "unaligned size refused");
    expect(!NativeMaliIovaAllocator(UINT64_MAX - 0x1000, 0x2000).valid(), "overflow refused");
    expect(NativeMaliIovaAllocator(0x1000, 0x10000).valid(), "a normal range is valid");

    NativeMaliIovaAllocator allocator(0x100000, 0x10000); /* 64 KiB, 16 pages */

    /* Basic reserve: first fit starts at the base. */
    uint64_t first = 0;
    expect(allocator.reserve(3 * kPage, &first), "reserve 3 pages");
    expect(first == 0x100000, "first fit starts at the base");
    expect(allocator.allocated_bytes() == 3 * kPage, "allocated bytes count");

    /* Zero, unaligned and null requests are refused without side effects. */
    uint64_t ignored = 0;
    expect(!allocator.reserve(0, &ignored), "zero bytes refused");
    expect(!allocator.reserve(kPage + 1, &ignored), "unaligned bytes refused");
    expect(!allocator.reserve(kPage, nullptr), "null out pointer refused");
    expect(allocator.allocated_bytes() == 3 * kPage, "refusals allocate nothing");

    /* Second reservation is contiguous and does not overlap. */
    uint64_t second = 0;
    expect(allocator.reserve(2 * kPage, &second), "reserve 2 pages");
    expect(second == first + 3 * kPage, "second allocation follows the first");
    expect(second >= first + 3 * kPage, "no overlap");

    /* Exact free, then reuse: the freed hole is taken before the tail. */
    expect(allocator.free(first, 3 * kPage), "free the first allocation");
    expect(allocator.free(first, 3 * kPage) == false, "double free refused");
    expect(allocator.free(first + kPage, kPage) == false, "free of an unknown range refused");
    expect(allocator.free(first + 1, 3 * kPage) == false, "unaligned free refused");

    uint64_t reused = 0;
    expect(allocator.reserve(kPage, &reused), "reserve into the hole");
    expect(reused == first, "the hole is reused first");

    /* Merge: free both sides of a middle allocation and the neighbours join. */
    NativeMaliIovaAllocator merge(0x200000, 4 * kPage);
    uint64_t left = 0, middle = 0, right = 0;
    expect(merge.reserve(kPage, &left), "merge: left");
    expect(merge.reserve(kPage, &middle), "merge: middle");
    expect(merge.reserve(kPage, &right), "merge: right");
    expect(merge.free_range_count() == 1, "merge: one tail range before frees");
    expect(merge.free(left, kPage), "merge: free left");
    expect(merge.free(middle, kPage), "merge: free middle");
    expect(merge.free(right, kPage), "merge: free right");
    expect(merge.free_range_count() == 1, "merge: all three ranges coalesce");
    expect(merge.free_bytes() == 4 * kPage, "merge: whole range free again");
    expect(merge.allocated_bytes() == 0, "merge: nothing allocated");

    /* Quarantine: a range freed behind a fence is not reusable until that
     * exact fence completes. Fences are opaque, so other values do nothing. */
    NativeMaliIovaAllocator quarantined(0x300000, 2 * kPage);
    uint64_t guarded = 0;
    expect(quarantined.reserve(2 * kPage, &guarded), "quarantine: reserve both pages");
    expect(quarantined.free(guarded, 2 * kPage, /*guard_fence=*/0x44),
           "quarantine: free with guard");
    expect(quarantined.quarantine_count() == 1, "quarantine: one entry");
    expect(quarantined.quarantined_bytes() == 2 * kPage, "quarantine: bytes counted");
    expect(quarantined.free_bytes() == 0, "quarantine: nothing is free while guarded");

    uint64_t no_room = 0;
    expect(!quarantined.reserve(kPage, &no_room), "quarantine: range not reusable yet");

    expect(quarantined.release(0x45) == 0, "quarantine: another fence releases nothing");
    expect(!quarantined.reserve(kPage, &no_room), "quarantine: still not reusable");
    expect(quarantined.release(0x44) == 1, "quarantine: the guard releases it");
    expect(quarantined.quarantine_count() == 0, "quarantine: empty again");
    expect(quarantined.free_bytes() == 2 * kPage, "quarantine: both pages free");
    expect(quarantined.reserve(2 * kPage, &no_room), "quarantine: reusable after release");
    expect(no_room == guarded, "quarantine: the freed range comes back");

    /* Reset: release_all() frees everything still quarantined. */
    NativeMaliIovaAllocator reset(0x400000, 3 * kPage);
    uint64_t a = 0, b = 0;
    expect(reset.reserve(kPage, &a), "reset: reserve a");
    expect(reset.reserve(kPage, &b), "reset: reserve b");
    expect(reset.free(a, kPage, 0x100), "reset: quarantine a");
    expect(reset.free(b, kPage, 0x200), "reset: quarantine b");
    expect(reset.release_all() == 2, "reset: both released");
    expect(reset.quarantine_count() == 0, "reset: no quarantine left");
    expect(reset.free_bytes() == 3 * kPage, "reset: whole range free");
    uint64_t after_reset = 0;
    expect(reset.reserve(3 * kPage, &after_reset), "reset: one contiguous allocation fits");
    expect(after_reset == 0x400000, "reset: allocation starts at the base");

    /* Exhaustion is reported, not approximated. */
    NativeMaliIovaAllocator small(0x500000, kPage);
    uint64_t only = 0;
    expect(small.reserve(kPage, &only), "small: the one page fits");
    expect(!small.reserve(kPage, &ignored), "small: the next page is refused");
    expect(!small.reserve(kPage + kPage, &ignored), "small: oversized request refused");
    expect(small.largest_free_bytes() == 0, "small: nothing large is free");

    /* A range exactly as large as the request is taken, not skipped. */
    NativeMaliIovaAllocator exact(0x600000, kPage);
    uint64_t exact_iova = 0;
    expect(exact.reserve(kPage, &exact_iova), "exact fit accepted");

    if (failures != 0) {
        std::fprintf(stderr, "%d check(s) failed\n", failures);
        return 1;
    }
    std::printf("OK   iova allocator: first fit, merge, exact free, quarantine, reset\n");
    return 0;
}
