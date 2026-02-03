/*
 * ============================================================
 * BIPED ROBOT - USER MANAGER ESP32
 * ============================================================
 * Firmware cho ESP32 quản lý người dùng, giao tiếp UART với 
 * Walking Controller và gửi dữ liệu lên Dashboard
 * 
 * Chức năng:
 * - RFID RC522: Xác thực người dùng + quét checkpoint
 * - OLED 0.96": Hiển thị thông tin (user, steps, status)
 * - 4 Buttons: Forward, Backward, Turn Left, Turn Right
 * - Rotary Encoder: Điều chỉnh tốc độ
 * - WiFi HTTP: Gửi dữ liệu về Dashboard API
 * - UART: Giao tiếp với Walking Controller ESP32
 * ============================================================
 */

#include <Wire.h>
#include <SPI.h>
#include <WiFi.h>
#include <WiFiManager.h>  // WiFiManager by tzapu
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <MFRC522.h>
#include <U8g2lib.h>
#include <Preferences.h>

// ============================================================
// PIN DEFINITIONS
// ============================================================

// RFID RC522 (SPI)
#define RFID_SS_PIN     5
#define RFID_RST_PIN    4
#define RFID_SCK_PIN    18
#define RFID_MISO_PIN   19
#define RFID_MOSI_PIN   23

// OLED SSD1306 0.96" (I2C)
#define OLED_SDA_PIN    21
#define OLED_SCL_PIN    22

// Buttons
#define BTN_FORWARD_PIN   32
#define BTN_BACKWARD_PIN  33
#define BTN_LEFT_PIN      25
#define BTN_RIGHT_PIN     26
#define BTN_STOP_PIN      27  // Emergency stop / End session

// Rotary Encoder
#define ENC_CLK_PIN     34
#define ENC_DT_PIN      35
#define ENC_SW_PIN      39  // Encoder button (confirm)

// UART to Walking Controller
#define UART_TX_PIN     17
#define UART_RX_PIN     16
#define UART_BAUD       115200

// LED Status
#define LED_STATUS_PIN  2   // Built-in LED

// ============================================================
// CONFIGURATION
// ============================================================

// WiFi & API
const char* DEFAULT_SSID = "Hospital_WiFi";
const char* DEFAULT_PASSWORD = "hospital123";
const char* DEFAULT_API_URL = "http://192.168.1.100:3000/api";

// Robot ID
const char* ROBOT_ID = "BIPED-001";
const char* ROBOT_NAME = "Biped Robot 1";

// Timing (ms)
#define DEBOUNCE_MS         50
#define RFID_SCAN_INTERVAL  500
#define HEARTBEAT_INTERVAL  5000
#define STEP_UPDATE_INTERVAL 2000
#define DISPLAY_UPDATE_INTERVAL 200

// Speed limits
#define SPEED_MIN   10
#define SPEED_MAX   100
#define SPEED_DEFAULT 50
#define SPEED_STEP  5

// ============================================================
// USER DATABASE (RFID UID -> User Info)
// ============================================================

// Struct để lưu thông tin user từ API
struct UserInfo {
  char cardUid[20];     // RFID UID as hex string
  char odpatientId[32];   // Patient ID from database
  char userName[48];    // Patient full name
  char roomBed[16];     // Room/Bed info
  bool isValid;         // Is this a valid user
};

// Current user info (fetched from API)
UserInfo currentUser;

// Checkpoint database (RFID UID -> Location)
struct CheckpointInfo {
  byte uid[4];
  const char* checkpointId;
  const char* description;
};

const CheckpointInfo CHECKPOINT_DATABASE[] = {
  {{0xC1, 0xC2, 0xC3, 0xC4}, "CP_LOBBY", "Sanh chinh"},
  {{0xD1, 0xD2, 0xD3, 0xD4}, "CP_R1", "Phong 1"},
  {{0xE1, 0xE2, 0xE3, 0xE4}, "CP_R2", "Phong 2"},
  {{0xF1, 0xF2, 0xF3, 0xF4}, "CP_R3", "Phong 3"},
  {{0xA1, 0xA2, 0xA3, 0xA4}, "CP_R4", "Phong 4"},
  {{0xB1, 0xB2, 0xB3, 0xB4}, "CP_HALL", "Hanh lang"},
};
const int CHECKPOINT_COUNT = sizeof(CHECKPOINT_DATABASE) / sizeof(CHECKPOINT_DATABASE[0]);

// ============================================================
// GLOBAL OBJECTS
// ============================================================

// RFID
MFRC522 rfid(RFID_SS_PIN, RFID_RST_PIN);

// OLED - SSD1306 128x64 I2C
U8G2_SSD1306_128X64_NONAME_F_HW_I2C oled(U8G2_R0, U8X8_PIN_NONE, OLED_SCL_PIN, OLED_SDA_PIN);

// UART to Walking Controller
HardwareSerial WalkingSerial(2);  // UART2

// Preferences for persistent storage
Preferences prefs;

// ============================================================
// STATE VARIABLES
// ============================================================

// System state
enum SystemState {
  STATE_IDLE,           // Waiting for user
  STATE_SESSION_ACTIVE, // Rehabilitation session in progress
  STATE_ERROR           // Error state
};

