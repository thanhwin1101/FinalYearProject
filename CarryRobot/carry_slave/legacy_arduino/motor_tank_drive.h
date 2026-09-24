#pragma once
// ====================================================================
//  Carry Robot – STM32 Slave – MotorTankDrive (Embedded C++ OOP)
// --------------------------------------------------------------------
//  - Điều khiển 4 bánh qua Cầu H L298N
//  - Quản lý hướng quay (IN1/IN2) và xung PWM phần cứng
//  - Chuyển toàn bộ các hàm rẽ (Turn 90, Turn 180) sang NON-BLOCKING
//    State Machine nhằm đảm bảo tính thời gian thực (Hard Real-Time)
//    cho các task FreeRTOS khác (Safety, Comms).
// ====================================================================
#include <Arduino.h>
#include <stdint.h>
#include "config.h"

enum class TurnAction : uint8_t {
    NONE = 0,
    TURN_90_LEFT,
    TURN_90_RIGHT,
    TURN_180,
    TURN_45_LEFT,
    TURN_45_RIGHT,
    BRAKE_PULSE
};

class MotorTankDrive {
private:
    int16_t _lastL = 0;
    int16_t _lastR = 0;

    // Non-blocking turn state machine
    TurnAction _currentTurn = TurnAction::NONE;
    uint32_t   _turnStartTime = 0;
    uint32_t   _turnDurationMs = 0;

    static void _sideDrive(uint8_t in1, uint8_t in2, uint8_t en, int16_t s) {
        if (s > DRIVE_MAX_PWM) s = DRIVE_MAX_PWM;
        if (s < -DRIVE_MAX_PWM) s = -DRIVE_MAX_PWM;
        
        if (s > 0) {
            digitalWrite(in1, HIGH);
            digitalWrite(in2, LOW);
            analogWrite(en, s);
        } else if (s < 0) {
            digitalWrite(in1, LOW);
            digitalWrite(in2, HIGH);
            analogWrite(en, -s);
        } else {
            digitalWrite(in1, LOW);
            digitalWrite(in2, LOW);
            analogWrite(en, 0);
        }
    }

public:
    MotorTankDrive() = default;

    void begin() {
        const uint8_t pins[] = {
            PIN_MOT_LEFT_IN1, PIN_MOT_LEFT_IN2, PIN_MOT_LEFT_EN,
            PIN_MOT_RIGHT_IN1, PIN_MOT_RIGHT_IN2, PIN_MOT_RIGHT_EN
        };
        for (uint8_t p : pins) {
            pinMode(p, OUTPUT);
        }
        stop();
    }

    // speedL / speedR trong dải [-255..255]. Dương = Tiến, Âm = Lùi
    void drive(int16_t speedL, int16_t speedR) {
        _sideDrive(PIN_MOT_LEFT_IN1, PIN_MOT_LEFT_IN2, PIN_MOT_LEFT_EN, speedL);
        _sideDrive(PIN_MOT_RIGHT_IN1, PIN_MOT_RIGHT_IN2, PIN_MOT_RIGHT_EN, speedR);
        _lastL = speedL;
        _lastR = speedR;
    }

    int16_t lastL() const { return _lastL; }
    int16_t lastR() const { return _lastR; }

    void forward(uint8_t pwm = DRIVE_CRUISE_PWM)  { drive(pwm, pwm); }
    void backward(uint8_t pwm = DRIVE_CRUISE_PWM) { drive(-pwm, -pwm); }
    void stop() { drive(0, 0); }

    // Phanh khẩn cấp lập tức
    void emergencyBrake() {
        _currentTurn = TurnAction::NONE;
        stop();
    }

    // ----------------------------------------------------------------
    // NON-BLOCKING TURN CONTROLLER
    // ----------------------------------------------------------------
    bool isTurning() const {
        return (_currentTurn != TurnAction::NONE);
    }

    TurnAction getCurrentTurn() const {
        return _currentTurn;
    }

    void startTurn(TurnAction action, uint32_t durationMs) {
        _currentTurn = action;
        _turnStartTime = millis();
        _turnDurationMs = durationMs;

        switch (action) {
            case TurnAction::TURN_90_LEFT:
            case TurnAction::TURN_45_LEFT:
            case TurnAction::TURN_180:
                drive(-DRIVE_TURN_PWM, DRIVE_TURN_PWM);
                break;
            case TurnAction::TURN_90_RIGHT:
            case TurnAction::TURN_45_RIGHT:
                drive(DRIVE_TURN_PWM, -DRIVE_TURN_PWM);
                break;
            case TurnAction::BRAKE_PULSE:
                drive(-DRIVE_BRAKE_PWM, -DRIVE_BRAKE_PWM);
                break;
            default:
                stop();
                break;
        }
    }

    void startTurn90Left()  { startTurn(TurnAction::TURN_90_LEFT, TURN_90_MS); }
    void startTurn90Right() { startTurn(TurnAction::TURN_90_RIGHT, TURN_90_MS); }
    void startTurn180()     { startTurn(TurnAction::TURN_180, TURN_180_MS); }
    void startTurn45Left()  { startTurn(TurnAction::TURN_45_LEFT, TURN_45_MS); }
    void startTurn45Right() { startTurn(TurnAction::TURN_45_RIGHT, TURN_45_MS); }
    void startBrakePulse()  { startTurn(TurnAction::BRAKE_PULSE, DRIVE_BRAKE_MS); }

    // Được gọi đều đặn trong TaskMotion (mỗi 10ms - 20ms)
    // Trả về true nếu vừa hoàn thành một chặng xoay
    bool updateTurnStateMachine(uint32_t nowMs) {
        if (_currentTurn == TurnAction::NONE) {
            return false;
        }

        if (nowMs - _turnStartTime >= _turnDurationMs) {
            stop();
            _currentTurn = TurnAction::NONE;
            return true; // Finished turn
        }
        return false;
    }
};
