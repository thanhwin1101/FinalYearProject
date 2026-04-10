#pragma once
// ====================================================================
//  carry_final  –  STM32 Slave  –  Pin & Constant Configuration
// ====================================================================

// ── UART to ESP32 (USART2) ─────────────────────────────────────────
#define PIN_UART_TX         PA2
#define PIN_UART_RX         PA3
#define ESP_BAUD            115200

// ── L298N #1   (Front-Left & Front-Right motors) ───────────────────
#define L1_IN1              PA0
#define L1_IN2              PA1
#define L1_ENA              PA8       // TIM1_CH1  PWM
#define L1_IN3              PA9
#define L1_IN4              PA10
#define L1_ENB              PB0       // TIM3_CH3  PWM

// ── L298N #2   (Back-Left & Back-Right motors) ─────────────────────
//  NOTE: PC13 is open-drain on Blue Pill (max 3 mA, on-board LED).
//        PC14 is the LSE oscillator pin – no hardware timer.
//        PB14 has no direct timer channel on F103C8.
//        Use analogWrite(); STM32duino may fall back to software PWM.
#define L2_IN1              PB12
#define L2_IN2              PB13
#define L2_ENA              PB14      // (software) PWM
#define L2_IN3              PB15
#define L2_IN4              PC13      // limited drive – see note
#define L2_ENB              PC14      // (software) PWM – see note

// ── PN532  NFC  (SPI1) ─────────────────────────────────────────────
#define PN532_SCK           PA5
#define PN532_MISO          PA6
#define PN532_MOSI          PA7
#define PN532_SS            PB1

// ── Line Sensor  (3 eyes, active LOW) ──────────────────────────────
#define LINE_S1             PB8       // left
#define LINE_S2             PB9       // center
#define LINE_S3             PA4       // right

// ── VL53L0X  ToF  (I2C1) ───────────────────────────────────────────
#define USE_TOF             1         // 1 = VL53L0X enabled
#define TOF_SDA             PB7
#define TOF_SCL             PB6
#define TOF_STOP_MM         200       // obstacle stop   (≤ 20 cm)
#define TOF_RESUME_MM       300       // resume distance (≥ 30 cm)

// ── Motor parameters ────────────────────────────────────────────────
#define PWM_FREQ            20000     // 20 kHz
#define PWM_RES             8         // 8-bit
#define MOTOR_RUN_SPEED     200       // 0-255
#define MOTOR_TURN_SPEED    180
#define MOTOR_TURN_90_MS    950       // measured: ~950 ms for 90°
#define MOTOR_TURN_180_MS   1900
#define MOTOR_BRAKE_PWM     150
#define MOTOR_BRAKE_MS      80

// ── Line-follower PID ───────────────────────────────────────────────
#define LF_KP               0.35f
#define LF_KI               0.0f
#define LF_KD               0.20f
#define LF_MAX_CORR          180.0f
#define LF_BASE_SPEED        MOTOR_RUN_SPEED

// ── NFC ─────────────────────────────────────────────────────────────
#define NFC_READ_MS          100
#define NFC_REPEAT_GUARD_MS  700

// ── UART Protocol ───────────────────────────────────────────────────
#define UART_STX             0x7E
#define UART_MAX_FRAME       128

// ── Timing ──────────────────────────────────────────────────────────
#define MAIN_LOOP_DELAY_MS   2
#define TOF_READ_MS          50

// ── Route ───────────────────────────────────────────────────────────
#define MAX_ROUTE_LEN        30
