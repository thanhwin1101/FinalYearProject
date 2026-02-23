/*
 * ============================================================
 * BIPED USER MANAGER — HARDWARE (Header)
 * ============================================================
 * RFID, OLED, Buttons, Encoder, UART, Buzzer functions
 * ============================================================
 */

#ifndef HARDWARE_H
#define HARDWARE_H

#include <Arduino.h>

// =========================================
// INITIALIZATION
// =========================================
void initPins();
void rfidInit();
void oledInit();
void uartInit();

// =========================================
// RFID
// =========================================
bool rfidReadCard(String& uidOut);

// =========================================
// BUTTONS
// =========================================
void handleButtons();
void handleButtonPress(int idx);
void handleButtonRelease(int idx);
void checkForwardLongPress();

// =========================================
// ENCODER
// =========================================
void IRAM_ATTR encoderISR();
void handleEncoder();

// =========================================
// UART — Walking Controller
// =========================================
void sendUartCommand(const char* cmd);
void sendUartSpeed(uint8_t speed);
void handleUartReceive();

// =========================================
// DISPLAY HELPERS
// =========================================
void displayInit();      // Splash screen
void displayIdle();
void displaySession();
void displayConnecting();
void displayWiFiSetup();
void displayError(const char* msg);
void displayChecking();
void displayCardInvalid();
void displaySessionEnd();
void displayWiFiOk();
void displayWiFiFail();
void updateDisplay();

#endif // HARDWARE_H
