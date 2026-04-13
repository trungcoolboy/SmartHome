/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file    UART/UART_Printf/Src/main.c
  * @brief   STM32 #02 UART motion controller shell for AB axis.
  ******************************************************************************
  */
/* USER CODE END Header */
/* Includes ------------------------------------------------------------------*/
#include "main.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include <stdio.h>
#include <stdbool.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>
/* USER CODE END Includes */

/* Private variables ---------------------------------------------------------*/
UART_HandleTypeDef huart1;
UART_HandleTypeDef huart2;

/* USER CODE BEGIN PV */
typedef struct
{
  const char *key;
  GPIO_TypeDef *step_port;
  uint16_t step_pin;
  GPIO_TypeDef *dir_port;
  uint16_t dir_pin;
  uint8_t driver_addr;
  uint8_t enabled;
  int32_t position;
  int32_t target;
  int32_t velocity;
  uint8_t moving;
  uint8_t homed;
} AxisState;

static AxisState axes[] = {
  {"a", A_STEP_GPIO_Port, A_STEP_Pin, A_DIR_GPIO_Port, A_DIR_Pin, 0U, 0U, 0, 0, 0, 0U, 0U},
  {"b", B_STEP_GPIO_Port, B_STEP_Pin, B_DIR_GPIO_Port, B_DIR_Pin, 1U, 0U, 0, 0, 0, 0U, 0U},
};
/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
static void MX_GPIO_Init(void);
static void MX_USART1_UART_Init(void);
static void MX_USART2_UART_Init(void);
/* USER CODE BEGIN PFP */
#if defined(__ICCARM__)
int iar_fputc(int ch);
#define PUTCHAR_PROTOTYPE int iar_fputc(int ch)
#elif defined ( __CC_ARM ) || defined(__ARMCC_VERSION)
#define PUTCHAR_PROTOTYPE int fputc(int ch, FILE *f)
#elif defined(__GNUC__)
#define PUTCHAR_PROTOTYPE int __io_putchar(int ch)
#endif

static void uart_write_line(const char *text);
static AxisState *find_axis(const char *key);
static void emit_axis_state(const AxisState *axis);
static void emit_all_axis_states(void);
static void emit_tmc_status(void);
static void apply_axis_enable_state(void);
static void pulse_axis_step(AxisState *axis, int32_t direction);
static uint8_t tmc_crc8(const uint8_t *bytes, uint8_t len);
static HAL_StatusTypeDef tmc_uart_send_bytes(const uint8_t *bytes, uint16_t len);
static int16_t tmc_uart_read_byte(uint32_t timeout_ms);
static void tmc_uart_flush_rx(void);
static void tmc_uart_dump_raw(uint32_t window_ms);
static HAL_StatusTypeDef tmc_uart_read_reg(uint8_t driver_addr, uint8_t reg_addr, uint32_t *value_out);
static HAL_StatusTypeDef tmc_uart_write_reg(uint8_t driver_addr, uint8_t reg_addr, uint32_t reg_value);
static void tmc_emit_driver_probe(uint8_t driver_addr);
static void process_command_line(char *line);
static void tick_axes(void);
/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */
static void uart_write_line(const char *text)
{
  printf("%s\r\n", text);
}

static AxisState *find_axis(const char *key)
{
  size_t i;
  for (i = 0U; i < sizeof(axes) / sizeof(axes[0]); i++)
  {
    if (strcmp(axes[i].key, key) == 0)
    {
      return &axes[i];
    }
  }
  return NULL;
}

static void emit_axis_state(const AxisState *axis)
{
  printf(
    "axis %s enabled %s moving %s pos %ld target %ld vel %ld homed %s\r\n",
    axis->key,
    axis->enabled ? "on" : "off",
    axis->moving ? "on" : "off",
    (long)axis->position,
    (long)axis->target,
    (long)axis->velocity,
    axis->homed ? "yes" : "no"
  );
}

static void emit_all_axis_states(void)
{
  size_t i;
  for (i = 0U; i < sizeof(axes) / sizeof(axes[0]); i++)
  {
    emit_axis_state(&axes[i]);
  }
}

static void emit_tmc_status(void)
{
  printf("tmc uart usart2 half_duplex PA2 ab_en active_low addr_a %u addr_b %u\r\n",
         (unsigned)axes[0].driver_addr,
         (unsigned)axes[1].driver_addr);
}

