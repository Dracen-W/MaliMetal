# Porting map: what is reused, what is forked

<!-- SPDX-License-Identifier: GPL-2.0-only -->
This document maps every top-level file in `AdrenoMetal-reference/` to a Mali
decision. It is a plan, not a claim that the files are already present.

The map is anchored to AdrenoMetal commit
[`ce5e19e33e05e1873d38d818e23cabfe0ac708c3`](https://github.com/UniXNU/AdrenoMetal/tree/ce5e19e33e05e1873d38d818e23cabfe0ac708c3)
(2026-10-25). The local reference copy was verified byte-for-byte against that
commit on 2026-10-31. Do not apply this map to a newer AdrenoMetal tree without
recording the new revision.

The reference is MIT. Any file copied or derived from it keeps its MIT header
and is recorded in `NOTICE`. Files that are rewritten for Mali get a
`GPL-2.0-only` header and a note naming the reference they were derived from.

## Summary

| Decision | Files |
| --- | --- |
| **Reuse unchanged** (may rename the bundle/private symbols) | `NativeBridgeMetalFrontend.mm`, `NativeBridgeMetalFrontend.h`, `bridge/NativeMetalObjects.h`, `bridge/NativeMetalObjects.rs`, `bridge/NativeMetalBridge.h`, `bridge/NativeMetalBridge.rs`, most `OfflineMetal*`, `HostMetalCommandServer.*`, `NativeOfflineGPU.*`, `OfflineMetalFrontend.mm`, `CacheExport.h`, `PrimeShaders.h`, `ExceptionDiag.h`, `NativeMetalBridgePhase.h`, `NativeOsLogExport.mm` |
| **Fork with Mali changes** | `NativeAdrenoMetalPlugin.mm` → `NativeMaliMetalPlugin.mm`; `GpuScanoutClient.h`, `GpuScanoutPresenter.h`, `NativeIOGPUProviderProbe.cpp` (display/provider paths) |
| **Replace** | `NativeAdrenoUserTest.h`, `NativeTurnipSmoke.h` (Adreno/AGX smoke tests) → Mali ABI smoke tests |
| **Reference only** | `OriginalAcceleratorABI.h`, `OriginalGPUProviderABI.h`, `NativeAdrenoProbeDaemon.cpp` |
| **Do not port** | anything that opens `AGXDeviceUserClient`, names Turnip, or assumes Adreno formats/features |

## File-by-file

### Top level

| File | Decision | Reason / required change |
| --- | --- | --- |
| `NativeAdrenoMetalPlugin.mm` | **fork** | Discovery, publish marker and client allow-list are reusable. Replace the Turnip path lookup with the PanVK ICD path; replace Adreno naming; remove AGX display probing; wire the MT6768 scanout path once known. |
| `NativeBridgeMetalFrontend.mm` | **reuse** | Depends only on `bridge/NativeMetalObjects.h`. Change the log prefix and bundle-private names only. |
| `NativeBridgeMetalFrontend.h` | **reuse** | Same exported C entry points; `loader` now names the PanVK ICD. |
| `NativeMetalBridgePhase.h` | **reuse** | Phase markers; no hardware assumptions. |
| `CacheExport.h` | **reuse** | Shader-cache directory export; independent of the GPU vendor. |
| `PrimeShaders.h` | **reuse** | Pipeline priming helpers; format/feature answers come from the runtime. |
| `ExceptionDiag.h` | **reuse** | Exception logging. |
| `NativeOsLogExport.mm` | **reuse** | `os_log` export. |
| `GpuScanoutClient.h` | **fork / pending** | The shape (map scanout, offer frames, GPU copy, completion) is reusable. The display-driver tunnel and method numbers are not known for MT6768. |
| `GpuScanoutPresenter.h` | **reuse if the scanout contract holds** | Pure presenter over a mapped scanout; verify row order, stride, format and cache behaviour on this display. |
| `NativeIOGPUProviderProbe.cpp` | **fork / pending** | Apple GPU provider probing is not relevant. Keep the idea of a read-only probe, not the AGX code. |
| `NativeAdrenoUserTest.h` | **replace** | Adreno-specific user test. Replace with a `NativeMali` ABI smoke test: Query, GetDeviceInfo, BO alloc/map, WRITE_VALUE submit, fence wait. |
| `NativeTurnipSmoke.h` | **replace** | Turnip/AGX smoke. Replace with a PanVK smoke test that opens the ICD and creates a device. |
| `OfflineMetalFrontend.mm` | **reuse** | Host-side Metal frontend used for wire tests; no GPU. |
| `OfflineMetal*Wire.h` | **reuse** | Offline wire format; no vendor dependency. |
| `HostMetalCommandServer.h/.mm` | **reuse** | Host command server; no GPU. |
| `NativeOfflineGPU.cpp` | **reuse** | Offline GPU model. |
| `NativeOfflineAccelerator.cpp` | **reuse** | Offline accelerator model. |
| `OriginalAcceleratorABI.h` | **reference only** | Apple ABI notes; not a Mali contract. |
| `OriginalGPUProviderABI.h` | **reference only** | Apple GPU provider ABI notes. |
| `NativeAdrenoProbeDaemon.cpp` | **reference only** | Adreno probe daemon. |
| `shaders/` | **reuse if fixtures are vendor-neutral** | Check each shader for Adreno-specific assumptions; otherwise replace the fixture. |
| `tools/` | **reuse where neutral** | `MetalVulkanProbe.rs` and its JSON outputs are useful for comparing Metal/Vulkan answers; re-run against PanVK. |

### `bridge/`

| File | Decision | Reason / required change |
| --- | --- | --- |
| `NativeMetalObjects.h` | **reuse unchanged** | The backend-neutral C ABI. Adding a function is allowed only when the Metal frontend needs it; removing or changing one breaks the frontend. |
| `NativeMetalObjects.rs` | **reuse unchanged** | Wraps the pinned `metal2vulkan` runtime. No Turnip or Adreno code. |
| `NativeMetalBridge.h` | **reuse** | Self-test C ABI. |
| `NativeMetalBridge.rs` | **reuse** | Self-test body. Change the loader argument to the PanVK ICD and the fixture set if needed. |
| `patches/0001..0016` | **re-evaluate** | These are `metal2vulkan` patches, not Turnip patches. Keep the ones that are backend-neutral; check each one against the pinned runtime revision. |
| `*_host.mm`, `*_host.cpp` | **reuse** | Host tests for the frontend/runtime; no GPU. |
| `quartzcore_cache*.cpp`, `quartzcore_cache.mm` | **reuse** | QuartzCore metallib cache handling; the shader content is target-specific but the mechanism is not. |
| `FrontendChecks.h` | **reuse** | Frontend checks. |
| `present_host.mm` | **reuse** | Presentation logic against a fake scanout. |
| `surface_import_host.mm`, `no_copy_import_host.mm` | **reuse as tests** | They document the desired IOSurface no-copy path. The kernel ABI has no import yet, so the device path will copy first. |

## What must be written for Mali

These files have no AdrenoMetal equivalent and start as new work:

| Planned file | Purpose |
| --- | --- |
| `src/backend/native_mali_abi.*` | Userspace client for `NativeMaliABI.h`: method dispatch, record validation, error mapping, BO/VA/fence bookkeeping. Pure logic, host-tested. |
| `src/backend/native_mali_kmod.*` | The `pan_kmod` backend (or the chosen seam) over that client. |
| `src/plugin/NativeMaliMetalPlugin.mm` | Fork of the Adreno plugin. |
| `tests/test_native_mali_abi.cpp` | ABI encode/decode, error mapping, VA allocator, fence lifetime. |
| `tests/test_native_mali_kmod.cpp` | Seam behaviour against a fake service. |
| `docs/NATIVE_MALI_ABI.md` | The kernel ABI subset and its gaps. Already present. |

## Port order

1. Finish the kernel prerequisites in `Mali-G52` (kext, probe, job, fence).
2. Pin `metal2vulkan` and Mesa; record hashes in `docs/DEPENDENCIES.md`.
3. Write and host-test the userspace ABI client (MM2).
4. Choose and implement the seam (MM3). Do not start with the plugin.
5. Bring up PanVK device creation and one job (MM4).
6. Bring up the plugin and publish a Metal device (MM5).
7. Run the Metal self-test (MM6).
8. Only then attempt QuartzCore (MM7) and scanout (MM8).

The order is deliberate: the frontend exposes kernel gaps immediately, so the
kernel must be proven first.

## Anti-patterns

- Copying the whole reference tree into `src/` and renaming symbols. Bring each
  file with its header, record its origin, and change only what Mali requires.
- Keeping an Adreno fallback in the Mali plugin. A failed Mali backend must
  return `nil`, not silently open Turnip.
- Adding a Vulkan type to `NativeBridgeMetalFrontend.mm` to "make it work".
- Putting a register sequence or a direct MMIO mapping in this repository.
- Treating the 26 MHz clock as a temporary detail and building a compositor
  that cannot be measured.
