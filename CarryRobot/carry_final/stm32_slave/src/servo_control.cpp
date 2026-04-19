// ====================================================================
//  carry_final  –  STM32 Slave  –  servo_control.cpp
//  Servo gimbal: Y axis only (vertical, PB4/TIM3_CH1)
//
//  PB4 is a JTAG pin by default.  servoInit() calls
//  __HAL_AFIO_REMAP_SWJ_NOJTAG() to keep SWD (ST-Link flashing works)
//  while freeing PB3, PB4, PA15 as normal GPIO/PWM.
// ====================================================================
#include "servo_control.h"
#include <Servo.h>
#include <stm32f1xx_hal.h>   // for __HAL_AFIO_REMAP_SWJ_NOJTAG

static Servo sY;

void servoInit() {
    // Disable JTAG, keep SWD — frees PB3, PB4, PA15
    __HAL_RCC_AFIO_CLK_ENABLE();
    __HAL_AFIO_REMAP_SWJ_NOJTAG();

    sY.attach(PIN_SERVO_Y);
    sY.write(SERVO_Y_LEVEL);
}

void servoSetY(int angle) {
    sY.write(constrain(angle, SERVO_MIN, SERVO_MAX));
}
