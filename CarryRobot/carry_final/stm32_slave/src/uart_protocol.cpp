#include "uart_protocol.h"

uint8_t crc8(const uint8_t *data, uint8_t len) {
    uint8_t crc = 0x00;
    for (uint8_t i = 0; i < len; i++) {
        crc ^= data[i];
        for (uint8_t b = 0; b < 8; b++)
            crc = (crc & 0x80) ? (crc << 1) ^ 0x07 : (crc << 1);
    }
    return crc;
}

void uartSendFrame(HardwareSerial &port, uint8_t cmd,
                   const uint8_t *data, uint8_t dataLen)
{
    uint8_t payloadLen = 1 + dataLen;
    uint8_t frame[UART_MAX_FRAME];
    uint8_t idx = 0;

    frame[idx++] = UART_STX;
    frame[idx++] = payloadLen;
    frame[idx++] = cmd;
    for (uint8_t i = 0; i < dataLen; i++)
        frame[idx++] = data[i];

    frame[idx] = crc8(&frame[2], payloadLen);
    idx++;

    port.write(frame, idx);
}

static uint8_t rxBuf[UART_MAX_FRAME];
static uint8_t rxIdx   = 0;
static uint8_t rxLen   = 0;
static uint8_t rxState = 0;

bool uartReceiveFrame(HardwareSerial &port,
                      uint8_t &cmd, uint8_t *buf, uint8_t &len)
{
    while (port.available()) {
        uint8_t b = port.read();

        switch (rxState) {
        case 0:
            if (b == UART_STX) { rxState = 1; rxIdx = 0; }
            break;
        case 1:
            rxLen = b;
            if (rxLen == 0 || rxLen > UART_MAX_FRAME - 4) { rxState = 0; break; }
            rxState = 2; rxIdx = 0;
            break;
        case 2:
            rxBuf[rxIdx++] = b;
            if (rxIdx == rxLen + 1) {
                rxState = 0;
                uint8_t expected = crc8(rxBuf, rxLen);
                if (rxBuf[rxLen] != expected) break;
                cmd = rxBuf[0];
                len = rxLen - 1;
                for (uint8_t i = 0; i < len; i++) buf[i] = rxBuf[1 + i];
                return true;
            }
            break;
        }
    }
    return false;
}
