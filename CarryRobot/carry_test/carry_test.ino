/*
  Carry Robot Motor Test - Web Control Version
  
  Features:
  - Web interface for motor control
  - Adjustable turn timing via web
  - Real-time OLED display
  - WiFiManager for easy setup
  
  Access: http://<robot_ip>/ after connecting to WiFi
*/

#include <WiFi.h>
#include <WebServer.h>
#include <WiFiManager.h>
#include <Preferences.h>
#include <Wire.h>
#include <U8g2lib.h>

// =========================================
// PINOUT
// =========================================
#define I2C_SDA 21
#define I2C_SCL 22

// Motors (L298N)
#define EN_LEFT   17  
#define FL_IN1    32
#define FL_IN2    33
#define RL_IN1    25
#define RL_IN2    26

#define EN_RIGHT  16  
#define FR_IN1    27
#define FR_IN2    14
#define RR_IN1    13
#define RR_IN2    4

#define BUZZER_PIN 2

// =========================================
// CONFIG
// =========================================
const bool INVERT_LEFT  = true;
const bool INVERT_RIGHT = true;

// Motion PWM (adjustable)
int PWM_FWD   = 165;      // Tốc độ đi thẳng (0-255)
int PWM_TURN  = 179;      // Tốc độ quay (0-255)
const int PWM_BRAKE = 150;

// Time-based Turn (adjustable)
unsigned long TURN_90_MS  = 620;
unsigned long TURN_180_MS = 1240;

// Turn Speed Zones
const int PWM_TURN_SLOW = 120;
const int PWM_TURN_FINE = 90;
const float SLOW_ZONE_RATIO = 0.25;
const float FINE_ZONE_RATIO = 0.10;

// Gain for straight driving
static float leftGain  = 1.00f;
static float rightGain = 1.011f;

// PWM Properties
const int MOTOR_PWM_FREQ = 20000;
const int MOTOR_PWM_RES  = 8;

// =========================================
// OBJECTS
// =========================================
U8G2_SH1106_128X64_NONAME_F_HW_I2C oled(U8G2_R0, U8X8_PIN_NONE);
WebServer server(80);
Preferences prefs;

String lastAction = "READY";
String robotIP = "";

// =========================================
// MOTOR FUNCTIONS
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

static inline int applyGainDuty(int pwm, float gain) {
  if (pwm <= 0) return 0;
  float v = (float)pwm * gain;
  if (v > 255.0f) v = 255.0f;
  return (int)(v + 0.5f);
}

// Forward
static void driveForward(int pwm) {
  int l = applyGainDuty(pwm, leftGain);
  int r = applyGainDuty(pwm, rightGain);
  setSideSpeed(l, r);

  digitalWrite(FL_IN1, INVERT_LEFT ? LOW : HIGH);  digitalWrite(FL_IN2, INVERT_LEFT ? HIGH : LOW);
  digitalWrite(RL_IN1, INVERT_LEFT ? LOW : HIGH);  digitalWrite(RL_IN2, INVERT_LEFT ? HIGH : LOW);
  digitalWrite(FR_IN1, INVERT_RIGHT ? LOW : HIGH); digitalWrite(FR_IN2, INVERT_RIGHT ? HIGH : LOW);
  digitalWrite(RR_IN1, INVERT_RIGHT ? LOW : HIGH); digitalWrite(RR_IN2, INVERT_RIGHT ? HIGH : LOW);
}

// Backward
static void driveBackward(int pwm) {
  int l = applyGainDuty(pwm, leftGain);
  int r = applyGainDuty(pwm, rightGain);
  setSideSpeed(l, r);

  digitalWrite(FL_IN1, INVERT_LEFT ? HIGH : LOW);  digitalWrite(FL_IN2, INVERT_LEFT ? LOW : HIGH);
  digitalWrite(RL_IN1, INVERT_LEFT ? HIGH : LOW);  digitalWrite(RL_IN2, INVERT_LEFT ? LOW : HIGH);
  digitalWrite(FR_IN1, INVERT_RIGHT ? HIGH : LOW); digitalWrite(FR_IN2, INVERT_RIGHT ? LOW : HIGH);
  digitalWrite(RR_IN1, INVERT_RIGHT ? HIGH : LOW); digitalWrite(RR_IN2, INVERT_RIGHT ? LOW : HIGH);
}

