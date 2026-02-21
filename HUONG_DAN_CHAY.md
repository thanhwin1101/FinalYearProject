# HƯỚNG DẪN CHẠY TOÀN BỘ HỆ THỐNG HOSPITAL ROBOT

## Tổng quan hệ thống

```
┌─────────────────────────────────────────────────────────────────┐
│                    KIẾN TRÚC HỆ THỐNG                          │
│                                                                 │
│  [Frontend:5173] ←→ [Backend:3000] ←→ [MongoDB:27017]          │
│                          ↕ MQTT                                 │
│                    [Mosquitto:1883]                              │
│                    ↕              ↕                              │
│            [Carry Robot]    [Biped Robot]                        │
│            (ESP32 + CAM)   (Walking + User Manager)             │
└─────────────────────────────────────────────────────────────────┘
```

### Các thành phần

| # | Thành phần | Mô tả | Port/Giao thức |
|---|-----------|--------|----------------|
| 1 | **MongoDB** | Cơ sở dữ liệu | `27017` (TCP) |
| 2 | **Mosquitto MQTT** | Message broker cho robot | `1883` (MQTT) |
| 3 | **Backend API** | Express.js REST API | `3000` (HTTP) |
| 4 | **Frontend** | React + Vite Dashboard | `5173` (HTTP) |
| 5 | **Carry Robot** | ESP32 robot vận chuyển | WiFi + MQTT |
| 6 | **Carry Robot CAM** | ESP32-CAM theo dõi AprilTag | ESP-NOW |
| 7 | **Biped Robot** | ESP32 robot đi bộ phục hồi | UART |
| 8 | **Biped User Manager** | ESP32 quản lý RFID/OLED | WiFi HTTP |

---

## YÊU CẦU HỆ THỐNG

### Phần mềm cần cài đặt

| Phần mềm | Link tải | Ghi chú |
|-----------|----------|---------|
| **Node.js** v18+ | https://nodejs.org | Backend + Frontend |
| **MongoDB** v7+ | https://mongodb.com/try/download | Database |
| **Mosquitto** | https://mosquitto.org/download | MQTT Broker |
| **PlatformIO** | Extension trong VS Code | Flash ESP32 |
| **Arduino IDE** (tùy chọn) | https://arduino.cc | Biped Robot |
| **VS Code** | https://code.visualstudio.com | IDE |

### Tài khoản & mật khẩu mặc định

| Mục | Giá trị |
|-----|---------|
| MQTT Robot User | `hospital_robot` / `123456` |
| MQTT Backend User | `hospital_backend` / `123456` |
| Carry Robot ID | `CARRY-01` |
| Biped Robot ID | `BIPED-001` |
| MQTT Server (cho ESP32) | `192.168.0.102:1883` (IP máy bạn) |
| WiFi mặc định (Biped) | `Hospital_WiFi` / `hospital123` |
| API mặc định (Biped) | `http://192.168.1.100:3000/api` |

---

## BƯỚC 1: KHỞI ĐỘNG MONGODB

### Kiểm tra MongoDB đang chạy
```powershell
# Kiểm tra service
Get-Service MongoDB* | Select-Object Name, Status

# Nếu chưa chạy:
Start-Service MongoDB
```

Hoặc chạy thủ công:
```powershell
mongod --dbpath "C:\data\db"
```

### Seed dữ liệu bản đồ (chạy 1 lần đầu)
```powershell
cd "Hospital Dashboard\Backend"
npm install          # Cài dependencies (lần đầu)
node seed/seedMap.js # Tạo bản đồ 37 checkpoint NFC
```

> **Output mong đợi:** `Seeded floor1 map with 37 nodes`

---

## BƯỚC 2: KHỞI ĐỘNG MQTT BROKER (MOSQUITTO)

### Cách 1: Chạy như Windows Service (khuyến nghị)
```powershell
# Mở PowerShell với quyền Admin
Start-Service mosquitto

# Kiểm tra
Get-Service mosquitto
# Status phải là "Running"
```

### Cách 2: Chạy trực tiếp (nếu service lỗi)
```powershell
& "C:\Program Files\mosquitto\mosquitto.exe" -c "C:\Program Files\mosquitto\mosquitto.conf" -v
```
> ⚠️ Giữ terminal này mở, không đóng.

### Kiểm tra MQTT hoạt động
```powershell
# Kiểm tra port 1883 đang lắng nghe
Get-NetTCPConnection -LocalPort 1883

# Test nhanh (mở 2 terminal)
# Terminal 1 - Subscribe:
& "C:\Program Files\mosquitto\mosquitto_sub.exe" -h localhost -p 1883 -u hospital_robot -P 123456 -t "hospital/#" -v

# Terminal 2 - Publish:
& "C:\Program Files\mosquitto\mosquitto_pub.exe" -h localhost -p 1883 -u hospital_robot -P 123456 -t "hospital/test" -m "Hello"
```

