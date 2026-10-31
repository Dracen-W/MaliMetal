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

NativeMaliError NativeMaliClient::reset() {
    return call(NativeMaliMethodReset, nullptr, 0, nullptr, 0);
}
