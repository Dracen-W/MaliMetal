<!-- SPDX-License-Identifier: GPL-2.0-only -->
# Maintenance and rebase guide

This repository depends on two fast-moving trees: Mesa (which moves daily) and
`metal2vulkan`. This guide is for whoever re-pins them. It assumes the reader
has `docs/DEPENDENCIES.md`, `docs/SEAM.md` and `deps/deps.toml` open.

**Owner of the rebase:** the repository owner (agreed 2026-10-31). No automated
process re-pins anything; a human runs the procedure below and records the
result. If that ever changes, it is a decision, not a convenience.

## What moves and what does not

| Layer | Source of truth | May move? |
| --- | --- | --- |
| Kernel ABI, registers, device facts | `Mali-G52` (separate repo) | only under that repository's rules |
| Mesa seam | `deps/deps.toml` + `patches/mesa/` | yes, per the procedure below |
| `metal2vulkan` + patches | `deps/deps.toml` + `patches/metal2vulkan/` | yes, rarely |
| Metal frontend, C object ABI | AdrenoMetal `ce5e19e` | only with a recorded re-pin |
| Kernel driver behaviour | `Mali-G52` | not here |

## The pin set

```sh
python3 tools/deps.py verify --require-all \
    --checkout mesa=/path/to/mesa \
    --checkout metal2vulkan=/path/to/metal2vulkan
```

`--require-all` makes a missing checkout a failure. A build refuses to run on
`FAIL` **or** `SKIP`; a `SKIP` that reaches a release build is a bug. Anything
that changes a pinned file's bytes — including a stray local edit — shows up
here even when `git rev-parse HEAD` is right.

## When to re-pin

**Cadence: once per Mesa major series, not per commit and not per point
release.** A "major series" is the first two components of the version
(`26.2`); a point release is the third (`26.2.6`). At adoption time, take the
newest point release in that series, because it carries the fixes already
backported to it. Then stay there.

Re-pin when:

1. A new major series is adopted (for example `26.2` → `26.3`/`27.0`).
2. The pin is unusable — build failure, a file the seam patches was renamed, or
   the v7 gate changed in a way that matters.
3. A security fix affects a component the port ships and was not backported to
   the pinned point release.
4. MM3 is about to start and the pin is older than the series the patch series
   targets.

Everything else waits. A point-release bump inside the same major series is not
a maintenance event on its own: it renumbers every line reference, invalidates
the patches' context and costs a hardware re-verification, and it rarely buys
anything the seam needs. If a point release really is needed, treat it as a
normal re-pin (procedure below) and say why in the pin history.

Do **not** re-pin per commit, "to stay current", or in the middle of a hardware
bring-up.

## Rebase procedure

Work on a branch (`rebase/mesa-<old>-to-<new>`), never on top of uncommitted
work. One re-pin is two commits, in this order:

### 1. Fetch and record the new base

```sh
git clone --depth 1 --branch <new-tag> --filter=blob:none \
    https://gitlab.freedesktop.org/mesa/mesa.git mesa-<new>
git -C mesa-<new> rev-parse HEAD
```

Use the official GitLab. A GitHub mirror may be months behind — the first pin
used one and had to be thrown away. Record the **peeled commit**, not the
annotated-tag object: `git rev-parse mesa-26.2.6^{commit}`.

### 2. Let the verifier tell you what changed

```sh
python3 tools/deps.py verify --checkout mesa="$PWD/mesa-<new>"
```

Expect `FAIL` on every hash. That is the tool doing its job: it proves the tree
moved. Do not "update the hashes to make it pass" before reading the diff.

### 3. Read the diff of the pinned files

```sh
git -C mesa-<new> diff <old-commit> <new-commit> -- \
  src/panfrost/vulkan/jm/panvk_vX_gpu_queue.c \
  src/panfrost/vulkan/panvk_physical_device.c \
  src/panfrost/lib/kmod/pan_kmod.c \
  src/panfrost/lib/kmod/pan_kmod.h \
  src/panfrost/lib/kmod/panfrost_kmod.c
```

