<!-- SPDX-License-Identifier: GPL-2.0-only -->
# The Metal object C ABI (imported, not yet implemented)

`NativeMetalObjects.h` is AdrenoMetal's C ABI between the Objective-C Metal
frontend and the `metal2vulkan` runtime. It is imported here unchanged because
the port keeps that contract; the Mali-specific work is behind it.

## Provenance

```text
AdrenoMetal
https://github.com/UniXNU/AdrenoMetal
Pinned revision: ce5e19e33e05e1873d38d818e23cabfe0ac708c3
path: bridge/NativeMetalObjects.h
licence: MIT (SPDX line in the file; full text in NOTICE)
sha256: 40c30763bad51f551d9eaf8c956acbeb2ea8c5064802a213d9b3c337efe24837
```

The file is **byte-identical** to the pinned revision and must stay that way.
`tests/test_pins.py` checks the hash. If the port ever needs a different ABI,
that is a fork with its own header and its own entry in `NOTICE` — not an edit
to a file whose whole value is that it matches upstream.

## What it is

Every Metal object is an opaque handle (`NmbDevice`, `NmbBuffer`, `NmbTexture`,
`NmbQueue`, `NmbEncoder`, ...). Failures return `0`/`NULL` and leave a reason in
`nmb_last_error()`. Metal's raw enumeration values cross as-is. The frontend
never sees Vulkan or PanVK types.

## What has to be implemented for Mali

The runtime behind this header (`metal2vulkan`, pinned separately) is reused, so
most of the ABI is answered by it. The Mali-specific parts are the ones that
assume a device and a driver:

- device open against the target's Vulkan ICD (PanVK on Mali, not Turnip);
- format and tiling queries, which PanVK answers;
- cache and memory-type decisions, which interact with the non-coherent device
  (`NativeMaliDeviceNonCoherent` in the kernel ABI);
- scanout and IOSurface paths, which are MT6768/UniXNU work, not ABI work.

## What Mali does not have, and must refuse rather than fake

Some of the header exists because the Adreno port needed it. On Mali these have
no honest answer yet and must fail closed (return `false`/`NULL` with a reason
in `nmb_last_error()`), not return a plausible default:

- the embedded AIR fixtures (`nmb_fixture_air`, `nmb_function_constant_air`) —
  they exist for the Adreno runtime's own tests;
- device family waivers and Apple-private selector answers that encode Adreno
  behaviour;
- any capability that would require a Metal feature PanVK does not expose.

The exact list is decided when the runtime is built for Mali (MM5), not now.

## Status

Imported for the contract only. There is no implementation, no `metal2vulkan`
build, and no Vulkan device in this repository yet. `tests/run.sh` only checks
that the header compiles on its own as C11 and C++17.
