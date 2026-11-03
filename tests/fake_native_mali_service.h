/* SPDX-License-Identifier: GPL-2.0-only
 *
 * A scripted NativeMali service for host tests.
 *
 * It answers the same records as the kernel's NativeMaliUserClient and applies
 * the rules the driver applies, but from a table in memory instead of hardware:
 * handles must exist, a buffer maps once, mapped ranges must not overlap, a job
 * chain must lie inside a mapped buffer, a fence belongs to the client that
 * submitted it, and a reset drops in-flight fences while keeping buffers.
 *
 * This is a test double, not a model of the GPU. It exists so the userspace
 * lifecycle (and later the pan_kmod backend) can be exercised without a phone,
 * and so a mistake in the client's record handling fails here rather than in a
 * kernel driver.
 */
#ifndef MALIMETAL_TEST_FAKE_NATIVE_MALI_SERVICE_H
#define MALIMETAL_TEST_FAKE_NATIVE_MALI_SERVICE_H

#include "NativeMaliTransport.h"

#include "NativeMaliABI.h"

#if defined(__APPLE__)
#include <IOKit/IOReturn.h>
#endif

#include <array>
#include <cstring>

struct FakeNativeMaliBuffer {
    uint64_t handle = 0;
    uint64_t bytes = 0;
    uint64_t iova = 0;
    uint32_t permissions = 0;
    uint32_t cpu_token = 0;
    bool mapped = false;
    bool cpu_mapped = false;
    bool live = false;
};

struct FakeNativeMaliFence {
    uint64_t fence = 0;
    bool signaled = false;
    bool live = false;
};

class FakeNativeMaliService final : public NativeMaliTransport {
public:
    static constexpr size_t kBufferCapacity = 64;
    static constexpr size_t kFenceCapacity = 256;

    /* Counters the test can move, standing in for the kernel's. */
    uint64_t faults = 0;
    uint64_t resets = 0;

    /* Optional failure injection for WaitFence: when non-zero, a wait on this
     * exact fence returns the code and leaves the fence pending. */
    int wait_failure_code = 0;
    uint64_t wait_failure_fence = 0;

    ~FakeNativeMaliService() override = default;

    size_t live_buffers() const {
        size_t count = 0;
        for (const auto &buffer : buffers_) {
            count += buffer.live ? 1 : 0;
        }
        return count;
    }

    size_t pending_fences() const {
        size_t count = 0;
        for (const auto &fence : fences_) {
            count += (fence.live && !fence.signaled) ? 1 : 0;
        }
        return count;
    }

    const FakeNativeMaliBuffer *find_buffer(uint64_t handle) const {
        for (const auto &buffer : buffers_) {
            if (buffer.live && buffer.handle == handle) {
                return &buffer;
            }
        }
        return nullptr;
    }

