/* SPDX-License-Identifier: GPL-2.0-only
 *
 * The userspace record of the buffers this client owns.
 *
 * The kernel hands out an opaque handle from BoAlloc; everything else about a
 * buffer (size, IOVA, permissions, CPU token) is kept here. The table is the
 * one place that decides whether an operation is legal for a buffer, so the
 * ABI calls later cannot disagree with each other.
 *
 * Fixed capacity and no heap: a compositor that allocates per frame must not
 * turn a GPU-out-of-memory into a userspace allocation failure, and a full
 * table is an explicit, testable refusal.
 */
#ifndef MALIMETAL_NATIVE_MALI_BO_TABLE_H
#define MALIMETAL_NATIVE_MALI_BO_TABLE_H

#include <stddef.h>
#include <stdint.h>

#include <array>

struct NativeMaliBo {
    uint64_t handle = 0;
    uint64_t bytes = 0;
    uint64_t iova = 0;       /* 0 = not mapped */
    uint32_t permissions = 0;
    uint32_t cpu_token = 0;  /* 0 = no CPU view */
    bool mapped = false;
    bool cpu_mapped = false;
};

class NativeMaliBoTable {
public:
    static constexpr size_t kCapacity = 4096;

    NativeMaliBoTable();

    /* Register a handle returned by BoAlloc. */
    bool add(uint64_t handle, uint64_t bytes);

    /* Record the mapping BoMap produced. Refuses a second map, a zero or
     * unaligned IOVA, and no permissions at all. */
    bool set_mapping(uint64_t handle, uint64_t iova, uint32_t permissions);

    /* Drop the mapping record; `iova` receives the range so the caller can free
     * or quarantine it. Refuses when the buffer is not mapped. */
    bool clear_mapping(uint64_t handle, uint64_t *iova);

    /* Record the token BoCpuMap returned, and drop it only when the token
     * matches the one that was handed out. */
    bool set_cpu_token(uint64_t handle, uint32_t token);
    bool clear_cpu_token(uint64_t handle, uint32_t token);

    /* Remove the buffer. Reports the mapping that must be freed, if any. */
    bool remove(uint64_t handle, uint64_t *iova);

    const NativeMaliBo *find(uint64_t handle) const;
    size_t count() const { return count_; }

private:
    static constexpr size_t kHashBuckets = 8192; /* power of 2, 2x kCapacity */
    static constexpr uint16_t kEmpty = 0xFFFFu;
    static constexpr uint16_t kTombstone = 0xFFFEu;

    static size_t hash_handle(uint64_t handle);
    size_t find_index(uint64_t handle) const;
    void hash_insert(uint64_t handle, uint16_t index);
    void hash_rebuild();
    NativeMaliBo *mutable_find(uint64_t handle);

    std::array<NativeMaliBo, kCapacity> entries_{};
    std::array<uint16_t, kHashBuckets> buckets_{};
    size_t count_ = 0;
    size_t tombstones_ = 0;
};

#endif /* MALIMETAL_NATIVE_MALI_BO_TABLE_H */
