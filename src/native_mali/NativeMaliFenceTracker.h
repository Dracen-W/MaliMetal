/* SPDX-License-Identifier: GPL-2.0-only
 *
 * The userspace record of the fences this client is waiting on.
 *
 * A NativeMali fence is opaque (NativeMaliABI.h): nonzero, produced by Submit,
 * consumed by WaitFence. It cannot be compared, decoded or ordered, so this
 * tracker only records identity and outcome. That is deliberate: a fence that
 * "looks older" is not evidence that the GPU finished it.
 *
 * After a reset every pending fence is Lost, never Signaled. A fence that was
 * dropped by the kernel must fail the work that waited on it; reporting it as
 * complete would corrupt whatever reads that buffer next.
 */
#ifndef MALIMETAL_NATIVE_MALI_FENCE_TRACKER_H
#define MALIMETAL_NATIVE_MALI_FENCE_TRACKER_H

#include <stddef.h>
#include <stdint.h>

#include <array>

enum class NativeMaliFenceState {
    Unknown = 0, /* never tracked, or already forgotten */
    Pending,
    Signaled,
    Lost,
};

class NativeMaliFenceTracker {
public:
    static constexpr size_t kCapacity = 4096;

    NativeMaliFenceTracker();

    /* Start tracking a fence just returned by Submit, with an optional
     * dependency fence that must complete before it. Zero, duplicates and a
     * full table are refused. */
    bool track(uint64_t fence, uint64_t dep_fence = 0);

    /* WaitFence succeeded: Pending -> Signaled. Refuses an unknown fence and
     * refuses to resurrect a Lost one. */
    bool resolve(uint64_t fence);

    /* WaitFence succeeded: resolve `fence` (Pending -> Signaled) and walk any
     * recorded dependency chain, marking pending ancestors Signaled as well.
     * Writes resolved fence IDs into `resolved_out` (up to `max_out`) and
     * returns the number of fences transitioned to Signaled (0 if `fence` was
     * not Pending). */
    size_t resolve_chain(uint64_t fence, uint64_t *resolved_out, size_t max_out);

    /* Device reset or a fault counter change: every Pending fence becomes Lost.
     * Returns how many fences were lost. Signaled fences keep their outcome. */
    size_t lose_all();

    /* Drop a fence after its result has been consumed. */
    bool forget(uint64_t fence);

    /* Evict the oldest Signaled (or, if none Signaled, oldest Lost) fence to
     * free one slot without ever dropping a Pending fence. */
    bool evict_oldest_finished();

    /* Return the oldest Pending fence (smallest insertion order), or 0 if none. */
    uint64_t oldest_pending() const;

    NativeMaliFenceState state(uint64_t fence) const;

    size_t count() const { return count_; }
    size_t pending_count() const { return pending_count_; }

private:
    static constexpr size_t kHashBuckets = 8192; /* power of 2, 2x kCapacity */
    static constexpr uint16_t kEmpty = 0xFFFFu;
    static constexpr uint16_t kTombstone = 0xFFFEu;

    struct Entry {
        uint64_t fence = 0;
        uint64_t dep_fence = 0;
        uint64_t order = 0;
        NativeMaliFenceState state = NativeMaliFenceState::Unknown;
    };

    static size_t hash_fence(uint64_t fence);
    size_t find_index(uint64_t fence) const;
    void hash_insert(uint64_t fence, uint16_t index);
    void hash_rebuild();
    bool forget_at(size_t index);

    std::array<Entry, kCapacity> entries_{};
    std::array<uint16_t, kHashBuckets> buckets_{};
    size_t count_ = 0;
    size_t pending_count_ = 0;
    size_t tombstones_ = 0;
    uint64_t next_order_ = 0;
};

#endif /* MALIMETAL_NATIVE_MALI_FENCE_TRACKER_H */
