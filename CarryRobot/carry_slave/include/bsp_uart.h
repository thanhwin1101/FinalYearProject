#pragma once
#include "stm32f4xx_hal.h"
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

extern UART_HandleTypeDef huart1;

void BSP_USART1_Init(void);
void BSP_UART_SendString(const char* str);
void BSP_UART_SendData(const uint8_t* data, uint16_t len);
bool BSP_UART_GetByte(uint8_t* outByte);

#ifdef __cplusplus
}
#endif
