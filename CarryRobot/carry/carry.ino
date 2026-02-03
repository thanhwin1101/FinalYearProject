/*
  Carry Robot ESP32 FULL VERSION
  Hardware: 
  - ESP32 WROOM
  - PN532 (VSPI: SCK=18, MISO=19, MOSI=23, SS=5)
  - VL53L0X + MPU6050 + OLED SH1106 (I2C: SDA=21, SCL=22)
  - Motor Driver L298N x2 (Left Side + Right Side)
  - SRF05 x2 (Left + Right)
  - Cargo Switch

  Features:
  - Gyro turns (90/180) with Hard Braking.
  - Full Node Map.
  - WiFiManager & REST API.
*/

#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>

#include <WiFiManager.h>
#include <Preferences.h>

#include <SPI.h>
#include <Adafruit_PN532.h>

#include <Wire.h>
#include <VL53L0X.h>
#include <Adafruit_MPU6050.h>
#include <Adafruit_Sensor.h>

#include <U8g2lib.h>
#include <vector>
#include <algorithm>
#include <ctype.h>

// =========================================
// 1. PINOUT DEFINITIONS (STRICTLY APPLIED)
// =========================================

// --- I2C BUS (OLED, VL53L0X, MPU6050) ---
#define I2C_SDA 21
#define I2C_SCL 22

// --- PN532 (VSPI) ---
#define PN532_SCK  18
#define PN532_MISO 19
#define PN532_MOSI 23
#define PN532_SS   5

// --- MOTORS (L298N) ---
// Left Side (Front Left + Rear Left)
// ENA nối ENB -> 1 chân PWM chung cho bên trái
#define EN_LEFT   17  
#define FL_IN1    32
#define FL_IN2    33
#define RL_IN1    25
#define RL_IN2    26

// Right Side (Front Right + Rear Right)
// ENA nối ENB -> 1 chân PWM chung cho bên phải
#define EN_RIGHT  16  
#define FR_IN1    27
#define FR_IN2    14
#define RR_IN1    13
#define RR_IN2    4

// --- SRF05 (Ultrasonic) ---
// Lưu ý: TRIG nối vào TX0/RX0, khi nạp code nên tháo dây ra
#define TRIG_LEFT  1   
#define ECHO_LEFT  34  // Input Only
#define TRIG_RIGHT 3   
#define ECHO_RIGHT 35  // Input Only

// --- SENSORS & UI ---
#define CARGO_SWITCH_PIN 15
#define BUZZER_PIN       2  // Dùng tạm GPIO 2 (Led onboard) hoặc chân khác nếu có

// =========================================
// 2. CONFIGURATION
// =========================================
static const char* ROBOT_ID      = "CARRY-01";
static const char* HOME_MED_UID  = "45:54:80:83";

// Tắt Serial Debug để dùng chân TX/RX cho SRF05
#define SERIAL_DEBUG 0 

// WiFiManager
static const int WIFI_PORTAL_TIMEOUT_S  = 180;
static const int WIFI_CONNECT_TIMEOUT_S = 25;
static const unsigned long CFG_RESET_HOLD_MS = 5000;

// Motor Inversion (tuned)
const bool INVERT_LEFT  = true;
const bool INVERT_RIGHT = true;

// Motion PWM (tuned: left=179, right=180, turn=179)
const int PWM_FWD   = 180;      // Base speed (max of left/right)
const int PWM_TURN  = 179;      // Tốc độ khi quay
const int PWM_BRAKE = 150;      // Lực phanh nghịch đảo

// Gyro Config (tuned: target 90 = 90.0)
const float TURN_90_TARGET  = 90.0;  // Target 90 degrees
const float TURN_180_TARGET = 180.0; // Target 180 degrees
const float GYRO_OFFSET_THRESHOLD = 2.0; // Bỏ qua nhiễu nhỏ

// Gain (Cân bằng động cơ khi chạy thẳng - tuned: 179/180)
static float leftGain  = 0.9944f;  // 179/180
static float rightGain = 1.00f;   

// PWM Properties
const int MOTOR_PWM_FREQ = 20000;
const int MOTOR_PWM_RES  = 8;

// Obstacle (VL53L0X)
const int OBSTACLE_MM             = 220;
const int OBSTACLE_RESUME_MM      = 300;
const int OBSTACLE_BEEP_PERIOD_MS = 600;
static bool obstacleHold = false;

// Timing
const unsigned long TELEMETRY_MS    = 2000;
const unsigned long POLL_MS         = 1500;
const unsigned long CANCEL_POLL_MS  = 2500;
const unsigned long OLED_MS         = 200;
const unsigned long WEB_OK_SHOW_MS  = 3000;
const unsigned long WEB_OK_ALIVE_MS = 10000;
const unsigned long SWITCH_DEBOUNCE_MS = 60;
const unsigned long NFC_REPEAT_GUARD_MS = 700;

static unsigned long nfcIgnoreUntil = 0;
static inline void ignoreNfcFor(unsigned long ms) { nfcIgnoreUntil = millis() + ms; }
static inline bool nfcAllowed() { return millis() >= nfcIgnoreUntil; }

// Turn Overlay
static char lastTurnChar = 'F';
static unsigned long turnOverlayUntil = 0;
static inline void showTurnOverlay(char a, unsigned long ms = 1500) {
  lastTurnChar = a;
  turnOverlayUntil = millis() + ms;
}

