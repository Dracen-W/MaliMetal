<!-- SPDX-License-Identifier: GPL-2.0-only -->
# Decisions

Decisions already taken. Reopening one requires new evidence and an entry in
`docs/OPEN_QUESTIONS.md` first.

## D1 — MaliMetal is a separate repository

- **Decision:** userspace Metal work lives in its own git repository, not inside
  `Mali-G52`.
- **Why:** the kernel is GPL-2.0-only and has its own source-citation and
  write-gate rules; the Metal port has a different dependency set (Mesa,
  `metal2vulkan`, Objective-C++) and a different release shape (bundle, not
  kext). Keeping them separate also prevents a userspace reference checkout
  from being committed to the kernel repository.
- **Consequence:** `Mali-G52` remains canonical for `NativeMaliABI.h`, register
  facts and device measurements. This repository never carries a second copy
  of those facts.
- **Date:** 2026-10-30.

## D2 — Port AdrenoMetal; do not use it unchanged

- **Decision:** adopt AdrenoMetal's architecture and reusable frontend, then
  replace the Adreno-specific lower layers.
- **Why:** Turnip is Freedreno/Adreno-only; the plugin opens
  `AGXDeviceUserClient`; the scanout and format paths are Adreno/AGX-specific.
  A direct reuse would either fail to open a device or silently program the
  wrong hardware.
- **Consequence:** the port boundary is the Vulkan backend. See
  `docs/PORTING.md` for the file-by-file map.
- **Date:** 2026-10-30.

## D3 — Reuse `metal2vulkan` and the Metal frontend

- **Decision:** keep the Metal frontend (`NativeBridgeMetalFrontend.mm`) and
  the `metal2vulkan` runtime (`bridge/NativeMetalObjects.rs`) as the top of the
  stack. Do not write a Metal-to-Mali translator.
- **Why:** these layers are already backend-neutral; they talk to a Vulkan ICD
  through `NativeMetalObjects.h`. Writing a translator would duplicate years of
  work and would still need a Bifrost compiler.
- **Consequence:** every new backend capability must fit the C ABI in
  `NativeMetalObjects.h`. Vulkan/PanVK types do not leak upward.
- **Date:** 2026-10-30.

## D4 — Reuse Mesa PanVK/Panfrost for Bifrost v7

- **Decision:** use Mesa's PanVK and its Bifrost compiler; do not write a
  shader compiler or command-stream generator.
- **Why:** PanVK already has a Bifrost v7 JM path, the NIR/Bifrost backend,
  format/tiling tables and a `pan_kmod` abstraction. A custom compiler is out
  of scope and would be unverifiable on this hardware.
- **Consequence:** the port needs a pinned Mesa dependency and a patch series
  or fork. Upstream marks v7 as not well-tested; that risk is recorded in
  `docs/OPEN_QUESTIONS.md` item 4.
- **Date:** 2026-10-30.

## D5 — The kernel stays canonical and comes first

- **Decision:** no backend implementation is validated before `Mali-G52` has a
  real on-device job and a usable clock.
- **Why:** the Metal frontend immediately exercises multi-BO, fences, cache
  maintenance and reset. Building it against an unproven kernel would turn
  kernel bugs into userspace bugs and vice versa. The current 26 MHz clock is
  also not a meaningful compositor target.
- **Consequence:** MM4+ are blocked on the kernel milestones, even if the
  userspace code is ready earlier.
- **Date:** 2026-10-30.

## D6 — No direct hardware access from userspace

- **Decision:** userspace never maps GPU registers and never issues a register
  sequence. All hardware access uses `NativeMaliABI.h`.
- **Why:** the kernel owns power, clocks, MMU and job slots. A userspace
  register write would bypass the write gate, the rollback rules and the
  single-owner model, and could hang the bus.
- **Consequence:** if PanVK needs something the ABI does not expose, the answer
  is a documented ABI gap and, if justified, a kernel change in `Mali-G52` —
  never a private mapping here.
- **Date:** 2026-10-30.

## D7 — Fail closed at every layer

- **Decision:** a refused Vulkan call becomes a refused Metal call; a failed
  command buffer reports an error; a partial device is never published.
- **Why:** the target is the system UI. A silent fallback or a fake success
  makes a compositor bug look like a rendering bug and can corrupt the display.
- **Consequence:** the plugin returns `nil` on failure and leaves the original
  software renderer running. Tests assert the refusal, not just the happy path.
- **Date:** 2026-10-30.

## D8 — Attribute AdrenoMetal and every derived file

- **Decision:** the official AdrenoMetal project is credited in `README.md` and
  `NOTICE`; every derived file keeps its MIT header and names its origin.
- **Why:** the frontend, C ABI, runtime integration and test style are
  AdrenoMetal's work. Attribution is both a licence condition and a matter of
  honesty.
- **Consequence:** a file without a provenance header is not accepted. The
  reference copy stays local and gitignored.
- **Date:** 2026-10-31.
