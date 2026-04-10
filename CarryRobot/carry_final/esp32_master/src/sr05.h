#pragma once
#include <Arduino.h>
#include "config.h"

void  sr05Init();
float sr05ReadLeft();    // distance in cm  (0 = timeout / no echo)
float sr05ReadRight();
bool  sr05WallLeft();    // < SR05_WALL_WARN_CM
bool  sr05WallRight();
