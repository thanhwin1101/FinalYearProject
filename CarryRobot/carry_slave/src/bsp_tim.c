#include "bsp_tim.h"
#include "bsp_pins.h"

TIM_HandleTypeDef htim4;

void BSP_TIM4_PWM_Init(void) {
    TIM_OC_InitTypeDef sConfigOC = {0};
    GPIO_InitTypeDef GPIO_InitStruct = {0};

    // 1. Kích hoạt xung nhịp TIM4 và GPIOB
    __HAL_RCC_TIM4_CLK_ENABLE();
    __HAL_RCC_GPIOB_CLK_ENABLE();

    // 2. Cấu hình chân PB6 (TIM4_CH1) và PB7 (TIM4_CH2) ở chế độ Alternate Function AF2
    GPIO_InitStruct.Pin = GPIO_PIN_6 | GPIO_PIN_7;
    GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
    GPIO_InitStruct.Alternate = GPIO_AF2_TIM4;
    HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);

    // 3. Cấu hình Timebase cho TIM4: 20kHz PWM (100MHz / 5 / 1000 = 20kHz)
    htim4.Instance = TIM4;
    htim4.Init.Prescaler = 4;
    htim4.Init.CounterMode = TIM_COUNTERMODE_UP;
    htim4.Init.Period = 999;
    htim4.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
    htim4.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_ENABLE;
    HAL_TIM_PWM_Init(&htim4);

    // 4. Cấu hình PWM Mode 1 trên 2 kênh CH1 và CH2
    sConfigOC.OCMode = TIM_OCMODE_PWM1;
    sConfigOC.Pulse = 0;
    sConfigOC.OCPolarity = TIM_OCPOLARITY_HIGH;
    sConfigOC.OCFastMode = TIM_OCFAST_DISABLE;

    HAL_TIM_PWM_ConfigChannel(&htim4, &sConfigOC, TIM_CHANNEL_1);
    HAL_TIM_PWM_ConfigChannel(&htim4, &sConfigOC, TIM_CHANNEL_2);

    // 5. Bật xuất xung PWM phần cứng
    HAL_TIM_PWM_Start(&htim4, TIM_CHANNEL_1);
    HAL_TIM_PWM_Start(&htim4, TIM_CHANNEL_2);
}

void BSP_Motor_SetPWM_Left(uint16_t duty) {
    if (duty > 1000) duty = 1000;
    __HAL_TIM_SET_COMPARE(&htim4, TIM_CHANNEL_1, duty);
}

void BSP_Motor_SetPWM_Right(uint16_t duty) {
    if (duty > 1000) duty = 1000;
    __HAL_TIM_SET_COMPARE(&htim4, TIM_CHANNEL_2, duty);
}
