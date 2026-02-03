/**
 * @file AprilTagDetector.h
 * @brief Simple AprilTag detection using edge detection
 * @details Lightweight implementation for ESP32-CAM
 *          Detects square markers by finding quadrilaterals
 * 
 * Note: This is a simplified detector. For production, consider using
 * a more robust library or external processing.
 */

#ifndef APRILTAG_DETECTOR_H
#define APRILTAG_DETECTOR_H

#include <Arduino.h>

// Detection result
typedef struct {
    uint8_t id;             // Detected tag ID (0 = no detection)
    int16_t centerX;        // Center X (-width/2 to width/2)
    int16_t centerY;        // Center Y (-height/2 to height/2)
    uint16_t size;          // Approximate size in pixels
    uint8_t confidence;     // Detection confidence (0-100)
    float corners[4][2];    // Corner coordinates
} TagDetection;

class AprilTagDetector {
public:
    /**
     * @brief Initialize detector
     * @param width Frame width
     * @param height Frame height
     */
    void begin(int width, int height);

    /**
     * @brief Detect AprilTags in grayscale image
     * @param grayscale Pointer to grayscale image data
     * @param width Image width
     * @param height Image height
     * @param results Array to store detection results
     * @param maxResults Maximum number of results
     * @return Number of tags detected
     */
    int detect(const uint8_t* grayscale, int width, int height, 
               TagDetection* results, int maxResults);

    /**
     * @brief Set detection parameters
     */
    void setMinSize(int minSize) { _minSize = minSize; }
    void setThreshold(int threshold) { _threshold = threshold; }

private:
    // Simple edge detection
    void sobelEdge(const uint8_t* src, uint8_t* dst, int width, int height);
    
    // Find quadrilaterals (potential tags)
    int findQuads(const uint8_t* edge, int width, int height,
                  TagDetection* results, int maxResults);
    
    // Decode tag ID from region
    uint8_t decodeTagId(const uint8_t* grayscale, int width,
                        const TagDetection& detection);

    int _width = 320;
    int _height = 240;
    int _minSize = 20;
    int _threshold = 50;
    
    // Working buffers
    uint8_t* _edgeBuffer = nullptr;
};

// Global instance
extern AprilTagDetector aprilTagDetector;

#endif // APRILTAG_DETECTOR_H
