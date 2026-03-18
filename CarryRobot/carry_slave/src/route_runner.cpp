#include "route_runner.h"
#include "config.h"
#include "globals.h"
#include "espnow_comm.h"
#include "line_follower.h"
#include "mecanum.h"
#include "rfid_reader.h"
#include <string.h>

struct UidEntry { const char* nodeId; const char* uid; };
static const UidEntry UID_MAP[] = {
    {"MED",   "45:54:80:83"},
    {"H_MED", "45:D3:91:83"},
    {"H_BOT", "45:79:31:83"},
    {"H_TOP", "45:86:AC:83"},
    {"J4",    "35:2C:3C:83"},
    {"R1M1",  "35:FD:E1:83"}, {"R1M2",  "45:AB:49:83"}, {"R1M3", "35:2E:CA:83"},
    {"R1O1",  "45:0E:9D:83"}, {"R1O2",  "35:58:97:83"}, {"R1O3", "35:F0:F8:83"},
    {"R1D1",  "35:F6:EF:83"}, {"R1D2",  "45:C7:37:83"},
    {"R2M1",  "35:1A:34:83"}, {"R2M2",  "45:BF:F6:83"}, {"R2M3", "35:DC:8F:83"},
    {"R2O1",  "45:35:C3:83"}, {"R2O2",  "45:27:34:83"}, {"R2O3", "35:2A:2D:83"},
    {"R2D1",  "35:4C:B8:83"}, {"R2D2",  "45:81:A4:83"},
    {"R3M1",  "35:22:F5:83"}, {"R3M2",  "45:C2:B8:83"}, {"R3M3", "35:BB:B1:83"},
    {"R3O1",  "45:26:F3:83"}, {"R3O2",  "45:1D:A4:83"}, {"R3O3", "35:1E:47:83"},
    {"R3D1",  "35:45:AF:83"}, {"R3D2",  "35:35:BA:83"},
    {"R4M1",  "45:83:FB:83"}, {"R4M2",  "45:8E:00:83"}, {"R4M3", "35:4D:9B:83"},
    {"R4O1",  "45:7D:5A:83"}, {"R4O2",  "35:DB:EA:83"}, {"R4O3", "35:EB:18:83"},
    {"R4D1",  "35:48:9F:83"}, {"R4D2",  "35:26:79:83"},
};
static const uint8_t UID_MAP_SIZE = sizeof(UID_MAP) / sizeof(UID_MAP[0]);

static const UidEntry* uidLookup(const char* uid) {
    for (uint8_t i = 0; i < UID_MAP_SIZE; i++)
        if (strcasecmp(UID_MAP[i].uid, uid) == 0) return &UID_MAP[i];
    return nullptr;
}

struct SlRoutePoint {
    char nodeId[ROUTE_POINT_NODE_LEN];
    char uid[ROUTE_POINT_UID_LEN];
    char action;
};

static SlRoutePoint s_outRoute[SL_ROUTE_MAX_LEN];
static SlRoutePoint s_retRoute[SL_ROUTE_MAX_LEN];
static uint8_t s_outLen = 0, s_retLen = 0;
static uint8_t s_outIdx = 0, s_retIdx = 0;

enum SlState {
    SL_IDLE,
    SL_OUTBOUND,
    SL_AT_DEST_UTURN,
    SL_WAIT_AT_DEST,
    SL_BACK,
    SL_AT_MED_UTURN
};
static SlState s_state = SL_IDLE;
static bool s_turnBusy = false;
static unsigned long s_waitAtDestStartMs = 0;
static bool s_startReturnRequested = false;
static uint8_t s_destUturnStep = 0;
static unsigned long s_destUturnStartMs = 0;
static const unsigned long LINE_REACQUIRE_MS = 500;
static char s_lastMatchedUid[ROUTE_POINT_UID_LEN];
static unsigned long s_lastRfidMs = 0;
static const unsigned long RFID_DEBOUNCE_MS = 800;

