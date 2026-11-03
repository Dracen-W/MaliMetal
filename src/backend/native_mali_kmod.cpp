/* SPDX-License-Identifier: GPL-2.0-only
 *
 * C facade over the userspace NativeMali stack. See native_mali_kmod.h.
 *
 * The facade adds no policy of its own: every call forwards to
 * NativeMaliDevice, which is host-tested, and every failure is the device's
 * failure translated into the C enum. What it does add is the two things a C
 * caller cannot do itself: it turns a function-pointer transport into the C++
 * interface the client expects, and it owns the allocator, the buffer table and
 * the fence tracker for the lifetime of the device.
 */
#include "native_mali_kmod.h"

#include "NativeMaliAbiTable.h"
#include "NativeMaliClient.h"
#include "NativeMaliDevice.h"

#include <cstdlib>
#include <cstring>
#include <new>

#if defined(__APPLE__)
#include <IOKit/IOKitLib.h>
#include <mach/mach.h>
#endif

namespace {

/* The C enum mirrors the C++ one exactly; if either is edited alone this fails
 * instead of silently mapping "busy" onto "device lost". */
static_assert(static_cast<int>(NATIVE_MALI_RESULT_OK) == static_cast<int>(NativeMaliError::None), "result/error alignment");
static_assert(static_cast<int>(NATIVE_MALI_RESULT_INVALID_ARGUMENT) == static_cast<int>(NativeMaliError::InvalidArgument), "result/error alignment");
static_assert(static_cast<int>(NATIVE_MALI_RESULT_OUT_OF_MEMORY) == static_cast<int>(NativeMaliError::OutOfMemory), "result/error alignment");
static_assert(static_cast<int>(NATIVE_MALI_RESULT_BUSY) == static_cast<int>(NativeMaliError::Busy), "result/error alignment");
static_assert(static_cast<int>(NATIVE_MALI_RESULT_NOT_READY) == static_cast<int>(NativeMaliError::NotReady), "result/error alignment");
static_assert(static_cast<int>(NATIVE_MALI_RESULT_UNSUPPORTED) == static_cast<int>(NativeMaliError::Unsupported), "result/error alignment");
static_assert(static_cast<int>(NATIVE_MALI_RESULT_ABORTED) == static_cast<int>(NativeMaliError::Aborted), "result/error alignment");
static_assert(static_cast<int>(NATIVE_MALI_RESULT_DEVICE_LOST) == static_cast<int>(NativeMaliError::DeviceLost), "result/error alignment");
static_assert(static_cast<int>(NATIVE_MALI_RESULT_IO_ERROR) == static_cast<int>(NativeMaliError::IoError), "result/error alignment");
static_assert(static_cast<int>(NATIVE_MALI_RESULT_UNKNOWN) == static_cast<int>(NativeMaliError::Unknown), "result/error alignment");

constexpr uint64_t kPageBytes = 4096;

NativeMaliResult to_result(NativeMaliError error) {
    switch (error) {
        case NativeMaliError::None: return NATIVE_MALI_RESULT_OK;
        case NativeMaliError::InvalidArgument: return NATIVE_MALI_RESULT_INVALID_ARGUMENT;
        case NativeMaliError::OutOfMemory: return NATIVE_MALI_RESULT_OUT_OF_MEMORY;
        case NativeMaliError::Busy: return NATIVE_MALI_RESULT_BUSY;
        case NativeMaliError::NotReady: return NATIVE_MALI_RESULT_NOT_READY;
        case NativeMaliError::Unsupported: return NATIVE_MALI_RESULT_UNSUPPORTED;
        case NativeMaliError::Aborted: return NATIVE_MALI_RESULT_ABORTED;
        case NativeMaliError::DeviceLost: return NATIVE_MALI_RESULT_DEVICE_LOST;
        case NativeMaliError::IoError: return NATIVE_MALI_RESULT_IO_ERROR;
        case NativeMaliError::Unknown: return NATIVE_MALI_RESULT_UNKNOWN;
    }
    return NATIVE_MALI_RESULT_UNKNOWN;
}

class CallbackTransport final : public NativeMaliTransport {
public:
    explicit CallbackTransport(const NativeMaliTransportCallbacks &callbacks)
        : callbacks_(callbacks) {}

    int call(uint32_t selector, const void *input, size_t input_bytes, void *output,
             size_t output_bytes) override {
        return callbacks_.call(callbacks_.context, selector, input, input_bytes, output,
                               output_bytes);
    }

private:
    NativeMaliTransportCallbacks callbacks_;
};

#if defined(__APPLE__) && !defined(MALIMETAL_TEST_NO_DEFAULT)
int IokitTransportCall(void *context, uint32_t selector, const void *input,
                       size_t input_bytes, void *output, size_t output_bytes) {
    io_connect_t connection = static_cast<io_connect_t>(reinterpret_cast<uintptr_t>(context));
    size_t size = output_bytes;
    return IOConnectCallStructMethod(connection, selector, input, input_bytes,
                                     output, &size);
}
#endif

} /* namespace */

