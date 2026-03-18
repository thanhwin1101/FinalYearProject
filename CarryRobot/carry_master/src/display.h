#pragma once
#include <Arduino.h>
#include "globals.h"

void displayInit();
void displayIdle();
void displayOutbound(const char* patient, const char* nextNode);
void displayObstacle();
void displayWaitAtDest();
void displayFollow(const char* targetLabel, uint16_t distCm);
void displayFaceAuth(const char* phase, uint8_t streak, uint8_t needed);
void displayRecovery(uint8_t step);
void displayBack(const char* nextNode);
void displayWiFiSetup();
void displayPortalLaunching();
void displayConnected(const char* ip);
void displayBootChecklist(bool wifiOk, bool mqttOk, bool slaveOk, uint16_t stableLeftMs);
void displayCentered(const char* l1, const char* l2 = nullptr,
                     const char* l3 = nullptr, const char* l4 = nullptr);
