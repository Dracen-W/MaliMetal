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

    /* What the other methods answer. */
    uint64_t reply_handle = 0xABC;
    uint32_t reply_token = 5;
    uint64_t reply_cpu_bytes = 8192;
    uint64_t reply_fence = 0xFEED;
    bool echo_map_iova = true;

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
            reply->gpu_id = 0x74021000;
            reply->flags = NativeMaliDeviceNonCoherent;
            reply->va_size = UINT64_C(1) << 48;
            reply->mmu_features = (40u << 8) | 48u;
            reply->texture_features[2] = 0x33330000;
            reply->pgsize_shift = NativeMaliAbiPageShift;
        } else if (sel == NativeMaliMethodGetStatus) {
            auto *reply = static_cast<NativeMaliAbiStatus *>(output);
            reply->faults = 3;
            reply->resets = 1;
        } else if (sel == NativeMaliMethodBoAlloc) {
            auto *reply = static_cast<NativeMaliAbiBoAllocReply *>(output);
            reply->handle = reply_handle;
        } else if (sel == NativeMaliMethodBoMap) {
            const auto *request = static_cast<const NativeMaliAbiBoMap *>(input);
            auto *reply = static_cast<NativeMaliAbiBoMapReply *>(output);
            reply->iova = echo_map_iova ? request->iova : request->iova + 0x1000;
            last_map_request = *request;
        } else if (sel == NativeMaliMethodBoCpuMap) {
            auto *reply = static_cast<NativeMaliAbiBoCpuMapReply *>(output);
            reply->token = reply_token;
            reply->bytes = reply_cpu_bytes;
        } else if (sel == NativeMaliMethodSubmit) {
            last_submit_request = *static_cast<const NativeMaliAbiSubmit *>(input);
            auto *reply = static_cast<NativeMaliAbiSubmitReply *>(output);
            reply->fence = reply_fence;
        }
        return 0;
    }

    NativeMaliAbiBoMap last_map_request{};
    NativeMaliAbiSubmit last_submit_request{};
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
    expect(info.gpu_id == 0x74021000, "device info copied");
    expect((info.flags & NativeMaliDeviceNonCoherent) != 0, "non-coherent flag visible");
    expect(info.mmu_features == ((40u << 8) | 48u), "v2 MMU features copied");
    expect(info.texture_features[2] == 0x33330000, "v2 texture features copied");
    expect(info.pgsize_shift == NativeMaliAbiPageShift, "v2 page shift copied");

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

    /* Buffers: the client validates before the service is called. */
    uint64_t handle = 0;
    expect(client.bo_alloc(2 * NativeMaliAbiPageBytes, &handle) == NativeMaliError::None,
           "BoAlloc succeeds");
    expect(handle == transport.reply_handle, "handle copied");
    expect(transport.selector == NativeMaliMethodBoAlloc && transport.input_bytes == 8 &&
               transport.output_bytes == 8,
           "BoAlloc uses selector 3 and 8-byte records");

    const unsigned calls_before_alloc = transport.calls;
    expect(client.bo_alloc(0, &handle) == NativeMaliError::InvalidArgument, "zero bytes refused");
    expect(client.bo_alloc(NativeMaliAbiPageBytes + 1, &handle) == NativeMaliError::InvalidArgument,
           "unaligned bytes refused");
    expect(client.bo_alloc(NATIVE_MALI_ABI_MAX_BO_BYTES + NativeMaliAbiPageBytes, &handle) ==
               NativeMaliError::InvalidArgument,
           "over the limit refused");
    expect(client.bo_alloc(NativeMaliAbiPageBytes, nullptr) == NativeMaliError::InvalidArgument,
           "null handle refused");
    expect(transport.calls == calls_before_alloc, "invalid BoAlloc never reaches the service");

    transport.reply_handle = 0;
    expect(client.bo_alloc(NativeMaliAbiPageBytes, &handle) == NativeMaliError::Unsupported,
           "a zero handle reply is refused");
    transport.reply_handle = 0xABC;

    /* BoMap: nonzero, aligned, read always; the reply must echo the address. */
    uint64_t mapped = 0;
    expect(client.bo_map(handle, 0x10000, NativeMaliBoRead | NativeMaliBoWrite, &mapped) ==
               NativeMaliError::None,
           "BoMap succeeds");
    expect(mapped == 0x10000, "mapped address copied");
    expect(transport.last_map_request.handle == handle &&
               transport.last_map_request.iova == 0x10000 &&
               transport.last_map_request.permissions ==
                   (NativeMaliBoRead | NativeMaliBoWrite),
           "BoMap request carries handle, IOVA and permissions");
    expect(client.bo_map(handle, 0, NativeMaliBoRead, &mapped) ==
               NativeMaliError::InvalidArgument,
           "BoMap refuses IOVA 0");
    expect(client.bo_map(handle, 0x10800, NativeMaliBoRead, &mapped) ==
               NativeMaliError::InvalidArgument,
           "BoMap refuses an unaligned IOVA");
    expect(client.bo_map(handle, 0x10000, NativeMaliBoWrite, &mapped) ==
               NativeMaliError::InvalidArgument,
           "BoMap refuses write without read");
    expect(client.bo_map(handle, 0x10000, NativeMaliBoRead | (1u << 9), &mapped) ==
               NativeMaliError::InvalidArgument,
           "BoMap refuses an unknown permission bit");
    expect(client.bo_map(0, 0x10000, NativeMaliBoRead, &mapped) ==
               NativeMaliError::InvalidArgument,
           "BoMap refuses handle 0");
    expect(client.bo_map(handle, 0x10000, NativeMaliBoRead, nullptr) ==
               NativeMaliError::InvalidArgument,
           "BoMap refuses a null out pointer");

    transport.echo_map_iova = false;
    expect(client.bo_map(handle, 0x10000, NativeMaliBoRead, &mapped) ==
               NativeMaliError::Unsupported,
           "BoMap refuses a reply that moved the address");
    transport.echo_map_iova = true;

    expect(client.bo_free(0) == NativeMaliError::InvalidArgument, "BoFree refuses handle 0");
    expect(client.bo_free(handle) == NativeMaliError::None, "BoFree succeeds");
    expect(transport.selector == NativeMaliMethodBoFree && transport.input_bytes == 8 &&
               transport.output_was_null,
           "BoFree uses selector 5, one record, no reply");

    /* CPU view: a token is nonzero and belongs to this buffer. */
    uint32_t token = 0;
    uint64_t cpu_bytes = 0;
    expect(client.bo_cpu_map(handle, &token, &cpu_bytes) == NativeMaliError::None,
           "BoCpuMap succeeds");
    expect(token == transport.reply_token && cpu_bytes == transport.reply_cpu_bytes,
           "token and bytes copied");
    expect(transport.selector == NativeMaliMethodBoCpuMap && transport.input_bytes == 8 &&
               transport.output_bytes == 16,
           "BoCpuMap uses selector 6 with 8/16-byte records");

    transport.reply_token = 0;
    expect(client.bo_cpu_map(handle, &token, &cpu_bytes) == NativeMaliError::Unsupported,
           "a zero token is refused");
    transport.reply_token = 5;
    expect(client.bo_cpu_map(handle, nullptr, &cpu_bytes) == NativeMaliError::InvalidArgument,
           "BoCpuMap refuses a null token pointer");
    expect(client.bo_cpu_map(handle, &token, nullptr) == NativeMaliError::InvalidArgument,
           "BoCpuMap refuses a null bytes pointer");

    expect(client.bo_cpu_unmap(handle, token) == NativeMaliError::None, "BoCpuUnmap succeeds");
    expect(transport.selector == NativeMaliMethodBoCpuUnmap && transport.input_bytes == 16 &&
               transport.output_was_null,
           "BoCpuUnmap uses selector 7");
    expect(client.bo_cpu_unmap(handle, 0) == NativeMaliError::InvalidArgument,
           "BoCpuUnmap refuses token 0");

    /* Submit: the chain is 64-byte aligned and the fence is nonzero. */
    uint64_t fence = 0;
    expect(client.submit(0x20000, 0, 0, &fence) == NativeMaliError::None, "Submit succeeds");
    expect(fence == transport.reply_fence, "fence copied");
    expect(transport.selector == NativeMaliMethodSubmit && transport.input_bytes == 32 &&
               transport.output_bytes == 8,
           "Submit uses selector 8 with 32/8-byte records");
    expect(transport.last_submit_request.job_chain == 0x20000 &&
               transport.last_submit_request.wait_fence == 0 &&
               transport.last_submit_request.wait_timeout_ns == 0,
           "Submit request carries the chain and no wait");

    expect(client.submit(0, 0, 0, &fence) == NativeMaliError::InvalidArgument,
           "Submit refuses chain 0");
    expect(client.submit(0x20008, 0, 0, &fence) == NativeMaliError::InvalidArgument,
           "Submit refuses an unaligned chain");
    expect(client.submit(0x20000, 0, 0, nullptr) == NativeMaliError::InvalidArgument,
           "Submit refuses a null fence pointer");

    transport.reply_fence = 0;
    expect(client.submit(0x20000, 0, 0, &fence) == NativeMaliError::Unsupported,
           "a zero fence reply is refused");
    transport.reply_fence = 0xFEED;

    expect(client.submit(0x20000, 0xFEED, 5000, &fence) == NativeMaliError::None,
           "Submit with a dependency succeeds");
    expect(transport.last_submit_request.wait_fence == 0xFEED &&
               transport.last_submit_request.wait_timeout_ns == 5000,
           "Submit passes the dependency and its timeout");

    /* WaitFence: a fence is required; zero is not "no wait" here. */
    expect(client.wait_fence(0, 0) == NativeMaliError::InvalidArgument,
           "WaitFence refuses fence 0");
    expect(client.wait_fence(0xFEED, 0) == NativeMaliError::None, "WaitFence succeeds");
    expect(transport.selector == NativeMaliMethodWaitFence && transport.input_bytes == 16 &&
               transport.output_was_null,
           "WaitFence uses selector 9, one record, no reply");

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
