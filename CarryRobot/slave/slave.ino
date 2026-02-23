// ==========================================
// ARDUINO NANO SLAVE - MOTOR & LINE TRACKER
// ==========================================
#include <Arduino.h>

// --- CẤU HÌNH ĐẢO CHIỀU ĐỘNG CƠ ---
const bool INVERT_LEFT  = true;
const bool INVERT_RIGHT = true;

// Khai báo chân L298N (Mecanum 4 bánh)
#define EN_FL 5 
#define IN1_FL 2
#define IN2_FL 3

#define EN_FR 6 
#define IN1_FR 4
#define IN2_FR 7

#define EN_RL 9 
#define IN1_RL 8
#define IN2_RL 11

#define EN_RR 10 
#define IN1_RR 12
#define IN2_RR 13

// Khai báo chân Dò Line 5 mắt
int linePins[5] = {A0, A1, A2, A3, A4};
bool isLineFollowing = false;
int lineBaseSpeed = 0;

void setup() {
  Serial.begin(115200);

  pinMode(EN_FL, OUTPUT); pinMode(IN1_FL, OUTPUT); pinMode(IN2_FL, OUTPUT);
  pinMode(EN_FR, OUTPUT); pinMode(IN1_FR, OUTPUT); pinMode(IN2_FR, OUTPUT);
  pinMode(EN_RL, OUTPUT); pinMode(IN1_RL, OUTPUT); pinMode(IN2_RL, OUTPUT);
  pinMode(EN_RR, OUTPUT); pinMode(IN1_RR, OUTPUT); pinMode(IN2_RR, OUTPUT);

  for(int i=0; i<5; i++) pinMode(linePins[i], INPUT);
}

// Hàm xuất tín hiệu Motor có tích hợp Đảo chiều (Invert)
void setMotor(int en, int in1, int in2, int speed, bool invert) {
  if (invert) speed = -speed; // Đảo dấu tốc độ nếu cần
  
  if (speed > 0) { 
    digitalWrite(in1, HIGH); digitalWrite(in2, LOW); 
  } else if (speed < 0) { 
    digitalWrite(in1, LOW); digitalWrite(in2, HIGH); 
  } else { 
    digitalWrite(in1, LOW); digitalWrite(in2, LOW); 
  }
  
  analogWrite(en, abs(speed));
}

// Hàm tính toán động học Mecanum Wheel
void setMecanum(int x, int y, int z) {
  int fl = y + x + z;
  int fr = y - x - z;
  int rl = y - x + z;
  int rr = y + x - z;

  // Cân bằng giới hạn PWM
  int maxSpeed = max(max(abs(fl), abs(fr)), max(abs(rl), abs(rr)));
  if (maxSpeed > 255) {
    fl = map(fl, -maxSpeed, maxSpeed, -255, 255);
    fr = map(fr, -maxSpeed, maxSpeed, -255, 255);
    rl = map(rl, -maxSpeed, maxSpeed, -255, 255);
    rr = map(rr, -maxSpeed, maxSpeed, -255, 255);
  }
  
  // Gọi hàm điều khiển phần cứng với cờ INVERT tương ứng
  setMotor(EN_FL, IN1_FL, IN2_FL, fl, INVERT_LEFT);
  setMotor(EN_FR, IN1_FR, IN2_FR, fr, INVERT_RIGHT);
  setMotor(EN_RL, IN1_RL, IN2_RL, rl, INVERT_LEFT);
  setMotor(EN_RR, IN1_RR, IN2_RR, rr, INVERT_RIGHT);
}

// Thuật toán Bám Line PID (Weighted Average)
void runLineFollower() {
  int L2 = digitalRead(linePins[0]);
  int L1 = digitalRead(linePins[1]);
  int C  = digitalRead(linePins[2]);
  int R1 = digitalRead(linePins[3]);
  int R2 = digitalRead(linePins[4]);

  // Nếu mất line hoàn toàn, trượt chậm thẳng tới để tìm lại line
  if (L2==0 && L1==0 && C==0 && R1==0 && R2==0) {
    setMecanum(0, lineBaseSpeed / 2, 0); 
    Serial.println("STATUS:LINE_LOST");
    return;
  }
  
  // Báo cáo đã tìm thấy Line (Hỗ trợ Visual Docking)
  Serial.println("STATUS:ON_LINE");

  // Tính trung bình trọng số (Weighted Average)
  int sum = L2 + L1 + C + R1 + R2;
  int weighted_sum = L2*(-20) + L1*(-10) + C*0 + R1*10 + R2*20;
  float error = (float)weighted_sum / (float)sum;

  // PID
  float Kp = 3.5;  // Hệ số P
  float Kd = 1.2;  // Hệ số D chống văng
  static float last_error = 0;
  
  int turn = (error * Kp) + ((error - last_error) * Kd);
  last_error = error;

  // Đưa lệnh điều khiển xuống bánh Mecanum (Y: Tiến, Z: Xoay)
  setMecanum(0, lineBaseSpeed, turn);
}

void loop() {
  // Nhận lệnh UART từ ESP32
  if (Serial.available()) {
    String cmd = Serial.readStringUntil('\n');
    cmd.trim();
    
    // Lệnh di chuyển chủ động (Bám người, rẽ ngã tư, phanh)
    if (cmd.startsWith("MOVE:")) {
      isLineFollowing = false;
      int c1 = cmd.indexOf(',');
      int c2 = cmd.lastIndexOf(',');
      int x = cmd.substring(5, c1).toInt();
      int y = cmd.substring(c1 + 1, c2).toInt();
      int z = cmd.substring(c2 + 1).toInt();
      setMecanum(x, y, z);
      Serial.println("ACK:MOVE");
    } 
    // Lệnh bám vạch
    else if (cmd.startsWith("LINE:")) {
      isLineFollowing = true;
      lineBaseSpeed = cmd.substring(5).toInt();
      Serial.println("ACK:LINE");
    }
    // Lệnh dừng
    else if (cmd == "STOP") {
      isLineFollowing = false;
      setMecanum(0, 0, 0);
      Serial.println("ACK:STOP");
    }
  }

  // Chạy vòng lặp PID nếu đang ở chế độ bám vạch
  if (isLineFollowing) runLineFollower();
}