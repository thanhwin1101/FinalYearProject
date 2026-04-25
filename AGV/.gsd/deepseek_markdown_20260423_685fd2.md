# 04. FOLLOW MODE FLOW (Dual ESP32)

## 1) Activating FOLLOW Mode (Entry Point)

User: Long press button (> 1.5s).

**ESP32-Master (`button_handler -> main.cpp`):**

- Check conditions: Robot must be in `MODE_AUTO`, state `AUTO_IDLE`, and currently at station `MED`. (If not satisfied → beep reject).
- Set `g_mode = MODE_FOLLOW`.
- Call `followModeInit()`.

**ESP32-Master (`followModeInit`):**

- Close Relay 1 (Power HuskyLens + Servo), Open Relay 2 (Turn off Line Sensor + NFC to save power or avoid interference).
- Send `CMD_SET_MODE` (`0x01`, value: `1`) to ESP32-Motion over UART.
- Set flag to update OLED screen to Follow interface.

## 2) Continuous Execution Loop (Main Loop)

After entering the mode, the two chips work completely independently and in parallel.

### ESP32-Master Branch (Supervision & UI) - `follow_mode.cpp`

ESP32-Master does **not** calculate velocities. It only does three main tasks each loop:

- **Receive Telemetry:** Receive `CMD_HUSKY_STATUS` packet from ESP32-Motion to update center coordinates `X`, `Y` and tag area on OLED (`oledFollowMode`).
- **Battery Watchdog:** If battery `<= 30%`, ESP32-Master starts a 10‑second countdown. Beep warning.
- **Tag Watchdog:** If `CMD_TAG_LOST` is received from ESP32-Motion, ESP32-Master starts a 30‑second countdown shown on OLED. If `CMD_TAG_FOUND` is received, it cancels the countdown.

### ESP32-Motion Branch (Motion Control) - `follow_runner.cpp`

ESP32-Motion has full control over how the robot moves.

- **Read Camera:** Call `husky_tracker_poll()` each loop. Every 80ms send coordinate status to ESP32-Master.
- **Handle Lost Tag:** If camera does not see the tag for more than 1 second → Send `CMD_TAG_LOST` to ESP32-Master and brake (`Motor Stop`).
- **Kinematics (Following Law):** If tag is visible, calculate:
  - **Rotation (`Vr`):** Use `X_Center` of the tag relative to screen center (`160`). If tag is left → compute left turn speed; tag right → right turn. (Deadband of about 12 pixels to avoid wobble when tag is centered).
  - **Forward/Backward (`Vy`):** Compare current tag area with target area (about 20% of frame).
    - Tag too small (far away) → positive `Vy` → move forward.
    - Tag too large (too close) → negative `Vy` → move back to keep safe distance.
- **Send to Motors:** Call `motor_drive(vy, 0, vr)`. Thanks to tank steering, this translates into differential left/right speeds smoothly.

## 3) Exiting FOLLOW Mode (Exit/Recovery)

Follow mode does not run forever. It will be interrupted and forced to switch to `MODE_AUTO` (specifically the Recovery flow – find the line back to station) in three cases:

- **Case 1 (User active):** User long‑presses the button again.
- **Case 2 (Tag loss timeout):** When ESP32-Motion reports tag lost, the 30‑second counter on ESP32-Master expires (meaning the user has walked away too long).
- **Case 3 (Battery timeout):** Battery drops below 30%, robot beeps for 10 seconds but no one intervenes.

**Actions on exit:**

- ESP32-Master sets `g_mode = MODE_AUTO` and raises flag `g_autoRecoveryReq = true`.
- ESP32-Master calls `autoModeEnterRecoveryTransition()`.
- ESP32-Master turns Relay 2 back on (Line + NFC) but keeps Relay 1 (HuskyLens).
- ESP32-Master sends Cancel command to ESP32-Motion. ESP32-Motion switches to `AUTO_BLIND_FOLLOW` state. The robot starts moving back / rotating to find the line (aided by HuskyLens line tracking), read the nearest tag, and request a route back to station `MED`.