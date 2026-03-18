#pragma once
#include <Arduino.h>

struct FollowOutput {
    float vX;
    float vY;
    float vR;
};

void followPidReset();

FollowOutput followPidUpdate(int16_t targetPixelX, bool targetSeen,
                             uint16_t tofDistMm, float dt);

bool followIsSearching();
