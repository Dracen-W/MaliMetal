/* SPDX-License-Identifier: GPL-2.0-only
 *
 * Host test for the buffer lifecycle and fence bookkeeping: allocation rollback,
 * mapping reservation, guarded free, wait semantics and reset.
 */
#include "NativeMaliDevice.h"

#include "NativeMaliABI.h"

#if defined(__APPLE__)
#include <IOKit/IOReturn.h>
#endif

#include <cstdio>

namespace {

int failures = 0;

void expect(bool condition, const char *what) {
    if (!condition) {
        std::fprintf(stderr, "FAIL %s\n", what);
        ++failures;
    }
}

struct FakeTransport final : NativeMaliTransport {
    uint64_t next_handle = 0x100;
    bool fixed_handle = false;
    uint64_t fixed_handle_value = 0x777;
    uint64_t next_fence = 0x1000;
    bool echo_map_iova = true;
    int wait_result = 0;
    uint32_t cpu_token = 7;

    unsigned alloc_calls = 0;
    unsigned free_calls = 0;
    unsigned map_calls = 0;
    unsigned wait_calls = 0;
    unsigned reset_calls = 0;
    uint64_t status_faults = 0;
    uint64_t status_resets = 0;

    int call(uint32_t selector, const void *input, size_t, void *output, size_t) override {
        switch (selector) {
            case NativeMaliMethodBoAlloc: {
                ++alloc_calls;
                auto *reply = static_cast<NativeMaliAbiBoAllocReply *>(output);
                reply->handle = fixed_handle ? fixed_handle_value : next_handle++;
                return 0;
            }
            case NativeMaliMethodBoMap: {
                ++map_calls;
                const auto *request = static_cast<const NativeMaliAbiBoMap *>(input);
                auto *reply = static_cast<NativeMaliAbiBoMapReply *>(output);
                reply->iova = echo_map_iova ? request->iova : request->iova + 0x1000;
                return 0;
            }
            case NativeMaliMethodBoFree: {
                ++free_calls;
                return 0;
            }
            case NativeMaliMethodBoCpuMap: {
                auto *reply = static_cast<NativeMaliAbiBoCpuMapReply *>(output);
                reply->token = cpu_token;
                reply->bytes = 8192;
                return 0;
            }
            case NativeMaliMethodSubmit: {
                auto *reply = static_cast<NativeMaliAbiSubmitReply *>(output);
                reply->fence = next_fence++;
                return 0;
            }
            case NativeMaliMethodWaitFence: {
                ++wait_calls;
                return wait_result;
            }
            case NativeMaliMethodReset: {
                ++reset_calls;
                return 0;
            }
            case NativeMaliMethodGetStatus: {
                auto *reply = static_cast<NativeMaliAbiStatus *>(output);
                reply->faults = status_faults;
                reply->resets = status_resets;
                return 0;
            }
            default:
                return 0;
        }
    }
};

} /* namespace */

