#pragma once
#include <Arduino.h>
#include "config.h"

// ────────────────────────────────────────────────────────────────────
//  UART frame:  [STX 0x7E] [LEN] [CMD] [DATA …] [CRC8]
//  LEN = sizeof(CMD + DATA)   (CRC not counted in LEN)
// ────────────────────────────────────────────────────────────────────

// ── Commands  ESP32 → STM32 ─────────────────────────────────────────
#define CMD_SET_MODE        0x01   // data: 1 byte mode
#define CMD_SEND_ROUTE      0x02   // data: [count][id_hi id_lo action]×N
#define CMD_DIRECT_VEL      0x03   // data: int16 Vx, Vy, Vr  (6 bytes)
#define CMD_REQUEST_STATUS  0x04   // no data
#define CMD_CANCEL_MISSION  0x05   // no data
#define CMD_CONFIRM_ARRIVAL 0x06   // data: uint16 checkpointId

// ── Commands  STM32 → ESP32 ─────────────────────────────────────────
#define CMD_BATTERY         0x81   // data: uint8 percent
#define CMD_CHECKPOINT      0x82   // data: uint16 checkpointId
#define CMD_OBSTACLE        0x83   // no data
#define CMD_ACK             0x84   // data: uint8 cmd_ref
#define CMD_MISSION_DONE    0x85   // no data
#define CMD_MISMATCH        0x86   // data: uint16 got, uint16 expected
#define CMD_DEBUG_MSG       0x87   // data: ASCII text from STM32
#define CMD_LINE_LOST       0x88   // no data: line sensor lost line

// ── CRC-8 (polynomial 0x07) ────────────────────────────────────────
uint8_t crc8(const uint8_t *data, uint8_t len);

// ── Send a frame on the given serial port ───────────────────────────
void uartSendFrame(HardwareSerial &port, uint8_t cmd,
                   const uint8_t *data, uint8_t dataLen);

// ── Try to receive a complete frame (non-blocking) ──────────────────
//    Returns true if a valid frame was decoded.
//    `cmd`  ← command byte
//    `buf`  ← payload (DATA only, without CMD)
//    `len`  ← length of payload
bool uartReceiveFrame(HardwareSerial &port,
                      uint8_t &cmd, uint8_t *buf, uint8_t &len);
