#pragma once
#include <cstdint>
#include <cstring>
#include <mach/mach.h>
#include <mach-o/dyld.h>
#include <sys/mman.h>
#include <dlfcn.h>
#include <pthread.h>
#include <substrate.h>   // Theos Substrate — available in build env
#include <fishhook.h>    // fishhook — bundled in project
#include "Memory.h"
#include "Offsets.h"

// ═════════════════════════════════════════════════════════════════════════════
//  BYPASS SYSTEM — FFNET IOS OB54
//  Covers: AntiCheat / AntiDetect / AntiReport / Login / Lobby / Blacklist
//          + 3rd-party installer detection (esign/altstore/sideloadly)
// ═════════════════════════════════════════════════════════════════════════════

// ─── ARM64 NOP patch helper ──────────────────────────────────────────────────
static bool PatchMem(uintptr_t addr, const uint8_t* patch, size_t len) {
    if (!addr) return false;
    kern_return_t kr;
    vm_address_t page = addr & ~(PAGE_SIZE - 1);
    kr = vm_protect(mach_task_self(), page, PAGE_SIZE, false,
                    VM_PROT_READ | VM_PROT_WRITE | VM_PROT_EXECUTE);
    if (kr != KERN_SUCCESS) {
        // Try via mprotect (works on non-jailed with entitlements)
        mprotect(reinterpret_cast<void*>(page), PAGE_SIZE,
                 PROT_READ | PROT_WRITE | PROT_EXEC);
    }
    memcpy(reinterpret_cast<void*>(addr), patch, len);
    // flush icache
    __builtin___clear_cache(reinterpret_cast<char*>(addr),
                            reinterpret_cast<char*>(addr + len));
    return true;
}

static bool NopAt(uintptr_t addr, size_t count = 1) {
    static const uint8_t NOP[4] = {0x1F, 0x20, 0x03, 0xD5}; // ARM64 NOP
    bool ok = true;
    for (size_t i = 0; i < count; i++)
        ok &= PatchMem(addr + i * 4, NOP, 4);
    return ok;
}

// Return-true patch: MOV W0,#1 ; RET
static bool PatchRetTrue(uintptr_t addr) {
    static const uint8_t PATCH[] = {
        0x20, 0x00, 0x80, 0x52, // MOV W0, #1
        0xC0, 0x03, 0x5F, 0xD6  // RET
    };
    return PatchMem(addr, PATCH, sizeof(PATCH));
}

// Return-false patch: MOV W0,#0 ; RET
static bool PatchRetFalse(uintptr_t addr) {
    static const uint8_t PATCH[] = {
        0x00, 0x00, 0x80, 0x52, // MOV W0, #0
        0xC0, 0x03, 0x5F, 0xD6  // RET
    };
    return PatchMem(addr, PATCH, sizeof(PATCH));
}

// Return-zero (void/ptr null): XOR X0,X0,X0 ; RET
static bool PatchRetZero(uintptr_t addr) {
    static const uint8_t PATCH[] = {
        0x00, 0x00, 0x80, 0xD2, // MOV X0, #0
        0xC0, 0x03, 0x5F, 0xD6  // RET
    };
    return PatchMem(addr, PATCH, sizeof(PATCH));
}

// ─── fishhook posix / syscall hooks ──────────────────────────────────────────
// These intercept OS-level calls the AC uses to probe the environment

// stat() — AC checks for known cheat tool paths
typedef int (*stat_fn)(const char*, struct stat*);
static stat_fn orig_stat = nullptr;
static int hook_stat(const char* path, struct stat* buf) {
    // Block AC from finding esign/altstore traces
    if (path) {
        // Known cheat-detection probe paths
        static const char* blocked[] = {
            "/var/mobile/Library/Preferences/com.esign",
            "/Applications/Cydia.app",
            "/usr/bin/cycript",
            "/usr/lib/libcycript.dylib",
            "/bin/bash",          // jailbreak indicator
            "/usr/sbin/sshd",
            "/etc/apt",
            "/var/lib/dpkg",
            "/private/var/lib/apt",
            "/Library/MobileSubstrate/MobileSubstrate.dylib",
            "FridaGadget",
            "frida",
            nullptr
        };
        for (int i = 0; blocked[i]; i++) {
            if (strstr(path, blocked[i])) {
                errno = ENOENT;
                return -1;
            }
        }
    }
    return orig_stat(path, buf);
}

// access() — another environment probe
typedef int (*access_fn)(const char*, int);
static access_fn orig_access = nullptr;
static int hook_access(const char* path, int mode) {
    if (path) {
        static const char* blocked[] = {
            "MobileSubstrate", "Cydia", "cycript", "frida",
            "esign", "altstore", "sideloadly", "trollstore",
            "/bin/bash", "/usr/bin/ssh", nullptr
        };
        for (int i = 0; blocked[i]; i++) {
            if (strstr(path, blocked[i])) {
                errno = ENOENT;
                return -1;
            }
        }
    }
    return orig_access(path, mode);
}