#define ROUTE_CHUNK_TYPE 0x02

void routeRunnerOnChunk(const uint8_t* data, int len) {
    if (len != (int)sizeof(MasterToSlaveRouteChunk)) return;
    const MasterToSlaveRouteChunk* c = (const MasterToSlaveRouteChunk*)data;
    if (c->type != ROUTE_CHUNK_TYPE) return;

    SlRoutePoint* dest = (c->segment == 0) ? s_outRoute : s_retRoute;
    uint8_t* destLen = (c->segment == 0) ? &s_outLen : &s_retLen;
    uint8_t start = c->chunkIndex * ROUTE_CHUNK_MAX_POINTS;
    if (start + c->numPoints > SL_ROUTE_MAX_LEN) return;

    for (uint8_t i = 0; i < c->numPoints; i++) {
        memcpy(dest[start + i].nodeId, c->points[i].nodeId, ROUTE_POINT_NODE_LEN);
        dest[start + i].nodeId[ROUTE_POINT_NODE_LEN - 1] = '\0';
        memcpy(dest[start + i].uid, c->points[i].uid, ROUTE_POINT_UID_LEN);
        dest[start + i].uid[ROUTE_POINT_UID_LEN - 1] = '\0';
        dest[start + i].action = c->points[i].action;
    }
    if (c->chunkIndex == c->chunkTotal - 1) {
        *destLen = start + c->numPoints;
    }
    Serial.printf("[ROUTE] Chunk %u/%u seg=%u -> %u pts, len=%u\n",
                  (unsigned)c->chunkIndex + 1, (unsigned)c->chunkTotal,
                  (unsigned)c->segment, (unsigned)c->numPoints, (unsigned)*destLen);
}

void routeRunnerOnMissionStart() {
    if (s_outLen == 0) {
        Serial.println(F("[ROUTE] Ignore start: no outbound route"));
        return;
    }
    s_state = SL_OUTBOUND;
    s_outIdx = 0;
    s_retIdx = 0;
    s_lastMatchedUid[0] = '\0';
    s_turnBusy = false;
    slaveReport.missionStatus = SL_MISSION_STATUS_ONGOING;
    slaveReport.routeIndex = 0;
    slaveReport.routeTotal = s_outLen;
    Serial.println(F("[ROUTE] Mission started – OUTBOUND"));
}

void routeRunnerOnMissionCancel() {
    s_state = SL_IDLE;
    s_turnBusy = false;
    s_startReturnRequested = false;
    s_destUturnStep = 0;
    slaveReport.missionStatus = SL_MISSION_STATUS_IDLE;
    slaveReport.routeIndex = 0;
    slaveReport.routeTotal = 0;
    mecanumHardBrake();
    Serial.println(F("[ROUTE] Mission cancelled"));
}

void routeRunnerOnStartReturn() {
    if (s_state == SL_WAIT_AT_DEST) {
        s_startReturnRequested = true;
        Serial.println(F("[ROUTE] Start return (SW)"));
    }
}

static void executeTurn(char act) {
    if (act != 'L' && act != 'R' && act != 'B') return;
    s_turnBusy = true;
    mecanumStop();
    switch (act) {
        case 'L': mecanumTurnLeft90();   break;
        case 'R': mecanumTurnRight90();  break;
        case 'B': mecanumUturn();        break;
        default:  break;
    }
    delay(100);
    lineResetPID();
    s_turnBusy = false;
    slaveReport.turnDone = 1;
}

bool routeRunnerMissionActive() {
    return (s_state == SL_OUTBOUND || s_state == SL_BACK);
}

