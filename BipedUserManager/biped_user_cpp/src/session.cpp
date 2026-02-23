/*
 * ============================================================
 * BIPED USER MANAGER — SESSION (Implementation)
 * ============================================================
 * Workflow:
 *   1. RFID scan → thẻ checkpoint? → báo vị trí
 *   2. RFID scan → thẻ người dùng (khi IDLE) → bắt đầu session
 *      - Gửi MQTT session/start + UART "BALANCE:ON"
 *      - Backend trả sessionId qua session/ack topic
 *   3. RFID scan → cùng thẻ (khi SESSION) → kết thúc session
 *      - Gửi MQTT session/end + UART "STOP" + "BALANCE:OFF"
 *   4. Mỗi 2s gửi MQTT session/update với step count
 *   5. Mỗi 5s gửi MQTT telemetry (heartbeat)
 * ============================================================
 */

#include "session.h"
#include "config.h"
#include "globals.h"
#include "hardware.h"
#include "mqtt_service.h"

// =========================================
// Stop button long-press tracking
// =========================================
static unsigned long stopPressStart = 0;
static bool stopLongPressTriggered = false;

// =========================================
// RFID HANDLING
// =========================================

void handleRFID() {
  String uidStr;
  if (!rfidReadCard(uidStr)) return;

  Serial.printf("[RFID] UID: %s\n", uidStr.c_str());

  // 1. Kiểm tra checkpoint trước
  const CheckpointEntry* cp = findCheckpoint(rfid.uid.uidByte);
  if (cp) {
    handleCheckpointCard(cp);
    return;
  }

  // 2. Thẻ người dùng
  handlePatientCard(uidStr);
}

void handleCheckpointCard(const CheckpointEntry* cp) {
  Serial.printf("[RFID] Checkpoint: %s - %s\n", cp->checkpointId, cp->description);
  strncpy(currentCheckpoint, cp->checkpointId, sizeof(currentCheckpoint) - 1);

  // Gửi checkpoint qua MQTT
  mqttSendCheckpoint(cp->checkpointId);
  buzzerBeep(60, 2200);
}

void handlePatientCard(const String& cardUid) {
  // === ĐANG CÓ SESSION ===
  if (currentState == STATE_SESSION_ACTIVE) {
    if (cardUid.equals(session.cardUid)) {
      // Cùng thẻ → Kết thúc session
      Serial.println("[SESSION] Same card → ending session");
      endSession("completed");

      displaySessionEnd();
      buzzerBeep(100, 1800);
      delay(2000);
      return;
    } else {
      // Thẻ khác → bỏ qua
      Serial.println("[SESSION] Different card — ignored");
      buzzerBeep(40, 800); // beep thấp báo lỗi
      return;
    }
  }

  // === IDLE → Bắt đầu session mới ===
  Serial.printf("[SESSION] Card %s → starting session\n", cardUid.c_str());
  startSession(cardUid);
}

// =========================================
// SESSION MANAGEMENT
// =========================================

void startSession(const String& cardUid) {
  // Reset session data
  memset(&session, 0, sizeof(session));
  strncpy(session.cardUid, cardUid.c_str(), sizeof(session.cardUid) - 1);

  // Tạo sessionId tạm (backend sẽ gửi lại qua ACK nếu cần)
  snprintf(session.sessionId, sizeof(session.sessionId), "BIPED-%08lX", (unsigned long)millis());

  // Gán userName = cardUid tạm thời (backend có thể lookup và gửi lại tên qua ACK)
  snprintf(session.userName, sizeof(session.userName), "Card %s", cardUid.c_str());
  
  // Copy user info nếu đã có (từ lần trước hoặc từ backend ACK)
  strncpy(currentUser.cardUid, cardUid.c_str(), sizeof(currentUser.cardUid) - 1);
  currentUser.isValid = true;

  session.stepCount = 0;
  session.startTime = millis();
  session.isActive  = true;

  currentState = STATE_SESSION_ACTIVE;
  digitalWrite(LED_STATUS_PIN, HIGH);

  // Gửi MQTT session start → backend sẽ:
  //   1. Lookup patient by cardNumber
  //   2. Tạo BipedSession document
  //   3. Trả sessionId + userName qua session/ack
  mqttSendSessionStart();

  // UART → Walking Controller: bật balance
  sendUartCommand(CMD_BALANCE_ON);

  buzzerBeep(120, 2200);
  Serial.printf("[SESSION] Started: %s\n", session.sessionId);
}

