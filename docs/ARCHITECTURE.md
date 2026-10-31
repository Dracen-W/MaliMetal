# Architecture

<!-- SPDX-License-Identifier: GPL-2.0-only -->
## Goal

Render the iOS system UI with GPU compositing on a Redmi "merlin" (MT6768,
Mali-G52 MC2, Bifrost v7, Job Manager). The target process is `backboardd`, the
process that owns the display and the render server.

The design reuses the Metal frontend that already works on AdrenoMetal and
replaces only the lower layers that are Adreno-specific.

## Layers

```text
 ┌────────────────────────────────────────────────────────────────────┐
 │ QuartzCore, Metal.framework, backboardd                             │
 └───────────────────────────────┬────────────────────────────────────┘
                                 │ Objective-C / Metal
 ┌───────────────────────────────▼────────────────────────────────────┐
 │ NativeMaliMetal.bundle                                               │
 │   plugin discovery, publish marker, client allow-list, scanout      │
 │   fork of NativeAdrenoMetalPlugin.mm                                 │
 └───────────────────────────────┬────────────────────────────────────┘
                                 │ exported C entry points
 ┌───────────────────────────────▼────────────────────────────────────┐
 │ NativeBridgeMetalFrontend.mm                                         │
 │   _MTLDevice subclass, resources, queues, encoders                   │
 │   reused from AdrenoMetal                                            │
 └───────────────────────────────┬────────────────────────────────────┘
                                 │ NativeMetalObjects.h (opaque handles)
 ┌───────────────────────────────▼────────────────────────────────────┐
 │ NativeMetalObjects.rs                                                │
 │   metal2vulkan runtime: Metal API objects over Vulkan                 │
 │   reused from AdrenoMetal, linked with pinned metal2vulkan            │
 └───────────────────────────────┬────────────────────────────────────┘
                                 │ Vulkan API, direct ICD entry points
 ┌───────────────────────────────▼────────────────────────────────────┐
 │ Mesa PanVK (Bifrost v7, JM backend)                                  │
 │   Vulkan driver, Bifrost compiler, format/layout/tiling tables       │
 │   pinned fork with a NativeMali backend patch series                 │
 └───────────────────────────────┬────────────────────────────────────┘
                                 │ pan_kmod seam (the port boundary)
 ┌───────────────────────────────▼────────────────────────────────────┐
 │ NativeMali ABI client                                                │
 │   BOs, explicit IOVAs, job chains, opaque fences, device reset       │
 └───────────────────────────────┬────────────────────────────────────┘
                                 │ IOConnectCallStructMethod / memory types
 ┌───────────────────────────────▼────────────────────────────────────┐
 │ NativeMali.kext                                                      │
 │   registers, ARM_MALI_LPAE, job slots, software fences               │
 └───────────────────────────────┬────────────────────────────────────┘
                                 │ MMIO / interrupts / PMIC / clocks
 ┌───────────────────────────────▼────────────────────────────────────┐
 │ Mali-G52 MC2                                                         │
 └────────────────────────────────────────────────────────────────────┘
```

## Layer responsibilities

### 1. Plugin

Responsibilities:

- answer the original Metal discovery
  (`IOAcceleratorES → MetalPluginName → NSBundle → principal class →
  initWithAcceleratorPort:`);
- decide whether this process is published (`NativeMaliMetal.publish`) or
  listed as a client (`NativeMaliMetal.clients`);
- open the PanVK ICD and pass its path plus a writable shader-cache directory
  to the runtime;
- expose the one Metal device through the target's `_MTLDevice` base class;
- hand the display driver's frames to the GPU scanout path, once that path is
  known.

This is the only file that must be written specifically for Mali at the top of
the stack. It is a fork of `NativeAdrenoMetalPlugin.mm`; the discovery and
publish logic is reusable, the Turnip path lookup and Adreno/AGX display probing
are not.

### 2. Metal frontend

`NativeBridgeMetalFrontend.mm` is a large Objective-C++ implementation of the
Metal object model over an opaque C ABI. It contains no Vulkan calls and no
Adreno calls; it only calls `nmb_*` functions from `NativeMetalObjects.h`.

This is the strongest reuse boundary in the project. The frontend can be taken
with its MIT header, its selector-missing hook and its telemetry, and only the
log prefix and the bundle's private names need to change.

Deliberately not implemented in the reference frontend, and therefore not
promised here: heaps, argument buffers, indirect commands, drawables and
cross-process shared events. Those gaps are inherited until measured work says
otherwise.

### 3. Runtime

