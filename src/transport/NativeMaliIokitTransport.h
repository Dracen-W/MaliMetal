/* SPDX-License-Identifier: GPL-2.0-only
 *
 * The IOKit transport: one external method call, nothing else.
 *
 * This is the only file in the userspace stack that touches IOKit, and it holds
 * no policy. Selector validation, record sizes and error mapping stay in
 * NativeMaliClient, which is host-tested; the transport only copies a fixed-size
 * record in and out.
 *
 * Rules kept here, because they are about the boundary and not about the GPU:
 *   - a null connection is refused before any call;
 *   - an output record larger than the ABI's biggest record is refused;
 *   - the reply is written to a local record and copied only when the call
 *     succeeded and reported exactly the requested size, so a failed call never
 *     touches the caller's memory.
 *
 * The connection is not retained or closed here: whoever opened the user client
 * owns it. Copying that ownership into a value type would make double-close and
 * use-after-free possible.
 */
#ifndef MALIMETAL_NATIVE_MALI_IOKIT_TRANSPORT_H
#define MALIMETAL_NATIVE_MALI_IOKIT_TRANSPORT_H

#if defined(__APPLE__)

#include <stddef.h>
#include <stdint.h>

#include <IOKit/IOKitLib.h>

#include "NativeMaliTransport.h"

class NativeMaliIokitTransport final : public NativeMaliTransport {
public:
    /* `connection` is an open user client; IO_OBJECT_NULL is accepted and every
     * call then fails with kIOReturnNotOpen. */
    explicit NativeMaliIokitTransport(io_connect_t connection);
    ~NativeMaliIokitTransport() override = default;

    NativeMaliIokitTransport(const NativeMaliIokitTransport &) = delete;
    NativeMaliIokitTransport &operator=(const NativeMaliIokitTransport &) = delete;

    int call(uint32_t selector, const void *input, size_t input_bytes, void *output,
             size_t output_bytes) override;

    bool connected() const { return connection_ != IO_OBJECT_NULL; }

    /* The largest record in NativeMaliABI.h is 64 bytes (GetDeviceInfo). */
    static constexpr size_t kMaxRecordBytes = 64;

private:
    io_connect_t connection_;
};

#endif /* __APPLE__ */

#endif /* MALIMETAL_NATIVE_MALI_IOKIT_TRANSPORT_H */
