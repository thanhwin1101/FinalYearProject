#pragma once
#include "stm32f4xx_hal.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

extern SPI_HandleTypeDef hspi1;

void BSP_SPI1_Init(void);
HAL_StatusTypeDef BSP_SPI1_TransmitReceive(uint8_t* pTxData, uint8_t* pRxData, uint16_t Size, uint32_t Timeout);
void BSP_PN532_CS_Low(void);
void BSP_PN532_CS_High(void);

#ifdef __cplusplus
}
#endif