// =========================================
// 3. OBJECTS
// =========================================
Adafruit_PN532 nfc(PN532_SS); 
VL53L0X tof;
bool tofOk = false;
Adafruit_MPU6050 mpu;
bool mpuOk = false;
U8G2_SH1106_128X64_NONAME_F_HW_I2C oled(U8G2_R0, U8X8_PIN_NONE);

// =========================================
// 4. API & STATE
// =========================================
Preferences prefs;
static const char* PREF_NS = "carrycfg";
static char apiBase[96] = "http://192.168.1.121:3000";

static bool shouldSaveConfig = false;
static void saveConfigCallback() { shouldSaveConfig = true; }

unsigned long lastTelemetry = 0;
unsigned long lastPoll = 0;
unsigned long lastCancelPoll = 0;
unsigned long lastObstacleBeep = 0;
unsigned long lastOLED = 0;
unsigned long lastWebOkAt = 0;
unsigned long webOkUntil = 0;

enum RunState { IDLE_AT_MED, RUN_OUTBOUND, WAIT_AT_DEST, RUN_RETURN };
RunState state = IDLE_AT_MED;

struct RoutePoint {
  String nodeId;
  String rfidUid;
  float x;
  float y;
  char action; 
};

String activeMissionId = "";
String activeMissionStatus = "";
String patientName = "";
String bedId = "";

std::vector<RoutePoint> outbound;
std::vector<RoutePoint> retRoute;
int routeIndex = 0;
bool haveSeenMED = false;

static bool cargoRaw = true;
static bool cargoStable = true;
static unsigned long cargoLastChange = 0;
static String lastNfcUid = "";
static unsigned long lastNfcAt = 0;
static bool cancelPending = false;          
static bool destUturnedBeforeWait = false;

// =========================================
// 5. HELPER FUNCTIONS
// =========================================
static String joinUrl(const char* base, const String& path) {
  String u(base);
  if (u.endsWith("/")) u.remove(u.length() - 1);
  if (!path.startsWith("/")) u += "/";
  u += path;
  return u;
}

static void markWebOk() {
  lastWebOkAt = millis();
  webOkUntil = lastWebOkAt + WEB_OK_SHOW_MS;
}

static bool webConnected() {
  return (lastWebOkAt > 0) && (millis() - lastWebOkAt <= WEB_OK_ALIVE_MS);
}

static String truncStr(const String& s, size_t maxLen) {
  if (s.length() <= (int)maxLen) return s;
  return s.substring(0, maxLen);
}

static void updateCargoSwitch() {
  bool r = digitalRead(CARGO_SWITCH_PIN);
  if (r != cargoRaw) {
    cargoRaw = r;
    cargoLastChange = millis();
  }
  if ((millis() - cargoLastChange) >= SWITCH_DEBOUNCE_MS) {
    cargoStable = cargoRaw;
  }
}
static bool cargoHeld() { return cargoStable == LOW; }

static void buzzerInit() {
  ledcAttach((uint8_t)BUZZER_PIN, 2000, 8);
  ledcWriteTone((uint8_t)BUZZER_PIN, 0);
}

static void toneOff() { ledcWriteTone((uint8_t)BUZZER_PIN, 0); }

static void beepOnce(int ms = 80, int freq = 2200) {
  ledcWriteTone((uint8_t)BUZZER_PIN, (uint32_t)freq);
  delay(ms);
  ledcWriteTone((uint8_t)BUZZER_PIN, 0);
}

static void beepArrivedPattern() {
  for (int i = 0; i < 3; i++) {
    beepOnce(140, 1800);
    delay(90);
  }
}

// =========================================
// 6. MOTOR CONTROL & GYRO
// =========================================

static inline uint8_t clampDuty(int v) {
  if (v < 0) return 0;
  if (v > 255) return 255;
  return (uint8_t)v;
}

static void motorPwmInit() {
  ledcAttach((uint8_t)EN_LEFT, MOTOR_PWM_FREQ, MOTOR_PWM_RES);
  ledcAttach((uint8_t)EN_RIGHT, MOTOR_PWM_FREQ, MOTOR_PWM_RES);
  ledcWrite((uint8_t)EN_LEFT, 0);
  ledcWrite((uint8_t)EN_RIGHT, 0);
}

static void setSideSpeed(int leftPwm, int rightPwm) {
  ledcWrite((uint8_t)EN_LEFT, clampDuty(leftPwm));
  ledcWrite((uint8_t)EN_RIGHT, clampDuty(rightPwm));
}

static void motorsStop() {
  setSideSpeed(0, 0);
  digitalWrite(FL_IN1, LOW); digitalWrite(FL_IN2, LOW);
  digitalWrite(RL_IN1, LOW); digitalWrite(RL_IN2, LOW);
  digitalWrite(FR_IN1, LOW); digitalWrite(FR_IN2, LOW);
  digitalWrite(RR_IN1, LOW); digitalWrite(RR_IN2, LOW);
}

// Hàm tính Gain cho chạy thẳng
static inline int applyGainDuty(int pwm, float gain) {
  if (pwm <= 0) return 0;
  if (gain < 0.0f) gain = 0.0f;
  float v = (float)pwm * gain;
  if (v > 255.0f) v = 255.0f;
  return (int)(v + 0.5f);
}

