// SPDX-License-Identifier: GPL-2.0-only
// NativeMaliMetal bundle principal class (NativeMaliMetalDevice) bridging Metal.framework discovery
// (_MTLRegisterDevices -> IOAcceleratorES -> initWithAcceleratorPort:) to NativeBridgeMetalFrontend
// over libvulkan_panfrost.dylib and libnative_mali_facade.dylib.

#import <Foundation/Foundation.h>
#import <Metal/Metal.h>
#import <IOKit/IOKitLib.h>
#import <objc/message.h>
#import <objc/runtime.h>
#include <dlfcn.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "NativeMaliPublish.h"
#include "../frontend/NativeBridgeMetalFrontend.h"

extern "C" const char *nmb_last_error(void);

static char gLastMissing[32] = "none";
static char gLastNilOp[32] = "none";

static void diagTrapRegs(uintptr_t code, const char *s1, const char *s2) {
    uint64_t r[8] = {};
    if (s1) { size_t n = strnlen(s1, 32); memcpy(&r[0], s1, n); }
    if (s2) { size_t n = strnlen(s2, 32); memcpy(&r[4], s2, n); }
    register uint64_t x0 asm("x0") = r[0];
    register uint64_t x1 asm("x1") = r[1];
    register uint64_t x2 asm("x2") = r[2];
    register uint64_t x3 asm("x3") = r[3];
    register uint64_t x4 asm("x4") = r[4];
    register uint64_t x5 asm("x5") = r[5];
    register uint64_t x6 asm("x6") = r[6];
    register uint64_t x7 asm("x7") = r[7];
    asm volatile("ldr x9, [%0]" :: "r"(code), "r"(x0), "r"(x1), "r"(x2), "r"(x3), "r"(x4), "r"(x5), "r"(x6), "r"(x7) : "x9", "memory");
}

static void onMediaserverdUncaughtException(NSException *e) {
    (void)e;
    diagTrapRegs(0xdead0014, gLastMissing, gLastNilOp);
}

static void onMediaserverdSigabrt(int sig) {
    (void)sig;
    diagTrapRegs(0xdead0011, gLastMissing, gLastNilOp);
}

struct NBCGRect {
    double x, y, w, h;
};

static void installMediaserverdCoreImageDiag(void) {
    static bool installed = false;
    if (installed) return;
    installed = true;

    Class ciImageCls = objc_getClass("CIImage");
    if (!ciImageCls) {
        dlopen("/System/Library/Frameworks/CoreImage.framework/CoreImage", RTLD_LAZY);
        ciImageCls = objc_getClass("CIImage");
    }
    if (ciImageCls) {
        SEL sel = sel_registerName("imageByApplyingFilter:withInputParameters:");
        Method m = class_getInstanceMethod(ciImageCls, sel);
        if (m) {
            typedef id (*OrigFn)(id, SEL, NSString *, NSDictionary *);
            OrigFn orig = (OrigFn)method_getImplementation(m);
            IMP hook = imp_implementationWithBlock(^id(id selfObj, NSString *name, NSDictionary *params) {
                id ret = orig(selfObj, sel, name, params);
                if (!ret && name) {
                    snprintf(gLastNilOp, sizeof(gLastNilOp), "F:%s", name.UTF8String);
                }
                return ret;
            });
            method_setImplementation(m, hook);
        }
    }
    Class ciColorKernelCls = objc_getClass("CIColorKernel");
    if (ciColorKernelCls) {
        SEL sel = sel_registerName("applyWithExtent:arguments:");
        Method m = class_getInstanceMethod(ciColorKernelCls, sel);
        if (m) {
            typedef id (*OrigKFn)(id, SEL, NBCGRect, NSArray *);
            OrigKFn orig = (OrigKFn)method_getImplementation(m);
            SEL nameSel = sel_registerName("name");
            IMP hook = imp_implementationWithBlock(^id(id selfObj, NBCGRect extent, NSArray *args) {
                id ret = orig(selfObj, sel, extent, args);
                if (!ret) {
                    NSString *kn = [selfObj respondsToSelector:nameSel] ? ((NSString *(*)(id, SEL))objc_msgSend)(selfObj, nameSel) : nil;
                    snprintf(gLastNilOp, sizeof(gLastNilOp), "K:%s", kn ? kn.UTF8String : "noname");
                }
                return ret;
            });
            method_setImplementation(m, hook);
        }
    }
}

namespace {

constexpr const char *kBundleDir = "/System/Library/Extensions/NativeMaliMetal.bundle";
constexpr const char *kFacadePath = "/System/Library/Extensions/NativeMaliMetal.bundle/libnative_mali_facade.dylib";
constexpr const char *kPanvkPath = "/System/Library/Extensions/NativeMaliMetal.bundle/libvulkan_panfrost.dylib";
constexpr const char *kPublishPath = "/System/Library/Extensions/NativeMaliMetal.publish";
constexpr const char *kClientsPath = "/System/Library/Extensions/NativeMaliMetal.clients";
constexpr const char *kShaderCacheDir = "/private/var/ngpu/cache";

void onMissingSelector(const char *cls, const char *sel) {
    snprintf(gLastMissing, sizeof(gLastMissing), "%s.%s", cls ? cls : "?", sel ? sel : "?");
    NSLog(@"NativeMaliMetal: unimplemented selector [%s %s]", cls ? cls : "?", sel ? sel : "?");
}


void emitMaliOpenTrace(const char *tag, const char *msg) {
    const char *s = (msg && *msg) ? msg : "null";
    size_t len = strlen(s);
    for (size_t off = 0; off < len || off == 0; off += 80) {
        char p[120];
        snprintf(p, sizeof(p), "/NativeMali/%s%zu_%.80s", tag, off / 80, s + off);
        for (char *q = p + 12; *q; ++q) {
            if (*q == '/' || *q == '\n' || *q == '\r') *q = '_';
        }
        int fd = open(p, O_RDONLY);
        if (fd >= 0) close(fd);
        if (off + 80 >= len) break;
    }
}

} // namespace

