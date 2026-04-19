#pragma once
// ====================================================================
//  carry_final  –  STM32 Slave  –  Pin & Constant Configuration
// ====================================================================

// ── UART to ESP32 (USART2) ─────────────────────────────────────────
#define PIN_UART_TX         PA2
#define PIN_UART_RX         PA3
#define ESP_BAUD            115200

// ── HuskyLens (USART3) ─────────────────────────────────────────────
#define PIN_HUSKY_TX        PB10      // STM32 TX → HuskyLens RX
#define PIN_HUSKY_RX        PB11      // STM32 RX ← HuskyLens TX
#define HUSKY_BAUD          9600

// ── L298N #1   (Front-Left & Front-Right motors) ───────────────────
#define L1_IN1              PA0
#define L1_IN2              PA1
#define L1_ENA              PA8       // TIM1_CH1  PWM
#define L1_IN3              PA9
#define L1_IN4              PA10
#define L1_ENB              PB0       // TIM3_CH3  PWM

// ── L298N #2   (Back-Left & Back-Right motors) ─────────────────────
//  PB3  = TIM2_CH2 (hardware PWM, safe with SWD debugger)  → L2_ENA (BL)
//  PB5  = TIM3_CH2 (hardware PWM)                          → L2_ENB (BR)
//  PA12 = standard GPIO (no HW timer needed for direction)  → L2_IN4 (BR)
//  PC13 = REMOVED – open-drain 3mA LED pin, not suitable for motor drive
#define L2_IN1              PB12
#define L2_IN2              PB13
#define L2_ENA              PB3       // TIM2_CH2 hardware PWM  (BL enable)
#define L2_IN3              PB15
#define L2_IN4              PA12      // standard GPIO           (BR direction)
#define L2_ENB              PB5       // TIM3_CH2 hardware PWM  (BR enable)

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
#define MOTOR_RUN_SPEED     190       // 0-255  (runtime: g_runSpeed)
#define MOTOR_TURN_SPEED    175       //         (runtime: g_turnSpeed)
#define MOTOR_TURN_90_MS    950       // measured: ~950 ms for 90°
#define MOTOR_TURN_180_MS   1900
#define MOTOR_BRAKE_PWM     150
#define MOTOR_BRAKE_MS      80

// ── PWM soft-start / soft-stop ──────────────────────────────────────
#define MOTOR_KICK_PWM      230       // kick-start PWM để thắng ma sát nghỉ
#define MOTOR_KICK_MS       100       // thời gian kick (ms)
#define MOTOR_SOFTSTOP_STEP 40        // bước giảm PWM mỗi tick khi soft-stop
#define MOTOR_SOFTSTOP_MS   15        // ms mỗi bước (~5 bước từ 200→0)

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

// ── Servo Gimbal (Y axis only) ─────────────────────────────────────────
// PB4  = TIM3_CH1 (requires disabling JTAG in setup — SWD flash still works)
#define PIN_SERVO_Y         PB4
#define SERVO_Y_LEVEL       100
#define SERVO_Y_TILT_DOWN   45
#define SERVO_Y_LOOK_UP     115
#define SERVO_MIN           0
#define SERVO_MAX           180
