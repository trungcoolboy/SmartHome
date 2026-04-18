/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file    Src/main.c
  * @brief   Minimal STM32 #02 B-axis-only diagnostic firmware.
  ******************************************************************************
  */
/* USER CODE END Header */
#include "main.h"

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

UART_HandleTypeDef huart1;
UART_HandleTypeDef huart2;
TIM_HandleTypeDef htim6;

typedef struct
{
  uint8_t enabled;
  int32_t position;
  uint8_t homed;
  uint16_t jog_interval_us;
} BAxisState;

typedef struct
{
  volatile uint8_t active;
  volatile int32_t direction;
  volatile uint32_t steps_remaining;
  volatile uint32_t moved_steps;
  volatile uint32_t interval_us;
  volatile uint32_t current_interval_us;
  volatile uint8_t stop_on_endstop;
  volatile uint8_t continuous;
  volatile uint8_t bounce_mode;
  volatile uint8_t pulse_high_phase;
} BMotionState;

static BAxisState b_axis = {
  .enabled = 0U,
  .position = 0,
  .homed = 0U,
  .jog_interval_us = 5000U,
};

static BMotionState b_motion = {
  .active = 0U,
  .direction = 1,
  .steps_remaining = 0U,
  .moved_steps = 0U,
  .interval_us = 5000U,
  .current_interval_us = 5000U,
  .stop_on_endstop = 0U,
  .continuous = 0U,
  .bounce_mode = 0U,
  .pulse_high_phase = 0U,
};

void SystemClock_Config(void);
static void MX_GPIO_Init(void);
static void MX_USART1_UART_Init(void);
static void MX_USART2_UART_Init(void);
static void MX_TIM6_Init(void);

static void uart_write_line(const char *text);
static void step_delay_us(uint16_t us);
static void b_apply_enable(void);
static uint8_t b_min_endstop_triggered(void);
static uint8_t b_max_endstop_triggered(void);
static void b_emit_status(void);
static void b_step_once(int32_t direction);
static void b_motion_start(int32_t direction, uint32_t steps, uint32_t interval_us, uint8_t stop_on_endstop);
static void b_motion_start_continuous(int32_t direction, uint32_t interval_us, uint8_t bounce_mode);
static void b_motion_stop(void);
static void b_motion_wait(void);
static uint32_t b_run_steps(int32_t direction, uint32_t steps, uint16_t interval_us, uint8_t stop_on_endstop);
static void b_release_from_endstop(void);
static void b_home(void);
static uint32_t b_target_interval_for_position(void);
static uint8_t tmc_crc8(const uint8_t *bytes, uint8_t len);
static void tmc_uart_flush_rx(void);
static HAL_StatusTypeDef tmc_uart_send_bytes(const uint8_t *bytes, uint16_t len);
static int16_t tmc_uart_read_byte(uint32_t timeout_ms);
static HAL_StatusTypeDef tmc_uart_read_reg(uint8_t driver_addr, uint8_t reg_addr, uint32_t *value_out);
static HAL_StatusTypeDef tmc_uart_write_reg(uint8_t driver_addr, uint8_t reg_addr, uint32_t reg_value);
static uint32_t tmc_rms_current_ma(uint8_t irun, uint8_t vsense);
static uint8_t tmc_pick_irun_for_ma(uint32_t target_ma, uint8_t *vsense_out);
static uint16_t tmc_microsteps_from_mres(uint8_t mres);
static int8_t tmc_mres_from_microsteps(uint16_t microsteps);
static void tmc_emit_status(void);
static void tmc_set_current(uint32_t target_ma);
static void tmc_set_microsteps(uint16_t microsteps);
static void tmc_set_stealth(uint8_t enable);
static void tmc_boot_init(void);
static void process_command_line(char *line);

#define B_HOME_SEEK_LIMIT_STEPS      50000U
#define B_HOME_RELEASE_LIMIT_STEPS    8000U
#define B_HOME_PROGRESS_STEPS         1000U
#define B_STEP_PULSE_HIGH_US             5U
#define B_BOUNCE_RAMP_START_US        2000U
#define B_BOUNCE_RAMP_DELTA_US          10U
#define B_TRAVEL_STEPS               22991U
#define B_DECEL_WINDOW_STEPS           300U

#if defined(__ICCARM__)
int iar_fputc(int ch);
#define PUTCHAR_PROTOTYPE int iar_fputc(int ch)
#elif defined(__CC_ARM) || defined(__ARMCC_VERSION)
#define PUTCHAR_PROTOTYPE int fputc(int ch, FILE *f)
#elif defined(__GNUC__)
#define PUTCHAR_PROTOTYPE int __io_putchar(int ch)
#endif

