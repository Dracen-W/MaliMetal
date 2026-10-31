/* SPDX-License-Identifier: GPL-2.0-only
 *
 * The one boundary between the ABI client and the thing that carries a record
 * to the kernel.
 *
 * On the phone this is NativeMaliIokitTransport (one IOConnectCallStructMethod).
 * In tests it is a scripted fake service. The client never sees which: it only
 * builds a record, checks the sizes, and reads the reply when the call
 * succeeded. Keeping this interface separate from the client is what lets the
 * IOKit transport and the fake service be swapped without touching validation.
 */
#ifndef MALIMETAL_NATIVE_MALI_TRANSPORT_H
#define MALIMETAL_NATIVE_MALI_TRANSPORT_H

#include <stddef.h>
#include <stdint.h>

class NativeMaliTransport {
public:
    virtual ~NativeMaliTransport() = default;

    /* One external method call. Returns 0 on success. Implementations must not
     * touch `output` when they return an error; the client does not read it. */
    virtual int call(uint32_t selector, const void *input, size_t input_bytes,
                     void *output, size_t output_bytes) = 0;
};

#endif /* MALIMETAL_NATIVE_MALI_TRANSPORT_H */
