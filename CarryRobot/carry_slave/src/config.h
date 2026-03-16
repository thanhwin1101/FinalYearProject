/*  config.h  –  Slave ESP32 pin map & constants
 *  Carry Robot – Dual-ESP32 architecture
 */
#pragma once

// ======================= Pin Map – Slave ESP32 ================

// PN532 NFC/RFID  (VSPI)
#define PN532_SCK             18
#define PN532_MISO            19
#define PN532_MOSI            23
#define PN532_SS              5

// Line-follower 3-sensor array (Left-Centre-Right)
// Using the middle 3 sensors from the previous 5-sensor harness.
#define LINE_S1               35      // Left
#define LINE_S2               36      // Centre  (VP)
#define LINE_S3               39      // Right   (VN)

// L298N #1  –  Left side motors
#define L1_ENA                13      // Front-Left speed (PWM)
#define L1_IN1                12      // Front-Left dir A
#define L1_IN2                14      // Front-Left dir B
#define L1_IN3                27      // Back-Left dir A
#define L1_IN4                26      // Back-Left dir B
#define L1_ENB                25      // Back-Left speed (PWM)

// L298N #2  –  Right side motors
#define L2_ENA                33      // Front-Right speed (PWM)
#define L2_IN1                4       // Front-Right dir A
#define L2_IN2                16      // Front-Right dir B
#define L2_IN3                17      // Back-Right dir A
#define L2_IN4                22      // Back-Right dir B
#define L2_ENB                21      // Back-Right speed (PWM)

// ======================= Motor PWM ============================
#define MOTOR_PWM_FREQ        20000   // Hz
#define MOTOR_PWM_RES         8       // bits  (0-255)

// Global speed scaling for movement commands received from Master.
// 100 = normal speed, 70 = slower, 120 = faster.
#define RUN_SPEED_PERCENT     100

// Optional extra scaling for rotation component (vR).
// Keep same as RUN_SPEED_PERCENT for uniform behavior.
#define ROTATE_SPEED_PERCENT  100

// LEDC channels  (4 enable pins = 4 channels)
#define CH_FL                 0       // Front-Left
#define CH_BL                 1       // Back-Left
#define CH_FR                 2       // Front-Right
#define CH_BR                 3       // Back-Right

// ======================= Line Follower PID ====================
#define LINE_KP               0.35f
#define LINE_KI               0.0f
#define LINE_KD               0.20f
#define LINE_MAX_CORRECTION   180.0f

// Sensor weights for error calculation  (leftmost -2 to rightmost +2)
// Black-on-white: sensor LOW = sees line
#define LINE_INVERT           true    // true = LOW means line detected

// ======================= RFID =================================
#define NFC_READ_INTERVAL     100     // ms between scans
#define NFC_REPEAT_GUARD_MS   700     // ignore same UID within this window

// ======================= Turn timing ==========================
#define TURN_90_MS            974
#define TURN_180_MS           1980
#define TURN_PWM              168

// Active electrical brake (both IN pins HIGH on each H-bridge leg)
#define PWM_BRAKE             150
#define BRAKE_MS              80

// ======================= ESP-NOW ==============================
//  Master MAC – from latest master upload log (COM7)
static const uint8_t MASTER_MAC[6] = {0x20,0xE7,0xC8,0x68,0x55,0xF0};

// WiFi channel  –  MUST match the Master's WiFi AP channel
#define ESPNOW_CHANNEL        7

// ======================= Timing ===============================
#define ESPNOW_TX_INTERVAL    50      // 20 Hz feedback to Master
#define MAIN_LOOP_DELAY       2       // ms
