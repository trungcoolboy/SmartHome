/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file    UART/UART_Printf/Src/main.c
  * @brief   STM32 #03 UART motion controller shell for XYZ axes.
  ******************************************************************************
  */
/* USER CODE END Header */

#include "main.h"

#include <limits.h>
#include <math.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

ADC_HandleTypeDef hadc1;
UART_HandleTypeDef huart1;

typedef struct
{
  const char *key;
  GPIO_TypeDef *step_port;
  uint16_t step_pin;
  GPIO_TypeDef *dir_port;
  uint16_t dir_pin;
  GPIO_TypeDef *en_port;
  uint16_t en_pin;
  GPIO_TypeDef *min_port;
  uint16_t min_pin;
  GPIO_TypeDef *max_port;
  uint16_t max_pin;
  uint8_t enabled;
  uint8_t moving;
  uint8_t homed;
  uint8_t homing;
  int32_t position;
  int32_t target;
  int32_t velocity;
  int32_t direction;
  uint32_t steps_remaining;
  uint32_t travel_steps;
  uint32_t interval_us;
  uint32_t next_step_us;
  uint32_t pulse_low_at_us;
  uint8_t pulse_high;
  uint8_t continuous;
  uint8_t stop_on_endstop;
  uint8_t state_dirty;
} AxisState;

typedef struct
{
  const char *key;
  GPIO_TypeDef *port;
  uint16_t pin;
  uint16_t duty;
  uint8_t dirty;
} PwmChannel;

typedef struct
{
  const char *key;
  uint32_t adc_channel;
  uint32_t filtered_raw;
  uint8_t filter_ready;
} TemperatureChannel;

#define AXIS_COUNT                 3U
#define LED_PWM_COUNT             11U
#define FAN_PWM_COUNT              2U
#define STEP_PULSE_HIGH_US         5U
#define DEFAULT_TRAVEL_STEPS       100000U
#define DEFAULT_MOVE_INTERVAL_US   160U
#define DEFAULT_HOME_INTERVAL_US   900U
#define STATUS_INTERVAL_MS         1000U
#define RX_LINE_MAX                96U
#define PWM_PERIOD_US              1000U
#define PWM_MAX_DUTY               1000U

static AxisState axes[AXIS_COUNT] = {
  {
    .key = "x",
    .step_port = X_STEP_GPIO_Port,
    .step_pin = X_STEP_Pin,
    .dir_port = X_DIR_GPIO_Port,
    .dir_pin = X_DIR_Pin,
    .en_port = X_EN_GPIO_Port,
    .en_pin = X_EN_Pin,
    .min_port = X_MIN_ENDSTOP_GPIO_Port,
    .min_pin = X_MIN_ENDSTOP_Pin,
    .max_port = X_MAX_ENDSTOP_GPIO_Port,
    .max_pin = X_MAX_ENDSTOP_Pin,
    .travel_steps = DEFAULT_TRAVEL_STEPS,
    .interval_us = DEFAULT_MOVE_INTERVAL_US,
  },
  {
    .key = "y",
    .step_port = Y_STEP_GPIO_Port,
    .step_pin = Y_STEP_Pin,
    .dir_port = Y_DIR_GPIO_Port,
    .dir_pin = Y_DIR_Pin,
    .en_port = Y_EN_GPIO_Port,
    .en_pin = Y_EN_Pin,
    .min_port = Y_MIN_ENDSTOP_GPIO_Port,
    .min_pin = Y_MIN_ENDSTOP_Pin,
    .max_port = Y_MAX_ENDSTOP_GPIO_Port,
    .max_pin = Y_MAX_ENDSTOP_Pin,
    .travel_steps = DEFAULT_TRAVEL_STEPS,
    .interval_us = DEFAULT_MOVE_INTERVAL_US,
  },
  {
    .key = "z",
    .step_port = Z_STEP_GPIO_Port,
    .step_pin = Z_STEP_Pin,
    .dir_port = Z_DIR_GPIO_Port,
    .dir_pin = Z_DIR_Pin,
    .en_port = Z_EN_GPIO_Port,
    .en_pin = Z_EN_Pin,
    .min_port = Z_MIN_ENDSTOP_GPIO_Port,
    .min_pin = Z_MIN_ENDSTOP_Pin,
    .max_port = Z_MAX_ENDSTOP_GPIO_Port,
    .max_pin = Z_MAX_ENDSTOP_Pin,
    .travel_steps = DEFAULT_TRAVEL_STEPS,
    .interval_us = DEFAULT_MOVE_INTERVAL_US,
  },
};