    int call(uint32_t selector, const void *input, size_t input_bytes, void *output,
             size_t output_bytes) override {
        switch (selector) {
            case NativeMaliMethodQuery:
                if (!sizes(input, input_bytes, sizeof(NativeMaliAbiQuery)) ||
                    !sizes(output, output_bytes, sizeof(NativeMaliAbiQuery))) {
                    return kIOReturnBadArgument;
                }
                return query(static_cast<const NativeMaliAbiQuery *>(input),
                             static_cast<NativeMaliAbiQuery *>(output));
            case NativeMaliMethodGetDeviceInfo:
                if (!sizes(input, input_bytes, 0) ||
                    !sizes(output, output_bytes, sizeof(NativeMaliAbiDeviceInfo))) {
                    return kIOReturnBadArgument;
                }
                return device_info(static_cast<NativeMaliAbiDeviceInfo *>(output));
            case NativeMaliMethodBoAlloc:
                if (!sizes(input, input_bytes, sizeof(NativeMaliAbiBoAlloc)) ||
                    !sizes(output, output_bytes, sizeof(NativeMaliAbiBoAllocReply))) {
                    return kIOReturnBadArgument;
                }
                return bo_alloc(static_cast<const NativeMaliAbiBoAlloc *>(input),
                                static_cast<NativeMaliAbiBoAllocReply *>(output));
            case NativeMaliMethodBoMap:
                if (!sizes(input, input_bytes, sizeof(NativeMaliAbiBoMap)) ||
                    !sizes(output, output_bytes, sizeof(NativeMaliAbiBoMapReply))) {
                    return kIOReturnBadArgument;
                }
                return bo_map(static_cast<const NativeMaliAbiBoMap *>(input),
                              static_cast<NativeMaliAbiBoMapReply *>(output));
            case NativeMaliMethodBoFree:
                if (!sizes(input, input_bytes, sizeof(NativeMaliAbiBoFree)) ||
                    !sizes(output, output_bytes, 0)) {
                    return kIOReturnBadArgument;
                }
                return bo_free(static_cast<const NativeMaliAbiBoFree *>(input));
            case NativeMaliMethodBoCpuMap:
                if (!sizes(input, input_bytes, sizeof(NativeMaliAbiBoCpuMap)) ||
                    !sizes(output, output_bytes, sizeof(NativeMaliAbiBoCpuMapReply))) {
                    return kIOReturnBadArgument;
                }
                return bo_cpu_map(static_cast<const NativeMaliAbiBoCpuMap *>(input),
                                  static_cast<NativeMaliAbiBoCpuMapReply *>(output));
            case NativeMaliMethodBoCpuUnmap:
                if (!sizes(input, input_bytes, sizeof(NativeMaliAbiBoCpuUnmap)) ||
                    !sizes(output, output_bytes, 0)) {
                    return kIOReturnBadArgument;
                }
                return bo_cpu_unmap(static_cast<const NativeMaliAbiBoCpuUnmap *>(input));
            case NativeMaliMethodSubmit:
                if (!sizes(input, input_bytes, sizeof(NativeMaliAbiSubmit)) ||
                    !sizes(output, output_bytes, sizeof(NativeMaliAbiSubmitReply))) {
                    return kIOReturnBadArgument;
                }
                return submit(static_cast<const NativeMaliAbiSubmit *>(input),
                              static_cast<NativeMaliAbiSubmitReply *>(output));
            case NativeMaliMethodWaitFence:
                if (!sizes(input, input_bytes, sizeof(NativeMaliAbiWaitFence)) ||
                    !sizes(output, output_bytes, 0)) {
                    return kIOReturnBadArgument;
                }
                return wait(static_cast<const NativeMaliAbiWaitFence *>(input));
            case NativeMaliMethodGetStatus:
                if (!sizes(input, input_bytes, 0) ||
                    !sizes(output, output_bytes, sizeof(NativeMaliAbiStatus))) {
                    return kIOReturnBadArgument;
                }
                status(static_cast<NativeMaliAbiStatus *>(output));
                return kIOReturnSuccess;
            case NativeMaliMethodReset:
                if (!sizes(input, input_bytes, 0) || !sizes(output, output_bytes, 0)) {
                    return kIOReturnBadArgument;
                }
                return reset();
            default:
                return kIOReturnUnsupported;
        }
    }

private:
    static bool sizes(const void *pointer, size_t actual, size_t expected) {
        if (expected == 0) {
            return actual == 0;
        }
        return pointer != nullptr && actual == expected;
    }

    FakeNativeMaliBuffer *mutable_buffer(uint64_t handle) {
        for (auto &buffer : buffers_) {
            if (buffer.live && buffer.handle == handle) {
                return &buffer;
            }
        }
        return nullptr;
    }

    int query(const NativeMaliAbiQuery *request, NativeMaliAbiQuery *reply) {
        if (request->abi_version != NativeMaliAbiVersion || request->flags != 0) {
            return kIOReturnBadArgument;
        }
        reply->abi_version = NativeMaliAbiVersion;
        reply->input_bits = 48;
        reply->output_bits = 40;
        reply->flags = 0;
        reply->max_bo_bytes = UINT64_C(1) << NativeMaliAbiMaxBoBytesShift;
        return kIOReturnSuccess;
    }

