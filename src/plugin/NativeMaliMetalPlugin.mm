// SPDX-License-Identifier: GPL-2.0-only
// NativeMaliMetal bundle principal class (NativeMaliMetalDevice) bridging Metal.framework discovery
// (_MTLRegisterDevices -> IOAcceleratorES -> initWithAcceleratorPort:) to NativeBridgeMetalFrontend
// over libvulkan_panfrost.dylib and libnative_mali_facade.dylib.

#import <Foundation/Foundation.h>
#import <Metal/Metal.h>
#import <IOKit/IOKitLib.h>
#import <objc/runtime.h>
#include <dlfcn.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "NativeMaliPublish.h"
#include "../frontend/NativeBridgeMetalFrontend.h"

extern "C" const char *nmb_last_error(void);

namespace {

constexpr const char *kBundleDir = "/System/Library/Extensions/NativeMaliMetal.bundle";
constexpr const char *kFacadePath = "/System/Library/Extensions/NativeMaliMetal.bundle/libnative_mali_facade.dylib";
constexpr const char *kPanvkPath = "/System/Library/Extensions/NativeMaliMetal.bundle/libvulkan_panfrost.dylib";
constexpr const char *kPublishPath = "/System/Library/Extensions/NativeMaliMetal.publish";
constexpr const char *kClientsPath = "/System/Library/Extensions/NativeMaliMetal.clients";
constexpr const char *kShaderCacheDir = "/private/var/ngpu/cache";

void onMissingSelector(const char *cls, const char *sel) {
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
    emitMaliOpenTrace("OK", devName ? [devName UTF8String] : "mali");
    return device;
}

@end