SystemState currentState = STATE_IDLE;

// Session data
struct SessionData {
  char sessionId[32];
  char userId[16];
  char userName[32];
  char patientId[16];
  uint32_t stepCount;
  uint32_t startTime;
  bool isActive;
} session;

// Movement
uint8_t currentSpeed = SPEED_DEFAULT;
bool isMoving = false;
char lastCommand[16] = "";

// Location
char currentCheckpoint[16] = "UNKNOWN";

// WiFi & API
char wifiSSID[32];
char wifiPassword[64];
char apiBaseUrl[128];
bool wifiConnected = false;

// Timing
unsigned long lastRfidScan = 0;
unsigned long lastHeartbeat = 0;
unsigned long lastStepUpdate = 0;
unsigned long lastDisplayUpdate = 0;

// Buttons
struct ButtonState {
  int pin;
  bool lastState;
  unsigned long lastDebounce;
  bool pressed;
};

ButtonState buttons[] = {
  {BTN_FORWARD_PIN, HIGH, 0, false},
  {BTN_BACKWARD_PIN, HIGH, 0, false},
  {BTN_LEFT_PIN, HIGH, 0, false},
  {BTN_RIGHT_PIN, HIGH, 0, false},
  {BTN_STOP_PIN, HIGH, 0, false}
};
const int BTN_COUNT = 5;

// Encoder
volatile int encoderPos = SPEED_DEFAULT / SPEED_STEP;
int lastEncoderPos = -1;
volatile bool encoderBtnPressed = false;

// Balance status from Walking Controller
char balanceStatus[16] = "OK";

// Long press detection for WiFi setup
unsigned long forwardBtnPressStart = 0;
bool forwardLongPressTriggered = false;
#define WIFI_SETUP_HOLD_TIME 3000  // 3 seconds to trigger WiFi setup

// ============================================================
// FUNCTION PROTOTYPES
// ============================================================

void setupPins();
void setupRFID();
void setupOLED();
void setupWiFi();
void setupUART();
void loadConfig();
void saveConfig();

void handleRFID();
void handlePatientCard(const char* cardUid);
void handleCheckpointCard(const CheckpointInfo* cp);
void handleButtons();
void handleEncoder();
void handleUARTReceive();
void handleAPITasks();

void startSessionWithUser(UserInfo* user);
void endSession(const char* status);
void updateStepCount(uint32_t steps);
void reportLocation(const char* checkpoint);

void sendCommand(const char* cmd);
void sendSpeed(uint8_t speed);

bool apiGetPatientByCard(const char* cardUid, UserInfo* outUser);
bool apiStartSession();
bool apiUpdateSteps();
bool apiEndSession(const char* status);
bool apiReportLocation();
bool apiHeartbeat();

void updateDisplay();
void displayIdle();
void displaySession();
void displayError(const char* msg);
void displayConnecting();
void displayWiFiSetup();

void startWiFiManager();
void checkForwardLongPress();

String uidToHexString(byte* uid, byte size);
const CheckpointInfo* findCheckpoint(byte* uid);
bool compareUID(byte* uid1, byte* uid2);

// ============================================================
// SETUP
// ============================================================

void setup() {
  Serial.begin(115200);
  Serial.println("\n========================================");
  Serial.println("BIPED ROBOT - USER MANAGER");
  Serial.println("========================================");

  setupPins();
  loadConfig();
  setupOLED();
  
  // Check if forward button is held at boot -> force WiFi setup
  if (digitalRead(BTN_FORWARD_PIN) == LOW) {
    Serial.println("Forward button held at boot - starting WiFi Manager");
    startWiFiManager();
  } else {
    displayConnecting();
    setupWiFi();
  }
  
  setupRFID();
  setupUART();

  // Initialize session
  memset(&session, 0, sizeof(session));

  currentState = STATE_IDLE;
  
  Serial.println("Setup complete!");
  updateDisplay();
}

// ============================================================
// MAIN LOOP
// ============================================================

void loop() {
  unsigned long now = millis();

  // Handle RFID scanning
  if (now - lastRfidScan >= RFID_SCAN_INTERVAL) {
    lastRfidScan = now;
    handleRFID();
  }

  // Check for forward button long press (WiFi setup)
  checkForwardLongPress();

  // Handle button inputs
  handleButtons();

  // Handle encoder
  handleEncoder();

  // Handle UART from Walking Controller
  handleUARTReceive();

  // Handle API tasks (heartbeat, step updates)
  handleAPITasks();

  // Update display
  if (now - lastDisplayUpdate >= DISPLAY_UPDATE_INTERVAL) {
    lastDisplayUpdate = now;
    updateDisplay();
  }

  // Maintain WiFi connection
  if (WiFi.status() != WL_CONNECTED) {
    wifiConnected = false;
    static unsigned long lastReconnect = 0;
    if (now - lastReconnect >= 10000) {
      lastReconnect = now;
      Serial.println("WiFi disconnected, reconnecting...");
      WiFi.reconnect();
    }
  } else {
    wifiConnected = true;
  }
}

// ============================================================
// SETUP FUNCTIONS
// ============================================================

