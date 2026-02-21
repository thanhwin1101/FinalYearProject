# Hướng dẫn cài đặt MQTT Mosquitto

## 1. Cài đặt Mosquitto trên Windows

### Cách 1: Download installer
1. Tải từ: https://mosquitto.org/download/
2. Chọn **Windows 64-bit** → Chạy installer
3. Tick chọn **Service** để Mosquitto tự chạy nền

### Cách 2: Dùng Chocolatey (nếu có)
```powershell
choco install mosquitto
```

## 2. Cấu hình xác thực (Username/Password)

### Bước 1: Tạo file password
Mở **PowerShell với quyền Admin**:
```powershell
cd "C:\Program Files\mosquitto"

# Tạo user đầu tiên (sẽ nhập password)
.\mosquitto_passwd -c passwordfile hospital_robot

# Thêm user cho backend (nếu cần)
.\mosquitto_passwd passwordfile hospital_backend
```

### Bước 2: Cấu hình mosquitto.conf
Mở file `C:\Program Files\mosquitto\mosquitto.conf` và thêm:
```conf
# Listener trên port 1883
listener 1883

# Bắt buộc xác thực
allow_anonymous false
password_file C:/Program Files/mosquitto/passwordfile

# (Tùy chọn) Log để debug
log_dest file C:/Program Files/mosquitto/mosquitto.log
log_type all
```

### Bước 3: Restart Mosquitto service
```powershell
# PowerShell Admin
Restart-Service mosquitto
```

## 3. Kiểm tra hoạt động

### Test với mosquitto_sub/pub
Mở 2 terminal:

**Terminal 1 - Subscribe:**
```powershell
cd "C:\Program Files\mosquitto"
.\mosquitto_sub -h localhost -p 1883 -u hospital_robot -P <password> -t "hospital/#" -v
```

**Terminal 2 - Publish:**
```powershell
cd "C:\Program Files\mosquitto"
.\mosquitto_pub -h localhost -p 1883 -u hospital_robot -P <password> -t "hospital/test" -m "Hello MQTT"
```

## 4. Cấu trúc MQTT Topics cho Hospital System

### Robot Telemetry (Robot → Backend)
- `hospital/robots/{robotId}/telemetry` - Robot gửi trạng thái định kỳ

### Mission Management
- `hospital/robots/{robotId}/mission/assign` - Backend gửi nhiệm vụ mới (robot subscribe)
- `hospital/robots/{robotId}/mission/progress` - Robot báo tiến độ
- `hospital/robots/{robotId}/mission/complete` - Robot báo hoàn thành
- `hospital/robots/{robotId}/mission/cancel` - Backend hủy nhiệm vụ (robot subscribe)

### Commands (Backend → Robot)
- `hospital/robots/{robotId}/command` - Lệnh điều khiển (stop, resume, etc.)

## 5. Xác định IP của máy chạy Mosquitto

Để ESP32 kết nối, cần biết IP của máy chủ:
```powershell
ipconfig
```
Tìm **IPv4 Address** trong adapter đang dùng (thường là WiFi hoặc Ethernet).

## 6. Firewall (Nếu ESP32 không kết nối được)
Mở port 1883:
```powershell
# PowerShell Admin
New-NetFirewallRule -DisplayName "Mosquitto MQTT" -Direction Inbound -LocalPort 1883 -Protocol TCP -Action Allow
```

## 7. Credentials mặc định (có thể thay đổi)
- **Username:** `hospital_robot` / `hospital_backend`
- **Password:** `123456`
- **Port:** `1883`
- **Host:** `192.168.0.102` (IP máy của bạn)
