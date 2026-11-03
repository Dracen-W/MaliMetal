/* SPDX-License-Identifier: GPL-2.0-only
 *
 * Host smoke test for the PanVK dylib against the NativeMali fake service.
 *
 * The dylib is built with its NativeMali facade calls left as weak imports.
 * This executable supplies the facade (native_mali_kmod.cpp) and the scripted
 * service, then loads the dylib with dlopen. A physical device proves that the
 * weak import crossed the image boundary and that instance enumeration reached
 * the fake service; it does not touch a GPU.
 */
#include "native_mali_kmod.h"

#include "fake_native_mali_service.h"

#include <vulkan/vulkan.h>

#include <dlfcn.h>

#include <cstdio>
#include <cstdlib>

namespace {

FakeNativeMaliService *g_service;

int trampoline(void *context, uint32_t selector, const void *input, size_t input_bytes,
               void *output, size_t output_bytes) {
    return static_cast<FakeNativeMaliService *>(context)->call(
        selector, input, input_bytes, output, output_bytes);
}

} /* namespace */

extern "C" NativeMaliKmod *native_mali_kmod_create_default(NativeMaliResult *error) {
    NativeMaliTransportCallbacks callbacks{};
    callbacks.call = trampoline;
    callbacks.context = g_service;
    return native_mali_kmod_create(&callbacks, error);
}

int main(int argc, char **argv) {
    if (argc != 2) {
        std::fprintf(stderr, "usage: %s /path/to/libvulkan_panfrost.dylib\n", argv[0]);
        return 2;
    }

    setenv("MALIMETAL_NATIVE", "1", 1);

    FakeNativeMaliService service;
    g_service = &service;

    void *lib = dlopen(argv[1], RTLD_NOW | RTLD_LOCAL);
    if (lib == nullptr) {
        std::fprintf(stderr, "dlopen: %s\n", dlerror());
        return 1;
    }

    auto get_proc = reinterpret_cast<PFN_vkGetInstanceProcAddr>(
        dlsym(lib, "vk_icdGetInstanceProcAddr"));
    if (get_proc == nullptr) {
        std::fprintf(stderr, "dlsym vk_icdGetInstanceProcAddr: %s\n", dlerror());
        dlclose(lib);
        return 1;
    }

    auto create_instance = reinterpret_cast<PFN_vkCreateInstance>(
        get_proc(nullptr, "vkCreateInstance"));
    auto enumerate = reinterpret_cast<PFN_vkEnumeratePhysicalDevices>(
        get_proc(nullptr, "vkEnumeratePhysicalDevices"));
    auto destroy_instance = reinterpret_cast<PFN_vkDestroyInstance>(
        get_proc(nullptr, "vkDestroyInstance"));
    if (create_instance == nullptr || enumerate == nullptr || destroy_instance == nullptr) {
        std::fprintf(stderr, "one or more instance entry points are missing\n");
        dlclose(lib);
        return 1;
    }

    VkApplicationInfo app{VK_STRUCTURE_TYPE_APPLICATION_INFO};
    app.pApplicationName = "panvk-smoke";
    app.applicationVersion = 1;
    app.pEngineName = "MaliMetal";
    app.engineVersion = 1;
    app.apiVersion = VK_MAKE_API_VERSION(0, 1, 3, 0);

    VkInstanceCreateInfo info{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
    info.pApplicationInfo = &app;

    VkInstance instance = VK_NULL_HANDLE;
    VkResult result = create_instance(&info, nullptr, &instance);
    if (result != VK_SUCCESS) {
        std::fprintf(stderr, "vkCreateInstance: %d\n", result);
        dlclose(lib);
        return 1;
    }

    uint32_t count = 0;
    result = enumerate(instance, &count, nullptr);
    if (result != VK_SUCCESS || count == 0) {
        std::fprintf(stderr, "vkEnumeratePhysicalDevices: result %d, count %u\n", result, count);
        destroy_instance(instance, nullptr);
        dlclose(lib);
        return 1;
    }

    VkPhysicalDevice device = VK_NULL_HANDLE;
    result = enumerate(instance, &count, &device);
    if (result != VK_SUCCESS || device == VK_NULL_HANDLE) {
        std::fprintf(stderr, "vkEnumeratePhysicalDevices(device): result %d\n", result);
        destroy_instance(instance, nullptr);
        dlclose(lib);
        return 1;
    }

    VkPhysicalDeviceProperties props{};
    auto get_props = reinterpret_cast<PFN_vkGetPhysicalDeviceProperties>(
        get_proc(instance, "vkGetPhysicalDeviceProperties"));
    if (get_props != nullptr)
        get_props(device, &props);

    std::printf("OK   panvk smoke: facade crossed into dylib, %u physical device(s), %s\n",
                count, props.deviceName[0] ? props.deviceName : "(unnamed)");

    destroy_instance(instance, nullptr);
    dlclose(lib);
    return 0;
}
