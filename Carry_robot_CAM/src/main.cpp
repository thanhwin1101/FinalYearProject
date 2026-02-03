/**
 * @file main.cpp
 * @brief ESP32-CAM AprilTag Detector - Follow Mode Camera
 * 
 * This firmware runs on ESP32-CAM module and:
 * 1. Captures frames from OV2640 camera
 * 2. Detects AprilTag-like markers (or colored markers)
 * 3. Sends marker position via ESP-NOW to main robot ESP32
 * 
 * Hardware: AI-Thinker ESP32-CAM module
 * 
 * Connection: ESP-NOW wireless (no physical wires needed)
 * 
 * For best results, use a simple colored marker (red/green square)
 * instead of full AprilTag detection, as it's more reliable on ESP32.
 */

#include <Arduino.h>
#include <WiFi.h>
#include <esp_now.h>
#include <esp_camera.h>
#include "config.h"
#include "AprilTagDetector.h"

// ============================================================================
// DATA STRUCTURES
// ============================================================================

// Data packet to send to main robot (must match FollowMode.h)
typedef struct __attribute__((packed)) {
    uint8_t tagId;          // Tag ID (0 = no detection)
    int16_t centerX;        // X position (-160 to 160)
    int16_t centerY;        // Y position (-120 to 120)  
    uint16_t tagSize;       // Size in pixels
    uint8_t confidence;     // Confidence 0-100
    uint32_t timestamp;     // Timestamp
} AprilTagData;

// ============================================================================
// GLOBAL VARIABLES
// ============================================================================

// Peer info for ESP-NOW
esp_now_peer_info_t peerInfo;
uint8_t robotMacAddr[] = ROBOT_MAC_ADDR;

// Detection data
AprilTagData tagData;
TagDetection detections[MAX_TAGS];

// Timing
unsigned long lastSendTime = 0;
unsigned long lastDetectionTime = 0;
unsigned long frameCount = 0;
unsigned long lastFpsTime = 0;

// Status
bool espNowReady = false;
bool cameraReady = false;

// ============================================================================
// FUNCTION DECLARATIONS
// ============================================================================

bool initCamera();
bool initEspNow();
void processFrame();
void sendTagData();
void onDataSent(const uint8_t* mac, esp_now_send_status_t status);
void blinkLed(int times, int delayMs);

// Color detection (alternative to AprilTag)
bool detectColorMarker(camera_fb_t* fb, TagDetection* result);

// ============================================================================
// SETUP
// ============================================================================

void setup() {
    Serial.begin(115200);
    delay(1000);
    
    Serial.println("\n========================================");
    Serial.println("  ESP32-CAM AprilTag Detector");
    Serial.println("  For Carry Robot Follow Mode");
    Serial.println("========================================\n");
    
    // Initialize LED
    pinMode(LED_BUILTIN, OUTPUT);
    pinMode(LED_FLASH, OUTPUT);
    digitalWrite(LED_BUILTIN, HIGH);  // LED off (inverted)
    digitalWrite(LED_FLASH, LOW);
    
    // Blink to show starting
    blinkLed(3, 100);
    
    // Initialize camera
    Serial.println("Initializing camera...");
    if (!initCamera()) {
        Serial.println("ERROR: Camera init failed!");
        while (1) {
            blinkLed(5, 200);
            delay(1000);
        }
    }
    cameraReady = true;
    Serial.println("Camera OK");
    
    // Initialize AprilTag detector
    Serial.println("Initializing detector...");
    aprilTagDetector.begin(FRAME_WIDTH, FRAME_HEIGHT);
    aprilTagDetector.setMinSize(MIN_TAG_SIZE);
    
    // Initialize ESP-NOW
    Serial.println("Initializing ESP-NOW...");
    if (!initEspNow()) {
        Serial.println("ERROR: ESP-NOW init failed!");
        while (1) {
            blinkLed(2, 500);
            delay(1000);
        }
    }
    espNowReady = true;
    Serial.println("ESP-NOW OK");
    
    // Print MAC address
    Serial.print("CAM MAC Address: ");
    Serial.println(WiFi.macAddress());
    Serial.println("\nCopy this MAC to main robot's config!\n");
    
    // Ready
    Serial.println("========================================");
    Serial.println("  READY - Looking for tags...");
    Serial.println("========================================\n");
    
    blinkLed(2, 50);
}

