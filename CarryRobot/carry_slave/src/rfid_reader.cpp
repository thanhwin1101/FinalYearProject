/*  rfid_reader.cpp  –  PN532 NFC tag reading via SPI
 */
#include "rfid_reader.h"
#include "config.h"
#include <SPI.h>
#include <Adafruit_PN532.h>

static Adafruit_PN532 nfc(PN532_SCK, PN532_MISO, PN532_MOSI, PN532_SS);
static bool nfcOk = false;

// Repeat guard
static char     lastUid[RFID_UID_LOCAL_LEN] = "";
static unsigned long lastUidMs  = 0;

void rfidInit() {
    nfc.begin();
    uint32_t ver = nfc.getFirmwareVersion();
    if (!ver) {
        Serial.println(F("[NFC] PN532 not found – continuing without NFC"));
        return;
    }
    Serial.printf("[NFC] PN532 FW %u.%u\n", (ver >> 16) & 0xFF, (ver >> 8) & 0xFF);
    nfc.SAMConfig();
    nfcOk = true;
}

bool rfidRead(char uid[RFID_UID_LOCAL_LEN]) {
    uid[0] = '\0';
    if (!nfcOk) return false;

    uint8_t uidBuf[7] = {};
    uint8_t uidLen = 0;
    if (!nfc.readPassiveTargetID(PN532_MIFARE_ISO14443A, uidBuf, &uidLen, 50))
        return false;

    // Format UID as uppercase colon-separated string, e.g. "45:D3:91:83".
    char hex[RFID_UID_LOCAL_LEN] = "";
    size_t pos = 0;
    for (uint8_t i = 0; i < uidLen && i < 7 && pos < sizeof(hex) - 1; i++) {
        int written = snprintf(&hex[pos], sizeof(hex) - pos, (i == 0) ? "%02X" : ":%02X", uidBuf[i]);
        if (written < 0) break;
        if ((size_t)written >= sizeof(hex) - pos) {
            pos = sizeof(hex) - 1;
            break;
        }
        pos += (size_t)written;
    }

    // Repeat guard
    if (strcmp(hex, lastUid) == 0 && millis() - lastUidMs < NFC_REPEAT_GUARD_MS)
        return false;

    strncpy(lastUid, hex, sizeof(lastUid));
    lastUid[sizeof(lastUid) - 1] = '\0';
    lastUidMs = millis();
    strncpy(uid, hex, RFID_UID_LOCAL_LEN);
    uid[RFID_UID_LOCAL_LEN - 1] = '\0';
    return true;
}
