#pragma once
#include <Arduino.h>
#include "config.h"

void    tofInit();
bool    tofAvailable();
int     tofReadMm();          // returns distance in mm, 0 on error
bool    tofObstacle();        // ≤ TOF_STOP_MM
bool    tofClear();           // ≥ TOF_RESUME_MM
