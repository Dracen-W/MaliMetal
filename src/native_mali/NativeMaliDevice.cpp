/* SPDX-License-Identifier: GPL-2.0-only
 *
 * Buffer lifecycle for the NativeMali ABI. See NativeMaliDevice.h.
 */
#include "NativeMaliDevice.h"

NativeMaliDevice::NativeMaliDevice(NativeMaliClient &client, NativeMaliBoTable &buffers,
                                   NativeMaliIovaAllocator &iova,
                                   NativeMaliFenceTracker &fences)
    : client_(client), buffers_(buffers), iova_(iova), fences_(fences) {}

NativeMaliError NativeMaliDevice::alloc(uint64_t bytes, uint64_t *handle) {
    uint64_t allocated = 0;
    const NativeMaliError error = client_.bo_alloc(bytes, &allocated);
    if (error != NativeMaliError::None) {
        return error;
    }
    if (!buffers_.add(allocated, bytes)) {
        /* The kernel now owns a buffer we cannot record. Give it back rather
         * than leak it; if that also fails there is nothing honest left to do
         * except report the failure. */
        const NativeMaliError rollback = client_.bo_free(allocated);
        return rollback == NativeMaliError::None ? NativeMaliError::Unsupported : rollback;
    }
    *handle = allocated;
    return NativeMaliError::None;
}

NativeMaliError NativeMaliDevice::map(uint64_t handle, uint32_t permissions, uint64_t *iova) {
    const NativeMaliBo *buffer = buffers_.find(handle);
    if (buffer == nullptr || buffer->mapped || iova == nullptr) {
        return NativeMaliError::InvalidArgument;
    }
    const uint64_t bytes = buffer->bytes;

    uint64_t reserved = 0;
    if (!iova_.reserve(bytes, &reserved)) {
        return NativeMaliError::OutOfMemory;
    }

    uint64_t mapped = 0;
    const NativeMaliError error = client_.bo_map(handle, reserved, permissions, &mapped);
    if (error != NativeMaliError::None) {
        iova_.free(reserved, bytes);
        return error;
    }
    if (!buffers_.set_mapping(handle, mapped, permissions)) {
        /* The kernel has a mapping we could not record. Releasing the address
         * would let it be reused under that mapping, so keep it reserved and
         * report the failure; the buffer must be freed to clear it. */
        return NativeMaliError::Unsupported;
    }
    *iova = mapped;
    return NativeMaliError::None;
}

NativeMaliError NativeMaliDevice::free(uint64_t handle, uint64_t guard_fence) {
    const NativeMaliBo *buffer = buffers_.find(handle);
    if (buffer == nullptr || handle == 0) {
        return NativeMaliError::InvalidArgument;
    }
    const uint64_t bytes = buffer->bytes;
    const bool mapped = buffer->mapped;
    const uint64_t iova = buffer->iova;

    const NativeMaliError error = client_.bo_free(handle);
    if (error != NativeMaliError::None) {
        /* The buffer still exists in the kernel; keep the record so the state
         * stays truthful. */
        return error;
    }

    uint64_t removed_iova = 0;
    if (!buffers_.remove(handle, &removed_iova)) {
        return NativeMaliError::Unsupported;
    }
    if (mapped) {
        if (!iova_.free(iova, bytes, guard_fence)) {
            return NativeMaliError::Unsupported;
        }
    }
    return NativeMaliError::None;
}

NativeMaliError NativeMaliDevice::cpu_map(uint64_t handle, uint32_t *token, uint64_t *bytes) {
    uint32_t mapped_token = 0;
    uint64_t mapped_bytes = 0;
    const NativeMaliError error = client_.bo_cpu_map(handle, &mapped_token, &mapped_bytes);
    if (error != NativeMaliError::None) {
        return error;
    }
    if (!buffers_.set_cpu_token(handle, mapped_token)) {
        /* The record already has a CPU view (or the handle vanished); drop the
         * one the kernel just made rather than leaving it untracked. */
        const NativeMaliError rollback = client_.bo_cpu_unmap(handle, mapped_token);
        return rollback == NativeMaliError::None ? NativeMaliError::InvalidArgument : rollback;
    }
    *token = mapped_token;
    *bytes = mapped_bytes;
    return NativeMaliError::None;
}

NativeMaliError NativeMaliDevice::cpu_unmap(uint64_t handle, uint32_t token) {
    const NativeMaliBo *buffer = buffers_.find(handle);
    if (buffer == nullptr || !buffer->cpu_mapped || buffer->cpu_token != token) {
        return NativeMaliError::InvalidArgument;
    }
    const NativeMaliError error = client_.bo_cpu_unmap(handle, token);
    if (error != NativeMaliError::None) {
        return error;
    }
    if (!buffers_.clear_cpu_token(handle, token)) {
        return NativeMaliError::Unsupported;
    }
    return NativeMaliError::None;
}

NativeMaliError NativeMaliDevice::submit(uint64_t job_chain, uint64_t wait_fence,
                                         uint64_t wait_timeout_ns, uint64_t *fence) {
    uint64_t submitted = 0;
    const NativeMaliError error =
        client_.submit(job_chain, wait_fence, wait_timeout_ns, &submitted);
    if (error != NativeMaliError::None) {
        return error;
    }
    if (!fences_.track(submitted)) {
        /* The job is already in the kernel but cannot be tracked. Reporting
         * success would let a later wait on it be skipped. */
        return NativeMaliError::Unsupported;
    }
    *fence = submitted;
    return NativeMaliError::None;
}

NativeMaliError NativeMaliDevice::wait(uint64_t fence, uint64_t timeout_ns) {
    if (fences_.state(fence) == NativeMaliFenceState::Unknown) {
        return NativeMaliError::InvalidArgument;
    }
    const NativeMaliError error = client_.wait_fence(fence, timeout_ns);
    if (error != NativeMaliError::None) {
        return error;
    }
    if (!fences_.resolve(fence)) {
        /* WaitFence succeeded for a fence this client should not be waiting
         * on, or one a reset already lost. Do not pretend it completed. */
        return NativeMaliError::DeviceLost;
    }
    iova_.release(fence);
    return NativeMaliError::None;
}

NativeMaliError NativeMaliDevice::reset() {
    const NativeMaliError error = client_.reset();
    if (error != NativeMaliError::None) {
        return error;
    }
    fences_.lose_all();
    iova_.release_all();
    return NativeMaliError::None;
}
