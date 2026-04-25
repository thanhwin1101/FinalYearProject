#include "HUSKYLENS.h"
#include "SoftwareSerial.h"

HUSKYLENS huskylens;

// SoftwareSerial init: RX = D10, TX = D11
SoftwareSerial mySerial(10, 11);

void setup() {
  // Serial monitor for debug output
  Serial.begin(115200);

  // HuskyLens UART baudrate
  mySerial.begin(9600);

  // Retry until HuskyLens responds
  while (!huskylens.begin(mySerial)) {
    Serial.println(F("Error: HuskyLens not found"));
    Serial.println(F("1) Check power and TX/RX crossover wiring"));
    Serial.println(F("2) Check HuskyLens protocol is Serial 9600"));
    delay(2000);
  }

  Serial.println(F("HuskyLens UART connected"));
}

void loop() {
  if (!huskylens.request()) {
    // No response from HuskyLens in this cycle.
  }
  else if (!huskylens.isLearned()) {
    Serial.println(F("HuskyLens has not learned any object yet"));
  }
  else if (!huskylens.available()) {
    Serial.println(F("No object currently visible"));
  }
  else {
    Serial.println(F("========= OBJECT DETECTED ========="));

    while (huskylens.available()) {
      HUSKYLENSResult result = huskylens.read();

      if (result.command == COMMAND_RETURN_BLOCK) {
        Serial.print(F("Block -> ID: "));
        Serial.print(result.ID);
        Serial.print(F(", X center: "));
        Serial.print(result.xCenter);
        Serial.print(F(", Y center: "));
        Serial.print(result.yCenter);
        Serial.print(F(", Width: "));
        Serial.print(result.width);
        Serial.print(F(", Height: "));
        Serial.println(result.height);
      }
      else if (result.command == COMMAND_RETURN_ARROW) {
        Serial.print(F("Arrow -> ID: "));
        Serial.print(result.ID);
        Serial.print(F(", X origin: "));
        Serial.print(result.xOrigin);
        Serial.print(F(", Y origin: "));
        Serial.print(result.yOrigin);
        Serial.print(F(", X target: "));
        Serial.print(result.xTarget);
        Serial.print(F(", Y target: "));
        Serial.println(result.yTarget);
      }
    }
  }

  delay(100);
}
