#include <Arduino.h>
#include <Wire.h>
#include <SPI.h>
#include "config.h"
#include "globals.h"
#include "hardware.h"
#include "mission.h"

static unsigned long lastTofRead = 0;
static unsigned long localLastTelemetry = 0;

static int currentPanAngle = 90;
static int currentTiltAngle = 90;

enum FollowState { FOLLOW_TRACKING, FOLLOW_SCANNING, FOLLOW_REALIGN };
FollowState currentFollowState = FOLLOW_TRACKING;

void checkObstacle() {
  if (millis() - lastTofRead < TOF_INTERVAL) return;
  lastTofRead = millis();
  uint16_t dist; if (!tofReadDistance(dist)) return;

  if (dist < TOF_STOP_DIST && !obstacleHold) {
    obstacleHold = true; motorsStop();
  } else if (dist >= TOF_RESUME_DIST && obstacleHold) {
    obstacleHold = false;
    if (state == ST_OUTBOUND || state == ST_CANCEL || state == ST_BACK || state == ST_WAIT_CHECKPOINT)
      driveForward(PWM_FWD);
  }
}

void processNFC() {
  if (state == ST_FOLLOW_PERSON || state == ST_VISUAL_FIND_LINE) return; 

  if (!nfcAllowed()) return;
  uint8_t uid[7]; uint8_t uidLen = 0;
  if (!readNFC(uid, &uidLen)) return;
  markNfcRead(); beepOnce(60, 3000);
  String uidStr = "";
  for (int i = 0; i < uidLen; i++) { if (uid[i] < 0x10) uidStr += "0"; uidStr += String(uid[i], HEX); }
  uidStr.toUpperCase(); handleCheckpointHit(uidStr);
}

void initPins() {
  pinMode(SW_PIN, INPUT_PULLUP);
  buzzerInit();
  Wire.begin(I2C_SDA, I2C_SCL);
  SPI.begin(SPI_SCK, SPI_MISO, SPI_MOSI, -1);
}

void setup() {
  Serial.begin(115200); state = ST_BOOT;
  initPins(); hardwareInit(); motorsStop();
  displayInit(); tofInit(); nfcInit();

  processButton(); delay(50); processButton();
  if (digitalRead(SW_PIN) == LOW) { state = ST_PORTAL; oledDraw(); wifiInit(true); } 
  else { state = ST_CONNECTING; oledDraw(); wifiInit(false); }

  if (!isWiFiConnected()) { delay(2000); ESP.restart(); }

  state = ST_CONNECTING; oledDraw();
  mqttInit(); mqttReconnect();
  goIdleReset(); beepOnce(120, 2200);
}

