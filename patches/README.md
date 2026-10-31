<!-- SPDX-License-Identifier: GPL-2.0-only -->
# Mesa patch series

Empty until MM3. This directory will hold the patches that add the
`NativeMali` backend seam to a pinned Mesa tree.

Rules:

- Patches apply to Mesa commit `e24dc5bd1e7fe6101bdc866fb16a15a8fcae1aae`
  (`docs/DEPENDENCIES.md`); the series must refuse to apply to anything else and
  must verify the recorded sha256 of the files it patches.
- One patch, one purpose. Do not mix a rebase with a behaviour change.
- Each patch keeps Mesa's MIT headers and adds a `Signed-off-by`-style note only
  if the owner asks for it.
- Do not commit a Mesa checkout here.
- Re-run the host tests after every series change.
- If a patch touches PanVK's mmap or sync paths, say so in its subject; those
  are the parts that make the seam larger than a `pan_kmod` vtable.

The first patch is expected to be the backend skeleton, not a working driver.
