# 03. STATE MACHINE FLOW (Dual ESP32)

**Receiving Route:** ESP32-Master receives outboundRoute and returnRoute from MQTT, stores them, and sends outboundRoute to ESP32-Motion. ESP32-Motion waits for START command from the button.

**Outbound Run:** Button press → ESP32-Motion runs the route. When a checkpoint is scanned: must brake → execute action (turn left/right/go straight) → continue.

**Destination:** Scanning the last checkpoint of Outbound → Buzzer sounds, OLED shows "ARRIVED" at RO or RM → ESP32-Motion automatically rotates 180° in place to prepare the front of the vehicle.

**Return Run:** Wait for user button confirmation → ESP32-Master sends returnRoute to ESP32-Motion → ESP32-Motion runs step by step back to MED.

**Mission Done:** Reaching MED (last checkpoint of Return) → ESP32-Motion automatically rotates 180° to be ready for the next mission.

**ToF Handling:** Obstacle detected → temporary brake. Obstacle cleared → automatically continue.

**Mismatch / Cancel:** If Cancel, ESP32-Master reports the new position to the Web to request a route back. If wrong checkpoint scanned (Mismatch), ESP32-Motion must rotate 180° on the spot, then ESP32-Master sends a request to get a new route back to MED from the Web.