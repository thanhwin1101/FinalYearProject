#pragma once

// ============================================================
// UART Handler — Communication with User Manager ESP32
// ============================================================

void uartInit();
void handleUserUART();
void processUserCommand(const char* cmd);
void sendToUser(const char* msg);
void sendStepCount();
void sendBalanceStatus();
