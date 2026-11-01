<!-- SPDX-License-Identifier: GPL-2.0-only -->
# Mesa integration points

What the patch series changes, what an ICD build must call, and what is still
DRM-shaped. Read `docs/SEAM.md` for the design and `patches/README.md` for the
series itself.

Status: **compiled, never run**. `mesa-patch` builds the kmod unit and
`mesa-panvk` builds `libvulkan_panfrost.so` with the series applied. Neither
job runs a driver.

## The series

| Patch | Hooks | Native behaviour |
| --- | --- | --- |
| `0001` | `pan_kmod.c`, `pan_kmod.h` | `pan_kmod_dev_create_native()`: create from ops without a DRM fd (`fd = -1`) |
| `0002` | `kmod/pan_kmod_native_mali.{c,h}`, meson | a `pan_kmod` backend over the NativeMali C facade; refuses creation while `mmu_features == 0`; BO alloc/free and AUTO_VA VM maps |
| `0003` | `pan_kmod.h`, the backend | `pan_kmod_submit` + `submit`/`wait_fence` ops |
| `0004` | `jm/panvk_queue.h`, `jm/panvk_vX_gpu_queue.c` | a queue that submits through the ops when they exist; skips `drmSyncobjCreate`; refuses semaphores/events |

## What an ICD build must do

1. Open the `NativeMali` service (IOKit) and build a `NativeMaliKmod` with
   `native_mali_kmod_create()`.
2. Create the device with `pan_kmod_dev_create_native_mali(kmod, allocator)`.
   It fails closed if the probe has not filled `mmu_features`, if the page
   shift is not 12, or if no shader is present.
3. Create queues normally. `create_gpu_queue()` detects `ops->submit` and
   installs the native path; no DRM fd is involved.
4. Submit chains built by PanVK as usual. The first (vertex/tiler) job becomes
   the dependency of the fragment job; `QueueWaitIdle` waits on the last
   opaque fence.

Limits the ICD must surface, not hide:

- **Binary semaphores and Vulkan events are refused** (`VK_ERROR_FEATURE_NOT_PRESENT`)
  until a native `vk_sync` type exists (patch 0006). A compositor uses both.
- **No per-BO wait**, no mmap offset, no import/export; `bo_wait` returns false
  rather than claiming readiness.
- **AUTO_VA only**: the facade's allocator picks the IOVA; explicit VA requests
  are refused.
- **Device-lost detection** comes from `GetStatus` via the facade; the queue
  reports `VK_ERROR_DEVICE_LOST` on submit/wait failure.

## Why the device path and the sync type are one change

Splitting "create a device without `/dev/dri`" (0005) from "a native `vk_sync`"
(0006) does not work. `panvk_physical_device_init()` is not just enumeration:

- `create_kmod_dev()` opens a render node and reads its DRM version;
- `get_device_sync_types()` calls `vk_drm_syncobj_get_type(dev->fd)` and treats
  a zero result as `VK_ERROR_INITIALIZATION_FAILED`. A native device has no
  syncobj type, so it must supply a `vk_sync_type` here;
- the memory budget is keyed by `device->drm.render_rdev` (`get_memory_budget`);
- `panvk_wsi_init()` and `panvk_wsi_finish()` assume DRM display objects.

So 0005 and 0006 have to land together as one "native physical device" patch
that also skips the DRM memory budget and WSI. Two smaller consequences:

- the instance-level hook itself is cheap: `vk_instance` already exposes
  `instance->vk.physical_devices.enumerate`, so no Vulkan runtime change is
  needed;
- the native `vk_sync_type` can only support **queue-signaled** semaphores.
  `vkSignalSemaphore` and timeline values need a userspace-signalable fence,
  which the v1/v2 ABI does not have (`Submit` produces, `WaitFence` consumes).
  That is an ABI v3 question for `Mali-G52`, recorded in `docs/OPEN_QUESTIONS.md`
  here and in the kernel repository.

## Still DRM-shaped in Mesa

| File | Why it matters |
| --- | --- |
| `vulkan/panvk_physical_device.c` | enumerates DRM render nodes with `drmGetDevices2`, opens one and `drmGetVersion`; needs a native enumeration path (patch 0005) |
| `vulkan/panvk_physical_device.c` | `get_device_sync_types()` requires a DRM syncobj type; the memory budget is keyed by `render_rdev`; `panvk_wsi_init()` assumes DRM display | 
| `vulkan/jm/panvk_vX_gpu_queue.c` | the DRM branch still creates syncobjs and fills `drm_panfrost_submit`; semaphores/events map onto them (patch 0006) |
| `vulkan/panvk_event.*` | events are DRM syncobjs |
| `lib/kmod/panfrost_kmod.c` | untouched by design: it remains the DRM backend |

## Kernel side

The ABI these patches consume is `Mali-G52/NativeMaliABI.h` v2, pinned by
`deps/abi.sha256`. The kernel integration points (XNU glue, personalities, ADT,
build) are listed in `Mali-G52/docs/KEXT_BUILD.md`; this repository never
modifies that repository.

## Build notes

- `mesa-patch` uses `-Dtools=panfrost` and builds `libpankmod_lib.a`; it does
  not need LLVM.
- `mesa-panvk` builds SPIRV-LLVM-Translator and SPIRV-Tools from source, then
  configures `-Dvulkan-drivers=panfrost` and builds `libvulkan_panfrost.so`.
- Both pass `-Dc_args=-I<this repo>/src/backend` so the backend sees
  `native_mali_kmod.h`.
