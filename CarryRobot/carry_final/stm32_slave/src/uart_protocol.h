#pragma once
#include <Arduino.h>
#include "config.h"

// ── Commands  ESP32 → STM32 ─────────────────────────────────────────
#define CMD_SET_MODE        0x01
#define CMD_SEND_ROUTE      0x02
#define CMD_DIRECT_VEL      0x03
#define CMD_WHEEL_SET       0x08   // data: int16 FL,FR,BL,BR (8 bytes, raw PWM -255..255)
#define CMD_SERVO_SET       0x07   // data: uint8 x_angle, uint8 y_angle
#define CMD_SERVO_SWEEP     0x09   // data: uint8 active(1/0), uint8 center(0-180), uint8 amplitude(0-90), uint8 freqX10(1-30)
#define CMD_TUNE_SPEED      0x0A   // data: uint8 runSpeed, uint8 turnSpeed
#define CMD_REQUEST_STATUS  0x04
#define CMD_CANCEL_MISSION  0x05
#define CMD_CONFIRM_ARRIVAL 0x06

// ── Commands  STM32 → ESP32 ─────────────────────────────────────────
#define CMD_BATTERY         0x81
#define CMD_CHECKPOINT      0x82
#define CMD_OBSTACLE        0x83
#define CMD_ACK             0x84
#define CMD_MISSION_DONE    0x85
#define CMD_MISMATCH        0x86
#define CMD_DEBUG_MSG       0x87   // data: ASCII string (up to ~120 chars)
#define CMD_LINE_LOST       0x88   // no data: line sensor lost line
#define CMD_HUSKY_STATUS    0x89   // data: detected(1) xCenter(2) yCenter(2) width(2) height(2) id(2)
#define CMD_TAG_LOST        0x8A   // no data: follow mode, tag lost >10s
#define CMD_TAG_FOUND       0x8B   // no data: find mode, tag re-acquired
#define CMD_LINE_STATUS     0x8C   // data: uint8 bits (bit0=L, bit1=C, bit2=R)

uint8_t crc8(const uint8_t *data, uint8_t len);

void uartSendFrame(HardwareSerial &port, uint8_t cmd,
                   const uint8_t *data, uint8_t dataLen);

bool uartReceiveFrame(HardwareSerial &port,
                      uint8_t &cmd, uint8_t *buf, uint8_t &len);
