<!-- SPDX-License-Identifier: GPL-2.0-only -->
# Kernel integration: what this repository needs, and what it already gives

MaliMetal is userspace. It does not build the kernel and does not modify
`UniXNU-merlinx`. This page records the boundary so the two sides can be worked
on separately without guessing.

The authoritative, file-by-file change list for the kernel side lives in
`Mali-G52/docs/KEXT_BUILD.md`. That repository is canonical for kernel facts;
this page only states what the userspace port needs from it.

## Status (2026-10-31)

| Item | State |
| --- | --- |
| Kernel driver source (M0–M7 pure logic, IOKit glue) | written in `Mali-G52`, host-tested; never built against the kernel ABI |
| Kernel build environment | missing: no iPhoneOS SDK on this machine, no `build/xnu` tree, no `.cpo.json`; the owner builds through GitHub CI |
| `NativeMali` ABI v1 | fixed by `Mali-G52/NativeMaliABI.h`, pinned here by `deps/abi.sha256` |
| Userspace ABI client | complete for v1: all 11 selectors, buffer lifecycle, fences, faults, IOKit transport |
| Integration with `UniXNU-merlinx` | **not applied**; the change list is a checklist for the owner |
| MM3 (`pan_kmod` seam) | blocked until the kernel runs a real job (D5) |

## What the kernel side must do (summary only)

From `Mali-G52/docs/KEXT_BUILD.md` section 7: add the driver submodule, one
component-catalog entry, one profile entry, one defconfig macro, one
`KernelConfigTables.cpp` personality patch, and one ADT node with
`compatible = "mediatek,mali"`. The kext route additionally needs a loader that
implements `UNIXNU_LOADER_PROTOCOL` revision 2, AMFI/trust-cache signing and KPI
symbol sets; it is not the recommended path.

## What this repository already provides

These are the interfaces the backend will sit on, and they are host-tested:

| Interface | Header | Purpose |
| --- | --- | --- |
| `NativeMaliTransport` | `src/native_mali/NativeMaliTransport.h` | one record pair across the boundary; the only thing IOKit or a test must implement |
| `NativeMaliError` | `src/native_mali/NativeMaliError.h` | one error vocabulary; `kIOReturn*` maps in, Vulkan maps out in the backend |
| `NativeMaliClient` | `src/native_mali/NativeMaliClient.h` | record validation and all 11 selectors |
| `NativeMaliDevice` | `src/native_mali/NativeMaliDevice.h` | buffer lifecycle, IOVA quarantine, fence tracking, fault detection |
| `NativeMaliIokitTransport` | `src/transport/NativeMaliIokitTransport.h` | the only IOKit file; one `IOConnectCallStructMethod` |

What is deliberately **not** here: a fake success path, a private ioctl, a
userspace register write, or a second copy of the ABI records.

## When the kernel build exists

1. Point `MALI_G52_DIR` at the kernel checkout; `tests/run.sh` already compiles
   against `deps/abi.sha256`.
2. Build the real transport against that kernel's IOKit headers and run one
   `Query` on the phone.
3. Only then start MM3 (`docs/SEAM.md`), because a backend cannot be validated
   against an unproven service.
