#pragma once
#include <Arduino.h>
#include "espnow_msg.h"
// FreeRTOS primitives (ESP32 Arduino)
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

enum RobotState : uint8_t {
    ST_IDLE           = 0,
    ST_OUTBOUND       = 1,
    ST_WAIT_AT_DEST   = 2,
    ST_BACK           = 3,
    ST_FOLLOW         = 4,
    ST_RECOVERY_VIS   = 5,
    ST_RECOVERY_BLIND = 6,
    ST_RECOVERY_CALL  = 7,
    ST_OBSTACLE       = 8,
    ST_MISSION_DELEGATED = 9
};

extern volatile RobotState  robotState;
extern volatile RobotState  stateBeforeObstacle;

extern volatile SlaveToMasterMsg slaveMsg;
extern volatile bool             slaveMsgNew;

extern MasterToSlaveMsg masterMsg;

extern String missionId;
extern String patientName;
extern String destBed;
extern String currentNodeIdLive;

extern volatile bool  obstacleHold;
extern volatile bool  obstacleBeepFlag;

extern bool   targetLocked;
extern bool   servoXLocked;
extern float  lastServoXAngle;

extern uint8_t  recoveryCheckpointsHit;
extern String   recoveryCpUids[2];

extern unsigned long lastTelemetryMs;
extern unsigned long lastOledMs;
extern unsigned long lastTofMs;
extern unsigned long lastUsMs;
extern unsigned long lastHuskyMs;
extern unsigned long lastEspnowTxMs;
extern unsigned long lastObstacleBeepMs;

// ======================= FreeRTOS shared resources =======================
// Protect I2C bus (OLED SSD1306 + VL53L0X share GPIO21/22)
extern SemaphoreHandle_t g_i2cMutex;

// Sensor cache updated by Task_Sensors (50ms)
extern volatile bool     g_tofValid;
extern volatile uint16_t g_tofMm;
extern volatile long     g_usLeftMm;
extern volatile long     g_usRightMm;
