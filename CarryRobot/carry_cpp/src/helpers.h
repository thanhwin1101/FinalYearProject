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
bool isCargoLoaded();  // Alias for cargoHeld

// NFC timing
void ignoreNfcFor(unsigned long ms);
bool nfcAllowed();
bool isNfcReady();     // Alias for nfcAllowed
void markNfcRead();    // Set default ignore time after read

// Buzzer
void buzzerInit();
void toneOff();
void beepOnce(int ms = 80, int freq = 2200);
void beepArrivedPattern();

#endif // HELPERS_H
