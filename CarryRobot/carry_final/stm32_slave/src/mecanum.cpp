#include "mecanum.h"
#include "motor_control.h"
#include "config.h"

// ── kinematics ──────────────────────────────────────────────────────
//   FL = Vy + Vx + Vr
//   FR = Vy - Vx - Vr
//   BL = Vy - Vx + Vr
//   BR = Vy + Vx - Vr
void mecanumDrive(int vx, int vy, int vr) {
    int fl = vy + vx + vr;
    int fr = vy - vx - vr;
    int bl = vy - vx + vr;
    int br = vy + vx - vr;

    // normalize
    int maxVal = max(max(abs(fl), abs(fr)), max(abs(bl), abs(br)));
    if (maxVal > 255) {
        fl = fl * 255 / maxVal;
        fr = fr * 255 / maxVal;
        bl = bl * 255 / maxVal;
        br = br * 255 / maxVal;
    }
    motorSet(fl, fr, bl, br);
}

void mecanumTurnLeft90() {
    motorSet(-MOTOR_TURN_SPEED, MOTOR_TURN_SPEED,
             -MOTOR_TURN_SPEED, MOTOR_TURN_SPEED);
    delay(MOTOR_TURN_90_MS);
    motorBrake();
}

void mecanumTurnRight90() {
    motorSet(MOTOR_TURN_SPEED, -MOTOR_TURN_SPEED,
             MOTOR_TURN_SPEED, -MOTOR_TURN_SPEED);
    delay(MOTOR_TURN_90_MS);
    motorBrake();
}

void mecanumTurn180() {
    motorSet(MOTOR_TURN_SPEED, -MOTOR_TURN_SPEED,
             MOTOR_TURN_SPEED, -MOTOR_TURN_SPEED);
    delay(MOTOR_TURN_180_MS);
    motorBrake();
}