struct NativeMaliKmod {
    NativeMaliKmod(const NativeMaliTransportCallbacks &callbacks, uint64_t va_base,
                   uint64_t va_size, uint32_t connection = 0)
        : transport(callbacks),
          client(transport),
          iova(va_base, va_size),
          device(client, buffers, iova, fences),
          connection(connection) {}

    ~NativeMaliKmod() {
#if defined(__APPLE__)
        if (connection != 0)
            IOServiceClose(static_cast<io_connect_t>(connection));
#endif
    }

    CallbackTransport transport;
    NativeMaliClient client;
    NativeMaliBoTable buffers;
    NativeMaliIovaAllocator iova;
    NativeMaliFenceTracker fences;
    NativeMaliDevice device;
    NativeMaliDeviceProps props{};
    uint32_t connection = 0;
};

const char *native_mali_kmod_error_name(NativeMaliResult result) {
    return NativeMaliErrorName(static_cast<NativeMaliError>(result));
}

NativeMaliKmod *native_mali_kmod_create(const NativeMaliTransportCallbacks *transport,
                                        NativeMaliResult *error) {
    NativeMaliResult ignored = NATIVE_MALI_RESULT_OK;
    if (error == nullptr) {
        error = &ignored;
    }
    *error = NATIVE_MALI_RESULT_INVALID_ARGUMENT;
    if (transport == nullptr || transport->call == nullptr) {
        return nullptr;
    }

    NativeMaliKmod *kmod = nullptr;
    try {
        kmod = new (std::nothrow) NativeMaliKmod(*transport, kPageBytes, kPageBytes);
    } catch (...) {
        kmod = nullptr;
    }
    if (kmod == nullptr) {
        *error = NATIVE_MALI_RESULT_OUT_OF_MEMORY;
        return nullptr;
    }

    /* Query and GetDeviceInfo before the allocator is usable: the address range
     * comes from the device, not from a constant. */
    NativeMaliAbiQuery query{};
    NativeMaliError client_error = kmod->client.query(&query);
    if (client_error != NativeMaliError::None) {
        delete kmod;
        *error = to_result(client_error);
        return nullptr;
    }
    NativeMaliAbiDeviceInfo info{};
    client_error = kmod->client.device_info(&info);
    if (client_error != NativeMaliError::None) {
        delete kmod;
        *error = to_result(client_error);
        return nullptr;
    }

    /* The ABI refuses IOVA 0, so the allocator starts at the first page-aligned
     * nonzero address inside the reported range. */
    uint64_t base = info.va_start;
    if (base % kPageBytes != 0) {
        base = (base + kPageBytes - 1) & ~(kPageBytes - 1);
    }
    if (base == 0) {
        base = kPageBytes;
    }
    if (info.va_size <= kPageBytes || base - info.va_start >= info.va_size) {
        delete kmod;
        *error = NATIVE_MALI_RESULT_UNSUPPORTED;
        return nullptr;
    }
    const uint64_t size = info.va_size - (base - info.va_start);

    /* Rebuild the object with the real range. The first one only held the
     * transport and client, so nothing leaks. */
    NativeMaliKmod *sized = nullptr;
    try {
        sized = new (std::nothrow) NativeMaliKmod(*transport, base, size);
    } catch (...) {
        sized = nullptr;
    }
    delete kmod;
    if (sized == nullptr) {
        *error = NATIVE_MALI_RESULT_OUT_OF_MEMORY;
        return nullptr;
    }
    if (!sized->iova.valid()) {
        delete sized;
        *error = NATIVE_MALI_RESULT_UNSUPPORTED;
        return nullptr;
    }

    NativeMaliDeviceProps &props = sized->props;
    props.gpu_id = info.gpu_id;
    props.shader_present_lo = info.shader_present_lo;
    props.shader_present_hi = info.shader_present_hi;
    props.tiler_present_lo = info.tiler_present_lo;
    props.tiler_present_hi = info.tiler_present_hi;
    props.thread_max_threads = info.thread_max_threads;
    props.max_workgroup_size = info.max_workgroup_size;
    props.max_barrier_size = info.max_barrier_size;
    props.l2_features = info.l2_features;
    props.core_features = info.core_features;
    props.tiler_features = info.tiler_features;
    props.non_coherent = (info.flags & NativeMaliDeviceNonCoherent) != 0 ? 1u : 0u;
    props.va_start = info.va_start;
    props.va_size = info.va_size;
    props.max_bo_bytes = query.max_bo_bytes;
    props.mem_features = info.mem_features;
    props.mmu_features = info.mmu_features;
    props.afbc_features = info.afbc_features;
    for (uint32_t index = 0; index < 4; ++index)
        props.texture_features[index] = info.texture_features[index];
    props.max_tasks_per_core = info.max_tasks_per_core;
    props.registers_per_core = info.registers_per_core;
    props.tls_instances_per_core = info.tls_instances_per_core;
    props.pgsize_shift = info.pgsize_shift;
    props.vm_op_flags = info.vm_op_flags;

    *error = NATIVE_MALI_RESULT_OK;
    return sized;
}

