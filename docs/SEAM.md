<!-- SPDX-License-Identifier: GPL-2.0-only -->
# Seam design: Option A (Mesa patch series)

Status: **design only**. No Mesa patch exists yet. The Mesa base is tag
`mesa-26.2.6`, commit `8e87a503ebd91408c88596d1b9eeb5f4c0b2088d`
(2026-10-28), from `https://gitlab.freedesktop.org/mesa/mesa`; the files and
sha256 hashes this design was checked against are in `deps/deps.toml` and
`docs/DEPENDENCIES.md`. Every file/function/line reference below is from that
commit. MM3 must run `python3 tools/deps.py verify` first and stop on a
mismatch.

This document describes the chosen seam: a native `pan_kmod` backend plus the
minimum PanVK changes needed to submit jobs and wait on fences through the
`NativeMali` ABI. It is not a kernel design and does not change
`NativeMaliABI.h`.

## Scope

In scope:

- PanVK on Bifrost v7 (JM backend);
- BO allocation, mapping and CPU views;
- one VM/address space per Metal client;
- job submission and fence completion;
- device-lost/reset reporting;
- host tests against a fake `NativeMali` service.

Out of scope:

- the Metal frontend, plugin and scanout (separate milestones);
- kernel changes (they go to `Mali-G52`);
- dma-buf/IOSurface import;
- DRM ioctl emulation;
- multi-GPU, multi-display, DVFS.

## Why the patch series is larger than a vtable

Mesa's `pan_kmod` vtable covers device properties, BOs, VM operations and waits,
but the JM queue bypasses it for submission and synchronization:

- `jm/panvk_vX_gpu_queue.c:panvk_queue_submit_batch()` builds a
  `drm_panfrost_submit` and calls
  `pan_kmod_ioctl(dev->drm_fd, DRM_IOCTL_PANFROST_SUBMIT, ...)`.
- The same file uses `drmSyncobjCreate`, `drmSyncobjWait`, `drmSyncobjDestroy`,
  `DRM_IOCTL_SYNCOBJ_HANDLE_TO_FD`, `DRM_IOCTL_SYNCOBJ_FD_TO_HANDLE` and
  `DRM_IOCTL_SYNCOBJ_RESET`.
- `panvk_per_arch(gpu_queue_submit)()` asserts that every wait/signal is a DRM
  syncobj.

Therefore the patch series has three parts:

1. a native `pan_kmod` backend for allocation/mapping/properties;
2. a submit operation that replaces the direct DRM submit;
3. a Vulkan sync type that can represent a `NativeMali` fence.

## Proposed Mesa additions

Names are proposals, not upstream API. Keep them in one patch series so a rebase
is a single conflict surface.

### 1. Backend descriptor and device creation

Upstream `pan_kmod_dev_create(int fd, ...)` requires a DRM fd. Add a
backend-neutral creation path, for example:

```c
/* proposed */
struct pan_kmod_native_ops {
   void *(*open)(const char *service_name);
   void (*close)(void *ctx);
   int (*query)(void *ctx, uint32_t method, void *in, size_t in_size,
                void *out, size_t out_size);
};
```

`pan_kmod_dev_create_native()` would fill `struct pan_kmod_dev_props` from
`Query`/`GetDeviceInfo` and set `dev->ops` to the native backend. The dev struct
must not carry a fake fd; code that still needs `dev->fd` must be changed or
compiled out for this backend.

Do not pretend a DRM version exists. If a code path genuinely needs a version,
give it an explicit capability flag instead.

### 2. BO and VM operations

Implement `struct pan_kmod_ops`:

| Op | NativeMali method | Notes |
| --- | --- | --- |
| `bo_alloc` | `BoAlloc` | size is exact; no implicit page rounding by Mesa |
| `bo_free` | `BoFree` | fail closed if the handle is unknown |
| `bo_get_mmap_offset` | — | **not used**; CPU map goes through a new op |
| `bo_cpu_map` (new) | `BoCpuMap` | returns a pointer plus a token; no fd/offset |
| `bo_cpu_unmap` (new) | `BoCpuUnmap` | token must match |
| `bo_wait` | — | **not supported** by v1; track fences or return `false` |
| `vm_create` | — | the client already owns one AS; return a local object |
| `vm_bind` | `BoMap` | caller supplies the IOVA from the userspace allocator |
| `vm_destroy` | — | no kernel call; the AS belongs to the client |
| `query_timestamp` | `GetDeviceInfo` / `GetStatus` | only if the ABI exposes it; do not invent a counter |

