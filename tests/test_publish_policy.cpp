/* SPDX-License-Identifier: GPL-2.0-only
 *
 * Host test for the publish policy: separators, exact names, "*", and the
 * order in which refusals win.
 */
#include "NativeMaliPublish.h"

#include <cstdio>

namespace {

int failures = 0;

void expect(bool condition, const char *what) {
    if (!condition) {
        std::fprintf(stderr, "FAIL %s\n", what);
        ++failures;
    }
}

} /* namespace */

int main() {
    /* Whole-token matching, never prefixes or substrings. */
    expect(NativeMaliClientListed("backboardd", "backboardd"), "exact name");
    expect(!NativeMaliClientListed("backboardd", "backboardd2"), "no prefix match");
    expect(!NativeMaliClientListed("backboardd2", "backboardd"), "no truncation match");
    expect(!NativeMaliClientListed("", "backboardd"), "empty list allows nobody");
    expect(!NativeMaliClientListed(nullptr, "backboardd"), "null list allows nobody");
    expect(!NativeMaliClientListed("backboardd", ""), "empty process name is refused");

    /* Every separator AdrenoMetal accepts. */
    expect(NativeMaliClientListed("SpringBoard backboardd", "backboardd"), "space");
    expect(NativeMaliClientListed("SpringBoard,backboardd", "backboardd"), "comma");
    expect(NativeMaliClientListed("SpringBoard\tbackboardd", "backboardd"), "tab");
    expect(NativeMaliClientListed("SpringBoard\r\nbackboardd\n", "backboardd"), "CRLF");
    expect(NativeMaliClientListed("  ,,backboardd,,  ", "backboardd"), "leading separators");
    expect(!NativeMaliClientListed("SpringBoard", "backboardd"), "unlisted name");

    /* The wildcard names every process, and only as a whole token. */
    expect(NativeMaliClientListed("*", "anything"), "wildcard");
    expect(NativeMaliClientListed("SpringBoard, *", "anything"), "wildcard after a name");
    expect(!NativeMaliClientListed("back*", "backboardd"), "'*' inside a name is not a wildcard");

    /* Decision order: requested, listed, ready. */
    expect(NativeMaliPublish(false, "backboardd", "backboardd", true) ==
               NativeMaliPublishDecision::RefuseNotRequested,
           "no marker refuses even when listed and ready");
    expect(NativeMaliPublish(true, "SpringBoard", "backboardd", true) ==
               NativeMaliPublishDecision::RefuseClientNotListed,
           "unlisted client refuses even when ready");
    expect(NativeMaliPublish(true, "backboardd", "backboardd", false) ==
               NativeMaliPublishDecision::RefuseDeviceNotReady,
           "listed client without a device is refused");
    expect(NativeMaliPublish(true, "backboardd", "backboardd", true) ==
               NativeMaliPublishDecision::Publish,
           "marker, listed and ready publishes");

    /* Names exist for logs; none may be empty. */
    for (int value = 0; value <= static_cast<int>(NativeMaliPublishDecision::RefuseDeviceNotReady);
         ++value) {
        const char *name = NativeMaliPublishDecisionName(
            static_cast<NativeMaliPublishDecision>(value));
        expect(name != nullptr && name[0] != '\0', "every decision has a name");
    }

    if (failures != 0) {
        std::fprintf(stderr, "%d check(s) failed\n", failures);
        return 1;
    }
    std::printf("OK   publish policy: separators, whole names, wildcard, refusal order\n");
    return 0;
}