// Chạy thẳng
static void driveForward(int pwm) {
  int l = applyGainDuty(pwm, leftGain);
  int r = applyGainDuty(pwm, rightGain);
  setSideSpeed(l, r);

  // Left Side Forward (respects INVERT_LEFT)
  digitalWrite(FL_IN1, INVERT_LEFT ? LOW : HIGH);  digitalWrite(FL_IN2, INVERT_LEFT ? HIGH : LOW);
  digitalWrite(RL_IN1, INVERT_LEFT ? LOW : HIGH);  digitalWrite(RL_IN2, INVERT_LEFT ? HIGH : LOW);
  // Right Side Forward (respects INVERT_RIGHT)
  digitalWrite(FR_IN1, INVERT_RIGHT ? LOW : HIGH); digitalWrite(FR_IN2, INVERT_RIGHT ? HIGH : LOW);
  digitalWrite(RR_IN1, INVERT_RIGHT ? LOW : HIGH); digitalWrite(RR_IN2, INVERT_RIGHT ? HIGH : LOW);
}

// Chạy lùi
static void driveBackward(int pwm) {
  int l = applyGainDuty(pwm, leftGain);
  int r = applyGainDuty(pwm, rightGain);
  setSideSpeed(l, r);

  // Left Side Backward (respects INVERT_LEFT)
  digitalWrite(FL_IN1, INVERT_LEFT ? HIGH : LOW);  digitalWrite(FL_IN2, INVERT_LEFT ? LOW : HIGH);
  digitalWrite(RL_IN1, INVERT_LEFT ? HIGH : LOW);  digitalWrite(RL_IN2, INVERT_LEFT ? LOW : HIGH);
  // Right Side Backward (respects INVERT_RIGHT)
  digitalWrite(FR_IN1, INVERT_RIGHT ? HIGH : LOW); digitalWrite(FR_IN2, INVERT_RIGHT ? LOW : HIGH);
  digitalWrite(RR_IN1, INVERT_RIGHT ? HIGH : LOW); digitalWrite(RR_IN2, INVERT_RIGHT ? LOW : HIGH);
}

// Cấu hình chân để quay TRÁI tại chỗ (Trái lùi, Phải tiến) - respects inversion
static void setMotorDirLeft() {
  // Left Side Backward
  digitalWrite(FL_IN1, INVERT_LEFT ? HIGH : LOW);  digitalWrite(FL_IN2, INVERT_LEFT ? LOW : HIGH);
  digitalWrite(RL_IN1, INVERT_LEFT ? HIGH : LOW);  digitalWrite(RL_IN2, INVERT_LEFT ? LOW : HIGH);
  // Right Side Forward
  digitalWrite(FR_IN1, INVERT_RIGHT ? LOW : HIGH); digitalWrite(FR_IN2, INVERT_RIGHT ? HIGH : LOW);
  digitalWrite(RR_IN1, INVERT_RIGHT ? LOW : HIGH); digitalWrite(RR_IN2, INVERT_RIGHT ? HIGH : LOW);
}

// Cấu hình chân để quay PHẢI tại chỗ (Trái tiến, Phải lùi) - respects inversion
static void setMotorDirRight() {
  // Left Side Forward
  digitalWrite(FL_IN1, INVERT_LEFT ? LOW : HIGH);  digitalWrite(FL_IN2, INVERT_LEFT ? HIGH : LOW);
  digitalWrite(RL_IN1, INVERT_LEFT ? LOW : HIGH);  digitalWrite(RL_IN2, INVERT_LEFT ? HIGH : LOW);
  // Right Side Backward
  digitalWrite(FR_IN1, INVERT_RIGHT ? HIGH : LOW); digitalWrite(FR_IN2, INVERT_RIGHT ? LOW : HIGH);
  digitalWrite(RR_IN1, INVERT_RIGHT ? HIGH : LOW); digitalWrite(RR_IN2, INVERT_RIGHT ? LOW : HIGH);
}

// --- HARD BRAKE LOGIC ---
static void applyHardBrake(bool wasTurningLeft) {
  // Đảo chiều motor trong thời gian ngắn để triệt tiêu quán tính
  if (wasTurningLeft) {
    // Vừa quay Trái -> Phanh bằng cách kích chiều Phải
    setMotorDirRight();
  } else {
    // Vừa quay Phải -> Phanh bằng cách kích chiều Trái
    setMotorDirLeft();
  }
  
  setSideSpeed(PWM_BRAKE, PWM_BRAKE);
  delay(100); // 100ms phanh
  motorsStop();
}

// --- GYRO TURN LOGIC ---
static void rotateWithGyro(float targetAngle, bool isLeft) {
  if (!mpuOk) {
    // Fallback: dùng delay nếu MPU lỗi
    setSideSpeed(PWM_TURN, PWM_TURN);
    if (isLeft) setMotorDirLeft(); else setMotorDirRight();
    delay(targetAngle > 100 ? 1240 : 620); 
    motorsStop();
    return;
  }

  motorsStop();
  delay(100); // Dừng hẳn trước khi đo

  float currentAngle = 0;
  unsigned long lastTime = millis();
  
  setSideSpeed(PWM_TURN, PWM_TURN);
  if (isLeft) setMotorDirLeft(); else setMotorDirRight();

  while (abs(currentAngle) < targetAngle) {
    sensors_event_t a, g, temp;
    mpu.getEvent(&a, &g, &temp);
    
    unsigned long now = millis();
    float dt = (now - lastTime) / 1000.0;
    lastTime = now;

    // g.gyro.z tính bằng rad/s. Đổi sang deg/s.
    float gyroZ = g.gyro.z * 57.2958; 

    // Cộng dồn góc (tích phân)
    if (abs(gyroZ) > GYRO_OFFSET_THRESHOLD) {
      currentAngle += gyroZ * dt;
    }
    
    // Safety break loop
    if (abs(currentAngle) > (targetAngle + 45)) break; 
  }

  // STOP & HARD BRAKE
  motorsStop();
  delay(10); // delay micro trước khi đảo chiều
  applyHardBrake(isLeft);
}