static PwmChannel led_pwm[LED_PWM_COUNT] = {
  {"led1", LED_PWM_1_GPIO_Port, LED_PWM_1_Pin, 0U, 0U},
  {"led2", LED_PWM_2_GPIO_Port, LED_PWM_2_Pin, 0U, 0U},
  {"led3", LED_PWM_3_GPIO_Port, LED_PWM_3_Pin, 0U, 0U},
  {"led4", LED_PWM_4_GPIO_Port, LED_PWM_4_Pin, 0U, 0U},
  {"led5", LED_PWM_5_GPIO_Port, LED_PWM_5_Pin, 0U, 0U},
  {"led6", LED_PWM_6_GPIO_Port, LED_PWM_6_Pin, 0U, 0U},
  {"led7", LED_PWM_7_GPIO_Port, LED_PWM_7_Pin, 0U, 0U},
  {"led8", LED_PWM_8_GPIO_Port, LED_PWM_8_Pin, 0U, 0U},
  {"led9", LED_PWM_9_GPIO_Port, LED_PWM_9_Pin, 0U, 0U},
  {"led10", LED_PWM_10_GPIO_Port, LED_PWM_10_Pin, 0U, 0U},
  {"led11", LED_PWM_11_GPIO_Port, LED_PWM_11_Pin, 0U, 0U},
};

static PwmChannel fan_pwm[FAN_PWM_COUNT] = {
  {"ledfan1", LEDFAN_PWM_1_GPIO_Port, LEDFAN_PWM_1_Pin, 0U, 0U},
  {"ledfan2", LEDFAN_PWM_2_GPIO_Port, LEDFAN_PWM_2_Pin, 0U, 0U},
};

static TemperatureChannel led_sink_temperature = {
  .key = "led_sink",
  .adc_channel = ADC_CHANNEL_1,
};

void SystemClock_Config(void);
static void MX_GPIO_Init(void);
static void MX_ADC1_Init(void);
static void MX_USART1_UART_Init(void);
static void axis_emit_state(const AxisState *axis);
static void axis_enable(AxisState *axis, uint8_t enabled);
static void axis_service(AxisState *axis, uint32_t now_us);
static void axis_start_move(AxisState *axis, int32_t direction, uint32_t steps, uint32_t interval_us, uint8_t continuous);
static void axis_stop(AxisState *axis, const char *reason);
static void dwt_init(void);
static void emit_all_states(void);
static void emit_full_status(void);
static void handle_line(char *line);
static void poll_uart(void);
static void pwm_service(uint32_t now_us);
static void emit_pwm_state(const char *group, const PwmChannel *channel);
static void emit_pwm_group(const char *group, PwmChannel *channels, uint32_t count);
static void handle_pwm_command(const char *group, PwmChannel *channels, uint32_t count, char *arg1);
static uint32_t read_adc_channel(uint32_t channel);
static uint32_t filter_temperature_raw(TemperatureChannel *sensor, uint32_t raw);
static int32_t ntc_raw_to_centi_c(uint32_t raw);
static void emit_temperature_state(TemperatureChannel *sensor);
static AxisState *find_axis(const char *key);
static GPIO_PinState dir_level(int32_t direction);
static uint32_t micros_now(void);
static uint8_t max_triggered(const AxisState *axis);
static uint8_t min_triggered(const AxisState *axis);

