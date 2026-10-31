/* SPDX-License-Identifier: GPL-2.0-only
 *
 * Integration test: the userspace client and lifecycle against a scripted
 * service that applies the driver's rules. No kernel, no phone, no GPU.
 */
#include "NativeMaliDevice.h"
#include "fake_native_mali_service.h"

#include "NativeMaliABI.h"

#include <cstdio>

namespace {

int failures = 0;

void expect(bool condition, const char *what) {
    if (!condition) {
        std::fprintf(stderr, "FAIL %s\n", what);
        ++failures;
    }
}

} /* namespace */

int main() {
    FakeNativeMaliService service;
    NativeMaliClient client(service);
    NativeMaliBoTable buffers;
    NativeMaliIovaAllocator iova(0x100000, 0x10000);
    NativeMaliFenceTracker fences;
    NativeMaliDevice device(client, buffers, iova, fences);

    /* The read-only facts come from the service, not from a guess. */
    NativeMaliAbiQuery query{};
    expect(client.query(&query) == NativeMaliError::None, "query against the fake service");
    expect(query.abi_version == NativeMaliAbiVersion, "ABI version echoed");
    expect(query.max_bo_bytes == (UINT64_C(1) << NativeMaliAbiMaxBoBytesShift),
           "BO limit reported");

    NativeMaliAbiDeviceInfo info{};
    expect(client.device_info(&info) == NativeMaliError::None, "device info");
    expect((info.flags & NativeMaliDeviceNonCoherent) != 0, "device is non-coherent");

    /* Allocate and map through the lifecycle; the fake checks the records. */
    constexpr uint64_t kSize = 2 * NativeMaliAbiPageBytes;
    uint64_t handle = 0;
    expect(device.alloc(kSize, &handle) == NativeMaliError::None, "alloc");
    expect(service.live_buffers() == 1, "service holds one buffer");

    uint64_t mapped = 0;
    expect(device.map(handle, NativeMaliBoRead | NativeMaliBoWrite, &mapped) ==
               NativeMaliError::None,
           "map");
    expect(mapped == 0x100000, "mapping at the allocator base");
    const FakeNativeMaliBuffer *buffer = service.find_buffer(handle);
    expect(buffer != nullptr && buffer->mapped && buffer->iova == mapped,
           "service recorded the mapping");

    /* Overlapping mappings are refused by the service, so a client that asked
     * for one gets a refusal, not two owners of the same range. */
    uint64_t second_handle = 0;
    expect(device.alloc(kSize, &second_handle) == NativeMaliError::None, "second alloc");
    uint64_t overlapping = 0;
    expect(client.bo_map(second_handle, mapped, NativeMaliBoRead, &overlapping) ==
               NativeMaliError::InvalidArgument,
           "overlapping map refused");

    /* A job chain must start inside a mapped buffer. */
    uint64_t fence = 0;
    expect(device.submit(mapped + 0x4000, 0, 0, &fence) == NativeMaliError::InvalidArgument,
           "chain outside the mapping refused");
    expect(device.submit(mapped, 0, 0, &fence) == NativeMaliError::None, "submit a chain");
    expect(service.pending_fences() == 1, "service has one pending fence");

    expect(device.wait(fence, 0) == NativeMaliError::None, "wait for the submitted fence");
    expect(service.pending_fences() == 0, "fence completed in the service");
    expect(fences.state(fence) == NativeMaliFenceState::Signaled, "fence signaled");

    /* A fence the client never received is refused. */
    expect(device.wait(0xFFFF, 0) == NativeMaliError::InvalidArgument,
           "unknown fence refused");

    /* CPU view on the second buffer. */
    uint32_t token = 0;
    uint64_t cpu_bytes = 0;
    expect(device.cpu_map(second_handle, &token, &cpu_bytes) == NativeMaliError::None,
           "cpu map");
    expect(token != 0 && cpu_bytes == kSize, "token and size from the service");
    expect(device.cpu_unmap(second_handle, token) == NativeMaliError::None, "cpu unmap");

    /* Reset: pending work is dropped, buffers and mappings survive. */
    uint64_t pending = 0;
    expect(device.submit(mapped, 0, 0, &pending) == NativeMaliError::None, "submit before reset");
    expect(service.pending_fences() == 1, "one pending before reset");
    expect(device.reset() == NativeMaliError::None, "reset");
    expect(service.pending_fences() == 0, "service dropped in-flight fences");
    expect(fences.state(pending) == NativeMaliFenceState::Lost, "client lost the fence");
    expect(service.live_buffers() == 2, "buffers survive the reset");
    expect(buffers.count() == 2, "client records survive the reset");
    expect(buffer->mapped, "mappings survive the reset");
    expect(device.wait(pending, 0) == NativeMaliError::DeviceLost,
           "waiting on a fence lost to reset reports device lost");

    /* A fault reported by the counters is DeviceLost, and the surviving
     * buffers can still be freed after the reset. */
    expect(device.refresh_status() == NativeMaliError::None, "status baseline");
    service.faults = 1;
    expect(device.refresh_status() == NativeMaliError::DeviceLost, "fault surfaces");
    expect(device.reset() == NativeMaliError::None, "recover");
    expect(device.free(handle, 0) == NativeMaliError::None, "free after recovery");
    expect(device.free(second_handle, 0) == NativeMaliError::None, "free the second buffer");
    expect(service.live_buffers() == 0, "service owns no buffers");
    expect(iova.allocated_bytes() == 0, "allocator is empty");

    if (failures != 0) {
        std::fprintf(stderr, "%d check(s) failed\n", failures);
        return 1;
    }
    std::printf("OK   fake service: records, mapping, chains, reset, fault, refusals\n");
    return 0;
}
