#include <WiFi.h>
#include <HTTPClient.h>
#include <SPI.h>
#include <MFRC522.h>

// ===== PIN & BUTTON =====
#define RFID_SS   5
#define RFID_RST  22
#define BTN_PIN   27

// ===== WIFI & API =====
const char* WIFI_SSID = "CUONG HA";
const char* WIFI_PASS = "0973567115";
const char* API_BASE  = "http://192.168.1.12:3000"; // đổi IP server nếu cần

// ===== RFID =====
MFRC522 rfid(RFID_SS, RFID_RST);

// ===== UART to UNO (chỉ ESP32->UNO) =====
// EXPLICT: RX=16, TX=17 để không lệch chân
HardwareSerial &uno = Serial2;
const uint32_t UART_BAUD = 38400;

// ===== SESSION STATE =====
bool sessionActive = false;
String sessionUID = "";
String sessionName = "";
uint32_t sessionPressCount = 0;
unsigned long lastActivityMs = 0;
const unsigned long SESSION_TIMEOUT = 20000; // 20s

// ===== BUTTON DEBOUNCE =====
unsigned long lastBtnChange = 0;
bool lastBtnState = HIGH; // INPUT_PULLUP
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

// ===== UART HELPERS =====
inline void sendLine(const char* s){ uno.print(s); uno.print('\n'); }
void sendNameToUno(const String &name) { uno.print("NAME:");  uno.print(name);  uno.print('\n'); }
void sendCountToUno(uint32_t c)        { uno.print("COUNT:"); uno.print(c);     uno.print('\n'); }
void sendEndToUno()                    { sendLine("END"); }
void sendUnregisteredToUno()           { sendLine("UNREGISTERED"); }

// ===== SESSION HELPERS =====
void startSession(const String& uid, const String& name) {
  sessionActive = true;
  sessionUID = uid;
  sessionName = name;
  sessionPressCount = 0;
  lastActivityMs = millis();
  Serial.printf("[LOGIN] UID=%s, name=%s\n", uid.c_str(), name.c_str());
  sendNameToUno(sessionName);  // báo LCD
}

void endSession() {
  if (sessionActive) {
    Serial.printf("[LOGOUT] UID=%s, total presses=%u\n", sessionUID.c_str(), sessionPressCount);
    sendEndToUno(); // báo LCD
  }
  sessionActive = false;
  sessionUID = ""; sessionName = ""; sessionPressCount = 0;
}

void setup() {
  Serial.begin(115200);

  // UART sang UNO: TX=17, RX=16 — ta chỉ dùng TX
  uno.begin(UART_BAUD, SERIAL_8N1, 16, 17);

  pinMode(BTN_PIN, INPUT_PULLUP);

  SPI.begin();
  rfid.PCD_Init();

  wifiConnect();
  Serial.println(WiFi.status() == WL_CONNECTED ? "WiFi OK" : "WiFi FAIL");
}

void loop() {
  unsigned long now = millis();

  // Auto logout khi timeout
  if (sessionActive && (now - lastActivityMs > SESSION_TIMEOUT)) {
    Serial.println("[TIMEOUT] Auto logout");
    endSession();
  }

  // Đọc thẻ (cooldown)
  String uid = readUIDOnce();
  if (uid.length() > 0) {
    if (now - lastCardSeenMs > CARD_COOLDOWN_MS) {
      lastCardSeenMs = now;

      if (!sessionActive) {
        String name;
        if (httpGetUserDetail(uid, name)) {
          startSession(uid, name);
        } else {
          // không có mạng hoặc thẻ chưa đăng ký: báo ngay trên LCD
          Serial.printf("[CARD] %s -> UNREGISTERED or NET FAIL\n", uid.c_str());
          sendUnregisteredToUno();
        }
      } else {
        if (uid == sessionUID) {
          // Toggle logout với cùng thẻ
          endSession();
        } else {
          // Chuyển phiên
          endSession();
          String name;
          if (httpGetUserDetail(uid, name)) {
            startSession(uid, name);
          } else {
            sendUnregisteredToUno();
          }
        }
      }
    }
    delay(100);
  }

  // Đọc nút – chỉ khi có phiên
  bool st = digitalRead(BTN_PIN);
  if (st != lastBtnState) { lastBtnChange = now; lastBtnState = st; }
  if (now - lastBtnChange > 30) {
    if (st == LOW && !pressedLatch) {
      pressedLatch = true;
      if (sessionActive) {
        enqueue(sessionUID, now);
        sessionPressCount++;
        lastActivityMs = now;
        sendCountToUno(sessionPressCount); // cập nhật LCD ngay
      } else {
        Serial.println("No active session. Tap card first.");
      }
    }
    if (st == HIGH && pressedLatch) pressedLatch = false;
  }

  // Gửi hàng đợi nếu có mạng
  if (WiFi.status() == WL_CONNECTED) {
    Event e;
    while (dequeue(e)) {
      if (!httpPostButton(e.uid, e.ts)) { enqueue(e.uid, e.ts); break; }
    }
  } else {
    static unsigned long lastTry = 0;
    if (now - lastTry > 5000) { wifiConnect(); lastTry = now; }
  }

  delay(5);
}
