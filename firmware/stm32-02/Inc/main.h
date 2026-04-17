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

#define A_STEP_Pin GPIO_PIN_0
#define A_STEP_GPIO_Port GPIOB
#define A_DIR_Pin GPIO_PIN_1
#define A_DIR_GPIO_Port GPIOB
#define AB_EN_Pin GPIO_PIN_2
#define AB_EN_GPIO_Port GPIOB
#define B_STEP_Pin GPIO_PIN_8
#define B_STEP_GPIO_Port GPIOA
#define B_DIR_Pin GPIO_PIN_7
#define B_DIR_GPIO_Port GPIOC
#define A_MIN_ENDSTOP_Pin GPIO_PIN_9
#define A_MIN_ENDSTOP_GPIO_Port GPIOA
#define A_MAX_ENDSTOP_Pin GPIO_PIN_10
#define A_MAX_ENDSTOP_GPIO_Port GPIOA
#define B_MIN_ENDSTOP_Pin GPIO_PIN_4
#define B_MIN_ENDSTOP_GPIO_Port GPIOB
#define B_MAX_ENDSTOP_Pin GPIO_PIN_5
#define B_MAX_ENDSTOP_GPIO_Port GPIOB

#define TMC_UART_TX_Pin GPIO_PIN_10
#define TMC_UART_TX_GPIO_Port GPIOB
#define TMC_UART_RX_Pin GPIO_PIN_11
#define TMC_UART_RX_GPIO_Port GPIOB

#define FAN1_SERVO_Pin GPIO_PIN_6
#define FAN1_SERVO_GPIO_Port GPIOA
#define FAN2_SERVO_Pin GPIO_PIN_7
#define FAN2_SERVO_GPIO_Port GPIOA
#define PAN1_SERVO_Pin GPIO_PIN_6
#define PAN1_SERVO_GPIO_Port GPIOB
#define PAN2_SERVO_Pin GPIO_PIN_1
#define PAN2_SERVO_GPIO_Port GPIOA
#define LID_SERVO_Pin GPIO_PIN_4
#define LID_SERVO_GPIO_Port GPIOA

#define INTEL_FAN1_PWM_Pin GPIO_PIN_6
#define INTEL_FAN1_PWM_GPIO_Port GPIOC
#define INTEL_FAN2_PWM_Pin GPIO_PIN_8
#define INTEL_FAN2_PWM_GPIO_Port GPIOC
#define INTEL_FAN1_TACH_Pin GPIO_PIN_9
#define INTEL_FAN1_TACH_GPIO_Port GPIOC
#define INTEL_FAN2_TACH_Pin GPIO_PIN_7
#define INTEL_FAN2_TACH_GPIO_Port GPIOB

#define OLED_SCL_Pin GPIO_PIN_8
#define OLED_SCL_GPIO_Port GPIOB
#define OLED_SDA_Pin GPIO_PIN_9
#define OLED_SDA_GPIO_Port GPIOB

#define BYJ1_STEP_Pin GPIO_PIN_12
#define BYJ1_STEP_GPIO_Port GPIOB
#define BYJ1_DIR_Pin GPIO_PIN_13
#define BYJ1_DIR_GPIO_Port GPIOB
#define BYJ1_EN_Pin GPIO_PIN_14
#define BYJ1_EN_GPIO_Port GPIOB
#define BYJ1_ENDSTOP_Pin GPIO_PIN_3
#define BYJ1_ENDSTOP_GPIO_Port GPIOB

#define BYJ2_STEP_Pin GPIO_PIN_15
#define BYJ2_STEP_GPIO_Port GPIOB
#define BYJ2_DIR_Pin GPIO_PIN_10
#define BYJ2_DIR_GPIO_Port GPIOC
#define BYJ2_EN_Pin GPIO_PIN_11
#define BYJ2_EN_GPIO_Port GPIOC

#define MAGNET_PWM_Pin GPIO_PIN_11
#define MAGNET_PWM_GPIO_Port GPIOA

#define FAN1_POWER_RELAY_Pin GPIO_PIN_12
#define FAN1_POWER_RELAY_GPIO_Port GPIOA
#define FAN2_POWER_RELAY_Pin GPIO_PIN_1
#define FAN2_POWER_RELAY_GPIO_Port GPIOC

/* USER CODE BEGIN Private defines */

/* USER CODE END Private defines */

#ifdef __cplusplus
}
#endif

#endif /* __MAIN_H */