int main(void)
{
  static char rx_line[96];
  static size_t rx_len = 0U;
  HAL_Init();
  SystemClock_Config();
  MX_GPIO_Init();
  MX_USART1_UART_Init();
  MX_USART2_UART_Init();
  MX_TIM6_Init();

  CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
  DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
  DWT->CYCCNT = 0U;

  printf("\r\nSTM32G431RB #02 B AXIS TUNE TEST\r\n");
  printf("host uart usart1 pc4/pc5 115200\r\n");
  printf("b step pa8 dir pc7 en pb2 endstop_min pb4 endstop_max pb5\r\n");
  b_axis.enabled = 1U;
  b_apply_enable();
  uart_write_line("ok b enable on");
  b_emit_status();
  b_axis.jog_interval_us = 500U;
  b_home();
  b_axis.jog_interval_us = 100U;
  b_motion_start_continuous(1, b_axis.jog_interval_us, 1U);
  printf("ok auto bounce on dir 1 interval_us %u travel_steps %u decel_window %u\r\n",
         (unsigned)b_axis.jog_interval_us,
         (unsigned)B_TRAVEL_STEPS,
         (unsigned)B_DECEL_WINDOW_STEPS);
  b_emit_status();

  while (1)
  {
    while (__HAL_UART_GET_FLAG(&huart1, UART_FLAG_RXNE) != RESET)
    {
      uint8_t rx_byte = (uint8_t)(huart1.Instance->RDR & 0xFFU);
      if (rx_byte == '\r' || rx_byte == '\n')
      {
        if (rx_len > 0U)
        {
          rx_line[rx_len] = '\0';
          printf("cmd %s\r\n", rx_line);
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

static void uart_write_line(const char *text)
{
  printf("%s\r\n", text);
}

static void step_delay_us(uint16_t us)
{
  uint32_t cycles_per_us = HAL_RCC_GetHCLKFreq() / 1000000U;
  uint32_t start = DWT->CYCCNT;
  uint32_t ticks = cycles_per_us * (uint32_t)us;
  while ((uint32_t)(DWT->CYCCNT - start) < ticks)
  {
  }
}

static void b_apply_enable(void)
{
  HAL_GPIO_WritePin(AB_EN_GPIO_Port, AB_EN_Pin, b_axis.enabled ? GPIO_PIN_RESET : GPIO_PIN_SET);
}

static uint8_t b_min_endstop_triggered(void)
{
  return HAL_GPIO_ReadPin(B_MIN_ENDSTOP_GPIO_Port, B_MIN_ENDSTOP_Pin) == GPIO_PIN_SET ? 1U : 0U;
}

static uint8_t b_max_endstop_triggered(void)
{
  return HAL_GPIO_ReadPin(B_MAX_ENDSTOP_GPIO_Port, B_MAX_ENDSTOP_Pin) == GPIO_PIN_SET ? 1U : 0U;
}

static void b_emit_status(void)
{
  printf("b enabled %s pos %ld homed %s endstop_min %s endstop_max %s interval_us %u\r\n",
         b_axis.enabled ? "on" : "off",
         (long)b_axis.position,
         b_axis.homed ? "yes" : "no",
         b_min_endstop_triggered() ? "trig" : "clear",
         b_max_endstop_triggered() ? "trig" : "clear",
         (unsigned)b_axis.jog_interval_us);
}

static void b_step_once(int32_t direction)
{
  HAL_GPIO_WritePin(B_DIR_GPIO_Port, B_DIR_Pin, (direction >= 0) ? GPIO_PIN_SET : GPIO_PIN_RESET);
  step_delay_us(20U);
  HAL_GPIO_WritePin(B_STEP_GPIO_Port, B_STEP_Pin, GPIO_PIN_SET);
  step_delay_us(40U);
  HAL_GPIO_WritePin(B_STEP_GPIO_Port, B_STEP_Pin, GPIO_PIN_RESET);
  step_delay_us(20U);
  b_axis.position += direction;
}

static void b_motion_start(int32_t direction, uint32_t steps, uint32_t interval_us, uint8_t stop_on_endstop)
{
  if (interval_us < 10U)
  {
    interval_us = 10U;
  }

  b_motion.direction = direction;
  b_motion.steps_remaining = steps;
  b_motion.moved_steps = 0U;
  b_motion.interval_us = interval_us;
  b_motion.current_interval_us = interval_us;
  b_motion.stop_on_endstop = stop_on_endstop;
  b_motion.continuous = 0U;
  b_motion.bounce_mode = 0U;
  b_motion.pulse_high_phase = 0U;
  b_motion.active = (steps > 0U) ? 1U : 0U;

  HAL_GPIO_WritePin(B_DIR_GPIO_Port, B_DIR_Pin, (direction >= 0) ? GPIO_PIN_SET : GPIO_PIN_RESET);
  __HAL_TIM_SET_AUTORELOAD(&htim6, ((b_motion.current_interval_us > B_STEP_PULSE_HIGH_US) ? (b_motion.current_interval_us - B_STEP_PULSE_HIGH_US) : 10U) - 1U);
  __HAL_TIM_SET_COUNTER(&htim6, 0U);

  if (b_motion.active)
  {
    HAL_TIM_Base_Start_IT(&htim6);
  }
}

static void b_motion_start_continuous(int32_t direction, uint32_t interval_us, uint8_t bounce_mode)
{
  if (interval_us < 10U)
  {
    interval_us = 10U;
  }

  b_motion.direction = direction;
  b_motion.steps_remaining = 0U;
  b_motion.moved_steps = 0U;
  b_motion.interval_us = interval_us;
  b_motion.current_interval_us = interval_us;
  b_motion.stop_on_endstop = 1U;
  b_motion.continuous = 1U;
  b_motion.bounce_mode = bounce_mode ? 1U : 0U;
  b_motion.pulse_high_phase = 0U;
  b_motion.active = 1U;

  HAL_GPIO_WritePin(B_DIR_GPIO_Port, B_DIR_Pin, (direction >= 0) ? GPIO_PIN_SET : GPIO_PIN_RESET);
  __HAL_TIM_SET_AUTORELOAD(&htim6, ((b_motion.current_interval_us > B_STEP_PULSE_HIGH_US) ? (b_motion.current_interval_us - B_STEP_PULSE_HIGH_US) : 10U) - 1U);
  __HAL_TIM_SET_COUNTER(&htim6, 0U);
  HAL_TIM_Base_Start_IT(&htim6);
}

static void b_motion_stop(void)
{
  HAL_TIM_Base_Stop_IT(&htim6);
  b_motion.active = 0U;
  b_motion.continuous = 0U;
  b_motion.bounce_mode = 0U;
  b_motion.pulse_high_phase = 0U;
  b_motion.steps_remaining = 0U;
  HAL_GPIO_WritePin(B_STEP_GPIO_Port, B_STEP_Pin, GPIO_PIN_RESET);
}

static void b_motion_wait(void)
{
  while (b_motion.active != 0U)
  {
  }
}

static uint32_t b_run_steps(int32_t direction, uint32_t steps, uint16_t interval_us, uint8_t stop_on_endstop)
{
  b_motion_start(direction, steps, interval_us, stop_on_endstop);
  b_motion_wait();
  return b_motion.moved_steps;
}

static void b_release_from_endstop(void)
{
  uint32_t released = 0U;

  if (b_min_endstop_triggered())
  {
    while (b_min_endstop_triggered() && released < B_HOME_RELEASE_LIMIT_STEPS)
    {
      released += b_run_steps(1, 100U, 1000U, 0U);
    }
    printf("ok b release min steps %lu endstop_min %s\r\n",
           (unsigned long)released,
           b_min_endstop_triggered() ? "trig" : "clear");
  }
  else if (b_max_endstop_triggered())
  {
    while (b_max_endstop_triggered() && released < B_HOME_RELEASE_LIMIT_STEPS)
    {
      released += b_run_steps(-1, 100U, 1000U, 0U);
    }
    printf("ok b release max steps %lu endstop_max %s\r\n",
           (unsigned long)released,
           b_max_endstop_triggered() ? "trig" : "clear");
  }
}

static void b_home(void)
{
  uint32_t seek_steps = 0U;
  uint32_t release_steps = 0U;

  if (!b_axis.enabled)
  {
    uart_write_line("err b disabled");
    return;
  }

  printf("ok b home start\r\n");

  while (!b_min_endstop_triggered() && seek_steps < B_HOME_SEEK_LIMIT_STEPS)
  {
    uint32_t chunk = B_HOME_PROGRESS_STEPS;
    uint32_t moved;
    if ((B_HOME_SEEK_LIMIT_STEPS - seek_steps) < chunk)
    {
      chunk = (B_HOME_SEEK_LIMIT_STEPS - seek_steps);
    }
    moved = b_run_steps(-1, chunk, b_axis.jog_interval_us, 1U);
    seek_steps += moved;
    printf("b home progress seek %lu pos %ld endstop_min %s\r\n",
           (unsigned long)seek_steps,
           (long)b_axis.position,
           b_min_endstop_triggered() ? "trig" : "clear");
    if (moved < chunk)
    {
      break;
    }
  }

  printf("b home seek_steps %lu endstop_min %s pos %ld\r\n",
         (unsigned long)seek_steps,
         b_min_endstop_triggered() ? "trig" : "clear",
         (long)b_axis.position);

  if (!b_min_endstop_triggered())
  {
    uart_write_line("err b home seek timeout");
    return;
  }

  while (b_min_endstop_triggered() && release_steps < B_HOME_RELEASE_LIMIT_STEPS)
  {
    uint32_t chunk = B_HOME_PROGRESS_STEPS;
    if ((B_HOME_RELEASE_LIMIT_STEPS - release_steps) < chunk)
    {
      chunk = (B_HOME_RELEASE_LIMIT_STEPS - release_steps);
    }
    release_steps += b_run_steps(1, chunk, b_axis.jog_interval_us, 0U);
    printf("b home progress release %lu pos %ld endstop_min %s\r\n",
           (unsigned long)release_steps,
           (long)b_axis.position,
           b_min_endstop_triggered() ? "trig" : "clear");
  }

  if (b_min_endstop_triggered())
  {
    uart_write_line("err b home release timeout");
    return;
  }

  b_axis.position = 0;
  b_axis.homed = 1U;
  printf("ok b homed release_steps %lu\r\n", (unsigned long)release_steps);
  b_emit_status();
}

static uint32_t b_target_interval_for_position(void)
{
  uint32_t distance_to_edge;
  uint32_t ramp_span = (B_BOUNCE_RAMP_START_US > b_motion.interval_us) ? (B_BOUNCE_RAMP_START_US - b_motion.interval_us) : 0U;

  if (b_motion.direction > 0)
  {
    distance_to_edge = ((uint32_t)b_axis.position >= B_TRAVEL_STEPS) ? 0U : (B_TRAVEL_STEPS - (uint32_t)b_axis.position);
  }
  else
  {
    distance_to_edge = (b_axis.position <= 0) ? 0U : (uint32_t)b_axis.position;
  }

  if (distance_to_edge >= B_DECEL_WINDOW_STEPS || ramp_span == 0U)
  {
    return b_motion.interval_us;
  }

  return b_motion.interval_us + (uint32_t)(((uint64_t)ramp_span * (uint64_t)(B_DECEL_WINDOW_STEPS - distance_to_edge)) / (uint64_t)B_DECEL_WINDOW_STEPS);
}

static void MX_TIM6_Init(void)
{
  __HAL_RCC_TIM6_CLK_ENABLE();

  htim6.Instance = TIM6;
  htim6.Init.Prescaler = (uint32_t)((HAL_RCC_GetHCLKFreq() / 1000000U) - 1U);
  htim6.Init.CounterMode = TIM_COUNTERMODE_UP;
  htim6.Init.Period = 999U;
  htim6.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_DISABLE;
  if (HAL_TIM_Base_Init(&htim6) != HAL_OK)
  {
    Error_Handler();
  }

  HAL_NVIC_SetPriority(TIM6_DAC_IRQn, 1U, 0U);
  HAL_NVIC_EnableIRQ(TIM6_DAC_IRQn);
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
  while (__HAL_UART_GET_FLAG(&huart2, UART_FLAG_RXNE) != RESET)
  {
    (void)huart2.Instance->RDR;
  }
}

static HAL_StatusTypeDef tmc_uart_send_bytes(const uint8_t *bytes, uint16_t len)
{
  tmc_uart_flush_rx();
  CLEAR_BIT(huart2.Instance->CR1, USART_CR1_RE);
  if (HAL_UART_Transmit(&huart2, (uint8_t *)bytes, len, 50U) != HAL_OK)
  {
    SET_BIT(huart2.Instance->CR1, USART_CR1_RE);
    return HAL_ERROR;
  }
  SET_BIT(huart2.Instance->CR1, USART_CR1_RE);
  return HAL_OK;
}

static int16_t tmc_uart_read_byte(uint32_t timeout_ms)
{
  uint8_t value = 0U;
  if (HAL_UART_Receive(&huart2, &value, 1U, timeout_ms) != HAL_OK)
  {
    return -1;
  }
  return (int16_t)value;
}

static HAL_StatusTypeDef tmc_uart_read_reg(uint8_t driver_addr, uint8_t reg_addr, uint32_t *value_out)
{
  uint8_t request[4] = {0x05U, driver_addr, reg_addr, 0U};
  uint8_t reply[8] = {0U};
  uint32_t sync = 0U;
  uint32_t sync_target = ((uint32_t)0x05U << 16) | 0xFF00U | reg_addr;
  uint8_t attempt;

  request[3] = tmc_crc8(request, 3U);

  for (attempt = 0U; attempt < 3U; attempt++)
  {
    uint32_t timeout_ms = 8U;
    uint32_t start_ms = HAL_GetTick();

    if (tmc_uart_send_bytes(request, sizeof(request)) != HAL_OK)
    {
      return HAL_ERROR;
    }

    step_delay_us(300U);
    sync = 0U;
    while (timeout_ms > 0U)
    {
      uint32_t now_ms = HAL_GetTick();
      int16_t res = tmc_uart_read_byte(1U);
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
      continue;
    }

    if (HAL_UART_Receive(&huart2, &reply[3], 5U, 8U) != HAL_OK)
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
  if (tmc_uart_send_bytes(datagram, sizeof(datagram)) != HAL_OK)
  {
    return HAL_ERROR;
  }
  HAL_Delay(2U);
  return HAL_OK;
}

static uint32_t tmc_rms_current_ma(uint8_t irun, uint8_t vsense)
{
  const uint32_t vfs_mv = vsense ? 180U : 325U;
  const uint32_t rsense_mohm = 110U;
  const uint64_t numerator = (uint64_t)(irun + 1U) * (uint64_t)vfs_mv * 1000000ULL;
  const uint64_t denominator = 32ULL * (uint64_t)(rsense_mohm + 20U) * 1414ULL;
  return (uint32_t)((numerator + (denominator / 2ULL)) / denominator);
}

static uint8_t tmc_pick_irun_for_ma(uint32_t target_ma, uint8_t *vsense_out)
{
  uint8_t best_irun = 0U;
  uint8_t best_vsense = 0U;
  uint32_t best_error = UINT32_MAX;
  uint8_t vsense;

  for (vsense = 0U; vsense <= 1U; vsense++)
  {
    uint8_t irun;
    for (irun = 0U; irun < 32U; irun++)
    {
      uint32_t ma = tmc_rms_current_ma(irun, vsense);
      uint32_t error = (ma > target_ma) ? (ma - target_ma) : (target_ma - ma);
      if (error < best_error)
      {
        best_error = error;
        best_irun = irun;
        best_vsense = vsense;
      }
    }
  }

  *vsense_out = best_vsense;
  return best_irun;
}

static uint16_t tmc_microsteps_from_mres(uint8_t mres)
{
  if (mres > 8U)
  {
    return 0U;
  }
  return (uint16_t)(256U >> mres);
}

static int8_t tmc_mres_from_microsteps(uint16_t microsteps)
{
  switch (microsteps)
  {
    case 256: return 0;
    case 128: return 1;
    case 64: return 2;
    case 32: return 3;
    case 16: return 4;
    case 8: return 5;
    case 4: return 6;
    case 2: return 7;
    case 1: return 8;
    default: return -1;
  }
}

static void tmc_emit_status(void)
{
  uint32_t ifcnt = 0U;
  uint32_t gconf = 0U;
  uint32_t chopconf = 0U;
  uint32_t ihold_irun = 0U;
  uint32_t ioin = 0U;
  uint8_t irun;
  uint8_t ihold;
  uint8_t vsense;
  uint8_t mres;

  if (tmc_uart_read_reg(3U, 0x02U, &ifcnt) != HAL_OK ||
      tmc_uart_read_reg(3U, 0x00U, &gconf) != HAL_OK ||
      tmc_uart_read_reg(3U, 0x6CU, &chopconf) != HAL_OK ||
      tmc_uart_read_reg(3U, 0x10U, &ihold_irun) != HAL_OK ||
      tmc_uart_read_reg(3U, 0x06U, &ioin) != HAL_OK)
  {
    uart_write_line("err tmc b status");
    return;
  }

  irun = (uint8_t)((ihold_irun >> 8) & 0x1FU);
  ihold = (uint8_t)(ihold_irun & 0x1FU);
  vsense = (uint8_t)((chopconf >> 17) & 0x01U);
  mres = (uint8_t)((chopconf >> 24) & 0x0FU);

  printf("tmc b ifcnt %lu gconf 0x%08lX chopconf 0x%08lX ihold_irun 0x%08lX ioin 0x%08lX current_ma %lu hold_cs %u run_cs %u microsteps %u stealth %s vsense %u\r\n",
         (unsigned long)(ifcnt & 0xFFU),
         (unsigned long)gconf,
         (unsigned long)chopconf,
         (unsigned long)ihold_irun,
         (unsigned long)ioin,
         (unsigned long)tmc_rms_current_ma(irun, vsense),
         (unsigned)ihold,
         (unsigned)irun,
         (unsigned)tmc_microsteps_from_mres(mres),
         ((gconf & (1UL << 2)) == 0U) ? "on" : "off",
         (unsigned)vsense);
}

static void tmc_set_current(uint32_t target_ma)
{
  uint32_t chopconf = 0U;
  uint32_t ihold_irun = 0U;
  uint8_t vsense = 0U;
  uint8_t irun;
  uint8_t ihold;

  if (tmc_uart_read_reg(3U, 0x6CU, &chopconf) != HAL_OK ||
      tmc_uart_read_reg(3U, 0x10U, &ihold_irun) != HAL_OK)
  {
    uart_write_line("err tmc b current");
    return;
  }

  irun = tmc_pick_irun_for_ma(target_ma, &vsense);
  ihold = (uint8_t)((irun / 2U) ? (irun / 2U) : 1U);

  if (vsense)
  {
    chopconf |= (1UL << 17);
  }
  else
  {
    chopconf &= ~(1UL << 17);
  }

  ihold_irun &= ~((uint32_t)0x1FU | ((uint32_t)0x1FU << 8) | ((uint32_t)0x0FU << 16));
  ihold_irun |= (uint32_t)ihold;
  ihold_irun |= ((uint32_t)irun << 8);
  ihold_irun |= ((uint32_t)8U << 16);

  if (tmc_uart_write_reg(3U, 0x6CU, chopconf) != HAL_OK ||
      tmc_uart_write_reg(3U, 0x10U, ihold_irun) != HAL_OK)
  {
    uart_write_line("err tmc b current write");
    return;
  }

  printf("ok tmc b current target_ma %lu applied_ma %lu run_cs %u hold_cs %u vsense %u\r\n",
         (unsigned long)target_ma,
         (unsigned long)tmc_rms_current_ma(irun, vsense),
         (unsigned)irun,
         (unsigned)ihold,
         (unsigned)vsense);
}

static void tmc_set_microsteps(uint16_t microsteps)
{
  uint32_t chopconf = 0U;
  int8_t mres = tmc_mres_from_microsteps(microsteps);

  if (mres < 0)
  {
    uart_write_line("err invalid microsteps");
    return;
  }
  if (tmc_uart_read_reg(3U, 0x6CU, &chopconf) != HAL_OK)
  {
    uart_write_line("err tmc b microsteps");
    return;
  }

  chopconf &= ~((uint32_t)0x0FU << 24);
  chopconf |= ((uint32_t)(uint8_t)mres << 24);
  chopconf |= (1UL << 28);

  if (tmc_uart_write_reg(3U, 0x6CU, chopconf) != HAL_OK)
  {
    uart_write_line("err tmc b microsteps write");
    return;
  }

  printf("ok tmc b microsteps %u\r\n", (unsigned)microsteps);
}

static void tmc_set_stealth(uint8_t enable)
{
  uint32_t gconf = 0U;

  if (tmc_uart_read_reg(3U, 0x00U, &gconf) != HAL_OK)
  {
    uart_write_line("err tmc b stealth");
    return;
  }

  gconf &= ~1UL;
  gconf |= ((1UL << 6) | (1UL << 7));
  if (enable)
  {
    gconf &= ~(1UL << 2);
  }
  else
  {
    gconf |= (1UL << 2);
  }

  if (tmc_uart_write_reg(3U, 0x00U, gconf) != HAL_OK)
  {
    uart_write_line("err tmc b stealth write");
    return;
  }

  printf("ok tmc b stealth %s\r\n", enable ? "on" : "off");
}

static void tmc_boot_init(void)
{
  uint32_t ioin = 0U;
  b_axis.enabled = 1U;
  b_apply_enable();
  HAL_Delay(10U);

  if (tmc_uart_read_reg(3U, 0x06U, &ioin) == HAL_OK)
  {
    printf("tmc b ioin 0x%08lX\r\n", (unsigned long)ioin);
  }
  else
  {
    uart_write_line("err tmc b probe");
  }

  tmc_set_stealth(0U);
  tmc_set_microsteps(4U);
  tmc_set_current(1000U);
  tmc_emit_status();
}

static void process_command_line(char *line)
{
  char *tokens[8] = {0};
  size_t count = 0U;
  char *cursor = strtok(line, " ");

  while (cursor != NULL && count < 8U)
  {
    tokens[count++] = cursor;
    cursor = strtok(NULL, " ");
  }

  if (count == 0U)
  {
    return;
  }

  if (strcmp(tokens[0], "status") == 0)
  {
    b_emit_status();
    tmc_emit_status();
    return;
  }

  if (count >= 2U && strcmp(tokens[0], "bounce") == 0)
  {
    if (!b_axis.enabled)
    {
      uart_write_line("err b disabled");
      return;
    }
    if (strcmp(tokens[1], "on") == 0)
    {
      b_motion_start_continuous(1, b_axis.jog_interval_us, 1U);
      uart_write_line("ok bounce on");
      b_emit_status();
      return;
    }
    if (strcmp(tokens[1], "off") == 0)
    {
      b_motion_stop();
      uart_write_line("ok bounce off");
      b_emit_status();
      return;
    }
  }

  if (strcmp(tokens[0], "stop") == 0)
  {
    b_motion_stop();
    uart_write_line("ok stop");
    b_emit_status();
    return;
  }

  if (strcmp(tokens[0], "tmc") == 0)
  {
    if (count >= 2U && strcmp(tokens[1], "status") == 0)
    {
      tmc_emit_status();
      return;
    }
    if (count >= 3U && strcmp(tokens[1], "current") == 0)
    {
      tmc_set_current((uint32_t)strtoul(tokens[2], NULL, 10));
      return;
    }
    if (count >= 3U && strcmp(tokens[1], "microsteps") == 0)
    {
      tmc_set_microsteps((uint16_t)strtoul(tokens[2], NULL, 10));
      return;
    }
    if (count >= 3U && strcmp(tokens[1], "stealth") == 0)
    {
      if (strcmp(tokens[2], "on") == 0)
      {
        tmc_set_stealth(1U);
        return;
      }
      if (strcmp(tokens[2], "off") == 0)
      {
        tmc_set_stealth(0U);
        return;
      }
    }
    uart_write_line("err invalid tmc command");
    return;
  }

  if (strcmp(tokens[0], "b") == 0)
  {
    if (count >= 3U && strcmp(tokens[1], "enable") == 0)
    {
      if (strcmp(tokens[2], "on") == 0)
      {
        b_axis.enabled = 1U;
        b_apply_enable();
        uart_write_line("ok b enable on");
        b_emit_status();
        return;
      }
      if (strcmp(tokens[2], "off") == 0)
      {
        b_axis.enabled = 0U;
        b_apply_enable();
        uart_write_line("ok b enable off");
        b_emit_status();
        return;
      }
    }

    if (count >= 2U && strcmp(tokens[1], "home") == 0)
    {
      b_home();
      return;
    }

    if (count >= 2U && strcmp(tokens[1], "zero") == 0)
    {
      b_axis.position = 0;
      b_axis.homed = 1U;
      uart_write_line("ok b zero");
      b_emit_status();
      return;
    }

    if (count >= 4U && strcmp(tokens[1], "step") == 0)
    {
      int32_t direction = strcmp(tokens[2], "-") == 0 ? -1 : 1;
      uint32_t steps = (uint32_t)strtoul(tokens[3], NULL, 10);
      if (!b_axis.enabled)
      {
        uart_write_line("err b disabled");
        return;
      }
      printf("ok b step dir %ld steps %lu moved %lu\r\n",
             (long)direction,
             (unsigned long)steps,
             (unsigned long)b_run_steps(direction, steps, b_axis.jog_interval_us, 1U));
      b_emit_status();
      return;
    }

    if (count >= 3U && strcmp(tokens[1], "bounce") == 0)
    {
      if (!b_axis.enabled)
      {
        uart_write_line("err b disabled");
        return;
      }
      if (strcmp(tokens[2], "on") == 0)
      {
        b_motion_start_continuous(1, b_axis.jog_interval_us, 1U);
        uart_write_line("ok b bounce on");
        b_emit_status();
        return;
      }
      if (strcmp(tokens[2], "off") == 0)
      {
        b_motion_stop();
        uart_write_line("ok b bounce off");
        b_emit_status();
        return;
      }
    }

    if (count >= 2U && strcmp(tokens[1], "stop") == 0)
    {
      b_motion_stop();
      uart_write_line("ok b stop");
      b_emit_status();
      return;
    }

    if (count >= 3U && strcmp(tokens[1], "interval") == 0)
    {
      uint32_t interval = strtoul(tokens[2], NULL, 10);
      if (interval < 10U)
      {
        interval = 10U;
      }
      if (interval > 20000U)
      {
        interval = 20000U;
      }
      b_axis.jog_interval_us = (uint16_t)interval;
      printf("ok b interval %u\r\n", (unsigned)b_axis.jog_interval_us);
      b_emit_status();
      return;
    }
  }

  uart_write_line("err invalid command");
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

  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK | RCC_CLOCKTYPE_SYSCLK |
                                RCC_CLOCKTYPE_PCLK1 | RCC_CLOCKTYPE_PCLK2;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV1;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;
  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_4) != HAL_OK)
  {
    Error_Handler();
  }
}

static void MX_GPIO_Init(void)
{
  GPIO_InitTypeDef GPIO_InitStruct = {0};

  __HAL_RCC_GPIOA_CLK_ENABLE();
  __HAL_RCC_GPIOB_CLK_ENABLE();
  __HAL_RCC_GPIOC_CLK_ENABLE();

  HAL_GPIO_WritePin(LED2_GPIO_Port, LED2_Pin, GPIO_PIN_RESET);
  HAL_GPIO_WritePin(B_STEP_GPIO_Port, B_STEP_Pin, GPIO_PIN_RESET);
  HAL_GPIO_WritePin(B_DIR_GPIO_Port, B_DIR_Pin, GPIO_PIN_RESET);
  HAL_GPIO_WritePin(AB_EN_GPIO_Port, AB_EN_Pin, GPIO_PIN_SET);

  GPIO_InitStruct.Pin = LED2_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(LED2_GPIO_Port, &GPIO_InitStruct);

  GPIO_InitStruct.Pin = AB_EN_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
  HAL_GPIO_Init(AB_EN_GPIO_Port, &GPIO_InitStruct);

  GPIO_InitStruct.Pin = B_STEP_Pin;
  HAL_GPIO_Init(B_STEP_GPIO_Port, &GPIO_InitStruct);

  GPIO_InitStruct.Pin = B_DIR_Pin;
  HAL_GPIO_Init(B_DIR_GPIO_Port, &GPIO_InitStruct);

  GPIO_InitStruct.Pin = B_MIN_ENDSTOP_Pin | B_MAX_ENDSTOP_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
  GPIO_InitStruct.Pull = GPIO_PULLUP;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);
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
  huart2.Instance = USART3;
  huart2.Init.BaudRate = 115200;
  huart2.Init.WordLength = UART_WORDLENGTH_8B;
  huart2.Init.StopBits = UART_STOPBITS_1;
  huart2.Init.Parity = UART_PARITY_NONE;
  huart2.Init.Mode = UART_MODE_TX_RX;
  huart2.Init.HwFlowCtl = UART_HWCONTROL_NONE;
  huart2.Init.OverSampling = UART_OVERSAMPLING_16;
  huart2.Init.OneBitSampling = UART_ONE_BIT_SAMPLE_DISABLE;
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
  HAL_UART_Transmit(&huart1, (uint8_t *)&ch, 1U, 0xFFFFU);
  return ch;
}

void Error_Handler(void)
{
  while (1)
  {
    HAL_GPIO_TogglePin(LED2_GPIO_Port, LED2_Pin);
    HAL_Delay(100U);
  }
}

void HAL_TIM_PeriodElapsedCallback(TIM_HandleTypeDef *htim)
{
  if (htim->Instance == TIM6)
  {
    if (b_motion.active == 0U || b_axis.enabled == 0U)
    {
      b_motion_stop();
      return;
    }

    if (b_motion.pulse_high_phase == 0U)
    {
      if (b_motion.stop_on_endstop != 0U)
      {
        if (b_motion.direction < 0 && b_min_endstop_triggered())
        {
          if (b_motion.bounce_mode != 0U)
          {
            b_axis.position = 0;
            b_motion.direction = 1;
            b_motion.current_interval_us = B_BOUNCE_RAMP_START_US;
            HAL_GPIO_WritePin(B_DIR_GPIO_Port, B_DIR_Pin, GPIO_PIN_SET);
          }
          else
          {
            b_motion_stop();
            return;
          }
        }
        else if (b_motion.direction > 0 && b_max_endstop_triggered())
        {
          if (b_motion.bounce_mode != 0U)
          {
            b_axis.position = (int32_t)B_TRAVEL_STEPS;
            b_motion.direction = -1;
            b_motion.current_interval_us = B_BOUNCE_RAMP_START_US;
            HAL_GPIO_WritePin(B_DIR_GPIO_Port, B_DIR_Pin, GPIO_PIN_RESET);
          }
          else
          {
            b_motion_stop();
            return;
          }
        }
      }

      HAL_GPIO_WritePin(B_DIR_GPIO_Port, B_DIR_Pin, (b_motion.direction >= 0) ? GPIO_PIN_SET : GPIO_PIN_RESET);
      HAL_GPIO_WritePin(B_STEP_GPIO_Port, B_STEP_Pin, GPIO_PIN_SET);
      b_motion.pulse_high_phase = 1U;
      __HAL_TIM_SET_AUTORELOAD(&htim6, B_STEP_PULSE_HIGH_US - 1U);
      __HAL_TIM_SET_COUNTER(&htim6, 0U);
      return;
    }

    HAL_GPIO_WritePin(B_STEP_GPIO_Port, B_STEP_Pin, GPIO_PIN_RESET);
    b_motion.pulse_high_phase = 0U;

    b_axis.position += b_motion.direction;
    b_motion.moved_steps++;

    if (b_motion.current_interval_us > b_target_interval_for_position())
    {
      uint32_t target_interval = b_target_interval_for_position();
      uint32_t next_interval = b_motion.current_interval_us - B_BOUNCE_RAMP_DELTA_US;
      b_motion.current_interval_us = (next_interval > target_interval) ? next_interval : target_interval;
    }
    else
    {
      b_motion.current_interval_us = b_target_interval_for_position();
    }

    if (b_motion.continuous == 0U && b_motion.steps_remaining > 0U)
    {
      b_motion.steps_remaining--;
      if (b_motion.steps_remaining == 0U)
      {
        b_motion_stop();
        return;
      }
    }

    __HAL_TIM_SET_AUTORELOAD(&htim6, ((b_motion.current_interval_us > B_STEP_PULSE_HIGH_US) ? (b_motion.current_interval_us - B_STEP_PULSE_HIGH_US) : 10U) - 1U);
    __HAL_TIM_SET_COUNTER(&htim6, 0U);
  }
}

#ifdef USE_FULL_ASSERT
void assert_failed(uint8_t *file, uint32_t line)
{
  (void)file;
  (void)line;
  while (1)
  {
  }
}
#endif
