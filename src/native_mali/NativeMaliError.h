/* SPDX-License-Identifier: GPL-2.0-only
 *
 * The error vocabulary of the NativeMali ABI client.
 *
 * It lives on its own so the transport, the client, the lifecycle and the
 * future backend all agree on one enum instead of each inventing a mapping.
 * The kernel's kIOReturn* values enter through NativeMaliErrorFromKernReturn();
 * Vulkan's VkResult leaves through the backend, which owns that mapping.
 */
#ifndef MALIMETAL_NATIVE_MALI_ERROR_H
#define MALIMETAL_NATIVE_MALI_ERROR_H

enum class NativeMaliError {
    None = 0,
    InvalidArgument,
    OutOfMemory,
    Busy,
    NotReady,
    Unsupported,
    Aborted,
    DeviceLost,
    IoError,
    Unknown,
};

const char *NativeMaliErrorName(NativeMaliError error);

/* Raw code -> internal error. On Apple platforms the argument is a
 * kern_return_t value; the constants are not redefined here. */
NativeMaliError NativeMaliErrorFromKernReturn(int code);

#endif /* MALIMETAL_NATIVE_MALI_ERROR_H */