void routeRunnerUpdate() {
    const unsigned long now = millis();

    if (s_state == SL_OUTBOUND) {
        slaveReport.missionStatus = SL_MISSION_STATUS_ONGOING;
        slaveReport.routeIndex = s_outIdx;
        slaveReport.routeTotal = s_outLen;
        slaveReport.routeSegment = 0;
    } else if (s_state == SL_BACK) {
        slaveReport.missionStatus = SL_MISSION_STATUS_ONGOING;
        slaveReport.routeIndex = s_retIdx;
        slaveReport.routeTotal = s_retLen;
        slaveReport.routeSegment = 1;
    }

    switch (s_state) {
        case SL_IDLE:
            return;

        case SL_AT_DEST_UTURN: {
            if (s_destUturnStep == 0) {
                executeTurn('B');
                s_destUturnStep = 1;
                s_destUturnStartMs = now;
            } else {
                if ((now - s_destUturnStartMs) >= LINE_REACQUIRE_MS || lineDetected()) {
                    s_state = SL_WAIT_AT_DEST;
                    s_waitAtDestStartMs = now;
                    slaveReport.missionStatus = SL_MISSION_STATUS_COMPLETE;
                    slaveReport.routeIndex = s_outIdx;
                    slaveReport.routeTotal = s_outLen;
                    slaveReport.routeSegment = 0;
                    s_destUturnStep = 0;
                    Serial.println(F("[ROUTE] At destination – WAIT_AT_DEST (press SW to return)"));
                }
            }
            return;
        }

        case SL_WAIT_AT_DEST:
            if (s_startReturnRequested && s_retLen > 0) {
                s_startReturnRequested = false;
                s_state = SL_BACK;
                slaveReport.missionStatus = SL_MISSION_STATUS_ONGOING;
                slaveReport.routeIndex = 0;
                slaveReport.routeTotal = s_retLen;
                slaveReport.routeSegment = 1;
                Serial.println(F("[ROUTE] Starting BACK"));
            }
            return;

        case SL_AT_MED_UTURN:
            executeTurn('B');
            s_state = SL_IDLE;
            slaveReport.missionStatus = SL_MISSION_STATUS_BACK;
            slaveReport.routeIndex = s_retLen;
            slaveReport.routeTotal = s_retLen;
            slaveReport.routeSegment = 1;
            Serial.println(F("[ROUTE] At MED – 180° done, mission done"));
            return;

        case SL_OUTBOUND:
        case SL_BACK: {
            if (s_turnBusy) return;

            SlRoutePoint* route = (s_state == SL_OUTBOUND) ? s_outRoute : s_retRoute;
            uint8_t len = (s_state == SL_OUTBOUND) ? s_outLen : s_retLen;
            uint8_t* idx = (s_state == SL_OUTBOUND) ? &s_outIdx : &s_retIdx;

            if (*idx + 1 >= len) {
                if (s_state == SL_OUTBOUND) {
                    s_state = SL_AT_DEST_UTURN;
                    s_destUturnStep = 0;
                    return;
                } else {
                    s_state = SL_AT_MED_UTURN;
                    return;
                }
            }

            const char* expectedUid = route[*idx + 1].uid;
            char act = route[*idx + 1].action;

            if (slaveReport.rfid_new && slaveReport.rfid_uid[0] != '\0') {
                if (now - s_lastRfidMs < RFID_DEBOUNCE_MS) return;
                if (strcasecmp(slaveReport.rfid_uid, expectedUid) == 0) {
                    s_lastRfidMs = now;
                    (*idx)++;
                    slaveReport.routeIndex = *idx;
                    slaveReport.routeTotal = len;

                    if (*idx == len - 1) {
                        if (s_state == SL_OUTBOUND) {
                            s_state = SL_AT_DEST_UTURN;
                            s_destUturnStep = 0;
                            Serial.println(F("[ROUTE] At last node – uturn then WAIT_AT_DEST"));
                        } else {
                            s_state = SL_AT_MED_UTURN;
                            Serial.println(F("[ROUTE] At MED – uturn then done"));
                        }
                        return;
                    }

                    if (act == 'L' || act == 'R' || act == 'B') {
                        executeTurn(act);
                    }
                }
            }
            return;
        }
    }
}
