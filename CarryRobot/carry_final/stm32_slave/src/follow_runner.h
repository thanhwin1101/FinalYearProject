#pragma once
#include <Arduino.h>

// ── Follow mode: strafe-based tag following via HuskyLens ──────────────
void followRunnerInit();
void followRunnerLoop();

// ── Recovery mode: spin to find tag → approach → report TAG_FOUND ───
void recoveryRunnerInit();
void recoveryRunnerLoop();