static void turnByAction(char a) {
  if (a == 'L') rotateWithGyro(TURN_90_TARGET, true);
  else if (a == 'R') rotateWithGyro(TURN_90_TARGET, false);
  else if (a == 'B') rotateWithGyro(TURN_180_TARGET, true); // U-turn ưu tiên quay trái
}

// =========================================
// 7. UID LOOKUP (FULL LIST)
// =========================================
static String uidLookupByNodeId(const String& nodeId) {
  // --- Beds/doors ---
  if (nodeId == "R1M1") return "35:FD:E1:83";
  if (nodeId == "R1M2") return "45:AB:49:83";
  if (nodeId == "R1M3") return "35:2E:CA:83";
  if (nodeId == "R1O1") return "45:0E:9D:83";
  if (nodeId == "R1O2") return "35:58:97:83";
  if (nodeId == "R1O3") return "35:F0:F8:83";
  if (nodeId == "R1D1") return "35:F6:EF:83";
  if (nodeId == "R1D2") return "45:C7:37:83";

  if (nodeId == "R2M1") return "35:1A:34:83";
  if (nodeId == "R2M2") return "45:BF:F6:83";
  if (nodeId == "R2M3") return "35:DC:8F:83";
  if (nodeId == "R2O1") return "45:35:C3:83";
  if (nodeId == "R2O2") return "45:27:34:83";
  if (nodeId == "R2O3") return "35:2A:2D:83";
  if (nodeId == "R2D1") return "35:4C:B8:83";
  if (nodeId == "R2D2") return "45:81:A4:83";

  if (nodeId == "R3M1") return "35:22:F5:83";
  if (nodeId == "R3M2") return "45:C2:B8:83";
  if (nodeId == "R3M3") return "35:BB:B1:83";
  if (nodeId == "R3O1") return "45:26:F3:83";
  if (nodeId == "R3O2") return "45:1D:A4:83";
  if (nodeId == "R3O3") return "35:1E:47:83";
  if (nodeId == "R3D1") return "35:45:AF:83";
  if (nodeId == "R3D2") return "35:35:BA:83";

  if (nodeId == "R4M1") return "45:83:FB:83";
  if (nodeId == "R4M2") return "45:8E:00:83";
  if (nodeId == "R4M3") return "35:4D:9B:83";
  if (nodeId == "R4O1") return "45:7D:5A:83";
  if (nodeId == "R4O2") return "35:DB:EA:83";
  if (nodeId == "R4O3") return "35:EB:18:83";
  if (nodeId == "R4D1") return "35:48:9F:83";
  if (nodeId == "R4D2") return "35:26:79:83";

  // --- Main path ---
  if (nodeId == "MED")   return "45:54:80:83";
  if (nodeId == "J4")    return "35:2C:3C:83";
  if (nodeId == "H_TOP") return "45:86:AC:83";
  if (nodeId == "H_BOT") return "45:79:31:83";
  if (nodeId == "H_MED") return "45:D3:91:83";

  return "";
}

// =========================================
// 8. NFC & TOF
// =========================================
static String bytesToUidString(const uint8_t* uid, uint8_t len) {
  String s;
  for (uint8_t i = 0; i < len; i++) {
    if (i) s += ":";
    if (uid[i] < 0x10) s += "0";
    s += String(uid[i], HEX);
  }
  s.toUpperCase();
  return s;
}

static String readNFCOnce() {
  uint8_t uid[7];
  uint8_t uidLength = 0;

  bool ok = nfc.readPassiveTargetID(PN532_MIFARE_ISO14443A, uid, &uidLength, 20); 
  if (!ok || uidLength == 0) return "";
  String s = bytesToUidString(uid, uidLength);

  unsigned long now = millis();
  if (s == lastNfcUid && (now - lastNfcAt) < NFC_REPEAT_GUARD_MS) return "";
  lastNfcUid = s;
  lastNfcAt = now;
  return s;
}

static uint16_t readTofMm() {
  if (!tofOk) return 65535;
  uint16_t mm = tof.readRangeContinuousMillimeters();
  if (tof.timeoutOccurred()) return 65535;
  return mm;
}

// =========================================
// 9. ROUTE LOGIC
// =========================================
static const std::vector<RoutePoint>& currentRoute() {
  return (state == RUN_RETURN) ? retRoute : outbound;
}

static String expectedNextUid() {
  const auto& r = currentRoute();
  if (routeIndex + 1 >= (int)r.size()) return "";
  return r[routeIndex + 1].rfidUid;
}

static String currentNodeIdSafe() {
  const auto& r = currentRoute();
  if (routeIndex >= 0 && routeIndex < (int)r.size()) return r[routeIndex].nodeId;
  return "";
}

static char upcomingTurnAtNextNode() {
  const auto& r = currentRoute();
  int idx = routeIndex + 1;
  if (idx >= 0 && idx < (int)r.size()) {
    char a = r[idx].action;
    a = (char)toupper((int)a);
    if (a == 'L' || a == 'R') return a;
  }
  return 'F';
}

static const char* turnCharLabel(char a) {
  if (a == 'L') return "L";
  if (a == 'R') return "R";
  if (a == 'B') return "B";
  return "-";
}

static char invertTurn(char a) {
  a = (char)toupper((int)a);
  if (a == 'L') return 'R';
  if (a == 'R') return 'L';
  return 'F';
}

