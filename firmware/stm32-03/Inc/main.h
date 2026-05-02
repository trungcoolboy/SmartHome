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

void Error_Handler(void);

/* USER CODE BEGIN EFP */

/* USER CODE END EFP */

/* Private defines -----------------------------------------------------------*/
#define LED2_Pin GPIO_PIN_5
#define LED2_GPIO_Port GPIOA

/* USER CODE BEGIN Private defines */
#define X_STEP_Pin GPIO_PIN_0
#define X_STEP_GPIO_Port GPIOB
#define X_DIR_Pin GPIO_PIN_1
#define X_DIR_GPIO_Port GPIOB
#define X_EN_Pin GPIO_PIN_2
#define X_EN_GPIO_Port GPIOB
#define X_MIN_ENDSTOP_Pin GPIO_PIN_9
#define X_MIN_ENDSTOP_GPIO_Port GPIOA
#define X_MAX_ENDSTOP_Pin GPIO_PIN_0
#define X_MAX_ENDSTOP_GPIO_Port GPIOC

#define Y_STEP_Pin GPIO_PIN_8
#define Y_STEP_GPIO_Port GPIOA
#define Y_DIR_Pin GPIO_PIN_7
#define Y_DIR_GPIO_Port GPIOC
#define Y_EN_Pin GPIO_PIN_12
#define Y_EN_GPIO_Port GPIOB
#define Y_MIN_ENDSTOP_Pin GPIO_PIN_4
#define Y_MIN_ENDSTOP_GPIO_Port GPIOB
#define Y_MAX_ENDSTOP_Pin GPIO_PIN_5
#define Y_MAX_ENDSTOP_GPIO_Port GPIOB

#define Z_STEP_Pin GPIO_PIN_6
#define Z_STEP_GPIO_Port GPIOB
#define Z_DIR_Pin GPIO_PIN_1
#define Z_DIR_GPIO_Port GPIOA
#define Z_EN_Pin GPIO_PIN_13
#define Z_EN_GPIO_Port GPIOB
#define Z_MIN_ENDSTOP_Pin GPIO_PIN_10
#define Z_MIN_ENDSTOP_GPIO_Port GPIOA
#define Z_MAX_ENDSTOP_Pin GPIO_PIN_11
#define Z_MAX_ENDSTOP_GPIO_Port GPIOA

#define LED_PWM_1_Pin GPIO_PIN_4
#define LED_PWM_1_GPIO_Port GPIOA
#define LED_PWM_2_Pin GPIO_PIN_6
#define LED_PWM_2_GPIO_Port GPIOA
#define LED_PWM_3_Pin GPIO_PIN_7
#define LED_PWM_3_GPIO_Port GPIOA
#define LED_PWM_4_Pin GPIO_PIN_12
#define LED_PWM_4_GPIO_Port GPIOA
#define LED_PWM_5_Pin GPIO_PIN_15
#define LED_PWM_5_GPIO_Port GPIOA
#define LED_PWM_6_Pin GPIO_PIN_3
#define LED_PWM_6_GPIO_Port GPIOB
#define LED_PWM_7_Pin GPIO_PIN_7
#define LED_PWM_7_GPIO_Port GPIOB
#define LED_PWM_8_Pin GPIO_PIN_8
#define LED_PWM_8_GPIO_Port GPIOB
#define LED_PWM_9_Pin GPIO_PIN_9
#define LED_PWM_9_GPIO_Port GPIOB
#define LED_PWM_10_Pin GPIO_PIN_10
#define LED_PWM_10_GPIO_Port GPIOB
#define LED_PWM_11_Pin GPIO_PIN_11
#define LED_PWM_11_GPIO_Port GPIOB

#define LEDFAN_PWM_1_Pin GPIO_PIN_14
#define LEDFAN_PWM_1_GPIO_Port GPIOB
#define LEDFAN_PWM_2_Pin GPIO_PIN_15
#define LEDFAN_PWM_2_GPIO_Port GPIOB

#define LED_SINK_NTC_Pin GPIO_PIN_0
#define LED_SINK_NTC_GPIO_Port GPIOA

/* USER CODE END Private defines */

#ifdef __cplusplus
}
#endif

#endif /* __MAIN_H */
