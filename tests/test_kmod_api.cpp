/* SPDX-License-Identifier: GPL-2.0-only
 *
 * Host test for the C facade the Mesa backend will call. It runs against the
 * same scripted service as the C++ lifecycle test, so the C boundary is checked
 * end to end: create/query, props, alloc/map, submit/wait, free, fault, reset.
 */
#include "native_mali_kmod.h"

#include "fake_native_mali_service.h"

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

int trampoline(void *context, uint32_t selector, const void *input, size_t input_bytes,
               void *output, size_t output_bytes) {
    return static_cast<FakeNativeMaliService *>(context)->call(
        selector, input, input_bytes, output, output_bytes);
}

} /* namespace */

int main() {
    NativeMaliResult error = NATIVE_MALI_RESULT_UNKNOWN;
    expect(native_mali_kmod_create(nullptr, &error) == nullptr, "null transport refused");
    expect(error == NATIVE_MALI_RESULT_INVALID_ARGUMENT, "null transport is invalid argument");

    NativeMaliTransportCallbacks callbacks{};
    callbacks.call = nullptr;
    callbacks.context = nullptr;
    expect(native_mali_kmod_create(&callbacks, &error) == nullptr, "null call refused");
    expect(error == NATIVE_MALI_RESULT_INVALID_ARGUMENT, "null call is invalid argument");

    FakeNativeMaliService service;
    callbacks.call = trampoline;
    callbacks.context = &service;

    NativeMaliKmod *kmod = native_mali_kmod_create(&callbacks, &error);
    expect(kmod != nullptr, "device created");
    expect(error == NATIVE_MALI_RESULT_OK, "create reports OK");

    const NativeMaliDeviceProps *props = native_mali_kmod_props(kmod);
    expect(props != nullptr, "props available");
    expect(props->gpu_id == 0x70021000, "GPU id from GetDeviceInfo");
    expect(props->non_coherent == 1, "non-coherent flag surfaced");
    expect(props->max_bo_bytes == (UINT64_C(1) << NativeMaliAbiMaxBoBytesShift), "BO limit");
    expect(props->va_size == (UINT64_C(1) << 48), "VA size from the device");

    expect(native_mali_kmod_props(nullptr) == nullptr, "props of a null device");

    /* A buffer through the C boundary. */
    constexpr uint64_t kSize = 2 * 4096;
    uint64_t handle = 0;
    expect(native_mali_kmod_alloc(kmod, kSize, &handle) == NATIVE_MALI_RESULT_OK, "alloc");
    expect(handle != 0, "handle is nonzero");

    expect(native_mali_kmod_alloc(kmod, 4097, &handle) == NATIVE_MALI_RESULT_INVALID_ARGUMENT,
           "unaligned size refused");

    uint64_t iova = 0;
    expect(native_mali_kmod_map(kmod, handle, 1u /* read */, &iova) == NATIVE_MALI_RESULT_OK,
           "map");
    expect(iova != 0, "IOVA is nonzero");

    uint32_t token = 0;
    uint64_t cpu_bytes = 0;
    expect(native_mali_kmod_cpu_map(kmod, handle, &token, &cpu_bytes) == NATIVE_MALI_RESULT_OK,
           "cpu map");
    expect(token != 0 && cpu_bytes == kSize, "cpu token and size");
    expect(native_mali_kmod_cpu_unmap(kmod, handle, token) == NATIVE_MALI_RESULT_OK,
           "cpu unmap");

    /* The chain must be inside a mapping; the service enforces it. */
    uint64_t fence = 0;
    expect(native_mali_kmod_submit(kmod, iova + 0x4000, 0, 0, &fence) ==
               NATIVE_MALI_RESULT_INVALID_ARGUMENT,
           "chain outside the mapping refused");
    expect(native_mali_kmod_submit(kmod, iova, 0, 0, &fence) == NATIVE_MALI_RESULT_OK,
           "submit");
    expect(native_mali_kmod_wait(kmod, fence, 0) == NATIVE_MALI_RESULT_OK, "wait");

    expect(native_mali_kmod_free(kmod, handle, 0) == NATIVE_MALI_RESULT_OK, "free");
    expect(native_mali_kmod_free(kmod, handle, 0) == NATIVE_MALI_RESULT_INVALID_ARGUMENT,
           "double free refused");

    /* Fault and recovery through the same facade. */
    expect(native_mali_kmod_refresh_status(kmod) == NATIVE_MALI_RESULT_OK, "status baseline");
    service.faults = 1;
    expect(native_mali_kmod_refresh_status(kmod) == NATIVE_MALI_RESULT_DEVICE_LOST,
           "fault surfaces as device lost");
    expect(native_mali_kmod_refresh_status(kmod) == NATIVE_MALI_RESULT_DEVICE_LOST,
           "still lost before reset");
    expect(native_mali_kmod_reset(kmod) == NATIVE_MALI_RESULT_OK, "reset");
    expect(native_mali_kmod_refresh_status(kmod) == NATIVE_MALI_RESULT_OK, "recovered");

    /* Null device: every call refuses, none crashes. */
    expect(native_mali_kmod_alloc(nullptr, 4096, &handle) ==
               NATIVE_MALI_RESULT_INVALID_ARGUMENT,
           "null device alloc refused");
    expect(native_mali_kmod_wait(nullptr, 1, 0) == NATIVE_MALI_RESULT_INVALID_ARGUMENT,
           "null device wait refused");
    expect(native_mali_kmod_reset(nullptr) == NATIVE_MALI_RESULT_INVALID_ARGUMENT,
           "null device reset refused");

    expect(native_mali_kmod_error_name(NATIVE_MALI_RESULT_DEVICE_LOST) != nullptr, "error name");
    native_mali_kmod_destroy(kmod);

    if (failures != 0) {
        std::fprintf(stderr, "%d check(s) failed\n", failures);
        return 1;
    }
    std::printf("OK   kmod C facade: create, props, buffers, submit/wait, fault, reset\n");
    return 0;
}
