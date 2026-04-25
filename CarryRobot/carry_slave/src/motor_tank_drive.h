#pragma once
// ====================================================================
//  Carry Robot – STM32 Slave – Single-L298N Tank Drive (4 wheels)
// --------------------------------------------------------------------
//  ONE L298N drives all 4 wheels:
//      OUT1 + OUT2  →  LEFT  pair  (FL + RL wired in parallel)
//      OUT3 + OUT4  →  RIGHT pair  (FR + RR wired in parallel)
//  Pinout comes from config.h.
// ====================================================================
#include <Arduino.h>
#include "config.h"

class MotorTankDrive {
public:
    void begin() {
        uint8_t pins[] = {
            PIN_MOT_LEFT_IN1, PIN_MOT_LEFT_IN2, PIN_MOT_LEFT_EN,
            PIN_MOT_RIGHT_IN1, PIN_MOT_RIGHT_IN2, PIN_MOT_RIGHT_EN
        };
        for (uint8_t p : pins) pinMode(p, OUTPUT);
        stop();
    }

    // speedL/speedR in range [-255..255]. Positive = forward.
    void drive(int16_t speedL, int16_t speedR) {
        _sideDrive(PIN_MOT_LEFT_IN1,  PIN_MOT_LEFT_IN2,  PIN_MOT_LEFT_EN,  speedL);
        _sideDrive(PIN_MOT_RIGHT_IN1, PIN_MOT_RIGHT_IN2, PIN_MOT_RIGHT_EN, speedR);
    }

    void forward(uint8_t pwm = DRIVE_CRUISE_PWM)  { drive( pwm,  pwm); }
    void backward(uint8_t pwm = DRIVE_CRUISE_PWM) { drive(-pwm, -pwm); }
    void stop() { drive(0, 0); }

    // Blocking tank turn (differential spin in place).
    void tankTurnLeft(uint16_t ms = TURN_180_MS / 2) {
        drive(-DRIVE_TURN_PWM,  DRIVE_TURN_PWM); delay(ms); stop();
    }
    void tankTurnRight(uint16_t ms = TURN_180_MS / 2) {
        drive( DRIVE_TURN_PWM, -DRIVE_TURN_PWM); delay(ms); stop();
    }
    void tankTurn180() {
        drive(-DRIVE_TURN_PWM, DRIVE_TURN_PWM);
        delay(TURN_180_MS);
        stop();
    }

    // Discrete junction turns (Action 'L' / 'R' at junction J4)
    void tankTurn90Left()  { drive(-DRIVE_TURN_PWM,  DRIVE_TURN_PWM); delay(TURN_90_MS); stop(); }
    void tankTurn90Right() { drive( DRIVE_TURN_PWM, -DRIVE_TURN_PWM); delay(TURN_90_MS); stop(); }

    // 45° rotational sweep used by FOLLOW_RECOVERY line-search
    void tankTurn45Left()  { drive(-DRIVE_TURN_PWM,  DRIVE_TURN_PWM); delay(TURN_45_MS); stop(); }
    void tankTurn45Right() { drive( DRIVE_TURN_PWM, -DRIVE_TURN_PWM); delay(TURN_45_MS); stop(); }

    // Brief brake (reverse pulse then stop)
    void brake() {
        drive(-DRIVE_BRAKE_PWM, -DRIVE_BRAKE_PWM);
        delay(DRIVE_BRAKE_MS);
        stop();
    }

private:
    static void _sideDrive(uint8_t in1, uint8_t in2, uint8_t en, int16_t s) {
        if (s >  DRIVE_MAX_PWM) s =  DRIVE_MAX_PWM;
        if (s < -DRIVE_MAX_PWM) s = -DRIVE_MAX_PWM;
        if (s > 0)      { digitalWrite(in1, HIGH); digitalWrite(in2, LOW);  analogWrite(en, s);  }
        else if (s < 0) { digitalWrite(in1, LOW);  digitalWrite(in2, HIGH); analogWrite(en, -s); }
        else            { digitalWrite(in1, LOW);  digitalWrite(in2, LOW);  analogWrite(en, 0);  }
    }
};
