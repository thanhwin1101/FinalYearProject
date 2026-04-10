#pragma once
#include <Arduino.h>

void autoRunnerInit();
void autoRunnerLoop();   // call from main loop when in AUTO mode
bool autoRunnerBusy();
