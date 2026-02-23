#include "hardware.h"
#include "config.h"
#include "globals.h"
#include "mission.h"
#include <ESP32Servo.h>
#include <ctype.h>

Servo panServo;
Servo tiltServo;

void hardwareInit() {
  Serial2.begin(115200, SERIAL_8N1, RX_NANO, TX_NANO);
  
  pinMode(TRIG_LEFT, OUTPUT); pinMode(ECHO_LEFT, INPUT);
  pinMode(TRIG_RIGHT, OUTPUT); pinMode(ECHO_RIGHT, INPUT);
  
  panServo.setPeriodHertz(50);
  panServo.attach(SERVO_PAN_PIN, 500, 2400);
  panServo.write(90);

  tiltServo.setPeriodHertz(50);
  tiltServo.attach(SERVO_TILT_PIN, 500, 2400);
  tiltServo.write(90);

  while (!huskylens.begin(Wire)) {
    oledDraw4("HuskyLens ERR", "Check I2C Wiring", NULL, NULL);
    delay(100);
  }
}

void motorsStop() { Serial2.println("STOP"); }
void driveForward(int pwm) { Serial2.printf("LINE:%d\n", pwm); }
void mecanumDrive(int x, int y, int z) { Serial2.printf("MOVE:%d,%d,%d\n", x, y, z); }

void turnByAction(char a) {
  if (a == 'L') { Serial2.printf("MOVE:0,0,%d\n", -PWM_TURN); delay(TURN_90_MS); } 
  else if (a == 'R') { Serial2.printf("MOVE:0,0,%d\n", PWM_TURN); delay(TURN_90_MS); } 
  else if (a == 'B') { Serial2.printf("MOVE:0,0,%d\n", PWM_TURN); delay(TURN_180_MS); }
  motorsStop();
}

void applyForwardBrake(int brakePwm, int brakeMs) {
  // Hard-braking bằng cách đảo chiều động cơ chớp nhoáng
  Serial2.printf("MOVE:0,%d,0\n", -brakePwm);
  delay(brakeMs);
  motorsStop();
}

void listenToNano() {
  if (Serial2.available()) {
    String feedback = Serial2.readStringUntil('\n');
    feedback.trim();
  }
}

void setHuskyLensMode(String mode) {
  if (mode == "TAG") huskylens.writeAlgorithm(ALGORITHM_TAG_RECOGNITION);
  else if (mode == "LINE") huskylens.writeAlgorithm(ALGORITHM_LINE_TRACKING);
}

void setServoPan(int angle) { panServo.write(angle); }
void setServoTilt(int angle) { tiltServo.write(angle); }

float readUltrasonic(int trig, int echo) {
  digitalWrite(trig, LOW); delayMicroseconds(2);
  digitalWrite(trig, HIGH); delayMicroseconds(10);
  digitalWrite(trig, LOW);
  long duration = pulseIn(echo, HIGH, 30000);
  if (duration == 0) return 999.0;
  return duration * 0.034 / 2;
}

void nfcInit() {
  nfc.begin();
  uint32_t versiondata = nfc.getFirmwareVersion();
  if (!versiondata) { oledDraw4("PN532 FAIL", NULL, NULL, NULL); while(1) delay(100); }
  nfc.SAMConfig();
  nfc.setPassiveActivationRetries(0xFF);
}

bool readNFC(uint8_t* uid, uint8_t* uidLen) { return nfc.readPassiveTargetID(PN532_MIFARE_ISO14443A, uid, uidLen, 100); }

void tofInit() { tof.setTimeout(500); if (tof.init()) tof.setMeasurementTimingBudget(20000); }
bool tofReadDistance(uint16_t &dist) { dist = tof.readRangeSingleMillimeters(); return !tof.timeoutOccurred(); }

