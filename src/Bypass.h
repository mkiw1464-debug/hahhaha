#pragma once
#include <cstdint>
#include <cstring>
#include <mach/mach.h>
#include <mach-o/dyld.h>
#include <sys/mman.h>
#include <dlfcn.h>
#include <errno.h>
#include <sys/stat.h>
#include <objc/runtime.h>
#include <objc/message.h>
#import <UIKit/UIKit.h>
#import <Foundation/Foundation.h>
#include "fishhook.h"
#include "Memory.h"
#include "Offsets.h"

// ─── ARM64 patch helpers ──────────────────────────────────────────────────────
static bool PatchMem(uintptr_t addr, const uint8_t* patch, size_t len) {
    if (!addr) return false;
    vm_address_t page = addr & ~(uintptr_t)(PAGE_SIZE - 1);
    vm_protect(mach_task_self(), page, PAGE_SIZE, false,
               VM_PROT_READ | VM_PROT_WRITE | VM_PROT_EXECUTE);
    memcpy(reinterpret_cast<void*>(addr), patch, len);
    __builtin___clear_cache(reinterpret_cast<char*>(addr),
                            reinterpret_cast<char*>(addr + len));
    return true;
}
static bool NopAt(uintptr_t addr, size_t count = 1) {
    static const uint8_t NOP[4] = {0x1F,0x20,0x03,0xD5};
    bool ok = true;
    for (size_t i = 0; i < count; i++) ok &= PatchMem(addr + i*4, NOP, 4);
    return ok;
}
static bool PatchRetTrue(uintptr_t addr) {
    static const uint8_t P[] = {0x20,0x00,0x80,0x52,0xC0,0x03,0x5F,0xD6};
    return PatchMem(addr, P, sizeof(P));
}
static bool PatchRetZero(uintptr_t addr) {
    static const uint8_t P[] = {0x00,0x00,0x80,0xD2,0xC0,0x03,0x5F,0xD6};
    return PatchMem(addr, P, sizeof(P));
}

// ─── posix hooks ─────────────────────────────────────────────────────────────
typedef int    (*stat_fn_t)(const char*, struct stat*);
typedef int    (*access_fn_t)(const char*, int);
typedef char*  (*getenv_fn_t)(const char*);
typedef int    (*sysctl_fn_t)(int*, unsigned int, void*, size_t*, void*, size_t);
typedef ssize_t(*sendto_fn_t)(int, const void*, size_t, int, const struct sockaddr*, socklen_t);

static stat_fn_t    orig_stat    = nullptr;
static access_fn_t  orig_access  = nullptr;
static getenv_fn_t  orig_getenv  = nullptr;
static sysctl_fn_t  orig_sysctl  = nullptr;
static sendto_fn_t  orig_sendto  = nullptr;

static const char* kBlockedPaths[] = {
    "MobileSubstrate","Cydia","cycript","frida","esign",
    "sideloadly","trollstore","/bin/bash","/usr/bin/ssh",nullptr
};
static bool IsBlocked(const char* path) {
    if (!path) return false;
    for (int i = 0; kBlockedPaths[i]; i++)
        if (strstr(path, kBlockedPaths[i])) return true;
    return false;
}

static int hook_stat(const char* p, struct stat* b) {
    if (IsBlocked(p)) { errno = ENOENT; return -1; }
    return orig_stat(p, b);
}
static int hook_access(const char* p, int m) {
    if (IsBlocked(p)) { errno = ENOENT; return -1; }
    return orig_access(p, m);
}
static char* hook_getenv(const char* n) {
    if (n && (strstr(n,"DYLD_INSERT")||strstr(n,"SUBSTRATE")||strstr(n,"FRIDA")))
        return nullptr;
    return orig_getenv(n);
}
static int hook_sysctl(int* name, unsigned int nl, void* oldp,
                        size_t* oldlenp, void* newp, size_t newlen) {
    int ret = orig_sysctl(name, nl, oldp, oldlenp, newp, newlen);
    if (ret == 0 && nl >= 4 && name[0] == CTL_KERN &&
        name[1] == KERN_PROC && name[2] == KERN_PROC_PID && oldp) {
        struct kinfo_proc* kp = (struct kinfo_proc*)oldp;
        kp->kp_proc.p_flag &= ~P_TRACED;
    }
    return ret;
}
static ssize_t hook_sendto(int s, const void* buf, size_t len, int flags,
                            const struct sockaddr* to, socklen_t tolen) {
    if (to && to->sa_family == AF_INET) {
        uint32_t ip = ntohl(((const struct sockaddr_in*)to)->sin_addr.s_addr);
        if ((ip & 0xFFFFFF00) == 0x67286B00) return (ssize_t)len; // drop report
    }
    return orig_sendto(s, buf, len, flags, to, tolen);
}

