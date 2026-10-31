<!-- SPDX-License-Identifier: GPL-2.0-only -->
# The NativeMali ABI as seen from MaliMetal

The canonical definition is `Mali-G52/NativeMaliABI.h`. This document explains
the subset a Vulkan driver needs and where the current ABI does not line up with
PanVK. It does not replace the header, and it must not be used to duplicate the
records.

## Header provenance

This repository never carries a second copy of the header. The copy that the
userspace code compiles against is generated:

```sh
python3 tools/sync-abi.py            # default: the sibling Mali-G52 checkout
python3 tools/sync-abi.py --kernel /path/to/Mali-G52
```

It copies the header into `build/abi/` (gitignored) and refuses unless its
sha256 equals the one in `deps/abi.sha256`. `--accept` records a new hash, but
refuses when the ABI version or the last method selector also moved: that is an
ABI decision for `Mali-G52`, not a hash bump.

`src/native_mali/NativeMaliAbiTable.cpp` restates the selectors and record
sizes as a table. It is checked two ways: `static_assert` against the pinned
header, and a host test (`tests/test_abi_table.cpp`). If the kernel header
changes shape, the build fails here before a malformed record reaches the
service.

`src/native_mali/NativeMaliClient.{h,cpp}` is the call path above the table: it
validates the selector and sizes, calls a `NativeMaliTransport` (the IOConnect
call on the phone, a scripted fake in tests), maps `kIOReturn*` onto a small
internal error enum, and leaves the caller's output untouched unless the call
succeeded. `Query` also refuses a reply whose ABI version or `max_bo_bytes`
disagrees with the header. Every selector is wrapped; the request-side rules
(alignment, nonzero handle/IOVA/fence, read permission, the 64-byte chain
alignment) are enforced before the service sees the record, and a reply that
does not echo the requested IOVA, or that carries a zero handle/token/fence, is
refused. `tests/test_native_mali_client.cpp` covers the success path, every
refusal and the untouched-output rule.

`src/native_mali/NativeMaliIovaAllocator.{h,cpp}` is the address allocator the
ABI expects userspace to supply: 4 KiB granularity, nonzero addresses, first fit
over a sorted free list, merging on release. A range freed while a job may still
read it is quarantined behind that exact fence (fences are opaque, so matching is
by equality) and released only when the fence completes or the device resets.

`src/native_mali/NativeMaliBoTable.{h,cpp}` is the fixed-capacity record of the
buffers this client owns: opaque handle, size, IOVA, permissions and CPU token.
It refuses a duplicate handle, a second mapping, an unaligned or zero IOVA, a
write-only mapping (the header requires read), an unknown permission bit, a
token that does not match, and a full table.

`src/native_mali/NativeMaliFenceTracker.{h,cpp}` records fences by identity
only. A fence cannot be compared or decoded, so the tracker has three outcomes
(`Pending`, `Signaled`, `Lost`) and no ordering. A reset turns every pending
fence into `Lost` and never into `Signaled`; a lost fence cannot be revived by a
later successful wait.

`src/native_mali/NativeMaliDevice.{h,cpp}` is the lifecycle above them:

- `alloc` asks the kernel, then records; a refused record frees the kernel
  allocation instead of leaking it;
- `map` reserves an IOVA, asks the kernel, then records; a refused call or a
  reply that moved the address releases the reservation;
- `free` asks the kernel first, then drops the record and releases the address;
  a guarded free quarantines the IOVA until that exact fence completes;
- `wait` marks the fence `Signaled` and releases its quarantined ranges only
  when `WaitFence` succeeded;
- `reset` loses pending fences and releases quarantined ranges, and keeps the
  buffers and mappings, as the header says `Reset` does;
- `refresh_status` reads `GetStatus` and compares it with the previous reading:
  any change in `faults` or `resets` loses pending fences, releases quarantined
  IOVAs and reports `DeviceLost` until a reset succeeds. The first reading is a
  baseline only, and the caller owns the polling cadence.

Every one of those paths is covered by `tests/test_device_lifecycle.cpp`,
including the rollbacks and the "lost fence cannot be revived" case.

`src/transport/NativeMaliIokitTransport.{h,cpp}` is the only file that touches
IOKit: one `IOConnectCallStructMethod` call, no policy. It refuses a null
connection, an output record larger than the ABI's biggest, and a missing output
pointer; it writes the reply to a local record and copies it only when the call
succeeded and reported exactly the requested size. The host test proves the
refusal half only; the success half needs the kext.

`tests/fake_native_mali_service.h` is a scripted stand-in for the kernel
service: it applies the driver's rules (handle exists, one map per buffer, no
overlapping IOVAs, the chain lies inside a mapping, the fence belongs to the
client, reset drops in-flight fences and keeps buffers, status counters) and
`tests/test_fake_service.cpp` drives the whole lifecycle through it. It is a
test double, not a GPU model; it exists so a mistake in the record handling
fails on the host instead of in the driver.

The ABI is fixed-size, little-endian, ARM64-to-ARM64. Every method has exactly
one input record and one output record, either of which may be absent. A
mismatched size, version, method or flag is refused, never ignored. An output
record is untouched unless the call succeeded.

## Methods

