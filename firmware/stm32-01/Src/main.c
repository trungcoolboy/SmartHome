/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file    UART/UART_Printf/Src/main.c
  * @author  MCD Application Team
  * @brief   This example shows how to retarget the C library printf function
  *          to the UART.
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
/* Includes ------------------------------------------------------------------*/
#include "main.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include <stdbool.h>
#include <string.h>
#include <stdlib.h>
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */

/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
UART_HandleTypeDef huart1;

/* USER CODE BEGIN PV */
typedef struct
{
  const char *group;
  const char *key;
  GPIO_TypeDef *port;
  uint16_t pin;
  uint8_t active_low;
  uint8_t auto_mode;
  uint8_t output_on;
} ControlChannel;

static ControlChannel pump_channels[] = {
  {"pump", "in", IN_PUMP_GPIO_Port, IN_PUMP_Pin, 1U, 1U, 0U},
  {"pump", "out", OUT_PUMP_GPIO_Port, OUT_PUMP_Pin, 1U, 1U, 0U},
  {"pump", "circulation", CIRCULATION_PUMP_GPIO_Port, CIRCULATION_PUMP_Pin, 1U, 1U, 0U},
  {"pump", "middle", MIDDLE_PUMP_GPIO_Port, MIDDLE_PUMP_Pin, 1U, 1U, 0U},
  {"pump", "filter", FILTER_PUMP_GPIO_Port, FILTER_PUMP_Pin, 0U, 1U, 0U},
  {"pump", "drain", DRAIN_PUMP_GPIO_Port, DRAIN_PUMP_Pin, 0U, 1U, 0U},
};

static ControlChannel misc_channels[] = {
  {"misc", "oxygen", OXYGEN_RELAY_GPIO_Port, OXYGEN_RELAY_Pin, 0U, 1U, 0U},
  {"misc", "co2", CO2_RELAY_GPIO_Port, CO2_RELAY_Pin, 0U, 1U, 0U},
  {"misc", "heater", TANK_HEATER_RELAY_GPIO_Port, TANK_HEATER_RELAY_Pin, 0U, 1U, 0U},
  {"misc", "pretreat_heater", PRETREAT_HEATER_RELAY_GPIO_Port, PRETREAT_HEATER_RELAY_Pin, 1U, 1U, 0U},
  {"misc", "inlet", WATER_INLET_RELAY_GPIO_Port, WATER_INLET_RELAY_Pin, 1U, 1U, 0U},
};

typedef struct
{
  const char *key;
  GPIO_TypeDef *port;
  uint16_t pin;
} SensorChannel;

static const SensorChannel water_level_sensors[] = {
  {"tank_low", TANK_LOW_SENSOR_GPIO_Port, TANK_LOW_SENSOR_Pin},
  {"tank_normal", TANK_NORMAL_SENSOR_GPIO_Port, TANK_NORMAL_SENSOR_Pin},
  {"tank_high", TANK_HIGH_SENSOR_GPIO_Port, TANK_HIGH_SENSOR_Pin},
  {"inlet_low", INLET_LOW_SENSOR_GPIO_Port, INLET_LOW_SENSOR_Pin},
  {"inlet_high", INLET_HIGH_SENSOR_GPIO_Port, INLET_HIGH_SENSOR_Pin},
  {"pretreat_low", PRETREAT_LOW_SENSOR_GPIO_Port, PRETREAT_LOW_SENSOR_Pin},
  {"pretreat_high", PRETREAT_HIGH_SENSOR_GPIO_Port, PRETREAT_HIGH_SENSOR_Pin},
  {"waste_low", WASTE_LOW_SENSOR_GPIO_Port, WASTE_LOW_SENSOR_Pin},
  {"waste_high", WASTE_HIGH_SENSOR_GPIO_Port, WASTE_HIGH_SENSOR_Pin},
};
/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
static void MX_GPIO_Init(void);
static void MX_USART1_UART_Init(void);
/* USER CODE BEGIN PFP */
#if defined(__ICCARM__)
/* New definition from EWARM V9, compatible with EWARM8 */
int iar_fputc(int ch);
#define PUTCHAR_PROTOTYPE int iar_fputc(int ch)
#elif defined ( __CC_ARM ) || defined(__ARMCC_VERSION)
/* ARM Compiler 5/6*/
#define PUTCHAR_PROTOTYPE int fputc(int ch, FILE *f)
#elif defined(__GNUC__)
#define PUTCHAR_PROTOTYPE int __io_putchar(int ch)
#endif /* __ICCARM__ */

