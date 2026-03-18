#pragma once

#define ROBOT_ID              "CARRY-01"
#define ROBOT_NAME            "Carry-01"
#define FW_VERSION            "carry-dual-v1"

#define HUSKY_RX_PIN          16
#define HUSKY_TX_PIN          17
#define HUSKY_BAUD            9600

#define I2C_SDA               21
#define I2C_SCL               22
#define OLED_I2C_ADDR         0x3C

#define US_LEFT_TRIG          25
#define US_LEFT_ECHO          26

#define US_RIGHT_TRIG         32
#define US_RIGHT_ECHO         33

#define SERVO_X_PIN           13
#define SERVO_Y_PIN           12
#define SERVO_X_FB_PIN        34

#define BUZZER_PIN            14

#define SW_PIN                4

#define SERVO_X_CENTER        90
#define SERVO_Y_LEVEL         90
#define SERVO_Y_TILT_DOWN     45
#define SERVO_Y_LOOK_UP      115
#define SERVO_X_MIN           0
#define SERVO_X_MAX           180
#define SERVO_Y_MIN           0
#define SERVO_Y_MAX           180

#define SERVO_FB_ADC_MIN      300
#define SERVO_FB_ADC_MAX      3700

#define HUSKY_IMG_W           320
#define HUSKY_IMG_H           240
#define HUSKY_CENTER_X        (HUSKY_IMG_W / 2)
#define HUSKY_CENTER_Y        (HUSKY_IMG_H / 2)
#define HUSKY_INIT_TIMEOUT_MS 7000
#define HUSKY_RECONNECT_INTERVAL_MS 700
#define HUSKY_FACE_AUTH_ID 1
#define HUSKY_FACE_AUTH_STREAK 3

#define TOF_STOP_DIST         220
#define TOF_RESUME_DIST       300
#define TOF_MEAS_BUDGET_US    20000

#define US_SIDE_WARN_MM       150
#define US_TIMEOUT_US         25000

#define KP_CAM                0.15f
#define KI_CAM                0.001f
#define KD_CAM                0.05f
#define CAM_I_MAX             30.0f

#define KP_ANGLE              2.0f
#define KD_ANGLE              0.3f

#define KP_DIST               0.5f
#define KD_DIST               0.15f
#define FOLLOW_TARGET_MM      500

#define LINE_BASE_SPEED       165

#define TELEMETRY_INTERVAL    1000
#define OLED_INTERVAL         200
#define TOF_INTERVAL          50
#define US_INTERVAL           100
#define HUSKY_INTERVAL        50
#define ESPNOW_TX_INTERVAL    50
#define ESPNOW_RELOCK_BEACON_INTERVAL_MS 10
#define ESPNOW_RELOCK_BEACON_MS 5000
#define ESPNOW_BOOT_WAIT_MS   5000
#define WIFI_LINK_CHECK_INTERVAL_MS 300
#define MQTT_RECONNECT_MS     2000
#define SYSTEM_READY_STABLE_MS 2000
#define SYSTEM_READY_MAX_WAIT_MS 30000
#define OBSTACLE_BEEP_MS      600

#define DEBOUNCE_MS           50
#define LONG_PRESS_MS         3000
#define DOUBLE_CLICK_MS       400
#define MONITOR_SERIAL        1

#define SW_ACTIVE_LOW         1

#define MQTT_DEFAULT_SERVER   "192.168.137.1"
#define MQTT_DEFAULT_PORT     1883
#define MQTT_DEFAULT_USER     "hospital_robot"
#define MQTT_DEFAULT_PASS     "123456"
#define MQTT_BUFFER_SIZE      4096
#define MQTT_PAYLOAD_VERSION  2

#define MQTT_SEND_LEGACY_FIELDS 0

#define WIFI_DEFAULT_SSID    "Test"
#define WIFI_DEFAULT_PASS    "123456789"
#define WIFI_PORTAL_SSID      "CarryMaster-Setup"
#define WIFI_PORTAL_PASS      "carry123"
#define WIFI_PORTAL_TIMEOUT   180

static const uint8_t SLAVE_MAC[6] = {0xB0,0xCB,0xD8,0xC9,0x9F,0x14};
#define ESPNOW_CHANNEL 7
#define ESPNOW_SLAVE_TIMEOUT_MS 1500

#define TURN_90_MS            974
#define TURN_180_MS           1980

#define HOME_MED_UID          "45:54:80:83"
