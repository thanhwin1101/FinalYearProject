#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <SoftwareSerial.h>

// LCD I2C
LiquidCrystal_I2C lcd(0x27, 16, 2);   // nếu không thấy: thử 0x3F

// UART từ ESP32 -> Uno (RX=10, TX=11; TX không nối cũng được)
SoftwareSerial espSerial(10, 11);     // RX=10, TX=11

String currentName = "";
int currentCount = 0;

void showWaiting() {
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Waiting card...");
  lcd.setCursor(0, 1);
  lcd.print("                ");
}

void showUnregistered() {
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Card not reg.");
  lcd.setCursor(0, 1);
  lcd.print("Access denied");
}

void showNameAndCount() {
  lcd.clear();
  lcd.setCursor(0, 0);
  String line = currentName;
  if (line.length() > 16) line = line.substring(0, 16);
  lcd.print(line);
  lcd.setCursor(0, 1);
  lcd.print("Presses: ");
  lcd.print(currentCount);
}

void showLogout() {
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("User logged out");
  lcd.setCursor(0, 1);
  lcd.print("Remove card...");
}

void setup() {
  lcd.init();
  lcd.backlight();
  showWaiting();

  espSerial.begin(38400);
  espSerial.setTimeout(200);
}

void loop() {
  if (espSerial.available()) {
    String line = espSerial.readStringUntil('\n');
    line.trim();

    if (line.startsWith("NAME:")) {
      currentName = line.substring(5);
      currentCount = 0;
      showNameAndCount();
    } else if (line.startsWith("COUNT:")) {
      currentCount = line.substring(6).toInt();
      showNameAndCount();
    } else if (line == "END") {
      currentName = "";
      currentCount = 0;
      showLogout();               // 👈 Hiển thị trạng thái logout
      delay(2000);                // giữ 2s cho người dùng thấy
      showWaiting();              // quay lại màn hình chờ
    } else if (line == "UNREGISTERED") {
      currentName = "";
      currentCount = 0;
      showUnregistered();
    }
  }
}