static void apply_axis_enable_state(void)
{
  size_t i;
  uint8_t any_enabled = 0U;
  for (i = 0U; i < sizeof(axes) / sizeof(axes[0]); i++)
  {
    if (axes[i].enabled)
    {
      any_enabled = 1U;
      break;
    }
  }

  HAL_GPIO_WritePin(AB_EN_GPIO_Port, AB_EN_Pin, any_enabled ? GPIO_PIN_RESET : GPIO_PIN_SET);
}

static void pulse_axis_step(AxisState *axis, int32_t direction)
{
  HAL_GPIO_WritePin(axis->dir_port, axis->dir_pin, (direction >= 0) ? GPIO_PIN_SET : GPIO_PIN_RESET);
  HAL_GPIO_WritePin(axis->step_port, axis->step_pin, GPIO_PIN_SET);
  HAL_Delay(2U);
  HAL_GPIO_WritePin(axis->step_port, axis->step_pin, GPIO_PIN_RESET);
  HAL_Delay(2U);
}

static uint8_t tmc_crc8(const uint8_t *bytes, uint8_t len)
{
  uint8_t crc = 0U;
  uint8_t i;

  for (i = 0U; i < len; i++)
  {
    uint8_t current = bytes[i];
    uint8_t bit;
    for (bit = 0U; bit < 8U; bit++)
    {
      if (((crc >> 7) ^ (current & 0x01U)) != 0U)
      {
        crc = (uint8_t)((crc << 1) ^ 0x07U);
      }
      else
      {
        crc = (uint8_t)(crc << 1);
      }
      current >>= 1;
    }
  }

  return crc;
}

static HAL_StatusTypeDef tmc_uart_send_bytes(const uint8_t *bytes, uint16_t len)
{
  if (len == 0U)
  {
    return HAL_OK;
  }
  return HAL_UART_Transmit(&huart2, (uint8_t *)bytes, len, 50U);
}

static int16_t tmc_uart_read_byte(uint32_t timeout_ms)
{
  uint8_t byte = 0U;
  if (HAL_UART_Receive(&huart2, &byte, 1U, timeout_ms) == HAL_OK)
  {
    return (int16_t)byte;
  }
  return -1;
}

static void tmc_uart_flush_rx(void)
{
  while (tmc_uart_read_byte(1U) >= 0) {}
}

static void tmc_uart_dump_raw(uint32_t window_ms)
{
  uint32_t start_ms = HAL_GetTick();
  uint8_t count = 0U;
  printf("tmc raw");
  while ((HAL_GetTick() - start_ms) < window_ms)
  {
    int16_t res = tmc_uart_read_byte(2U);
    if (res < 0)
    {
      continue;
    }
    printf(" %02X", (unsigned)(res & 0xFF));
    count++;
  }
  if (count == 0U)
  {
    printf(" none");
  }
  printf("\r\n");
}

static HAL_StatusTypeDef tmc_uart_read_reg(uint8_t driver_addr, uint8_t reg_addr, uint32_t *value_out)
{
  uint8_t request[4] = {0x55U, driver_addr, reg_addr, 0U};
  uint8_t reply[8] = {0};
  uint32_t sync = 0U;
  uint32_t sync_target = ((uint32_t)request[0] << 16) | 0xFF00U | reg_addr;
  uint32_t start_ms = 0U;
  uint32_t timeout_ms = 30U;
  uint8_t i = 0U;

  request[3] = tmc_crc8(request, 3U);

  tmc_uart_flush_rx();

  if (tmc_uart_send_bytes(request, sizeof(request)) != HAL_OK)
  {
    return HAL_ERROR;
  }

  HAL_Delay(2U);

  start_ms = HAL_GetTick();
  while (timeout_ms > 0U)
  {
    uint32_t now_ms = HAL_GetTick();
    int16_t res = tmc_uart_read_byte(2U);
    if (now_ms != start_ms)
    {
      timeout_ms -= (now_ms - start_ms);
      start_ms = now_ms;
    }
    if (res < 0)
    {
      continue;
    }

    sync = ((sync << 8) | ((uint8_t)res)) & 0xFFFFFFU;
    if (sync == sync_target)
    {
      reply[0] = (uint8_t)(sync >> 16);
      reply[1] = (uint8_t)(sync >> 8);
      reply[2] = (uint8_t)sync;
      break;
    }
  }

  if (sync != sync_target)
  {
    return HAL_TIMEOUT;
  }

  start_ms = HAL_GetTick();
  timeout_ms = 30U;
  for (i = 3U; i < sizeof(reply); )
  {
    uint32_t now_ms = HAL_GetTick();
    int16_t res = tmc_uart_read_byte(2U);
    if (now_ms != start_ms)
    {
      timeout_ms -= (now_ms - start_ms);
      start_ms = now_ms;
    }
    if (timeout_ms == 0U)
    {
      return HAL_TIMEOUT;
    }
    if (res < 0)
    {
      continue;
    }
    reply[i++] = (uint8_t)res;
  }

  if (reply[7] != tmc_crc8(reply, 7U))
  {
    return HAL_ERROR;
  }

  *value_out =
    ((uint32_t)reply[3] << 24) |
    ((uint32_t)reply[4] << 16) |
    ((uint32_t)reply[5] << 8) |
    ((uint32_t)reply[6]);

  return HAL_OK;
}

