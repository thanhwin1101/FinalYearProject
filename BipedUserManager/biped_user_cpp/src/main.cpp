/*
 * ============================================================
 * BIPED USER MANAGER — MAIN
 * ============================================================
 * ESP32 quản lý người dùng cho Biped Robot
 * 
 * Chức năng chính:
 *   - RFID RC522: Quét thẻ đăng nhập/đăng xuất session
 *   - OLED 0.96" I2C: Hiển thị tên user + số bước chân
 *   - 4 Buttons: Forward, Backward, Left, Right → UART to Walking Controller
 *   - Rotary Encoder: Điều chỉnh tốc độ di chuyển
 *   - MQTT: Giao tiếp 2 chiều với Hospital Dashboard
 *   - UART: Giao tiếp với Walking Controller ESP32
 * 
 * Luồng hoạt động:
 *   1. Boot → init hardware → kết nối WiFi → kết nối MQTT
 *   2. IDLE: Hiển thị "Sẵn sàng" → chờ quét thẻ
 *   3. Quét thẻ RFID → gửi MQTT session/start → backend ACK
 *   4. SESSION: Hiển thị tên + số bước → nút bấm điều khiển
 *   5. Walking Controller gửi STEP:xxx qua UART → cập nhật
 *   6. Mỗi 2s gửi MQTT session/update (step count)
 *   7. Mỗi 5s gửi MQTT telemetry (heartbeat)
 *   8. Quét thẻ lần 2 (cùng UID) → gửi MQTT session/end
 * 
 * Giao tiếp:
 *   MQTT Broker ←→ Dashboard Backend ←→ Frontend
 *   ESP32 User Manager ←UART→ ESP32 Walking Controller
 * ============================================================
 */

#include <Arduino.h>
#include <Wire.h>
#include <SPI.h>

#include "config.h"
#include "globals.h"
#include "hardware.h"
#include "mqtt_service.h"
#include "session.h"

// =========================================
// SETUP
// =========================================
void setup() {
  Serial.begin(115200);
  delay(100);

  Serial.println("\n========================================");
  Serial.println("  BIPED ROBOT — USER MANAGER (MQTT)");
  Serial.println("========================================");

  currentState = STATE_BOOT;

  // 1. Init GPIO
  initPins();
  buzzerInit();

  // 2. Init OLED → splash screen
  Wire.begin(OLED_SDA_PIN, OLED_SCL_PIN);
  oledInit();
  displayInit();

  // 3. Init RFID
  rfidInit();

  // 4. Init UART to Walking Controller
  uartInit();

  // 5. Load saved MQTT server từ NVS
  prefs.begin("biped", true);
  String savedMqtt = prefs.getString("mqtt_server", "");
  if (savedMqtt.length() > 0) {
    strncpy(mqttServer, savedMqtt.c_str(), sizeof(mqttServer) - 1);
    mqttServer[sizeof(mqttServer) - 1] = '\0';
  }
  prefs.end();

  // 6. Kiểm tra nút Forward giữ khi boot → mở WiFi portal
  if (digitalRead(BTN_FORWARD_PIN) == LOW) {
    Serial.println("[BOOT] Forward held → WiFi Manager");
    currentState = STATE_PORTAL;
    wifiInit(true);
  } else {
    currentState = STATE_CONNECTING;
    displayConnecting();
    wifiInit(false);
  }

  // 7. Kiểm tra kết nối WiFi
  if (!isWiFiConnected()) {
    displayWiFiFail();
    delay(2000);
    // Vẫn tiếp tục hoạt động offline
    Serial.println("[BOOT] WiFi failed — running offline");
  } else {
    displayWiFiOk();
    delay(1000);
  }

  // 8. Init MQTT
  mqttBuildTopics();
  mqttInit();
  if (isWiFiConnected()) {
    mqttReconnect();
  }

  // 9. Reset session data
  memset(&session, 0, sizeof(session));
  memset(&currentUser, 0, sizeof(currentUser));

  // 10. Set IDLE state
  currentState = STATE_IDLE;

  buzzerBeep(120, 2200); // Boot complete beep
  Serial.println("[BOOT] Setup complete!\n");
}

// =========================================
// MAIN LOOP
// =========================================
void loop() {
  unsigned long now = millis();

  // 1. MQTT loop (reconnect + process messages)
  mqttLoop();

  // 2. RFID scan (mỗi 500ms)
  if (now - lastRfidScan >= RFID_SCAN_INTERVAL) {
    lastRfidScan = now;
    handleRFID();
  }

  // 3. Check Forward long-press (WiFi setup)
  checkForwardLongPress();

  // 4. Handle buttons (di chuyển)
  handleButtons();

  // 5. Handle encoder (tốc độ)
  handleEncoder();

  // 6. Handle UART from Walking Controller
  handleUartReceive();

  // 7. Check Stop long-press (kết thúc session)
  checkStopLongPress();

  // 8. Periodic tasks (telemetry + step update)
  handleSessionTasks();

  // 9. Update display (mỗi 200ms)
  if (now - lastDisplayUpdate >= DISPLAY_UPDATE_INTERVAL) {
    lastDisplayUpdate = now;
    updateDisplay();
  }

  // 10. WiFi reconnect check
  if (!isWiFiConnected()) {
    wifiOk = false;
    static unsigned long lastWifiRetry = 0;
    if (now - lastWifiRetry >= WIFI_RECONNECT_INTERVAL) {
      lastWifiRetry = now;
      Serial.println("[WIFI] Disconnected — reconnecting...");
      WiFi.reconnect();
    }
  } else {
    wifiOk = true;
  }

  delay(5); // Yield
}
