/*  route_runner.h  –  Slave autonomous mission: store routes, run line+RFID, report status
 *  Master sends outbound + return routes; Slave runs them and reports ongoing/complete/back.
 */
#pragma once
#include <Arduino.h>
#include "espnow_msg.h"

#define SL_ROUTE_MAX_LEN  30
#define SL_MISSION_STATUS_IDLE    0
#define SL_MISSION_STATUS_ONGOING 1
#define SL_MISSION_STATUS_COMPLETE 2  // at destination (bed)
#define SL_MISSION_STATUS_BACK    3   // at MED

// Call when ESP-NOW receives a route chunk (len == sizeof(MasterToSlaveRouteChunk))
void routeRunnerOnChunk(const uint8_t* data, int len);

// Call when Master sends missionStart=1
void routeRunnerOnMissionStart();

// Call when Master sends missionCancel=1
void routeRunnerOnMissionCancel();

// Call when Master sends startReturn=1 (user pressed SW at WAIT_AT_DEST)
void routeRunnerOnStartReturn();

// Call every loop: run FSM, set slaveReport.missionStatus/routeIndex/routeTotal
void routeRunnerUpdate();

// True when Slave is in autonomous mission (OUTBOUND or BACK)
bool routeRunnerMissionActive();
