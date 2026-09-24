#pragma once
// ====================================================================
//  MotorTankDriver – Native STM32 HAL C++ OOP
// ====================================================================
#include "bsp_pins.h"
#include "bsp_tim.h"
#include <stdint.h>

enum class TurnAction : uint8_t {
    NONE = 0,
    TURN_90_LEFT,
    TURN_90_RIGHT,
    TURN_180,
    TURN_45_LEFT,
    TURN_45_RIGHT,
    BRAKE_PULSE
};

// Strongly-typed direction enum with sign multiplier (-1 / +1)
enum class TurnDir : int8_t {
    LEFT  = -1,
    RIGHT =  1
};

class MotorTankDriver {
private:
    int16_t _lastL = 0;
    int16_t _lastR = 0;

    TurnAction _currentTurn = TurnAction::NONE;
    uint32_t   _turnStartTime = 0;
    uint32_t   _turnDurationMs = 0;

    static void _sideDrive(GPIO_TypeDef* in1Port, uint16_t in1Pin,
                           GPIO_TypeDef* in2Port, uint16_t in2Pin,
                           void (*setPWM)(uint16_t), int16_t speed) {
        if (speed > DRIVE_MAX_PWM) speed = DRIVE_MAX_PWM;
        if (speed < -DRIVE_MAX_PWM) speed = -DRIVE_MAX_PWM;

        if (speed > 0) {
            HAL_GPIO_WritePin(in1Port, in1Pin, GPIO_PIN_SET);
            HAL_GPIO_WritePin(in2Port, in2Pin, GPIO_PIN_RESET);
            setPWM((uint16_t)speed);
        } else if (speed < 0) {
            HAL_GPIO_WritePin(in1Port, in1Pin, GPIO_PIN_RESET);
            HAL_GPIO_WritePin(in2Port, in2Pin, GPIO_PIN_SET);
            setPWM((uint16_t)(-speed));
        } else {
            HAL_GPIO_WritePin(in1Port, in1Pin, GPIO_PIN_RESET);
            HAL_GPIO_WritePin(in2Port, in2Pin, GPIO_PIN_RESET);
            setPWM(0);
        }
    }

public:
    MotorTankDriver() = default;

    void init() {
        stop();
    }

    // speedL/speedR: [-1000..1000]
    void drive(int16_t speedL, int16_t speedR) {
        _sideDrive(MOT_L_IN1_PORT, MOT_L_IN1_PIN, MOT_L_IN2_PORT, MOT_L_IN2_PIN,
                   BSP_Motor_SetPWM_Left, speedL);
        _sideDrive(MOT_R_IN1_PORT, MOT_R_IN1_PIN, MOT_R_IN2_PORT, MOT_R_IN2_PIN,
                   BSP_Motor_SetPWM_Right, speedR);
        _lastL = speedL;
        _lastR = speedR;
    }

    int16_t lastL() const { return _lastL; }
    int16_t lastR() const { return _lastR; }

    void forward(uint16_t pwm = DRIVE_CRUISE_PWM)  { drive(pwm, pwm); }
    void backward(uint16_t pwm = DRIVE_CRUISE_PWM) { drive(-pwm, -pwm); }
    void stop() { drive(0, 0); }

    void emergencyBrake() {
        _currentTurn = TurnAction::NONE;
        stop();
    }

    bool isTurning() const {
        return (_currentTurn != TurnAction::NONE);
    }

    TurnAction getCurrentTurn() const {
        return _currentTurn;
    }

    // ====================================================================
    // Compile-time Polymorphism (Function Overloading) & Abstraction
    // ====================================================================

    // Overload 1: Turn by Direction and Duration (ms) - Core sign-flipped kinematics
    void turn(TurnDir dir, uint32_t durationMs) {
        int8_t sign = static_cast<int8_t>(dir);
        _currentTurn = (dir == TurnDir::LEFT) ? TurnAction::TURN_90_LEFT : TurnAction::TURN_90_RIGHT;
        _turnStartTime = HAL_GetTick();
        _turnDurationMs = durationMs;

        // Symmetric Differential Kinematics:
        // Left (sign = -1):  drive(-DRIVE_TURN_PWM, +DRIVE_TURN_PWM)
        // Right (sign = +1): drive(+DRIVE_TURN_PWM, -DRIVE_TURN_PWM)
        drive(sign * DRIVE_TURN_PWM, -sign * DRIVE_TURN_PWM);
    }

    // Overload 2: Turn by Direction and Geometric Angle (degrees)
    void turn(TurnDir dir, float angleDeg) {
        constexpr float MS_PER_DEG = static_cast<float>(TURN_90_MS) / 90.0f;
        uint32_t durationMs = static_cast<uint32_t>(angleDeg * MS_PER_DEG);
        turn(dir, durationMs);
    }

    // Overload 3: Turn by instantaneous signed angular rate (ROS Twist format)
    // turnRate < 0: Counter-clockwise (Left), turnRate > 0: Clockwise (Right)
    void turn(int16_t turnRate) {
        drive(turnRate, -turnRate);
    }

    // Legacy / FSM Compatibility Layer
    void startTurn(TurnAction action, uint32_t durationMs) {
        _currentTurn = action;
        _turnStartTime = HAL_GetTick();
        _turnDurationMs = durationMs;

        if (action == TurnAction::BRAKE_PULSE) {
            drive(-DRIVE_BRAKE_PWM, -DRIVE_BRAKE_PWM);
        } else {
            // Sign determination: RIGHT = +1, LEFT / 180 = -1
            int8_t sign = (action == TurnAction::TURN_90_RIGHT || action == TurnAction::TURN_45_RIGHT) ? 1 : -1;
            drive(sign * DRIVE_TURN_PWM, -sign * DRIVE_TURN_PWM);
        }
    }

    void startTurn90Left()  { turn(TurnDir::LEFT, 90.0f); }
    void startTurn90Right() { turn(TurnDir::RIGHT, 90.0f); }
    void startTurn180()     { turn(TurnDir::LEFT, 180.0f); }
    void startTurn45Left()  { turn(TurnDir::LEFT, 45.0f); }
    void startTurn45Right() { turn(TurnDir::RIGHT, 45.0f); }
    void startBrakePulse()  { startTurn(TurnAction::BRAKE_PULSE, DRIVE_BRAKE_MS); }

    bool updateTurnStateMachine(uint32_t nowMs) {
        if (_currentTurn == TurnAction::NONE) {
            return false;
        }
        if (nowMs - _turnStartTime >= _turnDurationMs) {
            stop();
            _currentTurn = TurnAction::NONE;
            return true; // Finished
        }
        return false;
    }
};
