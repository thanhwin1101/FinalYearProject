#include "hardware.h"
#include "config.h"
#include "globals.h"
#include "mission.h"   // for currentRoute, isWiFiConnected, isMqttConnected (used in display)
#include <ctype.h>

// =============================================================================
//  MOTOR CONTROL
// =============================================================================

static inline uint8_t clampDuty(int v) {
  if (v < 0) return 0;
  if (v > 255) return 255;
  return (uint8_t)v;
}

static void setSideSpeed(int leftPwm, int rightPwm) {
  ledcWrite((uint8_t)EN_LEFT, clampDuty(leftPwm));
  ledcWrite((uint8_t)EN_RIGHT, clampDuty(rightPwm));
}

static inline int applyGainDuty(int pwm, float gain) {
  if (pwm <= 0) return 0;
  if (gain < 0.0f) gain = 0.0f;
  float v = (float)pwm * gain;
  if (v > 255.0f) v = 255.0f;
  return (int)(v + 0.5f);
}

#define PWM_CHANNEL_LEFT  0
#define PWM_CHANNEL_RIGHT 1

void motorPwmInit() {
#if ESP_ARDUINO_VERSION >= ESP_ARDUINO_VERSION_VAL(3, 0, 0)
  ledcAttach((uint8_t)EN_LEFT, MOTOR_PWM_FREQ, MOTOR_PWM_RES);
  ledcAttach((uint8_t)EN_RIGHT, MOTOR_PWM_FREQ, MOTOR_PWM_RES);
#else
  ledcSetup(PWM_CHANNEL_LEFT, MOTOR_PWM_FREQ, MOTOR_PWM_RES);
  ledcSetup(PWM_CHANNEL_RIGHT, MOTOR_PWM_FREQ, MOTOR_PWM_RES);
  ledcAttachPin(EN_LEFT, PWM_CHANNEL_LEFT);
  ledcAttachPin(EN_RIGHT, PWM_CHANNEL_RIGHT);
#endif
  ledcWrite((uint8_t)EN_LEFT, 0);
  ledcWrite((uint8_t)EN_RIGHT, 0);
}

void motorsStop() {
  setSideSpeed(0, 0);
  digitalWrite(FL_IN1, LOW); digitalWrite(FL_IN2, LOW);
  digitalWrite(RL_IN1, LOW); digitalWrite(RL_IN2, LOW);
  digitalWrite(FR_IN1, LOW); digitalWrite(FR_IN2, LOW);
  digitalWrite(RR_IN1, LOW); digitalWrite(RR_IN2, LOW);
}

void applyForwardBrake(int brakePwm, int brakeMs) {
  setSideSpeed(brakePwm, brakePwm);
  digitalWrite(FL_IN1, INVERT_LEFT ? HIGH : LOW);  digitalWrite(FL_IN2, INVERT_LEFT ? LOW : HIGH);
  digitalWrite(RL_IN1, INVERT_LEFT ? HIGH : LOW);  digitalWrite(RL_IN2, INVERT_LEFT ? LOW : HIGH);
  digitalWrite(FR_IN1, INVERT_RIGHT ? HIGH : LOW); digitalWrite(FR_IN2, INVERT_RIGHT ? LOW : HIGH);
  digitalWrite(RR_IN1, INVERT_RIGHT ? HIGH : LOW); digitalWrite(RR_IN2, INVERT_RIGHT ? LOW : HIGH);
  delay(brakeMs);
  motorsStop();
}

void driveForward(int pwm) {
  int l = applyGainDuty(pwm, leftGain);
  int r = applyGainDuty(pwm, rightGain);
  setSideSpeed(l, r);
  digitalWrite(FL_IN1, INVERT_LEFT ? LOW : HIGH);  digitalWrite(FL_IN2, INVERT_LEFT ? HIGH : LOW);
  digitalWrite(RL_IN1, INVERT_LEFT ? LOW : HIGH);  digitalWrite(RL_IN2, INVERT_LEFT ? HIGH : LOW);
  digitalWrite(FR_IN1, INVERT_RIGHT ? LOW : HIGH); digitalWrite(FR_IN2, INVERT_RIGHT ? HIGH : LOW);
  digitalWrite(RR_IN1, INVERT_RIGHT ? LOW : HIGH); digitalWrite(RR_IN2, INVERT_RIGHT ? HIGH : LOW);
}

