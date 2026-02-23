#ifndef HARDWARE_H
#define HARDWARE_H

#include <Arduino.h>
#include "config.h"

void hardwareInit();
void motorsStop();
void driveForward(int pwm);
void mecanumDrive(int x, int y, int z);
void turnByAction(char a);
void applyForwardBrake(int brakePwm = PWM_BRAKE, int brakeMs = BRAKE_FORWARD_MS);

void nfcInit();
bool readNFC(uint8_t* uid, uint8_t* uidLen);
void tofInit();
bool tofReadDistance(uint16_t &dist);
float readUltrasonic(int trig, int echo);

void setHuskyLensMode(String mode);
void setServoPan(int angle);
void setServoTilt(int angle);
void listenToNano();

extern const String HOME_MED_UID;
const char* uidLookupByNodeId(const uint8_t* uid, uint8_t len);
String uidLookupByUid(const String& uidHex);
String getUidForNode(const String& nodeName);

void displayInit();
void oledDraw();
void oledDraw4(const char* l1, const char* l2, const char* l3, const char* l4);
void showTurnOverlay(char direction, unsigned long durationMs);

#endif // HARDWARE_H