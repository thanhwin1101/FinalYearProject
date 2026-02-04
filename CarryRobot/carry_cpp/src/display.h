#ifndef DISPLAY_H
#define DISPLAY_H

#include <Arduino.h>

// =========================================
// DISPLAY FUNCTIONS
// =========================================
void displayInit();
void drawCentered(const char* line1, const char* line2 = nullptr, const char* line3 = nullptr, const char* line4 = nullptr);
void drawState(const char* stateName, const char* info = nullptr);
void drawRouteProgress(const char* phase, int idx, int total, const char* node);
void drawMissionInfo(const char* missionId, const char* dest, const char* status);
void drawWaitingCargo(const char* missionId, const char* dest);
void drawError(const char* errTitle, const char* errDetail);

#endif