Read every hunk that touches:

- `DRM_IOCTL_PANFROST_SUBMIT`, `drm_panfrost_submit`, `.requirements`;
- `drmSyncobj*` / `DRM_IOCTL_SYNCOBJ_*`;
- `vk_sync_type_is_drm_syncobj`, `vk_sync_as_drm_syncobj`;
- `struct pan_kmod_ops`, `pan_kmod_dev_create`, `bo_get_mmap_offset`,
  `pan_kmod_bo_mmap`, `os_mmap`;
- the arch switch and `PAN_I_WANT_A_BROKEN_VULKAN_DRIVER`;
- anything that looks like a new submit or VM-bind operation.

If a new `pan_kmod` operation appears that removes one of our patches, that is
good news: delete the patch, do not keep it "just in case".

### 4. Commit the re-pin alone

Update, in one commit that changes no behaviour:

- `deps/deps.toml` — tag, revision, revision date, file hashes;
- `docs/DEPENDENCIES.md` — the pin block, the verified-facts table, line numbers;
- `docs/SEAM.md` — status header and any line/function references;
- `docs/ARCHITECTURE.md` — the file list, if it changed.

Commit message: `deps: re-pin Mesa to <tag> (<sha12>)` plus one sentence on why.
State the exact files in the reply, as with any commit.

### 5. Rebase the patch series

- `git am` (or `patch -p1`) each patch in `patches/mesa/`, oldest first.
- Resolve conflicts against the **new** upstream code, not against our old
  patch. If our change is no longer needed, drop the patch and say so.
- Keep one purpose per patch. Do not fold a rebase fix into a behaviour change.
- If the conflict is in the JM submit path, stop and re-read the new code before
  resolving: the submission contract is the thing this port exists for.
- Regenerate the series (`git format-patch`) so line numbers and context are
  current.

### 6. Verify

```sh
python3 tools/deps.py verify --require-all \
    --checkout mesa="$PWD/mesa-<new>" \
    --checkout metal2vulkan=/path/to/metal2vulkan
tests/run.sh          # once it exists
tests/run.sh --san
```

No hardware is needed for this step, and no hardware result can excuse a failing
host test.

### 7. Record and commit

- Append a line to the pin history in `docs/DEPENDENCIES.md` (see below).
- Update `docs/DECISIONS.md` D11 with the new commit and the evidence; do not
  rewrite the old entry's history, add the new fact.
- If the change alters the seam's shape (a new op, a removed ioctl), update
  `docs/SEAM.md` and, if it changes the approach, `docs/DECISIONS.md` (D10).
- Commit the patches with a subject naming the base:
  `mesa: rebase NativeMali series onto <tag>`.

## Pin history

Keep a short log in `docs/DEPENDENCIES.md` so a bisect has a map:

```text
2026-10-31  mesa-26.2.6  8e87a503  first pin; release tag, GitLab
<date>      <tag>        <sha12>   <why>
```

Never delete a line. A pin that was wrong is still part of the record.

## Common breakages and what to look for

| Symptom | Likely cause | Action |
| --- | --- | --- |
| `panvk_vX_gpu_queue.c` missing | JM files renamed again (this already happened once: `panvk_vX_queue.c` → `panvk_vX_gpu_queue.c`) | find the new path, re-read the submit path, update the patch and every doc reference |
| `pan_kmod_dev_create` signature changed | the fd assumption moved | re-check whether a backend-neutral creation path now exists upstream; if so, use it and shrink the series |
| `bo_get_mmap_offset` removed | map path changed | re-read `pan_kmod_bo_mmap`; our `BoCpuMap` op may now fit a different hook |
| syncobj asserts gone | `vk_sync` generalised | our sync patch may shrink or disappear |
| new `submit` op in `pan_kmod` | upstream abstracted submission | delete the JM submit patch and use the op |
| v7 gate changed | upstream reclassified Bifrost | record it; do not hard-code the old env var |
| `requirements` gained a field | job affinity now expressible | revisit the `NativeMali` ABI gap in `Mali-G52` with evidence |

