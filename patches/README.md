<!-- SPDX-License-Identifier: GPL-2.0-only -->
# Patch series

Empty until MM3. This directory holds the patches that adapt pinned upstream
trees. Each dependency gets its own subdirectory:

```text
patches/mesa/           the native backend seam (docs/SEAM.md)
patches/metal2vulkan/   the Metal-runtime changes this port needs
```

Rules:

- Patches apply to the revisions in `deps/deps.toml` — Mesa `mesa-26.2.6`
  (`8e87a503ebd91408c88596d1b9eeb5f4c0b2088d`) and `metal2vulkan`
  `219f01365dc4c8eb346816c405b5b17403793764`. A series must refuse to apply to
  anything else and must verify the recorded sha256 of the files it patches
  (`python3 tools/deps.py verify`).
- One patch, one purpose. Do not mix a rebase with a behaviour change.
- Each patch keeps the upstream file's licence header (Mesa and `metal2vulkan`
  sources keep theirs; our GPL-2.0-only applies to our own files).
- Do not commit an upstream checkout here.
- Re-run the host tests and `tools/deps.py` after every series change.
- If a patch touches PanVK's mmap or sync paths, say so in its subject; those are
  the parts that make the seam larger than a `pan_kmod` vtable.

The first Mesa patch is expected to be the backend skeleton, not a working
driver. The first `metal2vulkan` patch should be one of AdrenoMetal's
`bridge/patches/0001`–`0016` that still applies unchanged; record which ones were
dropped and why.

A re-pin procedure, including how to refresh a series, is in
`docs/MAINTENANCE.md`.