static void uart_write_line(const char *text);
static void apply_relay_outputs(void);
static void emit_control_state(const ControlChannel *channel);
static void emit_sensor_state(const SensorChannel *sensor);
static ControlChannel *find_channel(const char *group, const char *key);
static void process_command_line(char *line);
/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */
static void uart_write_line(const char *text)
{
  printf("%s\r\n", text);
}

static void apply_relay_outputs(void)
{
  size_t i;
  GPIO_PinState pin_state;

  for (i = 0U; i < sizeof(pump_channels) / sizeof(pump_channels[0]); i++)
  {
    pin_state = pump_channels[i].output_on
      ? (pump_channels[i].active_low ? GPIO_PIN_RESET : GPIO_PIN_SET)
      : (pump_channels[i].active_low ? GPIO_PIN_SET : GPIO_PIN_RESET);
    HAL_GPIO_WritePin(
      pump_channels[i].port,
      pump_channels[i].pin,
      pin_state
    );
  }

  for (i = 0U; i < sizeof(misc_channels) / sizeof(misc_channels[0]); i++)
  {
    pin_state = misc_channels[i].output_on
      ? (misc_channels[i].active_low ? GPIO_PIN_RESET : GPIO_PIN_SET)
      : (misc_channels[i].active_low ? GPIO_PIN_SET : GPIO_PIN_RESET);
    HAL_GPIO_WritePin(
      misc_channels[i].port,
      misc_channels[i].pin,
      pin_state
    );
  }
}

static void emit_control_state(const ControlChannel *channel)
{
  printf(
    "state %s %s mode %s output %s\r\n",
    channel->group,
    channel->key,
    channel->auto_mode ? "auto" : "manual",
    channel->output_on ? "on" : "off"
  );
}

static void emit_sensor_state(const SensorChannel *sensor)
{
  const GPIO_PinState pin_state = HAL_GPIO_ReadPin(sensor->port, sensor->pin);
  printf(
    "sensor %s %s\r\n",
    sensor->key,
    (pin_state == GPIO_PIN_RESET) ? "wet" : "dry"
  );
}

static ControlChannel *find_channel(const char *group, const char *key)
{
  size_t i;
  ControlChannel *channels = NULL;
  size_t count = 0U;

  if (strcmp(group, "pump") == 0)
  {
    channels = pump_channels;
    count = sizeof(pump_channels) / sizeof(pump_channels[0]);
  }
  else if (strcmp(group, "misc") == 0)
  {
    channels = misc_channels;
    count = sizeof(misc_channels) / sizeof(misc_channels[0]);
  }
  else
  {
    return NULL;
  }

  for (i = 0U; i < count; i++)
  {
    if (strcmp(channels[i].key, key) == 0)
    {
      return &channels[i];
    }
  }

  return NULL;
}

