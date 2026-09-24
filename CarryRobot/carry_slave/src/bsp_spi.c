#include "bsp_spi.h"
#include "bsp_pins.h"

SPI_HandleTypeDef hspi1;

void BSP_SPI1_Init(void) {
    GPIO_InitTypeDef GPIO_InitStruct = {0};

    // 1. Kích hoạt xung nhịp SPI1 và GPIOA
    __HAL_RCC_SPI1_CLK_ENABLE();
    __HAL_RCC_GPIOA_CLK_ENABLE();

    // 2. Cấu hình chân SPI1: PA5 (SCK), PA6 (MISO), PA7 (MOSI) ở AF5
    GPIO_InitStruct.Pin = GPIO_PIN_5 | GPIO_PIN_6 | GPIO_PIN_7;
    GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
    GPIO_InitStruct.Alternate = GPIO_AF5_SPI1;
    HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

    // 3. Khởi tạo SPI1 Master Mode 0, LSB First (chuẩn PN532 SPI), Baudrate = 100MHz / 32 = 3.125MHz
    hspi1.Instance = SPI1;
    hspi1.Init.Mode = SPI_MODE_MASTER;
    hspi1.Init.Direction = SPI_DIRECTION_2LINES;
    hspi1.Init.DataSize = SPI_DATASIZE_8BIT;
    hspi1.Init.CLKPolarity = SPI_POLARITY_LOW;
    hspi1.Init.CLKPhase = SPI_PHASE_1EDGE;
    hspi1.Init.NSS = SPI_NSS_SOFT;
    hspi1.Init.BaudRatePrescaler = SPI_BAUDRATEPRESCALER_32;
    hspi1.Init.FirstBit = SPI_FIRSTBIT_LSB; // PN532 yêu cầu LSB first trong giao tiếp dữ liệu SPI
    hspi1.Init.TIMode = SPI_TIMODE_DISABLE;
    hspi1.Init.CRCCalculation = SPI_CRCCALCULATION_DISABLE;
    HAL_SPI_Init(&hspi1);
}

HAL_StatusTypeDef BSP_SPI1_TransmitReceive(uint8_t* pTxData, uint8_t* pRxData, uint16_t Size, uint32_t Timeout) {
    return HAL_SPI_TransmitReceive(&hspi1, pTxData, pRxData, Size, Timeout);
}

void BSP_PN532_CS_Low(void) {
    HAL_GPIO_WritePin(PN532_CS_PORT, PN532_CS_PIN, GPIO_PIN_RESET);
}

void BSP_PN532_CS_High(void) {
    HAL_GPIO_WritePin(PN532_CS_PORT, PN532_CS_PIN, GPIO_PIN_SET);
}
