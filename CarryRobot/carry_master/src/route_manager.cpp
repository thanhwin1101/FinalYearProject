#include "route_manager.h"
#include "config.h"
#include "globals.h"
#include "mqtt_comm.h"
#include "buzzer.h"
#include "display.h"

RoutePoint outRoute[MAX_ROUTE_LEN];
uint8_t    outRouteLen  = 0;
uint8_t    outRouteIdx  = 0;

RoutePoint retRoute[MAX_ROUTE_LEN];
uint8_t    retRouteLen  = 0;
uint8_t    retRouteIdx  = 0;

bool cancelPending = false;

static const UidEntry UID_MAP[] = {
    {"MED",   "45:54:80:83"},
    {"H_MED", "45:D3:91:83"},
    {"H_BOT", "45:79:31:83"},
    {"H_TOP", "45:86:AC:83"},
    {"J4",    "35:2C:3C:83"},

    {"R1M1",  "35:FD:E1:83"},  {"R1M2",  "45:AB:49:83"},  {"R1M3", "35:2E:CA:83"},
    {"R1O1",  "45:0E:9D:83"},  {"R1O2",  "35:58:97:83"},  {"R1O3", "35:F0:F8:83"},
    {"R1D1",  "35:F6:EF:83"},  {"R1D2",  "45:C7:37:83"},

    {"R2M1",  "35:1A:34:83"},  {"R2M2",  "45:BF:F6:83"},  {"R2M3", "35:DC:8F:83"},
    {"R2O1",  "45:35:C3:83"},  {"R2O2",  "45:27:34:83"},  {"R2O3", "35:2A:2D:83"},
    {"R2D1",  "35:4C:B8:83"},  {"R2D2",  "45:81:A4:83"},

    {"R3M1",  "35:22:F5:83"},  {"R3M2",  "45:C2:B8:83"},  {"R3M3", "35:BB:B1:83"},
    {"R3O1",  "45:26:F3:83"},  {"R3O2",  "45:1D:A4:83"},  {"R3O3", "35:1E:47:83"},
    {"R3D1",  "35:45:AF:83"},  {"R3D2",  "35:35:BA:83"},

    {"R4M1",  "45:83:FB:83"},  {"R4M2",  "45:8E:00:83"},  {"R4M3", "35:4D:9B:83"},
    {"R4O1",  "45:7D:5A:83"},  {"R4O2",  "35:DB:EA:83"},  {"R4O3", "35:EB:18:83"},
    {"R4D1",  "35:48:9F:83"},  {"R4D2",  "35:26:79:83"},
};
static const uint8_t UID_MAP_SIZE = sizeof(UID_MAP) / sizeof(UID_MAP[0]);

const UidEntry* uidLookupByUid(const char* uid) {
    for (uint8_t i = 0; i < UID_MAP_SIZE; i++)
        if (strcasecmp(UID_MAP[i].uid, uid) == 0) return &UID_MAP[i];
    return nullptr;
}

const UidEntry* uidLookupByNode(const char* nodeId) {
    for (uint8_t i = 0; i < UID_MAP_SIZE; i++)
        if (strcasecmp(UID_MAP[i].nodeId, nodeId) == 0) return &UID_MAP[i];
    return nullptr;
}

void routeClear() {
    outRouteLen = outRouteIdx = 0;
    retRouteLen = retRouteIdx = 0;
    cancelPending = false;
    missionId = "";
    patientName = "";
    destBed = "";
}

static void parseRouteArray(const JsonArrayConst &arr, RoutePoint* dest, uint8_t &len) {
    len = 0;
    for (JsonVariantConst v : arr) {
        if (len >= MAX_ROUTE_LEN) break;
        dest[len].nodeId  = v["nodeId"].as<String>();
        dest[len].rfidUid = v["rfidUid"].as<String>();
        dest[len].x       = v["x"] | 0.0f;
        dest[len].y       = v["y"] | 0.0f;
        const char* a     = v["action"];
        dest[len].action  = (a && a[0]) ? a[0] : 'F';
        len++;
    }
}

