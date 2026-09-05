#pragma once
#include <cstdint>

// FFNET IOS — OB54 — com.dts.freefireth
// Offsets from dump.cs (il2cpp Assembly-CSharp)

namespace OFF {

inline uintptr_t BASE = 0;

// Player property indices (NIPHKJAOKGI enum)
constexpr int PROP_CUR_HP     = 0;
constexpr int PROP_MAX_HP     = 1;
constexpr int PROP_VEST       = 2;
constexpr int PROP_HELMET     = 3;
constexpr int PROP_STATUS     = 11;
constexpr int PROP_KILL_COUNT = 13;
constexpr int PROP_TEAM_ID    = 34;

// UMAData (MonoBehaviour on player GO)
constexpr uintptr_t UMA_PLAYERID    = 0x68;
constexpr uintptr_t UMA_IS_LOCAL    = 0xE8;
constexpr uintptr_t UMA_SKELETON    = 0x138;

// UMASkeleton
constexpr uintptr_t SKEL_BONE_DICT  = 0x28;
constexpr uintptr_t BONEDATA_TRANS  = 0x10;

// Unity Dictionary<int,BoneData> internals
constexpr uintptr_t DICT_ENTRIES    = 0x18;
constexpr uintptr_t DICT_COUNT      = 0x40;
constexpr uintptr_t ENTRY_KEY       = 0x08;
constexpr uintptr_t ENTRY_VAL       = 0x10;

// UMA bone name hashes
constexpr int HASH_HEAD    = 0x5C61C9F8;
constexpr int HASH_NECK    = 0x35B9F7C4;
constexpr int HASH_CHEST   = 0x63FE43D6;
constexpr int HASH_SPINE   = 0x2C9DAA0B;
constexpr int HASH_PELVIS  = 0x791A5F3A;
constexpr int HASH_LTHIGH  = 0x18C72E4F;
constexpr int HASH_RTHIGH  = 0x2A3D9B11;

// Transform position native offset
constexpr uintptr_t TRANSFORM_POS   = 0x90;

// Anti-cheat RVAs (dump.cs → multiply by 4 for ARM64 il2cpp absolute)
constexpr uintptr_t RVA_AC_INIT     = 0x150AC8;  // MFHPGMELLCC.FHPHJAMPOGC
constexpr uintptr_t RVA_AC_REPORT   = 0x15110C;  // MFHPGMELLCC.GBMHPEPPEPJ
constexpr uintptr_t RVA_AC_STRCHK   = 0x1519A4;  // MFHPGMELLCC.GJLMNKFFGCI
constexpr uintptr_t RVA_SEC_LOGIN   = 0x154B88;  // SecPlayerLoginReq
constexpr uintptr_t RVA_ANTI_ADD    = 0x155010;  // AntiAddictionPlayerInfoNtf

} // namespace OFF