// ============================================================================
// MAIN LOOP
// ============================================================================

void loop() {
    unsigned long now = millis();
    
    // Process camera frame
    if (cameraReady) {
        processFrame();
        frameCount++;
    }
    
    // Send data at fixed interval
    if (espNowReady && (now - lastSendTime >= SEND_INTERVAL_MS)) {
        sendTagData();
        lastSendTime = now;
    }
    
    // Calculate and print FPS every second
    if (now - lastFpsTime >= 1000) {
        float fps = (float)frameCount * 1000.0f / (now - lastFpsTime);
        Serial.printf("FPS: %.1f\n", fps);
        frameCount = 0;
        lastFpsTime = now;
    }
    
    // Small delay
    delay(1);
}

// ============================================================================
// CAMERA INITIALIZATION
// ============================================================================

bool initCamera() {
    camera_config_t config;
    config.ledc_channel = LEDC_CHANNEL_0;
    config.ledc_timer = LEDC_TIMER_0;
    config.pin_d0 = Y2_GPIO_NUM;
    config.pin_d1 = Y3_GPIO_NUM;
    config.pin_d2 = Y4_GPIO_NUM;
    config.pin_d3 = Y5_GPIO_NUM;
    config.pin_d4 = Y6_GPIO_NUM;
    config.pin_d5 = Y7_GPIO_NUM;
    config.pin_d6 = Y8_GPIO_NUM;
    config.pin_d7 = Y9_GPIO_NUM;
    config.pin_xclk = XCLK_GPIO_NUM;
    config.pin_pclk = PCLK_GPIO_NUM;
    config.pin_vsync = VSYNC_GPIO_NUM;
    config.pin_href = HREF_GPIO_NUM;
    config.pin_sccb_sda = SIOD_GPIO_NUM;
    config.pin_sccb_scl = SIOC_GPIO_NUM;
    config.pin_pwdn = PWDN_GPIO_NUM;
    config.pin_reset = RESET_GPIO_NUM;
    config.xclk_freq_hz = 20000000;
    config.frame_size = FRAMESIZE_QVGA;  // 320x240
    config.pixel_format = PIXEL_FORMAT_GRAYSCALE;  // For detection
    config.grab_mode = CAMERA_GRAB_WHEN_EMPTY;
    config.fb_location = CAMERA_FB_IN_PSRAM;
    config.jpeg_quality = 12;
    config.fb_count = 2;
    
    // Initialize camera
    esp_err_t err = esp_camera_init(&config);
    if (err != ESP_OK) {
        Serial.printf("Camera init failed: 0x%x\n", err);
        return false;
    }
    
    // Get camera sensor
    sensor_t* s = esp_camera_sensor_get();
    if (s) {
        // Optimize for detection
        s->set_brightness(s, 0);
        s->set_contrast(s, 1);
        s->set_saturation(s, 0);
        s->set_gainceiling(s, GAINCEILING_4X);
        s->set_whitebal(s, 1);
        s->set_awb_gain(s, 1);
        s->set_exposure_ctrl(s, 1);
        s->set_aec2(s, 1);
        s->set_gain_ctrl(s, 1);
        s->set_agc_gain(s, 0);
        s->set_bpc(s, 1);
        s->set_wpc(s, 1);
    }
    
    return true;
}

// ============================================================================
// ESP-NOW INITIALIZATION
// ============================================================================