void driveBackward(int pwm) {
  int l = applyGainDuty(pwm, leftGain);
  int r = applyGainDuty(pwm, rightGain);
  setSideSpeed(l, r);
  digitalWrite(FL_IN1, INVERT_LEFT ? HIGH : LOW);  digitalWrite(FL_IN2, INVERT_LEFT ? LOW : HIGH);
  digitalWrite(RL_IN1, INVERT_LEFT ? HIGH : LOW);  digitalWrite(RL_IN2, INVERT_LEFT ? LOW : HIGH);
  digitalWrite(FR_IN1, INVERT_RIGHT ? HIGH : LOW); digitalWrite(FR_IN2, INVERT_RIGHT ? LOW : HIGH);
  digitalWrite(RR_IN1, INVERT_RIGHT ? HIGH : LOW); digitalWrite(RR_IN2, INVERT_RIGHT ? LOW : HIGH);
}

void setMotorDirLeft() {
  digitalWrite(FL_IN1, INVERT_LEFT ? HIGH : LOW);  digitalWrite(FL_IN2, INVERT_LEFT ? LOW : HIGH);
  digitalWrite(RL_IN1, INVERT_LEFT ? HIGH : LOW);  digitalWrite(RL_IN2, INVERT_LEFT ? LOW : HIGH);
  digitalWrite(FR_IN1, INVERT_RIGHT ? LOW : HIGH); digitalWrite(FR_IN2, INVERT_RIGHT ? HIGH : LOW);
  digitalWrite(RR_IN1, INVERT_RIGHT ? LOW : HIGH); digitalWrite(RR_IN2, INVERT_RIGHT ? HIGH : LOW);
}

void setMotorDirRight() {
  digitalWrite(FL_IN1, INVERT_LEFT ? LOW : HIGH);  digitalWrite(FL_IN2, INVERT_LEFT ? HIGH : LOW);
  digitalWrite(RL_IN1, INVERT_LEFT ? LOW : HIGH);  digitalWrite(RL_IN2, INVERT_LEFT ? HIGH : LOW);
  digitalWrite(FR_IN1, INVERT_RIGHT ? HIGH : LOW); digitalWrite(FR_IN2, INVERT_RIGHT ? LOW : HIGH);
  digitalWrite(RR_IN1, INVERT_RIGHT ? HIGH : LOW); digitalWrite(RR_IN2, INVERT_RIGHT ? LOW : HIGH);
}

void applyHardBrake(bool wasTurningLeft, int brakePwm, int brakeMs) {
  if (wasTurningLeft) setMotorDirRight(); else setMotorDirLeft();
  setSideSpeed(brakePwm, brakePwm);
  delay(brakeMs);
  motorsStop();
}

void rotateByTime(unsigned long totalMs, bool isLeft) {
  motorsStop();
  delay(50);

  unsigned long slowZoneStart = (unsigned long)(totalMs * (1.0 - SLOW_ZONE_RATIO));
  unsigned long fineZoneStart = (unsigned long)(totalMs * (1.0 - FINE_ZONE_RATIO));

  if (isLeft) setMotorDirLeft(); else setMotorDirRight();

  unsigned long startTime = millis();
  int currentPwm = PWM_TURN;
  setSideSpeed(currentPwm, currentPwm);

  while (true) {
    unsigned long elapsed = millis() - startTime;
    if (elapsed >= totalMs) break;

    int newPwm;
    if (elapsed >= fineZoneStart) {
      newPwm = PWM_TURN_FINE;
    } else if (elapsed >= slowZoneStart) {
      float progress = (float)(elapsed - slowZoneStart) / (fineZoneStart - slowZoneStart);
      newPwm = PWM_TURN_SLOW - (int)((PWM_TURN_SLOW - PWM_TURN_FINE) * progress);
    } else {
      newPwm = PWM_TURN;
    }

    if (abs(newPwm - currentPwm) >= 8) {
      currentPwm = newPwm;
      setSideSpeed(currentPwm, currentPwm);
    }
    delay(5);
  }

  motorsStop();
  delay(15);

  int brakePwm2 = (currentPwm < PWM_TURN_SLOW) ? (PWM_BRAKE / 2) : PWM_BRAKE;
  int brakeMs2  = (currentPwm < PWM_TURN_SLOW) ? 40 : 60;
  applyHardBrake(isLeft, brakePwm2, brakeMs2);

#if SERIAL_DEBUG
  Serial.print("Turn "); Serial.print(isLeft ? "L" : "R");
  Serial.print(" time="); Serial.print(totalMs);
  Serial.println("ms");
#endif
}

