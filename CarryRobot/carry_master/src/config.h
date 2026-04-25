#pragma once
// ====================================================================
//  Carry Robot – ESP32 MASTER – config.h
//  All pin/constant definitions in one place.
// ====================================================================

// ---- WiFiManager (Captive Portal) ---------------------------------
#define WM_AP_SSID          "AGV_01_Setup"
#define WM_AP_PASS          ""              // open AP for first-time setup
#define WM_PORTAL_TIMEOUT_S 0               // 0 = block forever until configured

// ---- MQTT broker (fixed per spec) ----------------------------------
#define MQTT_HOST           "192.168.1.93"
#define MQTT_PORT           1883
#define MQTT_CLIENT_ID      "hospital_robot"
#define MQTT_USER           "hospital_robot"
#define MQTT_PASS           "123456"   // placeholder

// ---- MQTT Topics — Hospital Dashboard backend contract ------------
//  Backend publishes mission to       carry/robot/cmd
//  Backend mirrors mission also to    hospital/robots/AGV-01/mission/assign
//  Master publishes events to         carry/robot/evt
//  Master asks for return route on    robot/return_request
#define TOPIC_CMD_RX        "carry/robot/cmd"
#define TOPIC_EVT_TX        "carry/robot/evt"
#define TOPIC_RETURN_REQ_TX "robot/return_request"
#define ROBOT_ID            "AGV-01"

// ---- UART to STM32 (Serial2) --------------------------------------
#define UART_STM32_RX       16
#define UART_STM32_TX       17
#define UART_STM32_BAUD     115200
#define UART_FRAME_MAX      256

// ---- OLED SH1106 1.3" (I2C) ---------------------------------------
#define PIN_OLED_SDA        21
#define PIN_OLED_SCL        22

// ---- Push button --------------------------------------------------
#define PIN_BUTTON          15
#define BTN_DEBOUNCE_MS     30
#define BTN_LONG_MS         1500

// ---- Buzzer (active HIGH) -----------------------------------------
#define PIN_BUZZER          25
#define ARRIVED_BEEP_MS     3000

// ---- 2-channel Relay -----------------------------------------------
//  Spec: Auto Mode  →  R2 = ON, R1 = OFF
//        Follow Mode →  R1 = ON, R2 = OFF
//  Many cheap 5V opto-coupled relay boards are ACTIVE-LOW (LOW = ON).
//  Set RELAY_ACTIVE_LOW to 1 for those modules; set 0 if your board is
//  active-HIGH (HIGH = ON).
#define RELAY_ACTIVE_LOW    1
#define RELAY_ON_LEVEL      (RELAY_ACTIVE_LOW ? LOW  : HIGH)
#define RELAY_OFF_LEVEL     (RELAY_ACTIVE_LOW ? HIGH : LOW)
#define PIN_RELAY_1         18      // Vision (HuskyLens + servo power)
#define PIN_RELAY_2         23      // Line sensors + PN532 power

// ---- Start checkpoint (fixed per spec) ----------------------------
#define START_CHECKPOINT    "MED"
