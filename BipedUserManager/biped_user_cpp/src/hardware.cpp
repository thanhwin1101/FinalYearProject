/*
 * ============================================================
 * BIPED USER MANAGER — HARDWARE (Implementation)
 * ============================================================
 * RFID RC522, OLED SSD1306, Buttons, Encoder, UART, Display
 * ============================================================
 */

#include "hardware.h"
#include "config.h"
#include "globals.h"
#include "session.h"

// =========================================
// INITIALIZATION
// =========================================

void initPins() {
  // Buttons — INPUT_PULLUP (active LOW)
  pinMode(BTN_FORWARD_PIN,  INPUT_PULLUP);
  pinMode(BTN_BACKWARD_PIN, INPUT_PULLUP);
  pinMode(BTN_LEFT_PIN,     INPUT_PULLUP);
  pinMode(BTN_RIGHT_PIN,    INPUT_PULLUP);
  pinMode(BTN_STOP_PIN,     INPUT_PULLUP);

  // Encoder
  pinMode(ENC_CLK_PIN, INPUT);
  pinMode(ENC_DT_PIN,  INPUT);
  pinMode(ENC_SW_PIN,  INPUT_PULLUP);

  // LED
  pinMode(LED_STATUS_PIN, OUTPUT);
  digitalWrite(LED_STATUS_PIN, LOW);

  // Attach encoder interrupt
  attachInterrupt(digitalPinToInterrupt(ENC_CLK_PIN), encoderISR, FALLING);
}

void rfidInit() {
  SPI.begin(RFID_SCK_PIN, RFID_MISO_PIN, RFID_MOSI_PIN, RFID_SS_PIN);
  rfid.PCD_Init();

  byte v = rfid.PCD_ReadRegister(MFRC522::VersionReg);
  if (v == 0x00 || v == 0xFF) {
    Serial.println("[RFID] Reader NOT found!");
  } else {
    Serial.printf("[RFID] Reader v0x%02X\n", v);
  }
}

void oledInit() {
  oled.begin();
  oled.setFont(u8g2_font_6x10_tf);
}

void uartInit() {
  WalkingSerial.begin(UART_BAUD, SERIAL_8N1, UART_RX_PIN, UART_TX_PIN);
  Serial.println("[UART] Walking Controller UART initialized");
}

// =========================================
// RFID
// =========================================

bool rfidReadCard(String& uidOut) {
  if (!rfid.PICC_IsNewCardPresent()) return false;
  if (!rfid.PICC_ReadCardSerial())   return false;

  uidOut = uidToHexString(rfid.uid.uidByte, rfid.uid.size);

  rfid.PICC_HaltA();
  rfid.PCD_StopCrypto1();
  return true;
}

// =========================================
// BUTTONS
// =========================================

void handleButtons() {
  unsigned long now = millis();

  for (int i = 0; i < BTN_COUNT; i++) {
    bool reading = digitalRead(buttons[i].pin);

    if (reading != buttons[i].lastState) {
      buttons[i].lastDebounce = now;
    }

    if ((now - buttons[i].lastDebounce) > DEBOUNCE_MS) {
      if (reading == LOW && !buttons[i].pressed) {
        buttons[i].pressed = true;
        handleButtonPress(i);
      } else if (reading == HIGH && buttons[i].pressed) {
        handleButtonRelease(i);
        buttons[i].pressed = false;
      }
    }

    buttons[i].lastState = reading;
  }
}

void handleButtonPress(int idx) {
  // Forward khi IDLE → bỏ qua, dùng long-press để setup WiFi
  if (idx == BTN_IDX_FWD && currentState == STATE_IDLE) return;

  if (currentState != STATE_SESSION_ACTIVE) {
    Serial.println("[BTN] Không có session — bỏ qua");
    return;
  }

  const char* cmd = nullptr;

  switch (idx) {
    case BTN_IDX_FWD:
      cmd = CMD_FWD_STR;
      strcpy(lastCommand, "FORWARD");
      break;
    case BTN_IDX_BACK:
      cmd = CMD_BACK_STR;
      strcpy(lastCommand, "BACKWARD");
      break;
    case BTN_IDX_LEFT:
      cmd = CMD_LEFT_STR;
      strcpy(lastCommand, "LEFT");
      break;
    case BTN_IDX_RIGHT:
      cmd = CMD_RIGHT_STR;
      strcpy(lastCommand, "RIGHT");
      break;
    case BTN_IDX_STOP:
      cmd = CMD_STOP_STR;
      strcpy(lastCommand, "STOP");
      break;
  }

  if (cmd) {
    sendUartCommand(cmd);
    isMoving = (idx != BTN_IDX_STOP);
  }
}

void handleButtonRelease(int idx) {
  // Khi nhả nút di chuyển → gửi STOP
  if (idx >= BTN_IDX_FWD && idx <= BTN_IDX_RIGHT && isMoving) {
    sendUartCommand(CMD_STOP_STR);
    isMoving = false;
    lastCommand[0] = '\0';
  }
}

