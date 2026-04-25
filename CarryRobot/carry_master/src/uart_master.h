#pragma once
// ====================================================================
//  Carry Robot – Master – UART framing <CMD:DATA>
//  Uses Serial2 on ESP32 @ 115200
// ====================================================================
#include <Arduino.h>
#include "config.h"

#define UART_STM32 Serial2

typedef void (*UartFrameCb)(const String& cmd, const String& data);

class UartMaster {
public:
    void begin(UartFrameCb cb) {
        _cb = cb;
        UART_STM32.begin(UART_STM32_BAUD, SERIAL_8N1, UART_STM32_RX, UART_STM32_TX);
        _buf.reserve(UART_FRAME_MAX);
    }

    // --- Send "<CMD|CRC>" or "<CMD:DATA|CRC>" (NFR-06 CRC8) -----------
    void send(const String& cmd) {
        uint8_t c = _crc8(cmd.c_str(), cmd.length());
        char hex[3]; snprintf(hex, sizeof(hex), "%02X", c);
        UART_STM32.print('<'); UART_STM32.print(cmd);
        UART_STM32.print('|'); UART_STM32.print(hex); UART_STM32.print('>');
    }
    void send(const String& cmd, const String& data) {
        String content = cmd + ":" + data;
        uint8_t c = _crc8(content.c_str(), content.length());
        char hex[3]; snprintf(hex, sizeof(hex), "%02X", c);
        UART_STM32.print('<'); UART_STM32.print(content);
        UART_STM32.print('|'); UART_STM32.print(hex); UART_STM32.print('>');
    }

    // --- Call every loop to parse incoming frames ----------------------
    void loop() {
        while (UART_STM32.available()) {
            char c = (char)UART_STM32.read();
            if (c == '<') { _buf = ""; _inFrame = true; continue; }
            if (c == '>' && _inFrame) {
                _inFrame = false;
                // CRC8 validation (NFR-06): expect last '|XX'
                int pipe = _buf.lastIndexOf('|');
                if (pipe < 0 || _buf.length() - (unsigned)pipe - 1 != 2) { _buf = ""; continue; }
                String crcStr  = _buf.substring(pipe + 1);
                uint8_t rxCrc  = (uint8_t)strtol(crcStr.c_str(), nullptr, 16);
                String content = _buf.substring(0, pipe);
                uint8_t calcCrc = _crc8(content.c_str(), content.length());
                if (rxCrc != calcCrc) { _buf = ""; continue; } // silently drop
                int sep = content.indexOf(':');
                String cmd, data;
                if (sep < 0) { cmd = content; }
                else { cmd = content.substring(0, sep); data = content.substring(sep + 1); }
                if (_cb) _cb(cmd, data);
                _buf = "";
                continue;
            }
            if (_inFrame && _buf.length() < UART_FRAME_MAX) _buf += c;
        }
    }

private:
    // CRC8 (poly 0x07, init 0x00) — same algorithm as STM32 slave
    static uint8_t _crc8(const char* data, size_t len) {
        uint8_t crc = 0x00;
        for (size_t i = 0; i < len; i++) {
            crc ^= (uint8_t)data[i];
            for (uint8_t b = 0; b < 8; b++)
                crc = (crc & 0x80) ? (crc << 1) ^ 0x07 : (crc << 1);
        }
        return crc;
    }

    UartFrameCb _cb      = nullptr;
    bool        _inFrame = false;
    String      _buf;
};