`bridge/NativeMetalObjects.rs` wraps the pinned `metal2vulkan` runtime and
exposes Metal objects as boxed opaque handles. `bridge/NativeMetalBridge.rs`
contains the self-test and the fixture path.

The runtime is linked unmodified from its own source. Nothing of it is copied
into this repository. Its `DeviceOptions.loader` is a path to a library
exporting `vkGetInstanceProcAddr`; on this target that becomes the PanVK ICD.

### 4. Vulkan driver

Mesa PanVK supplies:

- the Bifrost v7 compiler backend (NIR → Bifrost);
- the job-manager command stream;
- format, tiling, AFBC and sampler tables;
- the Vulkan feature set that the Metal frontend answers from.

The device is `gpu_id 0x70021000` (product `0x7002`, revision `0x1000`, r1p0),
which is `pan_arch == 7`. PanVK's own physical-device init currently refuses
v6/v7/v14 unless `PAN_I_WANT_A_BROKEN_VULKAN_DRIVER=1` is set
(`src/panfrost/vulkan/panvk_physical_device.c:panvk_physical_device_init()`).
That is a measured upstream statement, not a local preference, and it is an
open risk. See `docs/OPEN_QUESTIONS.md`.

### 5. `pan_kmod` seam

This is the actual port. Mesa's `pan_kmod` is a driver-agnostic abstraction over
a **Linux DRM** kernel driver:

- `pan_kmod_dev_create()` reads the DRM version name and dispatches to
  `panfrost_kmod_ops` or `panthor_kmod_ops`
  (`src/panfrost/lib/kmod/pan_kmod.c:pan_kmod_dev_create()`);
- `panfrost_kmod.c` issues `DRM_IOCTL_PANFROST_*` for parameters, BO creation,
  mapping offsets, waits, cache syncs and labels;
- `pan_kmod_bo_mmap()` calls `os_mmap(..., bo->dev->fd, mmap_offset)`
  (`src/panfrost/lib/kmod/pan_kmod.h:pan_kmod_bo_mmap()`);
- PanVK queries DRM syncobj features from the same fd
  (`src/panfrost/vulkan/panvk_physical_device.c:get_device_sync_types()`);
- PanVK opens `/dev/dri/renderD*` and checks for `panfrost`/`panthor`
  (`src/panfrost/vulkan/panvk_physical_device.c:create_kmod_dev()`).

`NativeMali` has none of those: no DRM fd, no ioctl namespace, no GEM handles,
no dma-buf, no DRM syncobj, no `mmap()` offset. It has fixed-size external
method records, explicit IOVAs, `clientMemoryForType` mappings and polled
opaque fences.

A `pan_kmod` vtable alone is therefore **not sufficient**. The seam must also
abstract the fd-based mmap and sync paths. The options are compared below.

#### What PanVK actually calls

The Job Manager path does not go through `pan_kmod` for submission. It calls
libdrm/ioctls directly:

- `src/panfrost/vulkan/jm/panvk_vX_gpu_queue.c:panvk_queue_submit_batch()`
  fills a `struct drm_panfrost_submit` (`bo_handles`, `in_syncs`, `out_sync`,
  `jc`, `requirements`) and calls
  `pan_kmod_ioctl(dev->drm_fd, DRM_IOCTL_PANFROST_SUBMIT, &submit)`.
- The same file creates and manipulates DRM syncobjs:
  `drmSyncobjCreate()`, `drmSyncobjWait()`, `drmSyncobjDestroy()`,
  `DRM_IOCTL_SYNCOBJ_HANDLE_TO_FD`, `DRM_IOCTL_SYNCOBJ_FD_TO_HANDLE` and
  `DRM_IOCTL_SYNCOBJ_RESET`.
- `src/panfrost/vulkan/jm/panvk_vX_gpu_queue.c:panvk_per_arch(gpu_queue_submit)()`
  asserts that every wait/signal is a DRM syncobj
  (`vk_sync_type_is_drm_syncobj()` / `vk_sync_as_drm_syncobj()`).
- The job chain itself is built before this point and is already the Bifrost v7
  chain that `NativeMali::Submit` expects. The gap is the **submission and
  synchronization contract**, not the command stream.

So Option A is a patch series that must replace at least:

