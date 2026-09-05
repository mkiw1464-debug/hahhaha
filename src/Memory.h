#pragma once
#include <cstdint>
#include <cstring>
#include <mach-o/dyld.h>
#include <dlfcn.h>
#include "Offsets.h"

template<typename T>
static inline T Read(uintptr_t addr) {
    if (!addr) return T{};
    T val{};
    memcpy(&val, reinterpret_cast<void*>(addr), sizeof(T));
    return val;
}

struct Vec3 { float x, y, z; };
struct Vec2 { float x, y; };

static inline uintptr_t GetIL2CPPBase() {
    for (uint32_t i = 0; i < _dyld_image_count(); i++) {
        const char* name = _dyld_get_image_name(i);
        if (name && strstr(name, "GameAssembly"))
            return (uintptr_t)_dyld_get_image_header(i);
    }
    return 0;
}
