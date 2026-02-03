/**
 * @file config.h
 * @brief Configuration for ESP32-CAM AprilTag detector
 */

#ifndef CONFIG_H
#define CONFIG_H

// ============================================================================
// CAMERA PINS (AI-Thinker ESP32-CAM)
// ============================================================================
#define PWDN_GPIO_NUM     32
#define RESET_GPIO_NUM    -1
#define XCLK_GPIO_NUM      0
#define SIOD_GPIO_NUM     26
#define SIOC_GPIO_NUM     27

#define Y9_GPIO_NUM       35
#define Y8_GPIO_NUM       34
#define Y7_GPIO_NUM       39
#define Y6_GPIO_NUM       36
#define Y5_GPIO_NUM       21
#define Y4_GPIO_NUM       19
#define Y3_GPIO_NUM       18
#define Y2_GPIO_NUM        5
#define VSYNC_GPIO_NUM    25
#define HREF_GPIO_NUM     23
#define PCLK_GPIO_NUM     22

// ============================================================================
// LED PINS
// ============================================================================
#define LED_BUILTIN       33  // Red LED on ESP32-CAM
#define LED_FLASH          4  // Flash LED

// ============================================================================
// CAMERA SETTINGS
// ============================================================================
#define FRAME_WIDTH       320
#define FRAME_HEIGHT      240
#define FRAME_RATE        15   // Target FPS

// ============================================================================
// APRILTAG SETTINGS
// ============================================================================
// Using Tag36h11 family (most common, good detection)
#define TAG_FAMILY        "tag36h11"
#define MIN_TAG_SIZE      20   // Minimum tag size in pixels to detect
#define MAX_TAGS          4    // Maximum tags to detect per frame

// ============================================================================
// ESP-NOW SETTINGS
// ============================================================================
// Main robot ESP32 MAC address (UPDATE THIS!)
// Get it by running: WiFi.macAddress() on main ESP32
#define ROBOT_MAC_ADDR    {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF}  // Broadcast initially

// ESP-NOW channel (should match main robot)
#define ESPNOW_CHANNEL    1

// ============================================================================
// TIMING
// ============================================================================
#define SEND_INTERVAL_MS  50   // Send data every 50ms (20Hz)
#define LED_BLINK_MS      100  // LED blink on detection

#endif // CONFIG_H
