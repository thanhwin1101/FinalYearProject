#pragma once
// ====================================================================
//  Carry Robot – STM32 SLAVE – config.h
//  All pin/constant definitions in one place.
//  Target MCU: STM32F103C8T6 (Blue Pill)
// ====================================================================

// ---- UART to ESP32 (Serial1 = USART1: PA9 TX, PA10 RX) ------------
#define UART_ESP_BAUD       115200
#define UART_FRAME_MAX      256

// ---- HuskyLens UART (Serial3 = USART3: PB10 TX, PB11 RX) ----------
#define HUSKY_BAUD          9600

// ---- SR05 Ultrasonic ----------------------------------------------
// PA11/PA12 không dùng được (USB D-/D+, có pull-up 1.5k cứng).
// PB10/PB11 đã dùng cho USART3 HuskyLens.
// Chọn PA2/PA3 (USART2 không dùng → GPIO trống, 5V tolerant).
#define PIN_SR05_TRIG       PA2
#define PIN_SR05_ECHO       PA3
#define SR05_STOP_CM        25          // brake threshold
#define SR05_RESUME_CM      35          // resume threshold

// ====================================================================
//  SINGLE L298N driving 4 wheels (both left in parallel on one channel,
//  both right in parallel on the other channel).
//    OUT1+OUT2 → Left  pair (FL + RL)
//    OUT3+OUT4 → Right pair (FR + RR)
//
//  NOTE: PA9 / PA10 are RESERVED for USART1 (Serial1 to ESP32 AND the
//        STM32 ROM bootloader for OTA pass-through flashing). Do NOT
//        use them for GPIO here.
// ====================================================================
#define PIN_MOT_LEFT_IN1    PA0
#define PIN_MOT_LEFT_IN2    PA1
#define PIN_MOT_LEFT_EN     PA8         // ENA  (TIM1_CH1  hardware PWM)
#define PIN_MOT_RIGHT_IN1   PB12        // direction  (plain GPIO)
#define PIN_MOT_RIGHT_IN2   PB13        // direction  (plain GPIO)
#define PIN_MOT_RIGHT_EN    PB1         // ENB  (TIM3_CH4  hardware PWM)

// ---- Motor tuning --------------------------------------------------
#define DRIVE_MAX_PWM       230
#define DRIVE_CRUISE_PWM    190
#define DRIVE_TURN_PWM      180
#define DRIVE_BRAKE_PWM     180
#define DRIVE_BRAKE_MS      80
#define TURN_180_MS         1900        // calibrate on real chassis

// ---- 3-eye Line sensor (active LOW on black line) -----------------
#define PIN_LINE_L          PB8
#define PIN_LINE_C          PB9
#define PIN_LINE_R          PA4

// ---- Line-follow PD gains -----------------------------------------
#define LF_BASE_PWM         170         // Auto cruise speed (3-eye PD)
#define LF_KP               70
#define LF_KD               55

// ---- PN532 (SPI1) -------------------------------------------------
//  SPI1 fixed pins on STM32F103:  SCK=PA5  MISO=PA6  MOSI=PA7
//  Chip-select is a normal GPIO.
#define PN532_SCK           PA5
#define PN532_MISO          PA6
#define PN532_MOSI          PA7
#define PN532_SS            PB0         // CS (free pin)
#define NFC_POLL_TIMEOUT_MS 80
#define NFC_REPEAT_MS       700

// ---- Servo (Y-axis pitch) -----------------------------------------
//   Relay 1 powers  : HuskyLens + Servo Y
//   Relay 2 powers  : PN532 + 3-eye line sensor
// Preset angles (per spec):
//   FOLLOW entry    : tilt UP to 110° (look up at the tag)
//   AUTO  re-entry  : tilt DOWN to 45° first, then detach
#define PIN_SERVO_Y         PB4
#define SERVO_Y_MIN         30
#define SERVO_Y_MAX         150
#define SERVO_Y_HOME        90
#define SERVO_Y_FOLLOW      90          // forced home on Follow entry (free-track after 1st tag)
#define SERVO_Y_AUTO_PARK   90          // forced home on Follow exit
#define SERVO_Y_RECOVERY    110          // tilt down on FOLLOW_RECOVERY (look at line)
#define SERVO_Y_PARK_MS     400         // wait for travel before detach
#define SERVO_Y_WARMUP_MS   3000        // wait 3s after Follow entry before any servo motion
#define SERVO_Y_REVERSED    1           // 1 = invert servo direction (mechanical mounting)
#define FOLLOW_LOST_MS      8000        // grace period before reporting tag lost (ms)
#define SERVO_Y_KP          0.08f       // faster tag tracking (was 0.02)
#define SERVO_Y_DEADBAND    10          // tighter centering (was 20)
#define SERVO_Y_MAX_STEP    4           // up to 4° per tick (~50 ms) → ~80°/s

// ---- HuskyLens screen geometry & Follow-Mode constants ------------
#define HUSKY_SCREEN_W          320
#define HUSKY_SCREEN_H          240
#define HUSKY_CX                (HUSKY_SCREEN_W / 2)
#define HUSKY_CY                (HUSKY_SCREEN_H / 2)
#define HUSKY_AREA_TOTAL        (HUSKY_SCREEN_W * HUSKY_SCREEN_H)

// STRICT spec: area ≥ 30 % → STOP, area < 30 % → move forward
#define FOLLOW_AREA_STOP_PCT    30

#define FOLLOW_CRUISE_PWM       170         // both wheels get at least this
#define FOLLOW_X_KP             0.45f
#define FOLLOW_X_KD             0.25f
// Asymmetric steering: the inner wheel never drops below FOLLOW_CRUISE_PWM;
// only the outer wheel gets a small positive boost to re-center the tag.
#define FOLLOW_X_MAX_BOOST      45          // outer-wheel PWM delta cap

// ---- Route ---------------------------------------------------------
#define MAX_ROUTE_STEPS         15          // fixed-size, no malloc
#define MAX_ROUTE_LEN           MAX_ROUTE_STEPS  // legacy alias
#define MAX_NODE_ID_LEN         24          // chars (incl '\0') – room for "45:54:80:AB"
#define START_CHECKPOINT        "MED"

// ---- Junction-turn timings (calibrate on chassis) -----------------
#define TURN_90_MS              900         // tank 90° at DRIVE_TURN_PWM
#define TURN_45_MS              450         // tank 45° (recovery search)
#define FOLLOW_REC_APPROACH_MS  600         // creep forward toward line
