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
#define SR05_STOP_CM        20          // brake threshold (lowered: noise floor ~27 cm)
#define SR05_RESUME_CM      40          // resume threshold (above noise floor)

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
// PWM moved OFF PA8: TIM1_CH1 was 1 pin away from SPI1 MOSI/MISO and
// coupled noise into PN532 reads. PB6 = TIM4_CH1, fully isolated from
// SPI1 (port A) and from the PN532 CS line.
#define PIN_MOT_LEFT_EN     PB6         // ENA  (TIM4_CH1  hardware PWM)
// Right side IN1/IN2 swapped (PB13/PB12 instead of PB12/PB13) so that
// "forward" PWM in the firmware actually drives the right wheels
// forward. Without this, the right pair was wired reversed and the
// robot would spin clockwise on a "drive(170,170)" command.
#define PIN_MOT_RIGHT_IN1   PB13        // direction  (plain GPIO) [swapped]
#define PIN_MOT_RIGHT_IN2   PB12        // direction  (plain GPIO) [swapped]
// PWM moved OFF PB1: it was the immediate neighbour of PB0 (PN532 CS)
// and every PWM edge glitched the CS line, killing SPI transactions.
// PB7 = TIM4_CH2, far from CS PB5 and from SPI1 lines.
#define PIN_MOT_RIGHT_EN    PB7         // ENB  (TIM4_CH2  hardware PWM)

// ---- Motor tuning --------------------------------------------------
#define DRIVE_MAX_PWM       255
#define DRIVE_CRUISE_PWM    190
#define DRIVE_TURN_PWM      170
#define DRIVE_BRAKE_PWM     180
#define DRIVE_BRAKE_MS      80
#define TURN_180_MS         1900        // calibrate on real chassis

// ---- 3-eye Line sensor (active LOW on black line) -----------------
#define PIN_LINE_L          PB8
#define PIN_LINE_C          PB9
#define PIN_LINE_R          PA4

// ---- Line-follow PD gains -----------------------------------------
#define LF_BASE_PWM         100         // Auto cruise speed (3-eye PD) - slow enough for PN532 to read tags reliably
#define LF_KP               70
#define LF_KD               55

// ---- PN532 (SPI1) -------------------------------------------------
//  SPI1 fixed pins on STM32F103:  SCK=PA5  MISO=PA6  MOSI=PA7
//  Chip-select is a normal GPIO.
#define PN532_SCK           PA5
#define PN532_MISO          PA6
#define PN532_MOSI          PA7
// CS pin history:
//   PB0  – sat next to PB1 (motor-right PWM) → glitched every edge.
//   PB5  – sat next to PB6 (motor-left PWM after the move)  → same EMI.
//   PB14 – far from PB6/PB7 PWM and from SPI1 (port A). No timer
//          neighbour. Confirmed clean.
#define PN532_SS            PB14        // CS – isolated from motor PWM
#define NFC_POLL_TIMEOUT_MS 40
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
// Servo-Y closed loop: proportional + derivative + slew limit.
// Oscillation cause = high KP and no D-term → overshoot above/below
// centre. Tuned values:
//   - smaller KP                        → less overshoot
//   - larger deadband (~25 px)          → ignore tiny jitter near centre
//   - small KD                          → damp the swing
//   - tighter max step + min update gap → enforce slew rate
#define SERVO_Y_KP          0.04f
#define SERVO_Y_KD          0.05f
#define SERVO_Y_DEADBAND    25
#define SERVO_Y_MAX_STEP    2           // ~2°/tick → smooth ramp
#define SERVO_Y_TICK_MS     40          // PID period (ms)

// ---- HuskyLens screen geometry & Follow-Mode constants ------------
#define HUSKY_SCREEN_W          320
#define HUSKY_SCREEN_H          240
#define HUSKY_CX                (HUSKY_SCREEN_W / 2)
#define HUSKY_CY                (HUSKY_SCREEN_H / 2)
#define HUSKY_AREA_TOTAL        (HUSKY_SCREEN_W * HUSKY_SCREEN_H)

// STRICT spec: area ≥ 30 % → STOP, area < 30 % → move forward
#define FOLLOW_AREA_STOP_PCT    30

// Speed-by-area: cruise PWM scales linearly with the tag’s on-screen
// area percentage. Tag tiny (far)  → FOLLOW_PWM_MAX (sprint to catch up).
// Tag near STOP threshold        → FOLLOW_PWM_MIN (gentle approach).
#define FOLLOW_PWM_MIN          100
#define FOLLOW_PWM_MAX          200
#define FOLLOW_CRUISE_PWM       FOLLOW_PWM_MIN  // legacy floor
#define FOLLOW_X_KP             0.90f
#define FOLLOW_X_KD             0.40f
// Asymmetric steering: the inner wheel never drops below FOLLOW_CRUISE_PWM;
// only the outer wheel gets a small positive boost to re-center the tag.
#define FOLLOW_X_MAX_BOOST      90          // outer-wheel PWM delta cap
// Only follow tags with learned ID strictly greater than 1.
#define FOLLOW_MIN_ID           1

// ---- Route ---------------------------------------------------------
#define MAX_ROUTE_STEPS         15          // fixed-size, no malloc
#define MAX_ROUTE_LEN           MAX_ROUTE_STEPS  // legacy alias
#define MAX_NODE_ID_LEN         24          // chars (incl '\0') – room for "45:54:80:AB"
#define START_CHECKPOINT        "MED"

// ---- Junction-turn timings (calibrate on chassis) -----------------
#define TURN_90_MS              900         // tank 90° at DRIVE_TURN_PWM
#define TURN_45_MS              450         // tank 45° (recovery search)
#define FOLLOW_REC_APPROACH_MS  600         // creep forward toward line
