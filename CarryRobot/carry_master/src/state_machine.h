/*  state_machine.h  –  Top-level state transitions and per-state logic
 */
#pragma once
#include <Arduino.h>

void smInit();
void smUpdate();          // call every loop iteration

// Button events (called from debounce logic in main)
void smOnSingleClick();
void smOnDoubleClick();   // 2 quick presses → Follow (when IDLE) or back to Idle (when FOLLOW)
void smOnLongPress();

// Slave message processing
void smOnSlaveRfid(const char* uid);
void smOnSlaveSyncDocking();

// MQTT remote command handler
// cmd: "set_mode"   value: "follow" | "idle"
// cmd: "follow"  or  cmd: "idle"   (shorthand)
void smOnMqttCommand(const char* cmd, const char* value = nullptr);

// Mission delegated to Slave: enter state and optionally call from mission_delegate after sending routes
void smEnterMissionDelegated();
void smEnterIdle();

// When Slave reports complete (at dest), set so next SW triggers startReturn
void smSetWaitingAtDest(bool waiting);
