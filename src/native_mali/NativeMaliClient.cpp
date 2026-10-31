/* SPDX-License-Identifier: GPL-2.0-only
 *
 * Userspace half of the v1 NativeMali ABI. See NativeMaliClient.h.
 *
 * The record conventions (fixed-size structs with the arrays inside them, a
 * version field that must match, unknown values refused rather than ignored)
 * follow NativeMaliABI.h in Mali-G52, which in turn credits NativeAdrenoABI.h
 * (MIT, see Mali-G52/NOTICE). No record is defined here; they all come from the
 * pinned kernel header.
 */
#include "NativeMaliClient.h"

#include "NativeMaliAbiTable.h"

#if defined(__APPLE__)
#include <IOKit/IOReturn.h>
#endif

namespace {

/* The one limit the client can check without the kernel: the header's own. */
constexpr uint64_t kExpectedMaxBoBytes = UINT64_C(1) << NativeMaliAbiMaxBoBytesShift;

} /* namespace */

const char *NativeMaliErrorName(NativeMaliError error) {
    switch (error) {
        case NativeMaliError::None: return "none";
        case NativeMaliError::InvalidArgument: return "invalid-argument";
        case NativeMaliError::OutOfMemory: return "out-of-memory";
        case NativeMaliError::Busy: return "busy";
        case NativeMaliError::NotReady: return "not-ready";
        case NativeMaliError::Unsupported: return "unsupported";
        case NativeMaliError::Aborted: return "aborted";
        case NativeMaliError::DeviceLost: return "device-lost";
        case NativeMaliError::IoError: return "io-error";
        case NativeMaliError::Unknown: return "unknown";
    }
    return "unknown";
}

NativeMaliError NativeMaliErrorFromKernReturn(int code) {
    if (code == 0) {
        return NativeMaliError::None;
    }
#if defined(__APPLE__)
    switch (code) {
        case kIOReturnBadArgument: return NativeMaliError::InvalidArgument;
        case kIOReturnNoMemory: return NativeMaliError::OutOfMemory;
        case kIOReturnNoResources: return NativeMaliError::OutOfMemory;
        case kIOReturnBusy: return NativeMaliError::Busy;
        case kIOReturnNotReady: return NativeMaliError::NotReady;
        case kIOReturnUnsupported: return NativeMaliError::Unsupported;
        case kIOReturnAborted: return NativeMaliError::Aborted;
        case kIOReturnNotPermitted: return NativeMaliError::InvalidArgument;
        case kIOReturnNotPrivileged: return NativeMaliError::InvalidArgument;
        case kIOReturnError: return NativeMaliError::IoError;
        default: break;
    }
#endif
    return NativeMaliError::Unknown;
}

NativeMaliClient::NativeMaliClient(NativeMaliTransport &transport, uint32_t abi_version)
    : transport_(transport), abi_version_(abi_version) {}

NativeMaliError NativeMaliClient::call(uint32_t selector, const void *input,
                                       size_t input_bytes, void *output,
                                       size_t output_bytes) {
    const NativeMaliAbiMethodRecord *method = NativeMaliAbiFindMethod(selector);
    if (!NativeMaliAbiRecordSizesMatch(method, input_bytes, output_bytes)) {
        /* A programming error in this client, not a kernel refusal. */
        last_kern_return_ = 0;
        return NativeMaliError::InvalidArgument;
    }
    const int code = transport_.call(selector, input, input_bytes, output, output_bytes);
    last_kern_return_ = code;
    if (code != 0) {
        return NativeMaliErrorFromKernReturn(code);
    }
    return NativeMaliError::None;
}

NativeMaliError NativeMaliClient::query(NativeMaliAbiQuery *out) {
    if (out == nullptr) {
        return NativeMaliError::InvalidArgument;
    }
    NativeMaliAbiQuery request{};
    request.abi_version = abi_version_;
    NativeMaliAbiQuery reply{};

    const NativeMaliError error =
        call(NativeMaliMethodQuery, &request, sizeof(request), &reply, sizeof(reply));
    if (error != NativeMaliError::None) {
        return error;
    }
    if (reply.abi_version != abi_version_) {
        return NativeMaliError::Unsupported;
    }
    if (reply.max_bo_bytes != kExpectedMaxBoBytes) {
        /* The driver and this header disagree about the one buffer limit that
         * is part of the ABI. Do not adopt either value; refuse. */
        return NativeMaliError::Unsupported;
    }
    *out = reply;
    return NativeMaliError::None;
}

NativeMaliError NativeMaliClient::device_info(NativeMaliAbiDeviceInfo *out) {
    if (out == nullptr) {
        return NativeMaliError::InvalidArgument;
    }
    NativeMaliAbiDeviceInfo reply{};
    const NativeMaliError error = call(NativeMaliMethodGetDeviceInfo, nullptr, 0, &reply,
                                       sizeof(reply));
    if (error != NativeMaliError::None) {
        return error;
    }
    *out = reply;
    return NativeMaliError::None;
}

NativeMaliError NativeMaliClient::status(NativeMaliAbiStatus *out) {
    if (out == nullptr) {
        return NativeMaliError::InvalidArgument;
    }
    NativeMaliAbiStatus reply{};
    const NativeMaliError error =
        call(NativeMaliMethodGetStatus, nullptr, 0, &reply, sizeof(reply));
    if (error != NativeMaliError::None) {
        return error;
    }
    *out = reply;
    return NativeMaliError::None;
}

