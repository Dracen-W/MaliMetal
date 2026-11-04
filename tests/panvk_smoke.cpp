/* SPDX-License-Identifier: GPL-2.0-only
 *
 * Host smoke test for the PanVK dylib against the NativeMali fake service.
 *
 * The dylib is built with its NativeMali facade calls left as weak imports.
 * This executable loads a separate facade dylib (facade + scripted service)
 * with RTLD_GLOBAL, then loads PanVK. A physical device proves that the weak
 * import crossed the image boundary and that enumeration reached the fake
 * service; it does not touch a GPU.
 *
 * Why the debug messenger: Mesa's vk_errorf() is compiled out of non-debug
 * builds unless an application messenger is registered, so a failing
 * panvk_physical_device_init() ("Unknown gpu_id", "None of the cores ...")
 * is invisible and vkEnumeratePhysicalDevices() just reports zero devices.
 * An instance-create messenger is not enough: those only live during
 * vkCreateInstance/vkDestroyInstance, so a persistent one is created after.
 */
#include <vulkan/vulkan.h>

#include <dlfcn.h>

#include <cstdio>
#include <cstdlib>
#include <ctime>

#include <unistd.h>

namespace {

VKAPI_ATTR VkBool32 VKAPI_CALL
on_message(VkDebugUtilsMessageSeverityFlagBitsEXT severity,
           VkDebugUtilsMessageTypeFlagsEXT /* types */,
           const VkDebugUtilsMessengerCallbackDataEXT *data, void * /* user */) {
    const char *level = (severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT)     ? "error"
                        : (severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT) ? "warning"
                                                                                       : "info";
    std::fprintf(stderr, "panvk %s: %s\n", level,
                 data != nullptr && data->pMessage != nullptr ? data->pMessage : "(no message)");
    return VK_FALSE;
}

struct Session {
    void *facade = nullptr;
    void *lib = nullptr;
    PFN_vkGetInstanceProcAddr get_proc = nullptr;
    VkInstance instance = VK_NULL_HANDLE;
    VkDebugUtilsMessengerEXT messenger = VK_NULL_HANDLE;

    /* Tears down whatever exists, in reverse order of creation. */
    void close() {
        if (messenger != VK_NULL_HANDLE) {
            auto destroy_messenger = reinterpret_cast<PFN_vkDestroyDebugUtilsMessengerEXT>(
                get_proc(instance, "vkDestroyDebugUtilsMessengerEXT"));
            if (destroy_messenger != nullptr)
                destroy_messenger(instance, messenger, nullptr);
            messenger = VK_NULL_HANDLE;
        }
        if (instance != VK_NULL_HANDLE) {
            auto destroy_instance = reinterpret_cast<PFN_vkDestroyInstance>(
                get_proc(instance, "vkDestroyInstance"));
            if (destroy_instance != nullptr)
                destroy_instance(instance, nullptr);
            instance = VK_NULL_HANDLE;
        }
        if (lib != nullptr)
            dlclose(lib);
        if (facade != nullptr)
            dlclose(facade);
        lib = facade = nullptr;
    }
};

} /* namespace */

