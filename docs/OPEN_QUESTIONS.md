<!-- SPDX-License-Identifier: GPL-2.0-only -->
# OPEN QUESTIONS

Following the rule "when unsure, stop": every item below is waiting on a human
answer, a hardware measurement, or a pinned dependency. Do not silently choose
one in code.

## 1. Mesa/PanVK dependency and patch policy — open

The port needs a Mesa fork or a patch series. Neither is approved yet.

Questions:

- Is a pinned Mesa fork acceptable as a build dependency?
- If yes, which commit/tag? It must contain PanVK's JM path, `pan_kmod` and the
  Bifrost v7 compiler.
- Do patches live in `patches/` in this repository (applied to a pristine Mesa
  checkout), or in a separate fork with a pinned commit?
- Who rebases the patches when Mesa moves?
- Is the target build (`iOS`-style userspace, not Linux/Android) supported by
  the chosen Mesa revision's build system? If not, what is the minimum build
  shim?

Until answered: MM3 cannot start. MM1 can record the candidate commit and the
files it depends on.

## 2. The `pan_kmod` seam — open

The three options are in `docs/ARCHITECTURE.md`:

- **A** native `pan_kmod` backend in a Mesa fork;
- **B** a Panfrost-compatible DRM UAPI in the kernel;
- **C** a userspace DRM emulation shim.

Current preference: **A**, because it speaks `NativeMali` directly and avoids a
large DRM implementation. It is not chosen because PanVK also uses the DRM fd
for mmap and syncobj, so the patch surface is larger than a vtable.

Evidence:

- `src/panfrost/vulkan/panvk_physical_device.c:create_kmod_dev()` opens a
  render node, calls `drmGetVersion()` and requires `panfrost`/`panthor`.
- `src/panfrost/lib/kmod/pan_kmod.c:pan_kmod_dev_create()` dispatches on the
  DRM version name.
- `src/panfrost/lib/kmod/pan_kmod.h:pan_kmod_bo_mmap()` uses
  `os_mmap(..., bo->dev->fd, mmap_offset)`.
- `src/panfrost/vulkan/panvk_physical_device.c:get_device_sync_types()` uses
  `vk_drm_syncobj_get_type(device->kmod.dev->fd)`.
- `src/panfrost/lib/kmod/panfrost_kmod.c` uses `DRM_IOCTL_PANFROST_*` for
  parameters, BOs, waits, cache syncs and labels.

Question: approve Option A, or require B/C? If A, approve that the patch series
may touch PanVK's mmap/sync paths, not only `pan_kmod`.

## 3. Kernel ABI gaps for a Vulkan driver — measured, decision needed

`NativeMaliABI.h` is fixed v1. PanVK needs operations the ABI does not expose.
Each one needs either a userspace workaround or a kernel ABI change (in
`Mali-G52`, under its own rules).

| Gap | Why PanVK needs it | Possible direction |
| --- | --- | --- |
| Job `requirements` / affinity | panfrost computes shader-core affinity per job; `NativeMali` uses all present cores | accept "all cores" for the first job; add a field only if correctness requires it |
| Client-chosen flush id | PanVK/Panfrost use a flush id for cache/tiler ordering; `NativeMali` reads `GPU_LATEST_FLUSH_ID` itself | keep kernel-chosen; verify tiler correctness |
| Per-BO wait | `pan_kmod_bo_wait()` waits for a BO to be idle | userspace fence lookup, or a kernel method |
| CPU mmap offset | `pan_kmod_bo_mmap()` uses an fd offset | replace with `BoCpuMap` token mapping in the backend |
| dma-buf / IOSurface import | zero-copy textures and scanout | copy first; add import later (method does not exist) |
| DRM syncobj / timeline | PanVK's sync types | userspace `vk_sync`, or restrict to polled fences |
| Cache maintenance method | non-coherent device | userland cache ops, or a new ABI method |
| One VM / one AS per client | PanVK assumes one VM per device | matches today; document the three-AS limit |
| 4 KiB pages only | 2 MiB blocks are a performance feature | acceptable initially; revisit with benchmarks |
| No interrupt-driven completion | polled fences burn CPU | acceptable for bring-up; not for a compositor |

Question: which of these may become `NativeMaliAbiVersion = 2`, and which must
be worked around in userspace? Do not change the kernel ABI from this
repository.

## 4. PanVK support for Bifrost v7 — risk accepted?