void loop() {
  unsigned long now = millis();
  mqttLoop(); 
  processButton(); 
  listenToNano(); 

  if (now - lastOLED >= OLED_MS) { lastOLED = now; oledDraw(); }
  if (now - localLastTelemetry >= TELEMETRY_INTERVAL) { localLastTelemetry = now; sendTelemetry(); }

  processNFC(); 

  if (flagDoubleClick) { 
    beepOnce(100, 3000); 
    if (state == ST_IDLE || state == ST_WAIT_AT_DEST) {
      state = ST_FOLLOW_PERSON; 
      currentFollowState = FOLLOW_TRACKING;
      currentPanAngle = 90;
      currentTiltAngle = 90;
      setServoPan(90);
      setHuskyLensMode("TAG");
    } else if (state == ST_FOLLOW_PERSON) {
      state = ST_VISUAL_FIND_LINE; 
      setHuskyLensMode("LINE"); 
    }
    flagDoubleClick = false;
  }

  switch (state) {
    case ST_IDLE:
      motorsStop(); toneOff(); break;

    case ST_GET_MISSION:
      motorsStop(); toneOff();
      if (flagSingleClick && currentCheckpoint == "MED") {
        beepOnce(100, 2400); 
        startOutbound();
        flagSingleClick = false;
      }
      break;

    case ST_OUTBOUND:
    case ST_CANCEL:
    case ST_BACK:
      checkObstacle(); 
      if (obstacleHold) { motorsStop(); break; }
      driveForward(PWM_FWD);
      break;

    case ST_WAIT_AT_DEST:
      motorsStop(); toneOff();
      if (flagSingleClick) {
        beepArrivedPattern(); sendComplete("ok");
        if (retRoute.size() < 2 && outbound.size() >= 2) buildReturnFromVisited();
        if (activeMissionStatus.length() == 0) activeMissionStatus = "completed";
        startReturn("return-after-unload", false); 
        flagSingleClick = false;
      }
      break;

    case ST_FOLLOW_PERSON: {
      bool tagFound = huskylens.request() && huskylens.isLearned() && huskylens.available();
      int tagX = 0, tagY = 0;
      
      if (tagFound) {
        HUSKYLENSResult result = huskylens.read();
        tagX = result.xCenter; tagY = result.yCenter; 
      }

      switch (currentFollowState) {
        case FOLLOW_TRACKING: 
          if (tagFound) {
            // Đồng bộ Pan-Tilt
            int errorY = tagY - 120; 
            currentTiltAngle -= (errorY * 0.05); 
            currentTiltAngle = constrain(currentTiltAngle, 45, 135); 
            setServoTilt(currentTiltAngle);

            int errorX = tagX - 160;
            currentPanAngle -= (errorX * 0.05);
            currentPanAngle = constrain(currentPanAngle, 45, 135);
            setServoPan(currentPanAngle);

            // Xoay thân xe bám theo góc Servo X
            int angleErrorX = currentPanAngle - 90;
            int turnSpeed = 0;
            if (abs(angleErrorX) > 10) turnSpeed = angleErrorX * 1.5;

            // Tiến/lùi bằng ToF
            uint16_t currentDist = 9999;
            bool hasTof = tofReadDistance(currentDist);
            int forwardSpeed = 0;

            if (hasTof && currentDist < 2500) {
               forwardSpeed = (currentDist - 700) * 0.15; 
               forwardSpeed = constrain(forwardSpeed, -60, 60);
            }

            mecanumDrive(0, forwardSpeed, turnSpeed); 
          } else {
            currentFollowState = FOLLOW_SCANNING; 
          }
          break;

        case FOLLOW_SCANNING: 
          float distL = readUltrasonic(TRIG_LEFT, ECHO_LEFT);
          float distR = readUltrasonic(TRIG_RIGHT, ECHO_RIGHT);

          if (distL > 150.0 && distL > distR) {
             mecanumDrive(0, 0, -80); 
             if (tagFound) currentFollowState = FOLLOW_TRACKING;
          } else if (distR > 150.0 && distR > distL) {
             mecanumDrive(0, 0, 80);
             if (tagFound) currentFollowState = FOLLOW_TRACKING;
          } else {
             mecanumDrive(0, 0, 80); 
             if (tagFound) currentFollowState = FOLLOW_TRACKING;
          }
          break;
      }
      break;
    }

    case ST_VISUAL_FIND_LINE: {
      setServoTilt(45);
      setServoPan(90); 
      
      if (huskylens.request() && huskylens.isLearned() && huskylens.available()) {
        HUSKYLENSResult result = huskylens.read();
        int errorX = result.xOrigin - 160;
        
        if (abs(errorX) < 20) { 
          mecanumDrive(0, 40, 0); 
          
          if (Serial2.available()) {
            String feedback = Serial2.readStringUntil('\n');
            feedback.trim();
            if (feedback == "STATUS:ON_LINE") {
               motorsStop(); 
               setServoTilt(90);
               state = ST_WAIT_CHECKPOINT; 
               driveForward(PWM_FWD);
               beepOnce(200, 2500);
            }
          }
        } else { mecanumDrive(errorX * 1.2, 0, 0); }
      } else { mecanumDrive(0, 40, 0); } 
      break;
    }

    case ST_WAIT_CHECKPOINT:
      checkObstacle(); if (obstacleHold) { motorsStop(); break; }
      driveForward(PWM_FWD);
      break;

    case ST_WAIT_RETURN_ROUTE:
      motorsStop(); toneOff();
      if (now - waitingReturnRouteStartTime > RETURN_ROUTE_TIMEOUT_MS) {
        buildReturnFromVisited(); 
        outbound.clear();
        waitingForReturnRoute = false; state = ST_BACK;
        routeIndex = 0; obstacleHold = false; beepOnce(120, 1200);
      }
      break;
  }
  delay(5);
}