    int device_info(NativeMaliAbiDeviceInfo *reply) {
        *reply = NativeMaliAbiDeviceInfo{};
        /* Mali-G52 r1 (product 0x7402), revision 0x1000 = r1p0.
         *
         * Mesa's pan_get_model() decodes gpu_id as
         * PAN_PROD_ID(arch_major = bits 31:28, arch_minor = bits 27:24,
         * product_major = bits 19:16). 0x74021000 -> (7,4,2) = "G52 r1".
         * The earlier 0x70021000 decodes to (7,0,2), which has no model entry,
         * so panvk_physical_device_init() fails with "Unknown gpu_id" and the
         * instance enumerates zero devices. Confirm the measured value against
         * Mali-G52 docs/DEVICE.md. */
        reply->gpu_id = 0x74021000;
        reply->shader_present_lo = 0x1f;
        reply->tiler_present_lo = 0x3;
        reply->thread_max_threads = 0x100;
        reply->max_workgroup_size = 0x200;
        reply->max_barrier_size = 0x300;
        reply->l2_features = 0x11;
        reply->core_features = 0x22;
        reply->tiler_features = 0x33;
        reply->flags = NativeMaliDeviceNonCoherent;
        reply->va_start = 0;
        reply->va_size = UINT64_C(1) << 48;
        reply->mem_features = 0x00000007;
        reply->mmu_features = (40u << 8) | 48u;
        reply->afbc_features = 0x0000000F;
        reply->texture_features[0] = 0x11110000;
        reply->texture_features[1] = 0x22220000;
        reply->texture_features[2] = 0x33330000;
        reply->texture_features[3] = 0x44440000;
        reply->max_tasks_per_core = 2;
        reply->registers_per_core = 96;
        reply->tls_instances_per_core = 64;
        reply->pgsize_shift = NativeMaliAbiPageShift;
        reply->vm_op_flags = NativeMaliAbiVmOpMap | NativeMaliAbiVmOpUnmap;
        return kIOReturnSuccess;
    }

    int bo_alloc(const NativeMaliAbiBoAlloc *request, NativeMaliAbiBoAllocReply *reply) {
        if (request->bytes == 0 || (request->bytes % NativeMaliAbiPageBytes) != 0 ||
            request->bytes > (UINT64_C(1) << NativeMaliAbiMaxBoBytesShift)) {
            return kIOReturnBadArgument;
        }
        for (auto &buffer : buffers_) {
            if (buffer.live) {
                continue;
            }
            buffer = FakeNativeMaliBuffer{};
            buffer.live = true;
            buffer.handle = next_handle_++;
            buffer.bytes = request->bytes;
            reply->handle = buffer.handle;
            return kIOReturnSuccess;
        }
        return kIOReturnNoMemory;
    }

    int bo_map(const NativeMaliAbiBoMap *request, NativeMaliAbiBoMapReply *reply) {
        FakeNativeMaliBuffer *buffer = mutable_buffer(request->handle);
        if (buffer == nullptr || buffer->mapped) {
            return kIOReturnBadArgument;
        }
        if (request->iova == 0 || (request->iova % NativeMaliAbiPageBytes) != 0 ||
            (request->permissions & NativeMaliBoRead) == 0 ||
            (request->permissions & ~(NativeMaliBoRead | NativeMaliBoWrite)) != 0 ||
            request->reserved != 0) {
            return kIOReturnBadArgument;
        }
        const uint64_t end = request->iova + buffer->bytes;
        if (end < request->iova) {
            return kIOReturnBadArgument;
        }
        for (const auto &other : buffers_) {
            if (!other.live || !other.mapped || other.handle == buffer->handle) {
                continue;
            }
            const uint64_t other_end = other.iova + other.bytes;
            if (request->iova < other_end && other.iova < end) {
                return kIOReturnBadArgument; /* the driver refuses overlapping maps */
            }
        }
        buffer->mapped = true;
        buffer->iova = request->iova;
        buffer->permissions = request->permissions;
        reply->iova = request->iova;
        return kIOReturnSuccess;
    }

