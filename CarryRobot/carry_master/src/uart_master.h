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

    // --- Send "<CMD>" or "<CMD:DATA>" ----------------------------------
    void send(const String& cmd) {
        UART_STM32.print('<');
        UART_STM32.print(cmd);
        UART_STM32.print('>');
    }
    void send(const String& cmd, const String& data) {
        UART_STM32.print('<');
        UART_STM32.print(cmd);
        UART_STM32.print(':');
        UART_STM32.print(data);
        UART_STM32.print('>');
    }

    // --- Call every loop to parse incoming frames ----------------------
    void loop() {
        while (UART_STM32.available()) {
            char c = (char)UART_STM32.read();
            if (c == '<') { _buf = ""; _inFrame = true; continue; }
            if (c == '>' && _inFrame) {
                _inFrame = false;
                int sep = _buf.indexOf(':');
                String cmd, data;
                if (sep < 0) { cmd = _buf; }
                else { cmd = _buf.substring(0, sep); data = _buf.substring(sep + 1); }
                if (_cb) _cb(cmd, data);
                _buf = "";
                continue;
            }
            if (_inFrame && _buf.length() < UART_FRAME_MAX) _buf += c;
        }
    }

private:
    UartFrameCb _cb      = nullptr;
    bool        _inFrame = false;
    String      _buf;
};
