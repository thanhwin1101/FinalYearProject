/*  mecanum.cpp  –  4-wheel Mecanum drive via 2× L298N
 *
 *  Inverse kinematics:
 *    FL = vX + vY + vR
 *    FR = vX - vY - vR
 *    BL = vX - vY + vR
 *    BR = vX + vY - vR
 */
#include "mecanum.h"
#include "config.h"

/* ─── Motor pin arrays for cleaner code ─── */
struct MotorPins {
    uint8_t en;     // PWM enable pin
    uint8_t in1;    // Direction pin A
    uint8_t in2;    // Direction pin B
    uint8_t ch;     // LEDC channel
};

static const MotorPins FL = {L1_ENA, L1_IN1, L1_IN2, CH_FL};
static const MotorPins BL = {L1_ENB, L1_IN3, L1_IN4, CH_BL};
static const MotorPins FR = {L2_ENA, L2_IN1, L2_IN2, CH_FR};
static const MotorPins BR_MOTOR = {L2_ENB, L2_IN3, L2_IN4, CH_BR};

static const MotorPins motors[4] = {FL, BL, FR, BR_MOTOR};

/* ─── Apply speed to a single motor ─── */
static void setMotor(const MotorPins &m, float speed) {
    int16_t spd = (int16_t)constrain(speed, -255.0f, 255.0f);
    if (spd > 0) {
        digitalWrite(m.in1, HIGH);
        digitalWrite(m.in2, LOW);
    } else if (spd < 0) {
        digitalWrite(m.in1, LOW);
        digitalWrite(m.in2, HIGH);
        spd = -spd;
    } else {
        digitalWrite(m.in1, LOW);
        digitalWrite(m.in2, LOW);
    }
    ledcWrite(m.ch, (uint8_t)spd);
}

/* ─── Init ─── */
void mecanumInit() {
    for (int i = 0; i < 4; i++) {
        pinMode(motors[i].in1, OUTPUT);
        pinMode(motors[i].in2, OUTPUT);
        ledcSetup(motors[i].ch, MOTOR_PWM_FREQ, MOTOR_PWM_RES);
        ledcAttachPin(motors[i].en, motors[i].ch);
        ledcWrite(motors[i].ch, 0);
    }
    mecanumStop();
    Serial.println(F("[MECANUM] 4 motors init OK"));
}

/* ─── Drive ─── */
void mecanumDrive(float vX, float vY, float vR) {
    float fl = vX + vY + vR;
    float fr = vX - vY - vR;
    float bl = vX - vY + vR;
    float br = vX + vY - vR;

    // Normalise if any motor exceeds 255
    float maxVal = max(max(fabsf(fl), fabsf(fr)), max(fabsf(bl), fabsf(br)));
    if (maxVal > 255.0f) {
        float scale = 255.0f / maxVal;
        fl *= scale; fr *= scale; bl *= scale; br *= scale;
    }

    setMotor(FL, fl);
    setMotor(FR, fr);
    setMotor(BL, bl);
    setMotor(BR_MOTOR, br);
}

void mecanumStop() {
    for (int i = 0; i < 4; i++) {
        digitalWrite(motors[i].in1, LOW);
        digitalWrite(motors[i].in2, LOW);
        ledcWrite(motors[i].ch, 0);
    }
}

void mecanumHardBrake(uint8_t pwm, uint16_t brakeMs) {
    for (int i = 0; i < 4; i++) {
        digitalWrite(motors[i].in1, HIGH);
        digitalWrite(motors[i].in2, HIGH);
        ledcWrite(motors[i].ch, pwm);
    }
    delay(brakeMs);
    mecanumStop();
}

/* ─── Timed turns ─── */
void mecanumTurnLeft90() {
    mecanumHardBrake();
    mecanumDrive(0, 0, -TURN_PWM);     // CCW
    delay(TURN_90_MS);
    mecanumStop();
}

void mecanumTurnRight90() {
    mecanumHardBrake();
    mecanumDrive(0, 0, TURN_PWM);      // CW
    delay(TURN_90_MS);
    mecanumStop();
}

void mecanumUturn() {
    mecanumHardBrake();
    mecanumDrive(0, 0, TURN_PWM);      // CW 180°
    delay(TURN_180_MS);
    mecanumStop();
}