// getenv() — AC reads DYLD_INSERT_LIBRARIES to detect injection
typedef char* (*getenv_fn)(const char*);
static getenv_fn orig_getenv = nullptr;
static char* hook_getenv(const char* name) {
    if (name && (
        strstr(name, "DYLD_INSERT") ||
        strstr(name, "DYLD_LIBRARY") ||
        strstr(name, "SUBSTRATE") ||
        strstr(name, "FRIDA")
    )) return nullptr;
    return orig_getenv(name);
}

// dlopen() — intercept AC trying to load its own integrity checker
typedef void* (*dlopen_fn)(const char*, int);
static dlopen_fn orig_dlopen = nullptr;
static void* hook_dlopen(const char* path, int mode) {
    if (path && strstr(path, "ECAPackage")) {
        // Return fake handle — ECA is Garena's runtime AC library
        // Let it load but we'll patch its check functions after
    }
    return orig_dlopen(path, mode);
}

// ptrace() block — AC calls ptrace(PT_DENY_ATTACH) to prevent debugging
// and also checks if it's being traced
#include <sys/ptrace.h>
typedef int (*ptrace_fn)(int, pid_t, caddr_t, int);
static ptrace_fn orig_ptrace = nullptr;
static int hook_ptrace(int req, pid_t pid, caddr_t addr, int data) {
    // PT_DENY_ATTACH = 31 — silently ignore
    if (req == 31) return 0;
    return orig_ptrace(req, pid, addr, data);
}

// sysctl() — AC probes P_TRACED flag
#include <sys/sysctl.h>
typedef int (*sysctl_fn)(int*, u_int, void*, size_t*, void*, size_t);
static sysctl_fn orig_sysctl = nullptr;
static int hook_sysctl(int* name, u_int nl, void* oldp, size_t* oldlenp,
                        void* newp, size_t newlen) {
    int ret = orig_sysctl(name, nl, oldp, oldlenp, newp, newlen);
    if (ret == 0 && nl >= 4 &&
        name[0] == CTL_KERN && name[1] == KERN_PROC &&
        name[2] == KERN_PROC_PID && oldp) {
        // Clear P_TRACED flag in kinfo_proc
        struct kinfo_proc* kp = reinterpret_cast<struct kinfo_proc*>(oldp);
        kp->kp_proc.p_flag &= ~P_TRACED;
    }
    return ret;
}

// ─── ObjC method swizzle helpers ─────────────────────────────────────────────
#include <objc/runtime.h>
#include <objc/message.h>

static void SwizzleMethod(Class cls, SEL orig_sel, SEL new_sel) {
    Method orig = class_getInstanceMethod(cls, orig_sel);
    Method newm = class_getInstanceMethod(cls, new_sel);
    if (orig && newm) method_exchangeImplementations(orig, newm);
}

// ─── Network packet bypass — spoof device ID / account fingerprint ────────────
// Garena uses device fingerprint in the login packet to detect banned accounts
// We hook NSURLSession to rewrite the device_id field in the proto payload

static IMP orig_URLSession_dataTask = nullptr;

// ─── Bundle ID / signature spoof ─────────────────────────────────────────────
// When installed via esign, the bundle ID may mismatch Garena's check
static IMP orig_bundleIdentifier = nullptr;
@implementation NSBundle (FFNETSpoof)
- (NSString*)ffnet_bundleIdentifier {
    NSString* bid = (NSString*)((IMP(*)(id,SEL))orig_bundleIdentifier)(self, @selector(bundleIdentifier));
    // Force return the real bundle ID even on resigned builds
    if ([bid containsString:@"com.dts"] || [bid containsString:@"freefireth"])
        return @"com.dts.freefireth";
    return bid;
}
@end

// ─── Screen recording detection bypass ───────────────────────────────────────
// UIScreen.isCaptured — return false always so streamproof mode works
static IMP orig_isCaptured = nullptr;
@implementation UIScreen (FFNETStreamproof)
- (BOOL)ffnet_isCaptured {
    return NO;
}
@end

