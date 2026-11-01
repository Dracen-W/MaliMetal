<!-- SPDX-License-Identifier: GPL-2.0-only -->
# Mesa patch series

Status: **patch 0001 exists and is compiled in CI**; it is a seam, not a
driver. The rest of the plan below is unchanged. The base is
Mesa tag `mesa-26.2.6`, commit `8e87a503ebd91408c88596d1b9eeb5f4c0b2088d`
(`deps/deps.toml`, `docs/DEPENDENCIES.md`). Every line reference below was read
from that commit; any other revision is a different port.

The series adds a `pan_kmod` backend that speaks the `NativeMali` C facade
(`src/backend/native_mali_kmod.h`) instead of opening a DRM render node. It does
not touch the command stream: PanVK still builds the Bifrost v7 job chain.

## What the backend is allowed to call

Only the C facade. That surface is host-tested through the scripted service
(`tests/test_kmod_api.cpp`), and it exists so the Mesa patch does not include
C++ classes, IOKit or XNU.

| `struct pan_kmod_ops` member (pan_kmod.h:452) | Maps to | Support |
| --- | --- | --- |
| `dev_create` (:456) | `native_mali_kmod_create` + `native_mali_kmod_props` | yes; the ABI has no DRM fd, so the fake fd path is removed rather than emulated |
| `dev_destroy` | `native_mali_kmod_destroy` | yes |
| `dev_query_user_va_range` (:468) | `props.va_start` / `props.va_size` | yes |
| `bo_alloc` (:474) | `native_mali_kmod_alloc` | yes |
| `bo_free` (:479) | `native_mali_kmod_free` (guard fence when in flight) | yes |
| `vm_create` (:510) | no call: the client already owns one address space | local object only |
| `vm_destroy` (:514) | no call | local object only |
| `vm_bind` (:523) | `native_mali_kmod_map` | map/unmap only; no sparse or range ops |
| `bo_wait` (:500) | `native_mali_kmod_wait` on the tracked fence | **refused** when no fence is known; never "ready" on a guess |
| `bo_get_mmap_offset` (:494) | not supported | the ABI has a token (`cpu_map`), not an fd offset |
| `bo_import` / `bo_export` | not supported | `BoImport` is absent from v1 |
| `flush_bo_map_syncs` | no-op initially | non-coherent mapping policy is documented in `docs/SEAM.md` |
| evictable / perf ops | not supported | return the documented failure |

The signature shapes above are the pinned header's, not ours. Do not copy the
header into this repository; the patch refers to Mesa's own.

## What the ops table cannot fix

The JM queue does not submit through `pan_kmod`:

- `src/panfrost/vulkan/jm/panvk_vX_gpu_queue.c:79` and `:123`
  (`panvk_queue_submit_batch()`) call
  `pan_kmod_ioctl(dev->drm_fd, DRM_IOCTL_PANFROST_SUBMIT, &submit)`.
- The same file creates, waits and destroys DRM syncobjs
  (`:352 drmSyncobjCreate`, `:242`/`:316` syncobj assertions).
- `src/panfrost/vulkan/panvk_physical_device.c:74` opens a render node and
  `:87 drmGetVersion` requires `panfrost`/`panthor`.

So the series has to grow a submission path and a `vk_sync` type; a `pan_kmod`
backend alone is not enough. That is the same conclusion as `docs/SEAM.md`, and
it is why this directory is still empty.

## Order of patches (proposed)

| Patch | Content | Gate |
| --- | --- | --- |
| `0001-native-device-creation` | **written** (`patches/mesa/0001-...`): `pan_kmod_dev_create_native()` with no DRM fd, so a backend can supply ops and props | `.github/workflows/host-tests.yml` clones 26.2.6, applies it and builds `libpankmod_lib.a` with `-Dtools=panfrost` (the unit is identical; a driver configuration would also pull the CLC/LLVM chain, which this check does not need) |
| `0002-native-backend` | implement `pan_kmod_ops` over the C facade using ABI v2 (`mmu_features`, `mem_features`, `texture_features`, ...). Must refuse creation while `mmu_features` is zero, because that means the probe has not run | fake-service test; CI compile |

ABI v2 now carries the fields; the remaining input is the measured values.
| `0002-bo-vm` | alloc/free/map over the facade | fake-service test |
| `0003-bo-wait` | refuse unless a tracked fence proves completion | fake-service test |
| `0004-device-creation` | physical-device path without `/dev/dri`, sync types | needs the submission patch |
| `0004-native-queue` | **written**: a queue that submits through the ops and refuses semaphores/events | compiled by `mesa-panvk` |
| `0005-device-enumeration` | enumerate/创建 a device without `/dev/dri` (`panvk_physical_device.c`) | not written |
| `0006-native-sync` | a `vk_sync` type for opaque fences, replacing DRM syncobjs | not written |
| `0007-cache` | non-coherent mapping policy | needs the kernel cache-maintenance answer |

No patch may claim success. Each one must state what it does **not** make work
yet, in its commit message.

## What must be true before patch 0005

- `Mali-G52` has run a real job on the phone and a fence has completed
  (D5, `docs/DECISIONS.md`).
- The kernel ABI has answered the gaps in `docs/NATIVE_MALI_ABI.md`: job
  `requirements`, per-BO wait, reset semantics, cache maintenance.
- The target's PanVK builds at all (toolchain open item).

Until then this directory stays empty except for this plan. An unbuildable patch
that "looks right" is worse than no patch.
