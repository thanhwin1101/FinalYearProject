#pragma once
#include <Arduino.h>

struct HuskyTarget {
    bool  detected;
    int16_t xCenter;
    int16_t yCenter;
    int16_t width;
    int16_t height;
    int16_t id;
};

struct HuskyLine {
    bool  detected;
    int16_t xOrigin;
    int16_t yOrigin;
    int16_t xTarget;
    int16_t yTarget;
};

void  huskyInit();
void  huskyMaintain();
bool  huskyIsReady();
bool  huskyRequest();
HuskyTarget huskyGetTarget();
HuskyLine   huskyGetLine();

void  huskySwitchToObjectTracking();
void  huskySwitchToFaceRecognition();
void  huskySwitchToLineTracking();
void  huskySwitchToTagRecognition();