void endSession(const char* status) {
  Serial.printf("[SESSION] Ending: %s (steps=%lu)\n", status, (unsigned long)session.stepCount);

  // UART → Walking Controller: dừng + tắt balance
  sendUartCommand(CMD_STOP_STR);
  sendUartCommand(CMD_BALANCE_OFF);
  isMoving = false;

  // Gửi MQTT session end
  mqttSendSessionEnd(status);

  // Reset state
  session.isActive = false;
  memset(&session, 0, sizeof(session));
  memset(&currentUser, 0, sizeof(currentUser));

  currentState = STATE_IDLE;
  digitalWrite(LED_STATUS_PIN, LOW);
  lastCommand[0] = '\0';

  Serial.println("[SESSION] Ended");
}

void endSessionLocal() {
  // Kết thúc session mà không gửi MQTT (dùng khi backend reject)
  sendUartCommand(CMD_STOP_STR);
  sendUartCommand(CMD_BALANCE_OFF);
  isMoving = false;

  session.isActive = false;
  memset(&session, 0, sizeof(session));
  memset(&currentUser, 0, sizeof(currentUser));

  currentState = STATE_IDLE;
  digitalWrite(LED_STATUS_PIN, LOW);
  lastCommand[0] = '\0';
}

void updateStepCount(uint32_t steps) {
  if (session.isActive && steps != session.stepCount) {
    session.stepCount = steps;
    Serial.printf("[SESSION] Steps: %lu\n", (unsigned long)steps);
  }
}

// =========================================
// PERIODIC TASKS
// =========================================

void handleSessionTasks() {
  unsigned long now = millis();

  // Telemetry / heartbeat mỗi 5s
  if (now - lastTelemetry >= TELEMETRY_INTERVAL) {
    lastTelemetry = now;
    mqttSendTelemetry();
  }

  // Step update mỗi 2s (chỉ khi session active)
  if (session.isActive && (now - lastStepUpdate >= STEP_UPDATE_INTERVAL)) {
    lastStepUpdate = now;
    mqttSendSessionUpdate();
  }
}

// =========================================
// STOP LONG-PRESS → END SESSION
// =========================================

void checkStopLongPress() {
  if (currentState != STATE_SESSION_ACTIVE) {
    stopPressStart = 0;
    stopLongPressTriggered = false;
    return;
  }

  bool pressed = (digitalRead(BTN_STOP_PIN) == LOW);

  if (pressed) {
    if (stopPressStart == 0) {
      stopPressStart = millis();
      stopLongPressTriggered = false;
    } else if (!stopLongPressTriggered) {
      unsigned long hold = millis() - stopPressStart;

      // Hiển thị progress trên OLED
      if (hold > 500) {
        int pct = constrain(map(hold, 500, SESSION_END_HOLD_TIME, 0, 100), 0, 100);
        oled.clearBuffer();
        oled.setFont(u8g2_font_6x10_tf);
        oled.drawStr(10, 20, "Giu de ket thuc...");
        oled.drawFrame(10, 30, 108, 12);
        oled.drawBox(12, 32, (int)(104.0 * pct / 100), 8);
        char buf[8];
        snprintf(buf, sizeof(buf), "%d%%", pct);
        oled.drawStr(55, 55, buf);
        oled.sendBuffer();
      }

      if (hold >= SESSION_END_HOLD_TIME) {
        stopLongPressTriggered = true;
        Serial.println("[SESSION] Stop long-press → ending session");
        endSession("completed");
        displaySessionEnd();
        buzzerBeep(100, 1800);
        delay(2000);
      }
    }
  } else {
    stopPressStart = 0;
    stopLongPressTriggered = false;
  }
}