void turnByAction(char a) {
  if (a == 'L') rotateByTime(TURN_90_MS, true);
  else if (a == 'R') rotateByTime(TURN_90_MS, false);
  else if (a == 'B') rotateByTime(TURN_180_MS, true);
}

// =============================================================================
//  SENSORS
// =============================================================================

void nfcInit() {
  nfc.begin();
  uint32_t versiondata = nfc.getFirmwareVersion();
  if (!versiondata) {
    oledDraw4("PN532 FAIL", NULL, NULL, NULL);
    while (1) delay(100);
  }
  nfc.SAMConfig();
  nfc.setPassiveActivationRetries(0xFF);
}

bool readNFC(uint8_t* uid, uint8_t* uidLen) {
  return nfc.readPassiveTargetID(PN532_MIFARE_ISO14443A, uid, uidLen, 100);
}

void tofInit() {
  tof.setTimeout(500);
  if (!tof.init()) {
    Serial.println("VL53L0X fail");
  } else {
    tof.setMeasurementTimingBudget(20000);
    Serial.println("VL53L0X OK");
  }
}

bool tofReadDistance(uint16_t &dist) {
  dist = tof.readRangeSingleMillimeters();
  return !tof.timeoutOccurred();
}

// =============================================================================
//  UID LOOKUP
// =============================================================================

static const struct { uint8_t uid[7]; uint8_t len; const char* node; } UID_MAP[] = {
  // Room 1 (M side)
  { { 0x35, 0xFD, 0xE1, 0x83 }, 4, "R1M1" },
  { { 0x45, 0xAB, 0x49, 0x83 }, 4, "R1M2" },
  { { 0x35, 0x2E, 0xCA, 0x83 }, 4, "R1M3" },
  // Room 1 (O side)
  { { 0x45, 0x0E, 0x9D, 0x83 }, 4, "R1O1" },
  { { 0x35, 0x58, 0x97, 0x83 }, 4, "R1O2" },
  { { 0x35, 0xF0, 0xF8, 0x83 }, 4, "R1O3" },
  // Room 1 doors
  { { 0x35, 0xF6, 0xEF, 0x83 }, 4, "R1D1" },
  { { 0x45, 0xC7, 0x37, 0x83 }, 4, "R1D2" },

  // Room 2 (M side)
  { { 0x35, 0x1A, 0x34, 0x83 }, 4, "R2M1" },
  { { 0x45, 0xBF, 0xF6, 0x83 }, 4, "R2M2" },
  { { 0x35, 0xDC, 0x8F, 0x83 }, 4, "R2M3" },
  // Room 2 (O side)
  { { 0x45, 0x35, 0xC3, 0x83 }, 4, "R2O1" },
  { { 0x45, 0x27, 0x34, 0x83 }, 4, "R2O2" },
  { { 0x35, 0x2A, 0x2D, 0x83 }, 4, "R2O3" },
  // Room 2 doors
  { { 0x35, 0x4C, 0xB8, 0x83 }, 4, "R2D1" },
  { { 0x45, 0x81, 0xA4, 0x83 }, 4, "R2D2" },

  // Room 3 (M side)
  { { 0x35, 0x22, 0xF5, 0x83 }, 4, "R3M1" },
  { { 0x45, 0xC2, 0xB8, 0x83 }, 4, "R3M2" },
  { { 0x35, 0xBB, 0xB1, 0x83 }, 4, "R3M3" },
  // Room 3 (O side)
  { { 0x45, 0x26, 0xF3, 0x83 }, 4, "R3O1" },
  { { 0x45, 0x1D, 0xA4, 0x83 }, 4, "R3O2" },
  { { 0x35, 0x1E, 0x47, 0x83 }, 4, "R3O3" },
  // Room 3 doors
  { { 0x35, 0x45, 0xAF, 0x83 }, 4, "R3D1" },
  { { 0x35, 0x35, 0xBA, 0x83 }, 4, "R3D2" },

  // Room 4 (M side)
  { { 0x45, 0x83, 0xFB, 0x83 }, 4, "R4M1" },
  { { 0x45, 0x8E, 0x00, 0x83 }, 4, "R4M2" },
  { { 0x35, 0x4D, 0x9B, 0x83 }, 4, "R4M3" },
  // Room 4 (O side)
  { { 0x45, 0x7D, 0x5A, 0x83 }, 4, "R4O1" },
  { { 0x35, 0xDB, 0xEA, 0x83 }, 4, "R4O2" },
  { { 0x35, 0xEB, 0x18, 0x83 }, 4, "R4O3" },
  // Room 4 doors
  { { 0x35, 0x48, 0x9F, 0x83 }, 4, "R4D1" },
  { { 0x35, 0x26, 0x79, 0x83 }, 4, "R4D2" },

  // Specials (MED, Junctions, Hallways)
  { { 0x45, 0x54, 0x80, 0x83 }, 4, "MED" },
  { { 0x35, 0x2C, 0x3C, 0x83 }, 4, "J4" },
  { { 0x45, 0x86, 0xAC, 0x83 }, 4, "H_TOP" },
  { { 0x45, 0x79, 0x31, 0x83 }, 4, "H_BOT" },
  { { 0x45, 0xD3, 0x91, 0x83 }, 4, "H_MED" }
};
static const int UID_MAP_SIZE = sizeof(UID_MAP) / sizeof(UID_MAP[0]);

