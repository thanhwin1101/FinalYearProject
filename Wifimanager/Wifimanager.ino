#include <WiFi.h>
#include <HTTPClient.h>
#include <SPI.h>
#include <MFRC522.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

// ===== NEW: WiFiManager & Preferences =====
#include <WiFiManager.h>   // https://github.com/tzapu/WiFiManager
#include <Preferences.h>

// ===== PIN & BUTTON =====
#define RFID_SS   5
#define RFID_RST  22
#define BTN_PIN   27

// ===== DEFAULTS (will be overridden by WiFiManager) =====
const char* WIFI_AP_NAME = "RFID-Setup";
const char* WIFI_AP_PASS = "12345678";

// API_BASE sẽ được nạp từ NVS hoặc nhập từ WiFiManager
String API_BASE = "http://10.123.189.129:3000"; // giá trị mặc định lần đầu

// ===== RFID =====
MFRC522 rfid(RFID_SS, RFID_RST);

// ===== OLED (SSD1306 0.91") =====
#define OLED_WIDTH    128
#define OLED_HEIGHT    32
#define OLED_ADDR    0x3C
#define I2C_SDA_PIN    21
#define I2C_SCL_PIN    26

Adafruit_SSD1306 display(OLED_WIDTH, OLED_HEIGHT, &Wire, -1);

// ===== UI TIMINGS (ms) ====
const unsigned long WIFI_SHOW_MS      = 2000;
const unsigned long UNREG_IDLE_DELAY  = 4000;
const unsigned long LOGIN_HOLD_MS     = 3000;
const unsigned long COUNT_HOLD_MS     = 2000;
const unsigned long LOGOUT_HOLD_MS    = 3000;

// ===== SESSION STATE =====
bool   sessionActive       = false;
String sessionUID          = "";
String sessionName         = "";
uint32_t sessionPressCount = 0;
unsigned long lastActivityMs = 0;

// ===== BUTTON DEBOUNCE =====
unsigned long lastBtnChange = 0;
bool lastBtnState = HIGH;
bool pressedLatch = false;

// ===== RFID COOLDOWN =====
unsigned long lastCardSeenMs = 0;
const unsigned long CARD_COOLDOWN_MS = 800;

// ===== EVENT QUEUE =====
struct Event { String uid; unsigned long ts; };
Event queueBuf[20];
int qHead = 0, qTail = 0;

void enqueue(const String& uid, unsigned long ts) {
  int next = (qTail + 1) % 20;
  if (next != qHead) { queueBuf[qTail] = {uid, ts}; qTail = next; }
}
bool dequeue(Event &out) {
  if (qHead == qTail) return false;
  out = queueBuf[qHead]; qHead = (qHead + 1) % 20; return true;
}

// ===== NEW: NVS (Preferences) & WiFiManager param =====
Preferences prefs;
// buffer cho ô nhập API_BASE trên portal
char apiBaseBuf[128] = {0};

// ===== OLED HELPERS =====
void oledInit() {
  Wire.begin(I2C_SDA_PIN, I2C_SCL_PIN);
  if (!display.begin(SSD1306_SWITCHCAPVCC, OLED_ADDR)) {
    Serial.println("SSD1306 allocation failed");
    return;
  }
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(WHITE);
  display.setCursor(0, 0);
  display.println("Booting...");
  display.display();
}
void oledClear() {
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(WHITE);
}
void oledTwoLines(const String &l1, const String &l2) {
  oledClear();
  display.setCursor(0, 0);
  display.println(l1);
  display.setCursor(0, 16);
  display.println(l2);
  display.display();
}
void oledShowWifi(bool ok)       { oledTwoLines("WiFi", ok ? "Connected" : "Not connected"); }
void oledShowIdle()              { oledTwoLines("Ready", "Tap your card"); }
void oledShowWelcomeStatic(const String &nameShort) { oledTwoLines("Login OK", "User: " + nameShort); }
void oledShowCount(uint32_t c)   { oledTwoLines("Press Count:", String(c)); }
void oledShowEnd()               { oledTwoLines("Logout", "Bye!"); }
void oledShowUnregistered()      { oledTwoLines("CARD:", "UNREGISTERED"); }
void oledShowLogout(const String &name) {
  String n = name;
  if (n.length() > 14) n = n.substring(0, 14);
  oledTwoLines("Logout", "Bye, " + n);
}

