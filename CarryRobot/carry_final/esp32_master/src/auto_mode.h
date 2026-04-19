#pragma once
#include <Arduino.h>

void autoModeInit();
void autoModeLoop();          // call from main loop
void autoModeActivateReturn(); // switch to AUTO_RETURNING with current route (called by MQTT)
