#ifndef SENSORS_H
#define SENSORS_H

#include <Arduino.h>

// =========================================
// NFC FUNCTIONS
// =========================================
void nfcInit();
bool readNFC(uint8_t* uid, uint8_t* uidLen);

// =========================================
// TOF FUNCTIONS
// =========================================
void tofInit();
bool tofReadDistance(uint16_t &dist);

#endif
