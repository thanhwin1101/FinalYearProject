# 01. ARCHITECTURE & PROTOCOL (Dual ESP32)

- **Communication:** UART between ESP32-Master and ESP32-Motion with frame `[STX 0x7E] [LEN] [CMD] [DATA...] [CRC8]`.
- **Command flow:**
  - `CMD_SET_MODE` (0x01): Change mode. When receiving this command, ESP32-Motion must wait exactly 2 seconds (to let relays power up sensors) before calling `init()` for HuskyLens and PN532.
  - `CMD_SEND_ROUTE` (0x02): Transmit checkpoint array.
  - `CMD_START_MISSION` (0x0B): Activate autonomous run.
- **Relay Control:** ESP32-Master controls sensor power via 2 relays (R1: HuskyLens, R2: NFC + Line sensors).