/*  globals.h  –  Master ESP32 global state & variables
 */
#pragma once
#include <Arduino.h>
#include "espnow_msg.h"

// ======================= Robot State ==========================
enum RobotState : uint8_t {
    ST_IDLE           = 0,
    ST_OUTBOUND       = 1,
    ST_WAIT_AT_DEST   = 2,
    ST_BACK           = 3,
    ST_FOLLOW         = 4,
    ST_RECOVERY_VIS   = 5,   // Recovery step 1: Visual Docking (HuskyLens → line)
    ST_RECOVERY_BLIND = 6,   // Recovery step 2: Blind Run (line-follow + RFID)
    ST_RECOVERY_CALL  = 7,   // Recovery step 3: Call Home (wait for route from backend)
    ST_OBSTACLE       = 8,
    ST_MISSION_DELEGATED = 9 // Slave runs route autonomously; Master relays status to MQTT
};

// ======================= Externs ==============================
extern volatile RobotState  robotState;
extern volatile RobotState  stateBeforeObstacle;

// Latest data received from Slave
extern volatile SlaveToMasterMsg slaveMsg;
extern volatile bool             slaveMsgNew;

// Message to send to Slave
extern MasterToSlaveMsg masterMsg;

// Mission data
extern String missionId;
extern String patientName;
extern String destBed;
extern String currentNodeIdLive;

// Obstacle
extern volatile bool  obstacleHold;
extern volatile bool  obstacleBeepFlag;

// Follow-mode
extern bool   targetLocked;       // HuskyLens sees target
extern bool   servoXLocked;       // Servo X locked at 90°
extern float  lastServoXAngle;

// Recovery
extern uint8_t  recoveryCheckpointsHit;
extern String   recoveryCpUids[2];

// Timing helpers
extern unsigned long lastTelemetryMs;
extern unsigned long lastOledMs;
extern unsigned long lastTofMs;
extern unsigned long lastUsMs;
extern unsigned long lastHuskyMs;
extern unsigned long lastEspnowTxMs;
extern unsigned long lastObstacleBeepMs;
