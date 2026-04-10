#include "servo_control.h"
#include <ESP32Servo.h>

static Servo sX, sY;
static int   curX = SERVO_X_CENTER;
static int   curY = SERVO_Y_LEVEL;

void servoInit() {
    sX.attach(PIN_SERVO_X, 500, 2500);
    sY.attach(PIN_SERVO_Y, 500, 2500);
    sX.write(SERVO_X_CENTER);
    sY.write(SERVO_Y_LEVEL);
}

void servoSetX(int angle) {
    angle = constrain(angle, SERVO_MIN, SERVO_MAX);
    sX.write(angle);
    curX = angle;
}

void servoSetY(int angle) {
    angle = constrain(angle, SERVO_MIN, SERVO_MAX);
    sY.write(angle);
    curY = angle;
}

int servoGetX() { return curX; }
int servoGetY() { return curY; }

int servoReadXFeedback() {
    return analogRead(PIN_SERVO_X_FB);
}

float servoReadXAngle() {
    int raw = servoReadXFeedback();
    // map ADC (300–3700) → 0–180°
    return constrain(map(raw, 300, 3700, 0, 180), 0, 180);
}
