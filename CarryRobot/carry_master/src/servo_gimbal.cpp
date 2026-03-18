#include "servo_gimbal.h"
#include "config.h"
#include <ESP32Servo.h>

static Servo srvX;
static Servo srvY;
static float targetX = SERVO_X_CENTER;
static float targetY = SERVO_Y_LEVEL;
static bool  xLocked = true;

void gimbalInit() {
    srvX.attach(SERVO_X_PIN, 500, 2500);
    srvY.attach(SERVO_Y_PIN, 500, 2500);
    srvX.write(SERVO_X_CENTER);
    srvY.write(SERVO_Y_LEVEL);
    Serial.println(F("[GIMBAL] Servos attached"));
}

void gimbalSetX(float angleDeg) {
    angleDeg = constrain(angleDeg, (float)SERVO_X_MIN, (float)SERVO_X_MAX);
    targetX  = angleDeg;
    srvX.write((int)angleDeg);
}

void gimbalSetY(float angleDeg) {
    angleDeg = constrain(angleDeg, (float)SERVO_Y_MIN, (float)SERVO_Y_MAX);
    targetY  = angleDeg;
    srvY.write((int)angleDeg);
}

float gimbalReadXAngle() {
    int raw = analogRead(SERVO_X_FB_PIN);
    float angle = (float)(raw - SERVO_FB_ADC_MIN) / (float)(SERVO_FB_ADC_MAX - SERVO_FB_ADC_MIN) * 180.0f;
    return constrain(angle, 0.0f, 180.0f);
}

void gimbalLockX(bool lock) {
    xLocked = lock;
    if (lock) gimbalSetX(SERVO_X_CENTER);
}

float gimbalGetTargetX() { return targetX; }
float gimbalGetTargetY() { return targetY; }