const String HOME_MED_UID = "45D39183"; 

static String uidBytesToHex(const uint8_t* uid, uint8_t len) {
  String s = "";
  for (int i = 0; i < len; i++) {
    if (uid[i] < 0x10) s += "0";
    s += String(uid[i], HEX);
  }
  s.toUpperCase();
  return s;
}

const char* uidLookupByNodeId(const uint8_t* uid, uint8_t len) {
  for (int i = 0; i < UID_MAP_SIZE; i++) {
    if (UID_MAP[i].len == len && memcmp(UID_MAP[i].uid, uid, len) == 0)
      return UID_MAP[i].node;
  }
  return nullptr;
}

String uidLookupByUid(const String& uidHex) {
  String uidUpper = uidHex;
  uidUpper.toUpperCase();
  for (int i = 0; i < UID_MAP_SIZE; i++) {
    String mapUid = uidBytesToHex(UID_MAP[i].uid, UID_MAP[i].len);
    if (mapUid == uidUpper) return String(UID_MAP[i].node);
  }
  return "";
}

String getUidForNode(const String& nodeName) {
  for (int i = 0; i < UID_MAP_SIZE; i++) {
    if (nodeName.equalsIgnoreCase(UID_MAP[i].node))
      return uidBytesToHex(UID_MAP[i].uid, UID_MAP[i].len);
  }
  return "";
}

// =============================================================================
//  DISPLAY
// =============================================================================

void displayInit() {
  oled.begin();
  oled.clearBuffer();
  oled.setFont(u8g2_font_6x10_tf);
  oled.drawStr(0, 12, "CARRY ROBOT");
  oled.drawStr(0, 26, "Initializing...");
  oled.sendBuffer();
}

void oledDraw4(const char* l1, const char* l2, const char* l3, const char* l4) {
  oled.clearBuffer();
  oled.setFont(u8g2_font_6x10_tf);
  if (l1) oled.drawStr(0, 12, l1);
  if (l2) oled.drawStr(0, 26, l2);
  if (l3) oled.drawStr(0, 40, l3);
  if (l4) oled.drawStr(0, 54, l4);
  oled.sendBuffer();
}