static const struct { uint8_t uid[7]; uint8_t len; const char* node; } UID_MAP[] = {
  { { 0x35, 0xFD, 0xE1, 0x83 }, 4, "R1M1" }, { { 0x45, 0xAB, 0x49, 0x83 }, 4, "R1M2" }, { { 0x35, 0x2E, 0xCA, 0x83 }, 4, "R1M3" },
  { { 0x45, 0x0E, 0x9D, 0x83 }, 4, "R1O1" }, { { 0x35, 0x58, 0x97, 0x83 }, 4, "R1O2" }, { { 0x35, 0xF0, 0xF8, 0x83 }, 4, "R1O3" },
  { { 0x35, 0xF6, 0xEF, 0x83 }, 4, "R1D1" }, { { 0x45, 0xC7, 0x37, 0x83 }, 4, "R1D2" },
  { { 0x35, 0x1A, 0x34, 0x83 }, 4, "R2M1" }, { { 0x45, 0xBF, 0xF6, 0x83 }, 4, "R2M2" }, { { 0x35, 0xDC, 0x8F, 0x83 }, 4, "R2M3" },
  { { 0x45, 0x35, 0xC3, 0x83 }, 4, "R2O1" }, { { 0x45, 0x27, 0x34, 0x83 }, 4, "R2O2" }, { { 0x35, 0x2A, 0x2D, 0x83 }, 4, "R2O3" },
  { { 0x35, 0x4C, 0xB8, 0x83 }, 4, "R2D1" }, { { 0x45, 0x81, 0xA4, 0x83 }, 4, "R2D2" },
  { { 0x35, 0x22, 0xF5, 0x83 }, 4, "R3M1" }, { { 0x45, 0xC2, 0xB8, 0x83 }, 4, "R3M2" }, { { 0x35, 0xBB, 0xB1, 0x83 }, 4, "R3M3" },
  { { 0x45, 0x26, 0xF3, 0x83 }, 4, "R3O1" }, { { 0x45, 0x1D, 0xA4, 0x83 }, 4, "R3O2" }, { { 0x35, 0x1E, 0x47, 0x83 }, 4, "R3O3" },
  { { 0x35, 0x45, 0xAF, 0x83 }, 4, "R3D1" }, { { 0x35, 0x35, 0xBA, 0x83 }, 4, "R3D2" },
  { { 0x45, 0x83, 0xFB, 0x83 }, 4, "R4M1" }, { { 0x45, 0x8E, 0x00, 0x83 }, 4, "R4M2" }, { { 0x35, 0x4D, 0x9B, 0x83 }, 4, "R4M3" },
  { { 0x45, 0x7D, 0x5A, 0x83 }, 4, "R4O1" }, { { 0x35, 0xDB, 0xEA, 0x83 }, 4, "R4O2" }, { { 0x35, 0xEB, 0x18, 0x83 }, 4, "R4O3" },
  { { 0x35, 0x48, 0x9F, 0x83 }, 4, "R4D1" }, { { 0x35, 0x26, 0x79, 0x83 }, 4, "R4D2" },
  { { 0x45, 0x54, 0x80, 0x83 }, 4, "MED" },  { { 0x35, 0x2C, 0x3C, 0x83 }, 4, "J4" },
  { { 0x45, 0x86, 0xAC, 0x83 }, 4, "H_TOP" },{ { 0x45, 0x79, 0x31, 0x83 }, 4, "H_BOT" },
  { { 0x45, 0xD3, 0x91, 0x83 }, 4, "H_MED" }
};
static const int UID_MAP_SIZE = sizeof(UID_MAP) / sizeof(UID_MAP[0]);
const String HOME_MED_UID = "45D39183"; 

static String uidBytesToHex(const uint8_t* uid, uint8_t len) {
  String s = "";
  for (int i = 0; i < len; i++) { if (uid[i] < 0x10) s += "0"; s += String(uid[i], HEX); }
  s.toUpperCase(); return s;
}
const char* uidLookupByNodeId(const uint8_t* uid, uint8_t len) {
  for (int i = 0; i < UID_MAP_SIZE; i++) { if (UID_MAP[i].len == len && memcmp(UID_MAP[i].uid, uid, len) == 0) return UID_MAP[i].node; }
  return nullptr;
}
String uidLookupByUid(const String& uidHex) {
  String uidUpper = uidHex; uidUpper.toUpperCase();
  for (int i = 0; i < UID_MAP_SIZE; i++) { if (uidBytesToHex(UID_MAP[i].uid, UID_MAP[i].len) == uidUpper) return String(UID_MAP[i].node); }
  return "";
}
String getUidForNode(const String& nodeName) {
  for (int i = 0; i < UID_MAP_SIZE; i++) { if (nodeName.equalsIgnoreCase(UID_MAP[i].node)) return uidBytesToHex(UID_MAP[i].uid, UID_MAP[i].len); }
  return "";
}

void displayInit() {
  oled.begin(); oled.clearBuffer(); oled.setFont(u8g2_font_6x10_tf);
  oled.drawStr(0, 12, "CARRY ROBOT"); oled.drawStr(0, 26, "Initializing..."); oled.sendBuffer();
}

void oledDraw4(const char* l1, const char* l2, const char* l3, const char* l4) {
  oled.clearBuffer(); oled.setFont(u8g2_font_6x10_tf);
  if (l1) oled.drawStr(0, 12, l1); if (l2) oled.drawStr(0, 26, l2);
  if (l3) oled.drawStr(0, 40, l3); if (l4) oled.drawStr(0, 54, l4);
  oled.sendBuffer();
}

