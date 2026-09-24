#pragma once
#include "stm32f4xx_hal.h"

#ifdef __cplusplus
extern "C" {
#endif

extern TIM_HandleTypeDef htim4;

void BSP_TIM4_PWM_Init(void);
void BSP_Motor_SetPWM_Left(uint16_t duty);
void BSP_Motor_SetPWM_Right(uint16_t duty);

#ifdef __cplusplus
}
#endif