void setupPins() {
  // Buttons (INPUT_PULLUP)
  pinMode(BTN_FORWARD_PIN, INPUT_PULLUP);
  pinMode(BTN_BACKWARD_PIN, INPUT_PULLUP);
  pinMode(BTN_LEFT_PIN, INPUT_PULLUP);
  pinMode(BTN_RIGHT_PIN, INPUT_PULLUP);
  pinMode(BTN_STOP_PIN, INPUT_PULLUP);

  // Encoder
  pinMode(ENC_CLK_PIN, INPUT);
  pinMode(ENC_DT_PIN, INPUT);
  pinMode(ENC_SW_PIN, INPUT_PULLUP);

  // LED
  pinMode(LED_STATUS_PIN, OUTPUT);
  digitalWrite(LED_STATUS_PIN, LOW);

  // Attach encoder interrupt
  attachInterrupt(digitalPinToInterrupt(ENC_CLK_PIN), encoderISR, FALLING);
}

void IRAM_ATTR encoderISR() {
  if (digitalRead(ENC_DT_PIN) == HIGH) {
    encoderPos++;
  } else {
    encoderPos--;
  }
  // Clamp
  if (encoderPos < SPEED_MIN / SPEED_STEP) encoderPos = SPEED_MIN / SPEED_STEP;
  if (encoderPos > SPEED_MAX / SPEED_STEP) encoderPos = SPEED_MAX / SPEED_STEP;
}

void setupRFID() {
  SPI.begin(RFID_SCK_PIN, RFID_MISO_PIN, RFID_MOSI_PIN, RFID_SS_PIN);
  rfid.PCD_Init();
  
  // Check RFID reader
  byte v = rfid.PCD_ReadRegister(MFRC522::VersionReg);
  if (v == 0x00 || v == 0xFF) {
    Serial.println("ERROR: RFID reader not found!");
  } else {
    Serial.print("RFID reader version: 0x");
    Serial.println(v, HEX);
  }
}

void setupOLED() {
  oled.begin();
  oled.setFont(u8g2_font_6x10_tf);
  oled.clearBuffer();
  oled.drawStr(20, 30, "BIPED ROBOT");
  oled.drawStr(15, 45, "Khoi dong...");
  oled.sendBuffer();
}

void setupWiFi() {
  Serial.print("Connecting to WiFi: ");
  Serial.println(wifiSSID);

  WiFi.mode(WIFI_STA);
  WiFi.begin(wifiSSID, wifiPassword);

  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 20) {
    delay(500);
    Serial.print(".");
    attempts++;
  }

  if (WiFi.status() == WL_CONNECTED) {
    wifiConnected = true;
    Serial.println("\nWiFi connected!");
    Serial.print("IP: ");
    Serial.println(WiFi.localIP());
  } else {
    wifiConnected = false;
    Serial.println("\nWiFi connection failed!");
    Serial.println("Hold FORWARD button 3s to setup WiFi");
  }
}

// ============================================================
// WIFI MANAGER
// ============================================================

void displayWiFiSetup() {
  oled.clearBuffer();
  
  oled.setFont(u8g2_font_7x14B_tf);
  oled.drawStr(10, 15, "WIFI SETUP");
  
  oled.drawHLine(0, 20, 128);
  
  oled.setFont(u8g2_font_6x10_tf);
  oled.drawStr(0, 35, "1. Ket noi WiFi:");
  oled.drawStr(10, 47, "BipedRobot-Setup");
  oled.drawStr(0, 60, "2. Vao 192.168.4.1");
  
  oled.sendBuffer();
}

void startWiFiManager() {
  Serial.println("Starting WiFi Manager...");
  
  displayWiFiSetup();
  
  // Create WiFiManager instance
  WiFiManager wm;
  
  // Set timeout (5 minutes)
  wm.setConfigPortalTimeout(300);
  
  // Custom parameters for API URL
  WiFiManagerParameter apiParam("api", "API URL", apiBaseUrl, 128);
  wm.addParameter(&apiParam);
  
  // Set dark theme for portal
  wm.setClass("invert");
  
  // Start config portal
  bool connected = wm.startConfigPortal("BipedRobot-Setup", "biped123");
  
  if (connected) {
    Serial.println("WiFi configured successfully!");
    
    // Save new credentials
    strncpy(wifiSSID, wm.getWiFiSSID().c_str(), sizeof(wifiSSID) - 1);
    strncpy(wifiPassword, wm.getWiFiPass().c_str(), sizeof(wifiPassword) - 1);
    strncpy(apiBaseUrl, apiParam.getValue(), sizeof(apiBaseUrl) - 1);
    
    saveConfig();
    
    wifiConnected = true;
    Serial.print("Connected to: ");
    Serial.println(wifiSSID);
    Serial.print("API URL: ");
    Serial.println(apiBaseUrl);
    
    // Show success on OLED
    oled.clearBuffer();
    oled.setFont(u8g2_font_7x14B_tf);
    oled.drawStr(20, 30, "WiFi OK!");
    oled.setFont(u8g2_font_6x10_tf);
    oled.drawStr(10, 50, WiFi.localIP().toString().c_str());
    oled.sendBuffer();
    delay(2000);
    
  } else {
    Serial.println("WiFi config portal timeout or cancelled");
    wifiConnected = false;
    
    // Show failure on OLED
    oled.clearBuffer();
    oled.setFont(u8g2_font_7x14B_tf);
    oled.drawStr(10, 30, "WiFi FAIL!");
    oled.setFont(u8g2_font_6x10_tf);
    oled.drawStr(5, 50, "Giu TIEN 3s de thu lai");
    oled.sendBuffer();
    delay(2000);
  }
}