// ─── Device ID spoof ─────────────────────────────────────────────────────────
static NSString* g_spoofUUID = nil;
static IMP orig_idForVendor  = nullptr;

static void GenerateSpoofUUID() {
    NSString* path = [[NSBundle mainBundle] bundlePath];
    unsigned long h = 5381;
    for (NSUInteger i = 0; i < path.length; i++)
        h = ((h << 5) + h) ^ [path characterAtIndex:i];
    g_spoofUUID = [NSString stringWithFormat:
        @"%08lX-%04lX-4%03lX-%04lX-%012lX",
        h&0xFFFFFFFF,(h>>32)&0xFFFF,(h>>16)&0x0FFF,
        0x8000|((h>>8)&0x3FFF),h^0xDEADBEEFCAFE];
}

// ─── Streamproof — MTLCaptureManager ─────────────────────────────────────────
static void InstallMetalStreamproof() {
    Class cap = NSClassFromString(@"MTLCaptureManager");
    if (!cap) return;
    Method m = class_getInstanceMethod(cap, NSSelectorFromString(@"isCapturing"));
    if (m) method_setImplementation(m,(IMP)[](__unsafe_unretained id,SEL)->BOOL{return NO;});
}

// ─── Main bypass install ──────────────────────────────────────────────────────
void InstallAllBypasses() {
    uintptr_t base = OFF::BASE;
    if (base) {
        NopAt(base + OFF::RVA_AC_INIT,    4);
        PatchRetZero(base + OFF::RVA_AC_REPORT);
        NopAt(base + OFF::RVA_SEC_LOGIN,  6);
        NopAt(base + OFF::RVA_ANTI_ADD,   4);
        PatchRetTrue(base + OFF::RVA_SEC_LOGIN);
    }

    rebind_symbols((struct rebinding[]){
        {"stat",    (void*)hook_stat,   (void**)&orig_stat},
        {"stat64",  (void*)hook_stat,   (void**)&orig_stat},
        {"access",  (void*)hook_access, (void**)&orig_access},
        {"getenv",  (void*)hook_getenv, (void**)&orig_getenv},
        {"sysctl",  (void*)hook_sysctl, (void**)&orig_sysctl},
        {"sendto",  (void*)hook_sendto, (void**)&orig_sendto},
    }, 6);

    // Bundle ID spoof
    Class bundleCls = [NSBundle class];
    Method bidM = class_getInstanceMethod(bundleCls, @selector(bundleIdentifier));
    if (bidM) {
        method_setImplementation(bidM,
            (IMP)[](__unsafe_unretained id, SEL) -> NSString* {
                return @"com.dts.freefireth";
            });
    }

    // UIScreen.isCaptured → NO
    Class scrCls = [UIScreen class];
    Method capM = class_getInstanceMethod(scrCls, @selector(isCaptured));
    if (capM) method_setImplementation(capM,
        (IMP)[](__unsafe_unretained id,SEL)->BOOL{return NO;});

    // Device UUID spoof
    GenerateSpoofUUID();
    Class devCls = [UIDevice class];
    Method idM = class_getInstanceMethod(devCls, @selector(identifierForVendor));
    if (idM) {
        orig_idForVendor = method_getImplementation(idM);
        method_setImplementation(idM,
            (IMP)[](__unsafe_unretained id, SEL) -> NSUUID* {
                return [[NSUUID alloc] initWithUUIDString:g_spoofUUID];
            });
    }

    InstallMetalStreamproof();
}
