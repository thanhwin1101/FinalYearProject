#pragma once
// ====================================================================
//  LineSensor – Native STM32 HAL 3-Eye Line Follower
// ====================================================================
#include "bsp_pins.h"

class LineSensor {
private:
    int _lastErr = 0;

public:
    LineSensor() = default;

    int readError() {
        int l = (HAL_GPIO_ReadPin(LINE_L_PORT, LINE_L_PIN) == GPIO_PIN_RESET) ? 1 : 0;
        int c = (HAL_GPIO_ReadPin(LINE_C_PORT, LINE_C_PIN) == GPIO_PIN_RESET) ? 1 : 0;
        int r = (HAL_GPIO_ReadPin(LINE_R_PORT, LINE_R_PIN) == GPIO_PIN_RESET) ? 1 : 0;
        int mask = (l << 2) | (c << 1) | r;

        switch (mask) {
            case 0b010: return  0;   // Chỉ giữa
            case 0b110: return -1;   // Trái + Giữa
            case 0b100: return -2;   // Chỉ Trái
            case 0b011: return +1;   // Giữa + Phải
            case 0b001: return +2;   // Chỉ Phải
            case 0b111: return  0;   // Ngã tư
            case 0b000:
                _lastErr = 0;
                return 0;
            default:
                return _lastErr;
        }
    }

    void step(int& outL, int& outR) {
        int err = readError();
        int d = err - _lastErr;
        _lastErr = err;

        int corr = LF_KP * err + LF_KD * d;
        outL = LF_BASE_PWM + corr;
        outR = LF_BASE_PWM - corr;

        if (outL < 0) outL = 0;
        if (outL > 900) outL = 900;
        if (outR < 0) outR = 0;
        if (outR > 900) outR = 900;
    }
};