static void buildReturnFromVisited() {
  if (outbound.size() < 2) return;
  if (routeIndex < 0) return;
  if (routeIndex >= (int)outbound.size()) routeIndex = (int)outbound.size() - 1;

  std::vector<RoutePoint> visited(outbound.begin(), outbound.begin() + (routeIndex + 1));
  std::reverse(visited.begin(), visited.end());
  
  auto findOutAction = [&](const String& nodeId) -> char {
    for (const auto& p : outbound) {
      if (p.nodeId == nodeId) return p.action;
    }
    return 'F';
  };

  for (auto& p : visited) {
    char oa = findOutAction(p.nodeId);
    p.action = invertTurn(oa);
  }

  if (!visited.empty()) visited[0].action = 'F';
  retRoute.swap(visited);
}

// =========================================
// 10. OLED DISPLAY
// =========================================
static void oledDraw() {
  oled.clearBuffer();
  oled.setFont(u8g2_font_6x10_tf);

  String l1;
  if (millis() < webOkUntil) {
    l1 = "WEB OK";
  } else {
    const char* st = (state == IDLE_AT_MED) ? "IDLE" : (state == RUN_OUTBOUND) ? "OUT" : (state == WAIT_AT_DEST) ? "WAIT" : "BACK";
    l1 = String("WEB:") + (webConnected() ? "OK " : "-- ") + st;
  }

  String n1 = "-", n2 = "-";
  const std::vector<RoutePoint>& r = currentRoute();
  int i1 = routeIndex + 1;
  int i2 = routeIndex + 2;
  if (i1 >= 0 && i1 < (int)r.size()) n1 = r[i1].nodeId;
  if (i2 >= 0 && i2 < (int)r.size()) n2 = r[i2].nodeId;

  String l2, l3, l4;
  if (state == IDLE_AT_MED) {
    l2 = String("BED: ") + (bedId.length() ? truncStr(bedId, 16) : "-");
    l3 = haveSeenMED ? "READY (MED OK)" : "SCAN MED...";
    if (activeMissionId.length() && haveSeenMED) {
      l4 = cargoHeld() ? "CARGO:OK -> GO" : "CARGO:LOAD...";
    } else {
      l4 = String("API: ") + truncStr(String(apiBase), 14);
    }
  } else if (state == WAIT_AT_DEST) {
    l2 = String("BED: ") + truncStr(bedId, 16);
    l3 = String("PT: ") + truncStr(patientName, 16);
    l4 = cargoHeld() ? "WAIT UNLOAD..." : "UNLOADED -> BACK";
  } else {
    l2 = String("BED: ") + truncStr(bedId, 16);
    l3 = String("PT: ") + truncStr(patientName, 16);
    if (millis() < turnOverlayUntil && (lastTurnChar == 'L' || lastTurnChar == 'R')) {
      l4 = String("TURN:") + turnCharLabel(lastTurnChar) + " @" + truncStr(currentNodeIdSafe(), 8);
    } else {
      char t = upcomingTurnAtNextNode();
      l4 = String("NEXT:") + truncStr(n1, 6) + ">" + truncStr(n2, 6) + " T:" + turnCharLabel(t);
    }
  }

  oled.drawStr(0, 12, l1.c_str());
  oled.drawStr(0, 26, l2.c_str());
  oled.drawStr(0, 40, l3.c_str());
  oled.drawStr(0, 54, l4.c_str());
  oled.sendBuffer();
}

// =========================================
// 11. COMMUNICATION
// =========================================
static bool httpJson(const String& url, const char* method, const String& body, String& out, int& code) {
  if (WiFi.status() != WL_CONNECTED) return false;
  HTTPClient http;
  http.begin(url);
  http.addHeader("Content-Type", "application/json");

  if (strcmp(method, "GET") == 0) code = http.GET();
  else if (strcmp(method, "POST") == 0) code = http.POST(body);
  else if (strcmp(method, "PUT") == 0) code = http.PUT(body);
  else { http.end(); return false; }

  out = http.getString();
  http.end();

  if (code >= 200 && code < 300) markWebOk();
  return true;
}

static void saveApiBase(const char* v) {
  prefs.begin(PREF_NS, false);
  prefs.putString("apiBase", String(v));
  prefs.end();
}

static void loadConfig() {
  prefs.begin(PREF_NS, true);
  String v = prefs.getString("apiBase", "");
  prefs.end();
  if (v.length() > 0) strlcpy(apiBase, v.c_str(), sizeof(apiBase));
}

static void clearConfig() {
  prefs.begin(PREF_NS, false);
  prefs.clear();
  prefs.end();
}

static void sendTelemetry() {
  if (WiFi.status() != WL_CONNECTED) return;
  StaticJsonDocument<256> doc;
  doc["name"] = String("Carry ") + ROBOT_ID;
  doc["type"] = "carry";
  doc["batteryLevel"] = 100;
  doc["firmwareVersion"] = "carry-ino-full-v2";
  bool busy = (state != IDLE_AT_MED) || (activeMissionId.length() > 0);
  doc["status"] = busy ? "busy" : "idle";
  String body; serializeJson(doc, body);
  String url = joinUrl(apiBase, String("/api/robots/") + ROBOT_ID + "/telemetry");
  String resp; int code = 0;
  httpJson(url, "PUT", body, resp, code);
}