// Turn Left direction (Left backward, Right forward)
static void setMotorDirLeft() {
  digitalWrite(FL_IN1, INVERT_LEFT ? HIGH : LOW);  digitalWrite(FL_IN2, INVERT_LEFT ? LOW : HIGH);
  digitalWrite(RL_IN1, INVERT_LEFT ? HIGH : LOW);  digitalWrite(RL_IN2, INVERT_LEFT ? LOW : HIGH);
  digitalWrite(FR_IN1, INVERT_RIGHT ? LOW : HIGH); digitalWrite(FR_IN2, INVERT_RIGHT ? HIGH : LOW);
  digitalWrite(RR_IN1, INVERT_RIGHT ? LOW : HIGH); digitalWrite(RR_IN2, INVERT_RIGHT ? HIGH : LOW);
}

// Turn Right direction (Left forward, Right backward)
static void setMotorDirRight() {
  digitalWrite(FL_IN1, INVERT_LEFT ? LOW : HIGH);  digitalWrite(FL_IN2, INVERT_LEFT ? HIGH : LOW);
  digitalWrite(RL_IN1, INVERT_LEFT ? LOW : HIGH);  digitalWrite(RL_IN2, INVERT_LEFT ? HIGH : LOW);
  digitalWrite(FR_IN1, INVERT_RIGHT ? HIGH : LOW); digitalWrite(FR_IN2, INVERT_RIGHT ? LOW : HIGH);
  digitalWrite(RR_IN1, INVERT_RIGHT ? HIGH : LOW); digitalWrite(RR_IN2, INVERT_RIGHT ? LOW : HIGH);
}

// Hard Brake
static void applyHardBrake(bool wasTurningLeft, int brakePwm = PWM_BRAKE, int brakeMs = 80) {
  if (wasTurningLeft) {
    setMotorDirRight();
  } else {
    setMotorDirLeft();
  }
  setSideSpeed(brakePwm, brakePwm);
  delay(brakeMs);
  motorsStop();
}