int main() {
    FakeTransport transport;
    NativeMaliClient client(transport);
    NativeMaliBoTable buffers;
    NativeMaliIovaAllocator iova(0x100000, 0x10000);
    NativeMaliFenceTracker fences;
    NativeMaliDevice device(client, buffers, iova, fences);

    constexpr uint64_t kSize = 2 * NativeMaliAbiPageBytes;

    /* Allocate, map, look at the CPU view, free without a guard. */
    uint64_t handle = 0;
    expect(device.alloc(kSize, &handle) == NativeMaliError::None, "alloc");
    expect(handle == 0x100, "handle from the service");
    expect(buffers.count() == 1, "buffer recorded");
    expect(buffers.find(handle) != nullptr && !buffers.find(handle)->mapped,
           "new buffer is unmapped");

    uint64_t mapped = 0;
    expect(device.map(handle, NativeMaliBoRead | NativeMaliBoWrite, &mapped) ==
               NativeMaliError::None,
           "map");
    expect(mapped == 0x100000, "map reserves at the allocator base");
    expect(iova.allocated_bytes() == kSize, "map reserves the buffer size");
    expect(buffers.find(handle)->mapped && buffers.find(handle)->iova == mapped,
           "mapping recorded");

    expect(device.map(handle, NativeMaliBoRead, &mapped) == NativeMaliError::InvalidArgument,
           "a second map is refused before any call");

    uint32_t token = 0;
    uint64_t cpu_bytes = 0;
    expect(device.cpu_map(handle, &token, &cpu_bytes) == NativeMaliError::None, "cpu map");
    expect(token == 7 && cpu_bytes == 8192, "cpu token and size copied");
    expect(device.cpu_unmap(handle, token) == NativeMaliError::None, "cpu unmap");
    expect(device.cpu_unmap(handle, token) == NativeMaliError::InvalidArgument,
           "a second cpu unmap is refused");

    const unsigned frees_before = transport.free_calls;
    expect(device.free(handle, 0) == NativeMaliError::None, "free");
    expect(transport.free_calls == frees_before + 1, "free reaches the service");
    expect(buffers.count() == 0, "buffer record dropped");
    expect(iova.allocated_bytes() == 0 && iova.free_bytes() == 0x10000,
           "address released immediately");
    expect(iova.quarantine_count() == 0, "no quarantine without a guard");

    /* A guarded free keeps the address until that exact fence completes. */
    uint64_t guard = 0;
    expect(device.submit(0x1FF00, 0, 0, &guard) == NativeMaliError::None, "guarded submit");
    uint64_t guarded_handle = 0;
    expect(device.alloc(kSize, &guarded_handle) == NativeMaliError::None, "guarded alloc");
    expect(device.map(guarded_handle, NativeMaliBoRead, &mapped) == NativeMaliError::None,
           "guarded map");
    expect(device.free(guarded_handle, guard) == NativeMaliError::None, "guarded free");
    expect(iova.quarantine_count() == 1, "guarded range quarantined");
    expect(iova.free_bytes() == 0x10000 - kSize, "quarantined range is not free");

    expect(device.wait(guard, 0) == NativeMaliError::None, "wait for the guard");
    expect(fences.state(guard) == NativeMaliFenceState::Signaled, "guard fence signaled");
    expect(iova.quarantine_count() == 0, "quarantined range released after the wait");
    expect(iova.free_bytes() == 0x10000, "whole range free again");

    /* A dependency must be a fence this client received and still owns. */
    uint64_t unused_fence = 0;
    expect(device.submit(0x20000, 0xDEAD, 0, &unused_fence) ==
               NativeMaliError::InvalidArgument,
           "unknown dependency fence refused");

    /* Waiting on a fence this client never submitted is a caller bug. */
    expect(device.wait(0xDEAD, 0) == NativeMaliError::InvalidArgument,
           "wait on an unknown fence is refused");

    /* Submit tracks the fence; a failed wait leaves it pending and releases
     * nothing. */
    uint64_t fence = 0;
    expect(device.submit(0x20000, 0, 0, &fence) == NativeMaliError::None, "submit");
    expect(fences.state(fence) == NativeMaliFenceState::Pending, "submitted fence is pending");

    transport.wait_result = kIOReturnBusy;
    expect(device.wait(fence, 5) == NativeMaliError::Busy, "a busy wait is reported");
    expect(fences.state(fence) == NativeMaliFenceState::Pending, "a failed wait keeps it pending");

    transport.wait_result = 0;
    expect(device.wait(fence, 5) == NativeMaliError::None, "the fence completes");
    expect(fences.state(fence) == NativeMaliFenceState::Signaled, "fence signaled");

    /* Reset loses pending fences, releases quarantined ranges, and keeps
     * buffers and mappings. */
    uint64_t pending_fence = 0;
    expect(device.submit(0x20040, 0, 0, &pending_fence) == NativeMaliError::None,
           "submit before reset");
    expect(fences.pending_count() == 1, "one pending fence before reset");

    uint64_t reset_handle = 0;
    expect(device.alloc(kSize, &reset_handle) == NativeMaliError::None, "alloc before reset");
    expect(device.map(reset_handle, NativeMaliBoRead, &mapped) == NativeMaliError::None,
           "map before reset");
    expect(device.free(reset_handle, pending_fence) == NativeMaliError::None,
           "guarded free before reset");
    expect(iova.quarantine_count() == 1, "quarantined before reset");

    expect(device.reset() == NativeMaliError::None, "reset");
    expect(transport.reset_calls == 1, "reset reaches the service");
    expect(fences.state(pending_fence) == NativeMaliFenceState::Lost, "pending fence is lost");
    expect(fences.pending_count() == 0, "nothing pending after reset");
    expect(iova.quarantine_count() == 0, "quarantine released by reset");
    expect(fences.state(fence) == NativeMaliFenceState::Signaled,
           "an already signaled fence keeps its outcome");

    /* A lost fence cannot be revived, nor used as a dependency. */
    expect(device.wait(pending_fence, 0) == NativeMaliError::DeviceLost,
           "waiting on a lost fence reports device lost");
    expect(device.submit(0x20000, pending_fence, 0, &fence) == NativeMaliError::DeviceLost,
           "a lost dependency is refused as device lost");

    /* Allocation rollback: the service returns a handle the record refuses. */
    transport.fixed_handle = true;
    transport.fixed_handle_value = 0x777;
    uint64_t first = 0, second = 0;
    expect(device.alloc(NativeMaliAbiPageBytes, &first) == NativeMaliError::None,
           "rollback: first alloc records");
    const unsigned rollback_frees = transport.free_calls;
    expect(device.alloc(NativeMaliAbiPageBytes, &second) == NativeMaliError::Unsupported,
           "rollback: duplicate handle is refused");
    expect(transport.free_calls == rollback_frees + 1,
           "rollback: the refused allocation is freed in the kernel");
    transport.fixed_handle = false;

    /* Mapping rollback: a reply that moved the address releases the reservation. */
    const uint64_t free_before = iova.free_bytes();
    transport.echo_map_iova = false;
    uint64_t rollback_handle = 0;
    expect(device.alloc(NativeMaliAbiPageBytes, &rollback_handle) == NativeMaliError::None,
           "rollback map: alloc");
    expect(device.map(rollback_handle, NativeMaliBoRead, &mapped) ==
               NativeMaliError::Unsupported,
           "rollback map: a moved address is refused");
    expect(iova.free_bytes() == free_before, "rollback map: the reservation is released");
    expect(!buffers.find(rollback_handle)->mapped, "rollback map: buffer stays unmapped");
    transport.echo_map_iova = true;

    /* A buffer this client does not know cannot be freed. */
    expect(device.free(0x999, 0) == NativeMaliError::InvalidArgument, "unknown free refused");

    /* Fault detection: the first reading is only a baseline. Any later change
     * to faults or resets means in-flight work is gone. */
    expect(iova.quarantine_count() == 0, "no quarantine before the fault check");
    expect(device.refresh_status() == NativeMaliError::None, "first status reading is a baseline");
    expect(!device.device_lost(), "no fault yet");

    uint64_t fault_fence = 0;
    expect(device.submit(0x20100, 0, 0, &fault_fence) == NativeMaliError::None,
           "submit before the fault");
    uint64_t fault_handle = 0;
    expect(device.alloc(kSize, &fault_handle) == NativeMaliError::None, "alloc before the fault");
    expect(device.map(fault_handle, NativeMaliBoRead, &mapped) == NativeMaliError::None,
           "map before the fault");
    expect(device.free(fault_handle, fault_fence) == NativeMaliError::None,
           "guarded free before the fault");
    expect(iova.quarantine_count() == 1, "range quarantined before the fault");

    transport.status_faults = 1;
    expect(device.refresh_status() == NativeMaliError::DeviceLost, "a fault is reported");
    expect(device.device_lost(), "device is marked lost");
    expect(fences.state(fault_fence) == NativeMaliFenceState::Lost,
           "pending fence lost on fault");
    expect(iova.quarantine_count() == 0, "quarantine released on fault");
    expect(device.refresh_status() == NativeMaliError::DeviceLost,
           "the device stays lost until reset");

    expect(device.reset() == NativeMaliError::None, "reset after the fault");
    expect(!device.device_lost(), "reset clears the lost state");
    expect(device.refresh_status() == NativeMaliError::None, "the status is re-baselined");

    transport.status_resets = 1;
    expect(device.refresh_status() == NativeMaliError::DeviceLost,
           "a reset counter change is reported as device lost");

    if (failures != 0) {
        std::fprintf(stderr, "%d check(s) failed\n", failures);
        return 1;
    }
    std::printf("OK   device lifecycle: alloc/map/free rollback, wait, reset, quarantine\n");
    return 0;
}