static HAL_StatusTypeDef tmc_uart_write_reg(uint8_t driver_addr, uint8_t reg_addr, uint32_t reg_value)
{
  uint8_t datagram[8] = {
    0x55U,
    driver_addr,
    (uint8_t)(reg_addr | 0x80U),
    (uint8_t)(reg_value >> 24),
    (uint8_t)(reg_value >> 16),
    (uint8_t)(reg_value >> 8),
    (uint8_t)(reg_value >> 0),
    0U
  };
  datagram[7] = tmc_crc8(datagram, 7U);
  tmc_uart_flush_rx();
  return tmc_uart_send_bytes(datagram, sizeof(datagram));
}

static void tmc_emit_driver_probe(uint8_t driver_addr)
{
  uint32_t ioin = 0U;
  uint32_t gconf_value = 0U;
  uint32_t ifcnt_before = 0U;
  uint32_t ifcnt_after = 0U;
  uint32_t gconf_write = (1UL << 6) | (1UL << 7); // pdn_disable + mstep_reg_select

  if (tmc_uart_read_reg(driver_addr, 0x02U, &ifcnt_before) != HAL_OK)
  {
    printf("tmc timeout addr %u ifcnt_before\r\n", (unsigned)driver_addr);
    return;
  }

  if (tmc_uart_write_reg(driver_addr, 0x00U, gconf_write) != HAL_OK)
  {
    printf("tmc write-fail addr %u gconf\r\n", (unsigned)driver_addr);
    return;
  }

  HAL_Delay(2U);

  if (tmc_uart_read_reg(driver_addr, 0x02U, &ifcnt_after) != HAL_OK)
  {
    printf("tmc timeout addr %u ifcnt_after\r\n", (unsigned)driver_addr);
    return;
  }

  if (tmc_uart_read_reg(driver_addr, 0x00U, &gconf_value) != HAL_OK)
  {
    printf("tmc timeout addr %u gconf\r\n", (unsigned)driver_addr);
    return;
  }

  if (tmc_uart_read_reg(driver_addr, 0x06U, &ioin) != HAL_OK)
  {
    printf("tmc timeout addr %u ioin\r\n", (unsigned)driver_addr);
    return;
  }

  printf(
    "tmc ok addr %u ifcnt_before %lu ifcnt_after %lu gconf 0x%08lX ioin 0x%08lX\r\n",
    (unsigned)driver_addr,
    (unsigned long)(ifcnt_before & 0xFFU),
    (unsigned long)(ifcnt_after & 0xFFU),
    (unsigned long)gconf_value,
    (unsigned long)ioin
  );
}

