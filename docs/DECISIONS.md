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
  `NOTICE`; every derived file keeps its MIT header and names its origin. The
  credit is anchored to commit
  [`454ff2ce83766ce7b3e0f355e83f1d72907708b3`](https://github.com/UniXNU/AdrenoMetal/tree/454ff2ce83766ce7b3e0f355e83f1d72907708b3),
  not to a moving branch.
- **Why:** the frontend, C ABI, runtime integration and test style are
  AdrenoMetal's work. Attribution is both a licence condition and a matter of
  honesty.
- **Consequence:** a file without a provenance header is not accepted. The
  reference copy stays local and gitignored; it is verified against the pinned
  commit before being used as a source.
- **Date:** 2026-10-31.

## D9 — AdrenoMetal credit is pinned to one commit

- **Decision:** all AdrenoMetal references in this repository name
  `454ff2ce83766ce7b3e0f355e83f1d72907708b3`.
- **Why:** "the current AdrenoMetal" is not a version. A porting map that points
  at `main` would silently describe different files after an upstream change,
  and the credit would no longer identify what was actually reused.
- **Consequence:** when the credit or the porting map is updated, the new commit
  must be recorded with the same evidence. A later upstream revision is not
  assumed to be compatible.
- **Date:** 2026-10-31.

## D10 — The PanVK seam is a Mesa patch series (Option A)

- **Decision:** implement the seam as a pinned Mesa patch series with a native
  `pan_kmod` backend, replacing the JM path's direct
  `DRM_IOCTL_PANFROST_SUBMIT` and `drmSyncobj*` use. Option B (Panfrost DRM
  UAPI) is the fallback; Option C (userspace DRM emulation) is rejected.
- **Why:** it speaks `NativeMali` directly, avoids implementing a DRM object
  model in XNU, and keeps the translation visible and testable. The owner chose
  it on 2026-10-31.
- **Consequence:** the patch series touches
  `panvk_physical_device.c`, `pan_kmod.{h,c}`, `panfrost_kmod.c`,
  `jm/panvk_vX_gpu_queue.c` and the Vulkan sync layer. The base commit is
  pinned in D11; every Mesa update needs a rebase and a full test run. The
  kernel ABI still has to work first.
- **Date:** 2026-10-31.

## D11 — The Mesa base is pinned to a release tag from the official GitLab

- **Decision:** the Mesa base is tag `mesa-26.2.6`, commit
  `8e87a503ebd91408c88596d1b9eeb5f4c0b2088d` (committed 2026-10-28), taken from
  `https://gitlab.freedesktop.org/mesa/mesa`. The five PanVK/`pan_kmod` files the
  seam depends on, plus `docs/license.rst`, are recorded by sha256 in
  `deps/deps.toml` and `docs/DEPENDENCIES.md`.
- **Why:** it is the newest point release and a fixed commit, so the base is
  reproducible and has backports. The canonical host is the freedesktop GitLab;
  a commit SHA is host-independent, so pinning GitLab does not stop anyone
  building from a mirror.
- **Rejected:** the earlier provisional pin (`e24dc5bd`, GitHub `mirror/mesa`,
  2026-05-29). That mirror was months behind main — its newest tag was
  `mesa-25.0.0` — and Mesa moved substantially in between. Pin history is kept in
  `docs/DEPENDENCIES.md`.
- **Consequence:** a re-pin is a two-commit operation (pin, then patch rebase) and
  is owned by the repository owner; the procedure is `docs/MAINTENANCE.md`. Any
  re-pin must re-verify hashes and re-read the JM submit path before a patch is
  applied.
- **Date:** 2026-10-31 (supersedes the same-day provisional pin).

## D12 — metal2vulkan is pinned and used under GPL-2.0-or-later

- **Decision:** `metal2vulkan` is pinned to commit
  `720ef1ce9ae2a2068999a8675717418b9416283e` (2026-10-14), the translator commit
  UniXNU qualifies in `scripts/data/gpu-metal-vulkan-inputs.json`, and is used
  under its
  `GPL-2.0-or-later` option, elected as GPLv2, so the combined work remains
  GPL-2.0-only. The `LGPL-3.0-or-later` option is not used.
- **Why:** that is its actual licence (`Cargo.toml`, `LICENSE`, both checked at
  the pinned commit). An MIT assumption was considered and rejected: it is not
  MIT, and the crates.io release `0.1.0` is LGPL-3.0-or-later only, so the git
  tree is what must be pinned. LGPLv3 cannot be combined with a GPLv2-only work.
- **Why this commit:** the newer tip `219f0136` builds the translator and runtime
  but **cannot compile AdrenoMetal's bridge**, with or without the patches that
  apply to it. `720ef1c` is the base the 16-patch series applies to cleanly, and
  it is the commit UniXNU's input record qualifies. Pinning the tip looked
  current but was wrong.
- **Consequence:** a permissively licensed MaliMetal is not possible while this
  runtime is linked in. If that changes, it is an explicit decision (separate
  process, different runtime, or relicensing), never a silent SPDX edit.
- **Date:** 2026-10-31.

## D13 — One owner re-pins dependencies, with a written procedure

- **Decision:** the repository owner owns Mesa/`metal2vulkan` re-pins, following
  `docs/MAINTENANCE.md`. Automated re-pinning is not used.
- **Why:** the seam patches the JM submission path and the sync layer; a
  mechanical version bump is exactly where a silent semantic change hides. A
  human who reads the diff is cheaper than a GPU hang on the phone.
- **Consequence:** `deps/deps.toml` and the docs are edited together, in a commit
  that changes no behaviour, before the patch series is rebased. The cadence is
  one re-pin per Mesa major series (`26.2`, `26.3`, ...), taking the newest point
  release of that series; point releases are not tracked individually.
- **Date:** 2026-10-31.
