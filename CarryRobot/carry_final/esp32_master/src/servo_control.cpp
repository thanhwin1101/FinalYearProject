#include "servo_control.h"
#include "uart_protocol.h"

extern HardwareSerial Serial2;

static int curY = SERVO_Y_LEVEL;

static void sendToSTM32() {
    // data[0] = X placeholder (90°), data[1] = Y angle
    uint8_t data[2] = { 90, (uint8_t)constrain(curY, SERVO_MIN, SERVO_MAX) };
    uartSendFrame(Serial2, CMD_SERVO_SET, data, 2);
}

void servoInit() {
    curY = SERVO_Y_LEVEL;
}

void servoSetY(int angle) {
    curY = constrain(angle, SERVO_MIN, SERVO_MAX);
    sendToSTM32();
}

int servoGetY() { return curY; }
