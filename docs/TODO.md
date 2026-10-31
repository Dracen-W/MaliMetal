<!-- SPDX-License-Identifier: GPL-2.0-only -->
# TODO — deliberately not implemented

Each entry says why it is not written yet. "Not started" is not an oversight; it
is usually waiting on the kernel, on a pinned dependency, or on a human
decision. The milestone order is deliberate: the Metal frontend exposes kernel
gaps immediately, so the kernel must be proven first.

## Milestone summary

| Milestone | State | Exit condition |
| --- | --- | --- |
| MM0 repository, licences, design, porting map | **done** | this scaffold is committed in its own repository |
| MM1 pin dependencies and reproduce a build | not started | `metal2vulkan` and Mesa commits pinned with hashes; a host build of the frontend/runtime exists |
| MM2 userspace NativeMali ABI client | not started | pure encode/decode, BO/VA/fence state machines host-tested |
| MM3 `pan_kmod` seam | not started | chosen seam compiles against the pinned Mesa; fake-service tests pass |
| MM4 PanVK device on the phone | blocked | device creation and one job complete on hardware |
| MM5 plugin publishes a Metal device | blocked | a test process gets a `_MTLDevice` from the bundle |
| MM6 Metal self-test | blocked | compute, render and import pass on hardware |
| MM7 QuartzCore / system UI | blocked | `backboardd` composites through the device |
| MM8 scanout and display | blocked | a frame reaches the panel through the GPU path |

## MM0 — repository scaffold

- [x] Separate git repository with its own `AGENTS.md`, `LICENSE`, `NOTICE`.
- [x] Architecture, porting map, decisions, ABI notes, open questions.
- [x] `.gitignore` excludes every reference copy and dependency checkout.
- [x] State plainly that there is no build and no binary yet.
- [ ] Commit the scaffold and record the exact file list.

## MM1 — pin dependencies

- [ ] Pin `metal2vulkan` to a commit and record the hash and licence in
      `docs/DEPENDENCIES.md`.
- [ ] Pin Mesa to a commit/tag that contains PanVK's JM path and `pan_kmod`.
      Record the commit and the files this port depends on.
- [ ] Record the toolchain (clang, Rust, linker, deployment target).
- [ ] Reproduce the `metal2vulkan` runtime + `NativeMetalObjects.rs` bridge on
      the host, without a GPU, using the reference's build entry point as the
      model.
- [ ] Decide whether the Mesa fork lives as a patch series in `patches/` or as a
      separate fork with a pinned commit. Do not commit a Mesa checkout here.
- [ ] Confirm that the target's PanVK can be built for the target OS and ABI,
      not only for Linux/Android.

## MM2 — userspace NativeMali ABI client

This is pure logic and must be testable without a phone.

- [ ] Method dispatch table for the records in `NativeMaliABI.h` (methods 1–11),
      with exact sizes and untouched output on failure, mirroring
      `NativeMaliUserClient::dispatch()`.
- [ ] Error mapping: `kIOReturn*` → a small internal error enum → Vulkan
      (`VK_ERROR_DEVICE_LOST`, `VK_ERROR_OUT_OF_DEVICE_MEMORY`, ...).
- [ ] BO table: handle, size, IOVA, permissions, CPU token, lifetime.
- [ ] IOVA allocator: alignment, first-fit/buddy, no overlap, no reuse while a
      submitted job may still reference the range. Host-tested.
- [ ] Fence tracker: opaque fences, dependency waits, timeout, reset
      invalidation, `GetStatus::faults/resets` changes.
- [ ] Device-info cache: `gpu_id`, present masks, thread limits, feature words,
      non-coherent flag, VA size.
- [ ] Fake service for tests that answers records from a scripted state, not
      from hardware.
- [ ] Tests: success, short record, unknown version/method/flag, out-of-range
      IOVA, double map, unmap-while-busy, fence from another client, reset
      during wait.

## MM3 — `pan_kmod` seam

Depends on the seam decision in `docs/OPEN_QUESTIONS.md` item 2.

- [ ] Add the chosen backend to the pinned Mesa tree via `patches/`.
- [ ] Replace the JM submission call: a `pan_kmod` submit op instead of
      `pan_kmod_ioctl(DRM_IOCTL_PANFROST_SUBMIT)`, including the BO list,
      requirements and in/out fences.
- [ ] Replace the DRM syncobj use in the JM queue (`drmSyncobjCreate`,
      `drmSyncobjWait`, `drmSyncobjDestroy`, `SYNCOBJ_*` ioctls) with a
      `vk_sync` implementation that can represent a `NativeMali` fence.
