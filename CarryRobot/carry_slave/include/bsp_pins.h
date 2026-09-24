#pragma once
// ====================================================================
//  BSP Pins – STM32F411CE BlackPill Pinout Definitions (Pure STM32 HAL)
// ====================================================================
#include "stm32f4xx_hal.h"

// LED trên mạch
#define LED_PIN                     GPIO_PIN_13
#define LED_PORT                    GPIOC

// Động cơ L298N
#define MOT_L_IN1_PIN               GPIO_PIN_0
#define MOT_L_IN1_PORT              GPIOA
#define MOT_L_IN2_PIN               GPIO_PIN_1
#define MOT_L_IN2_PORT              GPIOA
#define MOT_L_PWM_PIN               GPIO_PIN_6
#define MOT_L_PWM_PORT              GPIOB

#define MOT_R_IN1_PIN               GPIO_PIN_13
#define MOT_R_IN1_PORT              GPIOB
#define MOT_R_IN2_PIN               GPIO_PIN_12
#define MOT_R_IN2_PORT              GPIOB
#define MOT_R_PWM_PIN               GPIO_PIN_7
#define MOT_R_PWM_PORT              GPIOB

// 3 Mắt Dò Line
#define LINE_L_PIN                  GPIO_PIN_8
#define LINE_L_PORT                 GPIOB
#define LINE_C_PIN                  GPIO_PIN_9
#define LINE_C_PORT                 GPIOB
#define LINE_R_PIN                  GPIO_PIN_4
#define LINE_R_PORT                 GPIOA

// Cảm biến Siêu Âm SR05
#define SR05_TRIG_PIN               GPIO_PIN_2
#define SR05_TRIG_PORT              GPIOA
#define SR05_ECHO_PIN               GPIO_PIN_3
#define SR05_ECHO_PORT              GPIOA

// RFID PN532 (SPI1)
#define PN532_CS_PIN                GPIO_PIN_14
#define PN532_CS_PORT               GPIOB

// Tốc độ PWM và Thông số điều khiển
#define DRIVE_MAX_PWM               1000
#define DRIVE_CRUISE_PWM            700
#define DRIVE_TURN_PWM              600
#define DRIVE_BRAKE_PWM             650
#define DRIVE_BRAKE_MS              80
#define TURN_90_MS                  900
#define TURN_180_MS                 1800
#define TURN_45_MS                  450

#define LF_BASE_PWM                 400
#define LF_KP                       250
#define LF_KD                       180

#define SR05_STOP_CM                20
#define SR05_RESUME_CM              40
#define MAX_NODE_ID_LEN             24
#define MAX_ROUTE_STEPS             15
#define START_CHECKPOINT            "MED"
