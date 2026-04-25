#pragma once
// ====================================================================
//  Carry Robot – ESP32 MASTER – checkpoint_map.h
//  ------------------------------------------------------------------
//  Source of truth: Hospital Dashboard/Backend/seed/checkpointsF1.js
//  Backend cpId encoding (utils/checkpointIds.js::uidStringToCpId):
//
//     cpId = (UID[byte_n-2] << 8) | UID[byte_n-1]    // last 2 bytes
//
//  The backend transmits `id` (number) on `carry/robot/evt`
//  events, and `nodeId` (string) inside `mission.outboundRoute /
//  returnRoute`. The master uses this table to translate between the
//  two whenever it talks to the broker.
// ====================================================================
#include <Arduino.h>
#include <stdint.h>
#include <string.h>

struct CpEntry {
    const char* name;
    uint16_t    cpId;     // (hi<<8)|lo of last 2 UID bytes
};

// 37 entries — must mirror seed/checkpointsF1.js exactly.
// (Backend's encoding has a few real collisions on cpId — that is a
// backend property, not a bug here. cpIdToName returns the *first*
// matching name in those rare cases.)
static const CpEntry CP_TABLE[] = {
    {"R1M1", 0xE183}, {"R1M2", 0x4983}, {"R1M3", 0xCA83},
    {"R1O1", 0x9D83}, {"R1O2", 0x9783}, {"R1O3", 0xF883},
    {"R1D1", 0xEF83}, {"R1D2", 0x3783},

    {"R2M1", 0x3483}, {"R2M2", 0xF683}, {"R2M3", 0x8F83},
    {"R2O1", 0xC383}, {"R2O2", 0x3483}, {"R2O3", 0x2D83},
    {"R2D1", 0xB883}, {"R2D2", 0xA483},

    {"R3M1", 0xF583}, {"R3M2", 0xB883}, {"R3M3", 0xB183},
    {"R3O1", 0xF383}, {"R3O2", 0xA483}, {"R3O3", 0x4783},
    {"R3D1", 0xAF83}, {"R3D2", 0xBA83},

    {"R4M1", 0xFB83}, {"R4M2", 0x0083}, {"R4M3", 0x9B83},
    {"R4O1", 0x5A83}, {"R4O2", 0xEA83}, {"R4O3", 0x1883},
    {"R4D1", 0x9F83}, {"R4D2", 0x7983},

    {"MED",   0x8083},
    {"J4",    0x3C83},
    {"H_TOP", 0xAC83},
    {"H_BOT", 0x3183},
    {"H_MED", 0x9183},
};
static constexpr size_t CP_TABLE_N = sizeof(CP_TABLE) / sizeof(CP_TABLE[0]);

// Returns 0 when the name is not found (0 is not a valid cpId on this map).
inline uint16_t nameToCpId(const char* name) {
    if (!name || !*name) return 0;
    for (size_t i = 0; i < CP_TABLE_N; ++i) {
        if (strcmp(CP_TABLE[i].name, name) == 0) return CP_TABLE[i].cpId;
    }
    return 0;
}
inline uint16_t nameToCpId(const String& name) { return nameToCpId(name.c_str()); }

// Returns nullptr when no name maps to that cpId.
inline const char* cpIdToName(uint16_t cpId) {
    if (cpId == 0) return nullptr;
    for (size_t i = 0; i < CP_TABLE_N; ++i) {
        if (CP_TABLE[i].cpId == cpId) return CP_TABLE[i].name;
    }
    return nullptr;
}
