#ifndef STATE_MACHINE_H
#define STATE_MACHINE_H

#include <Arduino.h>

// =========================================
// STATE MACHINE FUNCTIONS
// =========================================
void startOutbound();
void enterWaitAtDest();
void startReturn(const char* note, bool doUturn);
void goIdleReset();

// =========================================
// CHECKPOINT HANDLER
// =========================================
void handleCheckpointHit(const String& uid);

#endif
