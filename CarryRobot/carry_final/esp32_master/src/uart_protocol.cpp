#include "uart_protocol.h"

// ── CRC-8 (polynomial 0x07, init 0x00) ─────────────────────────────
uint8_t crc8(const uint8_t *data, uint8_t len) {
    uint8_t crc = 0x00;
    for (uint8_t i = 0; i < len; i++) {
        crc ^= data[i];
        for (uint8_t b = 0; b < 8; b++)
            crc = (crc & 0x80) ? (crc << 1) ^ 0x07 : (crc << 1);
    }
    return crc;
}

// ── Transmit ────────────────────────────────────────────────────────
void uartSendFrame(HardwareSerial &port, uint8_t cmd,
                   const uint8_t *data, uint8_t dataLen)
{
    uint8_t payloadLen = 1 + dataLen;          // CMD + DATA
    uint8_t frame[UART_MAX_FRAME];
    uint8_t idx = 0;

    frame[idx++] = UART_STX;
    frame[idx++] = payloadLen;
    frame[idx++] = cmd;
    for (uint8_t i = 0; i < dataLen; i++)
        frame[idx++] = data[i];

    // CRC over CMD + DATA
    frame[idx] = crc8(&frame[2], payloadLen);
    idx++;

    port.write(frame, idx);
}

// ── Receive (state machine, non-blocking) ───────────────────────────
static uint8_t  rxBuf[UART_MAX_FRAME];
static uint8_t  rxIdx   = 0;
static uint8_t  rxLen   = 0;        // expected payload length
static uint8_t  rxState = 0;        // 0=wait STX, 1=wait LEN, 2=collect

bool uartReceiveFrame(HardwareSerial &port,
                      uint8_t &cmd, uint8_t *buf, uint8_t &len)
{
    while (port.available()) {
        uint8_t b = port.read();

        switch (rxState) {
        case 0:                           // wait for STX
            if (b == UART_STX) { rxState = 1; rxIdx = 0; }
            break;

        case 1:                           // LEN byte
            rxLen   = b;
            if (rxLen == 0 || rxLen > UART_MAX_FRAME - 4) {
                rxState = 0;              // invalid → reset
                break;
            }
            rxState = 2;
            rxIdx   = 0;
            break;

        case 2:                           // payload + CRC
            rxBuf[rxIdx++] = b;
            if (rxIdx == rxLen + 1) {     // +1 for CRC byte
                rxState = 0;

                // verify CRC
                uint8_t expected = crc8(rxBuf, rxLen);
                if (rxBuf[rxLen] != expected) break;   // bad CRC

                cmd = rxBuf[0];
                len = rxLen - 1;          // payload without CMD
                for (uint8_t i = 0; i < len; i++)
                    buf[i] = rxBuf[1 + i];
                return true;
            }
            break;
        }
    }
    return false;
}
