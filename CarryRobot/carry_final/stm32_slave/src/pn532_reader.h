#pragma once
#include <Arduino.h>
#include "config.h"

void     nfcInit();
void     nfcReset();           // force re-init on next read (call after relay power cycle)
bool     nfcAvailable();      // true if reader is present
uint16_t nfcReadCheckpoint(); // returns checkpoint ID or 0 if none
