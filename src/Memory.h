#pragma once
#include <cstdint>
#include <cstring>
#include <mach-o/dyld.h>
#include <dlfcn.h>
#include <pthread.h>
#include "Offsets.h"

// ─── Safe read — returns 0 on null ───────────────────────────────────────────
template<typename T>
static inline T Read(uintptr_t addr) {
    if (!addr) return T{};
    T val{};
    memcpy(&val, reinterpret_cast<void*>(addr), sizeof(T));
    return val;
}

template<typename T>
static inline T ReadChain(uintptr_t base, std::initializer_list<uintptr_t> offsets) {
    uintptr_t cur = base;
    for (auto off : offsets) {
        cur = Read<uintptr_t>(cur + off);
        if (!cur) return T{};
    }
    return Read<T>(cur);
}

struct Vec3 { float x, y, z; };
struct Vec2 { float x, y; };

// ─── il2cpp runtime API ───────────────────────────────────────────────────────
typedef void*       (*il2cpp_domain_get_t)();
typedef void**      (*il2cpp_domain_get_assemblies_t)(void*, size_t*);
typedef void*       (*il2cpp_assembly_get_image_t)(void*);
typedef void*       (*il2cpp_class_from_name_t)(void*, const char*, const char*);
typedef void*       (*il2cpp_class_get_method_from_name_t)(void*, const char*, int);
typedef void*       (*il2cpp_method_get_object_t)(void*, void*);
typedef void*       (*il2cpp_runtime_invoke_t)(void*, void*, void**, void**);
typedef void*       (*il2cpp_object_get_class_t)(void*);
typedef void*       (*il2cpp_class_get_field_from_name_t)(void*, const char*);
typedef void        (*il2cpp_field_get_value_t)(void*, void*, void*);
typedef void*       (*il2cpp_string_new_t)(const char*);
typedef const char* (*il2cpp_string_to_utf8_t)(void*);
typedef void*       (*il2cpp_array_new_t)(void*, uintptr_t);

struct IL2CPP {
    il2cpp_domain_get_t             domain_get;
    il2cpp_domain_get_assemblies_t  domain_get_assemblies;
    il2cpp_assembly_get_image_t     assembly_get_image;
    il2cpp_class_from_name_t        class_from_name;
    il2cpp_class_get_method_from_name_t class_get_method;
    il2cpp_runtime_invoke_t         runtime_invoke;
    il2cpp_object_get_class_t       obj_get_class;
    il2cpp_field_get_value_t        field_get_value;
    il2cpp_string_new_t             string_new;
    il2cpp_string_to_utf8_t         string_to_utf8;

    bool Init(void* il2cpp_handle) {
#define BIND(name) name = (decltype(name))dlsym(il2cpp_handle, "il2cpp_" #name); if (!name) return false;
        BIND(domain_get)
        BIND(domain_get_assemblies)
        BIND(assembly_get_image)
        BIND(class_from_name)
        (void*&)class_get_method = dlsym(il2cpp_handle, "il2cpp_class_get_method_from_name");
        if (!class_get_method) return false;
        BIND(runtime_invoke)
        BIND(obj_get_class)
        BIND(field_get_value)
        BIND(string_new)
        (void*&)string_to_utf8 = dlsym(il2cpp_handle, "il2cpp_string_chars");
        return true;
#undef BIND
    }
};

inline IL2CPP g_il2cpp;

// ─── Get il2cpp base from loaded image ───────────────────────────────────────
static inline uintptr_t GetIL2CPPBase() {
    for (uint32_t i = 0; i < _dyld_image_count(); i++) {
        const char* name = _dyld_get_image_name(i);
        if (name && strstr(name, "GameAssembly")) {
            return (uintptr_t)_dyld_get_image_header(i);
        }
    }
    return 0;
}

// ─── FindObjectsOfType via il2cpp Object.FindObjectsOfTypeAll ────────────────
// Used to scan for all UMAData instances → player list
static inline void* FindObjectsOfTypeAll(void* klass) {
    // Il2CppObject* UnityEngine.Object.FindObjectsOfTypeAll(Type)
    // We call it via known method pointer offset pattern
    // Fallback: iterate il2cpp GC heap — too slow for realtime
    // Instead we use the GameObject.FindObjectsOfType<UMAData> method
    return nullptr; // implemented in PlayerList.h with proper invoke
}
