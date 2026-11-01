/* SPDX-License-Identifier: GPL-2.0-only
 *
 * Method table for the v1 NativeMali ABI. See NativeMaliAbiTable.h.
 */
#include "NativeMaliAbiTable.h"

#include "NativeMaliABI.h"

#include <stddef.h>

namespace {

constexpr uint32_t kBytes(size_t value) { return static_cast<uint32_t>(value); }

/* The table must describe the pinned header, not a remembered layout. If a
 * record changes size, this file fails to compile instead of sending a short
 * record across the boundary. */
static_assert(sizeof(NativeMaliAbiQuery) == 24, "query record");
static_assert(sizeof(NativeMaliAbiDeviceInfo) == 112, "device info record");
static_assert(sizeof(NativeMaliAbiBoAlloc) == 8, "alloc record");
static_assert(sizeof(NativeMaliAbiBoAllocReply) == 8, "alloc reply");
static_assert(sizeof(NativeMaliAbiBoMap) == 24, "map record");
static_assert(sizeof(NativeMaliAbiBoMapReply) == 8, "map reply");
static_assert(sizeof(NativeMaliAbiBoFree) == 8, "free record");
static_assert(sizeof(NativeMaliAbiBoCpuMap) == 8, "cpu map record");
static_assert(sizeof(NativeMaliAbiBoCpuMapReply) == 16, "cpu map reply");
static_assert(sizeof(NativeMaliAbiBoCpuUnmap) == 16, "cpu unmap record");
static_assert(sizeof(NativeMaliAbiSubmit) == 32, "submit record");
static_assert(sizeof(NativeMaliAbiSubmitReply) == 8, "submit reply");
static_assert(sizeof(NativeMaliAbiWaitFence) == 16, "wait record");
static_assert(sizeof(NativeMaliAbiStatus) == 16, "status record");

} /* namespace */

const NativeMaliAbiMethodRecord kNativeMaliAbiMethods[] = {
    {NativeMaliMethodQuery, "Query", kBytes(sizeof(NativeMaliAbiQuery)),
     kBytes(sizeof(NativeMaliAbiQuery))},
    {NativeMaliMethodGetDeviceInfo, "GetDeviceInfo", 0,
     kBytes(sizeof(NativeMaliAbiDeviceInfo))},
    {NativeMaliMethodBoAlloc, "BoAlloc", kBytes(sizeof(NativeMaliAbiBoAlloc)),
     kBytes(sizeof(NativeMaliAbiBoAllocReply))},
    {NativeMaliMethodBoMap, "BoMap", kBytes(sizeof(NativeMaliAbiBoMap)),
     kBytes(sizeof(NativeMaliAbiBoMapReply))},
    {NativeMaliMethodBoFree, "BoFree", kBytes(sizeof(NativeMaliAbiBoFree)), 0},
    {NativeMaliMethodBoCpuMap, "BoCpuMap", kBytes(sizeof(NativeMaliAbiBoCpuMap)),
     kBytes(sizeof(NativeMaliAbiBoCpuMapReply))},
    {NativeMaliMethodBoCpuUnmap, "BoCpuUnmap",
     kBytes(sizeof(NativeMaliAbiBoCpuUnmap)), 0},
    {NativeMaliMethodSubmit, "Submit", kBytes(sizeof(NativeMaliAbiSubmit)),
     kBytes(sizeof(NativeMaliAbiSubmitReply))},
    {NativeMaliMethodWaitFence, "WaitFence", kBytes(sizeof(NativeMaliAbiWaitFence)), 0},
    {NativeMaliMethodGetStatus, "GetStatus", 0, kBytes(sizeof(NativeMaliAbiStatus))},
    {NativeMaliMethodReset, "Reset", 0, 0},
};

const size_t kNativeMaliAbiMethodCount =
    sizeof(kNativeMaliAbiMethods) / sizeof(kNativeMaliAbiMethods[0]);

static_assert(sizeof(kNativeMaliAbiMethods) / sizeof(kNativeMaliAbiMethods[0]) ==
                  NativeMaliMethodLast,
              "the table must have one entry per selector");

const NativeMaliAbiMethodRecord *NativeMaliAbiFindMethod(uint32_t selector) {
    for (size_t index = 0; index < kNativeMaliAbiMethodCount; ++index) {
        if (kNativeMaliAbiMethods[index].selector == selector) {
            return &kNativeMaliAbiMethods[index];
        }
    }
    return nullptr;
}

bool NativeMaliAbiRecordSizesMatch(const NativeMaliAbiMethodRecord *method,
                                   size_t input_bytes, size_t output_bytes) {
    if (method == nullptr) {
        return false;
    }
    return method->input_bytes == input_bytes && method->output_bytes == output_bytes;
}
