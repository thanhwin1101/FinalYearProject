#pragma once
#include <Arduino.h>

#define RFID_UID_LOCAL_LEN 24

void rfidInit();

bool rfidRead(char uid[RFID_UID_LOCAL_LEN]);