static bool fetchMission() {
  if (WiFi.status() != WL_CONNECTED) return false;
  String url = joinUrl(apiBase, String("/api/missions/carry/next?robotId=") + ROBOT_ID);
  String resp; int code = 0;
  if (!httpJson(url, "GET", "", resp, code)) return false;
  if (code != 200) return false;

  StaticJsonDocument<12288> doc;
  if (deserializeJson(doc, resp)) return false;

  JsonVariant m = doc["mission"];
  if (m.isNull()) {
    activeMissionId = ""; activeMissionStatus = "";
    patientName = ""; bedId = "";
    outbound.clear(); retRoute.clear(); routeIndex = 0;
    cancelPending = false; destUturnedBeforeWait = false;
    return false;
  }

  String mid = m["missionId"] | "";
  if (mid.length() == 0) return false;
  if (mid != activeMissionId) {
    routeIndex = 0; cancelPending = false; destUturnedBeforeWait = false;
  }

  activeMissionId = mid;
  activeMissionStatus = String((const char*)(m["status"] | ""));
  patientName = String((const char*)(m["patientName"] | ""));
  bedId = String((const char*)(m["bedId"] | ""));

  outbound.clear(); retRoute.clear();

  auto parseRoute = [](JsonArray arr, std::vector<RoutePoint>& outVec) {
    for (JsonObject p : arr) {
      RoutePoint rp;
      rp.nodeId  = String((const char*)(p["nodeId"] | ""));
      rp.rfidUid = String((const char*)(p["rfidUid"] | ""));
      rp.rfidUid.toUpperCase();
      rp.x = p["x"] | 0.0; rp.y = p["y"] | 0.0;
      const char* act = p["action"] | "";
      rp.action = 'F';
      if (act && act[0]) {
        char c = (char)toupper(act[0]);
        if (c=='L'||c=='R'||c=='B'||c=='F') rp.action = c;
      }
      if (rp.rfidUid.length() == 0) {
        rp.rfidUid = uidLookupByNodeId(rp.nodeId);
        rp.rfidUid.toUpperCase();
      }
      outVec.push_back(rp);
    }
  };

  parseRoute(m["outboundRoute"].as<JsonArray>(), outbound);
  parseRoute(m["returnRoute"].as<JsonArray>(), retRoute);

  if (retRoute.size() < 2 && outbound.size() >= 2) {
    retRoute = outbound;
    std::reverse(retRoute.begin(), retRoute.end());
  }
  return (outbound.size() >= 2);
}

static void sendProgress(const char* statusTextOrNull, const String& nodeId, const char* note = nullptr) {
  if (WiFi.status() != WL_CONNECTED) return;
  if (activeMissionId.length() == 0) return;
  StaticJsonDocument<256> doc;
  if (statusTextOrNull && statusTextOrNull[0]) doc["status"] = statusTextOrNull;
  doc["currentNodeId"] = nodeId;
  doc["batteryLevel"] = 100;
  if (note) doc["note"] = note;
  String body; serializeJson(doc, body);
  String url = joinUrl(apiBase, String("/api/missions/carry/") + activeMissionId + "/progress");
  String resp; int code = 0;
  httpJson(url, "PUT", body, resp, code);
}

static void sendComplete(const char* result = "ok") {
  if (WiFi.status() != WL_CONNECTED) return;
  if (activeMissionId.length() == 0) return;
  StaticJsonDocument<192> doc;
  doc["result"] = result;
  doc["note"] = "delivered; switch released; start return";
  String body; serializeJson(doc, body);
  String url = joinUrl(apiBase, String("/api/missions/carry/") + activeMissionId + "/complete");
  String resp; int code = 0;
  httpJson(url, "POST", body, resp, code);
}

static void sendReturned(const char* note = nullptr) {
  if (WiFi.status() != WL_CONNECTED) return;
  if (activeMissionId.length() == 0) return;
  StaticJsonDocument<192> doc;
  if (note) doc["note"] = note;
  String body; serializeJson(doc, body);
  String url = joinUrl(apiBase, String("/api/missions/carry/") + activeMissionId + "/returned");
  String resp; int code = 0;
  httpJson(url, "POST", body, resp, code);
}

// =========================================
// 12. STATE MACHINE TRANSITIONS
// =========================================
static void startOutbound() {
  state = RUN_OUTBOUND;
  routeIndex = 0;
  obstacleHold = false;
  ignoreNfcFor(600);
  cancelPending = false;
  destUturnedBeforeWait = false;
  if (outbound.size() > 0) {
    sendProgress("en_route", outbound[0].nodeId, "phase:outbound start");
  }
}

static void enterWaitAtDest() {
  state = WAIT_AT_DEST;
  motorsStop();
  toneOff();
  sendProgress("arrived", currentNodeIdSafe(), "phase:outbound arrived_bed");
}

static void startReturn(const char* note, bool doUturn) {
  motorsStop();
  toneOff();
  if (doUturn && destUturnedBeforeWait) doUturn = false;

  if (doUturn) {
    turnByAction('B'); // Gyro U-Turn
    ignoreNfcFor(900);
  } else {
    ignoreNfcFor(400);
  }

  state = RUN_RETURN;
  routeIndex = 0;
  obstacleHold = false;
  const char* st = "completed";
  if (activeMissionStatus == "cancelled") st = "cancelled";
  else if (activeMissionStatus == "failed") st = "failed";
  if (retRoute.size() > 0) sendProgress(st, retRoute[0].nodeId, note);
  else sendProgress(st, "", note);
}

static void goIdleReset() {
  motorsStop();
  toneOff();
  activeMissionId = ""; activeMissionStatus = "";
  patientName = ""; bedId = "";
  outbound.clear(); retRoute.clear(); routeIndex = 0;
  haveSeenMED = false; obstacleHold = false;
  lastTurnChar = 'F'; turnOverlayUntil = 0;
  cancelPending = false; destUturnedBeforeWait = false;
  state = IDLE_AT_MED;
}