- [ ] Device creation without a DRM fd/version; report properties from
      `GetDeviceInfo`, not from `/dev/dri`.
- [ ] BO alloc/free/map/unmap over methods 3–5.
- [ ] CPU map/unmap over methods 6–7, with the correct cacheability.
- [ ] VM create/bind over the client's single address space and IOVAs.
- [ ] Job submission over method 8; fence completion over method 9.
- [ ] Device-lost/reset over methods 10–11; surface `VK_ERROR_DEVICE_LOST`.
- [ ] Sync objects: either a userspace `vk_sync` backend or a documented
      restriction. Do not fake DRM syncobj semantics.
- [ ] BO wait: either a userspace job-fence lookup or a documented kernel gap.
- [ ] Host tests for every translation, including failure injection.
- [ ] A build that refuses to run if the Mesa patch series does not apply
      cleanly to the pinned commit.

## MM4 — PanVK device on the phone

Blocked on the kernel's first real job and a usable clock.

- [ ] PanVK creates a physical device from the Mali ICD.
- [ ] PanVK creates a logical device and a queue.
- [ ] A trivial buffer is allocated, mapped, filled and read back.
- [ ] A trivial compute dispatch or blit completes.
- [ ] The fence path reports completion without polling forever.
- [ ] A forced error is reported as a device loss, not a hang.
- [ ] Record the measured `gpu_id`, `mmu_features`, present masks and memory
      types in `Mali-G52/docs/DEVICE.md` (or a new MaliMetal device note that
      cites it).

## MM5 — plugin and Metal device

- [ ] Fork the discovery/publish logic from `NativeAdrenoMetalPlugin.mm`.
- [ ] Open the PanVK ICD by path; no Khronos loader.
- [ ] Publish only when `NativeMaliMetal.publish` exists; keep the software
      renderer otherwise.
- [ ] Client allow-list for test processes, then `backboardd`.
- [ ] `_MTLDevice` base-class ABI check for the target OS build.
- [ ] Missing-selector hook reports every gap once.
- [ ] Return `nil` on any failure; never publish a partial device.

## MM6 — Metal self-test

- [ ] Compute: 1000-element scale-and-offset, verify every result.
- [ ] Render: draw a known quad into a known target, verify texels.
- [ ] Import: host pages / IOSurface path (copy first if import is unavailable).
- [ ] Translate the target's own QuartzCore metallib into pipelines.
- [ ] Pipeline creation, shader cache hits and misses.
- [ ] Run from inside a real system process under its sandbox.
- [ ] A failed step names the step; no silent fallback.

## MM7 — QuartzCore / system UI

Blocked on MM6 and on the display path.

- [ ] Confirm what QuartzCore asks of the device: formats, features, drawables,
      presentation.
- [ ] Confirm the render server's framebuffer format and whether it must be
      forced to plain 8-bit BGRA (the Adreno reference needed this).
- [ ] Composite a real frame; compare pixels against the software renderer.
- [ ] Measure frame time at the available clock. At 26 MHz this milestone is
      not meaningful; record the actual clock.

## MM8 — scanout and display

- [ ] Discover the UniXNU MT6768 display driver's scanout interface. Do not
      assume the Adreno tunnel exists.
- [ ] Map the scanout safely; respect stride, format, row order and cache
      attributes.
- [ ] GPU copy or direct scanout, with a completion verdict.
- [ ] Fall back to the display driver's own path on failure, and count it.
- [ ] Verify a frame reaches the panel and is not corrupted.
- [ ] Only then claim system UI compositing.

## Deliberately not implemented

- **A custom Metal→Mali translator.** Reuse `metal2vulkan`; write only the
  backend seam.
- **A custom Bifrost compiler.** Reuse Mesa's NIR/Bifrost backend.
- **Turnip.** It is Freedreno/Adreno-only.
- **Direct MMIO or register sequences.** Kernel only.
- **A second kernel ABI.** All hardware access uses `NativeMaliABI.h`.
- **DRM ioctl emulation** unless Option B or C is chosen explicitly.
- **Multi-GPU, multi-display, DVFS.** After the single-display path works.
- **Cross-process shared events.** The reference frontend does not implement
  them; do not promise them.
- **Heaps, argument buffers, indirect commands, drawables.** Inherited gaps
  from the reference frontend; measure before adding.
- **A `Info.plist` or bundle now.** There is no `IOClass` or plugin that could
  match; adding one would only produce a misleading failure log.
