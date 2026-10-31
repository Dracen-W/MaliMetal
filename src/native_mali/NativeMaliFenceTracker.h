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

    /* Start tracking a fence just returned by Submit. Zero, duplicates and a
     * full table are refused. */
    bool track(uint64_t fence);

    /* WaitFence succeeded: Pending -> Signaled. Refuses an unknown fence and
     * refuses to resurrect a Lost one. */
    bool resolve(uint64_t fence);

    /* Device reset or a fault counter change: every Pending fence becomes Lost.
     * Returns how many fences were lost. Signaled fences keep their outcome. */
    size_t lose_all();

    /* Drop a fence after its result has been consumed. */
    bool forget(uint64_t fence);

    NativeMaliFenceState state(uint64_t fence) const;

    size_t count() const { return count_; }
    size_t pending_count() const;

private:
    struct Entry {
        uint64_t fence = 0;
        NativeMaliFenceState state = NativeMaliFenceState::Unknown;
    };

    Entry *mutable_find(uint64_t fence);

    std::array<Entry, kCapacity> entries_{};
    size_t count_ = 0;
};

#endif /* MALIMETAL_NATIVE_MALI_FENCE_TRACKER_H */
