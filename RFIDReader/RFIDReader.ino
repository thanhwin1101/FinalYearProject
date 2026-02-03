/*
 * RFID Reader for Hospital Patient Registration
 * Hardware: Arduino Uno + MFRC522 RFID Module
 * 
 * Wiring (MFRC522 to Arduino Uno):
 * ---------------------------------
 * SDA  -> Pin 10
 * SCK  -> Pin 13
 * MOSI -> Pin 11
 * MISO -> Pin 12
 * IRQ  -> Not connected
 * GND  -> GND
 * RST  -> Pin 9
 * 3.3V -> 3.3V (IMPORTANT: Use 3.3V, NOT 5V!)
 * 
 * Output Format: RFID:XXXXXXXX\n (hex UID)
 * Baud Rate: 9600
 */

#include <SPI.h>
#include <MFRC522.h>

// Pin definitions
#define RST_PIN   9    // Reset pin
#define SS_PIN    10   // Slave Select (SDA) pin

// LED indicators (optional)
#define LED_SUCCESS  7  // Green LED - card read successfully
#define LED_READY    6  // Blue LED - ready to scan

// Buzzer (optional)
#define BUZZER_PIN   8

MFRC522 rfid(SS_PIN, RST_PIN);

// Debounce: prevent reading same card multiple times
String lastUID = "";
unsigned long lastReadTime = 0;
const unsigned long DEBOUNCE_MS = 2000;  // 2 seconds debounce

void setup() {
  Serial.begin(9600);
  while (!Serial);  // Wait for serial port to connect
  
  // Initialize SPI bus
  SPI.begin();
  
  // Initialize MFRC522
  rfid.PCD_Init();
  delay(100);
  
  // Setup LEDs and buzzer (optional)
  pinMode(LED_SUCCESS, OUTPUT);
  pinMode(LED_READY, OUTPUT);
  pinMode(BUZZER_PIN, OUTPUT);
  
  // Initial state
  digitalWrite(LED_READY, HIGH);
  digitalWrite(LED_SUCCESS, LOW);
  
  // Print ready message
  Serial.println("RFID_READY");
  
  // Check if MFRC522 is connected
  byte version = rfid.PCD_ReadRegister(rfid.VersionReg);
  if (version == 0x00 || version == 0xFF) {
    Serial.println("ERROR:MFRC522_NOT_FOUND");
  } else {
    Serial.print("MFRC522_VERSION:");
    Serial.println(version, HEX);
  }
}

void loop() {
  // Blink ready LED to show device is active
  static unsigned long lastBlink = 0;
  if (millis() - lastBlink > 1000) {
    digitalWrite(LED_READY, !digitalRead(LED_READY));
    lastBlink = millis();
  }
  
  // Check for serial commands from PC
  if (Serial.available() > 0) {
    String cmd = Serial.readStringUntil('\n');
    cmd.trim();
    
    if (cmd == "PING") {
      Serial.println("PONG");
    } else if (cmd == "STATUS") {
      Serial.println("RFID_READY");
    } else if (cmd == "RESET") {
      rfid.PCD_Reset();
      rfid.PCD_Init();
      lastUID = "";
      Serial.println("RFID_RESET_OK");
    }
  }
  
  // Look for new cards
  if (!rfid.PICC_IsNewCardPresent()) {
    return;
  }
  
  // Select one of the cards
  if (!rfid.PICC_ReadCardSerial()) {
    return;
  }
  
  // Convert UID to hex string
  String uidStr = getUIDString();
  
  // Debounce check
  if (uidStr == lastUID && (millis() - lastReadTime) < DEBOUNCE_MS) {
    rfid.PICC_HaltA();
    rfid.PCD_StopCrypto1();
    return;
  }
  
  // Update debounce tracking
  lastUID = uidStr;
  lastReadTime = millis();
  
  // Send UID to PC
  Serial.print("RFID:");
  Serial.println(uidStr);
  
  // Visual/audio feedback
  successFeedback();
  
  // Halt PICC
  rfid.PICC_HaltA();
  
  // Stop encryption on PCD
  rfid.PCD_StopCrypto1();
}

String getUIDString() {
  String uid = "";
  for (byte i = 0; i < rfid.uid.size; i++) {
    if (rfid.uid.uidByte[i] < 0x10) {
      uid += "0";
    }
    uid += String(rfid.uid.uidByte[i], HEX);
  }
  uid.toUpperCase();
  return uid;
}

void successFeedback() {
  // LED feedback
  digitalWrite(LED_SUCCESS, HIGH);
  
  // Buzzer beep (optional)
  tone(BUZZER_PIN, 2000, 100);
  delay(150);
  tone(BUZZER_PIN, 2500, 100);
  
  delay(300);
  digitalWrite(LED_SUCCESS, LOW);
}
