#pragma once
#include <Arduino.h>
#include "espnow_msg.h"

extern volatile MasterToSlaveMsg masterCmd;
extern volatile bool             masterCmdNew;

extern SlaveToMasterMsg slaveReport;

extern volatile bool lineFollowActive;
extern volatile bool rfidActive;

extern unsigned long lastEspnowTxMs;
extern unsigned long lastNfcReadMs;
extern unsigned long lastLineMs;
