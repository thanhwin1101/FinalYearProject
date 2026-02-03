/*
 * ============================================================
 * BIPED USER MANAGER - WIRING DIAGRAM
 * ============================================================
 * Hướng dẫn kết nối phần cứng cho ESP32 User Manager
 * ============================================================
 * 
 * ESP32 DevKit V1 (38-pin) được sử dụng
 * 
 * ============================================================
 * 1. RFID RC522 (SPI)
 * ============================================================
 * 
 *   RC522 Pin     ESP32 Pin     Màu dây (gợi ý)
 *   ---------     ---------     ---------------
 *   SDA/SS        GPIO 5        Cam
 *   SCK           GPIO 18       Vàng
 *   MOSI          GPIO 23       Xanh lá
 *   MISO          GPIO 19       Xanh dương
 *   IRQ           (không dùng)  
 *   GND           GND           Đen
 *   RST           GPIO 4        Trắng
 *   3.3V          3.3V          Đỏ
 * 
 * ============================================================
 * 2. OLED SSD1306 0.96" (I2C)
 * ============================================================
 * 
 *   OLED Pin      ESP32 Pin     Màu dây
 *   --------      ---------     --------
 *   VCC           3.3V          Đỏ
 *   GND           GND           Đen
 *   SCL           GPIO 22       Vàng
 *   SDA           GPIO 21       Xanh dương
 * 
 * ============================================================
 * 3. CONTROL BUTTONS (5 buttons)
 * ============================================================
 * 
 * Tất cả buttons sử dụng INPUT_PULLUP, nối giữa GPIO và GND
 * 
 *   Button        ESP32 Pin     Chức năng
 *   ------        ---------     ---------
 *   FORWARD       GPIO 32       Đi tới
 *   BACKWARD      GPIO 33       Đi lùi
 *   LEFT          GPIO 25       Rẽ trái
 *   RIGHT         GPIO 26       Rẽ phải
 *   STOP          GPIO 27       Dừng / Kết thúc session (giữ 2s)
 * 
 *   Sơ đồ nối button:
 *   
 *   GPIO Pin ----[Button]---- GND
 * 
 * ============================================================
 * 4. ROTARY ENCODER (KY-040)
 * ============================================================
 * 
 *   Encoder Pin   ESP32 Pin     Chức năng
 *   -----------   ---------     ---------
 *   CLK (A)       GPIO 34       Clock signal
 *   DT (B)        GPIO 35       Direction signal
 *   SW            GPIO 39       Button (xác nhận)
 *   +             3.3V          Power
 *   GND           GND           Ground
 * 
 *   Lưu ý: GPIO 34, 35, 39 là input-only pins
 * 
 * ============================================================
 * 5. UART TO WALKING CONTROLLER
 * ============================================================
 * 
 *   User Manager    Walking Controller
 *   ------------    ------------------
 *   GPIO 17 (TX) -> GPIO 16 (RX)
 *   GPIO 16 (RX) <- GPIO 17 (TX)
 *   GND          -- GND
 * 
 *   QUAN TRỌNG: Kết nối GND chung giữa 2 ESP32!
 * 
 * ============================================================
 * 6. STATUS LED
 * ============================================================
 * 
 *   LED (Built-in): GPIO 2 (LED trên board ESP32)
 *   
 *   Hoặc LED ngoài:
 *   GPIO 2 ----[220Ω]----[LED]---- GND
 * 
 * ============================================================
 * 7. BUZZER (Tùy chọn)
 * ============================================================
 * 
 *   Buzzer Pin    ESP32 Pin
 *   ----------    ---------
 *   +             GPIO 12
 *   -             GND
 * 
 * ============================================================
 * SƠ ĐỒ TỔNG QUAN
 * ============================================================
 * 
 *                    +---------------------+
 *                    |     ESP32 DevKit    |
 *                    |                     |
 *   [RFID RC522] --> |  SPI (5,18,23,19)   |
 *                    |  RST (4)            |
 *                    |                     |
 *   [OLED 0.96"] --> |  I2C (21,22)        |
 *                    |                     |
 *   [5 Buttons]  --> |  GPIO 32,33,25,26,27|
 *                    |                     |
 *   [Encoder]    --> |  GPIO 34,35,39      |
 *                    |                     |
 *   [UART]       --> |  GPIO 16,17         | <--> [Walking ESP32]
 *                    |                     |
 *   [Status LED] <-- |  GPIO 2             |
 *                    |                     |
 *   [Buzzer]     <-- |  GPIO 12            |
 *                    +---------------------+
 * 
 * ============================================================
 * NGUỒN ĐIỆN
 * ============================================================
 * 
 * - ESP32 User Manager: Cấp nguồn qua USB hoặc VIN (5V)
 * - RFID RC522: Sử dụng 3.3V từ ESP32 (KHÔNG dùng 5V!)
 * - OLED: Sử dụng 3.3V từ ESP32
 * - Buttons/Encoder: Không cần nguồn riêng (INPUT_PULLUP)
 * 
 * ============================================================
 * LƯU Ý QUAN TRỌNG
 * ============================================================
 * 
 * 1. RFID RC522 chỉ hoạt động ở 3.3V, không nối vào 5V!
 * 
 * 2. Luôn nối GND chung giữa tất cả các module và ESP32
 * 
 * 3. GPIO 34, 35, 36, 39 chỉ là INPUT, không có pullup/pulldown
 *    nội. Nếu dùng encoder với các pin này, cần thêm điện trở 
 *    pullup bên ngoài (10kΩ)
 * 
 * 4. Khi nạp code, ngắt kết nối UART với Walking Controller
 *    để tránh xung đột
 * 
 * 5. Đảm bảo 2 ESP32 có GND chung khi giao tiếp UART
 * 
 * ============================================================
 * THƯ VIỆN CẦN CÀI (Arduino IDE / PlatformIO)
 * ============================================================
 * 
 * - MFRC522 (by GithubCommunity) - cho RFID
 * - U8g2 (by olikraus) - cho OLED
 * - ArduinoJson (by Benoit Blanchon) - cho JSON API
 * - Preferences (built-in) - cho NVS storage
 * 
 * PlatformIO (platformio.ini):
 * 
 *   [env:esp32dev]
 *   platform = espressif32
 *   board = esp32dev
 *   framework = arduino
 *   lib_deps = 
 *       miguelbalboa/MFRC522@^1.4.10
 *       olikraus/U8g2@^2.34.22
 *       bblanchon/ArduinoJson@^6.21.3
 *   monitor_speed = 115200
 * 
 * ============================================================
 */