NativeMaliError NativeMaliClient::bo_alloc(uint64_t bytes, uint64_t *handle) {
    if (handle == nullptr || bytes == 0 || (bytes % NativeMaliAbiPageBytes) != 0 ||
        bytes > NATIVE_MALI_ABI_MAX_BO_BYTES) {
        return NativeMaliError::InvalidArgument;
    }
    NativeMaliAbiBoAlloc request{};
    request.bytes = bytes;
    NativeMaliAbiBoAllocReply reply{};
    const NativeMaliError error =
        call(NativeMaliMethodBoAlloc, &request, sizeof(request), &reply, sizeof(reply));
    if (error != NativeMaliError::None) {
        return error;
    }
    if (reply.handle == 0) {
        return NativeMaliError::Unsupported; /* a handle of zero is not a handle */
    }
    *handle = reply.handle;
    return NativeMaliError::None;
}

NativeMaliError NativeMaliClient::bo_map(uint64_t handle, uint64_t iova, uint32_t permissions,
                                         uint64_t *mapped_iova) {
    if (mapped_iova == nullptr || handle == 0) {
        return NativeMaliError::InvalidArgument;
    }
    if (iova == 0 || (iova % NativeMaliAbiPageBytes) != 0) {
        return NativeMaliError::InvalidArgument;
    }
    if ((permissions & NativeMaliBoRead) == 0) {
        /* NativeMaliABI.h: panfrost always maps IOMMU_READ. */
        return NativeMaliError::InvalidArgument;
    }
    if ((permissions & ~(NativeMaliBoRead | NativeMaliBoWrite)) != 0) {
        return NativeMaliError::InvalidArgument;
    }
    NativeMaliAbiBoMap request{};
    request.handle = handle;
    request.iova = iova;
    request.permissions = permissions;
    NativeMaliAbiBoMapReply reply{};
    const NativeMaliError error =
        call(NativeMaliMethodBoMap, &request, sizeof(request), &reply, sizeof(reply));
    if (error != NativeMaliError::None) {
        return error;
    }
    if (reply.iova != iova) {
        /* The kernel must map where it was asked; anything else would make the
         * caller's page tables and the driver's disagree. */
        return NativeMaliError::Unsupported;
    }
    *mapped_iova = reply.iova;
    return NativeMaliError::None;
}

NativeMaliError NativeMaliClient::bo_free(uint64_t handle) {
    if (handle == 0) {
        return NativeMaliError::InvalidArgument;
    }
    NativeMaliAbiBoFree request{};
    request.handle = handle;
    return call(NativeMaliMethodBoFree, &request, sizeof(request), nullptr, 0);
}

NativeMaliError NativeMaliClient::bo_cpu_map(uint64_t handle, uint32_t *token, uint64_t *bytes) {
    if (handle == 0 || token == nullptr || bytes == nullptr) {
        return NativeMaliError::InvalidArgument;
    }
    NativeMaliAbiBoCpuMap request{};
    request.handle = handle;
    NativeMaliAbiBoCpuMapReply reply{};
    const NativeMaliError error =
        call(NativeMaliMethodBoCpuMap, &request, sizeof(request), &reply, sizeof(reply));
    if (error != NativeMaliError::None) {
        return error;
    }
    if (reply.token == 0 || reply.bytes == 0) {
        return NativeMaliError::Unsupported;
    }
    *token = reply.token;
    *bytes = reply.bytes;
    return NativeMaliError::None;
}

NativeMaliError NativeMaliClient::bo_cpu_unmap(uint64_t handle, uint32_t token) {
    if (handle == 0 || token == 0) {
        return NativeMaliError::InvalidArgument;
    }
    NativeMaliAbiBoCpuUnmap request{};
    request.handle = handle;
    request.token = token;
    return call(NativeMaliMethodBoCpuUnmap, &request, sizeof(request), nullptr, 0);
}

NativeMaliError NativeMaliClient::submit(uint64_t job_chain, uint64_t wait_fence,
                                         uint64_t wait_timeout_ns, uint64_t *fence) {
    if (fence == nullptr || job_chain == 0) {
        return NativeMaliError::InvalidArgument;
    }
    if ((job_chain % NativeMaliAbiJobChainAlign) != 0) {
        return NativeMaliError::InvalidArgument;
    }
    NativeMaliAbiSubmit request{};
    request.job_chain = job_chain;
    request.wait_fence = wait_fence;
    request.wait_timeout_ns = wait_fence == 0 ? 0 : wait_timeout_ns;
    NativeMaliAbiSubmitReply reply{};
    const NativeMaliError error =
        call(NativeMaliMethodSubmit, &request, sizeof(request), &reply, sizeof(reply));
    if (error != NativeMaliError::None) {
        return error;
    }
    if (reply.fence == 0) {
        /* The ABI says a fence is nonzero. A zero would be indistinguishable
         * from "no wait" and from "unset". */
        return NativeMaliError::Unsupported;
    }
    *fence = reply.fence;
    return NativeMaliError::None;
}

NativeMaliError NativeMaliClient::wait_fence(uint64_t fence, uint64_t timeout_ns) {
    if (fence == 0) {
        return NativeMaliError::InvalidArgument;
    }
    NativeMaliAbiWaitFence request{};
    request.fence = fence;
    request.timeout_ns = timeout_ns;
    return call(NativeMaliMethodWaitFence, &request, sizeof(request), nullptr, 0);
}

NativeMaliError NativeMaliClient::reset() {
    return call(NativeMaliMethodReset, nullptr, 0, nullptr, 0);
}