void oledDraw() {
  oled.clearBuffer();
  oled.setFont(u8g2_font_6x10_tf);

  // Lấy tên bệnh nhân an toàn (hiển thị "No Name" nếu payload MQTT rỗng)
  String safePatientName = (patientName.length() > 0) ? patientName : "No Name";

  switch (state) {

    case ST_BOOT:
      oled.drawStr(0, 12, "CARRY ROBOT");
      oled.drawStr(0, 26, "Initializing...");
      break;

    case ST_PORTAL: {
      oled.drawStr(0, 12, "OPEN PORTAL....");
      break;
    }

    case ST_CONNECTING: {
      oled.drawStr(0, 12, "Connecting....");
      String wl = String("wifi: ") + (isWiFiConnected() ? "OK" : "...");
      oled.drawStr(0, 26, wl.c_str());
      String ml = String("MQTT: ") + (isMqttConnected() ? "OK" : "...");
      oled.drawStr(0, 40, ml.c_str());
      break;
    }

    case ST_IDLE: {
      oled.drawStr(0, 12, "status: IDLE");
      
      // Ở TRẠNG THÁI BÌNH THƯỜNG: Hiển thị tên Robot
      String l2 = String("Name: ") + String(DEVICE_NAME);
      oled.drawStr(0, 26, l2.c_str());
      
      oled.drawStr(0, 40, "Curent checkpoint:");
      if (currentCheckpoint.length() > 0) {
        oled.drawStr(0, 54, currentCheckpoint.c_str());
      }
      break;
    }

    case ST_GET_MISSION:
    case ST_OUTBOUND: {
      String stat = (state == ST_GET_MISSION) ? "get mission" : "get mission"; 
      if (activeMissionStatus == "cancelled") stat = "Cancel";

      String l1 = "status: " + stat;
      oled.drawStr(0, 12, l1.c_str());
      
      // KHI ĐÃ NHẬN NHIỆM VỤ: Hiển thị tên Bệnh nhân
      String l2 = String("Name: ") + truncStr(safePatientName, 15);
      oled.drawStr(0, 26, l2.c_str());
      
      String cur = currentCheckpoint.length() > 0 ? currentCheckpoint : "";
      String nxt = "-";
      const auto& r = currentRoute();
      if (r.size() > 0) {
        if (state == ST_GET_MISSION) {
           nxt = r[0].nodeId;
        } else {
           if (routeIndex >= 0 && routeIndex < (int)r.size()) cur = r[routeIndex].nodeId;
           if (routeIndex + 1 >= 0 && routeIndex + 1 < (int)r.size()) nxt = r[routeIndex + 1].nodeId;
        }
      }
      
      String l3 = "Curent: " + cur;
      String l4 = "Next  : " + nxt;
      
      if (millis() < turnOverlayUntil && (lastTurnChar == 'L' || lastTurnChar == 'R')) {
        l4 = String("TURN  : ") + turnCharLabel(lastTurnChar);
      }
      
      oled.drawStr(0, 40, l3.c_str());
      oled.drawStr(0, 54, l4.c_str());
      break;
    }

    case ST_CANCEL:
    case ST_WAIT_RETURN_ROUTE: {
      oled.drawStr(0, 12, "status: Cancel");
      
      // KHI ĐÃ NHẬN NHIỆM VỤ (KỂ CẢ ĐANG HỦY): Hiển thị tên Bệnh nhân
      String l2 = String("Name: ") + truncStr(safePatientName, 15);
      oled.drawStr(0, 26, l2.c_str());
      
      String cur = currentCheckpoint.length() > 0 ? currentCheckpoint : "";
      String nxt = "-";
      const auto& r = currentRoute();
      if (routeIndex >= 0 && routeIndex < (int)r.size()) cur = r[routeIndex].nodeId;
      if (routeIndex + 1 >= 0 && routeIndex + 1 < (int)r.size()) nxt = r[routeIndex + 1].nodeId;
      if (state == ST_WAIT_RETURN_ROUTE) nxt = "WAITING...";
      
      String l3 = "Curent: " + cur;
      String l4 = "Next  : " + nxt;
      oled.drawStr(0, 40, l3.c_str());
      oled.drawStr(0, 54, l4.c_str());
      break;
    }

    case ST_WAIT_AT_DEST:
    case ST_BACK: {
      String stat = (activeMissionStatus == "cancelled") ? "Cancel" : "back";
      String l1 = "Status: " + stat;
      oled.drawStr(0, 12, l1.c_str());

      // KHI ĐÃ NHẬN NHIỆM VỤ (ĐANG CHỜ TẠI ĐÍCH HOẶC QUAY VỀ): Hiển thị tên Bệnh nhân
      String l2 = String("Name: ") + truncStr(safePatientName, 15);
      oled.drawStr(0, 26, l2.c_str());
      
      String cur = currentCheckpoint.length() > 0 ? currentCheckpoint : "";
      String nxt = "-";
      const auto& r = currentRoute();
      if (state == ST_BACK && r.size() > 0) {
        if (routeIndex >= 0 && routeIndex < (int)r.size()) cur = r[routeIndex].nodeId;
        if (routeIndex + 1 >= 0 && routeIndex + 1 < (int)r.size()) nxt = r[routeIndex + 1].nodeId;
      }
      
      String l3 = "Curent: " + cur;
      String l4 = "Next  : " + nxt;
      
      if (millis() < turnOverlayUntil && (lastTurnChar == 'L' || lastTurnChar == 'R')) {
        l4 = String("TURN  : ") + turnCharLabel(lastTurnChar);
      }
      
      oled.drawStr(0, 40, l3.c_str());
      oled.drawStr(0, 54, l4.c_str());
      break;
    }
  }

  oled.sendBuffer();
}

void showTurnOverlay(char direction, unsigned long durationMs) {
  lastTurnChar = direction;
  turnOverlayUntil = millis() + durationMs;
}