### Cấu hình Mosquitto (nếu chưa có)
File: `C:\Program Files\mosquitto\mosquitto.conf`
```conf
listener 1883
allow_anonymous false
password_file C:/Program Files/mosquitto/passwordfile
```

Tạo password file:
```powershell
cd "C:\Program Files\mosquitto"
.\mosquitto_passwd -c passwordfile hospital_robot    # Nhập: 123456
.\mosquitto_passwd passwordfile hospital_backend     # Nhập: 123456
```

### Mở Firewall (nếu ESP32 không kết nối được)
```powershell
# PowerShell Admin
New-NetFirewallRule -DisplayName "Mosquitto MQTT" -Direction Inbound -LocalPort 1883 -Protocol TCP -Action Allow
```

---

## BƯỚC 3: KHỞI ĐỘNG BACKEND API

### Cài đặt dependencies (lần đầu)
```powershell
cd "Hospital Dashboard\Backend"
npm install
```

### Cấu hình .env
File: `Hospital Dashboard/Backend/.env`
```env
MONGO_URI=mongodb://127.0.0.1:27017/hospital
PORT=3000

# MQTT Broker Configuration
MQTT_BROKER=mqtt://localhost:1883
MQTT_USER=hospital_backend
MQTT_PASS=123456
```

### Chạy Backend
```powershell
cd "Hospital Dashboard\Backend"
npm run dev
```

> **Output mong đợi:**
> ```
> Server running on 3000
> [MQTT] Connecting to mqtt://localhost:1883...
> [MQTT] Connected to broker
> [MQTT] Subscribed to hospital/robots/+/telemetry
> [MQTT] Subscribed to hospital/robots/+/mission/progress
> ...
> ```

### API Endpoints chính
| Method | Endpoint | Mô tả |
|--------|----------|--------|
| GET | `/api/patients` | Danh sách bệnh nhân |
| GET | `/api/patients/by-card/:cardNumber` | Tra cứu bệnh nhân qua RFID |
| GET | `/api/robots` | Danh sách robot |
| PUT | `/api/robots/:id/telemetry` | Cập nhật telemetry |
| GET | `/api/missions` | Danh sách nhiệm vụ |
| POST | `/api/missions` | Tạo nhiệm vụ mới |
| GET | `/api/maps` | Bản đồ checkpoint |
| POST | `/api/biped/session/start` | Bắt đầu phiên tập |
| PUT | `/api/biped/session/:id/step` | Cập nhật bước chân |
| POST | `/api/biped/session/:id/end` | Kết thúc phiên tập |

---

## BƯỚC 4: KHỞI ĐỘNG FRONTEND

### Cài đặt dependencies (lần đầu)
```powershell
cd "Hospital Dashboard\Frontend"
npm install
```

### Chạy Frontend
```powershell
cd "Hospital Dashboard\Frontend"
npm run dev
```

> **Output mong đợi:**
> ```
>   VITE v6.3.5  ready in 500ms
>   ➜  Local:   http://localhost:5173/
> ```

### Hoặc dùng script tự động (chạy cả Backend + Frontend)
```powershell
cd "Hospital Dashboard"
.\start-servers.bat
```

### Truy cập Dashboard
Mở trình duyệt: **http://localhost:5173**

> Frontend tự động proxy `/api/*` → `http://localhost:3000`

---

## BƯỚC 5: NẠP FIRMWARE CHO CARRY ROBOT (ESP32)

### Yêu cầu phần cứng
- ESP32 DevKit 38-pin
- Mecanum wheels + L298N motor driver
- PN532 NFC reader (SPI)
- OLED SH1106 (I2C)
- VL53L0X distance sensor (I2C)
- 2x SRF05 Ultrasonic
- Công tắc hàng hóa (GPIO 15)

### Cấu hình trước khi flash

Sửa file `CarryRobot/carry_cpp/src/config.h`:
```cpp
// Thay IP máy chạy Mosquitto của bạn
#define MQTT_DEFAULT_SERVER "192.168.0.102"  // ← SỬA IP NÀY
#define MQTT_DEFAULT_PORT   1883
#define MQTT_DEFAULT_USER   "hospital_robot"
#define MQTT_DEFAULT_PASS   "123456"
```

> **Tìm IP máy bạn:** chạy `ipconfig` trong PowerShell, lấy IPv4 của WiFi adapter.

