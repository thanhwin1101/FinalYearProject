#include <WiFi.h>
#include <HTTPClient.h>
#include <SPI.h>
#include <MFRC522.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

// ===== PIN & BUTTON =====
#define RFID_SS   5
#define RFID_RST  22
#define BTN_PIN   27

// ===== WIFI & API =====
const char* WIFI_SSID = "S23";
const char* WIFI_PASS = "123456789";
const char* API_BASE  = "http://10.123.189.129:3000"; // đổi IP server nếu cần

// ===== RFID =====
MFRC522 rfid(RFID_SS, RFID_RST);

// ===== OLED (SSD1306 0.91") =====
#define OLED_WIDTH    128
#define OLED_HEIGHT    32          // nếu màn 128x64 thì đổi = 64
#define OLED_ADDR    0x3C
// I2C: tránh trùng RST=22, dùng SDA=21, SCL=26 (đổi nếu phần cứng khác)
#define I2C_SDA_PIN    21
#define I2C_SCL_PIN    26

Adafruit_SSD1306 display(OLED_WIDTH, OLED_HEIGHT, &Wire, -1);

// ===== UI TIMINGS (ms) ====
// Thời gian giữ các màn tạm trước khi chuyển tiếp
const unsigned long WIFI_SHOW_MS      = 2000; // màn Wi-Fi ở setup
const unsigned long UNREG_IDLE_DELAY  = 4000; // "UNREGISTERED" -> Idle
const unsigned long LOGIN_HOLD_MS     = 3000; // Giữ "Login OK" tĩnh rồi mới bật marquee
const unsigned long COUNT_HOLD_MS     = 2000; // Giữ "Press Count" lâu hơn
const unsigned long LOGOUT_HOLD_MS   = 3000; // Giữ thông báo Logout 3s trước khi về Idle

// ===== SESSION STATE =====
bool   sessionActive       = false;
String sessionUID          = "";
String sessionName         = "";
uint32_t sessionPressCount = 0;
unsigned long lastActivityMs = 0;    // giữ lại để dùng sau nếu cần (không auto logout)

// ===== BUTTON DEBOUNCE =====
unsigned long lastBtnChange = 0;
bool lastBtnState = HIGH;            // INPUT_PULLUP
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

// ===== WIFI =====
void wifiConnect() {
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  unsigned long start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < 12000) delay(300);
}

