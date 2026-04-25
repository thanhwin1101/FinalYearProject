#pragma once
// ====================================================================
//  Carry Robot – STM32 Slave – HuskyLens Follow Mode
//  -------------------------------------------------
//  Y axis (Pitch) : closed-loop servo to keep tag vertically centered
//  X axis (Yaw)   : PD differential steering
//  Z axis (Dist)  : STRICT rule – Area = W*H vs 30 % of screen
//                   Area <  30%  → move forward
//                   Area >= 30%  → STOP immediately
//  Pins & constants come from config.h.
// ====================================================================
#include <Arduino.h>
#include <Servo.h>
#include "HUSKYLENS.h"
#include "config.h"
#include "motor_tank_drive.h"

class HuskyFollowPID {
public:
    void begin(HardwareSerial& huskySerial, MotorTankDrive* drive) {
        _drive      = drive;
        _serial     = &huskySerial;
        huskySerial.begin(HUSKY_BAUD);
        _ready = _husky.begin(huskySerial);
        if (_ready) _husky.writeAlgorithm(ALGORITHM_TAG_RECOGNITION);
        // Don't keep the servo powered at boot — R1 is OFF in Auto, so
        // driving PWM into an unpowered servo is wasteful. enable(true)
        // will re-attach when Follow mode starts.
        _servoPos = SERVO_Y_HOME;
        _lastErrX = 0;
    }

    // Re-handshake with the HuskyLens after a power-cycle from the relay.
    bool reinit() {
        if (!_serial) return false;
        _ready = false;
        // Re-open the port; HuskyLens may take ~500 ms to boot.
        _serial->end();
        delay(50);
        _serial->begin(HUSKY_BAUD);
        for (int i = 0; i < 3; ++i) {
            if (_husky.begin(*_serial)) {
                _husky.writeAlgorithm(ALGORITHM_TAG_RECOGNITION);
                // Servo attach is deferred to enable(true).
                _ready = true;
                return true;
            }
            delay(150);
        }
        return false;
    }

    bool ready() const { return _ready; }

    // Last-frame diagnostic counters (read by main.cpp for HB telemetry).
    bool lastReqOk()    const { return _lastReqOk; }
    bool lastLearned()  const { return _lastLearned; }
    bool lastAvail()    const { return _lastAvail; }
    int  lastBlockCnt() const { return _lastBlockCnt; }

    // When entering/leaving Follow
    void enable(bool on) {
        _enabled = on;
        // Reset tag-tracking state on every transition so a fresh follow
        // session always starts as "no tag seen yet" → no lost-tag alarm
        // until the camera actually locks on once.
        _seenAnyTag    = false;
        _lastSeenId    = -1;
        _idDirty       = false;
        _lostDirty     = false;
        _reportedLost  = false;
        _lostSince     = 0;
        if (on) {
            // Re-arm tag-recognition algorithm whenever follow re-enters.
            _husky.writeAlgorithm(ALGORITHM_TAG_RECOGNITION);
            // Defer servo attach + tracking by SERVO_Y_WARMUP_MS so that
            // the servo (and HuskyLens) settles after R1 power-up before
            // any motion. Loop() skips servo + drive until past this t.
            _warmupUntil = millis() + SERVO_Y_WARMUP_MS;
            _servoPos    = SERVO_Y_HOME;
            // Do NOT attach yet — attach happens in loop() once warmup
            // is over, which avoids an immediate twitch on entry.
        } else {
            if (_drive) _drive->stop();
            // Force servo back to neutral 90° before detaching.
            if (_servoY.attached()) {
                _servoPos = SERVO_Y_HOME;
                _servoY.write(_servoPos);
                delay(SERVO_Y_PARK_MS);
                _servoY.detach();
            }
        }
    }
    bool isEnabled() const { return _enabled; }

