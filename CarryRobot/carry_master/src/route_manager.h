#pragma once
#include <Arduino.h>
#include <ArduinoJson.h>

struct RoutePoint {
    String  nodeId;
    String  rfidUid;
    float   x, y;
    char    action;
};

#define MAX_ROUTE_LEN  30

extern RoutePoint  outRoute[MAX_ROUTE_LEN];
extern uint8_t     outRouteLen;
extern uint8_t     outRouteIdx;

extern RoutePoint  retRoute[MAX_ROUTE_LEN];
extern uint8_t     retRouteLen;
extern uint8_t     retRouteIdx;

extern bool        cancelPending;

void routeClear();

void routeParseAssign(const JsonDocument &doc);
void routeParseReturn(const JsonDocument &doc);
void routeHandleCancel();

RoutePoint* currentRoute();
uint8_t&    currentRouteIdx();
uint8_t     currentRouteLen();
String      expectedNextUid();
String      currentNodeId();
char        upcomingTurnAction();

void routeBuildReverseReturn();

struct UidEntry { const char* nodeId; const char* uid; };
const UidEntry* uidLookupByUid(const char* uid);
const UidEntry* uidLookupByNode(const char* nodeId);
