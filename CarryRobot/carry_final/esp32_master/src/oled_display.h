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
void oledFollowMode(int tagX, int tagY, int area, float wallL, float wallR);
void oledFindMode(uint8_t attempts);
void oledRecovery(const char *phase);
void oledObstacle();
void oledPortal(const char *apName, const char *ip);
void oledBatteryLow(uint8_t pct);
void oledError(const char *msg);