void checkForwardLongPress() {
  bool forwardPressed = (digitalRead(BTN_FORWARD_PIN) == LOW);
  
  if (forwardPressed) {
    if (forwardBtnPressStart == 0) {
      // Button just pressed - record start time
      forwardBtnPressStart = millis();
      forwardLongPressTriggered = false;
    } else if (!forwardLongPressTriggered) {
      // Button still held - check duration
      unsigned long holdTime = millis() - forwardBtnPressStart;
      
      // Show progress on OLED after 1 second
      if (holdTime > 1000 && currentState == STATE_IDLE) {
        int progress = map(holdTime, 1000, WIFI_SETUP_HOLD_TIME, 0, 100);
        if (progress > 100) progress = 100;
        
        oled.clearBuffer();
        oled.setFont(u8g2_font_6x10_tf);
        oled.drawStr(10, 25, "Giu de setup WiFi");
        
        // Progress bar
        oled.drawFrame(10, 35, 108, 15);
        oled.drawBox(12, 37, (int)(104.0 * progress / 100), 11);
        
        char pct[8];
        snprintf(pct, sizeof(pct), "%d%%", progress);
        oled.drawStr(55, 60, pct);
        oled.sendBuffer();
      }
      
      // Check if threshold reached
      if (holdTime >= WIFI_SETUP_HOLD_TIME) {
        forwardLongPressTriggered = true;
        Serial.println("Forward long press detected - starting WiFi Manager");
        startWiFiManager();
      }
    }
  } else {
    // Button released - reset
    forwardBtnPressStart = 0;
    forwardLongPressTriggered = false;
  }
}

void setupUART() {
  WalkingSerial.begin(UART_BAUD, SERIAL_8N1, UART_RX_PIN, UART_TX_PIN);
  Serial.println("UART to Walking Controller initialized");
}

void loadConfig() {
  prefs.begin("biped", false);
  
  // Load WiFi credentials
  String ssid = prefs.getString("ssid", DEFAULT_SSID);
  String pass = prefs.getString("pass", DEFAULT_PASSWORD);
  String api = prefs.getString("api", DEFAULT_API_URL);
  
  ssid.toCharArray(wifiSSID, sizeof(wifiSSID));
  pass.toCharArray(wifiPassword, sizeof(wifiPassword));
  api.toCharArray(apiBaseUrl, sizeof(apiBaseUrl));
  
  prefs.end();
  
  Serial.println("Config loaded");
}

void saveConfig() {
  prefs.begin("biped", false);
  prefs.putString("ssid", wifiSSID);
  prefs.putString("pass", wifiPassword);
  prefs.putString("api", apiBaseUrl);
  prefs.end();
  Serial.println("Config saved");
}

// ============================================================
// RFID HANDLING
// ============================================================

// Convert UID bytes to hex string (e.g., "12345678")
String uidToHexString(byte* uid, byte size) {
  String result = "";
  for (byte i = 0; i < size; i++) {
    if (uid[i] < 0x10) result += "0";
    result += String(uid[i], HEX);
  }
  result.toUpperCase();
  return result;
}

void handleRFID() {
  // Reset the loop if no new card present
  if (!rfid.PICC_IsNewCardPresent()) return;
  if (!rfid.PICC_ReadCardSerial()) return;

  String uidStr = uidToHexString(rfid.uid.uidByte, rfid.uid.size);
  Serial.print("RFID UID: ");
  Serial.println(uidStr);

  // First check if it's a checkpoint (local database)
  const CheckpointInfo* cp = findCheckpoint(rfid.uid.uidByte);
  if (cp) {
    handleCheckpointCard(cp);
    rfid.PICC_HaltA();
    rfid.PCD_StopCrypto1();
    return;
  }

  // Otherwise, try to find patient from API
  handlePatientCard(uidStr.c_str());
  
  rfid.PICC_HaltA();
  rfid.PCD_StopCrypto1();
}

