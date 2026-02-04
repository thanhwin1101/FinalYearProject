#ifndef HELPERS_H
#define HELPERS_H

#include <Arduino.h>

// URL helpers
String joinUrl(const char* base, const String& path);

// Web status
void markWebOk();
bool webConnected();

// String helpers
String truncStr(const String& s, size_t maxLen);

// Cargo switch
void updateCargoSwitch();
bool cargoHeld();

// NFC timing
void ignoreNfcFor(unsigned long ms);
bool nfcAllowed();

// Turn overlay
void showTurnOverlay(char a, unsigned long ms = 1500);

// Buzzer
void buzzerInit();
void toneOff();
void beepOnce(int ms = 80, int freq = 2200);
void beepArrivedPattern();

#endif // HELPERS_H
