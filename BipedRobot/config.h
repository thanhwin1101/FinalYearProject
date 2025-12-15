#pragma once // Đảm bảo file này chỉ được include 1 lần

// ==========================================================
// TỔNG QUAN ROBOT
// ==========================================================
#define JOINT_COUNT 10 // 10 servo (5 mỗi chân)

// ==========================================================
// 1. KÍCH THƯỚC (Đơn vị: mm)
// ==========================================================
// (Từ thông tin: 6cm, 7cm, 6.2cm, 8cm)

#define THIGH_LENGTH_MM      60.0  // (L1) Đùi: Hông -> Gối
#define SHANK_LENGTH_MM      70.0  // (L2) Cẳng chân: Gối -> Mắt cá
#define ANKLE_HEIGHT_MM      62.0  // (L3) Cao độ: Mắt cá -> Đất
#define FOOT_LENGTH_MM       80.0  // (L4) Dài bàn chân

// ==========================================================
// 2. BIÊN ĐỘ KHỚP (Đơn vị: Độ)
// (Quy ước: 0 độ là khi robot đứng thẳng)
// ==========================================================

// --- Chuyển động TỚI/LÙI (PITCH) ---
#define HIP_PITCH_MIN_ANGLE   -45.0
#define HIP_PITCH_MAX_ANGLE   45.0
#define KNEE_PITCH_MIN_ANGLE  -5.0
#define KNEE_PITCH_MAX_ANGLE  140.0
#define ANKLE_PITCH_MIN_ANGLE -30.0
#define ANKLE_PITCH_MAX_ANGLE 30.0


// --- Chuyển động NGANG (ROLL) ---
#define HIP_ROLL_MIN_ANGLE    -30.0 // (Đã xác nhận)
#define HIP_ROLL_MAX_ANGLE    30.0  // (Đã xác nhận)
#define ANKLE_ROLL_MIN_ANGLE  -30.0 // (Đã xác nhận)
#define ANKLE_ROLL_MAX_ANGLE  30.0  // (Đã xác nhận)

// ==========================================================
// 3. CẤU HÌNH SERVO (PIN, OFFSET, ĐẢO NGƯỢC)
// ==========================================================

// Định nghĩa cấu trúc cho một khớp
struct JointCfg {
  const char* name;     // Tên khớp (để debug)
  int pin;              // Kênh trên PCA9685
  float zero_offset;    // Giá trị (0-180) tương ứng với 0 độ
  bool inverted;        // Có bị đảo ngược chiều không
};

// Khởi tạo mảng cấu hình dựa trên dữ liệu bạn cung cấp
JointCfg joints[JOINT_COUNT] = {
  {"HIP_PITCH_L", 0, 47.50f, false},
  {"HIP_ROLL_L", 1, 56.00f, false},
  {"KNEE_PITCH_L", 2, 49.00f, false},
  {"ANKLE_PITCH_L", 3, 53.50f, false},
  {"ANKLE_ROLL_L", 4, 55.50f, false},
  {"HIP_PITCH_R", 5, 60.50f, true},
  {"HIP_ROLL_R", 6, 88.50f, false},
  {"KNEE_PITCH_R", 7, 53.50f, true},
  {"ANKLE_PITCH_R", 8, 57.00f, true},
  {"ANKLE_ROLL_R", 9, 59.00f, false}
};

// Index tiện lợi để truy cập mảng trên
enum JointIndex {
  HIP_PITCH_L = 0,  
  HIP_ROLL_L = 1,
  KNEE_PITCH_L = 2,
  ANKLE_PITCH_L = 3,
  ANKLE_ROLL_L = 4,
  HIP_PITCH_R = 5,
  HIP_ROLL_R = 6,
  KNEE_PITCH_R = 7,
  ANKLE_PITCH_R = 8,
  ANKLE_ROLL_R = 9
};
