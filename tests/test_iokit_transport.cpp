/* SPDX-License-Identifier: GPL-2.0-only
 *
 * Host checks for the IOKit transport without a kernel service.
 *
 * A real connection needs the NativeMali user client, which does not exist on
 * the target yet, so this test can only prove the refusal path: a null
 * connection, an oversized record, a missing output pointer, and that a failed
 * call leaves the caller's record untouched. That is exactly the half of the
 * transport that must never be wrong.
 */
#if defined(__APPLE__)

#include "NativeMaliIokitTransport.h"

#include "NativeMaliABI.h"
#include "NativeMaliClient.h"

#include <IOKit/IOReturn.h>

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

template <typename T>
T sentinel() {
    T value{};
    std::memset(&value, 0xA5, sizeof(value));
    return value;
}

template <typename T>
bool untouched(const T &value) {
    const T reference = sentinel<T>();
    return std::memcmp(&value, &reference, sizeof(value)) == 0;
}

} /* namespace */

int main() {
    NativeMaliIokitTransport transport(IO_OBJECT_NULL);
    expect(!transport.connected(), "a null connection is not connected");

    NativeMaliAbiQuery input{};
    input.abi_version = NativeMaliAbiVersion;
    NativeMaliAbiQuery output = sentinel<NativeMaliAbiQuery>();

    expect(transport.call(NativeMaliMethodQuery, &input, sizeof(input), &output, sizeof(output)) ==
               kIOReturnNotOpen,
           "a null connection refuses the call");
    expect(untouched(output), "a refused call leaves the output untouched");

    /* An output larger than the biggest ABI record is refused before IOKit. */
    uint8_t big[128] = {};
    expect(transport.call(NativeMaliMethodGetDeviceInfo, nullptr, 0, big, sizeof(big)) ==
               kIOReturnBadArgument,
           "an oversized reply is refused");
    bool zeroed = true;
    for (uint8_t byte : big) {
        zeroed = zeroed && byte == 0;
    }
    expect(zeroed, "the oversized buffer is not written");

    expect(transport.call(NativeMaliMethodGetDeviceInfo, nullptr, 0, nullptr, 16) ==
               kIOReturnBadArgument,
           "a missing output pointer is refused");

    /* Through the client: the refusal maps, and the caller's record survives. */
    NativeMaliClient client(transport);
    NativeMaliAbiQuery queried = sentinel<NativeMaliAbiQuery>();
    expect(client.query(&queried) != NativeMaliError::None, "query fails without a service");
    expect(untouched(queried), "the client does not adopt a refused reply");

    if (failures != 0) {
        std::fprintf(stderr, "%d check(s) failed\n", failures);
        return 1;
    }
    std::printf("OK   iokit transport: null connection, size refusal, untouched output\n");
    return 0;
}

#else

#include <cstdio>

int main() {
    std::printf("SKIP iokit transport: not an Apple platform\n");
    return 0;
}

#endif