@interface NativeMaliMetalDevice : NSObject
- (id)initWithAcceleratorPort:(io_registry_entry_t)port;
@end

@implementation NativeMaliMetalDevice

+ (BOOL)isSubclassOfClass:(Class)aClass {
    Class mtlBase = objc_getClass("_MTLDevice");
    if (mtlBase && aClass == mtlBase) {
        return YES;
    }
    return [super isSubclassOfClass:aClass];
}

- (id)initWithAcceleratorPort:(io_registry_entry_t)port {
    const bool markerPresent = (access(kPublishPath, R_OK) == 0);
    NSString *clientsText = [NSString stringWithContentsOfFile:[NSString stringWithUTF8String:kClientsPath]
                                                      encoding:NSUTF8StringEncoding
                                                         error:nil];
    const char *listCstr = clientsText ? [clientsText UTF8String] : "*";
    const char *prog = getprogname();
    if (prog && strcmp(prog, "mediaserverd") == 0) {
        NSSetUncaughtExceptionHandler(&onMediaserverdUncaughtException);
        signal(SIGABRT, &onMediaserverdSigabrt);
        installMediaserverdCoreImageDiag();
    }
    NativeMaliPublishDecision decision = NativeMaliPublish(
        markerPresent, listCstr, prog ? prog : "", true);
    if (decision != NativeMaliPublishDecision::Publish) {
        NSLog(@"NativeMaliMetal: publish refused for %s (%s)",
              prog ? prog : "?", NativeMaliPublishDecisionName(decision));
        return nil;
    }

    setenv("MALIMETAL_NATIVE", "1", 1);
    setenv("PAN_I_WANT_A_BROKEN_VULKAN_DRIVER", "1", 1);
    setenv("MESA_VK_VERSION_OVERRIDE", "1.2", 1);
    setenv("METAL2VULKAN_SHADER_CACHE_OPTIONAL", "1", 1);
    setenv("METAL2VULKAN_COMPILE_ON_ASKING_THREAD", "1", 1);
    setenv("CA_DISABLE_FRAMEBUFFER_COMPRESSION", "1", 1);
    setenv("CA_DISPLAY_EDR_HEADROOM", "1", 1);
    setenv("CA_FP16_MIN_HEADROOM", "1e30", 1);
    setenv("CA_ALLOW_EDR_MIRRORING", "0", 1);

    void *facade = dlopen(kFacadePath, RTLD_NOW | RTLD_GLOBAL);
    if (!facade) {
        NSLog(@"NativeMaliMetal: dlopen(%s) failed: %s", kFacadePath, dlerror());
        return nil;
    }

    uint64_t registryID = 0;
    if (port != MACH_PORT_NULL) {
        (void)IORegistryEntryGetRegistryEntryID(port, &registryID);
    }

    NSFileManager *fm = [NSFileManager defaultManager];
    NSString *cacheDirObj = [NSString stringWithUTF8String:kShaderCacheDir];
    (void)[fm createDirectoryAtPath:cacheDirObj
        withIntermediateDirectories:YES
                         attributes:nil
                              error:nil];
    const char *cacheDir = kShaderCacheDir;
    if (access(cacheDir, W_OK) != 0) {
        NSString *tmpCache = [NSTemporaryDirectory() stringByAppendingPathComponent:@"malimetal-cache"];
        (void)[fm createDirectoryAtPath:tmpCache
            withIntermediateDirectories:YES
                             attributes:nil
                                  error:nil];
        if (tmpCache && access([tmpCache fileSystemRepresentation], W_OK) == 0) {
            cacheDir = [tmpCache fileSystemRepresentation];
        } else {
            cacheDir = "/private/var/tmp";
        }
    }

    const char *loader = (access(kPanvkPath, R_OK) == 0) ? kPanvkPath : kBundleDir;
    id device = native_bridge_metal_open(loader, cacheDir, registryID, onMissingSelector);
    if (!device) {
        const char *err = nmb_last_error();
        NSLog(@"NativeMaliMetal: native_bridge_metal_open failed in %s: %s",
              prog ? prog : "?", err ? err : "(null)");
        emitMaliOpenTrace("ERR", err);
        return nil;
    }
    NSString *devName = [device respondsToSelector:@selector(name)] ? [device name] : nil;
    if (getenv("SHENG_DIAG") != nullptr) emitMaliOpenTrace("OK", devName ? [devName UTF8String] : "mali");
    return device;
}

@end
