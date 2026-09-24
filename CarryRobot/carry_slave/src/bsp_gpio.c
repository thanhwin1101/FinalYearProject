#include "bsp_gpio.h"

void BSP_GPIO_Init(void) {
    GPIO_InitTypeDef GPIO_InitStruct = {0};

    // 1. Kích hoạt xung nhịp cho các Port GPIO
    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_GPIOB_CLK_ENABLE();
    __HAL_RCC_GPIOC_CLK_ENABLE();

    // 2. Cấu hình LED trên board (PC13 - Active LOW)
    HAL_GPIO_WritePin(LED_PORT, LED_PIN, GPIO_PIN_SET);
    GPIO_InitStruct.Pin = LED_PIN;
    GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(LED_PORT, &GPIO_InitStruct);

    // 3. Cấu hình các chân điều khiển hướng động cơ L298N
    // Motor Left IN1 (PA0), IN2 (PA1)
    HAL_GPIO_WritePin(GPIOA, MOT_L_IN1_PIN | MOT_L_IN2_PIN, GPIO_PIN_RESET);
    GPIO_InitStruct.Pin = MOT_L_IN1_PIN | MOT_L_IN2_PIN;
    GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_MEDIUM;
    HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

    // Motor Right IN1 (PB13), IN2 (PB12)
    HAL_GPIO_WritePin(GPIOB, MOT_R_IN1_PIN | MOT_R_IN2_PIN, GPIO_PIN_RESET);
    GPIO_InitStruct.Pin = MOT_R_IN1_PIN | MOT_R_IN2_PIN;
    HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);

    // 4. Cấu hình chân cảm biến siêu âm SR05: Trig (PA2 Output), Echo (PA3 Input)
    HAL_GPIO_WritePin(SR05_TRIG_PORT, SR05_TRIG_PIN, GPIO_PIN_RESET);
    GPIO_InitStruct.Pin = SR05_TRIG_PIN;
    GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    HAL_GPIO_Init(SR05_TRIG_PORT, &GPIO_InitStruct);

    GPIO_InitStruct.Pin = SR05_ECHO_PIN;
    GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    HAL_GPIO_Init(SR05_ECHO_PORT, &GPIO_InitStruct);

    // 5. Cấu hình 3 mắt cảm biến dò Line (Active LOW -> Cần Pull-up)
    // PB8 (Left), PB9 (Center)
    GPIO_InitStruct.Pin = LINE_L_PIN | LINE_C_PIN;
    GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
    GPIO_InitStruct.Pull = GPIO_PULLUP;
    HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);

    // PA4 (Right)
    GPIO_InitStruct.Pin = LINE_R_PIN;
    HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

    // 6. Cấu hình chân Chip Select cho RFID PN532 (PB14 - Output High mặc định)
    HAL_GPIO_WritePin(PN532_CS_PORT, PN532_CS_PIN, GPIO_PIN_SET);
    GPIO_InitStruct.Pin = PN532_CS_PIN;
    GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(PN532_CS_PORT, &GPIO_InitStruct);
}
