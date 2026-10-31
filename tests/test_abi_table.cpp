/* SPDX-License-Identifier: GPL-2.0-only
 *
 * Host test for the v1 NativeMali method table: selector coverage, record
 * sizes, and the refusal rules the dispatch path will rely on.
 */
#include "NativeMaliAbiTable.h"

#include "NativeMaliABI.h"

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

constexpr size_t kExpectedMethods = 11;

} /* namespace */

int main() {
    /* One entry per selector, in order, no duplicates, no gaps. */
    expect(kNativeMaliAbiMethodCount == kExpectedMethods, "11 methods in the table");
    for (size_t index = 0; index < kNativeMaliAbiMethodCount; ++index) {
        const NativeMaliAbiMethodRecord &method = kNativeMaliAbiMethods[index];
        expect(method.selector == index + 1, "selectors are 1..11 in order");
        expect(method.name != nullptr && std::strlen(method.name) != 0, "every method is named");
        expect(kNativeMaliAbiMethods[index].selector == method.selector,
               "selectors are unique");
    }
    expect(kNativeMaliAbiMethods[kNativeMaliAbiMethodCount - 1].selector ==
               NativeMaliMethodLast,
           "last entry is NativeMaliMethodLast");

    /* Lookup must find each selector and refuse the two invalid ones. */
    for (size_t index = 0; index < kNativeMaliAbiMethodCount; ++index) {
        const NativeMaliAbiMethodRecord *found =
            NativeMaliAbiFindMethod(kNativeMaliAbiMethods[index].selector);
        expect(found == &kNativeMaliAbiMethods[index], "lookup returns the table entry");
    }
    expect(NativeMaliAbiFindMethod(0) == nullptr, "selector 0 is refused");
    expect(NativeMaliAbiFindMethod(NativeMaliMethodLast + 1) == nullptr,
           "selector after Last is refused");

    /* Record sizes match the header structs exactly. */
    const NativeMaliAbiMethodRecord *query = NativeMaliAbiFindMethod(NativeMaliMethodQuery);
    expect(NativeMaliAbiRecordSizesMatch(query, sizeof(NativeMaliAbiQuery),
                                         sizeof(NativeMaliAbiQuery)),
           "Query takes and returns 24 bytes");
    expect(!NativeMaliAbiRecordSizesMatch(query, sizeof(NativeMaliAbiQuery) - 1,
                                          sizeof(NativeMaliAbiQuery)),
           "Query refuses a short input");
    expect(!NativeMaliAbiRecordSizesMatch(query, sizeof(NativeMaliAbiQuery), 0),
           "Query requires its output record");

    const NativeMaliAbiMethodRecord *info =
        NativeMaliAbiFindMethod(NativeMaliMethodGetDeviceInfo);
    expect(NativeMaliAbiRecordSizesMatch(info, 0, sizeof(NativeMaliAbiDeviceInfo)),
           "GetDeviceInfo takes no input and returns 64 bytes");
    expect(!NativeMaliAbiRecordSizesMatch(info, sizeof(NativeMaliAbiDeviceInfo),
                                          sizeof(NativeMaliAbiDeviceInfo)),
           "GetDeviceInfo refuses an input record");

    const NativeMaliAbiMethodRecord *reset = NativeMaliAbiFindMethod(NativeMaliMethodReset);
    expect(NativeMaliAbiRecordSizesMatch(reset, 0, 0), "Reset carries no records");
    expect(!NativeMaliAbiRecordSizesMatch(reset, 0, sizeof(NativeMaliAbiStatus)),
           "Reset refuses an output record");
    expect(!NativeMaliAbiRecordSizesMatch(reset, sizeof(NativeMaliAbiQuery), 0),
           "Reset refuses an input record");

    const NativeMaliAbiMethodRecord *submit = NativeMaliAbiFindMethod(NativeMaliMethodSubmit);
    expect(NativeMaliAbiRecordSizesMatch(submit, sizeof(NativeMaliAbiSubmit),
                                         sizeof(NativeMaliAbiSubmitReply)),
           "Submit takes 32 and returns 8 bytes");
    expect(!NativeMaliAbiRecordSizesMatch(submit, sizeof(NativeMaliAbiSubmit),
                                          sizeof(NativeMaliAbiStatus)),
           "Submit refuses the status record as its reply");

    /* A method with no input must still accept its documented size and nothing
     * else, so BoFree cannot be called with a map record. */
    const NativeMaliAbiMethodRecord *free_method =
        NativeMaliAbiFindMethod(NativeMaliMethodBoFree);
    expect(NativeMaliAbiRecordSizesMatch(free_method, sizeof(NativeMaliAbiBoFree), 0),
           "BoFree takes 8 bytes");
    expect(!NativeMaliAbiRecordSizesMatch(free_method, sizeof(NativeMaliAbiBoMap), 0),
           "BoFree refuses the map record");

    expect(!NativeMaliAbiRecordSizesMatch(nullptr, 0, 0), "a null method matches nothing");

    if (failures != 0) {
        std::fprintf(stderr, "%d check(s) failed\n", failures);
        return 1;
    }
    std::printf("OK   abi table: %zu methods, sizes match NativeMaliABI.h v%d\n",
                kNativeMaliAbiMethodCount, NativeMaliAbiVersion);
    return 0;
}