#if defined(__ICCARM__)
int iar_fputc(int ch);
#define PUTCHAR_PROTOTYPE int iar_fputc(int ch)
#elif defined ( __CC_ARM ) || defined(__ARMCC_VERSION)
#define PUTCHAR_PROTOTYPE int fputc(int ch, FILE *f)
#elif defined(__GNUC__)
#define PUTCHAR_PROTOTYPE int __io_putchar(int ch)
#endif

static uint32_t micros_now(void)
{
  static uint32_t last_cycle = 0U;
  static uint64_t cycle_high = 0ULL;
  uint32_t cycle = DWT->CYCCNT;
  uint32_t cycles_per_us = HAL_RCC_GetHCLKFreq() / 1000000U;

  if (cycle < last_cycle)
  {
    cycle_high += 0x100000000ULL;
  }
  last_cycle = cycle;
  return (uint32_t)((cycle_high + (uint64_t)cycle) / (uint64_t)cycles_per_us);
}

static void dwt_init(void)
{
  CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
  DWT->CYCCNT = 0U;
  DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
}

static GPIO_PinState dir_level(int32_t direction)
{
  return (direction >= 0) ? GPIO_PIN_RESET : GPIO_PIN_SET;
}

static uint8_t min_triggered(const AxisState *axis)
{
  return HAL_GPIO_ReadPin(axis->min_port, axis->min_pin) == GPIO_PIN_SET ? 1U : 0U;
}

static uint8_t max_triggered(const AxisState *axis)
{
  return HAL_GPIO_ReadPin(axis->max_port, axis->max_pin) == GPIO_PIN_SET ? 1U : 0U;
}

static AxisState *find_axis(const char *key)
{
  for (uint32_t i = 0; i < AXIS_COUNT; i++)
  {
    if (strcmp(axes[i].key, key) == 0)
    {
      return &axes[i];
    }
  }
  return NULL;
}

static void axis_stop(AxisState *axis, const char *reason)
{
  HAL_GPIO_WritePin(axis->step_port, axis->step_pin, GPIO_PIN_RESET);
  axis->moving = 0U;
  axis->homing = 0U;
  axis->continuous = 0U;
  axis->pulse_high = 0U;
  axis->steps_remaining = 0U;
  axis->velocity = 0;
  axis->target = axis->position;
  axis->state_dirty = 1U;
  if (reason != NULL)
  {
    printf("axis %s stopped %s\r\n", axis->key, reason);
  }
}

static void axis_enable(AxisState *axis, uint8_t enabled)
{
  axis->enabled = enabled != 0U ? 1U : 0U;
  HAL_GPIO_WritePin(axis->en_port, axis->en_pin, axis->enabled ? GPIO_PIN_RESET : GPIO_PIN_SET);
  if (axis->enabled == 0U)
  {
    axis_stop(axis, "disable");
  }
  axis->state_dirty = 1U;
}

static void axis_emit_state(const AxisState *axis)
{
  printf(
    "axis %s enabled %s moving %s pos %ld target %ld vel %ld homed %s homing %s min %s max %s travel %lu\r\n",
    axis->key,
    axis->enabled ? "on" : "off",
    axis->moving ? "on" : "off",
    (long)axis->position,
    (long)axis->target,
    (long)axis->velocity,
    axis->homed ? "yes" : "no",
    axis->homing ? "seeking_min" : "idle",
    min_triggered(axis) ? "trig" : "clear",
    max_triggered(axis) ? "trig" : "clear",
    (unsigned long)axis->travel_steps);
}

static void emit_all_states(void)
{
  for (uint32_t i = 0; i < AXIS_COUNT; i++)
  {
    axis_emit_state(&axes[i]);
    axes[i].state_dirty = 0U;
  }
}

