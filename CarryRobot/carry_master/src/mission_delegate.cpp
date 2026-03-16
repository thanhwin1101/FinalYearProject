/*  mission_delegate.cpp  –  Send routes to Slave via ESP-NOW; start mission on SW+MED
 */
#include <Arduino.h>
#include <string.h>
#include "mission_delegate.h"
#include "config.h"
#include "globals.h"
#include "route_manager.h"
#include "espnow_comm.h"
#include "state_machine.h"
#include "display.h"

static void strCopyPad(char* dest, size_t destSize, const String& src) {
    size_t n = src.length();
    if (n >= destSize) n = destSize - 1;
    memcpy(dest, src.c_str(), n);
    dest[n] = '\0';
}

static void sendRouteChunks() {
    MasterToSlaveRouteChunk chunk = {};
    chunk.type = 0x02;

    const uint8_t nChunk = (outRouteLen + ROUTE_CHUNK_MAX_POINTS - 1) / ROUTE_CHUNK_MAX_POINTS;
    for (uint8_t c = 0; c < nChunk; c++) {
        chunk.segment = 0;
        chunk.chunkIndex = c;
        chunk.chunkTotal = nChunk;
        uint8_t start = c * ROUTE_CHUNK_MAX_POINTS;
        uint8_t n = 0;
        for (uint8_t i = 0; i < ROUTE_CHUNK_MAX_POINTS && (start + i) < outRouteLen; i++) {
            strCopyPad(chunk.points[i].nodeId, ROUTE_POINT_NODE_LEN, outRoute[start + i].nodeId);
            strCopyPad(chunk.points[i].uid,   ROUTE_POINT_UID_LEN,  outRoute[start + i].rfidUid);
            chunk.points[i].action = outRoute[start + i].action;
            n++;
        }
        chunk.numPoints = n;
        espnowSendRouteChunk(chunk);
        delay(20);
    }

    if (retRouteLen > 0) {
        const uint8_t nRetChunk = (retRouteLen + ROUTE_CHUNK_MAX_POINTS - 1) / ROUTE_CHUNK_MAX_POINTS;
        for (uint8_t c = 0; c < nRetChunk; c++) {
            chunk.segment = 1;
            chunk.chunkIndex = c;
            chunk.chunkTotal = nRetChunk;
            uint8_t start = c * ROUTE_CHUNK_MAX_POINTS;
            uint8_t n = 0;
            for (uint8_t i = 0; i < ROUTE_CHUNK_MAX_POINTS && (start + i) < retRouteLen; i++) {
                strCopyPad(chunk.points[i].nodeId, ROUTE_POINT_NODE_LEN, retRoute[start + i].nodeId);
                strCopyPad(chunk.points[i].uid,   ROUTE_POINT_UID_LEN,  retRoute[start + i].rfidUid);
                chunk.points[i].action = retRoute[start + i].action;
                n++;
            }
            chunk.numPoints = n;
            espnowSendRouteChunk(chunk);
            delay(20);
        }
    }
}

void missionDelegateSendRoutesOnly() {
    if (outRouteLen == 0) {
        Serial.println(F("[DELEGATE] No outbound route – skip"));
        return;
    }
    sendRouteChunks();
    Serial.printf("[DELEGATE] Routes sent (%u out, %u ret), wait SW+MED to start\n",
                  (unsigned)outRouteLen, (unsigned)retRouteLen);
}

void missionDelegateStartMission() {
    if (outRouteLen == 0) return;

    masterMsg.state         = (uint8_t)ST_MISSION_DELEGATED;
    masterMsg.enableLine    = 1;
    masterMsg.enableRFID    = 1;
    masterMsg.baseSpeed     = LINE_BASE_SPEED;
    masterMsg.vX            = 0;
    masterMsg.vY            = 0;
    masterMsg.vR            = 0;
    masterMsg.turnCmd       = 0;
    masterMsg.missionStart  = 1;
    masterMsg.missionCancel = 0;
    masterMsg.startReturn   = 0;
    espnowSendToSlave(masterMsg);

    smEnterMissionDelegated();
    displayOutbound(patientName.c_str(), "Slave running...");
    Serial.println(F("[DELEGATE] Mission started (SW+MED)"));
}