// ===== MARQUEE =====
bool marqueeOn = false;
String marqueeText = "";
int marqueeX = 0;
unsigned long lastMarqueeMs = 0;
const unsigned long MARQUEE_INTERVAL = 60;
int textPixelWidth = 0;

void marqueeStart(const String &text) {
  marqueeOn = true;
  marqueeText = "User: " + text + "   ";
  marqueeX = OLED_WIDTH;
  textPixelWidth = marqueeText.length() * 6;
}
void marqueeStop() { marqueeOn = false; }
void marqueeUpdate() {
  if (!marqueeOn) return;
  unsigned long now = millis();
  if (now - lastMarqueeMs < MARQUEE_INTERVAL) return;
  lastMarqueeMs = now;

  display.fillRect(0, 16, OLED_WIDTH, 16, BLACK);
  display.setCursor(marqueeX, 16);
  display.print(marqueeText);
  display.display();

  marqueeX -= 2;
  if (marqueeX < -textPixelWidth) marqueeX = OLED_WIDTH;
}

// ===== UI HOLD CONTROL =====
unsigned long uiHoldUntil = 0;
bool pendingMarquee = false;

// ===== UNREGISTERED AUTO-IDLE =====
unsigned long unregShownAt = 0;

// ===== SESSION HELPERS =====
void startSession(const String& uid, const String& name) {
  sessionActive = true;
  sessionUID = uid;
  sessionName = name;
  sessionPressCount = 0;
  lastActivityMs = millis();
  Serial.printf("[LOGIN] UID=%s, name=%s\n", uid.c_str(), name.c_str());

  marqueeStop();
  if (sessionName.length() > 14) {
    String shortName = sessionName.substring(0, 14);
    oledShowWelcomeStatic(shortName);
    pendingMarquee = true;
    uiHoldUntil    = millis() + LOGIN_HOLD_MS;
  } else {
    oledShowWelcomeStatic(sessionName);
    pendingMarquee = false;
    uiHoldUntil    = 0;
  }
}

void endSession() {
  String lastName = sessionName;
  if (sessionActive) {
    Serial.printf("[LOGOUT] UID=%s, total presses=%u\n", sessionUID.c_str(), sessionPressCount);
    marqueeStop();
    oledShowLogout(lastName);
  }
  sessionActive = false;
  sessionUID = "";
  sessionName = "";
  sessionPressCount = 0;

  pendingMarquee = false;
  uiHoldUntil = millis() + LOGOUT_HOLD_MS;
}

// ===== HTTP CALLS (dùng API_BASE động) =====
bool httpGetUserDetail(const String &uid, String &outName) {
  if (WiFi.status() != WL_CONNECTED) return false;
  HTTPClient http;
  String url = API_BASE + "/api/users/" + uid + "/detail";
  http.begin(url);
  http.setTimeout(6000);
  int code = http.GET();
  if (code == 200) {
    String payload = http.getString();
    int i = payload.indexOf("\"name\"");
    if (i >= 0) {
      int colon = payload.indexOf(':', i);
      int q1 = payload.indexOf('\"', colon + 1);
      int q2 = payload.indexOf('\"', q1 + 1);
      if (q1 > 0 && q2 > q1) outName = payload.substring(q1 + 1, q2);
    }
    http.end();
    return outName.length() > 0;
  }
  http.end();
  return false;
}

bool httpPostButton(const String& uid, unsigned long ts) {
  if (WiFi.status() != WL_CONNECTED) return false;
  HTTPClient http;
  String url = API_BASE + "/api/events/button";
  http.begin(url);
  http.addHeader("Content-Type", "application/json");
  http.setTimeout(6000);

  String payload = "{\"uid\":\"" + uid + "\",\"timestamp\":" + String(ts) + "}";
  int code = http.POST(payload);
  http.end();
  if (code == 403) return true; // thẻ chưa đăng ký → coi như xử lý xong
  return (code >= 200 && code < 300);
}

