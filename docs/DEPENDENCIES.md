<!-- SPDX-License-Identifier: GPL-2.0-only -->
# Dependencies

AdrenoMetal, Mesa/PanVK and `metal2vulkan` are pinned. The toolchains are not.
This file records what is fixed, what must still be fixed before MM1 can claim a
reproducible build, and what each dependency is allowed to contribute.

The machine-readable pins are in `deps/deps.toml`. Verify a checkout with

```sh
python3 tools/deps.py verify --checkout mesa=/path/to/mesa
```

A build must use `--require-all` and refuse to run when anything is `SKIP` or
`FAIL`. The prose below explains why the pins are what they are.

## Required

| Dependency | Role | Licence | State |
| --- | --- | --- | --- |
| **AdrenoMetal** | architecture, Metal frontend, C object ABI, runtime integration, test model | MIT | pinned: [`ce5e19e33e05e1873d38d818e23cabfe0ac708c3`](https://github.com/UniXNU/AdrenoMetal/tree/ce5e19e33e05e1873d38d818e23cabfe0ac708c3) |
| **metal2vulkan** | Metal API → Vulkan translation | GPL-2.0-or-later OR LGPL-3.0-or-later | pinned: `219f01365dc4c8eb346816c405b5b17403793764`; see below |
| **Mesa (PanVK + Panfrost)** | Vulkan driver, Bifrost compiler, `pan_kmod` | MIT (per file) | pinned: `mesa-26.2.6` / `8e87a503ebd91408c88596d1b9eeb5f4c0b2088d`; see below |
| **NativeMali** | kernel ABI and device facts | GPL-2.0-only | canonical in `Mali-G52`; `NativeMaliABI.h` pinned by sha256 in `deps/abi.sha256` |
| **Mesa build deps** | `libdrm` headers at build time, Python/mako for generated code, etc. | per package | not enumerated |
| **Rust toolchain** | builds the bridge/runtime | per toolchain | not pinned; metal2vulkan requires Rust 1.98 |
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
`bridge/NativeMetalObjects.*`, `bridge/NativeMetalBridge.*`, `bridge/patches/`,
the offline wire model and the host-test style are the basis for this
repository's frontend. The local reference copy is `AdrenoMetal-reference/`
(gitignored).

## Mesa (PanVK) pin

```text
Mesa — PanVK + Panfrost/Bifrost + pan_kmod
  repository: https://gitlab.freedesktop.org/mesa/mesa    (canonical)
  tag:        mesa-26.2.6
  commit:     8e87a503ebd91408c88596d1b9eeb5f4c0b2088d    (committed 2026-10-28)
  licence:    MIT, per-file SPDX headers; the text is docs/license.rst
  machine:    deps/deps.toml
```

**Reference the official GitLab, not a mirror.** A commit SHA is a SHA wherever
it is hosted, so the pin is host-independent; the URL above is the canonical
one. The GitHub mirror (`mirror/mesa`) was used once, when the GitLab web UI was
not reachable from this machine, and it was **months behind**: its newest tag
was `mesa-25.0.0` and its `main` stopped at 2026-05-29. It is not the pin and
must not be used to re-pin. `git` and raw-file access to `gitlab.freedesktop.org`
do work from here even when the web UI shows a bot check.

**Why a release tag and not `main`.** `mesa-26.2.6` is the newest point release
at the time of pinning (2026-10-31); `mesa-26.3.0` was still at `-rc3`. A point
release gives a stable base with backports, and the tag resolves to one commit,
which is recorded above. The seam still depends on internal PanVK files, so a
point release does not reduce the rebase work — it only makes the base
reproducible.

**What was verified at that commit** (read from a `gitlab.freedesktop.org`
checkout, not assumed):

| File | Verified fact | sha256 |
| --- | --- | --- |
| `src/panfrost/vulkan/jm/panvk_vX_gpu_queue.c` | `pan_kmod_ioctl(dev->drm_fd, DRM_IOCTL_PANFROST_SUBMIT, &submit)` at :79 and :123; `.requirements = PANFROST_JD_REQ_FS` at :112; `vk_sync_type_is_drm_syncobj()` assertions at :242 and :316; `drmSyncobjCreate()` at :352 | `9a0aac3ba88728cef7baf59643485cca71e9eb841c41e87abdcbeaaf5c136703` |
| `src/panfrost/vulkan/panvk_physical_device.c` | `create_kmod_dev()` at :74 opens the render node and requires `panfrost`/`panthor` (:87 `drmGetVersion`); `vk_drm_syncobj_get_type()` at :386; the arch switch at :462 gates arch 6/7/14 behind `PAN_I_WANT_A_BROKEN_VULKAN_DRIVER=1` (:464 `case 7`, :466 the check) | `4a0e5ca0c6ace4141f8fd4d890e1de14668d0e2d729e0a7b991dbaa4c731190e` |
| `src/panfrost/lib/kmod/pan_kmod.c` | `pan_kmod_dev_create(int fd, uint32_t flags, ...)` at :51, dispatching on `drmGetVersion(fd)` | `2055c0369cc1195d5128a66f9ebfee8aa8bc7c08081d63d5fb94ecef8c640dea` |
| `src/panfrost/lib/kmod/pan_kmod.h` | `pan_kmod_bo_mmap()` at :750 uses `os_mmap(..., bo->dev->fd, mmap_offset)` at :765, derived from `bo_get_mmap_offset()` | `55fa7f89e5728a209fb615a26ee4099733bba29cee0695c754878f0c5dc6c4c4` |
| `src/panfrost/lib/kmod/panfrost_kmod.c` | the `panfrost` backend of `pan_kmod_ops`, 19 `DRM_IOCTL_PANFROST_*` uses | `87b855b43e4d76a19a53f629ee688955b82d098e97556e1966ab14e2781cf3a4` |
| `docs/license.rst` | the Mesa licence text | `0d1a0472ecc81830e75c20d59b0ea02841e3db21255e0ebad97ab682c54d6615` |

Two facts worth keeping in mind:

- `panvk_vX_gpu_queue.c` and `pan_kmod.c` are byte-identical to the 2026-05-29
  snapshot that the seam design was first read against, even though the tree
  moved a lot elsewhere. The JM submission and syncobj work is still ahead of
  us, not invented by us.
- The v7 gate is still present: PanVK refuses to start Bifrost v7 unless
  `PAN_I_WANT_A_BROKEN_VULKAN_DRIVER=1` is set. That is upstream's own statement
  that v7 is not well tested, and the port must not hide it.

**Reproduce (never commit the checkout):**

```sh
git clone --depth 1 --branch mesa-26.2.6 --filter=blob:none \
    https://gitlab.freedesktop.org/mesa/mesa.git mesa
python3 tools/deps.py verify --checkout mesa="$PWD/mesa"
```

The checkout belongs under `third_party/mesa*/` (gitignored) or outside this
repository. It is a build input, not a deliverable.

## metal2vulkan pin

```text
metal2vulkan
  repository: https://github.com/steelbrain/metal2vulkan
  commit:     219f01365dc4c8eb346816c405b5b17403793764    (committed 2026-10-29)
  licence:    GPL-2.0-or-later OR LGPL-3.0-or-later
  machine:    deps/deps.toml
```

**It is not MIT.** The verifiable evidence at the pinned commit:

- `Cargo.toml`, `[workspace.package]`, has
  `license = "GPL-2.0-or-later OR LGPL-3.0-or-later"`;
- `LICENSE` says it is dual-licensed and ends with
  `SPDX-License-Identifier: GPL-2.0-or-later OR LGPL-3.0-or-later`;
- the crates.io release `0.1.0` (2026-07-22) is published as
  **LGPL-3.0-or-later only** — the dual licence is the newer git tree. Do not
  substitute the crates.io release for the pin.

| File | sha256 |
| --- | --- |
| `Cargo.toml` | `e2fc374386d6ee10854f9c2996a8c3858cbb4f86f549f15c93dacf20cc4db439` |
| `LICENSE` | `672ded943eeab6d5fdb81c68fd3cf4e2a10fc324dc52c23feb8dded444b9207b` |
| `LICENSE-GPL-2.0` | `edaef632cbb643e4e7a221717a6c441a4c1a7c918e6e4d56debc3d8739b233f6` |
| `LICENSE-LGPL-3.0` | `e3a994d82e644b03a792a930f574002658412f62407f5fee083f2555c5f23118` |

**Which option this repository takes.** MaliMetal is `GPL-2.0-only`. The port
therefore uses `metal2vulkan` under its **`GPL-2.0-or-later` option**, elected as
GPLv2, so the combined work stays GPL-2.0-only. The `LGPL-3.0-or-later` option is
not used: LGPLv3 is not compatible with a GPLv2-only work. This election is
recorded in `NOTICE`.

That has a consequence for the "make it reusable" goal: a permissively licensed
MaliMetal is not possible while it links this runtime statically. If that ever
matters, the options are a separate process/IPC boundary, a different runtime, or
an explicit relicence decision by the owner — not a silent SPDX edit.

**The AdrenoMetal patch series is the model.** AdrenoMetal carries 16 patches
against exactly this workspace (`bridge/patches/0001`–`0016`, applied to
`crates/metal2vulkan*`). MaliMetal will reuse the ones that still apply and keep
its own series under `patches/metal2vulkan/`. Nothing is assumed to apply: each
patch is verified against the pinned commit before it is adopted.

`metal2vulkan` requires Rust 1.98 per its workspace manifest; the toolchain is
still an open item in `docs/OPEN_QUESTIONS.md`.

## Pin history

```text
2026-10-31  mesa-26.2.6  8e87a503  first accepted pin; release tag, official GitLab
2026-10-31  (provisional) e24dc5bd  rejected: GitHub mirror, 2026-05-29, months behind
2026-10-31  metal2vulkan  219f0136  first pin; dual licence, git tree (not crates.io 0.1.0)
```

Never delete a line. A pin that was wrong is still part of the record.

## Pin format

Two forms, one truth:

- `deps/deps.toml` — machine-readable: repository, revision, licence, and the
  sha256 of every file this port depends on. `tools/deps.py` reads this.
- this file — prose: why the revision, what was verified, what is still open.

When a dependency is pinned or re-pinned, record:

- repository URL (canonical host first) and exact tag/commit;
- the date inspected;
- the licence and the licence-file hash;
- the files this port depends on, by path and line/function;
- how to reproduce the checkout (without committing it).

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

- The toolchains (clang, Rust, linker, deployment target) are not pinned.
  `metal2vulkan` needs Rust 1.98 or newer.
- Is `libdrm` only a build-time header dependency, or does the chosen seam still
  call `drm*` functions at runtime? If the latter, the seam is not native enough.
- Can the target's userspace build this Mesa tree at all? PanVK here expects a
  Linux/Android-style build (libdrm headers, meson, Python/mako).
