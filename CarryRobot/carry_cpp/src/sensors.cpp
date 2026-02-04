#include "sensors.h"
#include "config.h"
#include "globals.h"

// =========================================
// NFC FUNCTIONS
// =========================================
void nfcInit() {
  nfc.begin();
  uint32_t versiondata = nfc.getFirmwareVersion();
  if (!versiondata) {
    Serial.println("PN532 not found!");
    while (1) delay(10);
  }
  Serial.print("Found PN532 FW V.");
  Serial.println((versiondata >> 16) & 0xFF, HEX);
  nfc.SAMConfig();
}

bool readNFC(uint8_t* uid, uint8_t* uidLen) {
  return nfc.readPassiveTargetID(PN532_MIFARE_ISO14443A, uid, uidLen, 50);
}

// =========================================
// TOF FUNCTIONS
// =========================================
void tofInit() {
  if (!tof.begin()) {
    Serial.println("VL53L0X fail");
  } else {
    tof.setMeasurementTimingBudget(20000);
    Serial.println("VL53L0X OK");
  }
}

bool tofReadDistance(uint16_t &dist) {
  VL53L0X_RangingMeasurementData_t m;
  tof.rangingTest(&m, false);
  if (m.RangeStatus != 4) {
    dist = m.RangeMilliMeter;
    return true;
  }
  return false;
}
