#include "line_sensor.h"

static uint16_t s_consecLost = 0;

void lineInit() {
    pinMode(LINE_S1, INPUT_PULLUP);
    pinMode(LINE_S2, INPUT_PULLUP);
    pinMode(LINE_S3, INPUT_PULLUP);
    s_consecLost = 0;
}

// active LOW: LOW = line detected
bool lineLeft()   { return digitalRead(LINE_S1) == LOW; }
bool lineCenter() { return digitalRead(LINE_S2) == LOW; }
bool lineRight()  { return digitalRead(LINE_S3) == LOW; }

bool lineDetected() { return lineLeft() || lineCenter() || lineRight(); }

float lineReadError() {
    bool l = lineLeft();
    bool c = lineCenter();
    bool r = lineRight();

    // track consecutive no-line reads
    if (!l && !c && !r) {
        s_consecLost++;
        return 0.0f;
    }

    s_consecLost = 0;   // any sensor sees line → reset

    float sum   = 0.0f;
    float count = 0.0f;
    if (l) { sum += -1.0f; count += 1.0f; }
    if (c) { sum +=  0.0f; count += 1.0f; }
    if (r) { sum +=  1.0f; count += 1.0f; }

    return sum / count;                     // -1.0 … +1.0
}

uint16_t lineConsecLost() { return s_consecLost; }
void     lineHealthReset() { s_consecLost = 0; }