static void process_command_line(char *line)
{
  char *tokens[5] = {0};
  size_t token_count = 0U;
  char *cursor = strtok(line, " ");

  while (cursor != NULL && token_count < 5U)
  {
    tokens[token_count++] = cursor;
    cursor = strtok(NULL, " ");
  }

  if (token_count == 0U)
  {
    return;
  }

  if (strcmp(tokens[0], "status") == 0)
  {
    size_t i;
    for (i = 0U; i < sizeof(pump_channels) / sizeof(pump_channels[0]); i++)
    {
      emit_control_state(&pump_channels[i]);
    }
    for (i = 0U; i < sizeof(misc_channels) / sizeof(misc_channels[0]); i++)
    {
      emit_control_state(&misc_channels[i]);
    }
    for (i = 0U; i < sizeof(water_level_sensors) / sizeof(water_level_sensors[0]); i++)
    {
      emit_sensor_state(&water_level_sensors[i]);
    }
    return;
  }

  if (token_count < 3U)
  {
    uart_write_line("err invalid command");
    return;
  }

  ControlChannel *channel = find_channel(tokens[0], tokens[1]);
  if (channel == NULL)
  {
    uart_write_line("err unknown channel");
    return;
  }

  if (strcmp(tokens[2], "mode") == 0)
  {
    if (token_count < 4U)
    {
      uart_write_line("err missing mode");
      return;
    }

    if (strcmp(tokens[3], "auto") == 0)
    {
      channel->auto_mode = 1U;
      channel->output_on = 0U;
      apply_relay_outputs();
      printf("ok %s %s mode auto\r\n", channel->group, channel->key);
      emit_control_state(channel);
      return;
    }

    if (strcmp(tokens[3], "manual") == 0)
    {
      channel->auto_mode = 0U;
      channel->output_on = 0U;
      apply_relay_outputs();
      printf("ok %s %s mode manual\r\n", channel->group, channel->key);
      emit_control_state(channel);
      return;
    }

    uart_write_line("err invalid mode");
    return;
  }

  if (strcmp(tokens[2], "on") == 0 || strcmp(tokens[2], "off") == 0)
  {
    if (channel->auto_mode != 0U)
    {
      uart_write_line("err channel is in auto mode");
      return;
    }

    channel->output_on = (strcmp(tokens[2], "on") == 0) ? 1U : 0U;
    apply_relay_outputs();
    printf("ok %s %s %s\r\n", channel->group, channel->key, channel->output_on ? "on" : "off");
    emit_control_state(channel);
    return;
  }

  uart_write_line("err unsupported command");
}

/* USER CODE END 0 */

/**
  * @brief  The application entry point.
  * @retval int
  */
int main(void)
{
  /* USER CODE BEGIN 1 */

  /* STM32G4xx HAL library initialization:
       - Configure the Flash prefetch
       - Systick timer is configured by default as source of time base, but user
         can eventually implement his proper time base source (a general purpose
         timer for example or other time source), keeping in mind that Time base
         duration should be kept 1ms since PPP_TIMEOUT_VALUEs are defined and
         handled in milliseconds basis.
       - Set NVIC Group Priority to 4
       - Low Level Initialization
     */
  /* USER CODE END 1 */

  /* MCU Configuration--------------------------------------------------------*/

  /* Reset of all peripherals, Initializes the Flash interface and the Systick. */
  HAL_Init();

  /* USER CODE BEGIN Init */

  /* USER CODE END Init */

  /* Configure the system clock */
  SystemClock_Config();

  /* USER CODE BEGIN SysInit */
  /* USER CODE END SysInit */

  /* Initialize all configured peripherals */
  MX_GPIO_Init();
  MX_USART1_UART_Init();
  /* USER CODE BEGIN 2 */

  printf("\r\nSTM32G431RB #01 boot\r\n");
  printf("USART1 on PC4/PC5 via CN10-35/37, 115200 8N1\r\n");
  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  while (1)
  {
    uint8_t rx_byte = 0;
    static char rx_line[96];
    static size_t rx_len = 0U;
    static uint32_t last_led_toggle_ms = 0U;
    const uint32_t now_ms = HAL_GetTick();

    if (__HAL_UART_GET_FLAG(&huart1, UART_FLAG_ORE))
    {
      __HAL_UART_CLEAR_OREFLAG(&huart1);
    }
    if (__HAL_UART_GET_FLAG(&huart1, UART_FLAG_FE))
    {
      __HAL_UART_CLEAR_FEFLAG(&huart1);
    }
    if (__HAL_UART_GET_FLAG(&huart1, UART_FLAG_NE))
    {
      __HAL_UART_CLEAR_NEFLAG(&huart1);
    }

    while (HAL_UART_Receive(&huart1, &rx_byte, 1, 1) == HAL_OK)
    {
      if (rx_byte == '\r' || rx_byte == '\n')
      {
        if (rx_len > 0U)
        {
          printf("cmd %.*s\r\n", (int)rx_len, rx_line);
          rx_line[rx_len] = '\0';
          process_command_line(rx_line);
          rx_len = 0U;
        }
      }
      else if (rx_len < (sizeof(rx_line) - 1U))
      {
        rx_line[rx_len++] = (char)rx_byte;
      }
      else
      {
        rx_len = 0U;
        uart_write_line("err command too long");
      }
    }

    if (now_ms - last_led_toggle_ms >= 250U)
    {
      HAL_GPIO_TogglePin(LED2_GPIO_Port, LED2_Pin);
      last_led_toggle_ms = now_ms;
    }
    /* USER CODE END WHILE */

    /* USER CODE BEGIN 3 */

  }
  /* USER CODE END 3 */
}

