/* SPDX-License-Identifier: GPL-2.0-only
 *
 * IOKit transport for the NativeMali ABI. See NativeMaliIokitTransport.h.
 */
#if defined(__APPLE__)

#include "NativeMaliIokitTransport.h"

#include <IOKit/IOReturn.h>

#include <cstring>

NativeMaliIokitTransport::NativeMaliIokitTransport(io_connect_t connection)
    : connection_(connection) {}

int NativeMaliIokitTransport::call(uint32_t selector, const void *input, size_t input_bytes,
                                   void *output, size_t output_bytes) {
    /* Arguments first: a malformed record is a caller bug whether or not a
     * connection happens to be open. */
    if (output_bytes > kMaxRecordBytes) {
        return kIOReturnBadArgument;
    }
    if (output_bytes != 0 && output == nullptr) {
        return kIOReturnBadArgument;
    }
    if (connection_ == IO_OBJECT_NULL) {
        return kIOReturnNotOpen;
    }

    /* The reply lands here first. A failed call may still have written part of
     * the struct, and the caller must never see that. */
    uint8_t record[kMaxRecordBytes] = {};
    size_t reply_bytes = output_bytes;

    const kern_return_t result =
        IOConnectCallStructMethod(connection_, selector, input, input_bytes,
                                  output_bytes == 0 ? nullptr : record, &reply_bytes);
    if (result != KERN_SUCCESS) {
        return static_cast<int>(result);
    }
    if (reply_bytes != output_bytes) {
        /* The service wrote a different shape than the ABI defines. Refuse it
         * rather than copying a truncated or padded record. */
        return kIOReturnBadArgument;
    }
    if (output_bytes != 0) {
        std::memcpy(output, record, output_bytes);
    }
    return 0;
}

#endif /* __APPLE__ */
