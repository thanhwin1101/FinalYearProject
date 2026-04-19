#include "motor_control.h"

// ── track last commanded values for soft-stop ───────────────────────
static int s_lastFl = 0, s_lastFr = 0, s_lastBl = 0, s_lastBr = 0;

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
    s_lastFl = fl; s_lastFr = fr; s_lastBl = bl; s_lastBr = br;
    driveMotor(L1_ENA, L1_IN1, L1_IN2, -fl);   // Front-Left  (wiring reversed)
    driveMotor(L1_ENB, L1_IN3, L1_IN4, -fr);   // Front-Right (wiring reversed)
    driveMotor(L2_ENA, L2_IN1, L2_IN2, -bl);   // Back-Left   (wiring reversed)
    driveMotor(L2_ENB, L2_IN3, L2_IN4, -br);   // Back-Right  (wiring reversed)
}

void motorStop() {
    motorSet(0, 0, 0, 0);
}

// ── soft-stop: hạ PWM từ từ, bảo vệ bánh răng nhựa ─────────────────
void motorSoftStop() {
    int fl = s_lastFl, fr = s_lastFr, bl = s_lastBl, br = s_lastBr;

    while (fl != 0 || fr != 0 || bl != 0 || br != 0) {
        auto step = [](int v) -> int {
            if (v > 0) return max(0, v - MOTOR_SOFTSTOP_STEP);
            if (v < 0) return min(0, v + MOTOR_SOFTSTOP_STEP);
            return 0;
        };
        fl = step(fl); fr = step(fr); bl = step(bl); br = step(br);
        motorSet(fl, fr, bl, br);
        delay(MOTOR_SOFTSTOP_MS);
    }
}

// ── brake: soft-stop (thay thế reverse-pulse cũ) ────────────────────
void motorBrake() {
    motorSoftStop();
}

// ── kick-start: cấp PWM cao MOTOR_KICK_MS rồi xuống mức target ─────
void motorKickThenRun(int fl, int fr, int bl, int br) {
    // scale mỗi motor lên MOTOR_KICK_PWM, giữ nguyên chiều
    auto kick = [](int v) -> int {
        if (v > 0) return MOTOR_KICK_PWM;
        if (v < 0) return -MOTOR_KICK_PWM;
        return 0;
    };
    motorSet(kick(fl), kick(fr), kick(bl), kick(br));
    delay(MOTOR_KICK_MS);
    motorSet(fl, fr, bl, br);
}
