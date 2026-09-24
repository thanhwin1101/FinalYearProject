#pragma once
// ====================================================================
//  Carry Robot – STM32 Slave – 3-eye Line Follower + PN532 RFID (SPI)
// --------------------------------------------------------------------
//  - Line Follower PD từ 3 cảm biến quang trở (PA4, PB9, PB8)
//  - RFID PN532 giao tiếp Hardware SPI1 (PA5, PA6, PA7, CS PB14)
//  - ZERO HEAP ALLOCATION: Loại bỏ hoàn toàn 'new' và 'String'
//    dùng Static Buffer an toàn tuyệt đối cho hệ thống thời gian thực.
// ====================================================================
#include <Arduino.h>
#include <SPI.h>
#include <Adafruit_PN532.h>
#include <string.h>
#include "config.h"
#include "checkpoint_map.h"

// --------------------------------------------------------------------
//  Line follower with fast PD from 3 digital sensors
// --------------------------------------------------------------------
class LineFollower {
public:
    void begin() {
        pinMode(PIN_LINE_L, INPUT);
        pinMode(PIN_LINE_C, INPUT);
        pinMode(PIN_LINE_R, INPUT);
    }

    // Trả về sai số vị trí [-2..+2] (âm = lệch trái, dương = lệch phải)
    int readError() {
        int l = (digitalRead(PIN_LINE_L) == LOW) ? 1 : 0;
        int c = (digitalRead(PIN_LINE_C) == LOW) ? 1 : 0;
        int r = (digitalRead(PIN_LINE_R) == LOW) ? 1 : 0;
        int mask = (l << 2) | (c << 1) | r;

        switch (mask) {
            case 0b010: return  0;   // Chỉ mắt giữa thấy line
            case 0b110: return -1;   // Trái + Giữa
            case 0b100: return -2;   // Chỉ Trái
            case 0b011: return +1;   // Giữa + Phải
            case 0b001: return +2;   // Chỉ Phải
            case 0b111: return  0;   // Ngã tư / Cả 3 mắt
            case 0b000:
                // Mất line tạm thời -> giữ hướng thẳng để tìm lại line
                _lastErr = 0;
                return 0;
            default:    return _lastErr;
        }
    }

    // Tính toán công suất động cơ trái/phải theo thuật toán PD
    void step(int& outL, int& outR) {
        int err = readError();
        int d   = err - _lastErr;
        _lastErr = err;
        int corr = LF_KP * err + LF_KD * d;
        outL = LF_BASE_PWM + corr;
        outR = LF_BASE_PWM - corr;
        outL = constrain(outL, 0, 230);
        outR = constrain(outR, 0, 230);
    }

private:
    int _lastErr = 0;
};

// --------------------------------------------------------------------
//  PN532 RFID – Static Object, Zero Dynamic Allocation
// --------------------------------------------------------------------
class RfidCheckpoint {
public:
    RfidCheckpoint() : _pn532(PN532_SS) {}

    bool begin() {
        SPI.begin();
        _pn532.begin();
        uint32_t versiondata = _pn532.getFirmwareVersion();
        if (!versiondata) {
            _ready = false;
            return false;
        }
        _pn532.SAMConfig();
        _ready = true;
        return true;
    }

    bool reinit() {
        _ready = false;
        _lastId[0] = '\0';
        _lastMs = 0;
        for (int i = 0; i < 3; ++i) {
            if (begin()) return true;
            delay(50);
        }
        return false;
    }

    bool ready() const { return _ready; }

    // Quét thẻ Mifare không cấp phát heap.
    // Nếu có thẻ mới hợp lệ: chép nodeId vào outNodeId và trả về true.
    // Trả về false nếu không có thẻ hoặc thẻ lặp lại trong khoảng NFC_REPEAT_MS.
    bool poll(char* outNodeId, size_t maxLen) {
        if (!_ready) {
            _pollSkip++;
            return false;
        }

        uint8_t uid[7] = {0};
        uint8_t uidLen = 0;
        bool ok = _pn532.readPassiveTargetID(PN532_MIFARE_ISO14443A, uid, &uidLen, NFC_POLL_TIMEOUT_MS);
        _pollTotal++;

        if (!ok || uidLen == 0) {
            _pollEmpty++;
            return false;
        }
        _pollOk++;

        // Chuyển 4 byte đầu của UID sang chuỗi HEX viết hoa (ví dụ: "45548083")
        char hex[16] = {0};
        snprintf(hex, sizeof(hex), "%02X%02X%02X%02X", uid[0], uid[1], uid[2], uid[3]);

        const char* mappedNode = uidToNodeId(hex);
        char currentId[MAX_NODE_ID_LEN] = {0};

        if (mappedNode && *mappedNode) {
            strncpy(currentId, mappedNode, sizeof(currentId) - 1);
        } else {
            // Thẻ lạ: thêm dấu '?' ở đầu
            snprintf(currentId, sizeof(currentId), "?%s", hex);
        }

        uint32_t now = millis();
        if (strcmp(currentId, _lastId) == 0 && (now - _lastMs < NFC_REPEAT_MS)) {
            return false; // Bỏ qua quét lặp trong khoảng lặp
        }

        strncpy(_lastId, currentId, sizeof(_lastId) - 1);
        _lastMs = now;

        if (outNodeId && maxLen > 0) {
            strncpy(outNodeId, currentId, maxLen - 1);
            outNodeId[maxLen - 1] = '\0';
        }
        return true;
    }

    uint32_t pollOk()    const { return _pollOk; }
    uint32_t pollEmpty() const { return _pollEmpty; }
    uint32_t pollTotal() const { return _pollTotal; }

private:
    Adafruit_PN532 _pn532;                          // Đối tượng tĩnh (Không dùng new)
    char           _lastId[MAX_NODE_ID_LEN] = {0};   // Bộ đệm tĩnh (Không dùng String)
    uint32_t       _lastMs = 0;
    bool           _ready  = false;

    // Diagnostic counters
    uint32_t       _pollTotal = 0;
    uint32_t       _pollOk    = 0;
    uint32_t       _pollEmpty = 0;
    uint32_t       _pollSkip  = 0;
};
