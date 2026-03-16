/*  rfid_reader.h  –  PN532 NFC tag reading via SPI
 */
#pragma once
#include <Arduino.h>

#define RFID_UID_LOCAL_LEN 24

void rfidInit();

// Non-blocking read. Returns true if a new UID was read.
// uid[] will contain uppercase colon-separated UID (e.g. "45:D3:91:83").
bool rfidRead(char uid[RFID_UID_LOCAL_LEN]);
