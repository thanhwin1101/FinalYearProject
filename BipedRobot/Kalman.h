#pragma once
#include <math.h>

class Kalman {
public:
    Kalman() {
        // Giá trị khởi tạo, sẽ được set lại
        Q_angle = 0.001;
        Q_bias = 0.003;
        R_measure = 0.03;

        angle = 0; 
        bias = 0; 

        // Ma trận hiệp phương sai lỗi
        P[0][0] = 0; 
        P[0][1] = 0;
        P[1][0] = 0;
        P[1][1] = 0;
    }

    // Hàm chính: nhận góc (từ accel) và tốc độ quay (từ gyro)
    float getAngle(float newAngle, float newRate, float dt) {
        // Bước 1: Dự đoán (Predict)
        rate = newRate - bias;
        angle += dt * rate;

        // Cập nhật ma trận hiệp phương sai lỗi
        P[0][0] += dt * (dt * P[1][1] - P[0][1] - P[1][0] + Q_angle);
        P[0][1] -= dt * P[1][1];
        P[1][0] -= dt * P[1][1];
        P[1][1] += Q_bias * dt;

        // Bước 2: Hiệu chỉnh (Correct / Update)
        float S = P[0][0] + R_measure; // Lỗi (Error)
        
        // Tính toán Kalman Gain
        float K[2];
        K[0] = P[0][0] / S;
        K[1] = P[1][0] / S;

        // Tính toán góc và bias mới
        float y = newAngle - angle; // Sai số
        angle += K[0] * y;
        bias += K[1] * y;

        // Cập nhật lại ma trận hiệp phương sai lỗi
        float P00_temp = P[0][0];
        float P01_temp = P[0][1];

        P[0][0] -= K[0] * P00_temp;
        P[0][1] -= K[0] * P01_temp;
        P[1][0] -= K[1] * P00_temp;
        P[1][1] -= K[1] * P01_temp;

        return angle;
    }

    // --- Các hàm để "tune" bộ lọc ---
    void setAngle(float newAngle) { angle = newAngle; }
    void setQangle(float newQ_angle) { Q_angle = newQ_angle; }
    void setQbias(float newQ_bias) { Q_bias = newQ_bias; }
    void setRmeasure(float newR_measure) { R_measure = newR_measure; }

private:
    // Các hằng số "tune" (phải tự tinh chỉnh)
    float Q_angle;   // Độ nhiễu của Gyro (góc)
    float Q_bias;    // Độ nhiễu của Gyro (bias)
    float R_measure; // Độ nhiễu của Accel

    float angle; // Góc (state)
    float bias;  // Bias (state)
    float rate;  // Tốc độ quay (input)

    float P[2][2]; // Ma trận hiệp phương sai
};