static void axis_start_move(AxisState *axis, int32_t direction, uint32_t steps, uint32_t interval_us, uint8_t continuous)
{
  if (direction == 0 || (steps == 0U && continuous == 0U))
  {
    return;
  }
  if (min_triggered(axis) && max_triggered(axis))
  {
    printf("err axis %s endstops both_triggered\r\n", axis->key);
    axis_stop(axis, "fault");
    return;
  }
  if (direction < 0 && min_triggered(axis))
  {
    printf("err axis %s min_endstop\r\n", axis->key);
    axis_stop(axis, "min_endstop");
    return;
  }
  if (direction > 0 && max_triggered(axis))
  {
    printf("err axis %s max_endstop\r\n", axis->key);
    axis_stop(axis, "max_endstop");
    return;
  }
  if (interval_us < 20U)
  {
    interval_us = 20U;
  }

  axis_enable(axis, 1U);
  axis->direction = direction > 0 ? 1 : -1;
  axis->steps_remaining = steps;
  axis->interval_us = interval_us;
  axis->continuous = continuous != 0U ? 1U : 0U;
  axis->stop_on_endstop = 1U;
  axis->moving = 1U;
  axis->velocity = (int32_t)(1000000UL / interval_us) * axis->direction;
  axis->pulse_high = 0U;
  axis->next_step_us = micros_now();
  HAL_GPIO_WritePin(axis->dir_port, axis->dir_pin, dir_level(axis->direction));
  HAL_GPIO_WritePin(axis->step_port, axis->step_pin, GPIO_PIN_RESET);
  axis->state_dirty = 1U;
}

static void axis_service(AxisState *axis, uint32_t now_us)
{
  if (axis->moving == 0U || axis->enabled == 0U)
  {
    return;
  }
  if (axis->pulse_high != 0U)
  {
    if ((int32_t)(now_us - axis->pulse_low_at_us) >= 0)
    {
      HAL_GPIO_WritePin(axis->step_port, axis->step_pin, GPIO_PIN_RESET);
      axis->pulse_high = 0U;
      axis->next_step_us = now_us + axis->interval_us;
    }
    return;
  }
  if ((int32_t)(now_us - axis->next_step_us) < 0)
  {
    return;
  }

  if (axis->stop_on_endstop != 0U)
  {
    if (axis->direction < 0 && min_triggered(axis))
    {
      axis->position = 0;
      if (axis->homing != 0U)
      {
        axis->homed = 1U;
        printf("ok axis %s homed\r\n", axis->key);
      }
      axis_stop(axis, "min_endstop");
      return;
    }
    if (axis->direction > 0 && max_triggered(axis))
    {
      axis->position = (int32_t)axis->travel_steps;
      axis_stop(axis, "max_endstop");
      return;
    }
  }

  HAL_GPIO_WritePin(axis->dir_port, axis->dir_pin, dir_level(axis->direction));
  HAL_GPIO_WritePin(axis->step_port, axis->step_pin, GPIO_PIN_SET);
  axis->pulse_high = 1U;
  axis->pulse_low_at_us = now_us + STEP_PULSE_HIGH_US;
  axis->position += axis->direction;
  if (axis->continuous == 0U && axis->steps_remaining > 0U)
  {
    axis->steps_remaining--;
    if (axis->steps_remaining == 0U)
    {
      axis_stop(axis, "done");
    }
  }
}