static void process_command_line(char *line)
{
  char *tokens[10] = {0};
  size_t token_count = 0U;
  char *cursor = strtok(line, " ");

  while (cursor != NULL && token_count < 10U)
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
    emit_all_axis_states();
    emit_tmc_status();
    return;
  }

  if (strcmp(tokens[0], "tmc") == 0)
  {
    if (token_count >= 2U && strcmp(tokens[1], "status") == 0)
    {
      emit_tmc_status();
      return;
    }

    if (token_count >= 3U && strcmp(tokens[1], "raw") == 0)
    {
      uint8_t bytes[8] = {0};
      uint16_t byte_count = 0U;
      size_t idx;
      for (idx = 2U; idx < token_count && byte_count < (uint16_t)(sizeof(bytes) / sizeof(bytes[0])); idx++)
      {
        bytes[byte_count++] = (uint8_t)strtoul(tokens[idx], NULL, 0);
      }
      if (tmc_uart_send_bytes(bytes, byte_count) != HAL_OK)
      {
        uart_write_line("err tmc uart tx");
        return;
      }
      printf("ok tmc raw %u\r\n", (unsigned)byte_count);
      return;
    }

    if (token_count >= 2U && strcmp(tokens[1], "probe") == 0)
    {
      tmc_uart_flush_rx();
      tmc_emit_driver_probe(0U);
      tmc_uart_dump_raw(20U);
      return;
    }

    uart_write_line("err unsupported tmc command");
    return;
  }

  if (strcmp(tokens[0], "axis") != 0 || token_count < 3U)
  {
    uart_write_line("err invalid command");
    return;
  }

  AxisState *axis = find_axis(tokens[1]);
  if (axis == NULL)
  {
    uart_write_line("err unknown axis");
    return;
  }

  if (strcmp(tokens[2], "enable") == 0)
  {
    if (token_count < 4U)
    {
      uart_write_line("err missing enable state");
      return;
    }
    if (strcmp(tokens[3], "on") == 0)
    {
      axis->enabled = 1U;
      apply_axis_enable_state();
      printf("ok axis %s enable on\r\n", axis->key);
      emit_axis_state(axis);
      return;
    }
    if (strcmp(tokens[3], "off") == 0)
    {
      axis->enabled = 0U;
      axis->moving = 0U;
      axis->velocity = 0;
      axis->target = axis->position;
      apply_axis_enable_state();
      printf("ok axis %s enable off\r\n", axis->key);
      emit_axis_state(axis);
      return;
    }
    uart_write_line("err invalid enable state");
    return;
  }

  if (strcmp(tokens[2], "home") == 0)
  {
    axis->position = 0;
    axis->target = 0;
    axis->velocity = 0;
    axis->moving = 0U;
    axis->homed = 1U;
    printf("ok axis %s home\r\n", axis->key);
    emit_axis_state(axis);
    return;
  }

  if (strcmp(tokens[2], "stop") == 0)
  {
    axis->target = axis->position;
    axis->velocity = 0;
    axis->moving = 0U;
    printf("ok axis %s stop\r\n", axis->key);
    emit_axis_state(axis);
    return;
  }

  if (strcmp(tokens[2], "jog") == 0)
  {
    if (token_count < 4U)
    {
      uart_write_line("err missing jog direction");
      return;
    }
    if (axis->enabled == 0U)
    {
      uart_write_line("err axis disabled");
      return;
    }
    if (strcmp(tokens[3], "+") == 0)
    {
      axis->velocity = 1;
      axis->moving = 1U;
      axis->target = INT32_MAX;
      printf("ok axis %s jog +\r\n", axis->key);
      emit_axis_state(axis);
      return;
    }
    if (strcmp(tokens[3], "-") == 0)
    {
      axis->velocity = -1;
      axis->moving = 1U;
      axis->target = INT32_MIN;
      printf("ok axis %s jog -\r\n", axis->key);
      emit_axis_state(axis);
      return;
    }
    uart_write_line("err invalid jog direction");
    return;
  }

  if (strcmp(tokens[2], "move") == 0)
  {
    long delta = 0;
    if (token_count < 4U)
    {
      uart_write_line("err missing move delta");
      return;
    }
    delta = strtol(tokens[3], NULL, 10);
    if (axis->enabled == 0U)
    {
      uart_write_line("err axis disabled");
      return;
    }
    axis->target = axis->position + (int32_t)delta;
    axis->velocity = (delta == 0) ? 0 : ((delta > 0) ? 1 : -1);
    axis->moving = (delta == 0) ? 0U : 1U;
    printf("ok axis %s move %ld\r\n", axis->key, delta);
    emit_axis_state(axis);
    return;
  }

  if (strcmp(tokens[2], "goto") == 0)
  {
    long target = 0;
    if (token_count < 4U)
    {
      uart_write_line("err missing goto target");
      return;
    }
    target = strtol(tokens[3], NULL, 10);
    if (axis->enabled == 0U)
    {
      uart_write_line("err axis disabled");
      return;
    }
    axis->target = (int32_t)target;
    axis->velocity = (axis->target == axis->position) ? 0 : ((axis->target > axis->position) ? 1 : -1);
    axis->moving = (axis->target == axis->position) ? 0U : 1U;
    printf("ok axis %s goto %ld\r\n", axis->key, target);
    emit_axis_state(axis);
    return;
  }

  uart_write_line("err unsupported command");
}

static void tick_axes(void)
{
  size_t i;
  for (i = 0U; i < sizeof(axes) / sizeof(axes[0]); i++)
  {
    AxisState *axis = &axes[i];
    if (!axis->moving || !axis->enabled)
    {
      continue;
    }
    if (axis->position == axis->target)
    {
      axis->moving = 0U;
      axis->velocity = 0;
      emit_axis_state(axis);
      continue;
    }
    axis->position += axis->velocity;
    pulse_axis_step(axis, axis->velocity);
    if (axis->target == INT32_MAX || axis->target == INT32_MIN)
    {
      continue;
    }
    if (axis->position == axis->target)
    {
      axis->moving = 0U;
      axis->velocity = 0;
      emit_axis_state(axis);
    }
  }
}
/* USER CODE END 0 */

