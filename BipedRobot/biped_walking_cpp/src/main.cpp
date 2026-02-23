// --- Ghi đè vào biped_walking_cpp/src/main.cpp ---
#include <Arduino.h>
#include <Wire.h>
#include "globals.h"
#include "imu_balance.h"
#include "uart_handler.h"
#include "fsr_handler.h"    // THÊM DÒNG NÀY
#include "gait_generator.h" // THÊM DÒNG NÀY

void setup() {
  Serial.begin(115200);
  Wire.begin();
  uartInit();
  servoCtrl.begin();

  fsrInit();     // KHỞI TẠO CẢM BIẾN LỰC
  gaitInit();    // KHỞI TẠO DÁNG ĐI

  standStraight();

  if (!initIMU()) {
    sendToUser("ERROR:IMU_FAIL");
    while (1) delay(10);
  }

  calibrateReferenceAngles();
  sendToUser("STATUS:READY");
  sendBalanceStatus();

  balanceStartMs = millis();
  lastMs = millis();
  lastStepSendMs = millis();
}

void loop() {
  unsigned long now = millis();
  handleUserUART();

  if (now - lastStepSendMs >= STEP_SEND_INTERVAL) {
    lastStepSendMs = now;
    sendStepCount();
  }

  if (now - lastMs < UPDATE_INTERVAL_MS) return;
  float dt = (now - lastMs) / 1000.0f;
  lastMs = now;

  if (balanceEnabled) {
    updateControl(dt); // Hàm này giờ xử lý cả đi bộ, IMU và FSR
  }
}