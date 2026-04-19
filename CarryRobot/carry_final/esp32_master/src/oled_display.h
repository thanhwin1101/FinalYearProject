#pragma once
#include <Arduino.h>

void oledInit();
void oledSplash();
void oledBoot(bool wifi, bool mqtt);
void oledIdle();
void oledAutoWaitStart(const char *patient, const char *dest, uint8_t totalCp);
void oledAutoRunning(uint8_t cpIdx, uint8_t totalCp, const char *dest);
void oledAutoWaitReturn();
void oledAutoReturning(uint8_t cpIdx, uint8_t totalCp);
void oledFollowMode(int tagX, int tagY, int area);
void oledFollowTagLost(uint8_t secsLeft); // countdown before recovery
void oledFollowEnter(bool ok);      // shown when entering/rejecting follow mode
void oledAutoCancel();              // cancelled, following to find checkpoint
void oledAutoMismatch();            // wrong checkpoint scanned, re-routing
void oledMissionDone();             // brief flash when back at MED
// step 1=relay switch, 2=finding CP, 3=CP found, 4=calling MED, 5=waiting route, 6=got route
void oledRecovery(uint8_t step, uint16_t cpId = 0);
void oledObstacle();
void oledPortal(const char *apName, const char *ip);
void oledBatteryLow(uint8_t pct);
void oledError(const char *msg);
void oledOtaProgress(uint8_t pct, const char *label);  // OTA progress bar
