#pragma once

#define PN532_SCK             18
#define PN532_MISO            19
#define PN532_MOSI            23
#define PN532_SS              5

#define LINE_S1               35
#define LINE_S2               36
#define LINE_S3               39

#define L1_ENA                13
#define L1_IN1                12
#define L1_IN2                14
#define L1_IN3                27
#define L1_IN4                26
#define L1_ENB                25

#define L2_ENA                33
#define L2_IN1                4
#define L2_IN2                16
#define L2_IN3                17
#define L2_IN4                22
#define L2_ENB                21

#define MOTOR_PWM_FREQ        20000
#define MOTOR_PWM_RES         8

#define RUN_SPEED_PERCENT     100

#define ROTATE_SPEED_PERCENT  100

#define CH_FL                 0
#define CH_BL                 1
#define CH_FR                 2
#define CH_BR                 3

#define LINE_KP               0.35f
#define LINE_KI               0.0f
#define LINE_KD               0.20f
#define LINE_MAX_CORRECTION   180.0f

#define LINE_INVERT           true

#define NFC_READ_INTERVAL     100
#define NFC_REPEAT_GUARD_MS   700

#define TURN_90_MS            974
#define TURN_180_MS           1980
#define TURN_PWM              168

#define PWM_BRAKE             150
#define BRAKE_MS              80

static const uint8_t MASTER_MAC[6] = {0x20,0xE7,0xC8,0x68,0x55,0xF0};

#define ESPNOW_CHANNEL        7

#define ESPNOW_TX_INTERVAL    50
#define MAIN_LOOP_DELAY       2