// Long-press Forward (IDLE) → mở WiFiManager
void checkForwardLongPress() {
  bool pressed = (digitalRead(BTN_FORWARD_PIN) == LOW);

  if (pressed) {
    if (forwardBtnPressStart == 0) {
      forwardBtnPressStart = millis();
      forwardLongPressTriggered = false;
    } else if (!forwardLongPressTriggered && currentState == STATE_IDLE) {
      unsigned long hold = millis() - forwardBtnPressStart;

      // Hiển thị progress bar sau 1s
      if (hold > 1000) {
        int pct = constrain(map(hold, 1000, WIFI_SETUP_HOLD_TIME, 0, 100), 0, 100);
        oled.clearBuffer();
        oled.setFont(u8g2_font_6x10_tf);
        oled.drawStr(10, 25, "Giu de setup WiFi");
        oled.drawFrame(10, 35, 108, 15);
        oled.drawBox(12, 37, (int)(104.0 * pct / 100), 11);
        char buf[8];
        snprintf(buf, sizeof(buf), "%d%%", pct);
        oled.drawStr(55, 60, buf);
        oled.sendBuffer();
      }

      if (hold >= WIFI_SETUP_HOLD_TIME) {
        forwardLongPressTriggered = true;
        Serial.println("[WIFI] Forward long-press → WiFi Manager");
        // WiFi manager sẽ được gọi từ mqtt_service
        extern void startWiFiPortal();
        startWiFiPortal();
      }
    }
  } else {
    forwardBtnPressStart = 0;
    forwardLongPressTriggered = false;
  }
}

// =========================================
// ENCODER
// =========================================

void IRAM_ATTR encoderISR() {
  if (digitalRead(ENC_DT_PIN) == HIGH) {
    encoderPos++;
  } else {
    encoderPos--;
  }
  int minPos = SPEED_MIN / SPEED_STEP;
  int maxPos = SPEED_MAX / SPEED_STEP;
  if (encoderPos < minPos) encoderPos = minPos;
  if (encoderPos > maxPos) encoderPos = maxPos;
}

void handleEncoder() {
  int pos = encoderPos;
  if (pos != lastEncoderPos) {
    lastEncoderPos = pos;
    currentSpeed = pos * SPEED_STEP;
    if (currentSpeed < SPEED_MIN) currentSpeed = SPEED_MIN;
    if (currentSpeed > SPEED_MAX) currentSpeed = SPEED_MAX;

    Serial.printf("[ENC] Speed: %d\n", currentSpeed);
    sendUartSpeed(currentSpeed);
  }
}

// =========================================
// UART — Walking Controller
// =========================================

void sendUartCommand(const char* cmd) {
  WalkingSerial.print(cmd);
  WalkingSerial.print('\n');
  Serial.printf("[UART] TX → %s\n", cmd);
}

void sendUartSpeed(uint8_t speed) {
  char buf[16];
  snprintf(buf, sizeof(buf), "%s%d", CMD_SPEED_PREFIX, speed);
  sendUartCommand(buf);
}

// Forward declaration
static void processWalkingMessage(const char* msg);

// Buffer nhận UART
static char uartBuf[64];
static int  uartBufIdx = 0;

void handleUartReceive() {
  while (WalkingSerial.available()) {
    char c = WalkingSerial.read();
    if (c == '\n' || c == '\r') {
      if (uartBufIdx > 0) {
        uartBuf[uartBufIdx] = '\0';
        processWalkingMessage(uartBuf);
        uartBufIdx = 0;
      }
    } else if (uartBufIdx < (int)sizeof(uartBuf) - 1) {
      uartBuf[uartBufIdx++] = c;
    }
  }
}

static void processWalkingMessage(const char* msg) {
  Serial.printf("[UART] RX ← %s\n", msg);

  // Parse "KEY:VALUE"
  if (strncmp(msg, MSG_STEP_PREFIX, strlen(MSG_STEP_PREFIX)) == 0) {
    uint32_t steps = atoi(msg + strlen(MSG_STEP_PREFIX));
    updateStepCount(steps);
  } else if (strncmp(msg, MSG_BALANCE_PREFIX, strlen(MSG_BALANCE_PREFIX)) == 0) {
    strncpy(balanceStatus, msg + strlen(MSG_BALANCE_PREFIX), sizeof(balanceStatus) - 1);
  } else if (strncmp(msg, MSG_ERROR_PREFIX, strlen(MSG_ERROR_PREFIX)) == 0) {
    Serial.printf("[UART] Walking Error: %s\n", msg + strlen(MSG_ERROR_PREFIX));
  } else if (strncmp(msg, MSG_STATUS_PREFIX, strlen(MSG_STATUS_PREFIX)) == 0) {
    Serial.printf("[UART] Walking Status: %s\n", msg + strlen(MSG_STATUS_PREFIX));
  }
}

// =========================================
// DISPLAY FUNCTIONS
// =========================================