| Mesa file | Why |
| --- | --- |
| `src/panfrost/vulkan/panvk_physical_device.c` | device creation, DRM version check, sync types |
| `src/panfrost/lib/kmod/pan_kmod.{h,c}` | device creation without a DRM fd; mmap/wait abstraction |
| `src/panfrost/lib/kmod/panfrost_kmod.c` | backend over `NativeMali` (or a new file) |
| `src/panfrost/vulkan/jm/panvk_vX_gpu_queue.c` | replace `DRM_IOCTL_PANFROST_SUBMIT` and DRM syncobjs |
| `src/vulkan/runtime/vk_sync*` | a non-DRM sync object type for the XNU target |

This is a larger patch surface than "add one `pan_kmod` backend". It is still
smaller than implementing the Panfrost DRM UAPI in the kernel (Option B), but
the estimate must include it. The file list above matches the pinned Mesa tag
`mesa-26.2.6` (`8e87a503`); the per-file hashes are in `deps/deps.toml` and
`docs/DEPENDENCIES.md`, and MM3 must re-verify them before applying a patch.

### 6. NativeMali ABI

The canonical header is `Mali-G52/NativeMaliABI.h`. MaliMetal consumes a subset:

| Method | Use in PanVK |
| --- | --- |
| `Query` (1) | address-space geometry, max BO size |
| `GetDeviceInfo` (2) | GPU ID, present masks, thread limits, feature words |
| `BoAlloc` (3) | `pan_kmod_bo_alloc` |
| `BoMap` (4) | `pan_kmod_vm_bind` map; caller supplies the IOVA |
| `BoFree` (5) | BO destruction |
| `BoCpuMap` / `BoCpuUnmap` (6, 7) | CPU mapping, replacing an mmap offset |
| `Submit` (8) | one Bifrost job chain; returns an opaque fence |
| `WaitFence` (9) | completion and dependency waits |
| `GetStatus` (10) | fault/reset counters, context-loss detection |
| `Reset` (11) | device-wide recovery after a fault |

What the ABI deliberately does **not** provide, and what it costs PanVK, is
listed in `docs/NATIVE_MALI_ABI.md`. The important ones are: no
`requirements`/affinity field, no client-chosen flush id, no per-BO wait, no
dma-buf/IOSurface import, no syncobj/timeline, no cache-maintenance method, one
VM/address space per open client, and three address spaces in total.

### 7. Kernel

No MaliMetal code runs in the kernel. The kernel's own rules — source-cited
registers, read-before-write, host tests first, fail closed — remain in
`Mali-G52/AGENTS.md`.

## Data flow

### Device open

```text
backboardd
  → Metal discovery
  → NativeMaliMetal.bundle principal class
  → native_bridge_metal_open(panvk_icd_path, cache_dir, registryID, missing_hook)
  → metal2vulkan Device::system_default_with(options)
  → PanVK instance / physical device
  → NativeMali open + Query + GetDeviceInfo
```

If any step fails, the plugin returns `nil` and the original software renderer
keeps running. It must not publish a half-initialised device.

### Buffer allocation and mapping

```text
Metal buffer
  → metal2vulkan Buffer
  → VkDeviceMemory / VkBuffer
  → PanVK BO
  → native backend: NativeMali BoAlloc(bytes)
  → native backend: choose an IOVA
  → NativeMali BoMap(handle, iova, permissions)
  → CPU view: NativeMali BoCpuMap(handle) → memory-type token
```

PanVK normally lets the kernel assign the VA. `NativeMali` requires the caller
to choose it, so the native backend needs a userspace IOVA allocator scoped to
the client's address space. The allocator must be host-tested: first-fit or
buddy, alignment, no overlap, no reuse while a job may still reference the
range.

### Command submission

```text
Metal command buffer commit
  → metal2vulkan command buffer
  → vkQueueSubmit
  → PanVK JM job chain (Bifrost v7 headers)
  → NativeMali Submit(job_chain, wait_fence, timeout)
  → job slot chosen by the kernel; opaque fence returned
  → WaitFence(fence, timeout)
```

PanVK's job-manager path already builds the same kind of job chain that
`NativeMaliJob` packs. The kernel currently drives affinity with all present
shader cores and reads the flush id itself. That is acceptable for the first
job but may be insufficient for correct IDVS/tiler behaviour; see
`docs/NATIVE_MALI_ABI.md`.

### Presentation

```text
QuartzCore drawable / IOSurface
  → Metal texture (currently: copy into a BO)
  → GPU render or blit
  → scanout copy or direct scanout
  → display driver
```

The last step is the least known part. AdrenoMetal uses a display-driver tunnel
and a GPU copy into a scanout mapping (`GpuScanoutClient.h`,
`GpuScanoutPresenter.h`). The MT6768 display driver under UniXNU may have a
different interface. Do not assume the Adreno path exists; record what the
display driver actually offers.

