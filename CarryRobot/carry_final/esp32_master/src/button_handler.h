#pragma once
#include <Arduino.h>
#include "config.h"

void buttonInit();
void buttonLoop();   // call from main loop – updates g_btnSingleClick etc.
