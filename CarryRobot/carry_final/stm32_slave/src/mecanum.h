#pragma once
#include <Arduino.h>

// Mecanum kinematics: Vx (strafe), Vy (forward), Vr (rotation)
// All values scaled -255 … +255
void mecanumDrive(int vx, int vy, int vr);

// fixed manoeuvres
void mecanumTurnLeft90();
void mecanumTurnRight90();
void mecanumTurn180();
