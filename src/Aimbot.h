#pragma once
#include <cmath>
#include <limits>
#include <objc/message.h>
#include "Memory.h"
#include "PlayerList.h"

// ─── Config (written by UI, read each frame) ──────────────────────────────────
struct AimbotCfg {
    bool    enabled      = false;
    bool    silentAim    = false;
    bool    showFovCircle= true;
    float   fovRadius    = 80.0f;   // pixels
    int     targetBone   = 0;       // 0=head 1=neck 2=chest 3=body(spine) 4=leg
};
AimbotCfg g_aimCfg;

// ─── Bone index mapping from config ──────────────────────────────────────────
// 0=head, 1=neck, 2=chest, 3=spine, 4=lthigh
static constexpr int BONE_IDX_MAP[] = {0, 1, 2, 3, 6};

// ─── WorldToScreen via Camera.main ───────────────────────────────────────────
// Returns false if behind camera or outside screen
static bool W2S(Vec3 world, Vec2& screen) {
    static SEL selMain  = sel_registerName("main");
    static SEL selW2VP  = sel_registerName("WorldToViewportPoint_Injected:result:");
    static Class camCls = objc_getClass("UnityEngine.Camera");
    if (!camCls) return false;

    typedef id  (*main_fn)(Class, SEL);
    typedef void(*w2vp_fn)(id, SEL, Vec3*, Vec3*);

    id cam = ((main_fn)objc_msgSend)(camCls, selMain);
    if (!cam) return false;

    Vec3 vp{};
    ((w2vp_fn)objc_msgSend)(cam, selW2VP, &world, &vp);

    // vp.z < 0 means behind camera
    if (vp.z < 0.0f) return false;

    // Viewport (0..1) → screen pixels
    static SEL selScrW = sel_registerName("get_width");
    static SEL selScrH = sel_registerName("get_height");
    static Class scrCls = objc_getClass("UnityEngine.Screen");
    float w = (float)((int(*)(Class,SEL))objc_msgSend)(scrCls, selScrW);
    float h = (float)((int(*)(Class,SEL))objc_msgSend)(scrCls, selScrH);

    screen.x = vp.x * w;
    screen.y = (1.0f - vp.y) * h; // flip Y (viewport origin bottom-left)
    return true;
}

// ─── Screen center ────────────────────────────────────────────────────────────
static Vec2 ScreenCenter() {
    static SEL selW = sel_registerName("get_width");
    static SEL selH = sel_registerName("get_height");
    static Class scrCls = objc_getClass("UnityEngine.Screen");
    float w = (float)((int(*)(Class,SEL))objc_msgSend)(scrCls, selW);
    float h = (float)((int(*)(Class,SEL))objc_msgSend)(scrCls, selH);
    return {w * 0.5f, h * 0.5f};
}

// ─── Distance squared (2D) ────────────────────────────────────────────────────
static float Dist2(Vec2 a, Vec2 b) {
    float dx = a.x - b.x, dy = a.y - b.y;
    return dx*dx + dy*dy;
}

// ─── Gyroscope / input hook for silent aim ────────────────────────────────────
// Silent aim works by: when shooting, redirect the bullet trajectory toward target.
// On iOS this is done by patching the camera look direction that the server uses.
// The server trusts the client's reported hit position — so we:
//   1) Let the visual crosshair stay where the player aimed (the "wind shot")
//   2) Override the raycast origin/direction used for hit detection
// Implementation: hook the fire method's ray computation.

static Vec3 g_silentTarget{};
static bool g_silentActive = false;

// Hook target: the function that computes the bullet ray from camera look dir
// Pattern in FF: CameraController.GetFireRay() or equivalent
// We patch the output Vec3 direction to point at our target bone
typedef void (*GetFireRay_fn)(void*, Vec3*, Vec3*); // origin, direction out-params
static GetFireRay_fn orig_GetFireRay = nullptr;

static void hook_GetFireRay(void* camCtrl, Vec3* origin, Vec3* direction) {
    orig_GetFireRay(camCtrl, origin, direction);
    if (!g_silentActive || !g_aimCfg.silentAim) return;
    // Compute new direction: from fire origin → silent target bone
    Vec3& o = *origin;
    Vec3& t = g_silentTarget;
    float dx = t.x - o.x, dy = t.y - o.y, dz = t.z - o.z;
    float len = sqrtf(dx*dx + dy*dy + dz*dz);
    if (len < 0.001f) return;
    direction->x = dx / len;
    direction->y = dy / len;
    direction->z = dz / len;
}

