#pragma once
#include <stdint.h>
#include <stddef.h>

// ── ESP32 ArduinoOTA ────────────────────────────────────────────────
// Call otaInit() once after WiFi is connected (in setup).
// Call otaLoop() every loop iteration.
void otaInit();
void otaLoop();

// ── STM32 UART Bootloader flash ─────────────────────────────────────
// Download a .bin file from HTTP URL and flash it to STM32 via
// the AN3155 ROM bootloader (BOOT0 pin + USART1).
// Returns true on success.
bool otaDownloadAndFlashSTM32(const char *url);
