#include "uart_handler.h"
#include "globals.h"
#include "imu_balance.h"

// ============================================================
// UART Handler — Implementation
// ============================================================

void uartInit() {
  UserSerial.begin(USER_UART_BAUD, SERIAL_8N1, USER_UART_RX, USER_UART_TX);
  Serial.println("UART to User Manager initialized");
}

void handleUserUART() {
  while (UserSerial.available()) {
    char c = UserSerial.read();

    if (c == '\n' || c == '\r') {
      if (uartBufIdx > 0) {
        uartBuffer[uartBufIdx] = '\0';
        processUserCommand(uartBuffer);
        uartBufIdx = 0;
      }
    } else if (uartBufIdx < (int)sizeof(uartBuffer) - 1) {
      uartBuffer[uartBufIdx++] = c;
    }
  }
}

void processUserCommand(const char* cmd) {
  Serial.print("RX from User Manager: ");
  Serial.println(cmd);

  // Parse "KEY:VALUE" or just "KEY"
  char key[16], value[32];
  value[0] = '\0';

  const char* colonPos = strchr(cmd, ':');
  if (colonPos) {
    int keyLen = colonPos - cmd;
    if (keyLen > 15) keyLen = 15;
    strncpy(key, cmd, keyLen);
    key[keyLen] = '\0';
    strncpy(value, colonPos + 1, sizeof(value) - 1);
    value[sizeof(value) - 1] = '\0';
  } else {
    strncpy(key, cmd, sizeof(key) - 1);
    key[sizeof(key) - 1] = '\0';
  }

  // --- Process commands ---
  if (strcmp(key, "CMD") == 0) {
    if (strcmp(value, "FWD") == 0) {
      currentCmd = CMD_FORWARD;
      stepCount++;
      Serial.println("Command: FORWARD");
      sendToUser("ACK:FWD");
    } else if (strcmp(value, "BACK") == 0) {
      currentCmd = CMD_BACKWARD;
      stepCount++;
      Serial.println("Command: BACKWARD");
      sendToUser("ACK:BACK");
    } else if (strcmp(value, "LEFT") == 0) {
      currentCmd = CMD_LEFT;
      Serial.println("Command: LEFT");
      sendToUser("ACK:LEFT");
    } else if (strcmp(value, "RIGHT") == 0) {
      currentCmd = CMD_RIGHT;
      Serial.println("Command: RIGHT");
      sendToUser("ACK:RIGHT");
    }
  }
  else if (strcmp(key, "STOP") == 0 || strcmp(cmd, "STOP") == 0) {
    currentCmd = CMD_STOP;
    Serial.println("Command: STOP");
    sendToUser("ACK:STOP");
  }
  else if (strcmp(key, "SPEED") == 0) {
    moveSpeed = atoi(value);
    if (moveSpeed > 100) moveSpeed = 100;
    Serial.print("Speed set to: ");
    Serial.println(moveSpeed);
    sendToUser("ACK:SPEED");
  }
  else if (strcmp(key, "BALANCE") == 0) {
    if (strcmp(value, "ON") == 0) {
      balanceEnabled = true;
      calibrateReferenceAngles();
      balanceStartMs = millis();
      Serial.println("Balance ENABLED");
      sendToUser("BALANCE:OK");
    } else if (strcmp(value, "OFF") == 0) {
      balanceEnabled = false;
      standStraight();
      Serial.println("Balance DISABLED");
      sendToUser("BALANCE:OFF");
    }
  }
  else if (strcmp(key, "CALIBRATE") == 0 || strcmp(cmd, "CALIBRATE") == 0) {
    calibrateReferenceAngles();
    balanceStartMs = millis();
    Serial.println("Calibration complete");
    sendToUser("STATUS:CALIBRATED");
  }
  else if (strcmp(key, "STATUS") == 0 || strcmp(cmd, "STATUS") == 0) {
    sendStepCount();
    sendBalanceStatus();
  }
  else {
    Serial.print("Unknown command: ");
    Serial.println(cmd);
    sendToUser("ERROR:UNKNOWN_CMD");
  }
}

void sendToUser(const char* msg) {
  UserSerial.println(msg);
  Serial.print("TX to User Manager: ");
  Serial.println(msg);
}

void sendStepCount() {
  if (stepCount != lastStepSentCount) {
    char buf[24];
    snprintf(buf, sizeof(buf), "STEP:%lu", (unsigned long)stepCount);
    sendToUser(buf);
    lastStepSentCount = stepCount;
  }
}

void sendBalanceStatus() {
  if (balanceEnabled) {
    float pitch, roll, pitchRate, rollRate;
    readPitchRoll(0.02f, pitch, roll, pitchRate, rollRate);

    float pitchE = fabsf(pitchRef - pitch);
    float rollE  = fabsf(rollRef  - roll);

    if (pitchE < 5.0f && rollE < 5.0f) {
      sendToUser("BALANCE:OK");
    } else if (pitchE < 10.0f && rollE < 10.0f) {
      sendToUser("BALANCE:WARN");
    } else {
      sendToUser("BALANCE:ERROR");
    }
  } else {
    sendToUser("BALANCE:OFF");
  }
}