static void handle_axis_command(AxisState *axis, char *cmd)
{
  char *arg1 = strtok(NULL, " ");
  if (strcmp(cmd, "status") == 0)
  {
    axis_emit_state(axis);
  }
  else if (strcmp(cmd, "enable") == 0)
  {
    axis_enable(axis, (arg1 != NULL && strcmp(arg1, "on") == 0) ? 1U : 0U);
    printf("ok axis %s enable %s\r\n", axis->key, axis->enabled ? "on" : "off");
    axis_emit_state(axis);
  }
  else if (strcmp(cmd, "stop") == 0)
  {
    axis_stop(axis, "cmd");
    printf("ok axis %s stop\r\n", axis->key);
    axis_emit_state(axis);
  }
  else if (strcmp(cmd, "zero") == 0)
  {
    axis->position = 0;
    axis->target = 0;
    axis->homed = 1U;
    printf("ok axis %s zero\r\n", axis->key);
    axis_emit_state(axis);
  }
  else if (strcmp(cmd, "home") == 0)
  {
    if (min_triggered(axis) && max_triggered(axis))
    {
      printf("err axis %s endstops both_triggered\r\n", axis->key);
      return;
    }
    if (min_triggered(axis))
    {
      axis->position = 0;
      axis->target = 0;
      axis->homed = 1U;
      printf("ok axis %s homed\r\n", axis->key);
      axis_emit_state(axis);
      return;
    }
    axis->homed = 0U;
    axis->homing = 1U;
    axis_start_move(axis, -1, 0U, DEFAULT_HOME_INTERVAL_US, 1U);
    printf("ok axis %s home\r\n", axis->key);
  }
  else if (strcmp(cmd, "goto") == 0 && arg1 != NULL)
  {
    int32_t target = strtol(arg1, NULL, 10);
    int32_t delta = target - axis->position;
    axis->target = target;
    axis_start_move(axis, delta >= 0 ? 1 : -1, (uint32_t)labs(delta), axis->interval_us, 0U);
    printf("ok axis %s goto %ld\r\n", axis->key, (long)target);
  }
  else if (strcmp(cmd, "move") == 0 && arg1 != NULL)
  {
    int32_t delta = strtol(arg1, NULL, 10);
    axis->target = axis->position + delta;
    axis_start_move(axis, delta >= 0 ? 1 : -1, (uint32_t)labs(delta), axis->interval_us, 0U);
    printf("ok axis %s move %ld\r\n", axis->key, (long)delta);
  }
  else if (strcmp(cmd, "jog") == 0 && arg1 != NULL)
  {
    int32_t direction = (arg1[0] == '-') ? -1 : 1;
    axis_start_move(axis, direction, 0U, axis->interval_us, 1U);
    printf("ok axis %s jog %c\r\n", axis->key, direction > 0 ? '+' : '-');
  }
  else if (strcmp(cmd, "speed") == 0 && arg1 != NULL)
  {
    axis->interval_us = strtoul(arg1, NULL, 10);
    if (axis->interval_us < 20U)
    {
      axis->interval_us = 20U;
    }
    printf("ok axis %s speed %lu\r\n", axis->key, (unsigned long)axis->interval_us);
  }
  else if (strcmp(cmd, "travel") == 0 && arg1 != NULL)
  {
    axis->travel_steps = strtoul(arg1, NULL, 10);
    printf("ok axis %s travel %lu\r\n", axis->key, (unsigned long)axis->travel_steps);
  }
  else
  {
    printf("err axis %s unknown_cmd %s\r\n", axis->key, cmd);
  }
}