// Time-based rotation with speed zones
static void rotateByTime(unsigned long totalMs, bool isLeft) {
  motorsStop();
  delay(50);

  unsigned long slowZoneStart = (unsigned long)(totalMs * (1.0 - SLOW_ZONE_RATIO));
  unsigned long fineZoneStart = (unsigned long)(totalMs * (1.0 - FINE_ZONE_RATIO));
  
  // Calculate slow/fine speeds based on current PWM_TURN
  int pwmSlow = (PWM_TURN * 2) / 3;  // ~67% of turn speed
  int pwmFine = PWM_TURN / 2;        // ~50% of turn speed
  if (pwmSlow < 80) pwmSlow = 80;
  if (pwmFine < 60) pwmFine = 60;
  
  if (isLeft) setMotorDirLeft(); else setMotorDirRight();
  
  unsigned long startTime = millis();
  int currentPwm = PWM_TURN;
  setSideSpeed(currentPwm, currentPwm);

  while (true) {
    unsigned long elapsed = millis() - startTime;
    if (elapsed >= totalMs) break;
    
    int newPwm;
    if (elapsed >= fineZoneStart) {
      newPwm = pwmFine;
    } else if (elapsed >= slowZoneStart) {
      float progress = (float)(elapsed - slowZoneStart) / (fineZoneStart - slowZoneStart);
      newPwm = pwmSlow - (int)((pwmSlow - pwmFine) * progress);
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
  
  // Active braking - đảo chiều motor để dừng nhanh
  int brakePwm = (currentPwm < pwmSlow) ? (PWM_BRAKE / 2) : PWM_BRAKE;
  int brakeMs = (currentPwm < pwmSlow) ? 40 : 60;
  applyHardBrake(isLeft, brakePwm, brakeMs);
}

// =========================================
// BUZZER
// =========================================
static void buzzerInit() {
  ledcAttach((uint8_t)BUZZER_PIN, 2000, 8);
  ledcWriteTone((uint8_t)BUZZER_PIN, 0);
}

static void beepOnce(int ms = 80, int freq = 2200) {
  ledcWriteTone((uint8_t)BUZZER_PIN, (uint32_t)freq);
  delay(ms);
  ledcWriteTone((uint8_t)BUZZER_PIN, 0);
}

// =========================================
// OLED
// =========================================
void updateOLED() {
  oled.clearBuffer();
  oled.setFont(u8g2_font_6x10_tf);
  
  oled.drawStr(0, 12, "=== MOTOR TEST ===");
  oled.drawStr(0, 26, ("Action: " + lastAction).c_str());
  
  char buf90[24], buf180[24];
  sprintf(buf90, "TURN_90: %lu ms", TURN_90_MS);
  sprintf(buf180, "TURN_180: %lu ms", TURN_180_MS);
  oled.drawStr(0, 40, buf90);
  
  if (robotIP.length() > 0) {
    oled.drawStr(0, 54, robotIP.c_str());
  } else {
    oled.drawStr(0, 54, buf180);
  }
  
  oled.sendBuffer();
}

// =========================================
// SAVE/LOAD CONFIG
// =========================================
void saveConfig() {
  prefs.begin("motortest", false);
  prefs.putULong("turn90", TURN_90_MS);
  prefs.putULong("turn180", TURN_180_MS);
  prefs.putInt("pwmFwd", PWM_FWD);
  prefs.putInt("pwmTurn", PWM_TURN);
  prefs.end();
}

void loadConfig() {
  prefs.begin("motortest", true);
  TURN_90_MS = prefs.getULong("turn90", 620);
  TURN_180_MS = prefs.getULong("turn180", 1240);
  PWM_FWD = prefs.getInt("pwmFwd", 165);
  PWM_TURN = prefs.getInt("pwmTurn", 179);
  prefs.end();
}

// =========================================
// WEB SERVER HANDLERS
// =========================================
const char HTML_PAGE[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1.0">
  <title>Carry Robot Test</title>
  <style>
    * { box-sizing: border-box; margin: 0; padding: 0; }
    body { 
      font-family: Arial, sans-serif; 
      background: linear-gradient(135deg, #1a1a2e 0%, #16213e 100%);
      min-height: 100vh; 
      padding: 20px;
      color: #fff;
    }
    .container { max-width: 500px; margin: 0 auto; }
    h1 { text-align: center; margin-bottom: 20px; color: #00d4ff; }
    
    .section {
      background: rgba(255,255,255,0.1);
      border-radius: 15px;
      padding: 20px;
      margin-bottom: 20px;
    }
    .section h2 { 
      color: #00d4ff; 
      margin-bottom: 15px; 
      font-size: 1.1em;
      border-bottom: 1px solid rgba(255,255,255,0.2);
      padding-bottom: 10px;
    }
    
    .info-box {
      background: rgba(0,212,255,0.1);
      border: 1px solid rgba(0,212,255,0.3);
      border-radius: 8px;
      padding: 10px;
      margin-bottom: 15px;
      font-size: 0.9em;
    }
    .info-box .label { color: #aaa; }
    .info-box .value { color: #4CAF50; font-weight: bold; }
    
    .btn-grid {
      display: grid;
      grid-template-columns: repeat(3, 1fr);
      gap: 10px;
    }
    .btn-grid.single { grid-template-columns: 1fr; }
    
    .btn {
      padding: 20px 10px;
      font-size: 16px;
      font-weight: bold;
      border: none;
      border-radius: 10px;
      cursor: pointer;
      transition: all 0.2s;
      text-transform: uppercase;
    }
    .btn:active { transform: scale(0.95); }
    
    .btn-fwd { background: #4CAF50; color: white; grid-column: 2; }
    .btn-back { background: #ff9800; color: white; grid-column: 2; }
    .btn-left { background: #2196F3; color: white; }
    .btn-right { background: #2196F3; color: white; }
    .btn-stop { background: #f44336; color: white; }
    .btn-uturn { background: #9c27b0; color: white; }
    .btn-save { background: #00bcd4; color: white; }
    
    .btn:hover { filter: brightness(1.2); }
    
    .config-row {
      display: flex;
      align-items: center;
      justify-content: space-between;
      margin-bottom: 12px;
      padding: 10px;
      background: rgba(0,0,0,0.2);
      border-radius: 8px;
    }
    .config-row label { flex: 1; font-size: 0.95em; }
    .config-row input {
      width: 80px;
      padding: 8px;
      border: none;
      border-radius: 5px;
      font-size: 16px;
      text-align: center;
    }
    .config-row .unit { margin-left: 5px; color: #aaa; min-width: 30px; }
    
    .adjust-btns {
      display: flex;
      gap: 5px;
      margin-left: 8px;
    }
    .adjust-btns button {
      width: 32px;
      height: 32px;
      font-size: 16px;
      border: none;
      border-radius: 5px;
      cursor: pointer;
      background: #444;
      color: white;
    }
    .adjust-btns button:hover { background: #666; }
    
    .slider-row {
      margin-bottom: 12px;
      padding: 10px;
      background: rgba(0,0,0,0.2);
      border-radius: 8px;
    }
    .slider-row label {
      display: flex;
      justify-content: space-between;
      margin-bottom: 8px;
    }
    .slider-row input[type="range"] {
      width: 100%;
      height: 8px;
      -webkit-appearance: none;
      background: #333;
      border-radius: 4px;
      outline: none;
    }
    .slider-row input[type="range"]::-webkit-slider-thumb {
      -webkit-appearance: none;
      width: 20px;
      height: 20px;
      background: #00d4ff;
      border-radius: 50%;
      cursor: pointer;
    }
    
    .status {
      text-align: center;
      padding: 15px;
      background: rgba(0,212,255,0.1);
      border-radius: 10px;
      margin-top: 10px;
    }
    .status span { 
      font-size: 1.2em; 
      color: #00d4ff;
      font-weight: bold;
    }
    
    #log {
      background: #000;
      color: #0f0;
      padding: 10px;
      border-radius: 8px;
      height: 80px;
      overflow-y: auto;
      font-family: monospace;
      font-size: 12px;
    }
  </style>
</head>
<body>
  <div class="container">
    <h1>🤖 Carry Robot Test</h1>
    
    <div class="section">
      <h2>📍 Movement Control</h2>
      <div class="info-box">
        <span class="label">Active Brake:</span> <span class="value">✓ Enabled</span> |
        <span class="label">Brake PWM:</span> <span class="value">150</span>
      </div>
      <div class="btn-grid">
        <div></div>
        <button class="btn btn-fwd" onclick="cmd('F')">▲ Forward</button>
        <div></div>
        <button class="btn btn-left" onclick="cmd('L')">◀ Left 90°</button>
        <button class="btn btn-stop" onclick="cmd('S')">■ STOP</button>
        <button class="btn btn-right" onclick="cmd('R')">Right 90° ▶</button>
        <div></div>
        <button class="btn btn-back" onclick="cmd('B')">▼ Backward</button>
        <div></div>
      </div>
      <div class="btn-grid single" style="margin-top:10px;">
        <button class="btn btn-uturn" onclick="cmd('U')">↻ U-Turn 180°</button>
      </div>
    </div>
    
    <div class="section">
      <h2>🚀 Speed Config</h2>
      <div class="slider-row">
        <label>Forward Speed: <span id="pwmFwdVal">%PWMFWD%</span> / 255</label>
        <input type="range" id="pwmFwd" min="80" max="255" value="%PWMFWD%" oninput="document.getElementById('pwmFwdVal').textContent=this.value">
      </div>
      <div class="slider-row">
        <label>Turn Speed: <span id="pwmTurnVal">%PWMTURN%</span> / 255</label>
        <input type="range" id="pwmTurn" min="80" max="255" value="%PWMTURN%" oninput="document.getElementById('pwmTurnVal').textContent=this.value">
      </div>
    </div>
    
    <div class="section">
      <h2>⏱️ Turn Timing</h2>
      <div class="config-row">
        <label>Turn 90°:</label>
        <input type="number" id="turn90" value="%TURN90%" step="10">
        <span class="unit">ms</span>
        <div class="adjust-btns">
          <button onclick="adjust('turn90', -20)">-</button>
          <button onclick="adjust('turn90', 20)">+</button>
        </div>
      </div>
      <div class="config-row">
        <label>Turn 180°:</label>
        <input type="number" id="turn180" value="%TURN180%" step="20">
        <span class="unit">ms</span>
        <div class="adjust-btns">
          <button onclick="adjust('turn180', -40)">-</button>
          <button onclick="adjust('turn180', 40)">+</button>
        </div>
      </div>
      <div class="btn-grid single">
        <button class="btn btn-save" onclick="saveConfig()">💾 Save All Config</button>
      </div>
    </div>
    
    <div class="section">
      <h2>📋 Log</h2>
      <div id="log"></div>
      <div class="status">Last Action: <span id="lastAction">READY</span></div>
    </div>
  </div>
  
  <script>
    function log(msg) {
      const el = document.getElementById('log');
      const time = new Date().toLocaleTimeString();
      el.innerHTML += `[${time}] ${msg}\n`;
      el.scrollTop = el.scrollHeight;
    }
    
    function cmd(c) {
      const pwmF = document.getElementById('pwmFwd').value;
      const pwmT = document.getElementById('pwmTurn').value;
      log(`Cmd: ${c} (Fwd:${pwmF}, Turn:${pwmT})`);
      fetch(`/cmd?c=${c}&pf=${pwmF}&pt=${pwmT}`)
        .then(r => r.text())
        .then(d => {
          log(`Result: ${d}`);
          document.getElementById('lastAction').textContent = d;
        })
        .catch(e => log(`Error: ${e}`));
    }
    
    function adjust(id, delta) {
      const el = document.getElementById(id);
      let val = parseInt(el.value) + delta;
      if (val < 100) val = 100;
      el.value = val;
    }
    
    function saveConfig() {
      const t90 = document.getElementById('turn90').value;
      const t180 = document.getElementById('turn180').value;
      const pf = document.getElementById('pwmFwd').value;
      const pt = document.getElementById('pwmTurn').value;
      log(`Saving: T90=${t90}ms, T180=${t180}ms, PwmFwd=${pf}, PwmTurn=${pt}`);
      fetch(`/save?t90=${t90}&t180=${t180}&pf=${pf}&pt=${pt}`)
        .then(r => r.text())
        .then(d => log(`Saved: ${d}`))
        .catch(e => log(`Error: ${e}`));
    }
    
    log('Web interface ready');
    log('Active braking: ENABLED');
  </script>
</body>
</html>
)rawliteral";

void handleRoot() {
  String html = FPSTR(HTML_PAGE);
  html.replace("%TURN90%", String(TURN_90_MS));
  html.replace("%TURN180%", String(TURN_180_MS));
  html.replace("%PWMFWD%", String(PWM_FWD));
  html.replace("%PWMTURN%", String(PWM_TURN));
  server.send(200, "text/html", html);
}

void handleCmd() {
  if (!server.hasArg("c")) {
    server.send(400, "text/plain", "Missing cmd");
    return;
  }
  
  // Get PWM values from request (temporary, for testing)
  int tempPwmFwd = PWM_FWD;
  int tempPwmTurn = PWM_TURN;
  if (server.hasArg("pf")) {
    tempPwmFwd = server.arg("pf").toInt();
    if (tempPwmFwd < 80) tempPwmFwd = 80;
    if (tempPwmFwd > 255) tempPwmFwd = 255;
    PWM_FWD = tempPwmFwd;  // Update global for current session
  }
  if (server.hasArg("pt")) {
    tempPwmTurn = server.arg("pt").toInt();
    if (tempPwmTurn < 80) tempPwmTurn = 80;
    if (tempPwmTurn > 255) tempPwmTurn = 255;
    PWM_TURN = tempPwmTurn;  // Update global for current session
  }
  
  char cmd = server.arg("c").charAt(0);
  String result = "OK";
  
  switch (cmd) {
    case 'F':
      lastAction = "FWD@" + String(tempPwmFwd);
      updateOLED();
      beepOnce(50, 1800);
      driveForward(tempPwmFwd);
      delay(2000);
      motorsStop();
      result = "FORWARD done (PWM:" + String(tempPwmFwd) + ")";
      break;
      
    case 'B':
      lastAction = "BACK@" + String(tempPwmFwd);
      updateOLED();
      beepOnce(50, 1800);
      driveBackward(tempPwmFwd);
      delay(2000);
      motorsStop();
      result = "BACKWARD done (PWM:" + String(tempPwmFwd) + ")";
      break;
      
    case 'L':
      lastAction = "L90@" + String(tempPwmTurn);
      updateOLED();
      beepOnce(50, 2000);
      rotateByTime(TURN_90_MS, true);
      result = "LEFT done (" + String(TURN_90_MS) + "ms, PWM:" + String(tempPwmTurn) + ")";
      break;
      
    case 'R':
      lastAction = "R90@" + String(tempPwmTurn);
      updateOLED();
      beepOnce(50, 2000);
      rotateByTime(TURN_90_MS, false);
      result = "RIGHT done (" + String(TURN_90_MS) + "ms, PWM:" + String(tempPwmTurn) + ")";
      break;
      
    case 'U':
      lastAction = "U180@" + String(tempPwmTurn);
      updateOLED();
      beepOnce(50, 2200);
      rotateByTime(TURN_180_MS, true);
      result = "U-TURN done (" + String(TURN_180_MS) + "ms, PWM:" + String(tempPwmTurn) + ")";
      break;
      
    case 'S':
      lastAction = "STOPPED";
      motorsStop();
      result = "STOPPED";
      break;
      
    default:
      result = "Unknown cmd";
      break;
  }
  
  lastAction = "READY";
  updateOLED();
  server.send(200, "text/plain", result);
}

void handleSave() {
  if (server.hasArg("t90")) {
    TURN_90_MS = server.arg("t90").toInt();
    if (TURN_90_MS < 100) TURN_90_MS = 100;
  }
  if (server.hasArg("t180")) {
    TURN_180_MS = server.arg("t180").toInt();
    if (TURN_180_MS < 200) TURN_180_MS = 200;
  }
  if (server.hasArg("pf")) {
    PWM_FWD = server.arg("pf").toInt();
    if (PWM_FWD < 80) PWM_FWD = 80;
    if (PWM_FWD > 255) PWM_FWD = 255;
  }
  if (server.hasArg("pt")) {
    PWM_TURN = server.arg("pt").toInt();
    if (PWM_TURN < 80) PWM_TURN = 80;
    if (PWM_TURN > 255) PWM_TURN = 255;
  }
  
  saveConfig();
  updateOLED();
  
  String msg = "T90=" + String(TURN_90_MS) + "ms, T180=" + String(TURN_180_MS) + "ms, Fwd=" + String(PWM_FWD) + ", Turn=" + String(PWM_TURN);
  server.send(200, "text/plain", msg);
  beepOnce(100, 2400);
}

// =========================================
// SETUP
// =========================================
void setup() {
  Serial.begin(115200);
  Serial.println("\n=== Carry Robot Motor Test (Web) ===");

  // Pin Init
  pinMode(FL_IN1, OUTPUT); pinMode(FL_IN2, OUTPUT);
  pinMode(RL_IN1, OUTPUT); pinMode(RL_IN2, OUTPUT);
  pinMode(FR_IN1, OUTPUT); pinMode(FR_IN2, OUTPUT);
  pinMode(RR_IN1, OUTPUT); pinMode(RR_IN2, OUTPUT);

  motorPwmInit();
  motorsStop();
  buzzerInit();

  Wire.begin(I2C_SDA, I2C_SCL);
  oled.begin();
  
  // Load saved config
  loadConfig();
  
  // OLED: Show boot
  oled.clearBuffer();
  oled.setFont(u8g2_font_6x10_tf);
  oled.drawStr(0, 12, "MOTOR TEST");
  oled.drawStr(0, 26, "Connecting WiFi...");
  oled.sendBuffer();
  
  // WiFiManager
  WiFiManager wm;
  wm.setConfigPortalTimeout(180);
  
  String apName = "CarryTest-Setup";
  bool connected = wm.autoConnect(apName.c_str(), "test1234");
  
  if (!connected) {
    oled.clearBuffer();
    oled.drawStr(0, 12, "WiFi FAILED");
    oled.drawStr(0, 26, "Restarting...");
    oled.sendBuffer();
    delay(2000);
    ESP.restart();
  }
  
  // Get IP
  robotIP = WiFi.localIP().toString();
  Serial.print("IP: ");
  Serial.println(robotIP);
  
  // Setup web server
  server.on("/", handleRoot);
  server.on("/cmd", handleCmd);
  server.on("/save", handleSave);
  server.begin();
  Serial.println("Web server started");
  
  // Show IP on OLED
  updateOLED();
  beepOnce(100, 2400);
  
  Serial.println("Ready! Open browser: http://" + robotIP);
}

// =========================================
// LOOP
// =========================================
void loop() {
  server.handleClient();
  
  // Serial commands still work
  if (Serial.available()) {
    char cmd = toupper(Serial.read());
    
    switch (cmd) {
      case 'F':
        Serial.println(">> FORWARD 2s");
        lastAction = "FORWARD";
        updateOLED();
        driveForward(PWM_FWD);
        delay(2000);
        motorsStop();
        break;
        
      case 'B':
        Serial.println(">> BACKWARD 2s");
        lastAction = "BACKWARD";
        updateOLED();
        driveBackward(PWM_FWD);
        delay(2000);
        motorsStop();
        break;
        
      case 'L':
        Serial.println(">> LEFT 90");
        lastAction = "LEFT 90";
        updateOLED();
        rotateByTime(TURN_90_MS, true);
        break;
        
      case 'R':
        Serial.println(">> RIGHT 90");
        lastAction = "RIGHT 90";
        updateOLED();
        rotateByTime(TURN_90_MS, false);
        break;
        
      case 'U':
        Serial.println(">> U-TURN 180");
        lastAction = "U-TURN";
        updateOLED();
        rotateByTime(TURN_180_MS, true);
        break;
        
      case 'S':
        Serial.println(">> STOP");
        lastAction = "STOPPED";
        motorsStop();
        break;
    }
    
    lastAction = "READY";
    updateOLED();
  }
}
