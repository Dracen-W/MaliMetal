/* SPDX-License-Identifier: GPL-2.0-only
 *
 * Host test for the userspace NativeMali client: record sizes, the refusal
 * rules, error mapping and the "output untouched unless the call succeeded"
 * guarantee. No kernel, no IOKit connection, no GPU.
 */
#include "NativeMaliClient.h"
#include "NativeMaliAbiTable.h"

#include "NativeMaliABI.h"

#if defined(__APPLE__)
#include <IOKit/IOReturn.h>
#endif

#include <cstdio>
#include <cstring>

namespace {

int failures = 0;

void expect(bool condition, const char *what) {
    if (!condition) {
        std::fprintf(stderr, "FAIL %s\n", what);
        ++failures;
    }
}

struct FakeTransport final : NativeMaliTransport {
    int result = 0;
    uint32_t selector = 0;
    size_t input_bytes = 0;
    size_t output_bytes = 0;
    bool input_was_null = true;
    bool output_was_null = true;
    unsigned calls = 0;

    /* What Query should answer. */
    uint32_t reply_version = NativeMaliAbiVersion;
    uint64_t reply_max_bo_bytes = UINT64_C(1) << NativeMaliAbiMaxBoBytesShift;

    int call(uint32_t sel, const void *input, size_t in_bytes, void *output,
             size_t out_bytes) override {
        ++calls;
        selector = sel;
        input_bytes = in_bytes;
        output_bytes = out_bytes;
        input_was_null = input == nullptr;
        output_was_null = output == nullptr;

        if (result != 0) {
            /* A real service may leave garbage behind on failure; the client
             * must not read it. Scribble to make a copy visible if it does. */
            if (output != nullptr && out_bytes >= sizeof(uint64_t)) {
                std::memset(output, 0xAA, out_bytes);
            }
            return result;
        }

        if (sel == NativeMaliMethodQuery) {
            auto *reply = static_cast<NativeMaliAbiQuery *>(output);
            reply->abi_version = reply_version;
            reply->input_bits = 48;
            reply->output_bits = 40;
            reply->flags = 0;
            reply->max_bo_bytes = reply_max_bo_bytes;
        } else if (sel == NativeMaliMethodGetDeviceInfo) {
            auto *reply = static_cast<NativeMaliAbiDeviceInfo *>(output);
            reply->gpu_id = 0x90;
            reply->flags = NativeMaliDeviceNonCoherent;
            reply->va_size = UINT64_C(1) << 48;
        } else if (sel == NativeMaliMethodGetStatus) {
            auto *reply = static_cast<NativeMaliAbiStatus *>(output);
            reply->faults = 3;
            reply->resets = 1;
        }
        return 0;
    }
};

template <typename T>
T sentinel_record() {
    T record{};
    std::memset(&record, 0xA5, sizeof(record));
    return record;
}

template <typename T>
bool untouched(const T &record) {
    const T reference = sentinel_record<T>();
    return std::memcmp(&record, &reference, sizeof(record)) == 0;
}

} /* namespace */

