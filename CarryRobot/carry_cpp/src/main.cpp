// =========================================
// CARRY ROBOT - MAIN
// Hospital Transport Robot Firmware
// ESP32 + PN532 NFC + VL53L0X + SH1106 OLED
// MQTT Version — 10-state state machine
// =========================================

#include <Arduino.h>
#include <Wire.h>
#include <SPI.h>

#include "config.h"
#include "globals.h"
#include "hardware.h"
#include "mission.h"

// =========================================
// TIMING (local to main.cpp)
// =========================================
static unsigned long lastTofRead = 0;
static unsigned long localLastTelemetry = 0;

// =========================================
// OBSTACLE DETECTION
// =========================================
static void checkObstacle() {
  if (millis() - lastTofRead < TOF_INTERVAL) return;
  lastTofRead = millis();

  uint16_t dist;
  if (!tofReadDistance(dist)) return;

  if (dist < TOF_STOP_DIST && !obstacleHold) {
    obstacleHold = true;
    motorsStop();
  } else if (dist >= TOF_RESUME_DIST && obstacleHold) {
    obstacleHold = false;
    if (state == ST_OUTBOUND || state == ST_CANCEL || state == ST_BACK)
      driveForward(PWM_FWD);
  }
}

// =========================================
// NFC PROCESSING
// =========================================
static void processNFC() {
  // Đã bỏ chặn `if (obstacleHold) return;` để dù dừng xe vẫn test quét thẻ được
  if (!nfcAllowed()) return;

  uint8_t uid[7];
  uint8_t uidLen = 0;
  if (!readNFC(uid, &uidLen)) return;

  markNfcRead();
  beepOnce(60, 3000); // Kêu bíp 1 tiếng báo hiệu phần cứng đã nhận thẻ thành công

  String uidStr = "";
  for (int i = 0; i < uidLen; i++) {
    if (uid[i] < 0x10) uidStr += "0";
    uidStr += String(uid[i], HEX);
  }
  uidStr.toUpperCase();
  handleCheckpointHit(uidStr);
}

// =========================================
// PIN INITIALIZATION
// =========================================
static void initPins() {
  pinMode(FL_IN1, OUTPUT); pinMode(FL_IN2, OUTPUT);
  pinMode(FR_IN1, OUTPUT); pinMode(FR_IN2, OUTPUT);
  pinMode(RL_IN1, OUTPUT); pinMode(RL_IN2, OUTPUT);
  pinMode(RR_IN1, OUTPUT); pinMode(RR_IN2, OUTPUT);
  pinMode(EN_LEFT, OUTPUT);
  pinMode(EN_RIGHT, OUTPUT);
  pinMode(SW_PIN, INPUT_PULLUP);
  pinMode(BUZZER_PIN, OUTPUT);
  digitalWrite(BUZZER_PIN, LOW);
  Wire.begin(I2C_SDA, I2C_SCL);
  // Sửa NFC_SS thành -1 để thư viện Adafruit tự do điều khiển chân CS (Tránh xung đột phần cứng)
  SPI.begin(SPI_SCK, SPI_MISO, SPI_MOSI, -1); 
}

// =========================================
// SETUP
// =========================================
void setup() {
  Serial.begin(115200);
  delay(100);

  state = ST_BOOT;

  initPins();
  motorPwmInit();
  motorsStop();
  buzzerInit();
  displayInit();
  tofInit();
  nfcInit();

  // Load saved MQTT server from NVS
  prefs.begin("carrycfg", true);
  String savedMqtt = prefs.getString("mqtt_server", "");
  if (savedMqtt.length() > 0) {
    strncpy(mqttServer, savedMqtt.c_str(), sizeof(mqttServer) - 1);
    mqttServer[sizeof(mqttServer) - 1] = '\0';
  }
  prefs.end();

  // Portal or Auto-connect
  updateSW();
  delay(50);
  updateSW();

  if (swHeld()) {
    state = ST_PORTAL;
    oledDraw();
    wifiInit(true);
  } else {
    state = ST_CONNECTING;
    oledDraw();
    wifiInit(false);
  }

  if (!isWiFiConnected()) {
    oledDraw4("WIFI FAIL", "Restarting...", NULL, NULL);
    delay(2000);
    ESP.restart();
  }

  state = ST_CONNECTING;
  oledDraw();
  mqttInit();
  mqttReconnect();

#if SERIAL_DEBUG
  Serial.println("=== READY ===");
#endif
}