    // Algorithm switching used by FOLLOW_RECOVERY submode.
    void useTagAlgorithm()  { _husky.writeAlgorithm(ALGORITHM_TAG_RECOGNITION); }
    void useLineAlgorithm() { _husky.writeAlgorithm(ALGORITHM_LINE_TRACKING); }

    // Tilt the servo down to SERVO_Y_RECOVERY (45°) for line-search.
    // Called when entering FOLLOW_RECOVERY submode.
    void parkAtRecovery() {
        // Make sure the servo is attached and given enough PWM time to
        // physically slew to the commanded angle. enable(false) just
        // detached us, so we may need to re-attach here.
        if (!_servoY.attached()) _servoY.attach(PIN_SERVO_Y);
        _servoPos = SERVO_Y_RECOVERY;
        // Write a few times with short delays so cheap servos that
        // ignore the first pulse after attach still latch onto 45°.
        _servoY.write(_servoPos);
        delay(50);
        _servoY.write(_servoPos);
        delay(SERVO_Y_PARK_MS);
        _servoY.write(_servoPos);
    }

    // Returns true if the LINE_TRACKING algorithm currently sees a line arrow.
    bool seesLine() {
        if (!_husky.request() || !_husky.available()) return false;
        // Any returned arrow is a positive detection.
        return true;
    }

    // ---- tag-tracking telemetry (consumed by main.cpp) ------------
    // Spec: only enter "lost" once a tag has actually been seen first.
    // After 30 s without re-acquiring, the master alarms; reacquire
    // clears the alarm. main.cpp polls these on every loop tick.
    bool consumeIdChange(int16_t& outId) {
        if (!_idDirty) return false;
        outId    = _lastSeenId;
        _idDirty = false;
        return true;
    }
    bool consumeLostChange(bool& outLost) {
        if (!_lostDirty) return false;
        outLost     = _reportedLost;
        _lostDirty  = false;
        return true;
    }