static void setupWiFiManager(bool forceReset) {
  WiFi.mode(WIFI_STA);
  WiFiManager wm;
  wm.setConfigPortalTimeout(WIFI_PORTAL_TIMEOUT_S);
  wm.setConnectTimeout(WIFI_CONNECT_TIMEOUT_S);
  WiFiManagerParameter p_api("apiBase", "API Base", apiBase, sizeof(apiBase) - 1);
  wm.addParameter(&p_api);
  shouldSaveConfig = false;
  wm.setSaveConfigCallback(saveConfigCallback);

  if (forceReset) {
    wm.resetSettings();
    clearConfig();
  }

  String apName = String("CarryRobot-") + ROBOT_ID;
  bool ok = wm.autoConnect(apName.c_str());
  if (!ok) {
    oled.clearBuffer(); oled.drawStr(0, 12, "WIFI FAIL"); oled.sendBuffer();
    delay(800); ESP.restart();
  }

  const char* newApi = p_api.getValue();
  if (newApi && strlen(newApi) > 0) strlcpy(apiBase, newApi, sizeof(apiBase));
  if (shouldSaveConfig) saveApiBase(apiBase);
}

// =========================================
// 13. MAIN LOGIC LOOP
// =========================================
static void handleCheckpointHit(const String& uid) {
#if SERIAL_DEBUG
  Serial.print("NFC: "); Serial.println(uid);
#endif

  if (uid == String(HOME_MED_UID)) {
    haveSeenMED = true;
    if (state == RUN_RETURN && activeMissionId.length() > 0) {
      motorsStop(); toneOff();
      sendReturned(activeMissionStatus == "cancelled" ? "returned_after_cancel" : "returned_ok");
      goIdleReset();
      beepOnce(200, 2400);
    }
    return;
  }

  if (state != RUN_OUTBOUND && state != RUN_RETURN) return;
  const auto& route = currentRoute();
  if (route.size() < 2) return;

  String nextUid = expectedNextUid();
  if (nextUid.length() == 0) return;
  if (uid != nextUid) {
    motorsStop(); beepOnce(140, 1200);
    driveBackward(140); delay(250); motorsStop();
    ignoreNfcFor(500);
    return;
  }

  motorsStop();
  routeIndex++;
  if (state == RUN_OUTBOUND) {
    sendProgress("en_route", route[routeIndex].nodeId, "phase:outbound");
  } else {
    const char* st = (activeMissionStatus == "cancelled") ? "cancelled" : "completed";
    sendProgress(st, route[routeIndex].nodeId, "phase:return");
  }
  beepOnce(60, 2200);

  if (state == RUN_OUTBOUND && cancelPending) {
    cancelPending = false;
    buildReturnFromVisited();
    activeMissionStatus = "cancelled";
    startReturn("phase:return cancel_after_next_checkpoint", true);
    beepOnce(160, 1500);
    return;
  }

  char a = (char)toupper((int)route[routeIndex].action);
  if (a == 'L' || a == 'R') {
    showTurnOverlay(a, 1500);
    beepOnce(60, 2000);
    turnByAction(a); // Gyro Turn
    ignoreNfcFor(700);
  }

  if (state == RUN_OUTBOUND && routeIndex >= (int)outbound.size() - 1) {
    motorsStop(); toneOff();
    turnByAction('B'); // Gyro U-Turn
    destUturnedBeforeWait = true;
    ignoreNfcFor(900);
    enterWaitAtDest();
    return;
  }

  if (state == RUN_RETURN && routeIndex >= (int)retRoute.size() - 1) {
    sendReturned(activeMissionStatus == "cancelled" ? "returned_after_cancel" : "returned_ok");
    goIdleReset();
    beepOnce(200, 2400);
    return;
  }
}

void setup() {
#if SERIAL_DEBUG
  Serial.begin(115200);
#endif

  // --- Pin Init ---
  pinMode(FL_IN1, OUTPUT); pinMode(FL_IN2, OUTPUT);
  pinMode(RL_IN1, OUTPUT); pinMode(RL_IN2, OUTPUT);
  pinMode(FR_IN1, OUTPUT); pinMode(FR_IN2, OUTPUT);
  pinMode(RR_IN1, OUTPUT); pinMode(RR_IN2, OUTPUT);
  
  pinMode(TRIG_LEFT, OUTPUT); pinMode(ECHO_LEFT, INPUT);
  pinMode(TRIG_RIGHT, OUTPUT); pinMode(ECHO_RIGHT, INPUT);
  
  pinMode(CARGO_SWITCH_PIN, INPUT_PULLUP);
  cargoRaw = digitalRead(CARGO_SWITCH_PIN);
  cargoStable = cargoRaw;
  cargoLastChange = millis();

  motorPwmInit();
  motorsStop();
  buzzerInit();

  Wire.begin(I2C_SDA, I2C_SCL);
  
  oled.begin();
  oled.clearBuffer(); oled.setFont(u8g2_font_6x10_tf);
  oled.drawStr(0, 12, "BOOT SYSTEM..."); oled.sendBuffer();

  // --- MPU6050 Init ---
  if (!mpu.begin()) {
    oled.drawStr(0, 26, "MPU6050 FAIL"); oled.sendBuffer();
    mpuOk = false;
    delay(1000);
  } else {
    mpuOk = true;
    mpu.setAccelerometerRange(MPU6050_RANGE_8_G);
    mpu.setGyroRange(MPU6050_RANGE_500_DEG);
    mpu.setFilterBandwidth(MPU6050_BAND_21_HZ);
  }

  // --- ToF Init ---
  tof.setTimeout(120);
  tofOk = tof.init();
  if (tofOk) tof.startContinuous(50);

  // --- PN532 VSPI Init ---
  SPI.begin(PN532_SCK, PN532_MISO, PN532_MOSI, PN532_SS);
  nfc.begin();
  uint32_t ver = nfc.getFirmwareVersion();
  if (!ver) {
    oled.clearBuffer(); oled.drawStr(0, 12, "PN532 FAIL"); oled.sendBuffer();
    while (1) delay(100);
  }
  nfc.SAMConfig();
  nfc.setPassiveActivationRetries(0xFF);

  loadConfig();

  // --- Reset Config Logic ---
  bool forceReset = false;
  unsigned long t0 = millis();
  while (millis() - t0 < CFG_RESET_HOLD_MS) {
    updateCargoSwitch();
    if (!cargoHeld()) { forceReset = false; break; }
    forceReset = true;
    oled.clearBuffer(); oled.drawStr(0, 12, "HOLD -> RESET"); oled.sendBuffer();
    delay(50);
  }

  if (forceReset) {
    oled.clearBuffer(); oled.drawStr(0, 12, "RESET WIFI..."); oled.sendBuffer();
    delay(400);
  }

  setupWiFiManager(forceReset);

  goIdleReset();
  beepOnce(120, 2400);
}