void handlePatientCard(const char* cardUid) {
  // Nếu đang có session → quét thẻ cùng UID để kết thúc
  if (currentState == STATE_SESSION_ACTIVE) {
    if (strcmp(currentUser.cardUid, cardUid) == 0) {
      // Cùng thẻ → Kết thúc session
      Serial.println("Same card scanned - ending session");
      endSession("completed");
      
      // Hiển thị thông báo kết thúc
      oled.clearBuffer();
      oled.setFont(u8g2_font_7x14B_tf);
      oled.drawStr(10, 30, "KET THUC!");
      oled.setFont(u8g2_font_6x10_tf);
      oled.drawStr(5, 50, "Hen gap lai!");
      oled.sendBuffer();
      delay(2000);
      return;
    } else {
      // Thẻ khác → Bỏ qua
      Serial.println("Different card - ignored (session active)");
      return;
    }
  }

  // State là IDLE → Tra cứu và bắt đầu session mới
  Serial.println("Looking up patient from API...");
  
  // Hiển thị đang tìm
  oled.clearBuffer();
  oled.setFont(u8g2_font_6x10_tf);
  oled.drawStr(20, 30, "Dang kiem tra...");
  oled.sendBuffer();
  
  // Gọi API lấy thông tin bệnh nhân
  UserInfo fetchedUser;
  memset(&fetchedUser, 0, sizeof(fetchedUser));
  
  if (apiGetPatientByCard(cardUid, &fetchedUser)) {
    if (fetchedUser.isValid) {
      // ĐÃ ĐĂNG KÝ → Khởi động hệ thống
      Serial.print("Patient found: ");
      Serial.println(fetchedUser.userName);
      
      memcpy(&currentUser, &fetchedUser, sizeof(UserInfo));
      startSessionWithUser(&currentUser);
      
    } else {
      // CHƯA ĐĂNG KÝ → Không cho sử dụng
      Serial.println("Card NOT registered - access denied");
      oled.clearBuffer();
      oled.setFont(u8g2_font_7x14B_tf);
      oled.drawStr(5, 25, "KHONG HOP LE!");
      oled.setFont(u8g2_font_6x10_tf);
      oled.drawStr(5, 45, "The chua dang ky.");
      oled.drawStr(5, 57, "Lien he nhan vien.");
      oled.sendBuffer();
      delay(3000);
    }
  } else {
    // Lỗi kết nối API
    Serial.println("API call failed");
    oled.clearBuffer();
    oled.setFont(u8g2_font_6x10_tf);
    oled.drawStr(10, 30, "Loi ket noi!");
    oled.drawStr(10, 45, "Thu lai sau.");
    oled.sendBuffer();
    delay(2000);
  }
}

void handleCheckpointCard(const CheckpointInfo* cp) {
  Serial.print("Checkpoint: ");
  Serial.print(cp->checkpointId);
  Serial.print(" - ");
  Serial.println(cp->description);

  strncpy(currentCheckpoint, cp->checkpointId, sizeof(currentCheckpoint) - 1);
  
  // Report location to API
  reportLocation(cp->checkpointId);
}

const CheckpointInfo* findCheckpoint(byte* uid) {
  for (int i = 0; i < CHECKPOINT_COUNT; i++) {
    if (compareUID(uid, (byte*)CHECKPOINT_DATABASE[i].uid)) {
      return &CHECKPOINT_DATABASE[i];
    }
  }
  return nullptr;
}

bool compareUID(byte* uid1, byte* uid2) {
  for (int i = 0; i < 4; i++) {
    if (uid1[i] != uid2[i]) return false;
  }
  return true;
}

// ============================================================
// SESSION MANAGEMENT
// ============================================================

void startSessionWithUser(UserInfo* user) {
  Serial.print("Starting session for: ");
  Serial.println(user->userName);

  // Store session data
  strncpy(session.userId, user->cardUid, sizeof(session.userId) - 1);
  strncpy(session.userName, user->userName, sizeof(session.userName) - 1);
  strncpy(session.patientId, user->patientId, sizeof(session.patientId) - 1);
  session.stepCount = 0;
  session.startTime = millis();
  session.isActive = true;

  // Start session on API
  if (apiStartSession()) {
    currentState = STATE_SESSION_ACTIVE;
    
    // Enable balance on Walking Controller
    sendCommand("BALANCE:ON");
    
    digitalWrite(LED_STATUS_PIN, HIGH);
    Serial.println("Session started successfully");
  } else {
    Serial.println("Failed to start session on API");
    // Still allow local session
    currentState = STATE_SESSION_ACTIVE;
    snprintf(session.sessionId, sizeof(session.sessionId), "LOCAL-%lu", millis());
  }
}

void endSession(const char* status) {
  Serial.print("Ending session: ");
  Serial.println(status);

  // Stop movement
  sendCommand("STOP");
  sendCommand("BALANCE:OFF");
  isMoving = false;

  // End session on API
  apiEndSession(status);

  // Reset session data
  session.isActive = false;
  memset(&session, 0, sizeof(session));
  memset(&currentUser, 0, sizeof(currentUser));

  currentState = STATE_IDLE;
  
  digitalWrite(LED_STATUS_PIN, LOW);
  Serial.println("Session ended");
}

void updateStepCount(uint32_t steps) {
  if (session.isActive && steps != session.stepCount) {
    session.stepCount = steps;
    Serial.print("Steps updated: ");
    Serial.println(steps);
  }
}

void reportLocation(const char* checkpoint) {
  Serial.print("Reporting location: ");
  Serial.println(checkpoint);
  apiReportLocation();
}

// ============================================================
// BUTTON HANDLING
// ============================================================

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
      } else if (reading == HIGH) {
        if (buttons[i].pressed) {
          handleButtonRelease(i);
        }
        buttons[i].pressed = false;
      }
    }

    buttons[i].lastState = reading;
  }
}

