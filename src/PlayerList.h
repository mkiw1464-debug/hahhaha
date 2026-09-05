#pragma once
#include <vector>
#include <string>
#include <cstring>
#include <mach-o/dyld.h>
#include <objc/runtime.h>
#include <objc/message.h>
#include "Memory.h"
#include "Offsets.h"

struct PlayerEntry {
    uintptr_t   umaData;
    bool        isLocal;
    bool        isTeammate;
    int         curHP;
    int         maxHP;
    Vec3        bones[7]; // head, neck, chest, spine, pelvis, lthigh, rthigh
    Vec3        rootPos;
    std::string name;
};

static std::vector<PlayerEntry> g_players;
static uintptr_t                g_localUMA = 0;
static int                      g_localTeamID = -1;

// ─── Get Transform world position via ObjC message (most reliable on iOS) ─────
static Vec3 GetTransformPos(uintptr_t transform) {
    if (!transform) return {};
    // Unity Transform's get_position() is bridged as an ObjC method
    // We call it via objc_msgSend with the cached SEL
    static SEL selPos = sel_registerName("get_position_Injected:");
    Vec3 out{};
    typedef void (*get_pos_fn)(id, SEL, Vec3*);
    ((get_pos_fn)objc_msgSend)((id)transform, selPos, &out);
    return out;
}

// ─── Walk UMASkeleton.boneHashData Dictionary to find bone Transform ───────────
static uintptr_t GetBoneTransform(uintptr_t skeleton, int boneHash) {
    if (!skeleton) return 0;
    uintptr_t dict = Read<uintptr_t>(skeleton + OFF::SKEL_BONE_DICT);
    if (!dict) return 0;
    uintptr_t entries = Read<uintptr_t>(dict + OFF::DICT_ENTRIES);
    int count = Read<int>(dict + OFF::DICT_COUNT);
    if (!entries || count <= 0 || count > 512) return 0;

    // Each Entry struct (il2cpp Dictionary<int,BoneData>):
    //   int hashCode  @ 0x00
    //   int next      @ 0x04
    //   int key       @ 0x08
    //   BoneData val  @ 0x10  (BoneData has Transform* at 0x10 → BONEDATA_TRANS)
    constexpr size_t ENTRY_STRIDE = 0x28; // sizeof(Entry<int,BoneData>)
    for (int i = 0; i < count; i++) {
        uintptr_t entry = entries + 0x20 + (uintptr_t)i * ENTRY_STRIDE;
        int key = Read<int>(entry + 0x08);
        if (key == boneHash) {
            // val is embedded BoneData, Transform* at val+BONEDATA_TRANS
            uintptr_t boneData = entry + 0x10;
            return Read<uintptr_t>(boneData + OFF::BONEDATA_TRANS);
        }
    }
    return 0;
}

// ─── Read all 7 bone positions for a player ───────────────────────────────────
static void FillBones(PlayerEntry& p) {
    uintptr_t skel = Read<uintptr_t>(p.umaData + OFF::UMA_SKELETON);
    static const struct { int hash; } BONES[7] = {
        {OFF::HASH_HEAD},
        {OFF::HASH_NECK},
        {OFF::HASH_CHEST},
        {OFF::HASH_SPINE},
        {OFF::HASH_PELVIS},
        {OFF::HASH_LTHIGH},
        {OFF::HASH_RTHIGH},
    };
    for (int i = 0; i < 7; i++) {
        uintptr_t t = GetBoneTransform(skel, BONES[i].hash);
        p.bones[i] = GetTransformPos(t);
    }
}

// ─── Get player property value via il2cpp invoke ─────────────────────────────
// We cache the method pointer for GetPropertyValue(int) to avoid repeated lookup
static uintptr_t g_getPropMethod = 0;

static int GetPlayerProp(uintptr_t umaData, int propIdx) {
    if (!umaData) return 0;
    // The property system is accessible via a static method on the player component.
    // Pattern: cast umaData GO → find the player component → call GetPropertyValue
    // For speed we read the prop array directly:
    // Prop array pointer is at UMAData+0x1A0 (inferred from OB54 layout)
    constexpr uintptr_t PROP_ARRAY_OFF = 0x1A0;
    uintptr_t propArr = Read<uintptr_t>(umaData + PROP_ARRAY_OFF);
    if (!propArr) return 0;
    // Array<int> il2cpp layout: items start at 0x20, stride 4
    return Read<int>(propArr + 0x20 + (uintptr_t)propIdx * 4);
}

// ─── Get player display name ─────────────────────────────────────────────────
static std::string GetPlayerName(uintptr_t umaData) {
    // NickName string pointer at UMAData+0xF0 (common OB54 offset)
    constexpr uintptr_t NAME_OFF = 0xF0;
    uintptr_t strObj = Read<uintptr_t>(umaData + NAME_OFF);
    if (!strObj) return "Unknown";
    // il2cpp String: length @ 0x10, chars @ 0x14 (UTF-16)
    int len = Read<int>(strObj + 0x10);
    if (len <= 0 || len > 64) return "Unknown";
    std::string result;
    for (int i = 0; i < len && i < 32; i++) {
        uint16_t ch = Read<uint16_t>(strObj + 0x14 + (uintptr_t)i * 2);
        result += (ch < 128) ? (char)ch : '?';
    }
    return result;
}

// ─── Main player refresh — called each frame ──────────────────────────────────
static void RefreshPlayers() {
    g_players.clear();

    // Find all UMAData MonoBehaviours via ObjC FindObjectsOfType
    // il2cpp exposes this as UnityEngine.Object.FindObjectsOfType(Type)
    // We use ObjC bridging to call it:
    static Class umaClass = nullptr;
    if (!umaClass) {
        umaClass = objc_getClass("UMAData");
        if (!umaClass) return;
    }

    // Call Unity's Object.FindObjectsOfType<UMAData>()
    // Bridged as: [UnityEngine_Object findObjectsOfType: umaClass]
    static SEL selFind = sel_registerName("FindObjectsOfType");
    static Class objectClass = objc_getClass("UnityEngine.Object");
    if (!objectClass) return;

    typedef id (*find_fn)(Class, SEL, Class);
    id arr = ((find_fn)objc_msgSend)(objectClass, selFind, umaClass);
    if (!arr) return;

    // arr is NSArray* of UMAData instances
    NSUInteger cnt = [(NSArray*)arr count];

    for (NSUInteger i = 0; i < cnt; i++) {
        id obj = [(NSArray*)arr objectAtIndex:i];
        uintptr_t uma = (uintptr_t)obj;
        if (!uma) continue;

        PlayerEntry p{};
        p.umaData = uma;
        p.isLocal = Read<bool>(uma + OFF::UMA_IS_LOCAL);

        int teamID = GetPlayerProp(uma, OFF::PROP_TEAM_ID);
        if (p.isLocal) {
            g_localTeamID = teamID;
            g_localUMA = uma;
        }
        p.isTeammate = (!p.isLocal) && (teamID == g_localTeamID);
        // Skip teammates entirely — no ESP for them
        if (p.isTeammate) continue;

        p.curHP = GetPlayerProp(uma, OFF::PROP_CUR_HP);
        p.maxHP = GetPlayerProp(uma, OFF::PROP_MAX_HP);
        if (p.curHP <= 0) continue; // skip dead

        p.name = GetPlayerName(uma);
        FillBones(p);

        // Root position = pelvis bone (index 4)
        p.rootPos = p.bones[4];

        if (!p.isLocal) g_players.push_back(p);
    }
}
