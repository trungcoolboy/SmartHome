/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file    UART/UART_Printf/Inc/main.h
  * @author  MCD Application Team
  * @brief   Header for main.c module
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2019 STMicroelectronics.
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
#include "stm32g4xx_hal.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include <stdio.h>
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

/* Exported functions prototypes ---------------------------------------------*/
extern UART_HandleTypeDef huart1;
extern UART_HandleTypeDef huart2;
void Error_Handler(void);

/* USER CODE BEGIN EFP */

/* USER CODE END EFP */

/* Private defines -----------------------------------------------------------*/
#define LED2_Pin GPIO_PIN_5
#define LED2_GPIO_Port GPIOA

#define AB_EN_Pin GPIO_PIN_2
#define AB_EN_GPIO_Port GPIOB
#define B_STEP_Pin GPIO_PIN_8
#define B_STEP_GPIO_Port GPIOA
#define B_DIR_Pin GPIO_PIN_7
#define B_DIR_GPIO_Port GPIOC
#define B_MIN_ENDSTOP_Pin GPIO_PIN_4
#define B_MIN_ENDSTOP_GPIO_Port GPIOB
#define B_MAX_ENDSTOP_Pin GPIO_PIN_5
#define B_MAX_ENDSTOP_GPIO_Port GPIOB

#define TMC_UART_TX_Pin GPIO_PIN_10
#define TMC_UART_TX_GPIO_Port GPIOB
#define TMC_UART_RX_Pin GPIO_PIN_11
#define TMC_UART_RX_GPIO_Port GPIOB

/* USER CODE BEGIN Private defines */

/* USER CODE END Private defines */

#ifdef __cplusplus
}
#endif

#endif /* __MAIN_H */