void handleButtonPress(int btnIndex) {
  // Skip forward button if checking for WiFi setup long press
  if (btnIndex == 0 && currentState == STATE_IDLE) {
    return;  // Let checkForwardLongPress() handle it
  }
  
  if (currentState != STATE_SESSION_ACTIVE) {
    Serial.println("No active session - buttons disabled");
    return;
  }

  const char* cmd = nullptr;

  switch (btnIndex) {
    case 0: // Forward
      cmd = "CMD:FWD";
      strcpy(lastCommand, "FORWARD");
      break;
    case 1: // Backward
      cmd = "CMD:BACK";
      strcpy(lastCommand, "BACKWARD");
      break;
    case 2: // Left
      cmd = "CMD:LEFT";
      strcpy(lastCommand, "LEFT");
      break;
    case 3: // Right
      cmd = "CMD:RIGHT";
      strcpy(lastCommand, "RIGHT");
      break;
    case 4: // Stop / End session
      cmd = "STOP";
      strcpy(lastCommand, "STOP");
      // Long press to end session handled separately
      break;
  }

  if (cmd) {
    sendCommand(cmd);
    isMoving = (btnIndex != 4);
  }
}

void handleButtonRelease(int btnIndex) {
  // Stop movement when button released (for forward/backward/left/right)
  if (btnIndex >= 0 && btnIndex <= 3 && isMoving) {
    sendCommand("STOP");
    isMoving = false;
    strcpy(lastCommand, "");
  }
  
  // Check for long press on stop button to end session
  if (btnIndex == 4) {
    static unsigned long stopPressTime = 0;
    if (buttons[4].pressed) {
      if (stopPressTime == 0) {
        stopPressTime = millis();
      } else if (millis() - stopPressTime > 2000) {
        // Long press - end session
        endSession("completed");
        stopPressTime = 0;
      }
    } else {
      stopPressTime = 0;
    }
  }
}

// ============================================================
// ENCODER HANDLING
// ============================================================

void handleEncoder() {
  // Check encoder position changed
  int pos = encoderPos;
  if (pos != lastEncoderPos) {
    lastEncoderPos = pos;
    currentSpeed = pos * SPEED_STEP;
    
    // Clamp speed
    if (currentSpeed < SPEED_MIN) currentSpeed = SPEED_MIN;
    if (currentSpeed > SPEED_MAX) currentSpeed = SPEED_MAX;
    
    Serial.print("Speed: ");
    Serial.println(currentSpeed);
    
    // Send to Walking Controller
    sendSpeed(currentSpeed);
  }

  // Check encoder button
  static unsigned long lastEncBtn = 0;
  if (digitalRead(ENC_SW_PIN) == LOW && millis() - lastEncBtn > 200) {
    lastEncBtn = millis();
    Serial.println("Encoder button pressed");
    // Could be used for mode switch or confirm
  }
}

// ============================================================
// UART COMMUNICATION (Walking Controller)
// ============================================================

void sendCommand(const char* cmd) {
  WalkingSerial.print(cmd);
  WalkingSerial.print('\n');
  Serial.print("TX -> Walking: ");
  Serial.println(cmd);
}

void sendSpeed(uint8_t speed) {
  char buf[16];
  snprintf(buf, sizeof(buf), "SPEED:%d", speed);
  sendCommand(buf);
}

void handleUARTReceive() {
  static char buffer[64];
  static int bufIdx = 0;

  while (WalkingSerial.available()) {
    char c = WalkingSerial.read();
    
    if (c == '\n' || c == '\r') {
      if (bufIdx > 0) {
        buffer[bufIdx] = '\0';
        processWalkingMessage(buffer);
        bufIdx = 0;
      }
    } else if (bufIdx < sizeof(buffer) - 1) {
      buffer[bufIdx++] = c;
    }
  }
}

void processWalkingMessage(const char* msg) {
  Serial.print("RX <- Walking: ");
  Serial.println(msg);

  // Parse message format: "KEY:VALUE"
  char key[16], value[32];
  if (sscanf(msg, "%15[^:]:%31s", key, value) == 2) {
    if (strcmp(key, "STEP") == 0) {
      uint32_t steps = atoi(value);
      updateStepCount(steps);
    } else if (strcmp(key, "BALANCE") == 0) {
      strncpy(balanceStatus, value, sizeof(balanceStatus) - 1);
    } else if (strcmp(key, "ERROR") == 0) {
      Serial.print("Walking Controller Error: ");
      Serial.println(value);
      // Could trigger error display
    }
  }
}

// ============================================================
// API COMMUNICATION
// ============================================================

void handleAPITasks() {
  unsigned long now = millis();

  // Heartbeat
  if (now - lastHeartbeat >= HEARTBEAT_INTERVAL) {
    lastHeartbeat = now;
    apiHeartbeat();
  }

  // Step updates during active session
  if (session.isActive && now - lastStepUpdate >= STEP_UPDATE_INTERVAL) {
    lastStepUpdate = now;
    apiUpdateSteps();
  }
}

