#ifndef HARDWARE_H
#define HARDWARE_H

#include <Arduino.h>
#include "config.h"

// =========================================
// MOTORS
// =========================================
void motorPwmInit();
void motorsStop();
void driveForward(int pwm);
void driveBackward(int pwm);
void setMotorDirLeft();
void setMotorDirRight();
void applyForwardBrake(int brakePwm = PWM_BRAKE, int brakeMs = BRAKE_FORWARD_MS);
void applyHardBrake(bool wasTurningLeft, int brakePwm = PWM_BRAKE, int brakeMs = 50);
void rotateByTime(unsigned long totalMs, bool isLeft);
void turnByAction(char a);

// =========================================
// SENSORS
// =========================================
void nfcInit();
bool readNFC(uint8_t* uid, uint8_t* uidLen);
void tofInit();
bool tofReadDistance(uint16_t &dist);

// =========================================
// UID LOOKUP
// =========================================
extern const String HOME_MED_UID;
const char* uidLookupByNodeId(const uint8_t* uid, uint8_t len);
String uidLookupByUid(const String& uidHex);
String getUidForNode(const String& nodeName);

// =========================================
// DISPLAY
// =========================================
void displayInit();
void oledDraw();
void oledDraw4(const char* l1, const char* l2, const char* l3, const char* l4);
void showTurnOverlay(char direction, unsigned long durationMs);

#endif // HARDWARE_H
