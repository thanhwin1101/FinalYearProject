#include "mecanum.h"
#include "motor_control.h"
#include "config.h"
#include "globals.h"

// ── track trạng thái để phát hiện lúc bắt đầu từ đứng yên ──────────
static bool s_wasMoving = false;

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

    bool isZero = (fl == 0 && fr == 0 && bl == 0 && br == 0);

    // kick-start khi chuyển từ đứng yên sang chuyển động
    if (!isZero && !s_wasMoving) {
        motorKickThenRun(fl, fr, bl, br);
    } else {
        motorSet(fl, fr, bl, br);
    }

    s_wasMoving = !isZero;
}

void mecanumTurnLeft90() {
    motorSet(-g_turnSpeed, g_turnSpeed,
             -g_turnSpeed, g_turnSpeed);
    delay(MOTOR_TURN_90_MS);
    motorBrake();
}

void mecanumTurnRight90() {
    motorSet(g_turnSpeed, -g_turnSpeed,
             g_turnSpeed, -g_turnSpeed);
    delay(MOTOR_TURN_90_MS);
    motorBrake();
}

void mecanumTurn180() {
    motorSet(g_turnSpeed, -g_turnSpeed,
             g_turnSpeed, -g_turnSpeed);
    delay(MOTOR_TURN_180_MS);
    motorBrake();
}
