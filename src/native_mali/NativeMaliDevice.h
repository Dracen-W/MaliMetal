/* SPDX-License-Identifier: GPL-2.0-only
 *
 * The buffer lifecycle: one place that keeps the client, the buffer records,
 * the IOVA allocator and the fence tracker consistent with each other.
 *
 * Each operation is ordered so a failure leaves the earlier layers as they
 * were:
 *   - alloc: ask the kernel first, then record; roll the allocation back if the
 *     record is refused;
 *   - map: reserve an address, ask the kernel, then record; release the
 *     address if the kernel or the record refuses;
 *   - free: ask the kernel first, then drop the record and release the address;
 *     a guarded free quarantines the address until that fence completes;
 *   - wait: only a successful WaitFence marks a fence Signaled and releases its
 *     quarantined ranges. A failed wait leaves both alone.
 *   - reset: the ABI says buffers and mappings survive, so only fences are
 *     lost and quarantined ranges are released.
 */
#ifndef MALIMETAL_NATIVE_MALI_DEVICE_H
#define MALIMETAL_NATIVE_MALI_DEVICE_H

#include <stdint.h>

#include "NativeMaliBoTable.h"
#include "NativeMaliClient.h"
#include "NativeMaliFenceTracker.h"
#include "NativeMaliIovaAllocator.h"

class NativeMaliDevice {
public:
    NativeMaliDevice(NativeMaliClient &client, NativeMaliBoTable &buffers,
                     NativeMaliIovaAllocator &iova, NativeMaliFenceTracker &fences);

    NativeMaliError alloc(uint64_t bytes, uint64_t *handle);

    /* Reserve an IOVA for the buffer and map it. `permissions` must include
     * NativeMaliBoRead. */
    NativeMaliError map(uint64_t handle, uint32_t permissions, uint64_t *iova);

    /* Free the buffer. `guard_fence` is zero when no submitted job can still
     * reference it, otherwise the last fence that may: the IOVA is quarantined
     * until that fence completes. */
    NativeMaliError free(uint64_t handle, uint64_t guard_fence);

    NativeMaliError cpu_map(uint64_t handle, uint32_t *token, uint64_t *bytes);
    NativeMaliError cpu_unmap(uint64_t handle, uint32_t token);

    NativeMaliError submit(uint64_t job_chain, uint64_t wait_fence, uint64_t wait_timeout_ns,
                           uint64_t *fence);

    /* Wait for one fence. On success the fence becomes Signaled and its
     * quarantined ranges return to the allocator. */
    NativeMaliError wait(uint64_t fence, uint64_t timeout_ns);

    /* Read GetStatus and compare it with the previous reading. Any change in
     * `faults` or `resets` means in-flight work is gone: pending fences become
     * Lost, quarantined IOVAs are released, and DeviceLost is reported. The
     * first call only records a baseline. */
    NativeMaliError refresh_status();

    /* True once a fault or reset has been observed, until reset() succeeds. */
    bool device_lost() const { return device_lost_; }

    /* Device-wide recovery: every pending fence is Lost, quarantined IOVAs are
     * released, buffers and mappings stay valid (NativeMaliABI.h, Reset). */
    NativeMaliError reset();

private:
    NativeMaliClient &client_;
    NativeMaliBoTable &buffers_;
    NativeMaliIovaAllocator &iova_;
    NativeMaliFenceTracker &fences_;
    NativeMaliAbiStatus last_status_{};
    bool status_valid_ = false;
    bool device_lost_ = false;
};

#endif /* MALIMETAL_NATIVE_MALI_DEVICE_H */
