// ====================================================================
//  carry_final  â€“  STM32 Slave  â€“  follow_runner.cpp
//  Follow / Recovery modes driven locally by STM32
//  HuskyLens on USART3, motors via mecanum.h
// ====================================================================
#include "follow_runner.h"
#include "globals.h"
#include "config.h"
#include "mecanum.h"
#include "motor_control.h"
#include "huskylens_uart.h"
#include "tof_sensor.h"
#include "uart_protocol.h"

extern HardwareSerial SerialMaster;   // UART to ESP32 master

// â”€â”€ Follow constants â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
#define HUSKY_CX        160       // screen center X  (320/2)
#define HUSKY_CY        120       // screen center Y  (240/2)
// 20% of 320Ă—240 = 76800 Ă— 0.20 = 15360 px â†’ target distance
#define TARGET_AREA     15360
#define VX_GAIN         0.5f      // strafe gain: horizontal offset â†’ Vx
#define VY_GAIN         0.01f     // forward gain: area error â†’ Vy
#define TAG_LOST_MS     5000      // ms before reporting tag lost

// â”€â”€ Recovery constants â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
#define REC_SPIN_VR          80     // rotation speed while searching
#define REC_APPROACH_VY      80     // forward speed while approaching
#define REC_APPROACH_VR_GAIN 0.3f  // steering correction during approach
#define REC_CLOSE_AREA       8000   // tag area to consider "close enough"

// â”€â”€ Report helpers (STM32 â†’ ESP32) â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
static void reportHuskyStatus(const HuskyResult &r) {
    uint8_t buf[11];
    buf[0]  = r.detected ? 1 : 0;
    buf[1]  = (r.xCenter >> 8) & 0xFF;  buf[2]  = r.xCenter & 0xFF;
    buf[3]  = (r.yCenter >> 8) & 0xFF;  buf[4]  = r.yCenter & 0xFF;
    buf[5]  = (r.width   >> 8) & 0xFF;  buf[6]  = r.width   & 0xFF;
    buf[7]  = (r.height  >> 8) & 0xFF;  buf[8]  = r.height  & 0xFF;
    buf[9]  = (r.id      >> 8) & 0xFF;  buf[10] = r.id      & 0xFF;
    uartSendFrame(SerialMaster, CMD_HUSKY_STATUS, buf, 11);
}

static void reportTagLost() {
    uartSendFrame(SerialMaster, CMD_TAG_LOST, nullptr, 0);
}

static void reportTagFound() {
    uartSendFrame(SerialMaster, CMD_TAG_FOUND, nullptr, 0);
}

// =====================================================================
//  FOLLOW MODE  â€“ strafe left/right to center tag; advance/retreat for area
// =====================================================================
static uint32_t s_lastTagTime     = 0;
static uint32_t s_lastStatusMs    = 0;
static bool     s_tagLostReported = false;
static bool     s_tagEverSeen     = false;   // true once tag detected ≥1 time

void followRunnerInit() {
    if (!huskyConnected()) huskyReconnect();
    huskySetTagMode();
    s_lastTagTime     = 0;
    s_lastStatusMs    = 0;
    s_tagLostReported = false;
    s_tagEverSeen     = false;   // block reporting until first detection
    motorStop();
    Serial.println("[FOLLOW-STM] init");
}

void followRunnerLoop() {
    if (g_mode != MODE_FOLLOW) return;
    uint32_t now = millis();

    // obstacle â†’ stop
    if (tofObstacle()) { motorStop(); return; }

    HuskyResult tag = huskyRead();

    if (tag.detected) {
        if (!s_tagEverSeen) {
            s_tagEverSeen = true;
            Serial.println("[FOLLOW-STM] first tag detected");
        }
        if (s_tagLostReported) {
            // Tag came back after being reported lost → notify ESP32 immediately
            reportTagFound();
            Serial.println("[FOLLOW-STM] tag regained → TAG_FOUND");
        }
        s_lastTagTime     = now;
        s_tagLostReported = false;

        // strafe: tag X offset â†’ Vx  (positive = strafe right)
        float errX   = (float)(tag.xCenter - HUSKY_CX);
        int16_t vx   = (int16_t)(errX * VX_GAIN);
        vx = constrain(vx, (int16_t)-150, (int16_t)150);

        // forward/backward: area error â†’ Vy
        int16_t area = tag.width * tag.height;
        float errArea = (float)(TARGET_AREA - area);
        int16_t vy    = (int16_t)(errArea * VY_GAIN);
        vy = constrain(vy, (int16_t)-200, (int16_t)200);

        mecanumDrive(vx, vy, 0);
    } else {
        // tag not visible — only report lost if tag was seen at least once
        if (s_tagEverSeen && !s_tagLostReported && (now - s_lastTagTime > TAG_LOST_MS)) {
            motorStop();
            reportTagLost();
            s_tagLostReported = true;
            Serial.println("[FOLLOW-STM] tag lost → reported");
        }
    }

    // periodic status for ESP32 OLED + servo Y adjustment
    if (now - s_lastStatusMs > 200) {
        s_lastStatusMs = now;
        reportHuskyStatus(tag);
    }
}

// =====================================================================
//  RECOVERY MODE  â€“ spin in place until tag found, then approach
// =====================================================================
enum RecSTMPhase : uint8_t { REC_STM_SPIN, REC_STM_APPROACH, REC_STM_WAIT };

static RecSTMPhase s_recPhase   = REC_STM_SPIN;
static int16_t     s_recScanDir = 1;

void recoveryRunnerInit() {
    if (!huskyConnected()) huskyReconnect();
    huskySetTagMode();         // search for person tag
    s_recPhase   = REC_STM_SPIN;
    s_recScanDir = 1;
    mecanumDrive(0, 0, REC_SPIN_VR);   // start spinning immediately
    Serial.println("[RECOVERY-STM] init â€“ spinning");
}

void recoveryRunnerLoop() {
    if (g_mode != MODE_RECOVERY) return;
    uint32_t now = millis();

    HuskyResult r = huskyRead();

    // periodic status for ESP32 OLED
    static uint32_t lastRep = 0;
    if (now - lastRep > 200) {
        lastRep = now;
        reportHuskyStatus(r);
    }

    switch (s_recPhase) {

    case REC_STM_SPIN:
        mecanumDrive(0, 0, s_recScanDir * REC_SPIN_VR);
        if (r.detected) {
            motorStop();
            s_recPhase = REC_STM_APPROACH;
            Serial.println("[RECOVERY-STM] tag found â†’ approaching");
        }
        break;

    case REC_STM_APPROACH:
        if (r.detected) {
            int16_t area = r.width * r.height;
            if (area >= REC_CLOSE_AREA) {
                motorStop();
                reportTagFound();
                s_recPhase = REC_STM_WAIT;
                Serial.println("[RECOVERY-STM] close enough â†’ TAG_FOUND");
            } else {
                // drive forward + steer to keep tag centered
                float errX  = (float)(r.xCenter - HUSKY_CX);
                int16_t vr  = (int16_t)(errX * REC_APPROACH_VR_GAIN);
                mecanumDrive(0, REC_APPROACH_VY, vr);
            }
        } else {
            // lost tag during approach â†’ spin again
            motorStop();
            s_recPhase = REC_STM_SPIN;
            mecanumDrive(0, 0, s_recScanDir * REC_SPIN_VR);
            Serial.println("[RECOVERY-STM] tag lost during approach â†’ spin");
        }
        break;

    case REC_STM_WAIT:
        motorStop();   // ESP32 will switch mode when ready
        break;
    }
}