| # | Name | Input | Output | MaliMetal use |
| --- | --- | --- | --- | --- |
| 1 | `Query` | 24 bytes | same | version, address bits, max BO bytes |
| 2 | `GetDeviceInfo` | none | 64 bytes | GPU ID, masks, limits, feature words, non-coherent flag, VA range |
| 3 | `BoAlloc` | 8 bytes | 8 bytes | allocate a BO, get an opaque handle |
| 4 | `BoMap` | 24 bytes | 8 bytes | map at a caller-chosen IOVA |
| 5 | `BoFree` | 8 bytes | none | destroy a BO |
| 6 | `BoCpuMap` | 8 bytes | 16 bytes | get a memory-type token for the CPU view |
| 7 | `BoCpuUnmap` | 16 bytes | none | drop the CPU view |
| 8 | `Submit` | 32 bytes | 8 bytes | submit a Bifrost v7 job chain, get an opaque fence |
| 9 | `WaitFence` | 16 bytes | none | wait for completion or dependency |
| 10 | `GetStatus` | none | 16 bytes | fault and reset counters |
| 11 | `Reset` | none | none | device-wide recovery |

## What maps cleanly

- **Query / GetDeviceInfo** → `pan_kmod_dev_props` and the Vulkan physical
  device. `gpu_id`, `shader_present`, `tiler_features`, `l2_features`,
  `core_features`, `max_threads`, `max_workgroup_size`, `max_barrier_size`,
  `va_size` and the non-coherent flag all have a home. The kernel reads them
  from the M3 probe, so userspace does not guess.
- **BoAlloc / BoFree** → `pan_kmod_bo_alloc()` / `pan_kmod_bo_put()`. The
  handle is opaque; do not use it as a DRM GEM name.
- **BoMap** → `pan_kmod_vm_bind()` map. The IOVA is caller-chosen, so the native
  backend supplies a userspace allocator; PanVK must not assume the kernel
  assigns the VA.
- **Submit** → a Bifrost v7 job chain. The kernel picks the job slot; the
  caller does not name one.
- **WaitFence** → completion/dependency. The fence is opaque: round-trip it,
  never decode it.
- **GetStatus / Reset** → device-lost detection and recovery. Reset is
  device-wide; it destroys every client's in-flight job and their fences answer
  `IOError`.

## Gaps and consequences

### No job `requirements` or affinity

Panfrost computes a job's shader-core affinity from its requirements. The
current ABI drives `JS_AFFINITY_NEXT` with all present shader cores and has no
field for requirements. For the first job this is acceptable. For correct
IDVS/tiler scheduling it may not be. Do not add a field in this repository;
record the need and, if measurement proves it, change the kernel ABI in
`Mali-G52`.

### No client-chosen flush id

The service reads `GPU_LATEST_FLUSH_ID` itself. PanVK/Panfrost may want to
choose one for cache/tiler ordering. The current behaviour is kernel-chosen;
verify correctness before assuming it is equivalent.

### No per-BO wait

`pan_kmod_bo_wait()` waits for a BO to be idle. The ABI has only job fences, not
BO queries. Options: track which fences reference a BO in the native backend and
wait on those, or add a kernel method. Do not return "ready" without evidence.

### No mmap offset

`pan_kmod_bo_mmap()` expects an fd and an offset. `NativeMali` gives a CPU
mapping through a memory-type token (`BoCpuMap`) that the IOUserClient's
`clientMemoryForType()` resolves. The native backend must bypass the fd/offset
path and use the token; this is one of the reasons a vtable-only `pan_kmod`
backend is insufficient.

### No dma-buf or IOSurface import

`BoImport` is deliberately absent. Zero-copy IOSurface textures and dma-buf
export/import are not available. The first frame path copies into a BO. The cost
and the correct place to add import are open questions.

### No DRM syncobj or timeline

PanVK's `get_device_sync_types()` asks the DRM fd for syncobj features and uses
timeline sync objects. `NativeMali` fences are opaque, polled and per-client.
The native backend needs a userspace `vk_sync` implementation or a documented
restriction. Faking DRM syncobj semantics is not acceptable.

### No cache-maintenance method

The device is non-coherent. The kernel has a cache-maintainer interface
internally, but the ABI does not expose it. Userspace must either use
non-cacheable mappings or perform the correct `dc cvac`/`dc ivac` (or
`sys_dcache_flush`) operations. Record which one the mapping requires; do not
claim coherency.

### One VM / one address space per client, three in total

`NativeMali` allocates one AS per open client, and the device has three present
AS (`GPU_AS_PRESENT == 0x7`). PanVK assumes one VM per device, which matches, but
a process that opens several devices can starve the others. The backend must
report this limit honestly; it must not silently reuse another client's AS.

### 4 KiB pages only

`NativeMaliVm` maps 4 KiB leaves. PanVK may prefer 2 MiB blocks for large
allocations. The first implementation uses 4 KiB only and documents the
performance cost. Large pages are a later kernel milestone.

### Polled fences, no interrupt-driven completion

`WaitFence` polls. For bring-up this is enough. For a compositor it burns CPU
and adds latency. Interrupt-driven completion is a kernel milestone, not a
MaliMetal workaround.

### Device-wide reset

Any connected client may reset the device. A reset destroys every client's
in-flight jobs. MaliMetal must treat this as `VK_ERROR_DEVICE_LOST`: fail the
command buffers, recreate what it can, and re-query status. It must not retry a
destroyed fence as if the work had completed.

## What MaliMetal must never do

- Call a method with a record size other than the struct size.
- Decode an opaque fence; only round-trip it.
- Choose a job slot.
- Assume a BO is idle because no fence is known.
- Assume the kernel assigned an IOVA.
- Write a GPU register or map `0x13040000`.
- Add a private ioctl or a shadow ABI.
- Report a device ready after a partial `Query`/`GetDeviceInfo`.

## Measurement still needed

The following values are not yet measured on this phone and must come from the
kernel probe before PanVK can be configured:

- `GPU_MMU_FEATURES` (`ias`/`oas`);
- shader/tiler present masks;
- thread limits and `THREAD_TLS_ALLOC`;
- L2/core/tiler/memory feature words;
- the actual BO initial voltage and the usable GPU clock.

They belong in `Mali-G52/docs/DEVICE.md`, not in this repository.