`bo_wait` is a correctness issue: PanVK may call it for resource readiness. The
first implementation must either:

- return `false`/`VK_ERROR_DEVICE_LOST` when it cannot prove readiness, or
- map the BO to the fences that reference it and wait on those.

Returning `true` without evidence is forbidden.

### 3. Submit operation

Add a backend-neutral submit op, for example:

```c
/* proposed */
struct pan_kmod_submit {
   uint64_t job_chain;          /* Bifrost v7 chain, already built by PanVK */
   uint64_t wait_fence;         /* 0 or an earlier fence */
   uint64_t wait_timeout_ns;
   uint32_t flags;              /* 0 */
   uint32_t reserved;
};
int pan_kmod_submit(struct pan_kmod_dev *dev, const struct pan_kmod_submit *s,
                    uint64_t *out_fence);
```

The JM queue then calls `pan_kmod_submit()` instead of
`DRM_IOCTL_PANFROST_SUBMIT`. It must not pass a BO handle list: `NativeMali`
already validates the chain's GPU addresses against the client's mappings. That
is a deliberate difference from `drm_panfrost_submit`.

What is lost:

- `requirements` (`PANFROST_JD_REQ_FS`) has no ABI field. The first job runs with
  all present shader cores. If the fragment job misbehaves, the options are a
  kernel ABI addition or a PanVK-side workaround; do not fake the requirement.
- `in_syncs`/`out_sync` become a single opaque fence. PanVK's batch ordering is
  expressed by waiting on the previous batch's fence before submitting the next
  one. This is correct but may be slower than a multi-fence ioctl.

### 4. Sync type

PanVK needs a `vk_sync` type that is not a DRM syncobj. Proposed semantics:

- a sync object owns one `NativeMali` fence (or a "never signaled" state);
- `wait` calls `WaitFence(fence, timeout)`;
- `signal` is only meaningful for a fence that a submit produced; do not invent
  a userspace signal;
- `reset` destroys the reference, not the GPU work;
- `import`/`export` are unsupported until the ABI grows a handle type;
- timeline semantics are unsupported.

The JM queue's `vk_sync_type_is_drm_syncobj()` assumptions must be replaced with
a generic "supports wait/signal" check. Event syncobjs (`panvk_event`) need the
same treatment; if the first bring-up does not use events, return an explicit
unsupported error instead of a fake syncobj.

### 5. Device lost and reset

`GetStatus()` is the only source of truth for faults and resets. The backend
must:

- detect a reset between submissions and before a wait;
- fail in-flight command buffers with `VK_ERROR_DEVICE_LOST`;
- never re-submit a fence that the kernel reports as destroyed;
- re-query device info after a reset if the kernel says the device is usable
  again.

`NativeMali::Reset` is device-wide. A PanVK device that calls it must assume
every other client's work is gone; the Metal frontend must surface that.

## Userspace IOVA allocator

`NativeMali` requires the caller to choose the IOVA. The native backend owns an
allocator scoped to the client's address space.

Requirements:

- 4 KiB granularity;
- no overlap with an existing mapping;
- no reuse while a submitted job may still reference the range;
- explicit reserve/free, not just bump allocation;
- host-tested with a fake page table.

Proposed first implementation: first-fit over a sorted free list, with a
"quarantine" list for ranges freed while a fence is outstanding. Release a
quarantined range only after the fence completes. If the fence is lost to a
reset, release the range because the GPU can no longer access the old AS state;
record that decision in a test.

Status: implemented as `src/native_mali/NativeMaliIovaAllocator.{h,cpp}` and
host-tested (`tests/test_iova_allocator.cpp`): first fit, merge on release,
exact-size free only, quarantine by fence equality, `release_all()` on reset.
It is pure logic and makes no kernel call; the backend wires it to `BoMap`.