int main(void)
{
  HAL_Init();
  SystemClock_Config();
  MX_GPIO_Init();
  MX_USART1_UART_Init();
  MX_USART2_UART_Init();

  printf("\r\nSTM32G431RB #02 boot\r\n");
  printf("USART1 on PC4/PC5 via CN10-35/37, 115200 8N1\r\n");
  printf("USART2 half-duplex on PA2 for TMC2209 UART\r\n");

  while (1)
  {
    uint8_t rx_byte = 0;
    static char rx_line[96];
    static size_t rx_len = 0U;
    static uint32_t last_led_toggle_ms = 0U;
    const uint32_t now_ms = HAL_GetTick();

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

    tick_axes();

    if (now_ms - last_led_toggle_ms >= 250U)
    {
      HAL_GPIO_TogglePin(LED2_GPIO_Port, LED2_Pin);
      last_led_toggle_ms = now_ms;
    }
  }
}

void SystemClock_Config(void)
{
  RCC_OscInitTypeDef RCC_OscInitStruct = {0};
  RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

  HAL_PWREx_ControlVoltageScaling(PWR_REGULATOR_VOLTAGE_SCALE1_BOOST);

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

static void MX_USART2_UART_Init(void)
{
  huart2.Instance = USART2;
  huart2.Init.BaudRate = 115200;
  huart2.Init.WordLength = UART_WORDLENGTH_8B;
  huart2.Init.StopBits = UART_STOPBITS_1;
  huart2.Init.Parity = UART_PARITY_NONE;
  huart2.Init.Mode = UART_MODE_TX_RX;
  huart2.Init.HwFlowCtl = UART_HWCONTROL_NONE;
  huart2.Init.OverSampling = UART_OVERSAMPLING_16;
  huart2.Init.OneBitSampling = UART_ONE_BIT_SAMPLE_DISABLE;
  huart2.Init.ClockPrescaler = UART_PRESCALER_DIV1;
  huart2.AdvancedInit.AdvFeatureInit = UART_ADVFEATURE_NO_INIT;
  if (HAL_HalfDuplex_Init(&huart2) != HAL_OK)
  {
    Error_Handler();
  }
  if (HAL_UARTEx_SetTxFifoThreshold(&huart2, UART_TXFIFO_THRESHOLD_1_8) != HAL_OK)
  {
    Error_Handler();
  }
  if (HAL_UARTEx_SetRxFifoThreshold(&huart2, UART_RXFIFO_THRESHOLD_1_8) != HAL_OK)
  {
    Error_Handler();
  }
  if (HAL_UARTEx_DisableFifoMode(&huart2) != HAL_OK)
  {
    Error_Handler();
  }
}

static void MX_GPIO_Init(void)
{
  GPIO_InitTypeDef GPIO_InitStruct = {0};

  __HAL_RCC_GPIOA_CLK_ENABLE();
  __HAL_RCC_GPIOB_CLK_ENABLE();

  HAL_GPIO_WritePin(LED2_GPIO_Port, LED2_Pin, GPIO_PIN_RESET);
  HAL_GPIO_WritePin(GPIOB, A_STEP_Pin|A_DIR_Pin|B_STEP_Pin|B_DIR_Pin, GPIO_PIN_RESET);
  HAL_GPIO_WritePin(AB_EN_GPIO_Port, AB_EN_Pin, GPIO_PIN_SET);

  GPIO_InitStruct.Pin = LED2_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(LED2_GPIO_Port, &GPIO_InitStruct);

  GPIO_InitStruct.Pin = A_STEP_Pin|A_DIR_Pin|AB_EN_Pin|B_STEP_Pin|B_DIR_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
  HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);
}

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
#endif

PUTCHAR_PROTOTYPE
{
  HAL_UART_Transmit(&huart1, (uint8_t *)&ch, 1, 0xFFFF);
  return ch;
}

void Error_Handler(void)
{
  while (1)
  {
    HAL_GPIO_TogglePin(LED2_GPIO_Port, LED2_Pin);
    HAL_Delay(500);
  }
}

#ifdef  USE_FULL_ASSERT
void assert_failed(uint8_t *file, uint32_t line)
{
  (void)file;
  (void)line;
  while (1)
  {
  }
}
#endif
