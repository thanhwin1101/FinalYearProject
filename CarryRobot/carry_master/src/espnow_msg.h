#pragma once
#include <stdint.h>

#define RFID_UID_STR_LEN 24

#define ROUTE_POINT_NODE_LEN  10
#define ROUTE_POINT_UID_LEN   14
#define ROUTE_CHUNK_MAX_POINTS 4

typedef struct __attribute__((packed)) {
    uint8_t  state;
    float    vX;
    float    vY;
    float    vR;
    uint8_t  enableLine;
    uint8_t  enableRFID;
    uint8_t  turnCmd;
    uint8_t  baseSpeed;
    uint8_t  missionStart;
    uint8_t  missionCancel;
    uint8_t  startReturn;
} MasterToSlaveMsg;

typedef struct __attribute__((packed)) {
    char     rfid_uid[RFID_UID_STR_LEN];
    uint8_t  rfid_new;
    uint8_t  line_detected;
    uint8_t  sync_docking;
    int16_t  lineError;
    uint8_t  lineBits;
    uint8_t  turnDone;

    uint8_t  missionStatus;
    uint8_t  routeIndex;
    uint8_t  routeTotal;
    uint8_t  routeSegment;
} SlaveToMasterMsg;

typedef struct __attribute__((packed)) {
    uint8_t  type;
    uint8_t  segment;
    uint8_t  chunkIndex;
    uint8_t  chunkTotal;
    uint8_t  numPoints;
    struct {
        char nodeId[ROUTE_POINT_NODE_LEN];
        char uid[ROUTE_POINT_UID_LEN];
        char action;
    } points[ROUTE_CHUNK_MAX_POINTS];
} MasterToSlaveRouteChunk;
