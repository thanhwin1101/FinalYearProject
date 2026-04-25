#pragma once
// ====================================================================
//  Carry Robot – STM32 Slave – RFID UID → nodeId lookup
//  ------------------------------------------------------------------
//  Source of truth (keep in sync):
//    Hospital Dashboard/Backend/seed/checkpointsF1.js
//
//  Stored in flash (PROGMEM) as plain C arrays so we don't burn RAM
//  on a String table. UID format: 8 uppercase hex chars, NO colons.
//  Comparison is done on the raw hex (the slave strips ':' if any).
// ====================================================================
#include <Arduino.h>
#include <string.h>

struct UidMapEntry {
    const char* uidHex;     // e.g. "45548083"
    const char* nodeId;     // e.g. "MED"
};

// Order is irrelevant – linear scan is plenty fast for ~37 entries.
static const UidMapEntry CHECKPOINT_MAP[] = {
    { "35FDE183", "R1M1" }, { "45AB4983", "R1M2" }, { "352ECA83", "R1M3" },
    { "450E9D83", "R1O1" }, { "35589783", "R1O2" }, { "35F0F883", "R1O3" },
    { "35F6EF83", "R1D1" }, { "45C73783", "R1D2" },

    { "351A3483", "R2M1" }, { "45BFF683", "R2M2" }, { "35DC8F83", "R2M3" },
    { "4535C383", "R2O1" }, { "45273483", "R2O2" }, { "352A2D83", "R2O3" },
    { "354CB883", "R2D1" }, { "4581A483", "R2D2" },

    { "3522F583", "R3M1" }, { "45C2B883", "R3M2" }, { "35BBB183", "R3M3" },
    { "4526F383", "R3O1" }, { "451DA483", "R3O2" }, { "351E4783", "R3O3" },
    { "3545AF83", "R3D1" }, { "3535BA83", "R3D2" },

    { "4583FB83", "R4M1" }, { "458E0083", "R4M2" }, { "354D9B83", "R4M3" },
    { "457D5A83", "R4O1" }, { "35DBEA83", "R4O2" }, { "35EB1883", "R4O3" },
    { "35489F83", "R4D1" }, { "35267983", "R4D2" },

    { "45548083", "MED"   },
    { "352C3C83", "J4"    },
    { "4586AC83", "H_TOP" },
    { "45793183", "H_BOT" },
    { "45D39183", "H_MED" },
};
static const size_t CHECKPOINT_MAP_LEN =
    sizeof(CHECKPOINT_MAP) / sizeof(CHECKPOINT_MAP[0]);

// Returns "" if the UID is unknown.
inline const char* uidToNodeId(const char* uidHex) {
    if (!uidHex || !*uidHex) return "";
    for (size_t i = 0; i < CHECKPOINT_MAP_LEN; ++i) {
        if (strcmp(uidHex, CHECKPOINT_MAP[i].uidHex) == 0)
            return CHECKPOINT_MAP[i].nodeId;
    }
    return "";
}
