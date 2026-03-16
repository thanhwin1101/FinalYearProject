/*  espnow_comm.cpp  –  ESP-NOW master: send commands, receive sensor data
 */
#include "espnow_comm.h"
#include "config.h"
#include "globals.h"
#include <esp_now.h>
#include <WiFi.h>
#include <esp_wifi.h>

static volatile unsigned long s_lastSlaveRxMs = 0;
static unsigned long s_lastRelockTryMs = 0;
static const unsigned long MASTER_RELOCK_INTERVAL_MS = 700;

static void addOrRefreshSlavePeer() {
    if (esp_now_is_peer_exist(SLAVE_MAC)) {
        esp_now_del_peer(SLAVE_MAC);
    }

    // Use current WiFi channel when connected (so we match the AP); else fixed channel 7 for pre-WiFi link.
    uint8_t ch = ESPNOW_CHANNEL;
    if (WiFi.status() == WL_CONNECTED) {
        ch = (uint8_t)WiFi.channel();
        Serial.printf("[ESPNOW] Using WiFi channel %u\n", (unsigned)ch);
    }

    esp_now_peer_info_t peer = {};
    memcpy(peer.peer_addr, SLAVE_MAC, 6);
    peer.channel = ch;
    peer.encrypt = false;

    const esp_err_t addPeerRc = esp_now_add_peer(&peer);
    if (addPeerRc != ESP_OK && addPeerRc != ESP_ERR_ESPNOW_EXIST) {
        Serial.printf("[ESPNOW] Add peer FAILED (%d)\n", (int)addPeerRc);
    }
}

/* ─── Callbacks ─── */

static void onSent(const uint8_t *mac, esp_now_send_status_t status) {
    // (optional) track delivery rate
}

static void onRecv(const uint8_t *mac, const uint8_t *data, int len) {
    if (memcmp(mac, SLAVE_MAC, 6) != 0) {
        return;
    }

    if (len == sizeof(SlaveToMasterMsg)) {
        memcpy((void*)&slaveMsg, data, sizeof(SlaveToMasterMsg));
        slaveMsgNew = true;
        s_lastSlaveRxMs = millis();
    }
}

/* ─── Init ─── */
void espnowMasterInit() {
    const esp_err_t initRc = esp_now_init();
    if (initRc != ESP_OK && initRc != ESP_ERR_ESPNOW_EXIST) {
        Serial.printf("[ESPNOW] Init FAILED (%d)\n", (int)initRc);
        return;
    }

    esp_now_register_send_cb(onSent);
    esp_now_register_recv_cb(onRecv);
    addOrRefreshSlavePeer();
    Serial.println(F("[ESPNOW] Peer added/refreshed"));
}

/* ─── Send ─── */
void espnowSendToSlave(const MasterToSlaveMsg &msg) {
    esp_now_send(SLAVE_MAC, (const uint8_t*)&msg, sizeof(msg));
}

void espnowSendRouteChunk(const MasterToSlaveRouteChunk &chunk) {
    esp_now_send(SLAVE_MAC, (const uint8_t*)&chunk, sizeof(chunk));
}

bool espnowSlaveConnected() {
    const unsigned long lastRx = s_lastSlaveRxMs;
    if (lastRx == 0) return false;
    return (millis() - lastRx) <= ESPNOW_SLAVE_TIMEOUT_MS;
}

void espnowMasterMaintainLink() {
    if (espnowSlaveConnected()) return;

    const unsigned long now = millis();
    if ((now - s_lastRelockTryMs) < MASTER_RELOCK_INTERVAL_MS) return;
    s_lastRelockTryMs = now;

    addOrRefreshSlavePeer();
    Serial.println(F("[ESPNOW] Slave link down, trying peer relock"));
}
