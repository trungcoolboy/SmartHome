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
#define IN_PUMP_Pin GPIO_PIN_0
#define IN_PUMP_GPIO_Port GPIOB
#define OUT_PUMP_Pin GPIO_PIN_1
#define OUT_PUMP_GPIO_Port GPIOB
#define CIRCULATION_PUMP_Pin GPIO_PIN_2
#define CIRCULATION_PUMP_GPIO_Port GPIOB
#define MIDDLE_PUMP_Pin GPIO_PIN_10
#define MIDDLE_PUMP_GPIO_Port GPIOB
#define FILTER_PUMP_Pin GPIO_PIN_11
#define FILTER_PUMP_GPIO_Port GPIOB
#define DRAIN_PUMP_Pin GPIO_PIN_12
#define DRAIN_PUMP_GPIO_Port GPIOB
#define OXYGEN_RELAY_Pin GPIO_PIN_13
#define OXYGEN_RELAY_GPIO_Port GPIOB
#define CO2_RELAY_Pin GPIO_PIN_14
#define CO2_RELAY_GPIO_Port GPIOB
#define TANK_HEATER_RELAY_Pin GPIO_PIN_15
#define TANK_HEATER_RELAY_GPIO_Port GPIOB
#define PRETREAT_HEATER_RELAY_Pin GPIO_PIN_6
#define PRETREAT_HEATER_RELAY_GPIO_Port GPIOC
#define WATER_INLET_RELAY_Pin GPIO_PIN_7
#define WATER_INLET_RELAY_GPIO_Port GPIOC

#define TANK_LOW_SENSOR_Pin GPIO_PIN_0
#define TANK_LOW_SENSOR_GPIO_Port GPIOA
#define TANK_NORMAL_SENSOR_Pin GPIO_PIN_1
#define TANK_NORMAL_SENSOR_GPIO_Port GPIOA
#define TANK_HIGH_SENSOR_Pin GPIO_PIN_4
#define TANK_HIGH_SENSOR_GPIO_Port GPIOA
#define INLET_LOW_SENSOR_Pin GPIO_PIN_7
#define INLET_LOW_SENSOR_GPIO_Port GPIOA
#define INLET_HIGH_SENSOR_Pin GPIO_PIN_6
#define INLET_HIGH_SENSOR_GPIO_Port GPIOA
#define PRETREAT_LOW_SENSOR_Pin GPIO_PIN_10
#define PRETREAT_LOW_SENSOR_GPIO_Port GPIOC
#define PRETREAT_HIGH_SENSOR_Pin GPIO_PIN_11
#define PRETREAT_HIGH_SENSOR_GPIO_Port GPIOC
#define WASTE_LOW_SENSOR_Pin GPIO_PIN_8
#define WASTE_LOW_SENSOR_GPIO_Port GPIOB
#define WASTE_HIGH_SENSOR_Pin GPIO_PIN_9
#define WASTE_HIGH_SENSOR_GPIO_Port GPIOB

/* USER CODE BEGIN Private defines */

/* USER CODE END Private defines */

#ifdef __cplusplus
}
#endif

#endif /* __MAIN_H */
