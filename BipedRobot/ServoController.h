#pragma once
#include <Adafruit_PWMServoDriver.h>
#include "config.h" // Nạp các cấu hình

// Tần số PWM cho servo (hầu hết servo analog là 50Hz)
#define SERVO_FREQ 50
// Biên độ xung (pulse) cho servo 0-180 độ.
// Bạn có thể phải "tune" 2 giá trị này cho servo của mình (ví dụ: MG996R)
#define SERVOMIN_PULSE 150 // Xung tối thiểu (tương ứng 0 độ)
#define SERVOMAX_PULSE 600 // Xung tối đa (tương ứng 180 độ)

class ServoController {
public:
  // Khởi tạo đối tượng PCA9685
  Adafruit_PWMServoDriver pca = Adafruit_PWMServoDriver();

  ServoController() {}

  void begin() {
    pca.begin();
    pca.setOscillatorFrequency(27000000); // Tần số cho PCA
    pca.setPWMFreq(SERVO_FREQ); // Set tần số cho servo
  }

  /**
   * Đặt góc LOGIC cho một khớp (ví dụ: -30 đến +30 độ)
   * Lớp này sẽ tự động:
   * 1. Áp dụng đảo ngược (inverted)
   * 2. Áp dụng offset (zero_offset)
   * 3. Chuyển đổi góc 0-180 sang xung (pulse)
   * 4. Gửi lệnh tới PCA
   */
  void setAngle(int jointIndex, float logical_angle_deg) {
    if (jointIndex < 0 || jointIndex >= JOINT_COUNT) return;

    JointCfg cfg = joints[jointIndex];
    
    // 1. Áp dụng đảo ngược (nếu có)
    float final_logical_angle = cfg.inverted ? -logical_angle_deg : logical_angle_deg;

    // 2. Áp dụng offset
    // 'zero_offset' là giá trị servo (0-180) tương ứng với 0 độ logic
    float servo_angle = cfg.zero_offset + final_logical_angle;

    // 3. Kẹp giá trị trong khoảng an toàn của servo 0-180
    // (Lưu ý: hàm IK nên kẹp trong biên độ MIN/MAX logic trước)
    servo_angle = constrain(servo_angle, 0.0, 180.0);

    // 4. Chuyển đổi góc 0-180 sang xung
    int pulse = map(servo_angle, 0, 180, SERVOMIN_PULSE, SERVOMAX_PULSE);

    // 5. Gửi lệnh
    pca.setPWM(cfg.pin, 0, pulse);
  }

  // Đặt tất cả các servo về vị trí 0 độ (đứng thẳng)
  void setHomePosition() {
    for (int i = 0; i < JOINT_COUNT; i++) {
      setAngle(i, 0.0f); // Gửi lệnh 0 độ logic
    }
  }
};