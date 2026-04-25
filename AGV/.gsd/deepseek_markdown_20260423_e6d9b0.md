# 02. HARDWARE OVERVIEW (Dual ESP32)

- **Driving mechanism:** Tank Steering (Differential Drive). `left = vy + vr`, `right = vy - vr`. No Mecanum (ignore vx axis).
- **Line Sensor:** 3 eyes (Left, Center, Right). Active LOW.
- **RFID (PN532):** SPI interface. Debounce mechanism: Store card ID in `s_currentTag`. Report tag only once. Must have 3 consecutive cycles without tag (lift-off streak) before resetting `s_currentTag`.
- **ToF (VL53L0X):** Used for obstacle detection.
- **Processor split:**
  - **ESP32-Master:** MQTT, OLED, button, relay control, high‑level state machine, mode switching.
  - **ESP32-Motion:** Motor control, HuskyLens, PN532, line sensors, ToF, low‑level motion logic.