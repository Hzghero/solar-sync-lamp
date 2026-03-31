/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.h
  * @brief          : Header for main.c file.
  *                   This file contains the common defines of the application.
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2026 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */
/* USER CODE END Header */

/* Define to prevent recursive inclusion -------------------------------------*/
#ifndef __MAIN_H
#define __MAIN_H

#ifdef __cplusplus
extern "C" {
#endif

/* Includes ------------------------------------------------------------------*/
#include "stm32c0xx_hal.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */

/* USER CODE END Includes */

/* Exported types ------------------------------------------------------------*/
/* USER CODE BEGIN ET */

/* USER CODE END ET */

/* Exported constants --------------------------------------------------------*/
/* USER CODE BEGIN EC */

/* USER CODE END EC */

/* Exported macro ------------------------------------------------------------*/
/* USER CODE BEGIN EM */

/* USER CODE END EM */

void HAL_TIM_MspPostInit(TIM_HandleTypeDef *htim);

/* Exported functions prototypes ---------------------------------------------*/
void Error_Handler(void);

/* USER CODE BEGIN EFP */

/* USER CODE END EFP */

/* USER CODE BEGIN Private Defines */
/* 固件版本：每次源码改动请递增/更新该字符串 */
#define FW_VERSION  "v0.1.3-SleepRTC-ADCAvg16-3s-FixRSF"

/* UART 调试开关：1=输出调试信息；0=仅保留固件版本打印 */
#define ENABLE_UART_DEBUG 1

/* 低功耗模式选择：
 * 1 = 使用 STOP（更低功耗但更容易出现 SWD/下载不稳定）
 * 0 = 使用 SLEEP/WFI（更调试友好，推荐先调通功能）
 */
#define LOWPOWER_USE_STOP 0

/* 串口调试输出（ENABLE_UART_DEBUG=1 时输出） */
void DebugPrint(const char *s);
/* USER CODE END Private Defines */

/* Private defines -----------------------------------------------------------*/
#define KEY_IN_Pin GPIO_PIN_7
#define KEY_IN_GPIO_Port GPIOB
#define LED_DRV_Pin GPIO_PIN_2
#define LED_DRV_GPIO_Port GPIOA
#define BOOST_EN_Pin GPIO_PIN_3
#define BOOST_EN_GPIO_Port GPIOA
#define RF_CSN_Pin GPIO_PIN_4
#define RF_CSN_GPIO_Port GPIOA
#define RF_SCK_Pin GPIO_PIN_5
#define RF_SCK_GPIO_Port GPIOA
#define DIV_MOS_Pin GPIO_PIN_6
#define DIV_MOS_GPIO_Port GPIOA
#define RF_DATA_Pin GPIO_PIN_7
#define RF_DATA_GPIO_Port GPIOA
#define CHG_MOS_Pin GPIO_PIN_8
#define CHG_MOS_GPIO_Port GPIOA
#define LED_Pin GPIO_PIN_6
#define LED_GPIO_Port GPIOB

/* USER CODE BEGIN Private defines */

/* USER CODE END Private defines */

#ifdef __cplusplus
}
#endif

#endif /* __MAIN_H */