/**
  * @brief System Clock Configuration
  * @retval None
  */
void SystemClock_Config(void)
{
  RCC_OscInitTypeDef RCC_OscInitStruct = {0};
  RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

  /** Configure the main internal regulator output voltage
  */
  HAL_PWREx_ControlVoltageScaling(PWR_REGULATOR_VOLTAGE_SCALE1_BOOST);

  /** Initializes the RCC Oscillators according to the specified parameters
  * in the RCC_OscInitTypeDef structure.
  */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSI;
  RCC_OscInitStruct.HSIState = RCC_HSI_ON;
  RCC_OscInitStruct.HSICalibrationValue = RCC_HSICALIBRATION_DEFAULT;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
  RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSI;
  RCC_OscInitStruct.PLL.PLLM = RCC_PLLM_DIV4;
  RCC_OscInitStruct.PLL.PLLN = 85;
  RCC_OscInitStruct.PLL.PLLP = RCC_PLLP_DIV2;
  RCC_OscInitStruct.PLL.PLLQ = RCC_PLLQ_DIV2;
  RCC_OscInitStruct.PLL.PLLR = RCC_PLLR_DIV2;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    Error_Handler();
  }

  /** Initializes the CPU, AHB and APB buses clocks
  */
  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK
                              |RCC_CLOCKTYPE_PCLK1|RCC_CLOCKTYPE_PCLK2;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV1;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_4) != HAL_OK)
  {
    Error_Handler();
  }
}

/**
  * @brief LPUART1 Initialization Function
  * @param None
  * @retval None
  */
static void MX_USART1_UART_Init(void)
{
  huart1.Instance = USART1;
  huart1.Init.BaudRate = 115200;
  huart1.Init.WordLength = UART_WORDLENGTH_8B;
  huart1.Init.StopBits = UART_STOPBITS_1;
  huart1.Init.Parity = UART_PARITY_NONE;
  huart1.Init.Mode = UART_MODE_TX_RX;
  huart1.Init.HwFlowCtl = UART_HWCONTROL_NONE;
  huart1.Init.OverSampling = UART_OVERSAMPLING_16;
  huart1.Init.OneBitSampling = UART_ONE_BIT_SAMPLE_DISABLE;
  huart1.Init.ClockPrescaler = UART_PRESCALER_DIV1;
  huart1.AdvancedInit.AdvFeatureInit = UART_ADVFEATURE_NO_INIT;
  if (HAL_UART_Init(&huart1) != HAL_OK)
  {
    Error_Handler();
  }
  if (HAL_UARTEx_SetTxFifoThreshold(&huart1, UART_TXFIFO_THRESHOLD_1_8) != HAL_OK)
  {
    Error_Handler();
  }
  if (HAL_UARTEx_SetRxFifoThreshold(&huart1, UART_RXFIFO_THRESHOLD_1_8) != HAL_OK)
  {
    Error_Handler();
  }
  if (HAL_UARTEx_DisableFifoMode(&huart1) != HAL_OK)
  {
    Error_Handler();
  }
}

/**
  * @brief GPIO Initialization Function
  * @param None
  * @retval None
  */
