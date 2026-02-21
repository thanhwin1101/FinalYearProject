

#include <WiFi.h>
#include <PubSubClient.h>  // MQTT library - cài qua Library Manager hoặc PlatformIO
#include <ArduinoJson.h>

#include <WiFiManager.h>
#include <Preferences.h>

#include <SPI.h>
#include <Adafruit_PN532.h>

#include <Wire.h>
#include <VL53L0X.h>

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

// =========================================
// MQTT CONFIGURATION
// =========================================
static char mqttServer[64] = "192.168.0.102";  // IP máy chạy Mosquitto
static int  mqttPort = 1883;
static char mqttUser[32] = "hospital_robot";
static char mqttPass[32] = "123456";

// MQTT Topics
#define TOPIC_TELEMETRY    "hospital/robots/%s/telemetry"
#define TOPIC_MISSION_ASSIGN  "hospital/robots/%s/mission/assign"
#define TOPIC_MISSION_PROGRESS "hospital/robots/%s/mission/progress"
#define TOPIC_MISSION_COMPLETE "hospital/robots/%s/mission/complete"
#define TOPIC_MISSION_RETURNED "hospital/robots/%s/mission/returned"
#define TOPIC_MISSION_CANCEL   "hospital/robots/%s/mission/cancel"
#define TOPIC_MISSION_RETURN_ROUTE "hospital/robots/%s/mission/return_route"
#define TOPIC_POSITION_WAITING_RETURN "hospital/robots/%s/position/waiting_return"
#define TOPIC_COMMAND      "hospital/robots/%s/command"

// MQTT reconnect timing
const unsigned long MQTT_RECONNECT_MS = 5000;
static unsigned long lastMqttReconnect = 0;

// Motor Inversion (tuned)
const bool INVERT_LEFT  = true;
const bool INVERT_RIGHT = true;

// Motion PWM (tuned: left=179, right=180, turn=179)
const int PWM_FWD   = 165;      // Base speed (max of left/right)
const int PWM_TURN  = 168;      // Tốc độ khi quay
const int PWM_BRAKE = 150;      // Lực phanh nghịch đảo

// Time-based Turn Config (tuned values)
const unsigned long TURN_90_MS  = 974;   // Thời gian quay 90 độ (ms)
const unsigned long TURN_180_MS = 1980;  // ms for 180 degree turn (time-based)

// Turn Speed Zones (giảm tốc dần)
const int PWM_TURN_SLOW = 120;           // Tốc độ chậm khi gần đích
const int PWM_TURN_FINE = 90;            // Tốc độ rất chậm cuối
const float SLOW_ZONE_RATIO = 0.25;      // 25% cuối bắt đầu giảm tốc
const float FINE_ZONE_RATIO = 0.10;      // 10% cuối rất chậm

// Gain (Cân bằng động cơ khi chạy thẳng - tuned: 179/180)
static float leftGain  = 1.00f;  // 179/180
static float rightGain = 1.011f;   

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
U8G2_SH1106_128X64_NONAME_F_HW_I2C oled(U8G2_R0, U8X8_PIN_NONE);

// MQTT Client
WiFiClient espClient;
PubSubClient mqttClient(espClient);
static bool mqttConnected = false;

// =========================================
// 4. MQTT TOPICS & STATE
// =========================================
Preferences prefs;
static const char* PREF_NS = "carrycfg";

// Topic buffers (built at runtime with ROBOT_ID)
static char topicTelemetry[64];
static char topicMissionAssign[64];
static char topicMissionProgress[64];
static char topicMissionComplete[64];
static char topicMissionReturned[64];
static char topicMissionCancel[64];
static char topicMissionReturnRoute[64];
static char topicPositionWaitingReturn[64];
static char topicCommand[64];

static bool shouldSaveConfig = false;
static void saveConfigCallback() { shouldSaveConfig = true; }

unsigned long lastTelemetry = 0;
unsigned long lastPoll = 0;
unsigned long lastCancelPoll = 0;
unsigned long lastObstacleBeep = 0;
unsigned long lastOLED = 0;
unsigned long lastWebOkAt = 0;
unsigned long webOkUntil = 0;