static void handle_xyz_command(char *cmd)
{
  if (strcmp(cmd, "status") == 0)
  {
    emit_all_states();
  }
  else if (strcmp(cmd, "stop") == 0)
  {
    for (uint32_t i = 0; i < AXIS_COUNT; i++)
    {
      axis_stop(&axes[i], "xyz_stop");
    }
    printf("ok xyz stop\r\n");
    emit_all_states();
  }
  else if (strcmp(cmd, "home") == 0)
  {
    for (uint32_t i = 0; i < AXIS_COUNT; i++)
    {
      if (min_triggered(&axes[i]) && max_triggered(&axes[i]))
      {
        printf("err axis %s endstops both_triggered\r\n", axes[i].key);
        continue;
      }
      if (min_triggered(&axes[i]))
      {
        axes[i].position = 0;
        axes[i].target = 0;
        axes[i].homed = 1U;
        axes[i].homing = 0U;
        axis_emit_state(&axes[i]);
        continue;
      }
      axes[i].homed = 0U;
      axes[i].homing = 1U;
      axis_start_move(&axes[i], -1, 0U, DEFAULT_HOME_INTERVAL_US, 1U);
    }
    printf("ok xyz home\r\n");
  }
  else if (strcmp(cmd, "goto") == 0)
  {
    char *x_arg = strtok(NULL, " ");
    char *y_arg = strtok(NULL, " ");
    char *z_arg = strtok(NULL, " ");
    if (x_arg == NULL || y_arg == NULL || z_arg == NULL)
    {
      printf("err xyz goto args\r\n");
      return;
    }
    int32_t targets[AXIS_COUNT] = {
      strtol(x_arg, NULL, 10),
      strtol(y_arg, NULL, 10),
      strtol(z_arg, NULL, 10),
    };
    for (uint32_t i = 0; i < AXIS_COUNT; i++)
    {
      int32_t delta = targets[i] - axes[i].position;
      axes[i].target = targets[i];
      axis_start_move(&axes[i], delta >= 0 ? 1 : -1, (uint32_t)labs(delta), axes[i].interval_us, 0U);
    }
    printf("ok xyz goto %ld %ld %ld\r\n", (long)targets[0], (long)targets[1], (long)targets[2]);
  }
  else
  {
    printf("err xyz unknown_cmd %s\r\n", cmd);
  }
}

static void handle_line(char *line)
{
  char *root = strtok(line, " ");
  if (root == NULL)
  {
    return;
  }
  if (strcmp(root, "status") == 0)
  {
    emit_all_states();
  }
  else if (strcmp(root, "pins") == 0)
  {
    printf("pins uart tx PC4 CN10-35 rx PC5 CN10-37\r\n");
    printf("pins x step PB0 CN7-34 dir PB1 CN10-24 en PB2 CN10-22 min PA9 CN5-1 max PC0 CN7-38\r\n");
    printf("pins y step PA8 CN10-23 dir PC7 CN5-2 en PB12 min PB4 CN10-27 max PB5 CN10-29\r\n");
    printf("pins z step PB6 CN10-17 dir PA1 CN7-30 en PB13 min PA10 max PA11 CN10-14\r\n");
  }
  else if (strcmp(root, "axis") == 0)
  {
    char *axis_key = strtok(NULL, " ");
    char *cmd = strtok(NULL, " ");
    AxisState *axis = axis_key != NULL ? find_axis(axis_key) : NULL;
    if (axis == NULL || cmd == NULL)
    {
      printf("err axis args\r\n");
      return;
    }
    handle_axis_command(axis, cmd);
  }
  else if (strcmp(root, "xyz") == 0)
  {
    char *cmd = strtok(NULL, " ");
    if (cmd == NULL)
    {
      printf("err xyz args\r\n");
      return;
    }
    handle_xyz_command(cmd);
  }
  else
  {
    printf("err unknown_cmd %s\r\n", root);
  }
}

static void poll_uart(void)
{
  static char line[RX_LINE_MAX];
  static uint32_t used = 0U;
  uint8_t rx_byte = 0U;

  while (HAL_UART_Receive(&huart1, &rx_byte, 1, 0) == HAL_OK)
  {
    if (rx_byte == '\r')
    {
      continue;
    }
    if (rx_byte == '\n')
    {
      line[used] = '\0';
      if (used > 0U)
      {
        handle_line(line);
      }
      used = 0U;
    }
    else if (used < (RX_LINE_MAX - 1U))
    {
      line[used++] = (char)rx_byte;
    }
    else
    {
      used = 0U;
      printf("err line_too_long\r\n");
    }
  }
}