bool initEspNow() {
    // Set WiFi mode to station
    WiFi.mode(WIFI_STA);
    WiFi.disconnect();
    
    // Initialize ESP-NOW
    if (esp_now_init() != ESP_OK) {
        Serial.println("ESP-NOW init error");
        return false;
    }
    
    // Register send callback
    esp_now_register_send_cb(onDataSent);
    
    // Register peer (main robot)
    memcpy(peerInfo.peer_addr, robotMacAddr, 6);
    peerInfo.channel = ESPNOW_CHANNEL;
    peerInfo.encrypt = false;
    
    if (esp_now_add_peer(&peerInfo) != ESP_OK) {
        Serial.println("Failed to add peer");
        // Continue anyway - might be using broadcast
    }
    
    return true;
}

// ============================================================================
// FRAME PROCESSING
// ============================================================================

void processFrame() {
    // Capture frame
    camera_fb_t* fb = esp_camera_fb_get();
    if (!fb) {
        Serial.println("Frame capture failed");
        return;
    }
    
    // Detect tags
    int numTags = 0;
    
    // Option 1: AprilTag-like detection (edge-based)
    if (fb->format == PIXFORMAT_GRAYSCALE) {
        numTags = aprilTagDetector.detect(fb->buf, fb->width, fb->height,
                                          detections, MAX_TAGS);
    }
    
    // Option 2: Color marker detection (more reliable)
    // Uncomment if using colored marker instead of AprilTag
    // if (detectColorMarker(fb, &detections[0])) {
    //     numTags = 1;
    // }
    
    // Update tag data
    if (numTags > 0) {
        // Use first detection
        tagData.tagId = detections[0].id;
        tagData.centerX = detections[0].centerX;
        tagData.centerY = detections[0].centerY;
        tagData.tagSize = detections[0].size;
        tagData.confidence = detections[0].confidence;
        tagData.timestamp = millis();
        
        lastDetectionTime = millis();
        
        // Blink LED on detection
        digitalWrite(LED_BUILTIN, LOW);  // LED on
        
        // Debug
        Serial.printf("Tag %d: X=%d Y=%d Size=%d\n",
                      tagData.tagId, tagData.centerX, tagData.centerY, tagData.tagSize);
    } else {
        // No detection
        tagData.tagId = 0;
        tagData.centerX = 0;
        tagData.centerY = 0;
        tagData.tagSize = 0;
        tagData.confidence = 0;
        tagData.timestamp = millis();
        
        digitalWrite(LED_BUILTIN, HIGH);  // LED off
    }
    
    // Return frame buffer
    esp_camera_fb_return(fb);
}

// ============================================================================
// COLOR MARKER DETECTION (Alternative to AprilTag)
// ============================================================================

bool detectColorMarker(camera_fb_t* fb, TagDetection* result) {
    // This function detects a colored marker (e.g., red or green square)
    // More reliable than AprilTag detection on ESP32
    
    // Note: For color detection, camera should be in RGB565 format
    // This is a placeholder - implement based on your marker color
    
    // Example: Detect red marker
    // 1. Convert to HSV or check R > G && R > B
    // 2. Find centroid of red pixels
    // 3. Return position and size
    
    return false;  // Not implemented in this example
}

// ============================================================================
// SEND DATA VIA ESP-NOW
// ============================================================================

void sendTagData() {
    esp_err_t result = esp_now_send(robotMacAddr, (uint8_t*)&tagData, sizeof(tagData));
    
    if (result != ESP_OK) {
        // Serial.println("ESP-NOW send failed");
    }
}

void onDataSent(const uint8_t* mac, esp_now_send_status_t status) {
    // Optional: Track send success rate
    // if (status != ESP_NOW_SEND_SUCCESS) {
    //     Serial.println("Delivery failed");
    // }
}

// ============================================================================
// UTILITY FUNCTIONS
// ============================================================================

void blinkLed(int times, int delayMs) {
    for (int i = 0; i < times; i++) {
        digitalWrite(LED_BUILTIN, LOW);   // On
        delay(delayMs);
        digitalWrite(LED_BUILTIN, HIGH);  // Off
        delay(delayMs);
    }
}
