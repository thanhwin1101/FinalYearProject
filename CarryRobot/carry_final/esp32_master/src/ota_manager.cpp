// ota_manager.cpp  –  ESP32 ArduinoOTA + STM32 UART bootloader flash
// ====================================================================
//
//  ESP32 OTA (Wi-Fi)
//  -----------------
//  Uses ArduinoOTA (built into ESP32 Arduino framework, no extra lib).
//  Upload new firmware with:
//    pio run -e ota -t upload   (see platformio.ini [env:ota])
//  or via Arduino IDE → Port → agv-esp32.local
//
//  STM32 UART Bootloader flash (AN3155)
//  ------------------------------------
//  Requires 4 extra wires on the PCB:
//    ESP32 GPIO26 → STM32 BOOT0        (active HIGH = ROM bootloader)
//    ESP32 GPIO27 → STM32 NRST         (active LOW, 10kΩ pull-up to 3.3V)
//    ESP32 GPIO32 → STM32 PA9  (USART1 RX)
//    ESP32 GPIO33 ← STM32 PA10 (USART1 TX)
//
//  Trigger via MQTT:
//    topic: hospital/robots/AGV-01/command
//    payload: {"action":"ota_stm32","url":"http://192.168.1.x:8080/firmware.bin"}
// ====================================================================

#include "ota_manager.h"
#include "config.h"
#include "oled_display.h"
#include "buzzer.h"

#include <Arduino.h>
#include <ArduinoOTA.h>
#include <HTTPClient.h>
#include <WiFi.h>

// ── ESP32 OTA ──────────────────────────────────────────────────────

void otaInit() {
    ArduinoOTA.setHostname(OTA_HOSTNAME);
    ArduinoOTA.setPassword(OTA_PASSWORD);
    ArduinoOTA.setPort(3232);

    ArduinoOTA.onStart([]() {
        String type = (ArduinoOTA.getCommand() == U_FLASH) ? "sketch" : "filesystem";
        Serial.println("[OTA] Start: " + type);
        oledOtaProgress(0, "ESP32 OTA...");
        buzzerBeepN(1, 100, 0);
    });

    ArduinoOTA.onEnd([]() {
        Serial.println("[OTA] Done — rebooting");
        oledOtaProgress(100, "Done! Rebooting");
        buzzerBeepN(3, 80, 60);
    });

    ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
        uint8_t pct = (uint8_t)((uint32_t)progress * 100 / total);
        static uint8_t lastPct = 255;
        if (pct != lastPct) {
            lastPct = pct;
            Serial.printf("[OTA] %u%%\n", pct);
            oledOtaProgress(pct, "ESP32 OTA...");
        }
    });

    ArduinoOTA.onError([](ota_error_t error) {
        const char *msg = "Unknown";
        if      (error == OTA_AUTH_ERROR)    msg = "Auth failed";
        else if (error == OTA_BEGIN_ERROR)   msg = "Begin failed";
        else if (error == OTA_CONNECT_ERROR) msg = "Connect fail";
        else if (error == OTA_RECEIVE_ERROR) msg = "Receive fail";
        else if (error == OTA_END_ERROR)     msg = "End failed";
        Serial.printf("[OTA] Error: %s\n", msg);
        oledError(msg);
        buzzerBeepN(5, 50, 30);
    });

    ArduinoOTA.begin();
    Serial.printf("[OTA] ESP32 OTA ready  host=%s port=3232\n", OTA_HOSTNAME);
}

void otaLoop() {
    ArduinoOTA.handle();
}

// ── STM32 AN3155 UART bootloader ───────────────────────────────────

#define BL_ACK         0x79
#define BL_NACK        0x1F
#define BL_TIMEOUT_MS  2000
#define BL_PAGE        256            // max bytes per Write Memory command

static HardwareSerial blSerial(1);   // UART1, remapped to GPIO32/33

// Wait for ACK/NACK on bootloader UART
static bool blWaitAck(uint32_t timeoutMs = BL_TIMEOUT_MS) {
    uint32_t start = millis();
    while (millis() - start < timeoutMs) {
        if (blSerial.available()) {
            uint8_t b = (uint8_t)blSerial.read();
            if (b == BL_ACK)  return true;
            if (b == BL_NACK) { Serial.println("[BL] NACK"); return false; }
        }
        delay(1);
    }
    Serial.println("[BL] Timeout");
    return false;
}

static void blFlushRx() {
    while (blSerial.available()) blSerial.read();
    delay(5);
}

