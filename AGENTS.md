# Agent instructions

## Responsibility

Maintain the userspace Metal frontend for the MT6768 Mali-G52 MC2: the Metal
plugin, the `metal2vulkan` runtime integration, the PanVK backend seam, and the
tests that can run without a phone.

The kernel driver is out of scope. `Mali-G52` is canonical for every register,
page-table, power, clock and ABI fact.

## Required reading, in order

1. `README.md` — current state and layer contracts.
2. `docs/ARCHITECTURE.md` — where the port boundary is and why.
3. `docs/DECISIONS.md` — decisions already taken; do not reopen them silently.
4. `docs/TODO.md` — what is deliberately not implemented and why.
5. `docs/OPEN_QUESTIONS.md` — what waits on a human answer or hardware.
6. `docs/NATIVE_MALI_ABI.md` — the kernel ABI subset this port needs.
7. `docs/PORTING.md` — which AdrenoMetal file is reused, forked or replaced.
8. `docs/DEPENDENCIES.md` and `deps/deps.toml` — every pinned revision and hash.
9. `docs/MAINTENANCE.md` — the re-pin/rebase procedure; follow it before moving
   a dependency.
10. In `Mali-G52`: `README.md`, `docs/DEVICE.md`, `docs/OPEN_QUESTIONS.md`,
    `NativeMaliABI.h` and the header of every ABI method used.

## Non-negotiable rules

- **Separate repository.** Never add this tree, its build output or its
  reference copies to `Mali-G52`, `UniXNU-merlinx` or any other repository.
- **Never commit references.** `AdrenoMetal-reference/`, `AdrenoMetal/`,
  `third_party/mesa*/`, `third_party/metal2vulkan*/`, `workfile/` and any other
  checkout are development inputs. They are gitignored.
- **No direct hardware access.** Userspace never maps `0x13040000`, never writes
  a GPU register and never issues a register sequence. All hardware access goes
  through the fixed `NativeMali` ABI.
- **No guessed values.** A Vulkan feature, format, limit or kernel expectation
  must come from a measured `NativeMali` reply, a pinned Mesa source
  (`path:function` plus the pinned commit) or a pinned `metal2vulkan` source.
  Never fill a gap with a value from an Adreno, Valhall or different SoC.
- **Kernel changes go to `Mali-G52`.** If the port needs a new ABI method, a new
  record field, a page size or a fence semantic, write the requirement and the
  evidence in `docs/OPEN_QUESTIONS.md`, then change the kernel repository under
  its own rules. Do not add a private ioctl or a shadow ABI here.
- **Fail closed.** A refused Vulkan call must become a refused Metal call, a
  failed command buffer or a reported gap. Never swallow an error, substitute a
  default, retry behind the caller's back, or report a device ready after a
  partial initialisation.
- **Keep the frontend backend-neutral.** `NativeMetalObjects.h` is the contract.
  Do not leak Vulkan, PanVK or `pan_kmod` types into the Objective-C++ frontend.
  If a new operation is needed, add it to the C ABI and the runtime first.
- **Tests first.** Pure logic (ABI encoding/decoding, VA allocation, fence
  bookkeeping, BO lifetime, error mapping) must be host-testable without a GPU.
  Add the failing test, then the code.
- **Licences.** New files are `GPL-2.0-only`. Files derived from MIT material
  keep their MIT header and are recorded in `NOTICE`. `metal2vulkan` is linked
  unmodified under `GPL-2.0-or-later OR LGPL-3.0-or-later` — **not MIT** — and
  this repository elects the GPL-2.0-or-later option under GPLv2. Mesa stays MIT
  with its headers.
- **Never edit a hash to make a check pass.** `python3 tools/deps.py verify` is
  the gate. A mismatch means the tree moved or the pin is wrong; follow
  `docs/MAINTENANCE.md` and record the new pin in the same commit that changes
  the hashes, before any patch rebase.
- **Pin the AdrenoMetal credit.** Credit and porting references name commit
  `ce5e19e33e05e1873d38d818e23cabfe0ac708c3`, not `main`. If the credit or the
  porting map is updated, record the new commit with the same evidence.
- **DTB wins over prose** when they disagree. Record the conflict and the
  decision in `Mali-G52/docs/OPEN_QUESTIONS.md`, not in a code comment that
  contradicts the device tree.

## Build and verification

There is no build yet. When MM1 starts, the repository must provide one command
that:

- builds the host-testable parts without a phone or a GPU;
- runs every host test;
- builds the Mesa patch series and the `metal2vulkan` bridge with a recorded
  toolchain;
- refuses to produce a bundle if the pinned dependency hashes disagree.

Do not invent a build command in the README before it exists. A documented
command that cannot run is worse than an honest "not yet".

## Commits

- Commit in development order, one coherent step at a time.
- Never commit reference trees, Mesa checkouts, build output, caches, `.DS_Store`
  or user-specific paths.
- Do not push; the owner reviews first.
- After each commit, state exactly which files were committed.
- Update `NOTICE` when the set of derived files changes.

## When unsure

Stop. Write the question and the evidence in `docs/OPEN_QUESTIONS.md`, mark the
code path `Unsupported` or `NotReady`, and continue with a part that is certain.
A missing feature is recoverable; a guessed backend contract that corrupts
memory or hangs the GPU is not.
