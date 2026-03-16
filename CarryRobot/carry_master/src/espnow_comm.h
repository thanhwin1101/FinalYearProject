/*  espnow_comm.h  –  ESP-NOW master side
 */
#pragma once
#include <Arduino.h>
#include "espnow_msg.h"

void espnowMasterInit();
void espnowSendToSlave(const MasterToSlaveMsg &msg);
void espnowSendRouteChunk(const MasterToSlaveRouteChunk &chunk);
bool espnowSlaveConnected();
void espnowMasterMaintainLink();
