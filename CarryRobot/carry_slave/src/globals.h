/*  globals.h  –  Slave ESP32 global state
 */
#pragma once
#include <Arduino.h>
#include "espnow_msg.h"

// Latest command from Master
extern volatile MasterToSlaveMsg masterCmd;
extern volatile bool             masterCmdNew;

// Outgoing message to Master
extern SlaveToMasterMsg slaveReport;

// Slave operational flags
extern volatile bool lineFollowActive;
extern volatile bool rfidActive;

// Timing
extern unsigned long lastEspnowTxMs;
extern unsigned long lastNfcReadMs;
extern unsigned long lastLineMs;
