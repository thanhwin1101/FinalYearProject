#include "pn532_reader.h"
#include <SPI.h>
#include <Adafruit_PN532.h>
#include "uart_protocol.h"

extern HardwareSerial Serial2;   // UART to ESP32

// Software SPI — much more reliable on STM32duino than hardware SPI
static Adafruit_PN532 nfc(PN532_SCK, PN532_MISO, PN532_MOSI, PN532_SS);
static bool s_ready = false;
static uint32_t lastReadMs = 0;
static uint8_t  lastUid[7] = {};
static uint8_t  lastUidLen  = 0;
static uint32_t lastUidTime = 0;
static uint32_t lastRetryMs = 0;
static uint8_t  consecutiveFails = 0;
static const uint8_t  MAX_CONSEC_FAILS = 20;  // ~20 × 100ms = 2s of failures → re-init

// Send short debug text to ESP32 (shows as [STM32] ... on monitor)
static void sendDebug(const char *msg) {
    uint8_t n = strlen(msg);
    if (n > 100) n = 100;
    uartSendFrame(Serial2, CMD_DEBUG_MSG, (const uint8_t*)msg, n);
}

void nfcInit() {
    nfc.begin();
    uint32_t ver = nfc.getFirmwareVersion();
    if (!ver) {
        Serial.println("[NFC] PN532 not found (relay may be off)");
        sendDebug("NFC: PN532 not found");
        s_ready = false;
        return;
    }
    nfc.SAMConfig();
    s_ready = true;
    char tmp[40];
    snprintf(tmp, sizeof(tmp), "NFC: PN532 FW %u.%u OK", (ver >> 16) & 0xFF, (ver >> 8) & 0xFF);
    Serial.println(tmp);
    sendDebug(tmp);
}

bool nfcAvailable() { return s_ready; }

// Force s_ready=false so next nfcReadCheckpoint() triggers re-init.
// Call this after relay R3 is power-cycled (e.g. mode switch).
void nfcReset() {
    s_ready = false;
    Serial.println("[NFC] reset – will re-init");
    sendDebug("NFC: reset, will re-init");
}

// Retry init periodically if PN532 not detected (e.g. relay was off at boot)
static void nfcRetryInit() {
    uint32_t now = millis();
    if (now - lastRetryMs < 2000) return;   // retry every 2s
    lastRetryMs = now;
    Serial.println("[NFC] retrying init...");
    sendDebug("NFC: retrying init...");
    nfcInit();
}

uint16_t nfcReadCheckpoint() {
    if (!s_ready) {
        nfcRetryInit();
        return 0;
    }

    uint32_t now = millis();

    if (now - lastReadMs < NFC_READ_MS) return 0;
    lastReadMs = now;

    uint8_t uid[7];
    uint8_t uidLen = 0;
    if (!nfc.readPassiveTargetID(PN532_MIFARE_ISO14443A, uid, &uidLen, 50)) {
        // Track consecutive read failures — if PN532 lost power, re-init
        if (++consecutiveFails >= MAX_CONSEC_FAILS) {
            consecutiveFails = 0;
            Serial.println("[NFC] too many fails – re-init");
            sendDebug("NFC: too many fails, re-init");
            s_ready = false;
        }
        return 0;
    }
    consecutiveFails = 0;   // successful read resets counter

    // repeat guard: same UID within guard time → ignore
    if (uidLen == lastUidLen &&
        memcmp(uid, lastUid, uidLen) == 0 &&
        (now - lastUidTime) < NFC_REPEAT_GUARD_MS)
        return 0;

    memcpy(lastUid, uid, uidLen);
    lastUidLen  = uidLen;
    lastUidTime = now;

    // convert UID to uint16_t: use last 2 bytes
    uint16_t id = 0;
    if (uidLen >= 2) {
        id = ((uint16_t)uid[uidLen - 2] << 8) | uid[uidLen - 1];
    } else if (uidLen == 1) {
        id = uid[0];
    }
    return id;
}
