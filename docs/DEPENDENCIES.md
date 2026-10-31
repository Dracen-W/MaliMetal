<!-- SPDX-License-Identifier: GPL-2.0-only -->
# Dependencies

AdrenoMetal and Mesa/PanVK are pinned. `metal2vulkan` and the toolchains are
not. This file records what is fixed, what must still be fixed before MM1 can
claim a reproducible build, and what each dependency is allowed to contribute.

## Required

| Dependency | Role | Licence | State |
| --- | --- | --- | --- |
| **AdrenoMetal** | architecture, Metal frontend, C object ABI, runtime integration, test model | MIT | pinned: [`ce5e19e33e05e1873d38d818e23cabfe0ac708c3`](https://github.com/UniXNU/AdrenoMetal/tree/ce5e19e33e05e1873d38d818e23cabfe0ac708c3) |
| **metal2vulkan** | Metal API → Vulkan translation | GPL-2.0-or-later OR LGPL-3.0-or-later | commit not pinned |
| **Mesa (PanVK + Panfrost)** | Vulkan driver, Bifrost compiler, `pan_kmod` | MIT (per file) | pinned: [`e24dc5bd1e7fe6101bdc866fb16a15a8fcae1aae`](https://github.com/mirror/mesa/commit/e24dc5bd1e7fe6101bdc866fb16a15a8fcae1aae); see below |
| **NativeMali** | kernel ABI and device facts | GPL-2.0-only | canonical in `Mali-G52`; commit not recorded here |
| **Mesa build deps** | `libdrm` headers at build time, Python/mako for generated code, etc. | per package | not enumerated |
| **Rust toolchain** | builds the bridge/runtime | per toolchain | not pinned |
| **Clang / Objective-C++ toolchain** | plugin and frontend | per toolchain | not pinned |

## Attribution

AdrenoMetal is the upstream project this port is derived from:

```text
AdrenoMetal
https://github.com/UniXNU/AdrenoMetal
Pinned revision: ce5e19e33e05e1873d38d818e23cabfe0ac708c3
https://github.com/UniXNU/AdrenoMetal/tree/ce5e19e33e05e1873d38d818e23cabfe0ac708c3
MIT License
Copyright (c) 2026 NativeMetal contributors
```

The revision above is the one the porting map and file references were checked
against. It is a fixed commit, not `main`: a later AdrenoMetal change does not
silently change what this repository claims to be derived from. The local
reference copy was verified byte-for-byte against that commit on 2026-10-31
(all 94 blobs match; `LICENSE` sha256
`c51a32dfc2140d3d56f796a3a7010e574f2e3649b97c42e010303ad57f848265`). If the
copy is refreshed, re-check it against the pinned commit and record the result
rather than guessing.

Its `README.md`, `AGENTS.md`, `NativeBridgeMetalFrontend.mm`,
`bridge/NativeMetalObjects.*`, `bridge/NativeMetalBridge.*`, the offline wire
model and the host-test style are the basis for this repository's frontend.
The local reference copy is `AdrenoMetal-reference/` (gitignored).

`metal2vulkan` is linked unmodified; none of it is copied here. Mesa is used
under its MIT terms and stays in its own checkout/patch series.

## Mesa (PanVK) pin

```text
Mesa — PanVK + Panfrost/Bifrost + pan_kmod
  upstream: https://gitlab.freedesktop.org/mesa/mesa
  mirror:   https://github.com/mirror/mesa   (read-only; the source used here)
  commit:   e24dc5bd1e7fe6101bdc866fb16a15a8fcae1aae
  date:     committed 2026-05-29, inspected 2026-10-31
  tag:      none — this is a main-branch snapshot
  licence:  MIT, per-file SPDX headers; the licence text is `docs/license.rst`
            (there is no root `LICENSE` file at this commit)
```

**Why a commit and not a tag.** Through this environment the newest Mesa release
tag reachable is `mesa-25.0.0`, which is roughly a year older than the pinned
snapshot and predates the current PanVK layout. The pin is therefore an exact,
immutable commit SHA, not a moving branch. A re-pin to a release tag is required
before any product use; see `docs/OPEN_QUESTIONS.md`.

**What was verified at that commit** (read, not assumed):

| File | Verified fact | sha256 |
| --- | --- | --- |
| `src/panfrost/vulkan/jm/panvk_vX_gpu_queue.c` | `pan_kmod_ioctl(dev->drm_fd, DRM_IOCTL_PANFROST_SUBMIT, &submit)` at :79 and :123; `.requirements = PANFROST_JD_REQ_FS` at :112; `vk_sync_type_is_drm_syncobj()` assertions at :242 and :316; `drmSyncobjCreate()` at :352 | `9a0aac3ba88728cef7baf59643485cca71e9eb841c41e87abdcbeaaf5c136703` |
| `src/panfrost/lib/kmod/pan_kmod.c` | `pan_kmod_dev_create(int fd, uint32_t flags, ...)` at :51, dispatching on `drmGetVersion(fd)` | `2055c0369cc1195d5128a66f9ebfee8aa8bc7c08081d63d5fb94ecef8c640dea` |
| `src/panfrost/lib/kmod/pan_kmod.h` | `pan_kmod_bo_mmap()` at :682 uses `os_mmap(..., bo->dev->fd, mmap_offset)` derived from `bo_get_mmap_offset()` | `16943f988949791c9253b62bbd03a5629afdf5615fcf2d7be8726a18473b7bd8` |
| `src/panfrost/lib/kmod/panfrost_kmod.c` | the `panfrost` backend of `pan_kmod_ops` over `DRM_IOCTL_PANFROST_*` | `8c5735867abedea62d026f12ea790c3fd9fe98abc9fa37304317b809af77a903` |
| `src/panfrost/vulkan/panvk_physical_device.c` | `create_kmod_dev()` at :70 opens the render node and requires `panfrost`/`panthor` (:83 `drmGetVersion`); `vk_drm_syncobj_get_type()` at :337; the arch switch at :412 gates arch 6/7/14 behind `PAN_I_WANT_A_BROKEN_VULKAN_DRIVER=1` (:414 `case 7`, :416 the check) | `ba4b289b71f3b5f180208fe44c4813e6c8162d380d53e92785dd22431a00afb7` |

These are the facts the seam in `docs/SEAM.md` is written against. If any hash
differs, the patch series is invalid and MM3 stops.

**Reproduce (never commit the checkout):**

```sh
git clone --filter=blob:none https://github.com/mirror/mesa mesa-checkout
git -C mesa-checkout checkout e24dc5bd1e7fe6101bdc866fb16a15a8fcae1aae
git -C mesa-checkout rev-parse HEAD    # must print the pinned SHA
shasum -a 256 mesa-checkout/src/panfrost/vulkan/jm/panvk_vX_gpu_queue.c
```

The checkout belongs under `third_party/mesa*/` (gitignored) or outside this
repository. It is a build input, not a deliverable.

**Fallback tag.** If the pinned snapshot proves unbuildable on the target, the
newest release tag reachable here is `mesa-25.0.0`. At that tag the JM
submission lives in `src/panfrost/vulkan/jm/panvk_vX_queue.c` instead of
`panvk_vX_gpu_queue.c`, so the patch series needs re-targeting, not just a
rebase. Record the new base and the re-verified hashes before switching.

## Pin format

When a dependency is pinned, record:

- repository URL;
- exact commit or tag;
- the date inspected;
- the licence file hash;
- the files this port depends on, by path and function;
- how to reproduce the checkout (without committing it).

Example (not yet valid; no commit is being asserted):

```text
metal2vulkan
  repo:   <url>
  commit: <sha>
  licence: GPL-2.0-or-later OR LGPL-3.0-or-later
  used:   metal2vulkan_runtime::{Device, DeviceOptions, ...}
  via:    bridge/NativeMetalObjects.rs
```

## Not committed

The following are development inputs and stay out of git:

- `AdrenoMetal-reference/`
- `AdrenoMetal/`
- `third_party/mesa*/`
- `third_party/metal2vulkan*/`
- `third_party/drm*/`
- `workfile/`
- build output, caches, `.DS_Store`

## Open

- Mesa is pinned to a main-branch commit, not a release tag. Which release tag
  takes over before product use?
- Is `libdrm` only a build-time header dependency, or does the chosen seam
  still call `drm*` functions at runtime? If the latter, the seam is not
  native enough.
- Which Rust and Clang versions does the target image actually ship/build
  with?