void loop() {
  const unsigned long now = millis();

  updateCargoSwitch();
  
  if (now - lastOLED >= OLED_MS) {
    lastOLED = now;
    oledDraw();
  }

  if (now - lastTelemetry >= TELEMETRY_MS) {
    lastTelemetry = now;
    sendTelemetry();
  }

  if (nfcAllowed()) {
    String uid = readNFCOnce();
    if (uid.length() > 0) handleCheckpointHit(uid);
  }

  // --- IDLE STATE ---
  if (state == IDLE_AT_MED) {
    motorsStop(); toneOff();
    if (!haveSeenMED) return;
    if (now - lastPoll >= POLL_MS) {
      lastPoll = now;
      bool got = fetchMission();
      if (got) {
        if (activeMissionStatus == "cancelled") {
          sendReturned("cancelled_before_start");
          goIdleReset();
        } else {
          if (cargoHeld()) startOutbound();
        }
      }
    }
    return;
  }

  // --- WAIT STATE ---
  if (state == WAIT_AT_DEST) {
    motorsStop(); toneOff();
    if (now - lastCancelPoll >= CANCEL_POLL_MS) {
      lastCancelPoll = now;
      bool got = fetchMission();
      if (!got || activeMissionId.length() == 0) { goIdleReset(); return; }
      if (activeMissionStatus == "cancelled") {
        startReturn("phase:return cancelled-at-bed", false); return;
      }
    }
    if (!cargoHeld()) {
      beepArrivedPattern();
      sendComplete("ok");
      fetchMission();
      if (retRoute.size() < 2 && outbound.size() >= 2) {
        retRoute = outbound; std::reverse(retRoute.begin(), retRoute.end());
      }
      if (activeMissionStatus.length() == 0) activeMissionStatus = "completed";
      startReturn("phase:return start-after-unload", false);
    }
    return;
  }

  // --- RUNNING STATE ---
  if (state == RUN_OUTBOUND || state == RUN_RETURN) {
    static uint8_t tofBadCount = 0;
    uint16_t mm = readTofMm();
    bool timeoutOrNoSensor = (mm == 65535) || (!tofOk);
    bool tooClose = (!timeoutOrNoSensor && mm <= OBSTACLE_MM) || (mm == 0);
    if (timeoutOrNoSensor) {
      if (tofBadCount < 20) tofBadCount++;
      if (tofBadCount >= 5) tooClose = true;
    } else {
      tofBadCount = 0;
    }

    if (!obstacleHold) {
      if (tooClose) obstacleHold = true;
    } else {
      if (!timeoutOrNoSensor && mm >= OBSTACLE_RESUME_MM && mm != 0) {
        obstacleHold = false; tofBadCount = 0; toneOff();
      }
    }

    if (obstacleHold) {
      motorsStop();
      if (now - lastObstacleBeep >= (unsigned long)OBSTACLE_BEEP_PERIOD_MS) {
        lastObstacleBeep = now; beepOnce(120, 1800);
      }
      return;
    }

    if (now - lastCancelPoll >= CANCEL_POLL_MS) {
      lastCancelPoll = now;
      bool got = fetchMission();
      if (!got || activeMissionId.length() == 0) { goIdleReset(); return; }
      if (activeMissionStatus == "cancelled" && state == RUN_OUTBOUND) {
        if (!cancelPending) {
          cancelPending = true; beepOnce(80, 1500);
          sendProgress("en_route", currentNodeIdSafe(), "cancel_pending_next_checkpoint");
        }
      }
    }

    if (state == RUN_OUTBOUND && !cargoHeld()) {
      motorsStop();
      if (now - lastObstacleBeep >= 900) {
        lastObstacleBeep = now; beepOnce(60, 1400);
      }
      return;
    }

    const auto& route = currentRoute();
    if (route.size() < 2) { goIdleReset(); return; }
    String nextUid = expectedNextUid();
    if (nextUid.length() == 0) { motorsStop(); return; }

    driveForward(PWM_FWD);
  }
}