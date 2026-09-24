/**
 * @file bsp_clock.c
 * @brief Board Support Package - STM32F411CE Clock Configuration (100MHz)
 */
#include "bsp_clock.h"

void SystemClock_Config(void) {
    RCC_OscInitTypeDef RCC_OscInitStruct = {0};
    RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

    // Bật bộ điều chỉnh điện áp nội (Scale 1 cho xung nhịp tối đa 100MHz)
    __HAL_RCC_PWR_CLK_ENABLE();
    __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE1);

    // Khởi tạo thạch anh ngoài HSE 25MHz và mạch nhân tần PLL
    RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSE;
    RCC_OscInitStruct.HSEState = RCC_HSE_ON;
    RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
    RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSE;
    RCC_OscInitStruct.PLL.PLLM = 25;              // 25MHz / 25 = 1MHz
    RCC_OscInitStruct.PLL.PLLN = 200;             // 1MHz * 200 = 200MHz
    RCC_OscInitStruct.PLL.PLLP = RCC_PLLP_DIV2;   // 200MHz / 2 = 100MHz SYSCLK
    RCC_OscInitStruct.PLL.PLLQ = 4;
    HAL_RCC_OscConfig(&RCC_OscInitStruct);

    // Cấu hình các Bus AHB, APB1 (max 50MHz), APB2 (max 100MHz)
    RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK | RCC_CLOCKTYPE_SYSCLK |
                                  RCC_CLOCKTYPE_PCLK1 | RCC_CLOCKTYPE_PCLK2;
    RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
    RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1; // HCLK = 100MHz
    RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV2;   // PCLK1 = 50MHz
    RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;   // PCLK2 = 100MHz
    HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_3);
}

// Handler cho ngắt SysTick (1ms timebase cho HAL_GetTick())
void SysTick_Handler(void) {
    HAL_IncTick();
}
