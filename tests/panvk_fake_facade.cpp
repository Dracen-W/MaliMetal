/* SPDX-License-Identifier: GPL-2.0-only
 *
 * Test-only facade image for the PanVK smoke test.
 *
 * PanVK leaves the NativeMali facade calls as flat weak imports.  A separate
 * dylib, loaded RTLD_GLOBAL, is a reliable place for those imports to resolve:
 * it is the same arrangement the target will use for the test process.
 */
#include "native_mali_kmod.h"

#include "fake_native_mali_service.h"

#include <cstdio>

namespace {

FakeNativeMaliService service;

int trampoline(void *context, uint32_t selector, const void *input, size_t input_bytes,
               void *output, size_t output_bytes) {
    std::fprintf(stderr, "fake facade: selector %u\n", selector);
    return static_cast<FakeNativeMaliService *>(context)->call(
        selector, input, input_bytes, output, output_bytes);
}

} /* namespace */

extern "C" NativeMaliKmod *native_mali_kmod_create_default(NativeMaliResult *error) {
    std::fprintf(stderr, "fake facade: create_default\n");
    NativeMaliTransportCallbacks callbacks{};
    callbacks.call = trampoline;
    callbacks.context = &service;
    return native_mali_kmod_create(&callbacks, error);
}
