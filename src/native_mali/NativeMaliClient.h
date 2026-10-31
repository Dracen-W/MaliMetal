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
#include "NativeMaliError.h"
#include "NativeMaliTransport.h"

class NativeMaliClient {
public:
    explicit NativeMaliClient(NativeMaliTransport &transport,
                              uint32_t abi_version = NativeMaliAbiVersion);

    /* `out` is written only when the call succeeds and the reply is consistent
     * with the ABI version this client was built for. */
    NativeMaliError query(NativeMaliAbiQuery *out);
    NativeMaliError device_info(NativeMaliAbiDeviceInfo *out);
    NativeMaliError status(NativeMaliAbiStatus *out);

    /* Buffers. Sizes and addresses are validated here first, so a malformed
     * request never reaches the service. */
    NativeMaliError bo_alloc(uint64_t bytes, uint64_t *handle);
    NativeMaliError bo_map(uint64_t handle, uint64_t iova, uint32_t permissions,
                           uint64_t *mapped_iova);
    NativeMaliError bo_free(uint64_t handle);
    NativeMaliError bo_cpu_map(uint64_t handle, uint32_t *token, uint64_t *bytes);
    NativeMaliError bo_cpu_unmap(uint64_t handle, uint32_t token);

    /* Jobs and fences. `job_chain` is a GPU address in memory this client has
     * already mapped, aligned to NativeMaliAbiJobChainAlign. */
    NativeMaliError submit(uint64_t job_chain, uint64_t wait_fence,
                           uint64_t wait_timeout_ns, uint64_t *fence);
    NativeMaliError wait_fence(uint64_t fence, uint64_t timeout_ns);

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
