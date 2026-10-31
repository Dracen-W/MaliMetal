/* SPDX-License-Identifier: GPL-2.0-only
 *
 * Host test for fence bookkeeping: identity only (fences are opaque), outcome
 * transitions, reset semantics and the refusal to revive a lost fence.
 */
#include "NativeMaliFenceTracker.h"

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
    NativeMaliFenceTracker tracker;

    expect(tracker.state(1) == NativeMaliFenceState::Unknown, "untracked fence is unknown");
    expect(!tracker.track(0), "fence 0 refused");
    expect(tracker.track(0x100), "track a fence");
    expect(tracker.state(0x100) == NativeMaliFenceState::Pending, "new fence is pending");
    expect(!tracker.track(0x100), "duplicate fence refused");
    expect(tracker.count() == 1, "one fence tracked");
    expect(tracker.pending_count() == 1, "one pending");

    expect(!tracker.resolve(0x200), "resolving an unknown fence is refused");
    expect(tracker.resolve(0x100), "resolve a pending fence");
    expect(tracker.state(0x100) == NativeMaliFenceState::Signaled, "fence is signaled");
    expect(!tracker.resolve(0x100), "resolving twice is refused");
    expect(tracker.pending_count() == 0, "nothing pending");

    /* Reset: only pending fences are lost; a signaled fence keeps its outcome. */
    expect(tracker.track(0x300), "track a second fence");
    expect(tracker.lose_all() == 1, "one pending fence lost");
    expect(tracker.state(0x300) == NativeMaliFenceState::Lost, "pending becomes lost");
    expect(tracker.state(0x100) == NativeMaliFenceState::Signaled,
           "a signaled fence is not lost");
    expect(tracker.lose_all() == 0, "losing again loses nothing");

    /* A lost fence cannot be revived. */
    expect(!tracker.resolve(0x300), "a lost fence cannot be resolved");
    expect(tracker.state(0x300) == NativeMaliFenceState::Lost, "it stays lost");

    expect(tracker.forget(0x300), "forget a lost fence");
    expect(tracker.state(0x300) == NativeMaliFenceState::Unknown, "forgotten fence is unknown");
    expect(!tracker.forget(0x300), "forgetting twice is refused");

    /* Capacity is a refusal, not a silent drop: an untracked fence must never
     * be submitted. */
    NativeMaliFenceTracker full;
    for (size_t index = 0; index < full.kCapacity; ++index) {
        if (!full.track(0x10000 + index + 1)) {
            expect(false, "capacity: tracking within the table");
            break;
        }
    }
    expect(full.count() == full.kCapacity, "capacity: table is full");
    expect(!full.track(0x999999), "capacity: further fences are refused");
    expect(full.forget(0x10001), "capacity: a full table can still forget");
    expect(full.track(0x999999), "capacity: room is reused after a forget");

    if (failures != 0) {
        std::fprintf(stderr, "%d check(s) failed\n", failures);
        return 1;
    }
    std::printf("OK   fence tracker: identity, resolve, lost, forget, capacity\n");
    return 0;
}
