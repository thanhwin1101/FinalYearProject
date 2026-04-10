#include "motor_control.h"

// ── single motor helper ─────────────────────────────────────────────
static void driveMotor(int enPin, int in1, int in2, int speed) {
    if (speed > 0) {
        digitalWrite(in1, HIGH);
        digitalWrite(in2, LOW);
    } else if (speed < 0) {
        digitalWrite(in1, LOW);
        digitalWrite(in2, HIGH);
        speed = -speed;
    } else {
        digitalWrite(in1, LOW);
        digitalWrite(in2, LOW);
    }
    analogWrite(enPin, constrain(speed, 0, 255));
}

// ──────────────────────────────────────────────────────────────────
void motorInit() {
    // L298N #1
    pinMode(L1_IN1, OUTPUT); pinMode(L1_IN2, OUTPUT); pinMode(L1_ENA, OUTPUT);
    pinMode(L1_IN3, OUTPUT); pinMode(L1_IN4, OUTPUT); pinMode(L1_ENB, OUTPUT);
    // L298N #2
    pinMode(L2_IN1, OUTPUT); pinMode(L2_IN2, OUTPUT); pinMode(L2_ENA, OUTPUT);
    pinMode(L2_IN3, OUTPUT); pinMode(L2_IN4, OUTPUT); pinMode(L2_ENB, OUTPUT);

    motorStop();
}

void motorSet(int fl, int fr, int bl, int br) {
    driveMotor(L1_ENA, L1_IN1, L1_IN2, fl);   // Front-Left
    driveMotor(L1_ENB, L1_IN3, L1_IN4, fr);   // Front-Right
    driveMotor(L2_ENA, L2_IN1, L2_IN2, bl);   // Back-Left
    driveMotor(L2_ENB, L2_IN3, L2_IN4, br);   // Back-Right
}

void motorStop() {
    motorSet(0, 0, 0, 0);
}

void motorBrake() {
    // brief reverse pulse
    motorSet(-MOTOR_BRAKE_PWM, -MOTOR_BRAKE_PWM,
             -MOTOR_BRAKE_PWM, -MOTOR_BRAKE_PWM);
    delay(MOTOR_BRAKE_MS);
    motorStop();
}
