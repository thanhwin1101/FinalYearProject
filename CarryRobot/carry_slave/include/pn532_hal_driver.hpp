#pragma once
// ====================================================================
//  Pn532HalDriver – Native STM32 HAL SPI Driver for PN532 RFID
// --------------------------------------------------------------------
//  - Giao tiếp trực tiếp qua SPI1 (PA5/6/7) và chân CS (PB14)
//  - Không dùng Heap (Zero-malloc), không phụ thuộc thư viện Arduino
//  - Quét thẻ Mifare ISO14443A và ánh xạ sang Checkpoint Node ID
// ====================================================================
#include "bsp_spi.h"
#include "bsp_pins.h"
#include "checkpoint_map.h"
#include <string.h>
#include <stdio.h>

class Pn532HalDriver {
private:
    char     _lastId[MAX_NODE_ID_LEN] = {0};
    uint32_t _lastMs = 0;
    bool     _ready  = false;

    // Gửi lệnh thô xuống PN532 qua SPI
    bool sendCommand(const uint8_t* cmd, uint8_t cmdLen) {
        BSP_PN532_CS_Low();
        HAL_Delay(1);

        uint8_t writeCmd = 0x01; // PN532 SPI Data Write
        BSP_SPI1_TransmitReceive(&writeCmd, nullptr, 1, 10);

        uint8_t checksum = 0xD4;
        uint8_t frameHeader[5];
        frameHeader[0] = 0x00;
        frameHeader[1] = 0x00;
        frameHeader[2] = 0xFF;
        frameHeader[3] = cmdLen + 1; // Độ dài = cmdLen + 1 (cho byte TFI 0xD4)
        frameHeader[4] = (uint8_t)(~frameHeader[3] + 1); // LCS

        BSP_SPI1_TransmitReceive(frameHeader, nullptr, 5, 10);

        uint8_t tfi = 0xD4;
        BSP_SPI1_TransmitReceive(&tfi, nullptr, 1, 10);

        for (uint8_t i = 0; i < cmdLen; ++i) {
            BSP_SPI1_TransmitReceive((uint8_t*)&cmd[i], nullptr, 1, 10);
            checksum += cmd[i];
        }

        uint8_t frameTail[2];
        frameTail[0] = (uint8_t)(~checksum + 1); // DCS
        frameTail[1] = 0x00;                    // Postamble
        BSP_SPI1_TransmitReceive(frameTail, nullptr, 2, 10);

        BSP_PN532_CS_High();
        return true;
    }

    // Đợi module phản hồi sẵn sàng (Status Bit 0 == 1)
    bool waitReady(uint32_t timeoutMs) {
        uint32_t start = HAL_GetTick();
        while (HAL_GetTick() - start < timeoutMs) {
            BSP_PN532_CS_Low();
            uint8_t readStatus = 0x02;
            uint8_t status = 0;
            BSP_SPI1_TransmitReceive(&readStatus, &status, 1, 10);
            BSP_PN532_CS_High();

            if (status & 0x01) {
                return true;
            }
            HAL_Delay(1);
        }
        return false;
    }

public:
    Pn532HalDriver() = default;

    bool begin() {
        BSP_SPI1_Init();
        BSP_PN532_CS_High();
        HAL_Delay(50);

        // Gửi lệnh SAMConfiguration (0x14 0x01 0x14 0x01) - Normal mode, timeout 1s, IRQ enabled
        uint8_t samCmd[] = {0x14, 0x01, 0x14, 0x01};
        if (!sendCommand(samCmd, sizeof(samCmd))) {
            _ready = false;
            return false;
        }

        if (waitReady(100)) {
            _ready = true;
            return true;
        }
        _ready = false;
        return false;
    }

    bool ready() const { return _ready; }

    // Quét thẻ Mifare (InListPassiveTarget)
    bool poll(char* outNodeId, size_t maxLen) {
        if (!_ready) {
            return false;
        }

        // Lệnh InListPassiveTarget: 0x4A, 0x01 (1 thẻ), 0x00 (106 kbps Type A - Mifare)
        uint8_t scanCmd[] = {0x4A, 0x01, 0x00};
        sendCommand(scanCmd, sizeof(scanCmd));

        if (!waitReady(40)) {
            return false;
        }

        // Đọc phản hồi
        BSP_PN532_CS_Low();
        uint8_t readDataCmd = 0x03;
        uint8_t rxBuf[32] = {0};
        BSP_SPI1_TransmitReceive(&readDataCmd, nullptr, 1, 10);
        BSP_SPI1_TransmitReceive(nullptr, rxBuf, sizeof(rxBuf), 20);
        BSP_PN532_CS_High();

        // Kiểm tra xem có tìm thấy ít nhất 1 thẻ (NbTg == 1)
        // Cấu trúc response: [Preamble] [LEN] [TFI 0xD5] [0x4B] [NbTg] [Tg] [SENS_RES 2B] [SEL_RES 1B] [NFCIDLen 1B] [NFCID...]
        // Duyệt tìm byte 0xD5 0x4B
        uint8_t idx = 0;
        while (idx < 25 && !(rxBuf[idx] == 0xD5 && rxBuf[idx + 1] == 0x4B)) {
            idx++;
        }

        if (idx >= 25 || rxBuf[idx + 2] == 0) {
            return false; // Không tìm thấy thẻ
        }

        uint8_t uidLen = rxBuf[idx + 7];
        if (uidLen < 4) return false;

        uint8_t* uid = &rxBuf[idx + 8];
        char hex[16] = {0};
        snprintf(hex, sizeof(hex), "%02X%02X%02X%02X", uid[0], uid[1], uid[2], uid[3]);

        const char* mappedNode = uidToNodeId(hex);
        char currentId[MAX_NODE_ID_LEN] = {0};
        if (mappedNode && *mappedNode) {
            strncpy(currentId, mappedNode, sizeof(currentId) - 1);
        } else {
            snprintf(currentId, sizeof(currentId), "?%s", hex);
        }

        uint32_t now = HAL_GetTick();
        if (strcmp(currentId, _lastId) == 0 && (now - _lastMs < 700)) {
            return false; // Chống dội thẻ
        }

        strncpy(_lastId, currentId, sizeof(_lastId) - 1);
        _lastMs = now;

        if (outNodeId && maxLen > 0) {
            strncpy(outNodeId, currentId, maxLen - 1);
            outNodeId[maxLen - 1] = '\0';
        }
        return true;
    }
};
