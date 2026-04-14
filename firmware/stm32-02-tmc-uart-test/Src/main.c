/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file    UART/UART_Printf/Src/main.c
  * @brief   Minimal STM32 #02 TMC2209 UART diagnostic firmware.
  ******************************************************************************
  */
/* USER CODE END Header */
/* Includes ------------------------------------------------------------------*/
#include "main.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include <stdio.h>
#include <string.h>
#include <stdbool.h>
/* USER CODE END Includes */

/* Private variables ---------------------------------------------------------*/
UART_HandleTypeDef huart1;
UART_HandleTypeDef huart2;
/* USER CODE BEGIN PV */
static uint8_t axis_a_enabled = 0U;
/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
static void MX_GPIO_Init(void);
static void MX_USART1_UART_Init(void);
static void MX_USART3_UART_Init(void);
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
static uint8_t tmc_crc8(const uint8_t *bytes, uint8_t len);
static void tmc_uart_flush_rx(void);
static HAL_StatusTypeDef tmc_uart_write_bytes(const uint8_t *bytes, uint16_t len, uint32_t timeout_ms);
static int16_t tmc_uart_read_byte(uint32_t timeout_ms);
static HAL_StatusTypeDef tmc_uart_read_exact(uint8_t *bytes, uint16_t len, uint32_t timeout_ms);
static HAL_StatusTypeDef tmc_uart_read_reg(uint8_t driver_addr, uint8_t reg_addr, uint32_t *value_out);
static HAL_StatusTypeDef tmc_uart_write_reg(uint8_t driver_addr, uint8_t reg_addr, uint32_t reg_value);
static void tmc_uart_dump_raw(uint32_t window_ms);
static void tmc_uart_line_probe(uint8_t driver_addr, uint8_t reg_addr);
static void set_axis_a_enable(uint8_t enabled);
static void emit_status(void);
static void probe_driver(uint8_t driver_addr);
static void boot_probe(void);
static void process_command_line(char *line);
/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */
static void uart_write_line(const char *text)
{
  printf("%s\r\n", text);
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

static void tmc_uart_flush_rx(void)
{
  while ((huart2.Instance->ISR & USART_ISR_RXNE_RXFNE) != 0U)
  {
    volatile uint8_t discard = (uint8_t)huart2.Instance->RDR;
    (void)discard;
  }
  __HAL_UART_CLEAR_OREFLAG(&huart2);
  __HAL_UART_CLEAR_NEFLAG(&huart2);
  __HAL_UART_CLEAR_FEFLAG(&huart2);
  __HAL_UART_CLEAR_PEFLAG(&huart2);
}

static void tmc_uart_set_receiver(uint8_t enabled)
{
  if (enabled != 0U)
  {
    SET_BIT(huart2.Instance->CR1, USART_CR1_RE);
  }
  else
  {
    CLEAR_BIT(huart2.Instance->CR1, USART_CR1_RE);
  }
}

static HAL_StatusTypeDef tmc_uart_write_bytes(const uint8_t *bytes, uint16_t len, uint32_t timeout_ms)
{
  uint32_t start_ms = HAL_GetTick();
  uint16_t i;

  tmc_uart_set_receiver(0U);
  tmc_uart_flush_rx();

  for (i = 0U; i < len; i++)
  {
    while ((huart2.Instance->ISR & USART_ISR_TXE_TXFNF) == 0U)
    {
      if ((HAL_GetTick() - start_ms) >= timeout_ms)
      {
        return HAL_TIMEOUT;
      }
    }
    huart2.Instance->TDR = bytes[i];
  }

  while ((huart2.Instance->ISR & USART_ISR_TC) == 0U)
  {
    if ((HAL_GetTick() - start_ms) >= timeout_ms)
    {
      return HAL_TIMEOUT;
    }
  }

  tmc_uart_flush_rx();
  tmc_uart_set_receiver(1U);
  return HAL_OK;
}

static int16_t tmc_uart_read_byte(uint32_t timeout_ms)
{
  uint32_t start_ms = HAL_GetTick();

  while ((HAL_GetTick() - start_ms) < timeout_ms)
  {
    uint32_t isr = huart2.Instance->ISR;
    if ((isr & USART_ISR_RXNE_RXFNE) != 0U)
    {
      return (int16_t)((uint8_t)huart2.Instance->RDR);
    }
    if ((isr & (USART_ISR_ORE | USART_ISR_NE | USART_ISR_FE | USART_ISR_PE)) != 0U)
    {
      __HAL_UART_CLEAR_OREFLAG(&huart2);
      __HAL_UART_CLEAR_NEFLAG(&huart2);
      __HAL_UART_CLEAR_FEFLAG(&huart2);
      __HAL_UART_CLEAR_PEFLAG(&huart2);
    }
  }

  return -1;
}

static HAL_StatusTypeDef tmc_uart_read_exact(uint8_t *bytes, uint16_t len, uint32_t timeout_ms)
{
  uint16_t i;
  for (i = 0U; i < len; i++)
  {
    int16_t value = tmc_uart_read_byte(timeout_ms);
    if (value < 0)
    {
      return HAL_TIMEOUT;
    }
    bytes[i] = (uint8_t)value;
  }
  return HAL_OK;
}

static HAL_StatusTypeDef tmc_uart_read_reg(uint8_t driver_addr, uint8_t reg_addr, uint32_t *value_out)
{
  uint8_t request[4] = {0x05U, driver_addr, reg_addr, 0U};
  uint8_t reply[8] = {0};
  uint32_t sync_target = ((uint32_t)request[0] << 16) | 0xFF00U | reg_addr;
  uint8_t attempt;

  request[3] = tmc_crc8(request, 3U);

  for (attempt = 0U; attempt < 2U; attempt++)
  {
    uint32_t sync = 0U;
    uint32_t start_ms;
    uint32_t timeout_ms = 5U;

    tmc_uart_flush_rx();

    if (tmc_uart_write_bytes(request, sizeof(request), 20U) != HAL_OK)
    {
      return HAL_ERROR;
    }

    start_ms = HAL_GetTick();

    while (timeout_ms > 0U)
    {
      uint32_t now_ms = HAL_GetTick();
      int16_t value = tmc_uart_read_byte(1U);
      if (now_ms != start_ms)
      {
        timeout_ms -= (now_ms - start_ms);
        start_ms = now_ms;
      }
      if (value < 0)
      {
        continue;
      }

      sync = ((sync << 8) | ((uint8_t)value)) & 0xFFFFFFU;
      if (sync == sync_target)
      {
        reply[0] = (uint8_t)(sync >> 16);
        reply[1] = (uint8_t)(sync >> 8);
        reply[2] = (uint8_t)(sync);
        break;
      }
    }

    if (sync != sync_target)
    {
      continue;
    }

    if (tmc_uart_read_exact(&reply[3], 5U, 5U) != HAL_OK)
    {
      continue;
    }

    if (reply[7] != tmc_crc8(reply, 7U) || reply[7] == 0U)
    {
      continue;
    }

    *value_out =
      ((uint32_t)reply[3] << 24) |
      ((uint32_t)reply[4] << 16) |
      ((uint32_t)reply[5] << 8) |
      ((uint32_t)reply[6]);
    return HAL_OK;
  }

  return HAL_TIMEOUT;
}

static HAL_StatusTypeDef tmc_uart_write_reg(uint8_t driver_addr, uint8_t reg_addr, uint32_t reg_value)
{
  uint8_t datagram[8] = {
    0x05U,
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
  if (tmc_uart_write_bytes(datagram, sizeof(datagram), 30U) != HAL_OK)
  {
    return HAL_ERROR;
  }
  return HAL_OK;
}

static void tmc_uart_dump_raw(uint32_t window_ms)
{
  uint32_t start_ms = HAL_GetTick();
  uint8_t count = 0U;
  printf("tmc raw");
  while ((HAL_GetTick() - start_ms) < window_ms)
  {
    int16_t value = tmc_uart_read_byte(1U);
    if (value < 0)
    {
      continue;
    }
    printf(" %02X", (unsigned)(value & 0xFF));
    count++;
  }
  if (count == 0U)
  {
    printf(" none");
  }
  printf("\r\n");
}

static void tmc_uart_line_probe(uint8_t driver_addr, uint8_t reg_addr)
{
  uint8_t request[4] = {0x05U, driver_addr, reg_addr, 0U};
  uint32_t start_ms;
  uint32_t edges = 0U;
  GPIO_PinState previous;

  request[3] = tmc_crc8(request, 3U);
  tmc_uart_flush_rx();

  previous = HAL_GPIO_ReadPin(TMC_UART_RX_GPIO_Port, TMC_UART_RX_Pin);
  if (tmc_uart_write_bytes(request, sizeof(request), 20U) != HAL_OK)
  {
    printf("tmc line addr %u tx_fail\r\n", (unsigned)driver_addr);
    return;
  }

  start_ms = HAL_GetTick();
  while ((HAL_GetTick() - start_ms) < 5U)
  {
    GPIO_PinState current = HAL_GPIO_ReadPin(TMC_UART_RX_GPIO_Port, TMC_UART_RX_Pin);
    if (current != previous)
    {
      edges++;
      previous = current;
    }
  }

  printf("tmc line addr %u edges %lu final %s\r\n",
         (unsigned)driver_addr,
         (unsigned long)edges,
         previous == GPIO_PIN_SET ? "high" : "low");
}

static void set_axis_a_enable(uint8_t enabled)
{
  axis_a_enabled = enabled ? 1U : 0U;
  HAL_GPIO_WritePin(AB_EN_GPIO_Port, AB_EN_Pin, axis_a_enabled ? GPIO_PIN_RESET : GPIO_PIN_SET);
  printf("axis a enable %s\r\n", axis_a_enabled ? "on" : "off");
}

static void emit_status(void)
{
  printf("status axis_a_enabled %s ab_en %s\r\n",
         axis_a_enabled ? "on" : "off",
         HAL_GPIO_ReadPin(AB_EN_GPIO_Port, AB_EN_Pin) == GPIO_PIN_RESET ? "low" : "high");
}

static void probe_driver(uint8_t driver_addr)
{
  uint32_t ifcnt_before = 0U;
  uint32_t ifcnt_after = 0U;
  uint32_t gconf = 0U;
  uint32_t ioin = 0U;
  const uint32_t gconf_write = (1UL << 6) | (1UL << 7);

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

  if (tmc_uart_read_reg(driver_addr, 0x02U, &ifcnt_after) != HAL_OK)
  {
    printf("tmc timeout addr %u ifcnt_after\r\n", (unsigned)driver_addr);
    return;
  }

  if (tmc_uart_read_reg(driver_addr, 0x00U, &gconf) != HAL_OK)
  {
    printf("tmc timeout addr %u gconf\r\n", (unsigned)driver_addr);
    return;
  }

  if (tmc_uart_read_reg(driver_addr, 0x06U, &ioin) != HAL_OK)
  {
    printf("tmc timeout addr %u ioin\r\n", (unsigned)driver_addr);
    return;
  }

  printf("tmc ok addr %u ifcnt_before %lu ifcnt_after %lu gconf 0x%08lX ioin 0x%08lX\r\n",
         (unsigned)driver_addr,
         (unsigned long)(ifcnt_before & 0xFFU),
         (unsigned long)(ifcnt_after & 0xFFU),
         (unsigned long)gconf,
         (unsigned long)ioin);
}

static void boot_probe(void)
{
  set_axis_a_enable(1U);
  HAL_Delay(10U);
  tmc_uart_line_probe(0U, 0x02U);
  tmc_uart_line_probe(2U, 0x02U);
  probe_driver(0U);
  probe_driver(2U);
  tmc_uart_dump_raw(40U);
}

static void process_command_line(char *line)
{
  if (strcmp(line, "status") == 0)
  {
    emit_status();
    return;
  }

  if (strcmp(line, "enable on") == 0)
  {
    set_axis_a_enable(1U);
    return;
  }

  if (strcmp(line, "enable off") == 0)
  {
    set_axis_a_enable(0U);
    return;
  }

  if (strcmp(line, "probe") == 0)
  {
    tmc_uart_line_probe(0U, 0x02U);
    tmc_uart_line_probe(2U, 0x02U);
    probe_driver(0U);
    probe_driver(2U);
    tmc_uart_dump_raw(40U);
    return;
  }

  if (strcmp(line, "raw") == 0)
  {
    tmc_uart_dump_raw(40U);
    return;
  }

  uart_write_line("err invalid command");
}
/* USER CODE END 0 */

int main(void)
{
  HAL_Init();
  SystemClock_Config();
  MX_GPIO_Init();
  MX_USART1_UART_Init();
  MX_USART3_UART_Init();

  printf("\r\nSTM32G431RB #02 TMC UART TEST\r\n");
  printf("USART1 on PC4/PC5 via CN10-35/37, 115200 8N1\r\n");
  printf("USART3 on PB10/PB11 for TMC2209 UART\r\n");
  boot_probe();

  while (1)
  {
    uint8_t rx_byte = 0U;
    static char rx_line[64];
    static size_t rx_len = 0U;

    while (HAL_UART_Receive(&huart1, &rx_byte, 1U, 1U) == HAL_OK)
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

static void MX_USART3_UART_Init(void)
{
  huart2.Instance = USART3;
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
  if (HAL_UART_Init(&huart2) != HAL_OK)
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
    HAL_Delay(250);
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