### Flash firmware
```powershell
cd CarryRobot\carry_cpp
pio run -t upload        # Build + Flash
pio device monitor       # Serial monitor (115200 baud)
```

> ⚠️ **Quan trọng:** Rút dây TRIG_LEFT (GPIO 1) và TRIG_RIGHT (GPIO 3) khi flash vi chúng trùng TX/RX.

### Kết nối WiFi lần đầu
1. ESP32 khởi động → Tạo WiFi hotspot: `CarryRobot-Setup`
2. Kết nối hotspot bằng điện thoại/laptop
3. Truy cập `192.168.4.1` → Nhập WiFi SSID + Password
4. ESP32 kết nối WiFi → Kết nối MQTT

### Kiểm tra Carry Robot hoạt động
- OLED hiển thị trạng thái `IDLE`
- MQTT nhận telemetry: topic `hospital/robots/CARRY-01/telemetry`
- Dashboard hiển thị robot `CARRY-01` với status `idle`

---

## BƯỚC 6: NẠP FIRMWARE CHO CARRY ROBOT CAM (ESP32-CAM)

### Yêu cầu
- Module ESP32-CAM (AI-Thinker)

### Flash firmware
```powershell
cd Carry_robot_CAM
pio run -t upload
pio device monitor
```

> ESP32-CAM giao tiếp với Carry Robot qua **ESP-NOW** (không cần WiFi).  
> Dùng cho chế độ **Follow Mode** (theo dõi AprilTag).

---

## BƯỚC 7: NẠP FIRMWARE CHO BIPED ROBOT

### 7A. Walking Controller (BipedRobot.ino)

#### Yêu cầu phần cứng
- ESP32 + PCA9685 I2C servo driver
- 10 servo SG90/MG90s
- MPU6050 IMU
- 8x FSR402 cảm biến áp lực chân

#### Flash bằng Arduino IDE
1. Mở `BipedRobot/BipedRobot.ino`
2. Board: **ESP32 Dev Module**
3. Cài thư viện: `Adafruit PWM Servo Driver`, `MPU6050`
4. Upload

#### Flash bằng PlatformIO
```powershell
cd BipedRobot
pio run -t upload
```

### 7B. User Manager (BipedUserManager)

#### Yêu cầu phần cứng
- ESP32 riêng biệt
- RFID RC522 (SPI)
- OLED SSD1306 0.96" (I2C)
- 4 nút bấm + 1 rotary encoder

#### Cấu hình trước khi flash

Sửa file `BipedUserManager/config.h`:
```cpp
// WiFi mặc định
static const char* DEFAULT_WIFI_SSID = "Hospital_WiFi";  // ← SỬA
static const char* DEFAULT_WIFI_PASS = "hospital123";     // ← SỬA

// API Backend
static const char* DEFAULT_API_BASE = "http://192.168.1.100:3000/api"; // ← SỬA IP
```

#### Flash
```powershell
cd BipedUserManager
pio run -t upload
pio device monitor  # 115200 baud
```

#### Kết nối WiFi lần đầu
1. Giữ nút **FORWARD** 3 giây → Config portal
2. Kết nối hotspot `BipedRobot-Setup` (password: `biped123`)
3. Truy cập `192.168.4.1` → Nhập WiFi + API URL
4. ESp32 kết nối WiFi → Kết nối Dashboard API

#### UART giữa 2 ESP32 Biped

| Pin | Walking Controller | User Manager |
|-----|-------------------|--------------|
| TX  | GPIO 17           | GPIO 17      |
| RX  | GPIO 16           | GPIO 16      |

> Nối **chéo**: TX(Walking) → RX(User), TX(User) → RX(Walking)
> Baud rate: **115200**

---

## THỨ TỰ KHỞI ĐỘNG (CHECKLIST)

```
✅ Bước 1: MongoDB          → mongod hoặc MongoDB service
✅ Bước 2: Mosquitto MQTT   → Port 1883 listening
✅ Bước 3: Backend API      → http://localhost:3000 
✅ Bước 4: Frontend         → http://localhost:5173
✅ Bước 5: Carry Robot      → ESP32 flash + WiFi config
✅ Bước 6: Carry Robot CAM  → ESP32-CAM flash
✅ Bước 7: Biped Robot      → 2 ESP32 flash + WiFi config
```

### Khởi động nhanh (Dashboard only)
```powershell
# Terminal 1: MongoDB (nếu chưa chạy service)
mongod

# Terminal 2: MQTT
& "C:\Program Files\mosquitto\mosquitto.exe" -c "C:\Program Files\mosquitto\mosquitto.conf" -v

# Terminal 3: Backend  
cd "Hospital Dashboard\Backend"
npm run dev

# Terminal 4: Frontend
cd "Hospital Dashboard\Frontend"
npm run dev
```

