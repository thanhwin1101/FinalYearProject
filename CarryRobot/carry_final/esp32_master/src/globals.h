#pragma once
#include <Arduino.h>
#include "config.h"

// ── Robot modes ─────────────────────────────────────────────────────
enum RobotMode : uint8_t {
    MODE_AUTO     = 0,
    MODE_FOLLOW   = 1,
    MODE_FIND     = 2,   // sub-mode of Follow
    MODE_RECOVERY = 3
};

// ── Auto sub-states ─────────────────────────────────────────────────
enum AutoState : uint8_t {
    AUTO_IDLE,              // waiting for route from backend
    AUTO_WAIT_START,        // route received, waiting button
    AUTO_RUNNING,           // STM32 executing route
    AUTO_WAIT_RETURN_BTN,   // at destination, waiting button for return
    AUTO_WAIT_RETURN_ROUTE, // cancelled, waiting for return route from backend
    AUTO_RETURNING,         // running return route
    AUTO_COMPLETE           // returned to MED
};

// ── Route point ─────────────────────────────────────────────────────
struct RoutePoint {
    uint16_t checkpointId;
    uint8_t  action;        // 'F','L','R','B','S'
};

// ── Shared globals ──────────────────────────────────────────────────
extern volatile RobotMode   g_mode;
extern volatile AutoState   g_autoState;
extern volatile uint8_t     g_batteryPercent;
extern volatile bool        g_mqttConnected;
extern volatile bool        g_wifiConnected;

// route storage
extern RoutePoint  g_route[MAX_ROUTE_LEN];
extern uint8_t     g_routeLen;
extern uint8_t     g_routeIdx;          // current index in route

// mission info (from MQTT JSON)
extern char        g_patientName[32];
extern char        g_destination[16];
extern char        g_missionId[24];

// latest checkpoint reported by STM32
extern volatile uint16_t g_lastCheckpointId;
extern volatile bool     g_newCheckpoint;

// STM32 flags
extern volatile bool     g_stm32Obstacle;
extern volatile bool     g_stm32MissionDone;
extern volatile uint16_t g_stm32MismatchGot;
extern volatile uint16_t g_stm32MismatchExp;
extern volatile bool     g_stm32MismatchFlag;

// button events
extern volatile bool     g_btnSingleClick;
extern volatile bool     g_btnDoubleClick;
extern volatile bool     g_btnLongPress;

/** Đặt bởi MQTT topic cancel — auto_mode xử lý CMD 0x05 + return request. */
extern volatile bool     g_mqttCancel;

// ── Test lab tunables (set via MQTT from dashboard) ─────────────────
extern volatile uint16_t g_tuneSpinMs;     // turn spin duration ms
extern volatile uint16_t g_tuneBrakeMs;    // turn brake duration ms
extern volatile uint16_t g_tuneWallCm;     // SR05 wall threshold cm
extern volatile bool     g_testDashboard;  // OLED test dashboard mode
extern volatile bool     g_running;        // robot is actively executing
extern volatile bool     g_stopped;        // emergency stop flag
extern volatile bool     g_modeChangeReq;  // MQTT requested mode change → main loop handles init
