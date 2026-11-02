/* SPDX-License-Identifier: GPL-2.0-only
 *
 * The C interface the future Mesa backend calls.
 *
 * Mesa is C/C++ and must not see C++ classes, IOKit or XNU. This file is the
 * one place where the userspace NativeMali stack is exposed as plain C:
 * a transport made of one function pointer, an opaque device, and calls that
 * mirror the ABI methods one for one.
 *
 * It is deliberately smaller than pan_kmod: it describes what the ABI can do,
 * not what a particular Mesa revision wants. The Mesa patch maps its ops onto
 * these calls; the mapping is documented in docs/SEAM.md.
 *
 * Ownership: native_mali_kmod_create() returns a device the caller destroys
 * with native_mali_kmod_destroy(). Buffers belong to the device; destroying it
 * does not free kernel buffers behind the caller's back, it just drops the
 * userspace state (the service frees them when the client closes).
 *
 * Errors are a fixed enum; a call that fails leaves every output parameter
 * untouched, exactly like the ABI itself.
 */
#ifndef MALIMETAL_NATIVE_MALI_KMOD_H
#define MALIMETAL_NATIVE_MALI_KMOD_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Mesa compiles the pan_kmod backend without linking MaliMetal's C++ facade;
 * it defines MALIMETAL_KMOD_WEAK so a link-time check can still produce the
 * driver and a missing facade fails at runtime instead of at link time. The
 * real device build defines the strong symbols in this file. */
#ifndef MALIMETAL_KMOD_LINKAGE
#if (defined(MALIMETAL_KMOD_WEAK) || defined(PACKAGE_VERSION)) && defined(__GNUC__)
#define MALIMETAL_KMOD_LINKAGE __attribute__((weak))
#else
#define MALIMETAL_KMOD_LINKAGE
#endif
#endif

typedef struct NativeMaliKmod NativeMaliKmod;

/* Same values as NativeMaliBoRead/Write in the kernel ABI, restated so a
 * consumer of this header does not also need NativeMaliABI.h. */
enum {
    NATIVE_MALI_PERMISSION_READ = 1u << 0,
    NATIVE_MALI_PERMISSION_WRITE = 1u << 1,
};

typedef enum NativeMaliResult {
    NATIVE_MALI_RESULT_OK = 0,
    NATIVE_MALI_RESULT_INVALID_ARGUMENT,
    NATIVE_MALI_RESULT_OUT_OF_MEMORY,
    NATIVE_MALI_RESULT_BUSY,
    NATIVE_MALI_RESULT_NOT_READY,
    NATIVE_MALI_RESULT_UNSUPPORTED,
    NATIVE_MALI_RESULT_ABORTED,
    NATIVE_MALI_RESULT_DEVICE_LOST,
    NATIVE_MALI_RESULT_IO_ERROR,
    NATIVE_MALI_RESULT_UNKNOWN,
} NativeMaliResult;

/* One record pair across the boundary. Return 0 on success. The implementation
 * must not write `output` when it returns an error. */
typedef struct NativeMaliTransportCallbacks {
    int (*call)(void *context, uint32_t selector, const void *input, size_t input_bytes,
                void *output, size_t output_bytes);
    void *context;
} NativeMaliTransportCallbacks;

/* The device facts the backend needs, copied out of GetDeviceInfo once. */
typedef struct NativeMaliDeviceProps {
    uint32_t gpu_id;
    uint32_t shader_present_lo;
    uint32_t shader_present_hi;
    uint32_t tiler_present_lo;
    uint32_t tiler_present_hi;
    uint32_t thread_max_threads;
    uint32_t max_workgroup_size;
    uint32_t max_barrier_size;
    uint32_t l2_features;
    uint32_t core_features;
    uint32_t tiler_features;
    uint32_t non_coherent;
    uint64_t va_start;
    uint64_t va_size;
    uint64_t max_bo_bytes;

    /* v2: raw feature words a pan_kmod backend needs, plus the driver's
     * capability constants. Zero means the kernel probe did not answer. */
    uint32_t mem_features;
    uint32_t mmu_features;
    uint32_t afbc_features;
    uint32_t texture_features[4];
    uint32_t max_tasks_per_core;
    uint32_t registers_per_core;
    uint32_t tls_instances_per_core;
    uint32_t pgsize_shift;
    uint32_t vm_op_flags;
} NativeMaliDeviceProps;