enum RunState { IDLE_AT_MED, RUN_OUTBOUND, WAIT_AT_DEST, RUN_RETURN, WAIT_FOR_RETURN_ROUTE };
RunState state = IDLE_AT_MED;

// Position tracking for cancel/return
static String cancelAtNodeId = "";
static bool waitingForReturnRoute = false;

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

// Return route waiting timeout (5 seconds)
static const unsigned long RETURN_ROUTE_TIMEOUT_MS = 5000;
static unsigned long waitingReturnRouteStartTime = 0;

// =========================================
// 5. HELPER FUNCTIONS
// =========================================
static void markMqttOk() {
  lastWebOkAt = millis();
  webOkUntil = lastWebOkAt + WEB_OK_SHOW_MS;
}

static bool mqttOk() {
  return mqttConnected && mqttClient.connected();
}

static bool webConnected() {
  return mqttOk() && (lastWebOkAt > 0) && (millis() - lastWebOkAt <= WEB_OK_ALIVE_MS);
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

// --- FORWARD ACTIVE BRAKE ---
// Phanh chủ động khi đang đi thẳng (đảo chiều motor để dừng nhanh)
static void applyForwardBrake(int brakePwm = PWM_BRAKE, int brakeMs = 60) {
  // Đảo chiều: đang tiến -> kích lùi để phanh
  setSideSpeed(brakePwm, brakePwm);
  
  // Left Side Backward (respects INVERT_LEFT)
  digitalWrite(FL_IN1, INVERT_LEFT ? HIGH : LOW);  digitalWrite(FL_IN2, INVERT_LEFT ? LOW : HIGH);
  digitalWrite(RL_IN1, INVERT_LEFT ? HIGH : LOW);  digitalWrite(RL_IN2, INVERT_LEFT ? LOW : HIGH);
  // Right Side Backward (respects INVERT_RIGHT)
  digitalWrite(FR_IN1, INVERT_RIGHT ? HIGH : LOW); digitalWrite(FR_IN2, INVERT_RIGHT ? LOW : HIGH);
  digitalWrite(RR_IN1, INVERT_RIGHT ? HIGH : LOW); digitalWrite(RR_IN2, INVERT_RIGHT ? LOW : HIGH);
  
  delay(brakeMs);
  motorsStop();
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
static void applyHardBrake(bool wasTurningLeft, int brakePwm = PWM_BRAKE, int brakeMs = 80) {
  // Đảo chiều motor trong thời gian ngắn để triệt tiêu quán tính
  if (wasTurningLeft) {
    // Vừa quay Trái -> Phanh bằng cách kích chiều Phải
    setMotorDirRight();
  } else {
    // Vừa quay Phải -> Phanh bằng cách kích chiều Trái
    setMotorDirLeft();
  }
  
  setSideSpeed(brakePwm, brakePwm);
  delay(brakeMs);
  motorsStop();
}

// --- TIME-BASED TURN with Speed Zones & Active Braking ---
static void rotateByTime(unsigned long totalMs, bool isLeft) {
  motorsStop();
  delay(50); // Dừng hẳn trước khi quay

  // Tính các mốc thời gian
  unsigned long slowZoneStart = (unsigned long)(totalMs * (1.0 - SLOW_ZONE_RATIO));  // 75% tổng thời gian
  unsigned long fineZoneStart = (unsigned long)(totalMs * (1.0 - FINE_ZONE_RATIO));  // 90% tổng thời gian
  
  // Set direction
  if (isLeft) setMotorDirLeft(); else setMotorDirRight();
  
  unsigned long startTime = millis();
  int currentPwm = PWM_TURN;
  setSideSpeed(currentPwm, currentPwm);

  while (true) {
    unsigned long elapsed = millis() - startTime;
    if (elapsed >= totalMs) break;
    
    // Điều chỉnh tốc độ theo vùng thời gian
    int newPwm;
    if (elapsed >= fineZoneStart) {
      newPwm = PWM_TURN_FINE;  // Rất chậm 10% cuối
    } else if (elapsed >= slowZoneStart) {
      // Giảm dần từ PWM_TURN_SLOW xuống PWM_TURN_FINE
      float progress = (float)(elapsed - slowZoneStart) / (fineZoneStart - slowZoneStart);
      newPwm = PWM_TURN_SLOW - (int)((PWM_TURN_SLOW - PWM_TURN_FINE) * progress);
    } else {
      newPwm = PWM_TURN;  // Tốc độ bình thường 75% đầu
    }
    
    // Chỉ update PWM khi thay đổi đáng kể
    if (abs(newPwm - currentPwm) >= 8) {
      currentPwm = newPwm;
      setSideSpeed(currentPwm, currentPwm);
    }
    
    delay(5); // ~200Hz loop rate
  }

  // STOP
  motorsStop();
  delay(15);
  
  // Hard brake - nhẹ hơn nếu đã giảm tốc
  int brakePwm = (currentPwm < PWM_TURN_SLOW) ? (PWM_BRAKE / 2) : PWM_BRAKE;
  int brakeMs = (currentPwm < PWM_TURN_SLOW) ? 40 : 60;
  applyHardBrake(isLeft, brakePwm, brakeMs);

#if SERIAL_DEBUG
  Serial.print("Turn "); Serial.print(isLeft ? "L" : "R");
  Serial.print(" time="); Serial.print(totalMs);
  Serial.println("ms");
#endif
}

static void turnByAction(char a) {
  if (a == 'L') rotateByTime(TURN_90_MS, true);
  else if (a == 'R') rotateByTime(TURN_90_MS, false);
  else if (a == 'B') rotateByTime(TURN_180_MS, true); // U-turn ưu tiên quay trái
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
    const char* st = (state == IDLE_AT_MED) ? "IDLE" : 
                     (state == RUN_OUTBOUND) ? "OUT" : 
                     (state == WAIT_AT_DEST) ? "WAIT" : 
                     (state == WAIT_FOR_RETURN_ROUTE) ? "RTWT" : "BACK";
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
      l4 = String("MQTT:") + truncStr(String(mqttServer), 13);
    }
  } else if (state == WAIT_AT_DEST) {
    l2 = String("BED: ") + truncStr(bedId, 16);
    l3 = String("PT: ") + truncStr(patientName, 16);
    l4 = cargoHeld() ? "WAIT UNLOAD..." : "UNLOADED -> BACK";
  } else if (state == WAIT_FOR_RETURN_ROUTE) {
    l2 = String("CANCELLED AT:");
    l3 = truncStr(cancelAtNodeId, 16);
    unsigned long elapsed = (millis() - waitingReturnRouteStartTime) / 1000;
    l4 = String("WAIT ROUTE... ") + String(elapsed) + "s";
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
// 11. MQTT COMMUNICATION
// =========================================

// Forward declarations
static void parseMissionPayload(const char* payload);
static void parseCommandPayload(const char* payload);
static void parseCancelPayload(const char* payload);
static void parseReturnRoutePayload(const char* payload);

// MQTT Callback - Nhận message từ broker
static void mqttCallback(char* topic, byte* payload, unsigned int length) {
  // Null-terminate payload
  char* msg = (char*)malloc(length + 1);
  if (!msg) return;
  memcpy(msg, payload, length);
  msg[length] = '\0';

#if SERIAL_DEBUG
  Serial.print("MQTT Recv ["); Serial.print(topic); Serial.print("]: ");
  Serial.println(msg);
#endif

  markMqttOk();

  // Dispatch based on topic
  if (strcmp(topic, topicMissionAssign) == 0) {
    parseMissionPayload(msg);
  } else if (strcmp(topic, topicMissionCancel) == 0) {
    parseCancelPayload(msg);
  } else if (strcmp(topic, topicMissionReturnRoute) == 0) {
    parseReturnRoutePayload(msg);
  } else if (strcmp(topic, topicCommand) == 0) {
    parseCommandPayload(msg);
  }

  free(msg);
}

static void buildTopics() {
  snprintf(topicTelemetry, sizeof(topicTelemetry), TOPIC_TELEMETRY, ROBOT_ID);
  snprintf(topicMissionAssign, sizeof(topicMissionAssign), TOPIC_MISSION_ASSIGN, ROBOT_ID);
  snprintf(topicMissionProgress, sizeof(topicMissionProgress), TOPIC_MISSION_PROGRESS, ROBOT_ID);
  snprintf(topicMissionComplete, sizeof(topicMissionComplete), TOPIC_MISSION_COMPLETE, ROBOT_ID);
  snprintf(topicMissionReturned, sizeof(topicMissionReturned), TOPIC_MISSION_RETURNED, ROBOT_ID);
  snprintf(topicMissionCancel, sizeof(topicMissionCancel), TOPIC_MISSION_CANCEL, ROBOT_ID);
  snprintf(topicMissionReturnRoute, sizeof(topicMissionReturnRoute), TOPIC_MISSION_RETURN_ROUTE, ROBOT_ID);
  snprintf(topicPositionWaitingReturn, sizeof(topicPositionWaitingReturn), TOPIC_POSITION_WAITING_RETURN, ROBOT_ID);
  snprintf(topicCommand, sizeof(topicCommand), TOPIC_COMMAND, ROBOT_ID);
}

static void mqttReconnect() {
  if (mqttClient.connected()) {
    mqttConnected = true;
    return;
  }

  unsigned long now = millis();
  if (now - lastMqttReconnect < MQTT_RECONNECT_MS) return;
  lastMqttReconnect = now;

#if SERIAL_DEBUG
  Serial.print("MQTT connecting to "); Serial.print(mqttServer);
  Serial.print(":"); Serial.println(mqttPort);
#endif

  String clientId = String("CarryRobot-") + ROBOT_ID + "-" + String(random(0xffff), HEX);
  
  if (mqttClient.connect(clientId.c_str(), mqttUser, mqttPass)) {
    mqttConnected = true;
    markMqttOk();
    
    // Subscribe to relevant topics
    mqttClient.subscribe(topicMissionAssign);
    mqttClient.subscribe(topicMissionCancel);
    mqttClient.subscribe(topicMissionReturnRoute);  // Backend sends return route
    mqttClient.subscribe(topicCommand);
    
#if SERIAL_DEBUG
    Serial.println("MQTT connected, subscribed to topics");
#endif
    beepOnce(60, 2400);
  } else {
    mqttConnected = false;
#if SERIAL_DEBUG
    Serial.print("MQTT failed, rc="); Serial.println(mqttClient.state());
#endif
  }
}

static void mqttPublish(const char* topic, const String& payload, bool retained = false) {
  if (!mqttClient.connected()) return;
  mqttClient.publish(topic, payload.c_str(), retained);
  markMqttOk();
}

static void saveMqttConfig() {
  prefs.begin(PREF_NS, false);
  prefs.putString("mqttServer", String(mqttServer));
  prefs.putInt("mqttPort", mqttPort);
  prefs.putString("mqttUser", String(mqttUser));
  prefs.putString("mqttPass", String(mqttPass));
  prefs.end();
}

static void loadConfig() {
  prefs.begin(PREF_NS, true);
  String srv = prefs.getString("mqttServer", "");
  if (srv.length() > 0) strlcpy(mqttServer, srv.c_str(), sizeof(mqttServer));
  mqttPort = prefs.getInt("mqttPort", 1883);
  String usr = prefs.getString("mqttUser", "");
  if (usr.length() > 0) strlcpy(mqttUser, usr.c_str(), sizeof(mqttUser));
  String pwd = prefs.getString("mqttPass", "");
  if (pwd.length() > 0) strlcpy(mqttPass, pwd.c_str(), sizeof(mqttPass));
  prefs.end();
}

static void clearConfig() {
  prefs.begin(PREF_NS, false);
  prefs.clear();
  prefs.end();
}

// Parse mission assignment from MQTT
static void parseMissionPayload(const char* payload) {
  StaticJsonDocument<12288> doc;
  if (deserializeJson(doc, payload)) return;

  JsonVariant m = doc["mission"];
  if (m.isNull()) m = doc.as<JsonVariant>(); // Mission might be root object

  String mid = m["missionId"] | "";
  if (mid.length() == 0) return;

  // Ignore if we already have an active mission
  if (activeMissionId.length() > 0 && activeMissionId != mid) {
#if SERIAL_DEBUG
    Serial.println("Ignoring mission - already have active mission");
#endif
    return;
  }

  if (mid != activeMissionId) {
    routeIndex = 0; cancelPending = false; destUturnedBeforeWait = false;
  }

  activeMissionId = mid;
  activeMissionStatus = String((const char*)(m["status"] | "pending"));
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

  beepOnce(100, 2000);
#if SERIAL_DEBUG
  Serial.print("Mission received: "); Serial.println(mid);
  Serial.print("Bed: "); Serial.println(bedId);
#endif
}

// Parse cancel command from MQTT
static void parseCancelPayload(const char* payload) {
  StaticJsonDocument<256> doc;
  if (deserializeJson(doc, payload)) return;

  String mid = doc["missionId"] | "";
  if (mid.length() == 0 || mid != activeMissionId) return;

  activeMissionStatus = "cancelled";
  
  if (state == RUN_OUTBOUND) {
    cancelPending = true;
    beepOnce(80, 1500);
#if SERIAL_DEBUG
    Serial.println("Cancel pending - will stop at next checkpoint");
#endif
  } else if (state == WAIT_AT_DEST) {
    // Will be handled in main loop
#if SERIAL_DEBUG
    Serial.println("Cancel received at destination");
#endif
  }
}

// Parse command from MQTT (stop, resume, etc.)
static void parseCommandPayload(const char* payload) {
  StaticJsonDocument<256> doc;
  if (deserializeJson(doc, payload)) return;

  String cmd = doc["command"] | "";
  if (cmd == "stop") {
    motorsStop();
    obstacleHold = true;
    beepOnce(200, 1200);
  } else if (cmd == "resume") {
    obstacleHold = false;
    beepOnce(60, 2400);
  }
}

static void sendTelemetry() {
  if (!mqttClient.connected()) return;
  StaticJsonDocument<256> doc;
  doc["robotId"] = ROBOT_ID;
  doc["name"] = String("Carry ") + ROBOT_ID;
  doc["type"] = "carry";
  doc["batteryLevel"] = 100;
  doc["firmwareVersion"] = "carry-mqtt-v1";
  bool busy = (state != IDLE_AT_MED) || (activeMissionId.length() > 0);
  doc["status"] = busy ? "busy" : "idle";
  doc["mqttConnected"] = mqttConnected;
  String payload; serializeJson(doc, payload);
  mqttPublish(topicTelemetry, payload);
}

static void sendProgress(const char* statusTextOrNull, const String& nodeId, const char* note = nullptr) {
  if (!mqttClient.connected()) return;
  if (activeMissionId.length() == 0) return;
  StaticJsonDocument<256> doc;
  doc["missionId"] = activeMissionId;
  if (statusTextOrNull && statusTextOrNull[0]) doc["status"] = statusTextOrNull;
  doc["currentNodeId"] = nodeId;
  doc["batteryLevel"] = 100;
  if (note) doc["note"] = note;
  String payload; serializeJson(doc, payload);
  mqttPublish(topicMissionProgress, payload);
}

static void sendComplete(const char* result = "ok") {
  if (!mqttClient.connected()) return;
  if (activeMissionId.length() == 0) return;
  StaticJsonDocument<192> doc;
  doc["missionId"] = activeMissionId;
  doc["result"] = result;
  doc["note"] = "delivered; switch released; start return";
  String payload; serializeJson(doc, payload);
  mqttPublish(topicMissionComplete, payload);
}

static void sendReturned(const char* note = nullptr) {
  if (!mqttClient.connected()) return;
  if (activeMissionId.length() == 0) return;
  StaticJsonDocument<192> doc;
  doc["missionId"] = activeMissionId;
  if (note) doc["note"] = note;
  String payload; serializeJson(doc, payload);
  mqttPublish(topicMissionReturned, payload);
}

// Send current position to Backend and wait for return route
static void sendPositionWaitingReturn(const String& currentNodeId) {
  if (!mqttClient.connected()) return;
  if (activeMissionId.length() == 0) return;
  
  StaticJsonDocument<192> doc;
  doc["missionId"] = activeMissionId;
  doc["currentNodeId"] = currentNodeId;
  String payload; serializeJson(doc, payload);
  mqttPublish(topicPositionWaitingReturn, payload);
  
#if SERIAL_DEBUG
  Serial.print("Sent position waiting for return: "); Serial.println(currentNodeId);
#endif
}

// Parse return route from Backend
static void parseReturnRoutePayload(const char* payload) {
  StaticJsonDocument<2048> doc;  // Larger for route array
  if (deserializeJson(doc, payload)) {
#if SERIAL_DEBUG
    Serial.println("parseReturnRoutePayload: JSON parse error");
#endif
    return;
  }

  String mid = doc["missionId"] | "";
  if (mid.length() == 0 || mid != activeMissionId) return;
  
  String status = doc["status"] | "";
  if (status != "ok") {
#if SERIAL_DEBUG
    Serial.println("Return route status error - fallback to local");
#endif
    // Fallback: use local calculation
    buildReturnFromVisited();
    waitingForReturnRoute = false;
    state = RUN_RETURN;
    routeIndex = 0;
    obstacleHold = false;
    beepOnce(120, 1800);
    return;
  }

  // Clear existing return route
  retRoute.clear();
  
  // Parse return route array
  JsonArray arr = doc["returnRoute"].as<JsonArray>();
  if (arr.isNull() || arr.size() < 2) {
#if SERIAL_DEBUG
    Serial.println("Return route too short - fallback to local");
#endif
    buildReturnFromVisited();
    waitingForReturnRoute = false;
    state = RUN_RETURN;
    routeIndex = 0;
    obstacleHold = false;
    beepOnce(120, 1800);
    return;
  }

  for (JsonVariant v : arr) {
    RoutePoint rp;
    rp.nodeId = v["nodeId"] | "";
    rp.rfidUid = v["rfidUid"] | "";
    rp.x = v["x"] | 0.0f;
    rp.y = v["y"] | 0.0f;
    rp.action = (v["action"] | "F")[0];
    if (rp.nodeId.length() > 0) {
      retRoute.push_back(rp);
    }
  }

#if SERIAL_DEBUG
  Serial.print("Received return route from Backend: ");
  Serial.print(retRoute.size()); Serial.println(" nodes");
  for (int i = 0; i < (int)retRoute.size(); i++) {
    Serial.print("  "); Serial.print(i); Serial.print(": ");
    Serial.print(retRoute[i].nodeId); Serial.print(" act=");
    Serial.println(retRoute[i].action);
  }
#endif

  // Start return with Backend route
  waitingForReturnRoute = false;
  state = RUN_RETURN;
  routeIndex = 0;
  obstacleHold = false;
  beepOnce(120, 2400);
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
  
  // MQTT Parameters
  char portStr[8];
  snprintf(portStr, sizeof(portStr), "%d", mqttPort);
  WiFiManagerParameter p_server("mqttServer", "MQTT Server", mqttServer, sizeof(mqttServer) - 1);
  WiFiManagerParameter p_port("mqttPort", "MQTT Port", portStr, 7);
  WiFiManagerParameter p_user("mqttUser", "MQTT User", mqttUser, sizeof(mqttUser) - 1);
  WiFiManagerParameter p_pass("mqttPass", "MQTT Password", mqttPass, sizeof(mqttPass) - 1);
  
  wm.addParameter(&p_server);
  wm.addParameter(&p_port);
  wm.addParameter(&p_user);
  wm.addParameter(&p_pass);
  
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

  // Save MQTT config if changed
  const char* newServer = p_server.getValue();
  if (newServer && strlen(newServer) > 0) strlcpy(mqttServer, newServer, sizeof(mqttServer));
  const char* newPort = p_port.getValue();
  if (newPort && strlen(newPort) > 0) mqttPort = atoi(newPort);
  const char* newUser = p_user.getValue();
  if (newUser && strlen(newUser) > 0) strlcpy(mqttUser, newUser, sizeof(mqttUser));
  const char* newPass = p_pass.getValue();
  if (newPass && strlen(newPass) > 0) strlcpy(mqttPass, newPass, sizeof(mqttPass));
  
  if (shouldSaveConfig) saveMqttConfig();
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
      applyForwardBrake();
      toneOff();
      // Thêm: Quay 180 độ khi về MED
      turnByAction('B'); // U-turn ưu tiên quay trái
      ignoreNfcFor(900);
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
    applyForwardBrake(); beepOnce(140, 1200);
    driveBackward(140); delay(250); motorsStop();
    ignoreNfcFor(500);
    return;
  }

  applyForwardBrake();  // Phanh chủ động khi quét đúng checkpoint
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
    activeMissionStatus = "cancelled";
    
    // Do U-turn first
    applyForwardBrake();
    turnByAction('B');  // 180 degree turn
    ignoreNfcFor(900);
    
    // Get current node ID (the checkpoint we just reached)
    String currentNode = route[routeIndex].nodeId;
    cancelAtNodeId = currentNode;
    
    // Send position to Backend and wait for return route
    sendPositionWaitingReturn(currentNode);
    waitingForReturnRoute = true;
    waitingReturnRouteStartTime = millis();
    state = WAIT_FOR_RETURN_ROUTE;
    
#if SERIAL_DEBUG
    Serial.print("Cancel at checkpoint: "); Serial.println(currentNode);
    Serial.println("Waiting for return route from Backend...");
#endif
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
    applyForwardBrake(); toneOff();
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

  // --- MQTT Init ---
  buildTopics();
  mqttClient.setServer(mqttServer, mqttPort);
  mqttClient.setCallback(mqttCallback);
  mqttClient.setBufferSize(8192); // Tăng buffer cho mission payloads lớn
  
  // Initial MQTT connection
  oled.clearBuffer(); oled.drawStr(0, 12, "MQTT CONNECTING..."); oled.sendBuffer();
  mqttReconnect();

  goIdleReset();
  beepOnce(120, 2400);
}

void loop() {
  const unsigned long now = millis();

  // --- MQTT Maintenance ---
  if (!mqttClient.connected()) {
    mqttReconnect();
  }
  mqttClient.loop(); // Process incoming messages

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
  // Với MQTT, mission được push tới qua callback parseMissionPayload()
  // Không cần polling như HTTP
  if (state == IDLE_AT_MED) {
    motorsStop(); toneOff();
    if (!haveSeenMED) return;
    
    // Kiểm tra nếu đã nhận mission qua MQTT
    if (activeMissionId.length() > 0 && outbound.size() >= 2) {
      if (activeMissionStatus == "cancelled") {
        sendReturned("cancelled_before_start");
        goIdleReset();
      } else {
        if (cargoHeld()) startOutbound();
      }
    }
    return;
  }

  // --- WAIT STATE ---
  if (state == WAIT_AT_DEST) {
    motorsStop(); toneOff();
    
    // Cancel được xử lý qua MQTT callback parseCancelPayload()
    if (activeMissionStatus == "cancelled") {
      startReturn("phase:return cancelled-at-bed", false); 
      return;
    }
    
    if (!cargoHeld()) {
      beepArrivedPattern();
      sendComplete("ok");
      // Với MQTT, không cần fetchMission() - route đã có sẵn
      if (retRoute.size() < 2 && outbound.size() >= 2) {
        retRoute = outbound; std::reverse(retRoute.begin(), retRoute.end());
      }
      if (activeMissionStatus.length() == 0) activeMissionStatus = "completed";
      startReturn("phase:return start-after-unload", false);
    }
    return;
  }

  // --- WAIT FOR RETURN ROUTE FROM BACKEND ---
  if (state == WAIT_FOR_RETURN_ROUTE) {
    motorsStop(); toneOff();
    
    // Check timeout - fallback to local calculation if no response
    if (millis() - waitingReturnRouteStartTime > RETURN_ROUTE_TIMEOUT_MS) {
#if SERIAL_DEBUG
      Serial.println("Return route timeout - using local calculation");
#endif
      buildReturnFromVisited();
      waitingForReturnRoute = false;
      state = RUN_RETURN;
      routeIndex = 0;
      obstacleHold = false;
      beepOnce(120, 1200);
    }
    
    // Return route will be set by parseReturnRoutePayload() via MQTT callback
    // which will change state to RUN_RETURN
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

    // Cancel được xử lý qua MQTT callback - chỉ cần check flag
    if (activeMissionStatus == "cancelled" && state == RUN_OUTBOUND && !cancelPending) {
      cancelPending = true; 
      beepOnce(80, 1500);
      sendProgress("en_route", currentNodeIdSafe(), "cancel_pending_next_checkpoint");
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