Mesa's own physical-device init refuses v6/v7/v14 unless
`PAN_I_WANT_A_BROKEN_VULKAN_DRIVER=1` is set
(`src/panfrost/vulkan/panvk_physical_device.c:panvk_physical_device_init()`).
The G52 is v7. The JM backend exists, but upstream calls it not well-tested.

Questions:

- Is it acceptable to set that variable for a bring-up build?
- Do we need a local patch that removes the refusal, or is the variable the
  intended escape hatch?
- What is the fallback if PanVK v7 cannot render QuartzCore correctly? (A
  Gallium-based path is not a Metal backend; a different Vulkan driver does not
  exist for this GPU.)

## 5. Kernel readiness — blocked

MaliMetal cannot be validated before `Mali-G52` proves on the phone:

- kext builds and starts;
- read-only probe accepted;
- power/clock/GPU core under software control;
- one WRITE_VALUE job and fence complete;
- the clock is above the current fixed 26 MHz (`clk26m`, lowest vendor OPP).

Question: what is the target clock, and who owns DVFS? A UI compositor at 26 MHz
is not a meaningful target, so MM7 should not be attempted before this changes.

## 6. Display and scanout on MT6768 — unknown

AdrenoMetal's `GpuScanoutClient.h` uses a display-driver tunnel and a GPU copy
into a scanout mapping. Nothing proves that interface exists on this phone under
UniXNU.

Questions:

- Which driver owns the MT6768 display in the UniXNU image?
- Does it expose a scanout mapping, a frame-offer callback, or only a
  framebuffer?
- What is the pixel format, stride and row order?
- Is the scanout mapping cacheable, and who flushes it?
- Is a GPU copy into scanout allowed, or must the display driver own the copy?

Until answered: MM8 is `NotReady`; the plugin may render offscreen only.

## 7. IOSurface, drawables and cross-process events — open

- The Metal frontend has no drawable/presentation implementation in the
  reference. Does QuartzCore require `CAMetalLayer` nextDrawable, or does the
  render server use another path?
- `NativeMali` has no BO import. Is a copy into a BO acceptable for the first
  frame? What is the bandwidth cost at the panel resolution?
- Shared events are process-local in the reference frontend. Does the render
  server need cross-process events?

## 8. Metal feature set and QuartzCore expectations — open

The frontend answers Metal feature queries from the runtime, which answers from
Vulkan. PanVK's v7 feature set is smaller than an A740's. Questions:

- Which Metal families must `supportsFamily:` claim for the target QuartzCore?
- Which formats does the render server require for its framebuffer?
- Does it need wide-gamut/XR10 or compressed framebuffers? The Adreno port had
  to withhold XR10 because the display chain only accepted 8-bit BGRX. The Mali
  display path may impose the same constraint.
- What happens when a required feature is missing: software fallback, a smaller
  format, or a refused device?

## 9. Shader cache and sandbox — open

- Where may the process write the shader cache? AdrenoMetal uses a directory
  passed to `native_bridge_metal_open`.
- Is the render server's sandbox allowed to write there?
- Must the cache be keyed by the PanVK build, the GPU ID and the Metal runtime
  build? Mesa already keys by driver build and `gpu_id`
  (`panvk_physical_device.c:init_shader_caches()`); the bridge adds its own
  layer.

## 10. Licensing and distribution — open

- This repository is GPL-2.0-only. `metal2vulkan` is
  GPL-2.0-or-later OR LGPL-3.0-or-later and is linked unmodified. Mesa is MIT.
- Is the final image allowed to carry a GPL-2.0-only plugin and a linked
  `metal2vulkan`? The owner must confirm the distribution model.
- Do any AdrenoMetal-derived files need a different header because they are
  copied rather than rewritten? Record each one in `NOTICE`.

## 11. Test target — open

- Which OS build / QuartzCore version is the reference target?
- Is there a second device for destructive tests, or is the phone the only one?
- Can a test process be run under the render server's sandbox without
  publishing the device to `backboardd` first?

## 12. Naming — open

The repository is `MaliMetal`; the bundle, exported symbols and publish/client
marker files need final names. Current proposal:

- bundle: `NativeMaliMetal.bundle`
- principal class: `NativeMaliMetalDevice`
- publish marker: `NativeMaliMetal.publish`
- client list: `NativeMaliMetal.clients`
- plugin source: `NativeMaliMetalPlugin.mm`

Confirm before MM5, because the discovery path and the marker files are part of
the deployed layout.