// Synchronise with bootloader: send 0x7F, expect ACK
static bool blSync() {
    blFlushRx();
    for (int attempt = 0; attempt < 10; attempt++) {
        blSerial.write((uint8_t)0x7F);
        uint32_t start = millis();
        while (millis() - start < 500) {
            if (blSerial.available()) {
                uint8_t b = (uint8_t)blSerial.read();
                if (b == BL_ACK || b == 0x79) {
                    Serial.println("[BL] Synced");
                    return true;
                }
            }
            delay(1);
        }
    }
    Serial.println("[BL] Sync failed");
    return false;
}

// Send a bootloader command (cmd + XOR)
static bool blSendCmd(uint8_t cmd) {
    blSerial.write(cmd);
    blSerial.write((uint8_t)(cmd ^ 0xFF));
    return blWaitAck();
}

// Send a 32-bit address + XOR checksum
static bool blSendAddr(uint32_t addr) {
    uint8_t b[5];
    b[0] = (uint8_t)(addr >> 24);
    b[1] = (uint8_t)(addr >> 16);
    b[2] = (uint8_t)(addr >>  8);
    b[3] = (uint8_t)(addr >>  0);
    b[4] = b[0] ^ b[1] ^ b[2] ^ b[3];
    blSerial.write(b, 5);
    return blWaitAck();
}

// Extended Erase — mass erase (0xFF 0xFF special case)
static bool blMassErase() {
    if (!blSendCmd(0x44)) return false;
    // Special: two 0xFF bytes = mass erase, checksum = 0x00
    uint8_t eraseMsg[3] = { 0xFF, 0xFF, 0x00 };
    blSerial.write(eraseMsg, 3);
    return blWaitAck(30000);   // mass erase can take up to ~30 s
}

// Write Memory command — 1–256 bytes at address
static bool blWriteChunk(uint32_t addr, const uint8_t *data, uint8_t count) {
    if (!blSendCmd(0x31)) return false;
    if (!blSendAddr(addr)) return false;

    uint8_t n = count - 1;   // length byte = (count - 1)
    uint8_t xorVal = n;
    blSerial.write(n);
    for (uint8_t i = 0; i < count; i++) {
        blSerial.write(data[i]);
        xorVal ^= data[i];
    }
    blSerial.write(xorVal);
    return blWaitAck(5000);
}

// Go command — jump to application entry point
static bool blGo(uint32_t addr) {
    if (!blSendCmd(0x21)) return false;
    return blSendAddr(addr);
}