void showTurnOverlay(char direction, unsigned long durationMs) { lastTurnChar = direction; turnOverlayUntil = millis() + durationMs; }

void oledDraw() {
  oled.clearBuffer(); 
  oled.setFont(u8g2_font_6x10_tf);
  
  String safePatientName = (patientName.length() > 0) ? patientName : "No Name";
  
  String netStatus = isWiFiConnected() ? "W:OK " : "W:-- ";
  netStatus += mqttConnected ? "M:OK" : "M:--";
  oled.drawStr(0, 10, ("[" + netStatus + "] CARRY-01").c_str());

  switch (state) {
    case ST_BOOT:
    case ST_PORTAL:
    case ST_CONNECTING:
      oled.drawStr(0, 26, ">>> SYSTEM STARTING <<<");
      if(state == ST_PORTAL) oled.drawStr(0, 40, "CONNECT WIFI PORTAL!");
      else oled.drawStr(0, 40, "Connecting MQTT...");
      break;

    case ST_IDLE: 
    case ST_GET_MISSION:
      oled.drawStr(0, 24, "MODE: 1. AUTONOMOUS");
      oled.drawStr(0, 36, state == ST_IDLE ? "ST: IDLE (WAITING)" : "ST: READY TO GO");
      oled.drawStr(0, 48, ("Pos: " + currentCheckpoint + " | Pt: " + truncStr(safePatientName, 5)).c_str());
      oled.drawStr(0, 60, state == ST_IDLE ? ("IP: " + WiFi.localIP().toString()).c_str() : "PRESS SW 1x TO RUN!");
      break;

    case ST_OUTBOUND:
    case ST_BACK:
    case ST_WAIT_AT_DEST:
    case ST_CANCEL: {
      oled.drawStr(0, 24, "MODE: 1. AUTONOMOUS");
      String stat = (state == ST_OUTBOUND) ? "ST: RUNNING (OUT)" : 
                    (state == ST_BACK) ? "ST: RETURNING" : 
                    (state == ST_WAIT_AT_DEST) ? "ST: ARRIVED AT DEST" : "ST: CANCELLED";
      oled.drawStr(0, 36, stat.c_str());
      
      String cur = currentCheckpoint.length() > 0 ? currentCheckpoint : ""; 
      String nxt = "-";
      const auto& r = currentRoute();
      if (r.size() > 0) {
         if (routeIndex >= 0 && routeIndex < (int)r.size()) cur = r[routeIndex].nodeId;
         if (routeIndex + 1 >= 0 && routeIndex + 1 < (int)r.size()) nxt = r[routeIndex + 1].nodeId;
      }
      
      oled.drawStr(0, 48, ("Route: " + cur + " -> " + nxt).c_str());
      
      if (state == ST_WAIT_AT_DEST) oled.drawStr(0, 60, "PRESS SW 1x TO RETURN");
      else if (millis() < turnOverlayUntil && (lastTurnChar == 'L' || lastTurnChar == 'R')) {
        oled.drawStr(0, 60, (String(">>> TURNING ") + turnCharLabel(lastTurnChar) + " <<<").c_str());
      } else {
        oled.drawStr(0, 60, "Sensor: LINE & TOF OK");
      }
      break;
    }

    case ST_FOLLOW_PERSON:
      oled.drawStr(0, 24, "MODE: 2. FOLLOW PERSON");
      oled.drawStr(0, 36, "ST: TRACKING AI VISION");
      oled.drawStr(0, 48, "Cam: ON  |  ToF: ACTIVE");
      oled.drawStr(0, 60, "Dbl-Click -> RECOVERY");
      break;

    case ST_VISUAL_FIND_LINE:
      oled.drawStr(0, 24, "MODE: 3. RECOVERY");
      oled.drawStr(0, 36, "ST: VISUAL DOCKING");
      oled.drawStr(0, 48, "Cam Tilt: 45 Deg (DOWN)");
      oled.drawStr(0, 60, "Searching for Line...");
      break;

    case ST_WAIT_CHECKPOINT:
      oled.drawStr(0, 24, "MODE: 3. RECOVERY");
      oled.drawStr(0, 36, "ST: BLIND RUN (LINE)");
      oled.drawStr(0, 48, "Nano: Line Tracking...");
      oled.drawStr(0, 60, "Waiting for RFID Tag..");
      break;

    case ST_WAIT_RETURN_ROUTE:
      oled.drawStr(0, 24, "MODE: 3. RECOVERY");
      oled.drawStr(0, 36, "ST: CALLING HOME");
      oled.drawStr(0, 48, ("Pos Found: " + cancelAtNodeId).c_str());
      oled.drawStr(0, 60, "Requesting JSON Route.");
      break;
  }
  oled.sendBuffer();
}