// ===== HTTP CALLS =====
bool httpGetUserDetail(const String &uid, String &outName) {
  if (WiFi.status() != WL_CONNECTED) return false;
  HTTPClient http;
  String url = String(API_BASE) + "/api/users/" + uid + "/detail";
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
  String url = String(API_BASE) + "/api/events/button";
  http.begin(url);
  http.addHeader("Content-Type", "application/json");
  http.setTimeout(6000);

  String payload = "{\"uid\":\"" + uid + "\",\"timestamp\":" + String(ts) + "}";
  int code = http.POST(payload);
  http.end();
  if (code == 403) return true;            // thẻ chưa đăng ký → coi như xử lý xong
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

// ===== OLED HELPERS =====
void oledInit() {
  Wire.begin(I2C_SDA_PIN, I2C_SCL_PIN);
  if (!display.begin(SSD1306_SWITCHCAPVCC, OLED_ADDR)) {
    Serial.println("SSD1306 allocation failed");
    return;
  }
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(WHITE);       // dùng WHITE cho đơn sắc
  display.setCursor(0, 0);
  display.println("Booting...");
  display.display();
}
void oledShowLogout(const String &name) {
  String n = name;
  if (n.length() > 14) n = n.substring(0, 14);
  oledTwoLines("Logout", "Bye, " + n);
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
  display.setCursor(0, 16);          // 128x32: dòng 2 ở y=16
  display.println(l2);
  display.display();
}

void oledShowWelcomeStatic(const String &nameShort) { oledTwoLines("Login OK", "User: " + nameShort); }
void oledShowCount(uint32_t c)                      { oledTwoLines("Press Count:", String(c)); }
void oledShowEnd()                                  { oledTwoLines("Logout", "Bye!"); }
void oledShowUnregistered()                         { oledTwoLines("CARD:", "UNREGISTERED"); }
void oledShowWifi(bool ok)                          { oledTwoLines("WiFi", ok ? "Connected" : "Not connected"); }
void oledShowIdle()                                 { oledTwoLines("Ready", "Tap your card"); }

// ===== MARQUEE (cuộn tên dài dòng 2) =====
bool marqueeOn = false;
String marqueeText = "";
int marqueeX = 0;
unsigned long lastMarqueeMs = 0;
const unsigned long MARQUEE_INTERVAL = 60; // ms giữa các frame
int textPixelWidth = 0;

void marqueeStart(const String &text) {
  marqueeOn = true;
  marqueeText = "User: " + text + "   ";  // khoảng trắng đuôi cho mượt
  marqueeX = OLED_WIDTH;                   // bắt đầu cuộn từ mép phải
  textPixelWidth = marqueeText.length() * 6; // size=1, ~6 px/ký tự
}
void marqueeStop() {
  marqueeOn = false;
}
void marqueeUpdate() {
  if (!marqueeOn) return;
  unsigned long now = millis();
  if (now - lastMarqueeMs < MARQUEE_INTERVAL) return;
  lastMarqueeMs = now;

  // Xóa dòng 2, vẽ lại text cuộn
  display.fillRect(0, 16, OLED_WIDTH, 16, BLACK);
  display.setCursor(marqueeX, 16);
  display.print(marqueeText);
  display.display();

  marqueeX -= 2; // tốc độ cuộn
  if (marqueeX < -textPixelWidth) marqueeX = OLED_WIDTH;
}

// ===== UI HOLD CONTROL =====
unsigned long uiHoldUntil = 0;     // mốc thời gian giữ màn hiện tại
bool pendingMarquee = false;       // sẽ bật marquee sau khi hết giữ

// ===== UNREGISTERED AUTO-IDLE =====
unsigned long unregShownAt = 0;    // 0 = không hiển thị unreg (đang idle/khác)

// ===== SESSION HELPERS =====
void startSession(const String& uid, const String& name) {
  sessionActive = true;
  sessionUID = uid;
  sessionName = name;
  sessionPressCount = 0;
  lastActivityMs = millis();
  Serial.printf("[LOGIN] UID=%s, name=%s\n", uid.c_str(), name.c_str());

  // Welcome tĩnh trước, rồi mới bật marquee nếu tên dài
  marqueeStop();
  if (sessionName.length() > 14) {
    String shortName = sessionName.substring(0, 14);
    oledShowWelcomeStatic(shortName);
    pendingMarquee = true;                         // sẽ bật marquee sau khi hết giữ
    uiHoldUntil    = millis() + LOGIN_HOLD_MS;     // giữ welcome lâu hơn
  } else {
    oledShowWelcomeStatic(sessionName);            // tên ngắn: không cần marquee
    pendingMarquee = false;
    uiHoldUntil    = 0;
  }
}

void endSession() {
  // Lưu lại tên trước khi xóa để hiện thông báo đẹp
  String lastName = sessionName;

  if (sessionActive) {
    Serial.printf("[LOGOUT] UID=%s, total presses=%u\n", sessionUID.c_str(), sessionPressCount);
    marqueeStop();
    oledShowLogout(lastName);             // ✅ Hiện thông báo Logout kèm tên
  }

  // Reset phiên
  sessionActive = false;
  sessionUID = ""; 
  sessionName = ""; 
  sessionPressCount = 0;

  // GIỮ màn Logout thêm vài giây, rồi mới tự về Idle trong loop()
  pendingMarquee = false;
  uiHoldUntil = millis() + LOGOUT_HOLD_MS;
  // ❌ KHÔNG gọi oledShowIdle() ở đây nữa
}


void setup() {
  Serial.begin(115200);
  pinMode(BTN_PIN, INPUT_PULLUP);

  SPI.begin();
  rfid.PCD_Init();

  oledInit();

  wifiConnect();
  Serial.println(WiFi.status() == WL_CONNECTED ? "WiFi OK" : "WiFi FAIL");
  oledShowWifi(WiFi.status() == WL_CONNECTED);

  // Giữ màn Wi-Fi lâu hơn một chút
  delay(WIFI_SHOW_MS);

  // Hiển thị trạng thái chờ quẹt thẻ
  oledShowIdle();
}

void loop() {
  unsigned long now = millis();

  // ---- RFID ----
  String uid = readUIDOnce();
  if (uid.length() > 0) {
    if (now - lastCardSeenMs > CARD_COOLDOWN_MS) {
      lastCardSeenMs = now;

      if (!sessionActive) {
        String name;
        if (httpGetUserDetail(uid, name)) {
          unregShownAt = 0;                // clear cờ unreg
          startSession(uid, name);
        } else {
          // thẻ chưa đăng ký hoặc lỗi mạng
          Serial.printf("[CARD] %s -> UNREGISTERED or NET FAIL\n", uid.c_str());
          marqueeStop();
          oledShowUnregistered();
          unregShownAt = millis();         // bắt đầu đếm để tự về Idle
        }
      } else {
        if (uid == sessionUID) {
          // Quẹt lại cùng thẻ để logout
          endSession();
        } else {
          // Chuyển phiên sang thẻ mới
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

  // ---- Nút bấm ----
  bool st = digitalRead(BTN_PIN);
  if (st != lastBtnState) { lastBtnChange = now; lastBtnState = st; }
  if (now - lastBtnChange > 30) {
    if (st == LOW && !pressedLatch) {
      pressedLatch = true;
      if (sessionActive) {
        enqueue(sessionUID, now);
        sessionPressCount++;
        lastActivityMs = now;

        // Hiển thị count & giữ lâu hơn
        if (!marqueeOn) {
          oledShowCount(sessionPressCount);          // vẽ cả 2 dòng
          uiHoldUntil = millis() + COUNT_HOLD_MS;    // giữ màn count
        } else {
          // Đang marquee: chỉ vẽ dòng 1 (dòng 2 vẫn cuộn)
          display.fillRect(0, 0, OLED_WIDTH, 16, BLACK);
          display.setCursor(0, 0);
          display.print("Step Count: ");
          display.print(sessionPressCount);
          display.display();
          uiHoldUntil = millis() + COUNT_HOLD_MS;    // giữ dòng 1 count lâu hơn
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
    if (now - lastTry > 5000) { wifiConnect(); lastTry = now; oledShowWifi(false); }
  }

  // ---- Marquee update (nếu đang cuộn) ----
  marqueeUpdate();

  // ---- Sau UNREGISTERED tự về Idle ----
  if (!sessionActive && unregShownAt && (now - unregShownAt > UNREG_IDLE_DELAY)) {
    unregShownAt = 0;
    oledShowIdle();
  }

  // ---- Hết thời gian giữ màn Welcome -> bật marquee (nếu cần) ----
  if (pendingMarquee && sessionActive && now > uiHoldUntil) {
    pendingMarquee = false;
    uiHoldUntil = 0;
    marqueeStart(sessionName);   // bắt đầu cuộn "User: <tên>"
  }

  // ---- Hết giữ COUNT -> khôi phục màn trước ----
  if (uiHoldUntil && now > uiHoldUntil && !pendingMarquee) {
    uiHoldUntil = 0;

    if (sessionActive) {
      if (marqueeOn) {
        // Đang marquee: khôi phục dòng 1 mặc định cho marquee
        display.fillRect(0, 0, OLED_WIDTH, 16, BLACK);
        display.setCursor(0, 0);
        display.print("Login OK");
        display.display();
        // dòng 2 tiếp tục marquee bình thường
      } else {
        // Không marquee (tên ngắn): quay về màn Welcome tĩnh
        oledShowWelcomeStatic(sessionName);
      }
    } else {
      // Nếu không còn phiên (vừa logout), quay về Idle
      oledShowIdle();
    }
  }

  delay(5);
}