    int bo_free(const NativeMaliAbiBoFree *request) {
        FakeNativeMaliBuffer *buffer = mutable_buffer(request->handle);
        if (buffer == nullptr) {
            return kIOReturnBadArgument;
        }
        *buffer = FakeNativeMaliBuffer{};
        return kIOReturnSuccess;
    }

    int bo_cpu_map(const NativeMaliAbiBoCpuMap *request, NativeMaliAbiBoCpuMapReply *reply) {
        FakeNativeMaliBuffer *buffer = mutable_buffer(request->handle);
        if (buffer == nullptr || buffer->cpu_mapped) {
            return kIOReturnBadArgument;
        }
        buffer->cpu_mapped = true;
        buffer->cpu_token = next_token_++;
        reply->token = buffer->cpu_token;
        reply->reserved = 0;
        reply->bytes = buffer->bytes;
        return kIOReturnSuccess;
    }

    int bo_cpu_unmap(const NativeMaliAbiBoCpuUnmap *request) {
        FakeNativeMaliBuffer *buffer = mutable_buffer(request->handle);
        if (buffer == nullptr || !buffer->cpu_mapped || buffer->cpu_token != request->token ||
            request->reserved != 0) {
            return kIOReturnBadArgument;
        }
        buffer->cpu_mapped = false;
        buffer->cpu_token = 0;
        return kIOReturnSuccess;
    }

    int submit(const NativeMaliAbiSubmit *request, NativeMaliAbiSubmitReply *reply) {
        if (request->job_chain == 0 ||
            (request->job_chain % NativeMaliAbiJobChainAlign) != 0 ||
            request->flags != 0 || request->reserved != 0) {
            return kIOReturnBadArgument;
        }
        if (request->wait_fence != 0 && !known_fence(request->wait_fence)) {
            return kIOReturnBadArgument;
        }
        /* The driver validates the chain's addresses against this client's
         * mappings; the chain must start inside a mapped buffer. */
        bool inside = false;
        for (const auto &buffer : buffers_) {
            if (buffer.live && buffer.mapped && request->job_chain >= buffer.iova &&
                request->job_chain < buffer.iova + buffer.bytes) {
                inside = true;
                break;
            }
        }
        if (!inside) {
            return kIOReturnBadArgument;
        }
        for (auto &fence : fences_) {
            if (!fence.live) {
                fence.live = true;
                fence.signaled = false;
                fence.fence = next_fence_++;
                reply->fence = fence.fence;
                return kIOReturnSuccess;
            }
        }
        return kIOReturnNoMemory;
    }

    bool known_fence(uint64_t value) const {
        for (const auto &fence : fences_) {
            if (fence.live && fence.fence == value) {
                return true;
            }
        }
        return false;
    }

    int wait(const NativeMaliAbiWaitFence *request) {
        if (wait_failure_code != 0 && request->fence == wait_failure_fence) {
            return wait_failure_code;
        }
        for (auto &fence : fences_) {
            if (fence.live && fence.fence == request->fence) {
                fence.signaled = true;
                return kIOReturnSuccess;
            }
        }
        return kIOReturnBadArgument;
    }

    void status(NativeMaliAbiStatus *reply) {
        reply->faults = faults;
        reply->resets = resets;
    }

    int reset() {
        for (auto &fence : fences_) {
            fence = FakeNativeMaliFence{}; /* in-flight work is gone */
        }
        ++resets;
        /* Buffers and mappings stay valid: the driver reprograms the page
         * tables from its software state. */
        return kIOReturnSuccess;
    }

    std::array<FakeNativeMaliBuffer, kBufferCapacity> buffers_{};
    std::array<FakeNativeMaliFence, kFenceCapacity> fences_{};
    uint64_t next_handle_ = 0x1000;
    uint64_t next_fence_ = 0x2000;
    uint32_t next_token_ = 0x3000;
};

#endif /* MALIMETAL_TEST_FAKE_NATIVE_MALI_SERVICE_H */