// ===== RFID =====
String readUIDOnce() {
  if (!rfid.PICC_IsNewCardPresent() || !rfid.PICC_ReadCardSerial()) return "";
  String s = "";
  for (byte i = 0; i < rfid.uid.size; i++) {
    if (rfid.uid.uidByte[i] < 0x10) s += "0";
    s += String(rfid.uid.uidByte[i], HEX);
  }
  s.toUpperCase();
  rfid.PICC_HaltA();
  rfid.PCD_StopCrypto1();
  return s;
}

// ===== NEW: WiFiManager setup + param API_BASE =====
void wifiManagerSetup(bool forcePortal = false) {
  // 1) Nạp API_BASE đã lưu (nếu có)
  prefs.begin("cfg", false);
  String saved = prefs.getString("api", "");
  if (saved.length() > 0) {
    API_BASE = saved;
  }
  // copy sang buffer để hiển thị trên ô nhập
  memset(apiBaseBuf, 0, sizeof(apiBaseBuf));
  strncpy(apiBaseBuf, API_BASE.c_str(), sizeof(apiBaseBuf) - 1);

  // 2) Tạo WiFiManager + param
  WiFiManager wm;
  WiFiManagerParameter customApi("api", "API base (http://...)", apiBaseBuf, sizeof(apiBaseBuf));

  wm.addParameter(&customApi);
  wm.setTimeout(180);                   // portal timeout 3 phút
  wm.setConnectRetries(3);

  // Hiển thị trạng thái trên OLED
  oledTwoLines("WiFi", "Config / Connect");

  bool ok;
  if (forcePortal) {
    // Bắt buộc mở portal
    ok = wm.startConfigPortal(WIFI_AP_NAME, WIFI_AP_PASS);
  } else {
    // Tự kết nối; nếu chưa có Wi-Fi thì tự mở portal
    ok = wm.autoConnect(WIFI_AP_NAME, WIFI_AP_PASS);
  }

  if (!ok) {
    Serial.println("[WiFi] Connect/Portal failed");
    oledShowWifi(false);
  } else {
    Serial.print("[WiFi] Connected: ");
    Serial.println(WiFi.localIP());
    oledShowWifi(true);
  }

  // 3) Lấy giá trị API_BASE người dùng nhập và LƯU NVS nếu thay đổi
  const char* newApi = customApi.getValue();
  if (newApi && strlen(newApi) > 0) {
    String trimmed = String(newApi);
    trimmed.trim();
    if (trimmed.length() > 0 && trimmed != API_BASE) {
      API_BASE = trimmed;
      prefs.putString("api", API_BASE);
      Serial.printf("[CFG] Saved API_BASE: %s\n", API_BASE.c_str());
    }
  }
  prefs.end();

  // Giữ màn Wi-Fi 2s như cũ
  delay(WIFI_SHOW_MS);
}

void setup() {
  Serial.begin(115200);
  pinMode(BTN_PIN, INPUT_PULLUP);

  SPI.begin();
  rfid.PCD_Init();

  oledInit();

  // NEW: Dùng WiFiManager thay cho WiFi.begin(...)
  //   - Lần đầu: mở portal cho bạn chọn Wi-Fi + nhập API_BASE
  //   - Lần sau: autoConnect
  wifiManagerSetup(false);
Serial.print("[CFG] Current API_BASE = ");
Serial.println(API_BASE);
Serial.print("[WiFi] IP = ");
Serial.println(WiFi.localIP());
  // Hiển thị trạng thái chờ quẹt thẻ
  oledShowIdle();
}

