<!-- SPDX-License-Identifier: GPL-2.0-only -->
# Dependencies

Nothing here is pinned yet. This file records what must be pinned before MM1 can
claim a reproducible build, and what each dependency is allowed to contribute.

## Required

| Dependency | Role | Licence | State |
| --- | --- | --- | --- |
| **AdrenoMetal** | architecture, Metal frontend, C object ABI, runtime integration, test model | MIT | reference copy present; commit not pinned |
| **metal2vulkan** | Metal API → Vulkan translation | GPL-2.0-or-later OR LGPL-3.0-or-later | commit not pinned |
| **Mesa (PanVK + Panfrost)** | Vulkan driver, Bifrost compiler, `pan_kmod` | MIT (per file) | commit not pinned; fork/patch policy open |
| **NativeMali** | kernel ABI and device facts | GPL-2.0-only | canonical in `Mali-G52`; commit not recorded here |
| **Mesa build deps** | `libdrm` headers at build time, Python/mako for generated code, etc. | per package | not enumerated |
| **Rust toolchain** | builds the bridge/runtime | per toolchain | not pinned |
| **Clang / Objective-C++ toolchain** | plugin and frontend | per toolchain | not pinned |

## Attribution

AdrenoMetal is the upstream project this port is derived from:

```text
AdrenoMetal
https://github.com/UniXNU/AdrenoMetal
MIT License
Copyright (c) 2026 NativeMetal contributors
```

Its `README.md`, `AGENTS.md`, `NativeBridgeMetalFrontend.mm`,
`bridge/NativeMetalObjects.*`, `bridge/NativeMetalBridge.*`, the offline wire
model and the host-test style are the basis for this repository's frontend.
The local reference copy is `AdrenoMetal-reference/` (gitignored).

`metal2vulkan` is linked unmodified; none of it is copied here. Mesa is used
under its MIT terms and stays in its own checkout/patch series.

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

- Which Mesa commit is the base?
- Is the fork a patch series in `patches/` or a separate pinned fork?
- Is `libdrm` only a build-time header dependency, or does the chosen seam
  still call `drm*` functions at runtime? If the latter, the seam is not
  native enough.
- Which Rust and Clang versions does the target image actually ship/build
  with?
