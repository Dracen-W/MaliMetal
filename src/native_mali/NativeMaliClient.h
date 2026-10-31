/* SPDX-License-Identifier: GPL-2.0-only
 *
 * The userspace side of the v1 NativeMali ABI: validation, the call, and the
 * "output untouched unless the call succeeded" rule.
 *
 * This file knows nothing about IOKit. A transport (the IOConnect call on the
 * phone, a scripted fake in tests) carries one record pair across. Everything
 * that can be decided without the kernel is decided here.
 */
#ifndef MALIMETAL_NATIVE_MALI_CLIENT_H
#define MALIMETAL_NATIVE_MALI_CLIENT_H

#include <stddef.h>
#include <stdint.h>

#include "NativeMaliABI.h"

/* A small internal vocabulary. The kernel's kIOReturn* values are mapped onto
 * it in one place; Vulkan's VkResult is mapped from it in the backend. */
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

class NativeMaliTransport {
public:
    virtual ~NativeMaliTransport() = default;

    /* One external method call. Returns 0 on success. Implementations must not
     * touch `output` when they return an error; the client does not read it. */
    virtual int call(uint32_t selector, const void *input, size_t input_bytes,
                     void *output, size_t output_bytes) = 0;
};

class NativeMaliClient {
public:
    explicit NativeMaliClient(NativeMaliTransport &transport,
                              uint32_t abi_version = NativeMaliAbiVersion);

    /* `out` is written only when the call succeeds and the reply is consistent
     * with the ABI version this client was built for. */
    NativeMaliError query(NativeMaliAbiQuery *out);
    NativeMaliError device_info(NativeMaliAbiDeviceInfo *out);
    NativeMaliError status(NativeMaliAbiStatus *out);

    /* Device-wide recovery. Destroys every client's in-flight jobs. */
    NativeMaliError reset();

    /* The raw code of the last call, for diagnosis. Not an API to branch on. */
    int last_kern_return() const { return last_kern_return_; }

private:
    NativeMaliError call(uint32_t selector, const void *input, size_t input_bytes,
                         void *output, size_t output_bytes);

    NativeMaliTransport &transport_;
    uint32_t abi_version_;
    int last_kern_return_ = 0;
};

#endif /* MALIMETAL_NATIVE_MALI_CLIENT_H */