#if defined(__APPLE__) && !defined(MALIMETAL_TEST_NO_DEFAULT)
NativeMaliKmod *native_mali_kmod_create_default(NativeMaliResult *error) {
    NativeMaliResult ignored = NATIVE_MALI_RESULT_OK;
    if (error == nullptr)
        error = &ignored;
    *error = NATIVE_MALI_RESULT_UNSUPPORTED;

    io_service_t service = IOServiceGetMatchingService(
        kIOMainPortDefault, IOServiceMatching("NativeMaliGpuService"));
    if (service == IO_OBJECT_NULL)
        return nullptr;

    io_connect_t connection = IO_OBJECT_NULL;
    const kern_return_t opened = IOServiceOpen(
        service, mach_task_self(), 0, &connection);
    IOObjectRelease(service);
    if (opened != KERN_SUCCESS || connection == IO_OBJECT_NULL) {
        if (connection != IO_OBJECT_NULL)
            IOServiceClose(connection);
        return nullptr;
    }

    const NativeMaliTransportCallbacks callbacks = {
        .call = IokitTransportCall,
        .context = reinterpret_cast<void *>(static_cast<uintptr_t>(connection)),
    };
    NativeMaliKmod *kmod = native_mali_kmod_create(&callbacks, error);
    if (kmod == nullptr) {
        IOServiceClose(connection);
        return nullptr;
    }
    kmod->connection = static_cast<uint32_t>(connection);
    return kmod;
}
#elif !defined(MALIMETAL_TEST_NO_DEFAULT)
NativeMaliKmod *native_mali_kmod_create_default(NativeMaliResult *error) {
    if (error != nullptr)
        *error = NATIVE_MALI_RESULT_UNSUPPORTED;
    return nullptr;
}
#endif

void native_mali_kmod_destroy(NativeMaliKmod *kmod) { delete kmod; }

const NativeMaliDeviceProps *native_mali_kmod_props(const NativeMaliKmod *kmod) {
    return kmod == nullptr ? nullptr : &kmod->props;
}

NativeMaliResult native_mali_kmod_alloc(NativeMaliKmod *kmod, uint64_t bytes,
                                        uint64_t *handle) {
    if (kmod == nullptr) {
        return NATIVE_MALI_RESULT_INVALID_ARGUMENT;
    }
    return to_result(kmod->device.alloc(bytes, handle));
}

NativeMaliResult native_mali_kmod_map(NativeMaliKmod *kmod, uint64_t handle,
                                      uint32_t permissions, uint64_t *iova) {
    if (kmod == nullptr) {
        return NATIVE_MALI_RESULT_INVALID_ARGUMENT;
    }
    return to_result(kmod->device.map(handle, permissions, iova));
}

NativeMaliResult native_mali_kmod_free(NativeMaliKmod *kmod, uint64_t handle,
                                       uint64_t guard_fence) {
    if (kmod == nullptr) {
        return NATIVE_MALI_RESULT_INVALID_ARGUMENT;
    }
    return to_result(kmod->device.free(handle, guard_fence));
}

NativeMaliResult native_mali_kmod_cpu_map(NativeMaliKmod *kmod, uint64_t handle,
                                          uint32_t *token, uint64_t *bytes) {
    if (kmod == nullptr) {
        return NATIVE_MALI_RESULT_INVALID_ARGUMENT;
    }
    return to_result(kmod->device.cpu_map(handle, token, bytes));
}

NativeMaliResult native_mali_kmod_cpu_unmap(NativeMaliKmod *kmod, uint64_t handle,
                                            uint32_t token) {
    if (kmod == nullptr) {
        return NATIVE_MALI_RESULT_INVALID_ARGUMENT;
    }
    return to_result(kmod->device.cpu_unmap(handle, token));
}