int main(int argc, char **argv) {
    if (argc != 3) {
        std::fprintf(stderr, "usage: %s facade.dylib libvulkan_panfrost.dylib\n", argv[0]);
        return 2;
    }

    setenv("MALIMETAL_NATIVE", "1", 1);
    setenv("PAN_I_WANT_A_BROKEN_VULKAN_DRIVER", "1", 1);
    setenv("PANVK_DEBUG", "startup", 1);

    Session s;

    s.facade = dlopen(argv[1], RTLD_NOW | RTLD_GLOBAL);
    if (s.facade == nullptr) {
        std::fprintf(stderr, "dlopen facade: %s\n", dlerror());
        return 1;
    }

    s.lib = dlopen(argv[2], RTLD_NOW | RTLD_LOCAL);
    if (s.lib == nullptr) {
        std::fprintf(stderr, "dlopen: %s\n", dlerror());
        s.close();
        return 1;
    }

    s.get_proc = reinterpret_cast<PFN_vkGetInstanceProcAddr>(
        dlsym(s.lib, "vk_icdGetInstanceProcAddr"));
    if (s.get_proc == nullptr) {
        std::fprintf(stderr, "dlsym vk_icdGetInstanceProcAddr: %s\n", dlerror());
        s.close();
        return 1;
    }

    auto create_instance = reinterpret_cast<PFN_vkCreateInstance>(
        s.get_proc(nullptr, "vkCreateInstance"));
    if (create_instance == nullptr) {
        std::fprintf(stderr, "vkCreateInstance is missing\n");
        s.close();
        return 1;
    }

    VkApplicationInfo app{};
    app.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    app.pApplicationName = "panvk-smoke";
    app.applicationVersion = 1;
    app.pEngineName = "MaliMetal";
    app.engineVersion = 1;
    app.apiVersion = VK_API_VERSION_1_0;

    const char *extensions[] = {VK_EXT_DEBUG_UTILS_EXTENSION_NAME};
    VkInstanceCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    info.pApplicationInfo = &app;
    info.enabledExtensionCount = 1;
    info.ppEnabledExtensionNames = extensions;

    VkResult result = create_instance(&info, nullptr, &s.instance);
    if (result != VK_SUCCESS) {
        std::fprintf(stderr, "vkCreateInstance: %d\n", result);
        s.instance = VK_NULL_HANDLE;
        s.close();
        return 1;
    }

    auto create_messenger = reinterpret_cast<PFN_vkCreateDebugUtilsMessengerEXT>(
        s.get_proc(s.instance, "vkCreateDebugUtilsMessengerEXT"));
    if (create_messenger == nullptr) {
        std::fprintf(stderr, "vkCreateDebugUtilsMessengerEXT is missing; "
                             "PanVK errors will not be visible\n");
    } else {
        VkDebugUtilsMessengerCreateInfoEXT messenger_info{};
        messenger_info.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
        messenger_info.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT |
                                         VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT |
                                         VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
                                         VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
        messenger_info.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
                                     VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
                                     VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
        messenger_info.pfnUserCallback = on_message;
        result = create_messenger(s.instance, &messenger_info, nullptr, &s.messenger);
        if (result != VK_SUCCESS) {
            std::fprintf(stderr, "vkCreateDebugUtilsMessengerEXT: %d\n", result);
            s.messenger = VK_NULL_HANDLE;
        }
    }

    auto enumerate = reinterpret_cast<PFN_vkEnumeratePhysicalDevices>(
        s.get_proc(s.instance, "vkEnumeratePhysicalDevices"));
    if (enumerate == nullptr) {
        std::fprintf(stderr, "vkEnumeratePhysicalDevices is missing\n");
        s.close();
        return 1;
    }

    uint32_t count = 0;
    unsigned wait_seconds = 0;
    if (const char *wait = std::getenv("PANVK_SMOKE_WAIT_SECONDS"))
        wait_seconds = static_cast<unsigned>(std::strtoul(wait, nullptr, 10));
    const std::time_t deadline = std::time(nullptr) + wait_seconds;

    /* The service is published a few seconds into boot on the phone, after the
     * GPU is powered. The host canary leaves this at zero. */
    for (;;) {
        count = 0;
        result = enumerate(s.instance, &count, nullptr);
        if ((result == VK_SUCCESS && count > 0) || std::time(nullptr) >= deadline)
            break;
        std::fprintf(stderr, "panvk smoke: no physical device yet, retrying\n");
        sleep(1);
    }
    if (result != VK_SUCCESS || count == 0) {
        std::fprintf(stderr,
                     "vkEnumeratePhysicalDevices: result %d, count %u\n"
                     "  result 0 with count 0 means native enumeration returned "
                     "VK_ERROR_INCOMPATIBLE_DRIVER and the runtime fell through to the "
                     "(empty) DRM path; the reason is in the 'panvk error:' line above.\n",
                     result, count);
        s.close();
        return 1;
    }

    VkPhysicalDevice device = VK_NULL_HANDLE;
    result = enumerate(s.instance, &count, &device);
    if (result != VK_SUCCESS || device == VK_NULL_HANDLE) {
        std::fprintf(stderr, "vkEnumeratePhysicalDevices(device): result %d\n", result);
        s.close();
        return 1;
    }

    VkPhysicalDeviceProperties props{};
    auto get_props = reinterpret_cast<PFN_vkGetPhysicalDeviceProperties>(
        s.get_proc(s.instance, "vkGetPhysicalDeviceProperties"));
    if (get_props != nullptr)
        get_props(device, &props);

    std::printf("OK   panvk smoke: facade crossed into dylib, %u physical device(s), %s\n",
                count, props.deviceName[0] ? props.deviceName : "(unnamed)");

    s.close();
    return 0;
}