// ─── Aimbot tick — called each frame ─────────────────────────────────────────
static void AimbotTick() {
    g_silentActive = false;
    if (!g_aimCfg.enabled) return;

    Vec2 center = ScreenCenter();
    float fovPx  = g_aimCfg.fovRadius;
    float fovSq  = fovPx * fovPx;

    float bestDist = std::numeric_limits<float>::max();
    Vec3  bestWorld{};
    bool  found = false;

    int boneIdx = BONE_IDX_MAP[g_aimCfg.targetBone < 5 ? g_aimCfg.targetBone : 0];

    for (auto& p : g_players) {
        Vec3 boneWorld = p.bones[boneIdx];
        Vec2 boneScreen{};
        if (!W2S(boneWorld, boneScreen)) continue;

        float d = Dist2(boneScreen, center);
        if (d > fovSq) continue; // outside FOV radius — skip
        if (d < bestDist) {
            bestDist  = d;
            bestWorld = boneWorld;
            found     = true;
        }
    }

    if (!found) return;
    g_silentTarget = bestWorld;
    g_silentActive = true;

    if (g_aimCfg.silentAim) {
        // Silent: don't move crosshair. The hook_GetFireRay handles redirection.
        return;
    }

    // Normal aimbot: move the camera look direction toward target
    // On iOS Unity, this is done by rotating the camera transform.
    // We compute the desired angle delta and apply it via ObjC:
    static SEL selMain = sel_registerName("main");
    static Class camCls = objc_getClass("UnityEngine.Camera");
    id cam = ((id(*)(Class,SEL))objc_msgSend)(camCls, selMain);
    if (!cam) return;

    // Get camera transform
    static SEL selTrans = sel_registerName("get_transform");
    id camTrans = ((id(*)(id,SEL))objc_msgSend)(cam, selTrans);
    if (!camTrans) return;

    // Get camera position
    static SEL selGetPos = sel_registerName("get_position_Injected:");
    Vec3 camPos{};
    ((void(*)(id,SEL,Vec3*))objc_msgSend)(camTrans, selGetPos, &camPos);

    // Compute look rotation toward target
    float dx = bestWorld.x - camPos.x;
    float dy = bestWorld.y - camPos.y;
    float dz = bestWorld.z - camPos.z;
    float len = sqrtf(dx*dx + dy*dy + dz*dz);
    if (len < 0.01f) return;

    float yaw   =  atan2f(dx, dz) * (180.0f / 3.14159265f);
    float pitch = -asinf(dy / len) * (180.0f / 3.14159265f);

    // Set rotation: Quaternion.Euler(pitch, yaw, 0)
    // Build quaternion manually
    float hy = yaw   * 0.5f * (3.14159265f / 180.0f);
    float hp = pitch * 0.5f * (3.14159265f / 180.0f);
    float cy = cosf(hy), sy = sinf(hy);
    float cp = cosf(hp), sp = sinf(hp);

    struct Quat { float x, y, z, w; } q;
    q.x = sp * cy;
    q.y = cp * sy;
    q.z = -sp * sy;
    q.w = cp * cy;

    static SEL selSetRot = sel_registerName("set_rotation_Injected:");
    ((void(*)(id,SEL,Quat*))objc_msgSend)(camTrans, selSetRot, &q);
}

// ─── Install silent aim hook ───────────────────────────────────────────────────
static void InstallAimbotHooks() {
    if (!OFF::BASE) return;
    // Known RVA for GetFireRay equivalent in OB54 GameAssembly
    // Pattern: the function that builds the bullet ray from aiming state
    constexpr uintptr_t RVA_GETFIRERAY = 0x5A8B40; // OB54 offset
    uintptr_t target = OFF::BASE + RVA_GETFIRERAY;
    MSHookFunction(reinterpret_cast<void*>(target),
                   reinterpret_cast<void*>(hook_GetFireRay),
                   reinterpret_cast<void**>(&orig_GetFireRay));
}