static void MX_GPIO_Init(void)
{
  GPIO_InitTypeDef GPIO_InitStruct = {0};
/* USER CODE BEGIN MX_GPIO_Init_1 */
/* USER CODE END MX_GPIO_Init_1 */

  /* GPIO Ports Clock Enable */
  __HAL_RCC_GPIOA_CLK_ENABLE();
  __HAL_RCC_GPIOB_CLK_ENABLE();
  __HAL_RCC_GPIOC_CLK_ENABLE();

  HAL_GPIO_WritePin(LED2_GPIO_Port, LED2_Pin, GPIO_PIN_RESET);
  HAL_GPIO_WritePin(GPIOB, IN_PUMP_Pin|OUT_PUMP_Pin|CIRCULATION_PUMP_Pin|MIDDLE_PUMP_Pin, GPIO_PIN_SET);
  HAL_GPIO_WritePin(GPIOB, FILTER_PUMP_Pin|DRAIN_PUMP_Pin|OXYGEN_RELAY_Pin|CO2_RELAY_Pin|TANK_HEATER_RELAY_Pin, GPIO_PIN_RESET);
  HAL_GPIO_WritePin(GPIOC, PRETREAT_HEATER_RELAY_Pin|WATER_INLET_RELAY_Pin, GPIO_PIN_SET);

  GPIO_InitStruct.Pin = LED2_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(LED2_GPIO_Port, &GPIO_InitStruct);

  GPIO_InitStruct.Pin = IN_PUMP_Pin|OUT_PUMP_Pin|CIRCULATION_PUMP_Pin|MIDDLE_PUMP_Pin|FILTER_PUMP_Pin|
                        DRAIN_PUMP_Pin|OXYGEN_RELAY_Pin|CO2_RELAY_Pin|TANK_HEATER_RELAY_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);

  GPIO_InitStruct.Pin = PRETREAT_HEATER_RELAY_Pin|WATER_INLET_RELAY_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GPIOC, &GPIO_InitStruct);

  GPIO_InitStruct.Pin = TANK_LOW_SENSOR_Pin|TANK_NORMAL_SENSOR_Pin|TANK_HIGH_SENSOR_Pin|
                        INLET_LOW_SENSOR_Pin|INLET_HIGH_SENSOR_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
  GPIO_InitStruct.Pull = GPIO_PULLUP;
  HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

  GPIO_InitStruct.Pin = WASTE_LOW_SENSOR_Pin|WASTE_HIGH_SENSOR_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
  GPIO_InitStruct.Pull = GPIO_PULLUP;
  HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);

  GPIO_InitStruct.Pin = PRETREAT_LOW_SENSOR_Pin|PRETREAT_HIGH_SENSOR_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
  GPIO_InitStruct.Pull = GPIO_PULLUP;
  HAL_GPIO_Init(GPIOC, &GPIO_InitStruct);

/* USER CODE BEGIN MX_GPIO_Init_2 */
/* USER CODE END MX_GPIO_Init_2 */
}

/* USER CODE BEGIN 4 */
/**
  * @brief  Retargets the C library __write function to the IAR function iar_fputc.
  * @param  file: file descriptor.
  * @param  ptr: pointer to the buffer where the data is stored.
  * @param  len: length of the data to write in bytes.
  * @retval length of the written data in bytes.
  */
#if defined(__ICCARM__)
size_t __write(int file, unsigned char const *ptr, size_t len)
{
  size_t idx;
  unsigned char const *pdata = ptr;

  for (idx = 0; idx < len; idx++)
  {
    iar_fputc((int)*pdata);
    pdata++;
  }
  return len;
}
#endif /* __ICCARM__ */

/**
  * @brief  Retargets the C library printf function to the USART.
  */
PUTCHAR_PROTOTYPE
{
  /* Place your implementation of fputc here */
  /* e.g. write a character to the LPUART1 and Loop until the end of transmission */
  HAL_UART_Transmit(&huart1, (uint8_t *)&ch, 1, 0xFFFF);

  return ch;
}

/* USER CODE END 4 */

/**
  * @brief  This function is executed in case of error occurrence.
  * @retval None
  */
void Error_Handler(void)
{
  /* USER CODE BEGIN Error_Handler_Debug */
  /* User can add his own implementation to report the HAL error return state */
  while (1)
  {
    HAL_GPIO_TogglePin(LED2_GPIO_Port, LED2_Pin);
    HAL_Delay(500);
  }
  /* USER CODE END Error_Handler_Debug */
}

#ifdef  USE_FULL_ASSERT
/**
  * @brief  Reports the name of the source file and the source line number
  *         where the assert_param error has occurred.
  * @param  file: pointer to the source file name
  * @param  line: assert_param error line source number
  * @retval None
  */
void assert_failed(uint8_t *file, uint32_t line)
{
  /* USER CODE BEGIN 6 */
  /* User can add his own implementation to report the file name and line number,
    ex: printf("Wrong parameters value: file %s on line %d\r\n", file, line) */

  /* Infinite loop */
  while (1)
  {
  }
  /* USER CODE END 6 */
}
#endif /* USE_FULL_ASSERT */
