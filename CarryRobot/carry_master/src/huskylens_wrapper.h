/*  huskylens_wrapper.h  –  HuskyLens camera abstraction
 */
#pragma once
#include <Arduino.h>

struct HuskyTarget {
    bool  detected;
    int16_t xCenter;    // pixel x
    int16_t yCenter;    // pixel y
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
bool  huskyRequest();                      // Poll for new frame
HuskyTarget huskyGetTarget();              // Block detection (face / object / tag)
HuskyLine   huskyGetLine();                // Arrow detection (line tracking)

void  huskySwitchToObjectTracking();
void  huskySwitchToFaceRecognition();
void  huskySwitchToLineTracking();
void  huskySwitchToTagRecognition();
