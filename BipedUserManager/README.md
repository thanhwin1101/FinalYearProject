# Biped User Manager ESP32

Firmware cho ESP32 quản lý người dùng của robot biped, kết nối UART với Walking Controller và gửi dữ liệu lên Dashboard.

## Tính năng

- **RFID RC522**: Xác thực người dùng + quét checkpoint vị trí
- **OLED 0.96"**: Hiển thị thông tin (user, số bước, trạng thái)  
- **4 Buttons**: Forward, Backward, Turn Left, Turn Right
- **Rotary Encoder**: Điều chỉnh tốc độ di chuyển
- **WiFi HTTP**: Gửi dữ liệu về Dashboard API
- **UART**: Giao tiếp với Walking Controller ESP32

## Kiến trúc hệ thống

```
┌─────────────────────┐      UART      ┌─────────────────────┐
│  User Manager ESP32 │◄──────────────►│ Walking Controller  │
│                     │                │      ESP32          │
│ - RFID Reader       │                │ - Servo Control     │
│ - OLED Display      │                │ - IMU Balance       │
│ - Buttons/Encoder   │                │ - Gait Patterns     │
│ - WiFi to Dashboard │                │                     │
└─────────────────────┘                └─────────────────────┘
           │
           │ HTTP/REST
           ▼
    ┌─────────────────┐
    │  Dashboard API  │
    │   (Backend)     │
    └─────────────────┘
```

## Files

| File | Mô tả |
|------|-------|
| `BipedUserManager.ino` | Firmware chính |
| `config.h` | Cấu hình pin và thông số |
| `WIRING.h` | Hướng dẫn kết nối phần cứng |

## Giao thức UART

### Gửi đến Walking Controller
```
CMD:FWD\n      - Di chuyển tiến
CMD:BACK\n     - Di chuyển lùi
CMD:LEFT\n     - Rẽ trái
CMD:RIGHT\n    - Rẽ phải
STOP\n         - Dừng di chuyển
SPEED:50\n     - Đặt tốc độ (0-100)
BALANCE:ON\n   - Bật cân bằng
BALANCE:OFF\n  - Tắt cân bằng
CALIBRATE\n    - Hiệu chuẩn IMU
STATUS\n       - Yêu cầu trạng thái
```

### Nhận từ Walking Controller
```
STEP:123\n        - Số bước hiện tại
BALANCE:OK\n      - Trạng thái cân bằng tốt
BALANCE:WARN\n    - Cảnh báo cân bằng
BALANCE:ERROR\n   - Lỗi cân bằng
ACK:FWD\n         - Xác nhận lệnh
STATUS:READY\n    - Hệ thống sẵn sàng
ERROR:xxx\n       - Thông báo lỗi
```

## API Endpoints

| Endpoint | Method | Mô tả |
|----------|--------|-------|
| `/api/robots/biped/session/start` | POST | Bắt đầu phiên tập |
| `/api/robots/biped/session/:id/update` | PUT | Cập nhật số bước |
| `/api/robots/biped/session/:id/end` | POST | Kết thúc phiên tập |
| `/api/robots/:id/telemetry` | PUT | Cập nhật vị trí & trạng thái |

## Cài đặt

### Arduino IDE
1. Cài đặt board ESP32 từ Board Manager
2. Cài đặt thư viện:
   - MFRC522
   - U8g2
   - ArduinoJson
3. Mở `BipedUserManager.ino`
4. Chọn board: ESP32 Dev Module
5. Upload

### PlatformIO
```bash
cd BipedUserManager
pio run -t upload
pio device monitor
```

## Cấu hình

### WiFi & API (trong code hoặc NVS)
```cpp
const char* DEFAULT_SSID = "Hospital_WiFi";
const char* DEFAULT_PASSWORD = "hospital123";
const char* DEFAULT_API_URL = "http://192.168.1.100:3000/api";
```

### User Database (RFID)
Chỉnh sửa mảng `USER_DATABASE[]` trong `BipedUserManager.ino` để thêm/sửa thẻ RFID người dùng.

### Checkpoint Database
Chỉnh sửa mảng `CHECKPOINT_DATABASE[]` để thêm các điểm checkpoint trong bệnh viện.

## Luồng hoạt động

1. **Khởi động**: ESP32 kết nối WiFi, hiển thị màn hình chờ
2. **Quét thẻ**: Người dùng quét thẻ RFID để bắt đầu phiên tập
3. **Session Active**: 
   - Hiển thị tên người dùng, số bước
   - Nhận lệnh từ buttons để điều khiển robot
   - Gửi số bước lên Dashboard
4. **Kết thúc**: Giữ nút STOP 2 giây hoặc quét thẻ lại
5. **Quét checkpoint**: Robot quét thẻ checkpoint để báo vị trí

## Xử lý lỗi

| LED Status | Ý nghĩa |
|------------|---------|
| Tắt | Idle, chờ người dùng |
| Sáng | Session đang hoạt động |
| Nhấp nháy | Lỗi hoặc mất kết nối |

## Debug

Mở Serial Monitor (115200 baud) để xem log:
- Lệnh UART gửi/nhận
- Thông tin RFID
- Trạng thái kết nối API
