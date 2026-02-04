#ifndef STATE_MACHINE_H
#define STATE_MACHINE_H

#include <Arduino.h>

// =========================================
// STATE MACHINE FUNCTIONS
// =========================================
void startOutbound();
void enterWaitCargo();
void enterWaitAtDest();
void startReturn();
void enterIdle();

// =========================================
// CHECKPOINT HANDLER
// =========================================
void handleCheckpointHit(const char* nodeName);

#endif
