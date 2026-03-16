/*  espnow_msg.h  –  Shared ESP-NOW message structs
 *  *** Identical copy must exist in both carry_master/ and carry_slave/ ***
 */
#pragma once
#include <stdint.h>

#define RFID_UID_STR_LEN 24

// Route chunk: fixed-size point for transfer (Slave autonomous mode)
#define ROUTE_POINT_NODE_LEN  10
#define ROUTE_POINT_UID_LEN   14
#define ROUTE_CHUNK_MAX_POINTS 4

/* ===================================================================
 *  Master  ──►  Slave   (Control / chassis commands)
 * =================================================================== */
typedef struct __attribute__((packed)) {
    uint8_t  state;       // Current RobotState enum value
    float    vX;          // Forward / backward  (-255 … +255)
    float    vY;          // Strafe left / right  (-255 … +255)
    float    vR;          // Rotation             (-255 … +255)
    uint8_t  enableLine;  // 1 = Slave runs local line-follow PID
    uint8_t  enableRFID;  // 1 = Slave scans PN532 tags
    uint8_t  turnCmd;     // 0 = none, 'L' = left-90, 'R' = right-90, 'B' = U-turn
    uint8_t  baseSpeed;   // 0-255  base forward PWM when line-following
    uint8_t  missionStart;  // 1 = start autonomous mission (after routes received)
    uint8_t  missionCancel; // 1 = cancel current mission
    uint8_t  startReturn;   // 1 = at WAIT_AT_DEST, user pressed SW → start return leg
} MasterToSlaveMsg;       // 21 bytes

/* ===================================================================
 *  Slave  ──►  Master   (Sensor feedback + mission status)
 * =================================================================== */
typedef struct __attribute__((packed)) {
    char     rfid_uid[RFID_UID_STR_LEN];  // UID string, null-terminated (e.g. "45:D3:91:83")
    uint8_t  rfid_new;      // 1 = freshly scanned, 0 = stale / empty
    uint8_t  line_detected; // 1 = at least one sensor sees line
    uint8_t  sync_docking;  // 1 = centre sensor on line (Recovery sync)
    int16_t  lineError;     // Weighted position error  -2000 … +2000
    uint8_t  lineBits;      // Raw 3-bit sensor state  (b2 = left, b1 = centre, b0 = right)
    uint8_t  turnDone;      // 1 = Slave finished executing turnCmd
    // Mission status (Slave autonomous mode): 0=idle, 1=ongoing, 2=complete at dest, 3=back at MED
    uint8_t  missionStatus;
    uint8_t  routeIndex;   // current step index in route
    uint8_t  routeTotal;   // total steps in current route
    uint8_t  routeSegment; // 0 = outbound, 1 = return
} SlaveToMasterMsg;

/* ===================================================================
 *  Master  ──►  Slave   (Route data chunk for autonomous mission)
 *  Sent in sequence; Slave assembles full route then runs on missionStart.
 * =================================================================== */
typedef struct __attribute__((packed)) {
    uint8_t  type;         // 0x02 = route chunk (so Slave can distinguish from MasterToSlaveMsg)
    uint8_t  segment;      // 0 = outbound, 1 = return
    uint8_t  chunkIndex;   // chunk index for this segment
    uint8_t  chunkTotal;   // total chunks for this segment
    uint8_t  numPoints;   // number of points in this chunk (1..ROUTE_CHUNK_MAX_POINTS)
    struct {
        char nodeId[ROUTE_POINT_NODE_LEN];
        char uid[ROUTE_POINT_UID_LEN];
        char action;       // 'F', 'L', 'R', 'B'
    } points[ROUTE_CHUNK_MAX_POINTS];
} MasterToSlaveRouteChunk;
