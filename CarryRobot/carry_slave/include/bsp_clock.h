#pragma once
// ====================================================================
//  BSP Clock – STM32F411CE (100MHz System Clock from 25MHz HSE)
// ====================================================================
#include "stm32f4xx_hal.h"

#ifdef __cplusplus
extern "C" {
#endif

void SystemClock_Config(void);

#ifdef __cplusplus
}
#endif
