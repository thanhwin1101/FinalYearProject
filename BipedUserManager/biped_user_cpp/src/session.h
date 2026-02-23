/*
 * ============================================================
 * BIPED USER MANAGER — SESSION (Header)
 * ============================================================
 * Quản lý session tập luyện:
 *  - RFID scan → đăng nhập / đăng xuất
 *  - Đếm bước chân qua UART từ Walking Controller
 *  - Gửi session data lên Dashboard qua MQTT
 * ============================================================
 */

#ifndef SESSION_H
#define SESSION_H

#include <Arduino.h>
#include "config.h"

// RFID handling
void handleRFID();
void handlePatientCard(const String& cardUid);
void handleCheckpointCard(const CheckpointEntry* cp);

// Session management
void startSession(const String& cardUid);
void endSession(const char* status);
void endSessionLocal();     // Kết thúc session mà không gửi MQTT
void updateStepCount(uint32_t steps);

// Periodic tasks
void handleSessionTasks();  // Gửi step update, telemetry

// Stop button long-press → end session
void checkStopLongPress();

#endif // SESSION_H
