# MaliMetal

Metal frontend for the MediaTek MT6768 **Mali-G52 MC2** (Bifrost v7, Job
Manager), over `metal2vulkan` and Mesa **PanVK**, on the `NativeMali` kernel
ABI.

This repository is **preparatory**. It contains the design, the porting map and
the milestone plan; it does not yet contain a buildable Metal implementation.
The kernel driver lives in the separate `Mali-G52` repository and remains the
critical path.

The eventual goal is not a benchmark or a demo: it is GPU compositing for the
iOS system UI (`QuartzCore` / `backboardd`) on this phone, in the same shape as
AdrenoMetal does on the Adreno 740.

Licence: GPL-2.0-only (see `LICENSE` and `NOTICE`).

## What this is, and what it is not

It **is** a userspace port:

```text
backboardd / QuartzCore
        |
        v
Metal.framework  (the target's own API surface)
        |
        v
NativeMaliMetal.bundle        the plugin: discovery, publish, scanout hand-off
        |
        v
NativeBridgeMetalFrontend.mm  Metal objects over a C ABI
        |
        v
NativeMetalObjects.rs         metal2vulkan runtime
        |
        v
Vulkan ICD                    Mesa PanVK (Bifrost v7 / JM backend)
        |
        v
pan_kmod                      Mesa's kernel-module abstraction
        |
        v
NativeMali ABI                fixed-size records, external methods 1..11
        |
        v
NativeMali.kext               registers, MMU, job slots, fences
```

It is **not**:

- a kernel driver; all register work stays in `Mali-G52`;
- a Metal-to-Mali command-stream translator written from scratch;
- a Bifrost shader compiler written from scratch;
- a Turnip port (Turnip is Freedreno/Adreno-only);
- a DRM implementation hidden in the kernel;
- a copy of AdrenoMetal with names changed.

## Credit

This port is only possible because of **AdrenoMetal** by the UniXNU project:

- Official repository: <https://github.com/UniXNU/AdrenoMetal>
- Licence: MIT, Copyright (c) 2026 NativeMetal contributors
- Architecture and reusable frontend: the Metal API frontend
  (`NativeBridgeMetalFrontend.mm`), the Metal-object C ABI
  (`NativeMetalObjects.h/.rs`), the `metal2vulkan` integration, the offline wire
  model, the host-test style and the plugin discovery design are all
  AdrenoMetal's work, not ours.
- Our contribution is the Mali-specific port: the PanVK backend seam, the
  `NativeMali` ABI client, the MT6768 plugin/scanout path and the tests for
  those pieces.

A local reference copy lives in `AdrenoMetal-reference/` for development only.
It is gitignored and is never committed, built into the product, or presented
as part of this repository. Every derived file keeps the MIT header and names
its origin; the full attribution is in `NOTICE`.

## Why AdrenoMetal cannot be used unchanged

AdrenoMetal is the right architectural model, not a drop-in dependency:

| AdrenoMetal part | Why it does not carry over |
| --- | --- |
| Turnip as the Vulkan ICD | Turnip is Freedreno/Adreno-only; the G52 needs PanVK |
| `AGXDeviceUserClient` / AGX provider paths | Apple GPU provider ABI; MT6768 exposes `NativeMali` |
| Adreno-specific scanout and display probing | the MT6768 display/scanout path under UniXNU is not known yet |
| A740 feature answers and format tables | G52 feature set, AFBC/tiling and limits differ |
| Adreno smoke tests | must exercise `NativeMali` method numbers, not AGX ioctls |

The reusable part is the **Metal frontend and the `metal2vulkan` runtime**. The
Mali-specific part is the Vulkan backend: PanVK over a `pan_kmod` seam that
speaks `NativeMaliABI.h`.

## Current state

| Area | State |
| --- | --- |
| Repository scaffold, design, porting map | done |
| Dependency pin (metal2vulkan, Mesa/PanVK) | open |
| `pan_kmod` backend seam | open; options in `docs/ARCHITECTURE.md` |
| Userspace `NativeMali` ABI client | not started |
| PanVK device creation on `NativeMali` | blocked on kernel and seam |
| Metal plugin / device publish | blocked on PanVK |
| Metal self-test (compute, render, import) | blocked on device creation |
| QuartzCore / system UI | blocked on the above and on display scanout |
| Display scanout path | unknown for MT6768 under UniXNU |

Do not expect a binary, a bundle or an installable artifact yet.

## Milestones

The full checklists are in `docs/TODO.md`. The short form:

1. **MM0** — repository, licences, design, porting map.
2. **MM1** — pin `metal2vulkan` and a Mesa commit; reproduce the dependencies
   without hardware.
3. **MM2** — a pure, host-tested userspace `NativeMali` ABI client.
4. **MM3** — a Mesa `pan_kmod` backend over that client (the seam decision).
5. **MM4** — PanVK creates a device and runs a trivial job on the phone.
6. **MM5** — plugin publishes a Metal device to a test process.
7. **MM6** — Metal compute/render/import self-test passes on the phone.
8. **MM7** — QuartzCore / `backboardd` renders through it.
9. **MM8** — display scanout and the system UI frame path.

