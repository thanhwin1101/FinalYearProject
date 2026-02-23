#include "gait_generator.h"
#include "config.h"
#include "globals.h"
#include "Kinematics.h"
#include "fsr_handler.h"

Kinematics kin;
float gaitAngles[JOINT_COUNT] = {0};

float lx = 0, ly = -STAND_HEIGHT_MM, lyaw = 0, lroll = 0;
float rx = 0, ry = -STAND_HEIGHT_MM, ryaw = 0, rroll = 0;

enum WalkPhase { DOUBLE_STAND, SHIFT_LEFT, SWING_RIGHT, DROP_RIGHT, SHIFT_RIGHT, SWING_LEFT, DROP_LEFT };
WalkPhase phase = DOUBLE_STAND;
float phaseTimer = 0;

void gaitInit() {
    kin.calculateLeg(LEG_LEFT, 0, -STAND_HEIGHT_MM, 0, 0, 0);
    kin.calculateLeg(LEG_RIGHT, 0, -STAND_HEIGHT_MM, 0, 0, 0);
}

void gaitUpdate(float dt) {
    fsrUpdate();

    float t_lx = 0, t_ly = -STAND_HEIGHT_MM, t_lyaw = 0, t_lroll = 0;
    float t_rx = 0, t_ry = -STAND_HEIGHT_MM, t_ryaw = 0, t_rroll = 0;
    
    float step_len = 0, turn_l = 0, turn_r = 0;
    bool is_moving = false;

    if (currentCmd == CMD_FORWARD)       { step_len = STEP_LENGTH_MM; is_moving = true; }
    else if (currentCmd == CMD_BACKWARD) { step_len = -STEP_LENGTH_MM; is_moving = true; }
    else if (currentCmd == CMD_LEFT)     { turn_l = -TURN_ANGLE_DEG; turn_r = -TURN_ANGLE_DEG; is_moving = true; }
    else if (currentCmd == CMD_RIGHT)    { turn_l = TURN_ANGLE_DEG; turn_r = TURN_ANGLE_DEG; is_moving = true; }

    if (is_moving) {
        phaseTimer += dt * (moveSpeed / 50.0f) * 2.0f; 
        
        switch (phase) {
            case DOUBLE_STAND:
                phase = SHIFT_LEFT; phaseTimer = 0; break;

            case SHIFT_LEFT:
                t_lroll = BODY_TILT_DEG; t_rroll = BODY_TILT_DEG; // Đổ người sang trái
                t_lx = step_len / 2; t_rx = -step_len / 2;
                t_lyaw = turn_l/2; t_ryaw = turn_r/2;
                if (phaseTimer > 0.4f && footR.total < FSR_CONTACT_THRES * 1.5f) { phase = SWING_RIGHT; phaseTimer = 0; }
                break;

            case SWING_RIGHT:
                t_lroll = BODY_TILT_DEG; t_rroll = BODY_TILT_DEG;
                t_lx = -step_len / 2; t_rx = step_len;
                t_lyaw = 0; t_ryaw = turn_r; // Xoay chữ V chân phải nếu đang rẽ
                t_ry = -STAND_HEIGHT_MM + STEP_CLEARANCE_MM;
                if (phaseTimer > 0.4f) { phase = DROP_RIGHT; phaseTimer = 0; }
                break;

            case DROP_RIGHT:
                t_lroll = BODY_TILT_DEG; t_rroll = BODY_TILT_DEG;
                t_lx = -step_len / 2; t_rx = step_len;
                t_lyaw = 0; t_ryaw = turn_r;
                t_ry = -STAND_HEIGHT_MM;
                if (footR.isGrounded || phaseTimer > 0.8f) { phase = SHIFT_RIGHT; phaseTimer = 0; }
                break;

            case SHIFT_RIGHT:
                t_lroll = -BODY_TILT_DEG; t_rroll = -BODY_TILT_DEG; // Đảo trụ sang phải
                t_lx = -step_len / 2; t_rx = step_len / 2;
                t_lyaw = turn_l/2; t_ryaw = turn_r/2;
                if (phaseTimer > 0.4f && footL.total < FSR_CONTACT_THRES * 1.5f) { phase = SWING_LEFT; phaseTimer = 0; }
                break;

            case SWING_LEFT:
                t_lroll = -BODY_TILT_DEG; t_rroll = -BODY_TILT_DEG;
                t_rx = -step_len / 2; t_lx = step_len;
                t_ryaw = 0; t_lyaw = turn_l;
                t_ly = -STAND_HEIGHT_MM + STEP_CLEARANCE_MM;
                if (phaseTimer > 0.4f) { phase = DROP_LEFT; phaseTimer = 0; }
                break;

            case DROP_LEFT:
                t_lroll = -BODY_TILT_DEG; t_rroll = -BODY_TILT_DEG;
                t_rx = -step_len / 2; t_lx = step_len;
                t_ryaw = 0; t_lyaw = turn_l;
                t_ly = -STAND_HEIGHT_MM;
                if (footL.isGrounded || phaseTimer > 0.8f) { phase = SHIFT_LEFT; phaseTimer = 0; stepCount++; }
                break;
        }
    } else {
        phase = DOUBLE_STAND;
    }

    // Làm mượt (slew-rate limit) để không bị giật
    float rateXYZ = 100.0f; float rateDeg = 60.0f;
    lx = slewLimit(lx, t_lx, rateXYZ, dt); ly = slewLimit(ly, t_ly, rateXYZ, dt);
    rx = slewLimit(rx, t_rx, rateXYZ, dt); ry = slewLimit(ry, t_ry, rateXYZ, dt);
    lyaw = slewLimit(lyaw, t_lyaw, rateDeg, dt); lroll = slewLimit(lroll, t_lroll, rateDeg, dt);
    ryaw = slewLimit(ryaw, t_ryaw, rateDeg, dt); rroll = slewLimit(rroll, t_rroll, rateDeg, dt);

    kin.calculateLeg(LEG_LEFT, lx, ly, 0, lyaw, lroll);
    kin.calculateLeg(LEG_RIGHT, rx, ry, 0, ryaw, rroll);

    for(int i=0; i<JOINT_COUNT; i++) gaitAngles[i] = kin.target_angles[i];
}