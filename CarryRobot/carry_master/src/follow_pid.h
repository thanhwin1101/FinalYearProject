/*  follow_pid.h  –  Cascaded PID for Follow-Person mode
 *
 *  Inner Loop:  pixel error  →  Servo X angle
 *  Outer Loop:  Servo X angle error (from 90°)  →  Mecanum strafe vY & rotate vR
 *  Distance:    ToF mm  →  forward vX
 */
#pragma once
#include <Arduino.h>

struct FollowOutput {
    float vX;       // forward / backward
    float vY;       // strafe left / right
    float vR;       // rotation
};

void followPidReset();

/* Main entry – call at HUSKY_INTERVAL rate.
 * Returns chassis velocities to send to Slave. */
FollowOutput followPidUpdate(int16_t targetPixelX, bool targetSeen,
                             uint16_t tofDistMm, float dt);

/* Lost-target sweep state */
bool followIsSearching();