int main(void)
{
  HAL_Init();
  SystemClock_Config();
  dwt_init();
  MX_GPIO_Init();
  MX_USART1_UART_Init();

  printf("\r\nSTM32G431RB #03 XYZ boot\r\n");
  printf("USART1 on PC4/PC5 via CN10-35/37, 115200 8N1\r\n");
  printf("cmd: status | pins | axis x home/goto/move/jog/stop/zero/speed/travel | xyz goto/home/stop/status\r\n");
  emit_all_states();

  uint32_t last_status_ms = HAL_GetTick();
  while (1)
  {
    uint32_t now_us = micros_now();
    poll_uart();
    for (uint32_t i = 0; i < AXIS_COUNT; i++)
    {
      axis_service(&axes[i], now_us);
      if (axes[i].state_dirty != 0U)
      {
        axis_emit_state(&axes[i]);
        axes[i].state_dirty = 0U;
      }
    }
    if ((HAL_GetTick() - last_status_ms) >= STATUS_INTERVAL_MS)
    {
      last_status_ms = HAL_GetTick();
      HAL_GPIO_TogglePin(LED2_GPIO_Port, LED2_Pin);
      emit_all_states();
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

static void MX_GPIO_Init(void)
{
  GPIO_InitTypeDef GPIO_InitStruct = {0};

  __HAL_RCC_GPIOA_CLK_ENABLE();
  __HAL_RCC_GPIOB_CLK_ENABLE();
  __HAL_RCC_GPIOC_CLK_ENABLE();

  HAL_GPIO_WritePin(LED2_GPIO_Port, LED2_Pin, GPIO_PIN_RESET);
  HAL_GPIO_WritePin(X_STEP_GPIO_Port, X_STEP_Pin, GPIO_PIN_RESET);
  HAL_GPIO_WritePin(Y_STEP_GPIO_Port, Y_STEP_Pin, GPIO_PIN_RESET);
  HAL_GPIO_WritePin(Z_STEP_GPIO_Port, Z_STEP_Pin, GPIO_PIN_RESET);
  HAL_GPIO_WritePin(X_DIR_GPIO_Port, X_DIR_Pin, GPIO_PIN_RESET);
  HAL_GPIO_WritePin(Y_DIR_GPIO_Port, Y_DIR_Pin, GPIO_PIN_RESET);
  HAL_GPIO_WritePin(Z_DIR_GPIO_Port, Z_DIR_Pin, GPIO_PIN_RESET);
  HAL_GPIO_WritePin(X_EN_GPIO_Port, X_EN_Pin, GPIO_PIN_SET);
  HAL_GPIO_WritePin(Y_EN_GPIO_Port, Y_EN_Pin, GPIO_PIN_SET);
  HAL_GPIO_WritePin(Z_EN_GPIO_Port, Z_EN_Pin, GPIO_PIN_SET);

  GPIO_InitStruct.Pin = LED2_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(LED2_GPIO_Port, &GPIO_InitStruct);

  GPIO_InitStruct.Pin = Y_STEP_Pin|Z_DIR_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
  HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

  GPIO_InitStruct.Pin = X_STEP_Pin|X_DIR_Pin|X_EN_Pin|Y_EN_Pin|Z_STEP_Pin|Z_EN_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
  HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);

  GPIO_InitStruct.Pin = Y_DIR_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
  HAL_GPIO_Init(GPIOC, &GPIO_InitStruct);

  GPIO_InitStruct.Pin = X_MIN_ENDSTOP_Pin|Z_MIN_ENDSTOP_Pin|Z_MAX_ENDSTOP_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
  GPIO_InitStruct.Pull = GPIO_PULLUP;
  HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

  GPIO_InitStruct.Pin = Y_MIN_ENDSTOP_Pin|Y_MAX_ENDSTOP_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
  GPIO_InitStruct.Pull = GPIO_PULLUP;
  HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);

  GPIO_InitStruct.Pin = X_MAX_ENDSTOP_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
  GPIO_InitStruct.Pull = GPIO_PULLUP;
  HAL_GPIO_Init(GPIOC, &GPIO_InitStruct);
}

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