static String normalizeTextField(const String& in) {
    String s = in;
    s.trim();
    if (s.equalsIgnoreCase("null") || s.equalsIgnoreCase("undefined")) return "";
    return s;
}

void routeParseAssign(const JsonDocument &doc) {

    JsonVariantConst root = doc.as<JsonVariantConst>();
    if (doc.containsKey("mission") && doc["mission"].is<JsonObjectConst>()) {
        root = doc["mission"].as<JsonVariantConst>();
    }

    missionId   = normalizeTextField(root["missionId"].as<String>());
    patientName = normalizeTextField(root["patientName"].as<String>());

    destBed = normalizeTextField(root["destBed"].as<String>());
    if (destBed.length() == 0) {
        destBed = normalizeTextField(root["bedId"].as<String>());
    }

    if (root["outboundRoute"].is<JsonArrayConst>()) {
        parseRouteArray(root["outboundRoute"].as<JsonArrayConst>(), outRoute, outRouteLen);
    }
    if (root["returnRoute"].is<JsonArrayConst>()) {
        parseRouteArray(root["returnRoute"].as<JsonArrayConst>(), retRoute, retRouteLen);
    }

    if (destBed.length() == 0 && outRouteLen > 0) {
        String tail = outRoute[outRouteLen - 1].nodeId;
        if (tail.startsWith("R")) {
            destBed = tail;
        }
    }

    Serial.printf("[ROUTE] Mission %s → %s (%u out, %u ret)\n",
                  missionId.c_str(), destBed.c_str(), outRouteLen, retRouteLen);

    displayIdle();

}

void routeParseReturn(const JsonDocument &doc) {
    if (doc.containsKey("returnRoute")) {
        parseRouteArray(doc["returnRoute"].as<JsonArrayConst>(), retRoute, retRouteLen);
        retRouteIdx = 0;
        Serial.printf("[ROUTE] Return route received (%u pts)\n", retRouteLen);
    }
    if (retRouteLen < 2) {
        Serial.println(F("[ROUTE] Invalid return route – building reverse"));
        routeBuildReverseReturn();
    }
}

void routeHandleCancel() {
    cancelPending = true;
    displayCentered("MISSION CANCELED", "Returning to MED...");
    Serial.println(F("[ROUTE] Cancel pending"));
}

RoutePoint* currentRoute() {
    return (robotState == ST_BACK) ? retRoute : outRoute;
}

uint8_t& currentRouteIdx() {
    return (robotState == ST_BACK) ? retRouteIdx : outRouteIdx;
}

uint8_t currentRouteLen() {
    return (robotState == ST_BACK) ? retRouteLen : outRouteLen;
}

String expectedNextUid() {
    RoutePoint* r = currentRoute();
    uint8_t idx   = currentRouteIdx();
    uint8_t len   = currentRouteLen();
    if (idx + 1 < len) return r[idx + 1].rfidUid;
    return "";
}

String currentNodeId() {
    RoutePoint* r = currentRoute();
    uint8_t idx   = currentRouteIdx();
    uint8_t len   = currentRouteLen();
    if (idx < len) return r[idx].nodeId;
    return "???";
}

char upcomingTurnAction() {
    RoutePoint* r = currentRoute();
    uint8_t idx   = currentRouteIdx();
    uint8_t len   = currentRouteLen();
    if (idx + 1 < len) return r[idx + 1].action;
    return 'F';
}

void routeBuildReverseReturn() {
    retRouteLen = 0;
    uint8_t visited = outRouteIdx + 1;
    for (int i = visited - 1; i >= 0 && retRouteLen < MAX_ROUTE_LEN; i--) {
        retRoute[retRouteLen] = outRoute[i];

        if (retRoute[retRouteLen].action == 'L')      retRoute[retRouteLen].action = 'R';
        else if (retRoute[retRouteLen].action == 'R')  retRoute[retRouteLen].action = 'L';
        retRouteLen++;
    }
    retRouteIdx = 0;
    Serial.printf("[ROUTE] Built reverse return (%u pts)\n", retRouteLen);
}
