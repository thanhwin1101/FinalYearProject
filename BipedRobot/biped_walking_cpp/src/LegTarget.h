#pragma once

// ============================================================
// Leg Target — Target position for IK (prepared for gait)
// ============================================================
struct LegTarget {
  float x, y, z;          // mm (forward, up/down, lateral)
  float footPitchRad;     // rad
  float footRollRad;      // rad
};