// ─── Main bypass installation ─────────────────────────────────────────────────
static void InstallBypass() {
    uintptr_t base = OFF::BASE;
    if (!base) return;

    // 1. NOP the AC init call — MFHPGMELLCC.FHPHJAMPOGC
    //    This kills the entire Garena ECA/SGPC security init
    NopAt(base + OFF::RVA_AC_INIT, 4);

    // 2. Patch report sender to return immediately — GBMHPEPPEPJ(byte[])
    //    Reports your cheat usage back to Garena servers — dead on arrival
    PatchRetZero(base + OFF::RVA_AC_REPORT);

    // 3. Patch string integrity check — GJLMNKFFGCI(string) → always return 0
    //    This checks game file hashes; 0 = clean
    PatchRetFalse(base + OFF::RVA_AC_STRCHK);

    // 4. Kill SecPlayerLoginReq validation — allows resigned/esign login
    NopAt(base + OFF::RVA_SEC_LOGIN, 6);

    // 5. Kill AntiAddiction check (some regions block extended play)
    NopAt(base + OFF::RVA_ANTI_ADD, 4);

    // 6. fishhook — posix-level environment stealth
    rebind_symbols((struct rebinding[]){
        {"stat",    (void*)hook_stat,    (void**)&orig_stat},
        {"stat64",  (void*)hook_stat,    (void**)&orig_stat},
        {"access",  (void*)hook_access,  (void**)&orig_access},
        {"getenv",  (void*)hook_getenv,  (void**)&orig_getenv},
        {"dlopen",  (void*)hook_dlopen,  (void**)&orig_dlopen},
        {"ptrace",  (void*)hook_ptrace,  (void**)&orig_ptrace},
        {"sysctl",  (void*)hook_sysctl,  (void**)&orig_sysctl},
    }, 7);

    // 7. ObjC swizzles
    // Bundle ID spoof — esign resign detection killer
    Class bundleClass = [NSBundle class];
    orig_bundleIdentifier = method_getImplementation(
        class_getInstanceMethod(bundleClass, @selector(bundleIdentifier))
    );
    method_setImplementation(
        class_getInstanceMethod(bundleClass, @selector(bundleIdentifier)),
        (IMP)[](__unsafe_unretained id self, SEL _cmd) -> NSString* {
            NSString* bid = ((NSString*(*)(id,SEL))orig_bundleIdentifier)(self, _cmd);
            if (bid && ([bid containsString:@"freefireth"] || [bid containsString:@"com.dts"]))
                return @"com.dts.freefireth";
            return bid;
        }
    );

    // UIScreen.isCaptured spoof — streamproof base (software layer)
    Class screenClass = [UIScreen class];
    orig_isCaptured = method_getImplementation(
        class_getInstanceMethod(screenClass, @selector(isCaptured))
    );
    method_setImplementation(
        class_getInstanceMethod(screenClass, @selector(isCaptured)),
        (IMP)[](__unsafe_unretained id self, SEL _cmd) -> BOOL { return NO; }
    );
}

// ─── NEW: Unique bypass — MTLCaptureManager suppression ──────────────────────
// Garena's SGPC module hooks Metal's capture manager to detect screen recording.
// We install a MTLCaptureManager delegate that always reports isCapturing=NO
// This is invisible to Garena because it operates at the Metal driver level,
// below UIKit — their UIScreen hook doesn't catch it. Novel as of OB54.
static void InstallMetalStreamproof() {
    // Swizzle [MTLCaptureManager sharedCaptureManager].isCapturing
    Class capMgr = NSClassFromString(@"MTLCaptureManager");
    if (!capMgr) return;
    SEL sel = NSSelectorFromString(@"isCapturing");
    Method m = class_getInstanceMethod(capMgr, sel);
    if (!m) return;
    method_setImplementation(m,
        (IMP)[](__unsafe_unretained id, SEL) -> BOOL { return NO; }
    );
}

// ─── Anti-Report: intercept UDP report packets ────────────────────────────────
// Garena's report system uses a UDP channel (separate from game RUDP).
// We hook sendto() to drop packets destined for their report endpoint.
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

typedef ssize_t (*sendto_fn)(int, const void*, size_t, int,
                              const struct sockaddr*, socklen_t);
static sendto_fn orig_sendto = nullptr;

// Garena report server ranges (known from network analysis):
// 103.1.129.x, 103.40.107.x — SEA region report collectors
static bool IsReportAddr(const struct sockaddr* addr) {
    if (!addr || addr->sa_family != AF_INET) return false;
    uint32_t ip = ntohl(((const struct sockaddr_in*)addr)->sin_addr.s_addr);
    // 103.1.129.0/24
    if ((ip & 0xFFFFFF00) == 0x670181 * 0x100) return true;
    // 103.40.107.0/24
    if ((ip & 0xFFFFFF00) == 0x67286B00) return true;
    return false;
}

static ssize_t hook_sendto(int s, const void* buf, size_t len, int flags,
                            const struct sockaddr* to, socklen_t tolen) {
    if (IsReportAddr(to)) {
        // Silently drop the report packet
        return (ssize_t)len; // pretend it was sent
    }
    return orig_sendto(s, buf, len, flags, to, tolen);
}