void loop() {
  unsigned long now = millis();

  // ---- Giữ nút > 5s để mở lại portal cấu hình (đổi Wi-Fi / API_BASE) ----
  static unsigned long holdStart = 0;
  bool stNow = (digitalRead(BTN_PIN) == LOW);
  if (stNow) {
    if (holdStart == 0) holdStart = now;
    else if (now - holdStart > 5000) {
      // mở portal bắt buộc
      oledTwoLines("Config Mode", "Open AP: RFID-Setup");
      wifiManagerSetup(true);          // ép mở portal
      oledShowIdle();
      holdStart = 0;
      // chống lọt xuống phần xử lý nút bấm bên dưới
      delay(300);
      return;
    }
  } else {
    holdStart = 0;
  }

  // ---- RFID ----
  String uid = readUIDOnce();
  if (uid.length() > 0) {
    if (now - lastCardSeenMs > CARD_COOLDOWN_MS) {
      lastCardSeenMs = now;

      if (!sessionActive) {
        String name;
        if (httpGetUserDetail(uid, name)) {
          unregShownAt = 0;
          startSession(uid, name);
        } else {
          Serial.printf("[CARD] %s -> UNREGISTERED or NET FAIL\n", uid.c_str());
          marqueeStop();
          oledShowUnregistered();
          unregShownAt = millis();
        }
      } else {
        if (uid == sessionUID) {
          endSession();
        } else {
          endSession();
          String name;
          if (httpGetUserDetail(uid, name)) {
            startSession(uid, name);
          } else {
            marqueeStop();
            oledShowUnregistered();
            unregShownAt = millis();
          }
        }
      }
    }
    delay(100);
  }

  // ---- Nút bấm (count & queue) ----
  bool st = digitalRead(BTN_PIN);
  if (st != lastBtnState) { lastBtnChange = now; lastBtnState = st; }
  if (now - lastBtnChange > 30) {
    if (st == LOW && !pressedLatch) {
      pressedLatch = true;
      if (sessionActive) {
        enqueue(sessionUID, now);
        sessionPressCount++;
        lastActivityMs = now;

        if (!marqueeOn) {
          oledShowCount(sessionPressCount);
          uiHoldUntil = millis() + COUNT_HOLD_MS;
        } else {
          display.fillRect(0, 0, OLED_WIDTH, 16, BLACK);
          display.setCursor(0, 0);
          display.print("Step Count: ");
          display.print(sessionPressCount);
          display.display();
          uiHoldUntil = millis() + COUNT_HOLD_MS;
        }
      } else {
        Serial.println("No active session. Tap card first.");
        oledShowIdle();
      }
    }
    if (st == HIGH && pressedLatch) pressedLatch = false;
  }

  // ---- Gửi hàng đợi khi có mạng ----
  if (WiFi.status() == WL_CONNECTED) {
    Event e;
    while (dequeue(e)) {
      if (!httpPostButton(e.uid, e.ts)) { enqueue(e.uid, e.ts); break; }
    }
  } else {
    static unsigned long lastTry = 0;
    if (now - lastTry > 5000) {
      // Nếu rớt mạng, thử autoConnect nhanh (không mở portal)
      WiFiManager wm;
      if (wm.autoConnect(WIFI_AP_NAME, WIFI_AP_PASS)) {
        oledShowWifi(true);
      } else {
        oledShowWifi(false);
      }
      lastTry = now;
    }
  }

  // ---- Marquee ----
  marqueeUpdate();

  // ---- Sau UNREGISTERED tự về Idle ----
  if (!sessionActive && unregShownAt && (now - unregShownAt > UNREG_IDLE_DELAY)) {
    unregShownAt = 0;
    oledShowIdle();
  }

  // ---- Hết giữ Welcome -> bật marquee (nếu cần) ----
  if (pendingMarquee && sessionActive && now > uiHoldUntil) {
    pendingMarquee = false;
    uiHoldUntil = 0;
    marqueeStart(sessionName);
  }

  // ---- Hết giữ COUNT -> khôi phục màn trước ----
  if (uiHoldUntil && now > uiHoldUntil && !pendingMarquee) {
    uiHoldUntil = 0;
    if (sessionActive) {
      if (marqueeOn) {
        display.fillRect(0, 0, OLED_WIDTH, 16, BLACK);
        display.setCursor(0, 0);
        display.print("Login OK");
        display.display();
      } else {
        oledShowWelcomeStatic(sessionName);
      }
    } else {
      oledShowIdle();
    }
  }

  delay(5);
}
