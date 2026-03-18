#pragma once
#include <Arduino.h>
#include "espnow_msg.h"

#define SL_ROUTE_MAX_LEN  30
#define SL_MISSION_STATUS_IDLE    0
#define SL_MISSION_STATUS_ONGOING 1
#define SL_MISSION_STATUS_COMPLETE 2
#define SL_MISSION_STATUS_BACK    3

void routeRunnerOnChunk(const uint8_t* data, int len);

void routeRunnerOnMissionStart();

void routeRunnerOnMissionCancel();

void routeRunnerOnStartReturn();

void routeRunnerUpdate();

bool routeRunnerMissionActive();
