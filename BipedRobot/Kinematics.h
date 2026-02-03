#pragma once
#include <math.h>
#include "config.h"

// Định nghĩa chân
#define LEG_LEFT 0
#define LEG_RIGHT 1

class Kinematics {
public:
  // Mảng lưu trữ 10 góc servo đích (đã tính toán)
  float target_angles[JOINT_COUNT];

  Kinematics() {
    // Khởi tạo tất cả góc về 0
    for (int i = 0; i < JOINT_COUNT; i++) {
      target_angles[i] = 0.0f;
    }
  }

  /**
   * Tính toán Động học nghịch 3D (5-DOF)
   * Input:
   * leg: LEG_LEFT hoặc LEG_RIGHT
   * x: Vị trí tới/lùi (forward/backward) của mắt cá (mm)
   * y: Vị trí cao/thấp (up/down) của mắt cá (mm) - [QUAN TRỌNG: y là âm, ví dụ -150mm]
   * z: Vị trí ngang (left/right) của mắt cá (mm)
   * ankle_pitch_rad: Góc nghiêng tới/lui của bàn chân (rad) - 0 là phẳng
   * ankle_roll_rad: Góc nghiêng ngang của bàn chân (rad) - 0 là phẳng
   *
   * Output:
   * 5 góc (độ) được lưu vào mảng target_angles[]
   */
  bool calculateIK_3D(int leg, float x, float y, float z, float ankle_pitch_rad, float ankle_roll_rad) {
    
    // --- Bước 1: Tính toán vị trí cổ chân (ankle center)
    // Đây là vị trí tâm xoay của khớp ankle roll/pitch
    // Bỏ qua bước này nếu (x,y,z) đã là vị trí tâm cổ chân
    
    // --- Bước 2: Tính toán góc Hip Roll và Ankle Roll
    // Giải bài toán 2D trong mặt phẳng Y-Z (nhìn từ phía trước)
    
    float hip_roll_rad = atan2(z, -y); // -y vì y là số âm
    float L_eff = sqrt(y*y + z*z); // Chiều dài "hiệu dụng" của chân trong mặt phẳng 2D

    // --- Bước 3: Tính toán góc Hip Pitch và Knee Pitch
    // Giải bài toán 2D trong mặt phẳng X-L_eff (nhìn từ bên hông)
    
    // Khoảng cách từ hông tới mắt cá
    float D_sq = x*x + (L_eff - ANKLE_HEIGHT_MM)*(L_eff - ANKLE_HEIGHT_MM); // Khoảng cách bình phương
    float D = sqrt(D_sq);

    // Kiểm tra xem có vươn tới được không
    if (D > (THIGH_LENGTH_MM + SHANK_LENGTH_MM)) {
      // Serial.println("IK Error: Target out of reach");
      return false; // Quá xa
    }

    // Sử dụng Định lý Cosin để tìm góc gối
    // beta là góc tại đầu gối
    float beta_rad = acos((THIGH_LENGTH_MM*THIGH_LENGTH_MM + SHANK_LENGTH_MM*SHANK_LENGTH_MM - D_sq) / 
                          (2 * THIGH_LENGTH_MM * SHANK_LENGTH_MM));

    float knee_pitch_rad = PI - beta_rad; // Góc gối (luôn dương)

    // Tìm góc hip pitch
    float alpha_rad = acos((THIGH_LENGTH_MM*THIGH_LENGTH_MM + D_sq - SHANK_LENGTH_MM*SHANK_LENGTH_MM) /
                          (2 * THIGH_LENGTH_MM * D));

    float gamma_rad = atan2(x, (L_eff - ANKLE_HEIGHT_MM));

    float hip_pitch_rad = gamma_rad - alpha_rad;

    // --- Bước 4: Tính toán góc Ankle Pitch
    // Để giữ bàn chân song song mặt đất
    float ankle_pitch_rad_comp = -hip_pitch_rad - knee_pitch_rad;
    
    // Thêm góc nghiêng chủ động (nếu có)
    ankle_pitch_rad = ankle_pitch_rad_comp + ankle_pitch_rad; // (ankle_pitch_rad từ input)

    // --- Bước 5: Chuyển đổi Radian sang Độ và KẸP (Clamp) giá trị
    
    float angles_deg[5];
    angles_deg[0] = constrain(degrees(hip_pitch_rad), HIP_PITCH_MIN_ANGLE, HIP_PITCH_MAX_ANGLE);
    angles_deg[1] = constrain(degrees(hip_roll_rad), HIP_ROLL_MIN_ANGLE, HIP_ROLL_MAX_ANGLE);
    angles_deg[2] = constrain(degrees(knee_pitch_rad), KNEE_PITCH_MIN_ANGLE, KNEE_PITCH_MAX_ANGLE);
    angles_deg[3] = constrain(degrees(ankle_pitch_rad), ANKLE_PITCH_MIN_ANGLE, ANKLE_PITCH_MAX_ANGLE);
    angles_deg[4] = constrain(degrees(ankle_roll_rad), ANKLE_ROLL_MIN_ANGLE, ANKLE_ROLL_MAX_ANGLE);

    // --- Bước 6: Lưu vào mảng target_angles chính
    if (leg == LEG_LEFT) {
      target_angles[HIP_PITCH_L]   = angles_deg[0];
      target_angles[HIP_ROLL_L]    = angles_deg[1];
      target_angles[KNEE_PITCH_L]  = angles_deg[2];
      target_angles[ANKLE_PITCH_L] = angles_deg[3];
      target_angles[ANKLE_ROLL_L]  = angles_deg[4];
    } else { // LEG_RIGHT
      target_angles[HIP_PITCH_R]   = angles_deg[0];
      target_angles[HIP_ROLL_R]    = angles_deg[1];
      target_angles[KNEE_PITCH_R]  = angles_deg[2];
      target_angles[ANKLE_PITCH_R] = angles_deg[3];
      target_angles[ANKLE_ROLL_R]  = angles_deg[4];
    }
    
    return true; // Tính toán thành công
  }
};