    // Call every ~50 ms
    void loop() {
        if (!_enabled || !_drive) return;

        // Servo / drive warmup gate — hold still for 3 s after entering
        // Follow mode so the HuskyLens + servo bus stabilise.
        if (millis() < _warmupUntil) {
            _drive->stop();
            return;
        }
        // First tick after warmup: attach servo at neutral 90°.
        if (!_servoY.attached()) {
            _servoY.attach(PIN_SERVO_Y);
            _servoPos = SERVO_Y_HOME;
            _servoY.write(_servoPos);
        }

        _lastReqOk    = _husky.request();
        _lastLearned  = _lastReqOk ? _husky.isLearned() : false;
        _lastAvail    = (_lastReqOk && _lastLearned) ? _husky.available() : false;
        _lastBlockCnt = _lastAvail ? _husky.countBlocks() : 0;
        const bool ok = _lastReqOk && _lastLearned && _lastAvail;
        if (!ok) {
            // Lost the tag this frame. Per spec we only flag a lost
            // transition AFTER FOLLOW_LOST_MS of continuous loss, and
            // only if a tag had previously been seen.
            if (_seenAnyTag && !_reportedLost) {
                if (_lostSince == 0) _lostSince = millis();
                if ((millis() - _lostSince) >= FOLLOW_LOST_MS) {
                    _reportedLost = true;
                    _lostDirty    = true;
                }
            }
            _drive->stop();
            return;
        }
        // Tag visible this frame — reset the lost-debounce timer.
        _lostSince = 0;

        HUSKYLENSResult r = _husky.read();

        // Tag re-acquired — clear lost flag.
        if (_reportedLost) {
            _reportedLost = false;
            _lostDirty    = true;
        }
        // First sighting OR ID changed → tell master to refresh OLED.
        if (!_seenAnyTag || r.ID != _lastSeenId) {
            _seenAnyTag = true;
            _lastSeenId = r.ID;
            _idDirty    = true;
        }
        // --- Y-axis servo closed loop (tag-up → raise, tag-down → lower)
        // errY = yCenter - CY:  errY < 0 ↔ tag is ABOVE centre.
        //   tag ABOVE  (errY < 0) → want servo angle to INCREASE  (look up)
        //   tag BELOW  (errY > 0) → want servo angle to DECREASE  (look down)
        // delta = KP * errY  (positive when tag below) →  servoPos -= delta.
        int errY = r.yCenter - HUSKY_CY;
        if (abs(errY) > SERVO_Y_DEADBAND) {
            float deltaF = SERVO_Y_KP * (float)errY;
            // Slew-rate limit for smoother motion.
            int delta = (int)deltaF;
            if (delta >  SERVO_Y_MAX_STEP) delta =  SERVO_Y_MAX_STEP;
            if (delta < -SERVO_Y_MAX_STEP) delta = -SERVO_Y_MAX_STEP;
            // If KP*errY rounds to 0 but we're outside the deadband,
            // still nudge by 1° in the correct direction.
            if (delta == 0) delta = (errY > 0) ? 1 : -1;
#if SERVO_Y_REVERSED
            _servoPos += delta;
#else
            _servoPos -= delta;
#endif
            if (_servoPos < SERVO_Y_MIN) _servoPos = SERVO_Y_MIN;
            if (_servoPos > SERVO_Y_MAX) _servoPos = SERVO_Y_MAX;
            _servoY.write(_servoPos);
        }

        // --- Z-axis STRICT 30 % area rule -----------------------
        int32_t area    = (int32_t)r.width * (int32_t)r.height;
        int32_t pct100  = (area * 100) / HUSKY_AREA_TOTAL;

        if (pct100 >= FOLLOW_AREA_STOP_PCT) {
            _drive->stop();                // tag close enough → halt
            _lastErrX = 0;
            return;
        }

        // --- X-axis PD steering (asymmetric) --------------------
        // Spec: the wheel on the side the tag drifts toward keeps
        // cruising; only the opposite wheel gets a small boost so the
        // robot yaws back to centre without ever halting either side.
        int errX  = r.xCenter - HUSKY_CX;  // >0 = tag to the right
        int dX    = errX - _lastErrX;
        _lastErrX = errX;
        float corr = FOLLOW_X_KP * errX + FOLLOW_X_KD * dX;
        if (corr >  FOLLOW_X_MAX_BOOST) corr =  FOLLOW_X_MAX_BOOST;
        if (corr < -FOLLOW_X_MAX_BOOST) corr = -FOLLOW_X_MAX_BOOST;

        // Tag on LEFT  (errX < 0) → boost RIGHT wheel → yaw left.
        // Tag on RIGHT (errX > 0) → boost LEFT  wheel → yaw right.
        int leftBoost  = (corr > 0) ? (int)corr       : 0;
        int rightBoost = (corr < 0) ? (int)(-corr)    : 0;
        int left  = FOLLOW_CRUISE_PWM + leftBoost;
        int right = FOLLOW_CRUISE_PWM + rightBoost;
        _drive->drive(left, right);
    }

private:
    HUSKYLENS        _husky;
    Servo            _servoY;
    HardwareSerial*  _serial   = nullptr;
    MotorTankDrive*  _drive    = nullptr;
    int              _servoPos = SERVO_Y_HOME;
    int              _lastErrX = 0;
    uint32_t         _warmupUntil = 0;
    bool             _enabled  = false;
    bool             _ready    = false;
    // Tag tracking state – fed up to master via main.cpp consumers.
    int16_t          _lastSeenId   = -1;
    bool             _seenAnyTag   = false;
    bool             _idDirty      = false;
    bool             _reportedLost = false;
    bool             _lostDirty    = false;
    uint32_t         _lostSince    = 0;
    // Per-frame diagnostic snapshot
    bool             _lastReqOk    = false;
    bool             _lastLearned  = false;
    bool             _lastAvail    = false;
    int              _lastBlockCnt = 0;
};