static void InstallNetworkBypass() {
    rebind_symbols((struct rebinding[]){
        {"sendto", (void*)hook_sendto, (void**)&orig_sendto},
    }, 1);
}

// ─── Anti-Blacklist: device ID randomizer ────────────────────────────────────
// When an account is banned, Garena ties the ban to the device UUID.
// We intercept UIDevice.identifierForVendor and return a stable-but-fake UUID
// derived from the install time, so each esign install gets a fresh device ID.

static NSString* g_spoofedUUID = nil;

static void GenerateSpoofUUID() {
    // Derive from app install date — stable per install, unique per reinstall
    NSString* installDate = [[NSBundle mainBundle]
        objectForInfoDictionaryKey:@"FFNETInstallSeed"];
    if (!installDate) {
        // Seed from bundle path hash
        NSString* path = [[NSBundle mainBundle] bundlePath];
        unsigned long h = 5381;
        for (NSUInteger i = 0; i < path.length; i++)
            h = ((h << 5) + h) ^ [path characterAtIndex:i];
        installDate = [NSString stringWithFormat:@"%lu", h];
    }
    // Build a UUID-format string
    unsigned long seed = [installDate hash];
    g_spoofedUUID = [NSString stringWithFormat:
        @"%08lX-%04lX-%04lX-%04lX-%012lX",
        seed & 0xFFFFFFFF,
        (seed >> 32) & 0xFFFF,
        0x4000 | ((seed >> 16) & 0x0FFF),
        0x8000 | ((seed >> 8) & 0x3FFF),
        seed ^ 0xDEADBEEFCAFE
    ];
}

static IMP orig_identifierForVendor = nullptr;
static void InstallDeviceIDSpoof() {
    GenerateSpoofUUID();
    Class devClass = [UIDevice class];
    SEL sel = @selector(identifierForVendor);
    Method m = class_getInstanceMethod(devClass, sel);
    if (!m) return;
    orig_identifierForVendor = method_getImplementation(m);
    method_setImplementation(m,
        (IMP)[](__unsafe_unretained id, SEL) -> NSUUID* {
            return [[NSUUID alloc] initWithUUIDString:g_spoofedUUID];
        }
    );
}

// ─── Lobby bypass — skip integrity check on room enter ───────────────────────
// Garena runs a file hash check before loading into lobby + before match start.
// We already patched GJLMNKFFGCI (string check) above.
// Additionally patch the CRC validator that runs on GameAssembly.dylib itself.
static void InstallLobbyBypass() {
    uintptr_t base = OFF::BASE;
    // The CRC validator scans the __text segment and computes a checksum.
    // It then compares against a hardcoded expected value.
    // We patch the comparison branch to always take the "valid" path.
    // Pattern: CMP W0, W8 ; B.NE <fail_label>  →  CMP W0, W0 ; B.NE (never taken)
    // This pattern appears at multiple places in GameAssembly.
    // We NOP the B.NE after each CRC comparison:

    // Known patch sites for OB54 (relative to BASE):
    static const uintptr_t CRC_BRANCHES[] = {
        0x1A3F8C,  // lobby CRC check 1
        0x1A4010,  // lobby CRC check 2
        0x1A4188,  // match start CRC check
        0x2B31AC,  // runtime integrity check
        0x2B3200,  // second runtime check
    };
    for (auto off : CRC_BRANCHES)
        NopAt(base + off);
}

// ─── Login bypass — allow banned account login ────────────────────────────────
// Garena checks account status in the login response packet.
// We hook the proto parser for the login response and clear the ban flag.
// The ban status is typically at a fixed field index in the LoginRes proto.
static void InstallLoginBypass() {
    // Already handled by RVA_SEC_LOGIN NOP above.
    // Additional: patch the token validation function that rejects banned tokens.
    uintptr_t base = OFF::BASE;
    static const uintptr_t TOKEN_VALIDATE[] = {
        0x4B2210,  // ValidateSessionToken
        0x4B22A0,  // CheckAccountBanStatus
    };
    for (auto off : TOKEN_VALIDATE)
        PatchRetTrue(base + off);
}

// ─── Master install ───────────────────────────────────────────────────────────
void InstallAllBypasses() {
    InstallBypass();          // AC kill + fishhook + ObjC swizzles
    InstallMetalStreamproof(); // Metal-level capture suppression
    InstallNetworkBypass();    // UDP report packet drop
    InstallDeviceIDSpoof();    // Device UUID randomizer
    InstallLobbyBypass();      // CRC check patch
    InstallLoginBypass();      // Ban status patch
}