/* Open the device. On success returns a device and sets *error to OK. On
 * failure returns NULL and sets *error (never above UNSUPPORTED for a layout
 * mismatch, never OK). Query and GetDeviceInfo are performed here; a partial
 * device is not returned.
 *
 * The IOVA allocator starts at the first page-aligned, nonzero address at or
 * above props.va_start, because the ABI refuses IOVA 0. */
NativeMaliKmod *native_mali_kmod_create(const NativeMaliTransportCallbacks *transport,
                                        NativeMaliResult *error) MALIMETAL_KMOD_LINKAGE;

/* The phone path: open the NativeMaliGpuService IOKit service and use its
 * IOConnect transport. Returns NULL with UNSUPPORTED on a host without the
 * service, and never guesses a transport. */
NativeMaliKmod *native_mali_kmod_create_default(NativeMaliResult *error) MALIMETAL_KMOD_LINKAGE;

void native_mali_kmod_destroy(NativeMaliKmod *kmod) MALIMETAL_KMOD_LINKAGE;

/* Valid until destroy. Never NULL for a live device. */
const NativeMaliDeviceProps *native_mali_kmod_props(const NativeMaliKmod *kmod) MALIMETAL_KMOD_LINKAGE;

const char *native_mali_kmod_error_name(NativeMaliResult result) MALIMETAL_KMOD_LINKAGE;

/* Buffers. `bytes` must be a nonzero multiple of 4096 and at most
 * props->max_bo_bytes. */
NativeMaliResult native_mali_kmod_alloc(NativeMaliKmod *kmod, uint64_t bytes, uint64_t *handle) MALIMETAL_KMOD_LINKAGE;
NativeMaliResult native_mali_kmod_map(NativeMaliKmod *kmod, uint64_t handle, uint32_t permissions,
                                      uint64_t *iova) MALIMETAL_KMOD_LINKAGE;
/* `guard_fence` is 0 when no submitted job can still reference the buffer. */
NativeMaliResult native_mali_kmod_free(NativeMaliKmod *kmod, uint64_t handle,
                                       uint64_t guard_fence) MALIMETAL_KMOD_LINKAGE;
NativeMaliResult native_mali_kmod_cpu_map(NativeMaliKmod *kmod, uint64_t handle, uint32_t *token,
                                          uint64_t *bytes) MALIMETAL_KMOD_LINKAGE;
/* The same call, plus the userspace address IOKit maps for the token. Only the
 * default IOKit transport can provide an address; the callback transport
 * returns UNSUPPORTED. The returned address is unmapped with
 * native_mali_kmod_cpu_address_unmap(), not os_munmap(). */
NativeMaliResult native_mali_kmod_cpu_map_address(NativeMaliKmod *kmod, uint64_t handle,
                                                  uint32_t *token, void **address,
                                                  uint64_t *bytes) MALIMETAL_KMOD_LINKAGE;
NativeMaliResult native_mali_kmod_cpu_unmap(NativeMaliKmod *kmod, uint64_t handle,
                                            uint32_t token) MALIMETAL_KMOD_LINKAGE;
NativeMaliResult native_mali_kmod_cpu_address_unmap(NativeMaliKmod *kmod, uint32_t token,
                                                    void *address) MALIMETAL_KMOD_LINKAGE;

/* `chain` is a GPU address in a buffer this device mapped, 64-byte aligned. */
NativeMaliResult native_mali_kmod_submit(NativeMaliKmod *kmod, uint64_t chain,
                                         uint64_t wait_fence, uint64_t wait_timeout_ns,
                                         uint64_t *fence) MALIMETAL_KMOD_LINKAGE;
NativeMaliResult native_mali_kmod_wait(NativeMaliKmod *kmod, uint64_t fence,
                                       uint64_t timeout_ns) MALIMETAL_KMOD_LINKAGE;

/* Compare GetStatus with the previous reading. A change loses pending fences and
 * reports DEVICE_LOST until reset() succeeds. The first call is a baseline. */
NativeMaliResult native_mali_kmod_refresh_status(NativeMaliKmod *kmod) MALIMETAL_KMOD_LINKAGE;
NativeMaliResult native_mali_kmod_reset(NativeMaliKmod *kmod) MALIMETAL_KMOD_LINKAGE;

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* MALIMETAL_NATIVE_MALI_KMOD_H */
