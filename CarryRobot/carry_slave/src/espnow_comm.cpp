#include "espnow_comm.h"
#include "config.h"
#include "globals.h"
#include "route_runner.h"
#include <esp_now.h>
#include <WiFi.h>
#include <esp_wifi.h>

static unsigned long s_lastMasterRxMs = 0;
static bool s_linkLocked = false;
static unsigned long s_lastPeerRefreshMs = 0;
static uint8_t s_scanChannel = 1;
static uint8_t s_currentChannel = ESPNOW_CHANNEL;

static const unsigned long LINK_LOSS_TIMEOUT_MS = 1200;
static const unsigned long RELINK_PEER_REFRESH_MS = 400;
static const uint8_t WIFI_CHANNEL_MIN = 1;
static const uint8_t WIFI_CHANNEL_MAX = 13;

static void addOrRefreshMasterPeer() {
    if (esp_now_is_peer_exist(MASTER_MAC)) {
        esp_now_del_peer(MASTER_MAC);
    }

    esp_now_peer_info_t peer = {};
    memcpy(peer.peer_addr, MASTER_MAC, 6);
    peer.channel = s_currentChannel;
    peer.encrypt = false;

    const esp_err_t addRc = esp_now_add_peer(&peer);
    if (addRc != ESP_OK && addRc != ESP_ERR_ESPNOW_EXIST) {
        Serial.printf("[ESPNOW] Add peer FAILED (%d)\n", (int)addRc);
    }
}

static void setWiFiChannel(uint8_t ch) {
    const uint8_t safeCh = constrain(ch, WIFI_CHANNEL_MIN, WIFI_CHANNEL_MAX);
    s_currentChannel = safeCh;
    esp_err_t err = esp_wifi_set_channel(safeCh, WIFI_SECOND_CHAN_NONE);
    if (err != ESP_OK) {
        Serial.printf("[ESPNOW] set_channel(%u) failed: %d\n", safeCh, (int)err);
    }
}

static void onRecv(const uint8_t *mac, const uint8_t *data, int len) {
    if (memcmp(mac, MASTER_MAC, 6) != 0) {
        return;
    }

    if (len == sizeof(MasterToSlaveRouteChunk)) {
        routeRunnerOnChunk(data, len);
        s_lastMasterRxMs = millis();
        if (!s_linkLocked) {
            s_linkLocked = true;
            Serial.printf("[ESPNOW] Link locked with master on channel %u\n", (unsigned)s_currentChannel);
        }
        return;
    }

    if (len == sizeof(MasterToSlaveMsg)) {
        memcpy((void*)&masterCmd, data, sizeof(MasterToSlaveMsg));
        masterCmdNew = true;
        s_lastMasterRxMs = millis();

        if (!s_linkLocked) {
            s_linkLocked = true;
            Serial.printf("[ESPNOW] Link locked with master on channel %u\n", (unsigned)s_currentChannel);
        }
    }
}

static void onSent(const uint8_t *mac, esp_now_send_status_t status) {

}

void espnowSlaveInit() {
    WiFi.mode(WIFI_STA);

    WiFi.disconnect(false, false);

    setWiFiChannel(ESPNOW_CHANNEL);

    if (esp_now_init() != ESP_OK) {
        Serial.println(F("[ESPNOW] Init FAILED"));
        return;
    }
    esp_now_register_recv_cb(onRecv);
    esp_now_register_send_cb(onSent);

    addOrRefreshMasterPeer();
    Serial.println(F("[ESPNOW] Peer added/refreshed"));

    s_lastMasterRxMs = 0;
    s_linkLocked = false;
    s_scanChannel = ESPNOW_CHANNEL;
    s_lastPeerRefreshMs = millis();
}

void espnowSendToMaster(const SlaveToMasterMsg &msg) {
    esp_now_send(MASTER_MAC, (const uint8_t*)&msg, sizeof(msg));
}

void espnowSlaveMaintainLink() {
    const unsigned long now = millis();

    if (s_linkLocked && (now - s_lastMasterRxMs) <= LINK_LOSS_TIMEOUT_MS) {
        return;
    }

    if (s_linkLocked) {
        s_linkLocked = false;
        Serial.println(F("[ESPNOW] Link lost, scanning channels 1–13"));
    }

    if ((now - s_lastPeerRefreshMs) >= RELINK_PEER_REFRESH_MS) {
        s_lastPeerRefreshMs = now;
        s_scanChannel = (s_scanChannel % 13) + 1;
        setWiFiChannel(s_scanChannel);
        addOrRefreshMasterPeer();
    }
}

bool espnowSlaveLinked() {
    return s_linkLocked;
}
