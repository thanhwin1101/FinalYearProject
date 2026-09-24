#pragma once
// ====================================================================
//  UltrasonicSr05 – Native STM32 HAL Ultrasonic Driver (Zero-Blocking)
// ====================================================================
#include "bsp_pins.h"

class UltrasonicSr05 {
public:
    static void delayUs(uint32_t us) {
        // CPU 100MHz: 1µs = 100 cycles, ~25 vòng lặp NOP
        volatile uint32_t count = us * 25;
        while (count--) {
            __NOP();
        }
    }

    static long readDistanceCm() {
        // 1. Bắn xung kích hoạt Trigger 10µs
        HAL_GPIO_WritePin(SR05_TRIG_PORT, SR05_TRIG_PIN, GPIO_PIN_RESET);
        delayUs(2);
        HAL_GPIO_WritePin(SR05_TRIG_PORT, SR05_TRIG_PIN, GPIO_PIN_SET);
        delayUs(10);
        HAL_GPIO_WritePin(SR05_TRIG_PORT, SR05_TRIG_PIN, GPIO_PIN_RESET);

        // 2. Chờ chân Echo lên mức HIGH (Timeout ~3000µs ~ 50cm)
        volatile uint32_t timeout = 3000 * 25;
        while (HAL_GPIO_ReadPin(SR05_ECHO_PORT, SR05_ECHO_PIN) == GPIO_PIN_RESET) {
            if (--timeout == 0) return 999;
        }

        // 3. Đo độ rộng xung Echo mức HIGH
        uint32_t pulseCycles = 0;
        timeout = 3000 * 25;
        while (HAL_GPIO_ReadPin(SR05_ECHO_PORT, SR05_ECHO_PIN) == GPIO_PIN_SET) {
            pulseCycles++;
            if (--timeout == 0) break;
        }

        uint32_t us = pulseCycles / 25;
        if (us == 0) return 999;
        long cm = us / 58;
        if (cm < 2 || cm > 400) return 999;
        return cm;
    }
};
