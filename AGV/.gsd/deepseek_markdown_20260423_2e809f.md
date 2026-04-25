# CURRENT TASK: Rewrite AUTO Mode (ESP32-Motion)

- **Goal:** Completely rebuild the AUTO mode logic inside `auto_runner.cpp` as a fully non‑blocking state machine.
- **Status:** In progress.
- **Detailed requirements:** 
  1. Remove `MODE_RECOVERY`, incorporate it as a sub‑state `AUTO_BLIND_FOLLOW`.
  2. Apply Tank Steering for line following PID.
  3. Implement 3 phases (IDLE/SPIN/BRAKE) for the turning routine so that UART is never blocked.