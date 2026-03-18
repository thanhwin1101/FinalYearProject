#pragma once
#include <Arduino.h>

void smInit();
void smUpdate();

void smOnSingleClick();
void smOnDoubleClick();
void smOnLongPress();

void smOnSlaveRfid(const char* uid);
void smOnSlaveSyncDocking();

void smOnMqttCommand(const char* cmd, const char* value = nullptr);

void smEnterMissionDelegated();
void smEnterIdle();

void smSetWaitingAtDest(bool waiting);
