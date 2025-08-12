#include <WiFi.h>
#include <HTTPClient.h>
#include <SPI.h>
#include <MFRC522.h>

#define RFID_SS   5
#define RFID_RST  22
#define BTN_PIN   27

const char* WIFI_SSID = "Nguyen";
const char* WIFI_PASS = "Nguyen2004";
const char* API_BASE  = "http://192.168.1.112:3000"; // đổi IP server

MFRC522 rfid(RFID_SS, RFID_RST);

String currentUID = "";           // UID hiện tại sau khi quẹt thẻ
unsigned long lastBtnChange = 0;  // debounce
bool lastBtnState = HIGH;         // dùng INPUT_PULLUP
bool pressedLatch = false;        // chốt 1 lần nhấn

struct Event {
  String uid;
  unsigned long ts;
};
Event queueBuf[20];
int qHead = 0, qTail = 0;

void enqueue(const String& uid, unsigned long ts) {
  int next = (qTail + 1) % 20;
  if (next != qHead) {
    queueBuf[qTail] = {uid, ts};
    qTail = next;
  }
}

bool dequeue(Event &out) {
  if (qHead == qTail) return false;
  out = queueBuf[qHead];
  qHead = (qHead + 1) % 20;
  return true;
}

void wifiConnect() {
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  unsigned long start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < 12000) {
    delay(400);
  }
}

bool httpPostButton(const String& uid, unsigned long ts) {
  if (WiFi.status() != WL_CONNECTED) return false;

  HTTPClient http;
  String url = String(API_BASE) + "/api/events/button";
  http.begin(url);
  http.addHeader("Content-Type", "application/json");

  // ISO timestamp (đơn giản dùng millis, server sẽ lấy server time nếu muốn)
  String payload = "{\"uid\":\"" + uid + "\",\"timestamp\":" + String(ts) + "}";
  int code = http.POST(payload);
  http.end();
  return code >= 200 && code < 300;
}

String readUID() {
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

void setup() {
  Serial.begin(115200);
  pinMode(BTN_PIN, INPUT_PULLUP);

  SPI.begin();
  rfid.PCD_Init();

  wifiConnect();
  Serial.println(WiFi.status() == WL_CONNECTED ? "WiFi OK" : "WiFi FAIL");
}

void loop() {
  // 1) Đọc thẻ
  String uid = readUID();
  if (uid.length() > 0) {
    currentUID = uid;
    Serial.print("Card UID: "); Serial.println(currentUID);
    delay(300); // tránh đọc lặp
  }

  // 2) Đọc nút (debounce)
  bool st = digitalRead(BTN_PIN);
  unsigned long now = millis();
  if (st != lastBtnState) {
    lastBtnChange = now;
    lastBtnState = st;
  }
  if (now - lastBtnChange > 30) { // debounce 30ms
    // Nhấn (mức LOW) 1 lần
    if (st == LOW && !pressedLatch) {
      pressedLatch = true;
      if (currentUID.length() > 0) {
        enqueue(currentUID, now);
        Serial.println("Button press queued for UID: " + currentUID);
      } else {
        Serial.println("No UID set. Please tap card first.");
      }
    }
    if (st == HIGH && pressedLatch) {
      pressedLatch = false;
    }
  }

  // 3) Gửi hàng đợi nếu có mạng
  if (WiFi.status() != WL_CONNECTED) {
    static unsigned long lastTry = 0;
    if (millis() - lastTry > 5000) {
      wifiConnect();
      lastTry = millis();
    }
  } else {
    Event e;
    while (dequeue(e)) {
      if (!httpPostButton(e.uid, e.ts)) {
        // gửi thất bại → nhét lại và thoát vòng (đợi lần sau)
        enqueue(e.uid, e.ts);
        break;
      } else {
        Serial.println("Sent press of UID " + e.uid);
      }
    }
  }

  delay(5);
}
