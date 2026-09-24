#include "bsp_uart.h"
#include <string.h>

UART_HandleTypeDef huart1;

#define UART_RX_RING_SIZE 256
static uint8_t s_rxRing[UART_RX_RING_SIZE];
static volatile uint16_t s_rxHead = 0;
static volatile uint16_t s_rxTail = 0;
static uint8_t s_rxByte = 0;

void BSP_USART1_Init(void) {
    GPIO_InitTypeDef GPIO_InitStruct = {0};

    // 1. Kích hoạt xung nhịp USART1 và GPIOA
    __HAL_RCC_USART1_CLK_ENABLE();
    __HAL_RCC_GPIOA_CLK_ENABLE();

    // 2. Cấu hình PA9 (TX) và PA10 (RX) ở chế độ Alternate Function AF7
    GPIO_InitStruct.Pin = GPIO_PIN_9 | GPIO_PIN_10;
    GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
    GPIO_InitStruct.Pull = GPIO_PULLUP;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
    GPIO_InitStruct.Alternate = GPIO_AF7_USART1;
    HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

    // 3. Khởi tạo thông số giao tiếp UART: 115200 8N1
    huart1.Instance = USART1;
    huart1.Init.BaudRate = 115200;
    huart1.Init.WordLength = UART_WORDLENGTH_8B;
    huart1.Init.StopBits = UART_STOPBITS_1;
    huart1.Init.Parity = UART_PARITY_NONE;
    huart1.Init.Mode = UART_MODE_TX_RX;
    huart1.Init.HwFlowCtl = UART_HWCONTROL_NONE;
    huart1.Init.OverSampling = UART_OVERSAMPLING_16;
    HAL_UART_Init(&huart1);

    // 4. Kích hoạt ngắt NVIC cho USART1
    HAL_NVIC_SetPriority(USART1_IRQn, 5, 0);
    HAL_NVIC_EnableIRQ(USART1_IRQn);

    // 5. Bắt đầu nhận byte đầu tiên qua ngắt
    HAL_UART_Receive_IT(&huart1, &s_rxByte, 1);
}

void BSP_UART_SendString(const char* str) {
    if (!str) return;
    HAL_UART_Transmit(&huart1, (uint8_t*)str, strlen(str), 100);
}

void BSP_UART_SendData(const uint8_t* data, uint16_t len) {
    if (!data || len == 0) return;
    HAL_UART_Transmit(&huart1, (uint8_t*)data, len, 200);
}

bool BSP_UART_GetByte(uint8_t* outByte) {
    if (s_rxHead == s_rxTail) {
        return false; // Rỗng
    }
    *outByte = s_rxRing[s_rxTail];
    s_rxTail = (s_rxTail + 1) % UART_RX_RING_SIZE;
    return true;
}

// Callback khi nhận xong 1 byte qua ngắt
void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart) {
    if (huart->Instance == USART1) {
        uint16_t nextHead = (s_rxHead + 1) % UART_RX_RING_SIZE;
        if (nextHead != s_rxTail) {
            s_rxRing[s_rxHead] = s_rxByte;
            s_rxHead = nextHead;
        }
        // Tiếp tục lắng nghe byte kế tiếp
        HAL_UART_Receive_IT(&huart1, &s_rxByte, 1);
    }
}

// Hàm ngắt phần cứng USART1
void USART1_IRQHandler(void) {
    HAL_UART_IRQHandler(&huart1);
}