// =========================================
// MAIN LOOP
// =========================================
void loop() {
  unsigned long now = millis();

  mqttLoop();
  updateSW();

  if (now - lastOLED >= OLED_MS) {
    lastOLED = now;
    oledDraw();
  }

  if (now - localLastTelemetry >= TELEMETRY_INTERVAL) {
    localLastTelemetry = now;
    sendTelemetry();
  }

  processNFC();

  // ========= STATE MACHINE =========
  switch (state) {

    case ST_CONNECTING:
      if (isWiFiConnected() && isMqttConnected()) {
        goIdleReset();
        beepOnce(120, 2200);
      }
      break;

    case ST_IDLE:
      if (!isWiFiConnected() || !isMqttConnected()) {
        state = ST_CONNECTING;
        break;
      }
      motorsStop();
      toneOff();
      break;

    case ST_GET_MISSION:
      motorsStop();
      toneOff();
      if (activeMissionStatus == "cancelled") {
        sendReturned("cancelled_before_start");
        goIdleReset();
      }
      break;

    case ST_OUTBOUND:
    case ST_CANCEL:
      checkObstacle();

      if (obstacleHold) {
        motorsStop();
        if (now - lastObstacleBeep >= (unsigned long)OBSTACLE_BEEP_PERIOD_MS) {
          lastObstacleBeep = now;
          beepOnce(120, 1800);
        }
        break;
      }

      if (state == ST_OUTBOUND && !swHeld()) {
        motorsStop();
        if (now - lastObstacleBeep >= 900) {
          lastObstacleBeep = now;
          beepOnce(60, 1400);
        }
        break;
      }

      {
        const auto& route = currentRoute();
        if (route.size() < 2) { goIdleReset(); break; }
        if (expectedNextUid().length() == 0) { motorsStop(); break; }
      }

      driveForward(PWM_FWD);
      break;

    case ST_WAIT_AT_DEST:
      motorsStop();
      toneOff();

      if (activeMissionStatus == "cancelled") {
        startReturn("cancelled-at-bed", false);
        break;
      }

      if (!swHeld()) {
        beepArrivedPattern();
        sendComplete("ok");
        if (retRoute.size() < 2 && outbound.size() >= 2)
          buildReturnFromVisited();
        if (activeMissionStatus.length() == 0) activeMissionStatus = "completed";
        startReturn("return-after-unload", false);
      }
      break;

    case ST_BACK:
      checkObstacle();

      if (obstacleHold) {
        motorsStop();
        if (now - lastObstacleBeep >= (unsigned long)OBSTACLE_BEEP_PERIOD_MS) {
          lastObstacleBeep = now;
          beepOnce(120, 1800);
        }
        break;
      }

      {
        const auto& route = currentRoute();
        if (route.size() < 2) { goIdleReset(); break; }
        if (expectedNextUid().length() == 0) { motorsStop(); break; }
      }

      driveForward(PWM_FWD);
      break;

    case ST_WAIT_RETURN_ROUTE:
      motorsStop();
      toneOff();
      if (now - waitingReturnRouteStartTime > RETURN_ROUTE_TIMEOUT_MS) {
        buildReturnFromVisited();
        waitingForReturnRoute = false;
        state = ST_BACK;
        routeIndex = 0;
        obstacleHold = false;
        beepOnce(120, 1200);
      }
      break;

    default:
      break;
  }

  delay(5);
}