/* SPDX-License-Identifier: GPL-2.0-only
 *
 * Host test for the userspace buffer records: handle lifetime, mapping state,
 * CPU tokens, and every refusal that keeps the ABI calls consistent.
 */
#include "NativeMaliBoTable.h"

#include "NativeMaliABI.h"

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
    NativeMaliBoTable table;

    /* Registration: a handle is unique, nonzero, page sized and within the
     * header's per-buffer limit. */
    expect(table.add(1, 3 * NativeMaliAbiPageBytes), "add a buffer");
    expect(table.count() == 1, "one buffer recorded");
    expect(!table.add(1, NativeMaliAbiPageBytes), "duplicate handle refused");
    expect(!table.add(0, NativeMaliAbiPageBytes), "handle 0 refused");
    expect(!table.add(2, 0), "zero bytes refused");
    expect(!table.add(2, NativeMaliAbiPageBytes + 1), "unaligned bytes refused");
    expect(!table.add(2, NATIVE_MALI_ABI_MAX_BO_BYTES + NativeMaliAbiPageBytes),
           "over the header limit refused");
    expect(table.add(2, NATIVE_MALI_ABI_MAX_BO_BYTES), "the header limit itself is allowed");
    expect(table.find(1) != nullptr && table.find(1)->bytes == 3 * NativeMaliAbiPageBytes,
           "find returns the record");
    expect(table.find(99) == nullptr, "unknown handle not found");

    /* Mapping: one map per buffer, nonzero and aligned, read always required,
     * unknown permission bits refused. */
    expect(!table.set_mapping(1, 0, NativeMaliBoRead), "iova 0 refused");
    expect(!table.set_mapping(1, 0x1001, NativeMaliBoRead), "unaligned iova refused");
    expect(!table.set_mapping(1, 0x1000, NativeMaliBoWrite), "write without read refused");
    expect(!table.set_mapping(1, 0x1000, NativeMaliBoRead | (1u << 7)),
           "unknown permission bit refused");
    expect(!table.set_mapping(99, 0x1000, NativeMaliBoRead), "map of an unknown handle refused");
    expect(table.set_mapping(1, 0x2000, NativeMaliBoRead | NativeMaliBoWrite), "map a buffer");
    expect(table.find(1)->mapped && table.find(1)->iova == 0x2000, "mapping recorded");
    expect(!table.set_mapping(1, 0x3000, NativeMaliBoRead), "second map refused");

    /* Unmapping returns the range exactly once. */
    uint64_t iova = 0;
    expect(!table.clear_mapping(2, &iova), "unmap of an unmapped buffer refused");
    expect(!table.clear_mapping(1, nullptr), "unmap without an out pointer refused");
    expect(table.clear_mapping(1, &iova), "unmap succeeds");
    expect(iova == 0x2000, "the mapped range is returned");
    expect(!table.find(1)->mapped, "buffer is unmapped");
    expect(!table.clear_mapping(1, &iova), "second unmap refused");

    /* CPU token: one view per buffer, and it can only be dropped by the token
     * that was handed out. */
    expect(!table.set_cpu_token(1, 0), "token 0 refused");
    expect(table.set_cpu_token(1, 7), "cpu token recorded");
    expect(!table.set_cpu_token(1, 8), "second cpu view refused");
    expect(!table.clear_cpu_token(1, 8), "wrong token cannot drop the view");
    expect(table.clear_cpu_token(1, 7), "the matching token drops the view");
    expect(table.set_cpu_token(1, 9), "a new view is allowed after the old one");

    /* Removing a mapped buffer reports its IOVA so the caller can free it. */
    expect(table.set_mapping(1, 0x4000, NativeMaliBoRead), "re-map before remove");
    expect(!table.remove(1, nullptr), "remove of a mapped buffer needs an out pointer");
    expect(table.remove(1, &iova), "remove reports the mapping");
    expect(iova == 0x4000, "removed mapping returned");
    expect(table.find(1) == nullptr, "removed buffer is gone");
    expect(table.count() == 1, "one buffer left");

    iova = 0xdead;
    expect(table.remove(2, &iova), "remove an unmapped buffer");
    expect(iova == 0, "no mapping to free");
    expect(!table.remove(2, &iova), "double remove refused");

    /* Capacity is a refusal, not a crash or a silent wrap. */
    NativeMaliBoTable full;
    for (size_t index = 0; index < full.kCapacity; ++index) {
        if (!full.add(index + 1, NativeMaliAbiPageBytes)) {
            expect(false, "capacity: add within the table");
            break;
        }
    }
    expect(full.count() == full.kCapacity, "capacity: table is full");
    expect(!full.add(full.kCapacity + 1, NativeMaliAbiPageBytes), "capacity: further adds refused");
    expect(full.remove(1, &iova), "capacity: a full table can still remove");
    expect(full.add(12345, NativeMaliAbiPageBytes), "capacity: room is reused after a remove");

    if (failures != 0) {
        std::fprintf(stderr, "%d check(s) failed\n", failures);
        return 1;
    }
    std::printf("OK   bo table: handles, mapping, tokens, capacity refusals\n");
    return 0;
}