## Seam options

Three ways to put PanVK on `NativeMali`. None is chosen yet.

### Option A — native `pan_kmod` backend inside a pinned Mesa fork

Add a `native_mali` backend implementing `struct pan_kmod_ops`, plus the
minimum extra abstraction for:

- device creation without a DRM fd/version;
- CPU mapping without an fd offset;
- sync objects without DRM syncobj;
- BO waits without `DRM_IOCTL_PANFROST_WAIT_BO`.

Advantages:

- speaks the `NativeMali` ABI directly;
- no fake DRM, no kernel ioctl emulation;
- the patch surface is visible and reviewable;
- the existing `pan_kmod` vtable is the natural starting point.

Costs:

- PanVK reaches around the vtable for job submission and syncobjs (see "What
  PanVK actually calls" above), so this is a Mesa patch series, not a single
  new file;
- the JM queue and the Vulkan sync layer must both be touched, not only
  `pan_kmod`;
- every Mesa update needs a rebase;
- the backend must reproduce panfrost's implicit VA assignment in userspace;
- the patch series must be tested against a fake service before it ever reaches
  the phone.

### Option B — Panfrost-compatible kernel UAPI

Make the kernel side look enough like `panfrost` DRM that upstream PanVK runs
unmodified: DRM version query, GEM handles, `CREATE_BO`, `MMAP_BO`,
`GET_BO_OFFSET`, `SUBMIT`, `WAIT_BO`, syncobj, dma-buf.

Advantages:

- upstream Mesa and PanVK stay unmodified;
- the contract is well documented by the kernel UAPI.

Costs:

- this is a large kernel project (DRM object model, syncobj, dma-buf) that the
  current `NativeMali` design explicitly avoided;
- it duplicates work the native ABI already does;
- it is hard to justify before the native ABI has run a single job.

### Option C — a userspace DRM emulation shim

Present a fake fd and translate `ioctl()`/`mmap()` calls into `NativeMali`
external methods.

Advantages:

- upstream PanVK sees the interface it expects.

Costs:

- XNU has no `/dev/dri`, no DRM major, and no straightforward way to make a
  userspace fd answer `ioctl()` with DRM semantics;
- `mmap()` on that fd still has to be intercepted;
- the shim becomes a second ABI with worse observability than a Mesa backend.

### Chosen: Option A

Option A is the recorded decision (2026-10-31). It must not be started before
the `NativeMali` ABI has run a real job on the phone. The direct
`DRM_IOCTL_PANFROST_SUBMIT` and `drmSyncobj*` calls in the JM queue mean that
Option A is a real Mesa patch series, not a drop-in backend. Option B is the
fallback if the owner will not maintain that series; Option C is rejected.
The technical patch plan is in `docs/SEAM.md`.

## Memory and coherency

`NativeMali` reports `NativeMaliDeviceNonCoherent`. The CPU must clean and
invalidate around GPU use. PanVK already has a non-coherent path:
`pan_kmod_queue_bo_map_sync()` and
`pan_kmod_flush_bo_map_syncs()` choose userland cache operations when available,
otherwise a kernel ioctl. On XNU the kernel ioctl does not exist, so the native
backend must either:

- map BOs non-cacheable (safe, slower), or
- use the target's `sys_dcache_flush` / `dc cvac` / `dc ivac` equivalent and
  record exactly which cache maintenance the mapping requires.

Do not claim coherency the kernel does not provide. `is_io_coherent` must stay
false unless the kernel and the mapping attributes prove otherwise.

## Failure model

- A refused Vulkan call becomes a refused Metal call with the runtime's reason;
  it is not silently replaced by a default.
- A failed command buffer reports `status == Error`, runs its handlers, and
  does not pretend the work completed.
- An ABI error is mapped to the closest Vulkan error and logged once with the
  method number and the record size.
- A GPU reset destroys in-flight fences. `WaitFence` returns an error fence and
  `GetStatus::resets` advances. PanVK's device-lost path must surface
  this as `VK_ERROR_DEVICE_LOST`; the Metal frontend then fails the command
  buffer rather than hanging.
- A partial bring-up never publishes a device.

## Non-goals

- A custom Metal→Mali translator.
- A custom Bifrost compiler.
- A Turnip fork.
- Direct MMIO from userspace.
- A second kernel ABI hidden in this repository.
- Multi-GPU or multi-display support before the single-display path works.
- DVFS. The kernel owns the fixed clock; MaliMetal does not change it.
