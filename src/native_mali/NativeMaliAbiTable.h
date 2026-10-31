/* SPDX-License-Identifier: GPL-2.0-only
 *
 * The method table for the v1 NativeMali wire ABI.
 *
 * The canonical header is NativeMaliABI.h in the Mali-G52 repository; this
 * file adds no record and changes no constant. It exists so the userspace
 * side can validate a call before it reaches a service:
 *
 *   - the selector must be known;
 *   - the input and output record sizes must be exactly the struct sizes;
 *   - a method with no records (Reset) accepts zero bytes and nothing else.
 *
 * The kernel's NativeMaliUserClient::dispatch() applies the same rules on its
 * side. Two implementations of one contract are intentional: a userspace bug
 * should fail before the boundary, not inside the driver.
 */
#ifndef MALIMETAL_NATIVE_MALI_ABI_TABLE_H
#define MALIMETAL_NATIVE_MALI_ABI_TABLE_H

#include <stddef.h>
#include <stdint.h>

struct NativeMaliAbiMethodRecord {
    uint32_t selector;
    const char *name;
    /* 0 means the method takes no record of that direction. */
    uint32_t input_bytes;
    uint32_t output_bytes;
};

/* Selector order, 1..NativeMaliMethodLast. */
extern const NativeMaliAbiMethodRecord kNativeMaliAbiMethods[];
extern const size_t kNativeMaliAbiMethodCount;

/* nullptr when the selector is unknown. */
const NativeMaliAbiMethodRecord *NativeMaliAbiFindMethod(uint32_t selector);

/* True only for the exact record sizes the selector defines. */
bool NativeMaliAbiRecordSizesMatch(const NativeMaliAbiMethodRecord *method,
                                   size_t input_bytes, size_t output_bytes);

#endif /* MALIMETAL_NATIVE_MALI_ABI_TABLE_H */
