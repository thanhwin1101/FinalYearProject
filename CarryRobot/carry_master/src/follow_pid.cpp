/*  follow_pid.cpp  –  Cascaded PID for Follow-Person mode
 *
 *  Vòng 1 (Inner):  e_cam  = HUSKY_CENTER_X − targetX
 *                    → PID → Servo X angle
 *  Vòng 2 (Outer):  e_angle = θ_servo − 90°
 *                    → PD → vY (strafe)  +  vR (rotation bù)
 *  Distance:         e_dist = tofMm − FOLLOW_TARGET_MM
 *                    → PD → vX
 */
#include "follow_pid.h"
#include "config.h"
#include "globals.h"
#include "servo_gimbal.h"

/* ─── PID state ─── */
static float camIntegral  = 0;
static float camPrevErr   = 0;
static float anglePrevErr = 0;
static float distPrevErr  = 0;

/* ─── Lost-target search ─── */
static bool  searching    = false;
static float sweepAngle   = SERVO_X_CENTER;
static float sweepDir     = 1.0f;       // +1 or −1
static const float SWEEP_SPEED  = 60.0f;  // deg/s
static const float SWEEP_MIN    = 20.0f;
static const float SWEEP_MAX    = 160.0f;

// How long without target before we start searching
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

    /* ============================================================
     *  A) Target visible – normal tracking
     * ============================================================ */
    if (targetSeen) {
        lastSeenMs = millis();

        // If we were searching, re-lock
        if (searching) {
            searching    = false;
            // Don't snap servo immediately – let outer loop bring chassis around
        }
        targetLocked = true;

        /* ── Inner loop: Camera pixel → Servo X ── */
        float eCam = (float)HUSKY_CENTER_X - (float)targetPixelX;
        camIntegral += eCam * dt;
        camIntegral  = constrain(camIntegral, -CAM_I_MAX, CAM_I_MAX);
        float camDeriv = (eCam - camPrevErr) / dt;
        camPrevErr = eCam;

        float servoCmd = KP_CAM * eCam + KI_CAM * camIntegral + KD_CAM * camDeriv;

        if (servoXLocked) {
            // Servo X is locked at 90° – camera centring is handled by chassis strafe only
            // Small pixel offset → pure strafe (bypass servo)
            // Large pixel offset could still unlock … but keep simple
        } else {
            // Servo X free – adjust servo to keep target in centre
            float newAngle = gimbalGetTargetX() + servoCmd;
            newAngle = constrain(newAngle, (float)SERVO_X_MIN, (float)SERVO_X_MAX);
            gimbalSetX(newAngle);
        }

        /* ── Outer loop: Servo X angle → chassis strafe + rotation ── */
        float thetaX  = gimbalReadXAngle();
        lastServoXAngle = thetaX;
        float eAngle  = thetaX - (float)SERVO_X_CENTER;   // positive = target is to the right

        float angleDeriv = (eAngle - anglePrevErr) / dt;
        anglePrevErr = eAngle;

        out.vY = KP_ANGLE * eAngle + KD_ANGLE * angleDeriv;
        out.vR = eAngle * 0.3f;    // gentle rotation to realign chassis

        // If servo is close to centre, lock it
        if (!servoXLocked && fabsf(eAngle) < 5.0f) {
            gimbalLockX(true);
            servoXLocked = true;
        }

        /* ── Distance loop: ToF → forward/backward ── */
        float eDist = (float)tofDistMm - (float)FOLLOW_TARGET_MM;
        float distDeriv = (eDist - distPrevErr) / dt;
        distPrevErr = eDist;

        out.vX = KP_DIST * eDist + KD_DIST * distDeriv;

    }
    /* ============================================================
     *  B) Target lost – search
     * ============================================================ */
    else {
        targetLocked = false;

        if (millis() - lastSeenMs > LOST_TIMEOUT_MS) {
            // Unlock servo X and sweep
            if (!searching) {
                searching    = true;
                servoXLocked = false;
                gimbalLockX(false);
                sweepAngle   = gimbalReadXAngle();
                // Start sweeping in direction target was last seen
                sweepDir = (sweepAngle >= SERVO_X_CENTER) ? -1.0f : 1.0f;
            }

            // Sweep servo X
            sweepAngle += sweepDir * SWEEP_SPEED * dt;
            if (sweepAngle >= SWEEP_MAX) { sweepAngle = SWEEP_MAX; sweepDir = -1.0f; }
            if (sweepAngle <= SWEEP_MIN) { sweepAngle = SWEEP_MIN; sweepDir =  1.0f; }
            gimbalSetX(sweepAngle);

            // Rotate chassis in sweep direction so the whole body turns
            out.vR = sweepDir * 80.0f;
        }
        // else: brief loss – hold position
    }

    // Clamp outputs
    out.vX = constrain(out.vX, -255.0f, 255.0f);
    out.vY = constrain(out.vY, -255.0f, 255.0f);
    out.vR = constrain(out.vR, -255.0f, 255.0f);

    return out;
}
