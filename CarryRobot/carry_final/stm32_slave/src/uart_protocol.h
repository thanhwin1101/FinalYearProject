#pragma once
#include <Arduino.h>
#include "config.h"

// ── Commands  ESP32 → STM32 ─────────────────────────────────────────
#define CMD_SET_MODE        0x01
#define CMD_SEND_ROUTE      0x02
#define CMD_DIRECT_VEL      0x03
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

uint8_t crc8(const uint8_t *data, uint8_t len);

void uartSendFrame(HardwareSerial &port, uint8_t cmd,
                   const uint8_t *data, uint8_t dataLen);

bool uartReceiveFrame(HardwareSerial &port,
                      uint8_t &cmd, uint8_t *buf, uint8_t &len);
