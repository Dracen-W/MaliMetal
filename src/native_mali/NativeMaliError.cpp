/* SPDX-License-Identifier: GPL-2.0-only
 *
 * Error mapping for the NativeMali ABI client. See NativeMaliError.h.
 */
#include "NativeMaliError.h"

#if defined(__APPLE__)
#include <IOKit/IOReturn.h>
#endif

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