NativeMaliResult native_mali_kmod_cpu_map_address(NativeMaliKmod *kmod, uint64_t handle,
                                                  uint32_t *token, void **address,
                                                  uint64_t *bytes) {
    if (kmod == nullptr || token == nullptr || address == nullptr || bytes == nullptr)
        return NATIVE_MALI_RESULT_INVALID_ARGUMENT;
#if defined(__APPLE__)
    if (kmod->connection == 0) {
#if defined(MALIMETAL_TESTING)
        uint32_t local_token = 0;
        uint64_t local_bytes = 0;
        const NativeMaliResult result = to_result(
            kmod->device.cpu_map(handle, &local_token, &local_bytes));
        if (result != NATIVE_MALI_RESULT_OK)
            return result;
        if (local_bytes > SIZE_MAX)
            return NATIVE_MALI_RESULT_OUT_OF_MEMORY;
        void *mapped = nullptr;
        if (posix_memalign(&mapped, kPageBytes, static_cast<size_t>(local_bytes)) != 0)
            return NATIVE_MALI_RESULT_OUT_OF_MEMORY;
        std::memset(mapped, 0, static_cast<size_t>(local_bytes));
        *token = local_token;
        *address = mapped;
        *bytes = local_bytes;
        return NATIVE_MALI_RESULT_OK;
#else
        return NATIVE_MALI_RESULT_UNSUPPORTED;
#endif
    }
    uint32_t local_token = 0;
    uint64_t local_bytes = 0;
    const NativeMaliResult result = to_result(
        kmod->device.cpu_map(handle, &local_token, &local_bytes));
    if (result != NATIVE_MALI_RESULT_OK)
        return result;

    mach_vm_address_t mapped = 0;
    mach_vm_size_t mapped_bytes = 0;
    const kern_return_t kr = IOConnectMapMemory64(
        static_cast<io_connect_t>(kmod->connection), local_token, mach_task_self(),
        &mapped, &mapped_bytes, kIOMapAnywhere | kIOMapDefaultCache);
    if (kr != KERN_SUCCESS) {
        (void)kmod->device.cpu_unmap(handle, local_token);
        return NATIVE_MALI_RESULT_IO_ERROR;
    }
    *token = local_token;
    *address = reinterpret_cast<void *>(static_cast<uintptr_t>(mapped));
    *bytes = static_cast<uint64_t>(mapped_bytes);
    return NATIVE_MALI_RESULT_OK;
#else
    (void)handle; (void)token; (void)address; (void)bytes;
    return NATIVE_MALI_RESULT_UNSUPPORTED;
#endif
}

NativeMaliResult native_mali_kmod_cpu_address_unmap(NativeMaliKmod *kmod, uint32_t token,
                                                    void *address) {
    if (kmod == nullptr || token == 0 || address == nullptr)
        return NATIVE_MALI_RESULT_INVALID_ARGUMENT;
#if defined(__APPLE__)
    if (kmod->connection == 0) {
#if defined(MALIMETAL_TESTING)
        std::free(address);
        return NATIVE_MALI_RESULT_OK;
#else
        return NATIVE_MALI_RESULT_UNSUPPORTED;
#endif
    }
    const kern_return_t kr = IOConnectUnmapMemory64(
        static_cast<io_connect_t>(kmod->connection), token, mach_task_self(),
        static_cast<mach_vm_address_t>(reinterpret_cast<uintptr_t>(address)));
    return kr == KERN_SUCCESS ? NATIVE_MALI_RESULT_OK : NATIVE_MALI_RESULT_IO_ERROR;
#else
    (void)token; (void)address;
    return NATIVE_MALI_RESULT_UNSUPPORTED;
#endif
}

NativeMaliResult native_mali_kmod_submit(NativeMaliKmod *kmod, uint64_t chain,
                                         uint64_t wait_fence, uint64_t wait_timeout_ns,
                                         uint64_t *fence) {
    if (kmod == nullptr) {
        return NATIVE_MALI_RESULT_INVALID_ARGUMENT;
    }
    return to_result(kmod->device.submit(chain, wait_fence, wait_timeout_ns, fence));
}

NativeMaliResult native_mali_kmod_wait(NativeMaliKmod *kmod, uint64_t fence,
                                       uint64_t timeout_ns) {
    if (kmod == nullptr) {
        return NATIVE_MALI_RESULT_INVALID_ARGUMENT;
    }
    return to_result(kmod->device.wait(fence, timeout_ns));
}

NativeMaliResult native_mali_kmod_refresh_status(NativeMaliKmod *kmod) {
    if (kmod == nullptr) {
        return NATIVE_MALI_RESULT_INVALID_ARGUMENT;
    }
    return to_result(kmod->device.refresh_status());
}

NativeMaliResult native_mali_kmod_reset(NativeMaliKmod *kmod) {
    if (kmod == nullptr) {
        return NATIVE_MALI_RESULT_INVALID_ARGUMENT;
    }
    return to_result(kmod->device.reset());
}
