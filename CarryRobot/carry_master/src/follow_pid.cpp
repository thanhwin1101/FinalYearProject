#include "follow_pid.h"
#include "config.h"
#include "globals.h"
#include "servo_gimbal.h"

static float camIntegral  = 0;
static float camPrevErr   = 0;
static float anglePrevErr = 0;
static float distPrevErr  = 0;

static bool  searching    = false;
static float sweepAngle   = SERVO_X_CENTER;
static float sweepDir     = 1.0f;
static const float SWEEP_SPEED  = 60.0f;
static const float SWEEP_MIN    = 20.0f;
static const float SWEEP_MAX    = 160.0f;

static unsigned long lastSeenMs = 0;
static const unsigned long LOST_TIMEOUT_MS = 400;

void followPidReset() {
    camIntegral  = 0;
    camPrevErr   = 0;
    anglePrevErr = 0;
    distPrevErr  = 0;
    searching    = false;
    sweepAngle   = SERVO_X_CENTER;
    sweepDir     = 1.0f;
    lastSeenMs   = millis();
    gimbalLockX(true);
    servoXLocked = true;
    targetLocked = false;
}

bool followIsSearching() { return searching; }

FollowOutput followPidUpdate(int16_t targetPixelX, bool targetSeen,
                             uint16_t tofDistMm, float dt)
{
    FollowOutput out = {0, 0, 0};
    if (dt <= 0) dt = 0.05f;

    if (targetSeen) {
        lastSeenMs = millis();

        if (searching) {
            searching    = false;

        }
        targetLocked = true;

        float eCam = (float)HUSKY_CENTER_X - (float)targetPixelX;
        camIntegral += eCam * dt;
        camIntegral  = constrain(camIntegral, -CAM_I_MAX, CAM_I_MAX);
        float camDeriv = (eCam - camPrevErr) / dt;
        camPrevErr = eCam;

        float servoCmd = KP_CAM * eCam + KI_CAM * camIntegral + KD_CAM * camDeriv;

        if (servoXLocked) {

        } else {

            float newAngle = gimbalGetTargetX() + servoCmd;
            newAngle = constrain(newAngle, (float)SERVO_X_MIN, (float)SERVO_X_MAX);
            gimbalSetX(newAngle);
        }

        float thetaX  = gimbalReadXAngle();
        lastServoXAngle = thetaX;
        float eAngle  = thetaX - (float)SERVO_X_CENTER;

        float angleDeriv = (eAngle - anglePrevErr) / dt;
        anglePrevErr = eAngle;

        out.vY = KP_ANGLE * eAngle + KD_ANGLE * angleDeriv;
        out.vR = eAngle * 0.3f;

        if (!servoXLocked && fabsf(eAngle) < 5.0f) {
            gimbalLockX(true);
            servoXLocked = true;
        }

        float eDist = (float)tofDistMm - (float)FOLLOW_TARGET_MM;
        float distDeriv = (eDist - distPrevErr) / dt;
        distPrevErr = eDist;

        out.vX = KP_DIST * eDist + KD_DIST * distDeriv;

    }

    else {
        targetLocked = false;

        if (millis() - lastSeenMs > LOST_TIMEOUT_MS) {

            if (!searching) {
                searching    = true;
                servoXLocked = false;
                gimbalLockX(false);
                sweepAngle   = gimbalReadXAngle();

                sweepDir = (sweepAngle >= SERVO_X_CENTER) ? -1.0f : 1.0f;
            }

            sweepAngle += sweepDir * SWEEP_SPEED * dt;
            if (sweepAngle >= SWEEP_MAX) { sweepAngle = SWEEP_MAX; sweepDir = -1.0f; }
            if (sweepAngle <= SWEEP_MIN) { sweepAngle = SWEEP_MIN; sweepDir =  1.0f; }
            gimbalSetX(sweepAngle);

            out.vR = sweepDir * 80.0f;
        }

    }

    out.vX = constrain(out.vX, -255.0f, 255.0f);
    out.vY = constrain(out.vY, -255.0f, 255.0f);
    out.vR = constrain(out.vR, -255.0f, 255.0f);

    return out;
}