int main() {
    FakeTransport transport;
    NativeMaliClient client(transport);

    /* Query: the request carries the version, the reply is validated. */
    NativeMaliAbiQuery query{};
    expect(client.query(&query) == NativeMaliError::None, "Query succeeds");
    expect(transport.selector == NativeMaliMethodQuery, "Query uses selector 1");
    expect(transport.input_bytes == sizeof(NativeMaliAbiQuery), "Query input size");
    expect(transport.output_bytes == sizeof(NativeMaliAbiQuery), "Query output size");
    expect(!transport.input_was_null, "Query sends its request record");
    expect(query.abi_version == NativeMaliAbiVersion, "reply version is v1");
    expect(query.max_bo_bytes == (UINT64_C(1) << NativeMaliAbiMaxBoBytesShift),
           "reply max BO bytes");

    /* A reply for a different ABI must not be adopted, and the caller's record
     * must not be touched. */
    transport.reply_version = NativeMaliAbiVersion + 1;
    NativeMaliAbiQuery bad = sentinel_record<NativeMaliAbiQuery>();
    expect(client.query(&bad) == NativeMaliError::Unsupported, "wrong version refused");
    expect(untouched(bad), "wrong version leaves the output untouched");
    transport.reply_version = NativeMaliAbiVersion;

    /* A driver that disagrees about the buffer limit is not this ABI. */
    transport.reply_max_bo_bytes = 1u << 20;
    bad = sentinel_record<NativeMaliAbiQuery>();
    expect(client.query(&bad) == NativeMaliError::Unsupported, "wrong BO limit refused");
    expect(untouched(bad), "wrong BO limit leaves the output untouched");
    transport.reply_max_bo_bytes = UINT64_C(1) << NativeMaliAbiMaxBoBytesShift;

    /* GetDeviceInfo: no input record, fixed 64-byte reply. */
    NativeMaliAbiDeviceInfo info{};
    expect(client.device_info(&info) == NativeMaliError::None, "GetDeviceInfo succeeds");
    expect(transport.input_bytes == 0 && transport.input_was_null,
           "GetDeviceInfo sends no input");
    expect(transport.output_bytes == sizeof(NativeMaliAbiDeviceInfo),
           "GetDeviceInfo reply size");
    expect(info.gpu_id == 0x90, "device info copied");
    expect((info.flags & NativeMaliDeviceNonCoherent) != 0, "non-coherent flag visible");

    /* A failed call maps the kernel code and leaves the output alone. */
    transport.result = kIOReturnBusy;
    NativeMaliAbiDeviceInfo busy = sentinel_record<NativeMaliAbiDeviceInfo>();
    expect(client.device_info(&busy) == NativeMaliError::Busy, "Busy maps to Busy");
    expect(untouched(busy), "failed GetDeviceInfo leaves the output untouched");
    expect(client.last_kern_return() == kIOReturnBusy, "raw code kept for diagnosis");

    transport.result = kIOReturnNoMemory;
    expect(client.device_info(&busy) == NativeMaliError::OutOfMemory,
           "NoMemory maps to OutOfMemory");

    transport.result = 0x1234;
    expect(client.device_info(&busy) == NativeMaliError::Unknown, "unknown code maps to Unknown");
    transport.result = 0;

    /* GetStatus. */
    NativeMaliAbiStatus status{};
    expect(client.status(&status) == NativeMaliError::None, "GetStatus succeeds");
    expect(transport.input_was_null && transport.input_bytes == 0,
           "GetStatus sends no input");
    expect(transport.output_bytes == sizeof(NativeMaliAbiStatus), "GetStatus reply size");
    expect(status.faults == 3 && status.resets == 1, "status counters copied");

    /* Reset: no records at all. */
    expect(client.reset() == NativeMaliError::None, "Reset succeeds");
    expect(transport.selector == NativeMaliMethodReset, "Reset uses selector 11");
    expect(transport.input_was_null && transport.output_was_null,
           "Reset carries no records");
    expect(transport.input_bytes == 0 && transport.output_bytes == 0, "Reset sizes are zero");

    /* Null output pointers are refused before any transport call. */
    const unsigned calls_before = transport.calls;
    expect(client.query(nullptr) == NativeMaliError::InvalidArgument, "null Query reply refused");
    expect(client.device_info(nullptr) == NativeMaliError::InvalidArgument,
           "null device info refused");
    expect(client.status(nullptr) == NativeMaliError::InvalidArgument, "null status refused");
    expect(transport.calls == calls_before, "no transport call for a null reply");

    /* The error names exist for logs. */
    for (int value = 0; value <= static_cast<int>(NativeMaliError::Unknown); ++value) {
        const char *name = NativeMaliErrorName(static_cast<NativeMaliError>(value));
        expect(name != nullptr && name[0] != '\0', "every error has a name");
    }
    expect(NativeMaliErrorFromKernReturn(0) == NativeMaliError::None, "0 maps to None");

    /* Size validation: the table is the single source, so a method with no
     * input cannot be called with one. */
    const NativeMaliAbiMethodRecord *reset = NativeMaliAbiFindMethod(NativeMaliMethodReset);
    expect(!NativeMaliAbiRecordSizesMatch(reset, sizeof(NativeMaliAbiStatus), 0),
           "Reset refuses an input record");

    if (failures != 0) {
        std::fprintf(stderr, "%d check(s) failed\n", failures);
        return 1;
    }
    std::printf("OK   native-mali client: records, errors, untouched-on-failure\n");
    return 0;
}
