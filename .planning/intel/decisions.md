# Decisions (ADRs)

No ADRs were present in the ingested document set.

All locked architectural decisions would be recorded here. Downstream consumers
(roadmapper, planner) should treat the absence of locked decisions as "design
is still open" — constraints from SPECs and requirements from the PRD still apply,
but no decision has been formally locked against future override.

## Suggested candidates for future ADRs

The following technical decisions are implied by the ingested SPEC/PRD/DOC set
but have not been explicitly locked. Consider promoting any of these to an ADR
if they should be immune to silent change:

- **Two-MCU architecture** — ESP32 master (WiFi/MQTT/UX) + STM32 slave (real-time motion, sensors on the motion hot-path)
  - source: AGV/docs/agent_skill.txt
  - source: AGV/docs/Checklist.txt

- **UART transport with 0x7E framing + CRC8** as the ESP32↔STM32 contract
  - source: AGV/docs/agent_skill.txt
  - source: AGV/docs/Checklist.txt (1.4, 2.2)

- **Three-relay power-domain split** (R1 Vision, R2 Line, R3 NFC) to manage sensor contention / cross-talk and save battery
  - source: AGV/docs/agent_skill.txt (relay table)
  - source: AGV/docs/Auto_mode.txt
  - source: AGV/docs/Follow_mode.txt

- **Mecanum drive via 2× L298N** (4 motors, 8 direction pins + PWM on TIM1/TIM2/TIM3)
  - source: AGV/docs/Checklist.txt (2.3)

- **WiFiManager captive portal "Robot_Setup" at 192.168.4.1** as the provisioning mechanism
  - source: AGV/docs/setup_wifi_MQTT.txt
  - source: AGV/docs/Checklist.txt (1.2)

- **Preferences (NVS) as credential store** (WiFi + MQTT), restart-on-save
  - source: AGV/docs/setup_wifi_MQTT.txt

- **OLED via U8g2 on SH1106** per-mode screens
  - source: AGV/docs/agent_skill.txt
  - source: AGV/docs/Checklist.txt (1.6)

- **Huskylens over UART** (not I2C) for tag + line detection
  - source: AGV/docs/agent_skill.txt
  - source: AGV/docs/Checklist.txt (1.14)

- **PN532 over SPI** (not I2C) for NFC checkpoint reading
  - source: AGV/docs/agent_skill.txt
  - source: AGV/docs/Checklist.txt (2.5)

- **Battery ≥30% hard gate** before executing any motion command
  - source: AGV/docs/agent_skill.txt ("Pin phải ≥30% mới cho phép thực thi lệnh")
  - source: AGV/docs/Checklist.txt (1.13, 4.7)

These are candidates only. The roadmapper may route them to discuss/ADR
creation if they warrant locking.