// Flash binary already in RAM to STM32 via UART bootloader
static bool otaFlashSTM32(const uint8_t *bin, size_t len) {
    Serial.printf("[BL] Flashing %u bytes to STM32\n", len);

    // 1. Assert BOOT0 HIGH, pulse NRST to enter ROM bootloader mode
    pinMode(PIN_STM32_BOOT0, OUTPUT);
    pinMode(PIN_STM32_NRST,  OUTPUT);
    digitalWrite(PIN_STM32_NRST,  HIGH);
    digitalWrite(PIN_STM32_BOOT0, HIGH);   // BOOT0 HIGH = ROM bootloader
    delay(10);
    digitalWrite(PIN_STM32_NRST, LOW);    // assert reset
    delay(50);
    digitalWrite(PIN_STM32_NRST, HIGH);   // release reset
    delay(100);                            // bootloader startup time

    // 2. Open bootloader UART (8E1 as required by AN3155)
    blSerial.begin(STM32_BL_BAUD, SERIAL_8E1, PIN_STM32_BL_RX, PIN_STM32_BL_TX);
    delay(50);

    bool ok = false;

    // 3. Sync
    oledOtaProgress(2, "BL sync...");
    if (!blSync()) {
        Serial.println("[BL] Sync failed — check BOOT0/NRST wiring and USART1 connections");
        goto cleanup;
    }

    // 4. Mass erase
    Serial.println("[BL] Erasing flash (mass erase)...");
    oledOtaProgress(5, "Erasing...");
    if (!blMassErase()) {
        Serial.println("[BL] Mass erase failed");
        goto cleanup;
    }
    Serial.println("[BL] Erase done");

    // 5. Write in BL_PAGE-byte chunks
    {
        size_t offset = 0;
        while (offset < len) {
            uint8_t chunkSize = (len - offset >= BL_PAGE) ? BL_PAGE : (uint8_t)(len - offset);
            uint32_t addr = STM32_FLASH_BASE + (uint32_t)offset;

            if (!blWriteChunk(addr, bin + offset, chunkSize)) {
                Serial.printf("[BL] Write failed at offset %u (addr 0x%08X)\n", offset, addr);
                goto cleanup;
            }
            offset += chunkSize;

            // Progress update every 4 KB
            if ((offset % 4096) < BL_PAGE || offset >= len) {
                uint8_t pct = (uint8_t)(5 + (uint32_t)offset * 90 / len);
                Serial.printf("[BL] %u / %u bytes (%u%%)\n", offset, len, pct);
                oledOtaProgress(pct, "Writing STM32...");
            }
        }
    }

    // 6. Jump to app
    Serial.println("[BL] Go → 0x08000000");
    oledOtaProgress(98, "Starting STM32...");
    blGo(STM32_FLASH_BASE);
    ok = true;

cleanup:
    blSerial.end();

    // CRITICAL: Release GPIO32/33 back to tri-state INPUT so they do NOT
    // conflict with STM32 driving PA9/PA10 as motor-direction outputs during
    // normal operation.  blSerial.end() stops the UART peripheral but does
    // NOT change the GPIO mode on ESP32 Arduino.
    pinMode(PIN_STM32_BL_TX, INPUT);
    pinMode(PIN_STM32_BL_RX, INPUT);

    // 7. Release BOOT0 and reset STM32 to run the new firmware
    digitalWrite(PIN_STM32_BOOT0, LOW);
    delay(10);
    digitalWrite(PIN_STM32_NRST, LOW);
    delay(50);
    digitalWrite(PIN_STM32_NRST, HIGH);
    delay(200);

    // 8. Restore main UART to STM32 (USART2 / Serial2)
    Serial2.begin(STM32_BAUD, SERIAL_8N1, PIN_STM32_RX, PIN_STM32_TX);

    if (ok) {
        Serial.println("[BL] STM32 flash SUCCESS");
        oledOtaProgress(100, "STM32 Done!");
        buzzerBeepN(3, 80, 60);
    } else {
        Serial.println("[BL] STM32 flash FAILED");
        oledError("STM32 OTA Fail");
        buzzerBeepN(5, 50, 30);
    }
    return ok;
}

// Public: download .bin from HTTP URL and flash STM32
bool otaDownloadAndFlashSTM32(const char *url) {
    Serial.printf("[BL] Downloading from: %s\n", url);
    oledOtaProgress(0, "Downloading...");

    HTTPClient http;
    http.setTimeout(30000);

    if (!http.begin(url)) {
        Serial.println("[BL] HTTP begin failed");
        oledError("HTTP begin fail");
        return false;
    }

    int httpCode = http.GET();
    if (httpCode != 200) {
        Serial.printf("[BL] HTTP error %d\n", httpCode);
        http.end();
        oledError("HTTP error");
        return false;
    }

    int contentLen = http.getSize();
    if (contentLen <= 0 || contentLen > (int)STM32_MAX_BIN_SIZE) {
        Serial.printf("[BL] Bad content-length: %d\n", contentLen);
        http.end();
        oledError("Bad bin size");
        return false;
    }

    uint8_t *bin = (uint8_t *)malloc((size_t)contentLen);
    if (!bin) {
        Serial.println("[BL] malloc failed — not enough RAM");
        http.end();
        oledError("OTA: no RAM");
        return false;
    }

    // Stream download
    WiFiClient *stream = http.getStreamPtr();
    size_t received = 0;
    uint32_t lastLog = millis();

    while (received < (size_t)contentLen) {
        int avail = stream->available();
        if (avail > 0) {
            int toRead = min(avail, contentLen - (int)received);
            int n = stream->readBytes(bin + received, toRead);
            received += (size_t)n;
            if (millis() - lastLog > 500) {
                uint8_t pct = (uint8_t)((uint32_t)received * 45 / (uint32_t)contentLen);
                oledOtaProgress(pct, "Downloading...");
                lastLog = millis();
            }
        } else {
            delay(5);
        }
    }
    http.end();

    bool ok = false;
    if (received == (size_t)contentLen) {
        Serial.printf("[BL] Downloaded %u bytes — starting flash\n", received);
        ok = otaFlashSTM32(bin, received);
    } else {
        Serial.printf("[BL] Download incomplete: %u / %d\n", received, contentLen);
        oledError("Download fail");
    }

    free(bin);
    return ok;
}