## Stop conditions

Stop and write to `docs/OPEN_QUESTIONS.md`, rather than forcing a rebase, when:

- a pinned file's hash disagrees and you cannot say why;
- a patch no longer has a test that fails without it;
- the new base needs a kernel ABI change (that belongs in `Mali-G52`);
- the new base would make the v7 gate stricter and the port cannot meet it;
- the licence of a dependency changed (re-read `LICENSE`, do not diff the SPDX
  line only).

## Upstream contribution path

The stated goal is reusable, standardised code — possibly upstream. Keep that
possible by construction:

- **Separate the generic from the XNU-specific.** A `pan_kmod` submit/VM-bind
  operation and the removal of the JM path's direct DRM calls are plausible
  upstream changes. The `NativeMali` backend itself is not; it names an Apple
  kernel ABI.
- **Propose the smallest generic patch first.** If upstream accepts a submit op,
  our series shrinks to the backend plus a `vk_sync` type.
- **Keep Mesa patches MIT and in Mesa's style.** Our GPL-2.0-only applies to our
  files, not to patches against MIT code.
- **Do not put a private ABI in generic code.** No `NativeMali` names in
  `src/vulkan/`, no XNU types in `pan_kmod.h`.
- **A drivers/ entry is not a fork.** If this ever ships as a standard
  component, the pin file and this guide are what make it reviewable.

## Licences that must be re-checked on every re-pin

- Mesa: MIT per file; `docs/license.rst` hash is pinned.
- `metal2vulkan`: **GPL-2.0-or-later OR LGPL-3.0-or-later**, not MIT. This
  repository elects GPL-2.0-or-later under GPLv2 (see `NOTICE`). If a re-pin
  changes the licence, stop.
- AdrenoMetal: MIT; a refresh needs the same byte-for-byte verification the first
  pin had.

## Refreshing the AdrenoMetal reference

Only when a frontend file is actually needed:

```sh
git -C AdrenoMetal-reference fetch
git -C AdrenoMetal-reference checkout ce5e19e33e05e1873d38d818e23cabfe0ac708c3
# verify every blob against the commit before copying anything
```

If a newer AdrenoMetal commit is wanted, it is a new decision: record the commit
and the verification in `docs/DECISIONS.md`, `NOTICE`, `README.md` and
`docs/PORTING.md`, and re-run the tests. Never mix "refresh" with "port".

## When the kernel ABI header changes

`deps/abi.sha256` pins the sha256 of `Mali-G52/NativeMaliABI.h` and the ABI
version/method markers. `tests/run.sh` runs `tools/sync-abi.py`, which fails
when the file drifts.

1. Read the kernel diff first. It belongs to `Mali-G52`; do not edit the
   canonical header from here.
2. If the change is additive and does not move `NativeMaliAbiVersion` or
   `NativeMaliMethodLast` (the `Reset` selector was added that way), update the
   hash with `python3 tools/sync-abi.py --accept`, re-run `tests/run.sh`, and
   record the change in `docs/DEPENDENCIES.md`.
3. If the version or the last selector moved, `--accept` refuses. That is the
   intended behaviour: it is an ABI break, and it needs a decision in
   `Mali-G52`, a client change here and a note in `docs/DECISIONS.md`.
4. If a record's size changed, `NativeMaliAbiTable.cpp` fails to compile. Do not
   paper over it with a cast: the table is the userspace half of the contract.

## If a re-pin goes wrong

Revert the pin commit; the previous `deps/deps.toml` and doc block come back
together, so the tree returns to a state that was actually verified. Do not
hand-edit a hash to keep going. A wrong pin that "builds" is worse than a pin
that fails loudly.
