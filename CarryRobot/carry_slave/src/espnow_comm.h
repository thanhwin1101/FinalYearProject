/*  espnow_comm.h  –  ESP-NOW slave side
 */
#pragma once
#include <Arduino.h>
#include "espnow_msg.h"

void espnowSlaveInit();
void espnowSendToMaster(const SlaveToMasterMsg &msg);
void espnowSlaveMaintainLink();
bool espnowSlaveLinked();
