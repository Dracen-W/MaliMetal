/* SPDX-License-Identifier: GPL-2.0-only
 *
 * Host smoke test for the PanVK dylib against the NativeMali fake service.
 *
 * The dylib is built with its NativeMali facade calls left as weak imports.
 * This executable loads a separate facade dylib (facade + scripted service)
 * with RTLD_GLOBAL, then loads PanVK. A physical device proves that the weak
 * import crossed the image boundary and that enumeration reached the fake
 * service; it does not touch a GPU.
 */
#include <vulkan/vulkan.h>

#include <dlfcn.h>

#include <cstdio>
#include <cstdlib>

int main(int argc, char **argv) {
    if (argc != 3) {
        std::fprintf(stderr, "usage: %s facade.dylib libvulkan_panfrost.dylib\n", argv[0]);
        return 2;
    }

    setenv("MALIMETAL_NATIVE", "1", 1);
    setenv("MESA_DEBUG", "1", 1);
    setenv("MESA_LOG_LEVEL", "debug", 1);
    setenv("PANVK_DEBUG", "1", 1);

    void *facade = dlopen(argv[1], RTLD_NOW | RTLD_GLOBAL);
    if (facade == nullptr) {
        std::fprintf(stderr, "dlopen facade: %s\n", dlerror());
        return 1;
    }

    void *lib = dlopen(argv[2], RTLD_NOW | RTLD_LOCAL);
    if (lib == nullptr) {
        std::fprintf(stderr, "dlopen: %s\n", dlerror());
        dlclose(facade);
        return 1;
    }

    auto get_proc = reinterpret_cast<PFN_vkGetInstanceProcAddr>(
        dlsym(lib, "vk_icdGetInstanceProcAddr"));
    if (get_proc == nullptr) {
        std::fprintf(stderr, "dlsym vk_icdGetInstanceProcAddr: %s\n", dlerror());
        dlclose(lib);
        dlclose(facade);
        return 1;
    }

    auto create_instance = reinterpret_cast<PFN_vkCreateInstance>(
        get_proc(nullptr, "vkCreateInstance"));
    if (create_instance == nullptr) {
        std::fprintf(stderr, "vkCreateInstance is missing\n");
        dlclose(lib);
        dlclose(facade);
        return 1;
    }

    VkApplicationInfo app{};
    app.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    app.pApplicationName = "panvk-smoke";
    app.applicationVersion = 1;
    app.pEngineName = "MaliMetal";
    app.engineVersion = 1;
    app.apiVersion = VK_API_VERSION_1_0;

    VkInstanceCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    info.pApplicationInfo = &app;

    VkInstance instance = VK_NULL_HANDLE;
    VkResult result = create_instance(&info, nullptr, &instance);
    if (result != VK_SUCCESS) {
        std::fprintf(stderr, "vkCreateInstance: %d\n", result);
        dlclose(lib);
        dlclose(facade);
        return 1;
    }

    auto enumerate = reinterpret_cast<PFN_vkEnumeratePhysicalDevices>(
        get_proc(instance, "vkEnumeratePhysicalDevices"));
    auto destroy_instance = reinterpret_cast<PFN_vkDestroyInstance>(
        get_proc(instance, "vkDestroyInstance"));
    if (enumerate == nullptr || destroy_instance == nullptr) {
        std::fprintf(stderr, "instance entry points are missing (enumerate=%p, destroy=%p)\n",
                     reinterpret_cast<void *>(enumerate),
                     reinterpret_cast<void *>(destroy_instance));
        if (destroy_instance != nullptr)
            destroy_instance(instance, nullptr);
        dlclose(lib);
        dlclose(facade);
        return 1;
    }

    uint32_t count = 0;
    result = enumerate(instance, &count, nullptr);
    if (result != VK_SUCCESS || count == 0) {
        std::fprintf(stderr, "vkEnumeratePhysicalDevices: result %d, count %u\n", result, count);
        destroy_instance(instance, nullptr);
        dlclose(lib);
        dlclose(facade);
        return 1;
    }

    VkPhysicalDevice device = VK_NULL_HANDLE;
    result = enumerate(instance, &count, &device);
    if (result != VK_SUCCESS || device == VK_NULL_HANDLE) {
        std::fprintf(stderr, "vkEnumeratePhysicalDevices(device): result %d\n", result);
        destroy_instance(instance, nullptr);
        dlclose(lib);
        dlclose(facade);
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
    dlclose(facade);
    return 0;
}