Kernel prerequisites, in `Mali-G52`, are tracked separately: first kext build,
accepted read-only probe, usable power/clock, a real WRITE_VALUE job and a
completed fence. MaliMetal must not pretend to be ready before those exist.

## Layer contracts

### Metal frontend ↔ runtime

`NativeMetalObjects.h` is the only contract between the Objective-C++ Metal
frontend and the Rust runtime. It is backend-neutral: opaque handles, Metal raw
enumeration values, per-thread `nmb_last_error()`. The frontend does not know
whether the Vulkan driver is Turnip, PanVK or something else.

**Porting rule:** keep this header byte-for-byte compatible. If PanVK needs a
new operation, add it here and in the runtime; do not leak Vulkan or PanVK
types into the frontend.

### Runtime ↔ Vulkan ICD

The runtime opens a library exporting `vkGetInstanceProcAddr`. On AdrenoMetal
this is Turnip. On Mali it must be a PanVK ICD built for the same OS/ABI. There
is no Khronos loader on the target, so the ICD is opened directly by path, the
same way AdrenoMetal opens Turnip.

### Vulkan ICD ↔ kernel

This is the part that does not exist upstream for XNU. PanVK's `pan_kmod`
assumes a Linux DRM render node, DRM ioctls, `mmap()` offsets and DRM syncobjs.
`NativeMali` exposes fixed-size external-method records, explicit IOVAs,
`clientMemoryForType` mappings and polled job fences. The seam must translate
between those two models. The options and the evidence are in
`docs/ARCHITECTURE.md`; the choice is recorded as an open question.

## Repository layout

```text
.
├── AGENTS.md                  agent rules for this repository
├── README.md                  this file
├── docs/
│   ├── ARCHITECTURE.md        layers, contracts, data flow, seam options
│   ├── DECISIONS.md           decisions already taken and why
│   ├── DEPENDENCIES.md        what is pinned, what is still floating
│   ├── NATIVE_MALI_ABI.md     the subset of the kernel ABI this port needs
│   ├── OPEN_QUESTIONS.md      what needs a human answer or hardware
│   ├── PORTING.md             file-by-file reuse and fork map
│   └── TODO.md                milestones, checklists and deliberate gaps
├── patches/                   Mesa patch series (empty until MM3)
├── AdrenoMetal-reference/     local MIT reference, gitignored, never committed
└── third_party/               local dependency checkouts, gitignored
```

Planned source tree, once MM1 starts:

```text
src/plugin/      NativeMaliMetalPlugin.mm        discovery, publish, scanout
src/frontend/    NativeBridgeMetalFrontend.mm    Metal objects (mostly reused)
src/bridge/      NativeMetalObjects.{h,rs}       metal2vulkan C ABI
src/backend/     native_mali_kmod.*              PanVK/pan_kmod seam
tests/           host tests for the userspace ABI client and seam logic
```

No directory is created just to hold a placeholder; a path appears when its
first real file does.

## Required kernel state

MaliMetal cannot be validated before `Mali-G52` proves, on the phone:

- the kext builds and starts, and the read-only probe is accepted by a human;
- power, clock and the GPU core come up under software control;
- one WRITE_VALUE job is submitted and its fence completes;
- the GPU clock is more than the current fixed 26 MHz (`clk26m`, lowest vendor
  OPP). At 26 MHz a UI compositor is not a meaningful target.

The current kernel ABI also has known limits that matter for a Vulkan driver:
one address space per client, three job slots, 4 KiB pages only, polled fences,
no interrupt-driven completion, no AS LRU, no dma-buf/IOSurface import and no
cache-maintenance method. `docs/NATIVE_MALI_ABI.md` lists each one and what it
means for PanVK.

## Relationship to the other repositories

| Repository | Role | Rule |
| --- | --- | --- |
| `Mali-G52` | canonical kernel driver, register facts, device dumps, `NativeMaliABI.h` | change kernel facts there, not here |
| `MaliMetal` (this) | userspace Metal frontend and Vulkan backend seam | separate git repository |
| `UniXNU-merlinx` | OS image, ADT, build orchestration, display driver | do not modify from here |
| `AdrenoMetal` | MIT reference for architecture and reusable frontend | never commit the copy |
| Mesa | PanVK and the Bifrost compiler | pinned fork/patches, never commit a checkout |
| `metal2vulkan` | Metal API over Vulkan | linked unmodified, pinned revision |

## References

- `Mali-G52/README.md`, `docs/DEVICE.md`, `docs/OPEN_QUESTIONS.md` and
  `NativeMaliABI.h` for the kernel side and the measured phone facts.
- `AdrenoMetal-reference/README.md` and `AGENTS.md` for the model.
- Mesa `src/panfrost/vulkan/panvk_physical_device.c`,
  `src/panfrost/lib/kmod/pan_kmod.h` and `panfrost_kmod.c` for the backend
  assumptions. The exact commit still has to be pinned in
  `docs/DEPENDENCIES.md`.

## Licence

GPL-2.0-only for this repository as a whole. `NativeMaliHandleIndex.h`-style
MIT literals, the MIT reference material and the dual-licensed `metal2vulkan`
are handled in `NOTICE`. Do not copy a file from a reference tree without
recording its origin and licence.
