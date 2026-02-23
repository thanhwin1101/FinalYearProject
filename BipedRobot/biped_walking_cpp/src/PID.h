#pragma once

// ============================================================
// PID Controller — Derivative-on-measurement to avoid kick
// (Defined but not actively used; main loop uses inline PD)
// ============================================================
class PID {
public:
  PID() : kp(0), ki(0), kd(0),
          out_min(-255), out_max(255),
          last_input(0), integral(0) {}

  void setTunings(float Kp, float Ki, float Kd) {
    kp = Kp; ki = Ki; kd = Kd;
  }

  void setOutputLimits(float Min, float Max) {
    out_min = Min; out_max = Max;
  }

  void reset() {
    integral = 0;
    last_input = 0;
  }

  float compute(float setpoint, float input, float dt) {
    if (dt == 0) return 0;

    float error = setpoint - input;
    float P_out = kp * error;

    integral += ki * error * dt;
    integral = constrain(integral, out_min, out_max);
    float I_out = integral;

    float derivative = (input - last_input) / dt;
    float D_out = -kd * derivative;

    float output = P_out + I_out + D_out;
    output = constrain(output, out_min, out_max);

    last_input = input;
    return output;
  }

private:
  float kp, ki, kd;
  float out_min, out_max;
  float last_input, integral;
};