Hoặc 1 lệnh:
```powershell
cd "Hospital Dashboard"
.\start-servers.bat
```
> ⚠️ `start-servers.bat` chỉ chạy Backend + Frontend. MongoDB và MQTT phải chạy trước.

---

## LUỒNG HOẠT ĐỘNG CHÍNH

### 1. Giao thuốc (Carry Robot)
```
Dashboard tạo nhiệm vụ → MQTT assign → Carry Robot nhận
→ Robot đọc NFC di chuyển → MQTT progress → Dashboard cập nhật
→ Đến giường bệnh → MQTT complete → Dashboard ghi nhận
→ Robot quay về → MQTT returned → Dashboard kết thúc
```

### 2. Phục hồi chức năng (Biped Robot)
```
Bệnh nhân quét RFID → User Manager tra API → Bắt đầu phiên tập
→ Bệnh nhân nhấn nút điều khiển → UART gửi lệnh Walking Controller
→ Walking Controller đếm bước → UART gửi lại User Manager
→ User Manager gửi HTTP cập nhật Dashboard → OLED hiển thị
→ Quét RFID lại → Kết thúc phiên → Dashboard ghi nhận
```

### 3. Vận chuyển Biped (Biped + Carry)
```
Biped quét RFID checkpoint → Dashboard biết vị trí
→ Dashboard ra lệnh Carry Robot đến checkpoint
→ Carry Robot đến → Carry Biped Robot về
```

---

## XỬ LÝ SỰ CỐ

### MQTT không kết nối
```powershell
# Kiểm tra Mosquitto đang chạy
Get-Process mosquitto
Get-NetTCPConnection -LocalPort 1883

# Kiểm tra log
Get-Content "C:\Program Files\mosquitto\mosquitto.log" -Tail 20

# Restart
Restart-Service mosquitto
```

### ESP32 không kết nối WiFi
1. Giữ nút config → Mở WiFi portal
2. Kiểm tra SSID/password đúng
3. Đảm bảo router 2.4GHz (ESP32 không hỗ trợ 5GHz)

### ESP32 không kết nối MQTT
1. Kiểm tra IP Mosquitto server đúng chưa (chạy `ipconfig`)
2. Kiểm tra firewall mở port 1883
3. Kiểm tra username/password MQTT
4. ESP32 và máy tính phải cùng mạng WiFi

### Backend lỗi kết nối MongoDB
```powershell
# Kiểm tra MongoDB
Get-Service MongoDB*
# Hoặc
mongosh --eval "db.runCommand('ping')"
```

### Frontend không load dữ liệu
1. Kiểm tra Backend đang chạy (http://localhost:3000)
2. Vite proxy cấu hình tự động `/api` → `localhost:3000`
3. Mở DevTools (F12) → Network tab → Kiểm tra lỗi API

### Carry Robot không flash được
- **Rút dây GPIO 1 (TX) và GPIO 3 (RX)** trước khi flash
- Giữ nút BOOT trên ESP32 khi bắt đầu upload
- Kiểm tra đúng COM port trong PlatformIO

---

## CẤU TRÚC MQTT TOPICS

```
hospital/
├── robots/
│   └── {robotId}/
│       ├── telemetry              # Robot → Backend (mỗi 2s)
│       ├── command                # Backend → Robot (điều khiển)
│       ├── mission/
│       │   ├── assign             # Backend → Robot (giao nhiệm vụ)
│       │   ├── progress           # Robot → Backend (tiến độ)
│       │   ├── complete           # Robot → Backend (hoàn thành outbound)
│       │   ├── returned           # Robot → Backend (đã quay về)
│       │   ├── cancel             # Backend → Robot (hủy nhiệm vụ)
│       │   └── return_route       # Backend → Robot (tuyến đường về)
│       └── position/
│           └── waiting_return     # Robot → Backend (báo vị trí khi cancel)
```

---

## GHI CHÚ QUAN TRỌNG

1. **Thứ tự bật:** MongoDB → Mosquitto → Backend → Frontend → Robots
2. **Cùng mạng WiFi:** Tất cả ESP32 và máy tính phải cùng mạng
3. **IP tĩnh:** Nên đặt IP tĩnh cho máy chạy Mosquitto/Backend
4. **GPIO 1/3:** Không dùng cho peripheral khi debug qua Serial
5. **Gyro calibration:** Robot phải đứng yên 3 giây khi khởi động
6. **NFC UIDs:** Đã hard-code trong `config.h`, phải khớp với thẻ NFC thực tế
7. **Motor gain:** Chỉnh `leftGain`/`rightGain` trong `config.h` nếu robot chạy lệch