// Fetch patient info by RFID card number from API
bool apiGetPatientByCard(const char* cardUid, UserInfo* outUser) {
  if (!wifiConnected) {
    Serial.println("WiFi not connected - cannot fetch patient");
    return false;
  }

  HTTPClient http;
  String url = String(apiBaseUrl) + "/patients/by-card/" + String(cardUid);
  
  Serial.print("API GET: ");
  Serial.println(url);
  
  http.begin(url);
  http.setTimeout(5000);  // 5 second timeout
  
  int httpCode = http.GET();
  
  if (httpCode == HTTP_CODE_OK) {
    String response = http.getString();
    Serial.print("Response: ");
    Serial.println(response);
    
    // Parse JSON response
    StaticJsonDocument<512> doc;
    DeserializationError error = deserializeJson(doc, response);
    
    if (error) {
      Serial.print("JSON parse error: ");
      Serial.println(error.c_str());
      http.end();
      return false;
    }
    
    bool found = doc["found"] | false;
    if (found) {
      JsonObject patient = doc["patient"];
      
      // Copy data to output struct
      strncpy(outUser->cardUid, cardUid, sizeof(outUser->cardUid) - 1);
      
      const char* patientId = patient["patientId"] | "";
      strncpy(outUser->patientId, patientId, sizeof(outUser->patientId) - 1);
      
      const char* fullName = patient["fullName"] | "Unknown";
      strncpy(outUser->userName, fullName, sizeof(outUser->userName) - 1);
      
      const char* roomBed = patient["roomBed"] | "";
      strncpy(outUser->roomBed, roomBed, sizeof(outUser->roomBed) - 1);
      
      outUser->isValid = true;
      
      Serial.print("Patient loaded: ");
      Serial.print(outUser->userName);
      Serial.print(" (ID: ");
      Serial.print(outUser->patientId);
      Serial.print(", Room: ");
      Serial.print(outUser->roomBed);
      Serial.println(")");
      
      http.end();
      return true;
    } else {
      // Card not found
      outUser->isValid = false;
      http.end();
      return true;  // API call succeeded, but no patient found
    }
  } else if (httpCode == 404) {
    Serial.println("Patient not found (404)");
    outUser->isValid = false;
    http.end();
    return true;  // API call succeeded, but no patient
  } else {
    Serial.print("HTTP Error: ");
    Serial.println(httpCode);
    http.end();
    return false;  // API call failed
  }
}

bool apiStartSession() {
  if (!wifiConnected) return false;

  HTTPClient http;
  String url = String(apiBaseUrl) + "/robots/biped/session/start";
  
  http.begin(url);
  http.addHeader("Content-Type", "application/json");

  // Build JSON payload with patient info from API
  StaticJsonDocument<384> doc;
  doc["robotId"] = ROBOT_ID;
  doc["userId"] = session.userId;  // cardUid
  doc["userName"] = session.userName;  // patient fullName from API
  
  if (strlen(session.patientId) > 0) {
    doc["patientId"] = session.patientId;
  }
  
  // Include patient name for display
  doc["patientName"] = session.userName;
  
  // Include room/bed info if available
  if (strlen(currentUser.roomBed) > 0) {
    doc["roomBed"] = currentUser.roomBed;
  }

  String payload;
  serializeJson(doc, payload);

  Serial.print("API POST: ");
  Serial.println(url);
  Serial.print("Payload: ");
  Serial.println(payload);

  int httpCode = http.POST(payload);
  
  if (httpCode == HTTP_CODE_OK || httpCode == 201) {
    String response = http.getString();
    Serial.print("Response: ");
    Serial.println(response);

    // Parse response to get sessionId
    StaticJsonDocument<256> respDoc;
    if (deserializeJson(respDoc, response) == DeserializationError::Ok) {
      const char* sid = respDoc["sessionId"];
      if (sid) {
        strncpy(session.sessionId, sid, sizeof(session.sessionId) - 1);
        Serial.print("Session ID: ");
        Serial.println(session.sessionId);
      }
    }
    
    http.end();
    return true;
  } else {
    Serial.print("HTTP Error: ");
    Serial.println(httpCode);
    http.end();
    return false;
  }
}

bool apiUpdateSteps() {
  if (!wifiConnected || strlen(session.sessionId) == 0) return false;

  HTTPClient http;
  String url = String(apiBaseUrl) + "/robots/biped/session/" + session.sessionId + "/update";
  
  http.begin(url);
  http.addHeader("Content-Type", "application/json");

  StaticJsonDocument<128> doc;
  doc["steps"] = session.stepCount;

  String payload;
  serializeJson(doc, payload);

  int httpCode = http.PUT(payload);
  http.end();

  return (httpCode == HTTP_CODE_OK);
}

bool apiEndSession(const char* status) {
  if (!wifiConnected || strlen(session.sessionId) == 0) return false;

  HTTPClient http;
  String url = String(apiBaseUrl) + "/robots/biped/session/" + session.sessionId + "/end";
  
  http.begin(url);
  http.addHeader("Content-Type", "application/json");

  StaticJsonDocument<128> doc;
  doc["status"] = status;

  String payload;
  serializeJson(doc, payload);

  int httpCode = http.POST(payload);
  http.end();

  return (httpCode == HTTP_CODE_OK);
}

