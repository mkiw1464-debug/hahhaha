#import <UIKit/UIKit.h>
#import <Foundation/Foundation.h>
#include <pthread.h>
#include <mach-o/dyld.h>
#include <dlfcn.h>
#include <unistd.h>

#include "Offsets.h"
#include "Memory.h"
#include "Bypass.h"
#include "PlayerList.h"
#include "Aimbot.h"
#include "ESP.h"
#include "Menu.h"

// ─── Wait for GameAssembly to be loaded ──────────────────────────────────────
static uintptr_t WaitForBase() {
    for (;;) {
        for (uint32_t i = 0; i < _dyld_image_count(); i++) {
            const char* name = _dyld_get_image_name(i);
            if (name && strstr(name, "GameAssembly")) {
                uintptr_t base = (uintptr_t)_dyld_get_image_header(i);
                if (base) return base;
            }
        }
        usleep(200000); // 200ms poll
    }
}

// ─── Background init thread ───────────────────────────────────────────────────
static void* InitThread(void*) {
    // 1. Wait for il2cpp to be loaded
    OFF::BASE = WaitForBase();

    // Give the game 2 seconds to finish its own init
    sleep(2);

    // 2. Install all bypasses first — before AC has a chance to scan
    InstallAllBypasses();

    // 3. Install aimbot hooks
    InstallAimbotHooks();

    // 4. Spin up the menu on the main thread
    dispatch_async(dispatch_get_main_queue(), ^{
        SetupMenuWindow();
    });

    return nullptr;
}

// ─── dylib constructor ────────────────────────────────────────────────────────
__attribute__((constructor))
static void FFNETInit() {
    pthread_t t;
    pthread_create(&t, nullptr, InitThread, nullptr);
    pthread_detach(t);
}
