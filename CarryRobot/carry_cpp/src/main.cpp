// =========================================
// CARRY ROBOT - MAIN
// Hospital Transport Robot Firmware
// ESP32 + PN532 NFC + VL53L0X + SH1106 OLED
// MQTT Version
// =========================================

#include <Arduino.h>
#include <Wire.h>
#include <SPI.h>
#include <algorithm>

#include "config.h"
#include "globals.h"
#include "motor_control.h"
#include "sensors.h"
#include "uid_lookup.h"
#include "route_logic.h"
#include "display.h"
#include "communication.h"
#include "state_machine.h"
#include "helpers.h"

// =========================================
// TIMING VARIABLES (local to main.cpp)
// =========================================
static unsigned long lastNfcRead = 0;
static unsigned long lastTofRead = 0;
static unsigned long localLastTelemetry = 0;

// =========================================
// OBSTACLE DETECTION
// =========================================
void checkObstacle() {
  if (millis() - lastTofRead >= TOF_INTERVAL) {
    lastTofRead = millis();
    
    uint16_t dist;
    if (tofReadDistance(dist)) {
      if (dist < TOF_STOP_DIST && !obstacleHold) {
        obstacleHold = true;
        motorsStop();
#if SERIAL_DEBUG
        Serial.print("Obstacle! dist="); Serial.println(dist);
#endif
      } else if (dist >= TOF_RESUME_DIST && obstacleHold) {
        obstacleHold = false;
#if SERIAL_DEBUG
        Serial.println("Obstacle cleared");
#endif
        // Resume movement if in transit
        if (state == RUN_OUTBOUND || state == RUN_RETURN) {
          driveForward(PWM_FWD);
        }
      }
    }
  }
}

// =========================================
// NFC CHECKPOINT PROCESSING
// =========================================
void processNFC() {
  if (obstacleHold) return;
  if (!isNfcReady()) return;
  
  uint8_t uid[7];
  uint8_t uidLen = 0;
  
  if (readNFC(uid, &uidLen)) {
    markNfcRead();
    
    // Convert UID to hex string
    String uidStr = "";
    for (int i = 0; i < uidLen; i++) {
      if (uid[i] < 0x10) uidStr += "0";
      uidStr += String(uid[i], HEX);
    }
    uidStr.toUpperCase();
    
#if SERIAL_DEBUG
    Serial.print("NFC UID: "); Serial.println(uidStr);
#endif
    
    handleCheckpointHit(uidStr);
  }
}

// =========================================
// CARGO BUTTON CHECK
// =========================================
void checkCargoButton() {
  if (state == WAIT_AT_DEST) {
    if (isCargoLoaded()) {
      delay(50);  // Debounce
      if (isCargoLoaded()) {
        beepOnce(120, 2200);
        startReturn("button_confirm", true);
      }
    }
  }
}

// =========================================
// WAIT FOR RETURN ROUTE TIMEOUT
// =========================================
void checkReturnRouteTimeout() {
  if (state == WAIT_FOR_RETURN_ROUTE && waitingForReturnRoute) {
    if (millis() - waitingReturnRouteStartTime > RETURN_ROUTE_TIMEOUT_MS) {
#if SERIAL_DEBUG
      Serial.println("Return route timeout - building from visited");
#endif
      waitingForReturnRoute = false;
      
      // Fallback: reverse outbound route
      retRoute = outbound;
      std::reverse(retRoute.begin(), retRoute.end());
      
      startReturn("timeout_fallback", false);
    }
  }
}

// =========================================
// TELEMETRY
// =========================================
void sendPeriodicTelemetry() {
  if (millis() - localLastTelemetry < TELEMETRY_INTERVAL) return;
  localLastTelemetry = millis();
  
  sendTelemetry();
}

// =========================================
// PIN INITIALIZATION
// =========================================
void initPins() {
  // Motor direction pins
  pinMode(FL_IN1, OUTPUT); pinMode(FL_IN2, OUTPUT);
  pinMode(FR_IN1, OUTPUT); pinMode(FR_IN2, OUTPUT);
  pinMode(RL_IN1, OUTPUT); pinMode(RL_IN2, OUTPUT);
  pinMode(RR_IN1, OUTPUT); pinMode(RR_IN2, OUTPUT);
  
  // PWM enable pins
  pinMode(EN_LEFT, OUTPUT);
  pinMode(EN_RIGHT, OUTPUT);
  
  // Cargo button
  pinMode(CARGO_BTN, INPUT_PULLUP);
  
  // Buzzer
  pinMode(BUZZER_PIN, OUTPUT);
  digitalWrite(BUZZER_PIN, LOW);
  
  // I2C
  Wire.begin(I2C_SDA, I2C_SCL);
  
  // SPI for NFC
  SPI.begin(SPI_SCK, SPI_MISO, SPI_MOSI, NFC_SS);
}

// =========================================
// SETUP
// =========================================
void setup() {
  Serial.begin(115200);
  delay(100);
  
#if SERIAL_DEBUG
  Serial.println("\n=== CARRY ROBOT STARTING (MQTT) ===");
#endif
  
  // Initialize hardware
  initPins();
  motorPwmInit();
  motorsStop();
  
  // Display
  displayInit();
  drawCentered("CARRY ROBOT", "Starting...", "", "");
  
  // Sensors
  nfcInit();
  tofInit();
  
  // WiFi + MQTT
  drawCentered("CARRY ROBOT", "Connecting", "WiFi + MQTT", "");
  mqttInit();
  
  // Initial state
  goIdleReset();
  
  beepOnce(120, 2200);
  
#if SERIAL_DEBUG
  Serial.println("=== READY ===");
#endif
}

// =========================================
// MAIN LOOP
// =========================================
void loop() {
  // MQTT processing
  mqttLoop();
  
  // Check obstacle (highest priority)
  checkObstacle();
  
  // Process NFC checkpoints
  processNFC();
  
  // Check cargo button
  checkCargoButton();
  
  // Check return route timeout
  checkReturnRouteTimeout();
  
  // Send telemetry
  sendPeriodicTelemetry();
  
  // Small delay for stability
  delay(5);
}