The allocator must not use the whole 48-bit space blindly. `Query` reports the
usable VA size; use it.

## CPU mapping and cache policy

The backend gets a CPU pointer and a token from `BoCpuMap`. Two policies:

1. **Safe first:** map non-cacheable. Correct, slower, no cache instructions.
2. **Later:** map write-back cacheable and perform the exact clean/invalidate
   around GPU use.

Do not claim `is_io_coherent`. PanVK's non-coherent path
(`pan_kmod_queue_bo_map_sync()` / `pan_kmod_flush_bo_map_syncs()`) must either
use the target's cache primitives or be bypassed by policy 1. The choice is
recorded in the backend and tested with a fake cache-maintainer.

## Patch series layout

Proposed order. Each patch must apply to the pinned Mesa commit and keep Mesa's
MIT headers.

| Patch | Content | Test |
| --- | --- | --- |
| `0001-native-backend-skeleton` | build guard, backend descriptor, no-op device | builds; no GPU call |
| `0002-device-props` | `Query`/`GetDeviceInfo` → `pan_kmod_dev_props` | fake replies, refusal on short record |
| `0003-bo-vm` | BO alloc/free/map, IOVA allocator | fake service, overlap/quarantine |
| `0004-cpu-map` | `BoCpuMap`/`BoCpuUnmap`, cache policy | token mismatch, double map |
| `0005-submit` | `pan_kmod_submit`, JM queue replacement | one job, fence round-trip, timeout |
| `0006-sync` | native `vk_sync` type, queue/event changes | wait/signal, device lost |
| `0007-reset` | `GetStatus`/`Reset`, `VK_ERROR_DEVICE_LOST` | reset during wait, fence invalidation |
| `0008-tests` | host tests, fake service, failure injection | full matrix |

Do not combine a rebase with a behaviour change. Do not add a patch that only
"makes it compile" without a test or a documented reason.

## Test matrix

Host tests, no phone:

- device props: valid, short record, unknown version, unsupported GPU;
- BO: alloc/free, double free, map at 0, unaligned IOVA, overlapping map,
  unmap while a job is outstanding;
- CPU map: double map, wrong token, unmap after free;
- submit: one job, chain misaligned, chain outside a mapped BO, wait timeout,
  wait on another client's fence;
- reset: `GetStatus` changes, in-flight fence becomes an error, new submit
  refused or accepted according to the kernel's state;
- sync: wait/signal, reset, unsupported timeline, unsupported import/export.

Hardware tests, after the kernel prerequisites:

- one WRITE_VALUE job;
- one PanVK compute dispatch;
- one PanVK render pass;
- a forced fault → device lost → recovery (or a documented refusal).

## Risks

- **Mesa rebase cost.** The JM queue and sync layer are not a small patch.
  Mitigation: one patch series, one pinned base, a test that applies the series
  and fails loudly.
- **v7 support.** PanVK marks v7 as not well-tested and requires
  `PAN_I_WANT_A_BROKEN_VULKAN_DRIVER=1`. This is independent of the seam and
  remains open.
- **`bo_wait` semantics.** A wrong "ready" corrupts rendering. Fail closed
  until fence tracking is proven.
- **Cache coherency.** A wrong mapping attribute corrupts textures. Start
  non-cacheable.
- **Fence lifetime.** A fence is opaque; never decode it. Track ownership in one
  place.
- **Kernel ABI gaps.** `requirements`, `flush id`, import and cache maintenance
  may force a v2 ABI. Do not paper over them.

## Definition of done for MM3

- The patch series applies cleanly to the pinned Mesa commit.
- A host build of PanVK with the native backend succeeds without a phone.
- Fake-service tests cover the matrix above.
- No code path calls `drmIoctl`, `drmSyncobj*` or `os_mmap(fd, ...)` for the
  native backend.
- Every unsupported operation returns an explicit error, not a default.
- The kernel has already run a real job; otherwise MM3 stops at the host build.