void displayInit() {
  oled.clearBuffer();
  oled.setFont(u8g2_font_7x14B_tf);
  oled.drawStr(20, 30, STR_TITLE);
  oled.setFont(u8g2_font_6x10_tf);
  oled.drawStr(15, 50, "Khoi dong...");
  oled.sendBuffer();
}

void displayIdle() {
  oled.clearBuffer();

  oled.setFont(u8g2_font_7x14B_tf);
  oled.drawStr(15, 15, STR_TITLE);
  oled.drawHLine(0, 20, 128);

  oled.setFont(u8g2_font_6x10_tf);
  oled.drawStr(15, 38, STR_READY);
  oled.drawStr(5, 55, STR_SCAN_CARD);

  // WiFi/MQTT status indicator
  if (mqttConnected) {
    oled.drawStr(100, 10, "MQTT");
  } else if (wifiOk) {
    oled.drawStr(100, 10, "WiFi");
  }

  oled.sendBuffer();
}

void displaySession() {
  oled.clearBuffer();

  // === Tên bệnh nhân (dòng trên, canh giữa) ===
  oled.setFont(u8g2_font_7x14B_tf);
  char truncName[18];
  strncpy(truncName, session.userName, 17);
  truncName[17] = '\0';
  int nameW = oled.getStrWidth(truncName);
  oled.drawStr((128 - nameW) / 2, 15, truncName);

  oled.drawHLine(0, 20, 128);

  // === Số bước (font lớn, giữa màn hình) ===
  oled.setFont(u8g2_font_logisoso28_tn);
  char steps[12];
  snprintf(steps, sizeof(steps), "%lu", (unsigned long)session.stepCount);
  int stepsW = oled.getStrWidth(steps);
  oled.drawStr((128 - stepsW) / 2, 52, steps);

  // Label "buoc"
  oled.setFont(u8g2_font_6x10_tf);
  oled.drawStr(52, 63, STR_STEPS_LABEL);

  oled.sendBuffer();
}

void displayConnecting() {
  oled.clearBuffer();
  oled.setFont(u8g2_font_7x14B_tf);
  oled.drawStr(15, 25, STR_TITLE);
  oled.setFont(u8g2_font_6x10_tf);
  oled.drawStr(20, 45, STR_CONNECTING);
  oled.sendBuffer();
}

void displayWiFiSetup() {
  oled.clearBuffer();
  oled.setFont(u8g2_font_7x14B_tf);
  oled.drawStr(10, 15, "WIFI SETUP");
  oled.drawHLine(0, 20, 128);
  oled.setFont(u8g2_font_6x10_tf);
  oled.drawStr(0, 35, "1. Ket noi WiFi:");
  oled.drawStr(10, 47, WIFI_PORTAL_SSID);
  oled.drawStr(0, 60, "2. Vao 192.168.4.1");
  oled.sendBuffer();
}

void displayError(const char* msg) {
  oled.clearBuffer();
  oled.setFont(u8g2_font_7x14B_tf);
  oled.drawStr(40, 25, "LOI!");
  oled.setFont(u8g2_font_6x10_tf);
  oled.drawStr(10, 45, msg);
  oled.sendBuffer();
}

void displayChecking() {
  oled.clearBuffer();
  oled.setFont(u8g2_font_6x10_tf);
  oled.drawStr(20, 30, STR_CHECKING);
  oled.sendBuffer();
}

void displayCardInvalid() {
  oled.clearBuffer();
  oled.setFont(u8g2_font_7x14B_tf);
  oled.drawStr(5, 25, STR_INVALID_CARD);
  oled.setFont(u8g2_font_6x10_tf);
  oled.drawStr(5, 45, STR_CARD_NOT_REG);
  oled.drawStr(5, 57, STR_CONTACT_STAFF);
  oled.sendBuffer();
}

void displaySessionEnd() {
  oled.clearBuffer();
  oled.setFont(u8g2_font_7x14B_tf);
  oled.drawStr(10, 30, STR_END_SESSION);
  oled.setFont(u8g2_font_6x10_tf);
  oled.drawStr(5, 50, STR_GOODBYE);
  oled.sendBuffer();
}

void displayWiFiOk() {
  oled.clearBuffer();
  oled.setFont(u8g2_font_7x14B_tf);
  oled.drawStr(20, 30, "WiFi OK!");
  oled.setFont(u8g2_font_6x10_tf);
  oled.drawStr(10, 50, WiFi.localIP().toString().c_str());
  oled.sendBuffer();
}

void displayWiFiFail() {
  oled.clearBuffer();
  oled.setFont(u8g2_font_7x14B_tf);
  oled.drawStr(10, 30, "WiFi FAIL!");
  oled.setFont(u8g2_font_6x10_tf);
  oled.drawStr(5, 50, "Giu TIEN 3s de thu lai");
  oled.sendBuffer();
}

void updateDisplay() {
  switch (currentState) {
    case STATE_IDLE:           displayIdle();    break;
    case STATE_SESSION_ACTIVE: displaySession(); break;
    case STATE_CONNECTING:     displayConnecting(); break;
    case STATE_ERROR:          displayError("System error"); break;
    default: break;
  }
}
