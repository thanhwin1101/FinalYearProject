#pragma once
#include <Arduino.h>
#include "config.h"

void relayInit();

void relayVisionOn();     // R1 – HuskyLens, servo, SR05
void relayVisionOff();
void relayLineNfcOn();    // R2 – line sensors + PN532
void relayLineNfcOff();

void relayAllOff();

// convenience presets
void relaySetAuto();      // R1 off, R2 on
void relaySetFollow();    // R1 on,  R2 off
void relaySetRecovery();  // R1 on,  R2 on

bool relayGetVision();    // true = ON (R1)
bool relayGetLineNfc();   // true = ON (R2)

// backward-compat aliases
inline void relayLineOn()  { relayLineNfcOn();  }
inline void relayLineOff() { relayLineNfcOff(); }
inline void relayNfcOn()   { relayLineNfcOn();  }
inline void relayNfcOff()  { relayLineNfcOff(); }
inline bool relayGetLine() { return relayGetLineNfc(); }
inline bool relayGetNfc()  { return relayGetLineNfc(); }