bool apiReportLocation() {
  if (!wifiConnected) return false;

  HTTPClient http;
  String url = String(apiBaseUrl) + "/robots/" + ROBOT_ID + "/telemetry";
  
  http.begin(url);
  http.addHeader("Content-Type", "application/json");

  StaticJsonDocument<256> doc;
  doc["robotId"] = ROBOT_ID;
  JsonObject loc = doc.createNestedObject("currentLocation");
  loc["checkpoint"] = currentCheckpoint;
  loc["timestamp"] = millis();
  
  if (session.isActive) {
    doc["status"] = "busy";
    doc["currentUser"] = session.userName;
  } else {
    doc["status"] = "idle";
  }

  String payload;
  serializeJson(doc, payload);

  int httpCode = http.PUT(payload);
  http.end();

  // Also trigger biped location endpoint for Carry Robot pickup
  if (session.isActive) {
    apiTriggerCarryRobotFetch();
  }

  return (httpCode == HTTP_CODE_OK);
}

bool apiTriggerCarryRobotFetch() {
  // This endpoint would trigger the Carry Robot to come pick up the Biped
  HTTPClient http;
  String url = String(apiBaseUrl) + "/missions/biped-pickup";
  
  http.begin(url);
  http.addHeader("Content-Type", "application/json");

  StaticJsonDocument<128> doc;
  doc["bipedId"] = ROBOT_ID;
  doc["checkpoint"] = currentCheckpoint;

  String payload;
  serializeJson(doc, payload);

  int httpCode = http.POST(payload);
  http.end();

  return (httpCode == HTTP_CODE_OK || httpCode == 201);
}

bool apiHeartbeat() {
  if (!wifiConnected) return false;

  HTTPClient http;
  String url = String(apiBaseUrl) + "/robots/" + ROBOT_ID + "/telemetry";
  
  http.begin(url);
  http.addHeader("Content-Type", "application/json");

  StaticJsonDocument<192> doc;
  doc["robotId"] = ROBOT_ID;
  doc["type"] = "biped";
  doc["status"] = session.isActive ? "busy" : "idle";
  doc["batteryLevel"] = 100;  // TODO: Read actual battery
  
  if (session.isActive) {
    doc["currentUser"] = session.userName;
    doc["stepCount"] = session.stepCount;
    doc["currentSessionId"] = session.sessionId;
  }

  String payload;
  serializeJson(doc, payload);

  int httpCode = http.PUT(payload);
  http.end();

  return (httpCode == HTTP_CODE_OK);
}

// ============================================================
// DISPLAY FUNCTIONS
// ============================================================

void updateDisplay() {
  // Chỉ hiển thị khi đang IDLE hoặc SESSION
  if (currentState == STATE_IDLE) {
    displayIdle();
  } else if (currentState == STATE_SESSION_ACTIVE) {
    displaySession();
  }
}

void displayIdle() {
  oled.clearBuffer();
  
  // Header
  oled.setFont(u8g2_font_7x14B_tf);
  oled.drawStr(15, 15, "BIPED ROBOT");
  
  oled.drawHLine(0, 20, 128);
  
  // Status
  oled.setFont(u8g2_font_6x10_tf);
  oled.drawStr(15, 38, "San sang su dung");
  
  // Instructions
  oled.drawStr(5, 55, "-> Quet the de bat dau");
  
  oled.sendBuffer();
}

void displaySession() {
  oled.clearBuffer();
  
  // ===== TÊN BỆNH NHÂN (dòng trên) =====
  oled.setFont(u8g2_font_7x14B_tf);
  
  // Cắt tên nếu quá dài
  char truncName[18];
  strncpy(truncName, session.userName, 17);
  truncName[17] = '\0';
  
  // Canh giữa tên
  int nameWidth = oled.getStrWidth(truncName);
  int nameX = (128 - nameWidth) / 2;
  oled.drawStr(nameX, 15, truncName);
  
  oled.drawHLine(0, 20, 128);
  
  // ===== SỐ BƯỚC (lớn, ở giữa) =====
  oled.setFont(u8g2_font_logisoso28_tn);  // Font số lớn
  char steps[12];
  snprintf(steps, sizeof(steps), "%lu", session.stepCount);
  int stepsWidth = oled.getStrWidth(steps);
  int stepsX = (128 - stepsWidth) / 2;
  oled.drawStr(stepsX, 52, steps);
  
  // Label "buoc"
  oled.setFont(u8g2_font_6x10_tf);
  oled.drawStr(52, 63, "buoc");
  
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

void displayConnecting() {
  oled.clearBuffer();
  
  oled.setFont(u8g2_font_7x14B_tf);
  oled.drawStr(15, 25, "BIPED ROBOT");
  
  oled.setFont(u8g2_font_6x10_tf);
  oled.drawStr(20, 45, "Dang ket noi...");
  
  oled.sendBuffer();
}

// ============================================================
// UTILITY FUNCTIONS
// ============================================================

// Get session duration in seconds
uint32_t getSessionDuration() {
  if (!session.isActive) return 0;
  return (millis() - session.startTime) / 1000;
}

// Format duration as MM:SS
void formatDuration(uint32_t seconds, char* buf, size_t len) {
  snprintf(buf, len, "%02lu:%02lu", seconds / 60, seconds % 60);
}
