#pragma once
#include <Arduino.h>
#include "config.h"

// ── Robot modes (matches ESP32 enum) ────────────────────────────────
enum RobotMode : uint8_t {
    MODE_AUTO   = 0,
    MODE_FOLLOW = 1,
    MODE_FIND   = 2,
    MODE_RECOVERY = 3
};

// ── Route point ─────────────────────────────────────────────────────
struct RoutePoint {
    uint16_t checkpointId;
    uint8_t  action;        // 'F','L','R','B','S'
};

// ── Shared globals ──────────────────────────────────────────────────
extern volatile RobotMode g_mode;

// route
extern RoutePoint g_route[MAX_ROUTE_LEN];
extern uint8_t    g_routeLen;
extern uint8_t    g_routeIdx;

// velocity command from ESP32 (Follow mode)
extern volatile int16_t g_cmdVx;
extern volatile int16_t g_cmdVy;
extern volatile int16_t g_cmdVr;
extern volatile bool    g_newVelCmd;

// mission control flags
extern volatile bool    g_missionStart;
extern volatile bool    g_missionCancel;
extern volatile bool    g_missionRunning;

// obstacle
extern volatile bool    g_obstacleDetected;

// latest NFC
extern volatile uint16_t g_lastNfcId;
extern volatile bool     g_newNfc;
