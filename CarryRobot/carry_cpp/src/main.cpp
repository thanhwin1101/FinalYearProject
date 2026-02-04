// =========================================
// CARRY ROBOT - MAIN
// Hospital Transport Robot Firmware
// ESP32 + PN532 NFC + VL53L0X + SH1106 OLED
// =========================================

#include <Arduino.h>
#include <Wire.h>
#include <SPI.h>

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
// TIMING VARIABLES
// =========================================
unsigned long lastNfcRead = 0;
unsigned long lastTofRead = 0;
unsigned long lastTelemetry = 0;
unsigned long lastMissionPoll = 0;
unsigned long waitStartTime = 0;

// =========================================
// OBSTACLE DETECTION
// =========================================
bool obstacleDetected = false;
uint16_t currentDistance = 9999;

void checkObstacle() {
  if (millis() - lastTofRead >= TOF_INTERVAL) {
    lastTofRead = millis();
    
    uint16_t dist;
    if (tofReadDistance(dist)) {
      currentDistance = dist;
      
      if (dist < TOF_STOP_DIST && !obstacleDetected) {
        obstacleDetected = true;
        motorsStop();
#if SERIAL_DEBUG
        Serial.print("Obstacle! dist="); Serial.println(dist);
#endif
      } else if (dist >= TOF_RESUME_DIST && obstacleDetected) {
        obstacleDetected = false;
#if SERIAL_DEBUG
        Serial.println("Obstacle cleared");
#endif
        // Resume movement if in transit
        if (robotState == STATE_RUN_OUTBOUND || robotState == STATE_RUN_RETURN) {
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
  if (obstacleDetected) return;
  if (!isNfcReady()) return;
  
  uint8_t uid[7];
  uint8_t uidLen = 0;
  
  if (readNFC(uid, &uidLen)) {
    const char* nodeName = uidLookupByNodeId(uid, uidLen);
    
    if (nodeName) {
      markNfcRead();
      applyForwardBrake(PWM_BRAKE, BRAKE_FORWARD_MS);  // Active brake on checkpoint
      handleCheckpointHit(nodeName);
    }
#if SERIAL_DEBUG
    else {
      Serial.print("Unknown UID: ");
      for (int i = 0; i < uidLen; i++) {
        Serial.print(uid[i], HEX);
        Serial.print(" ");
      }
      Serial.println();
    }
#endif
  }
}

// =========================================
// CARGO BUTTON CHECK
// =========================================
void checkCargoButton() {
  if (robotState == STATE_WAIT_CARGO) {
    if (isCargoLoaded()) {
      delay(50);  // Debounce
      if (isCargoLoaded()) {
        buzzOK();
        startOutbound();
      }
    }
  } else if (robotState == STATE_WAIT_AT_DEST) {
    if (isCargoLoaded()) {
      delay(50);
      if (isCargoLoaded()) {
        buzzOK();
        startReturn();
      }
    }
  }
}

// =========================================
// MISSION POLLING
// =========================================
void pollMission() {
  if (robotState != STATE_IDLE_AT_MED) return;
  if (millis() - lastMissionPoll < MISSION_POLL_INTERVAL) return;
  
  lastMissionPoll = millis();
  
  if (fetchNextMission()) {
#if SERIAL_DEBUG
    Serial.print("New mission: "); Serial.println(currentMissionId);
    Serial.print("Destination: "); Serial.println(missionDestBed);
#endif
    buzzOK();
    enterWaitCargo();
  }
}

// =========================================
// TELEMETRY
// =========================================
void sendPeriodicTelemetry() {
  if (millis() - lastTelemetry < TELEMETRY_INTERVAL) return;
  lastTelemetry = millis();
  
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
// LOAD SAVED CONFIG
// =========================================
void loadConfig() {
  prefs.begin("carry", true);  // Read-only
  String savedUrl = prefs.getString("apibase", DEFAULT_API_BASE);
  strncpy(apiBaseUrl, savedUrl.c_str(), sizeof(apiBaseUrl) - 1);
  apiBaseUrl[sizeof(apiBaseUrl) - 1] = '\0';
  prefs.end();
  
#if SERIAL_DEBUG
  Serial.print("Loaded API Base: "); Serial.println(apiBaseUrl);
#endif
}

// =========================================
// SETUP
// =========================================
void setup() {
  Serial.begin(115200);
  delay(100);
  
#if SERIAL_DEBUG
  Serial.println("\n=== CARRY ROBOT STARTING ===");
#endif
  
  // Initialize hardware
  initPins();
  motorPwmInit();
  motorsStop();
  
  // Display
  displayInit();
  drawCentered("CARRY ROBOT", "Starting...", "", "");
  
  // Load config
  loadConfig();
  
  // Sensors
  nfcInit();
  tofInit();
  
  // WiFi
  drawCentered("CARRY ROBOT", "Connecting", "WiFi...", "");
  wifiInit();
  
  // Initial state
  enterIdle();
  
  buzzOK();
  
#if SERIAL_DEBUG
  Serial.println("=== READY ===");
#endif
}

// =========================================
// MAIN LOOP
// =========================================
void loop() {
  // Check obstacle (highest priority)
  checkObstacle();
  
  // Process NFC checkpoints
  processNFC();
  
  // Check cargo button
  checkCargoButton();
  
  // Poll for new missions
  pollMission();
  
  // Send telemetry
  sendPeriodicTelemetry();
  
  // Small delay for stability
  delay(5);
}
