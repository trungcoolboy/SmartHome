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
#include "a_axis_motion.h"
#include "b_axis_motion.h"
#include "servo_control.h"
#include "fan_control.h"
#include "axis_travel_store.h"

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
TIM_HandleTypeDef htim1;
TIM_HandleTypeDef htim2;
TIM_HandleTypeDef htim3;
TIM_HandleTypeDef htim4;
TIM_HandleTypeDef htim6;
TIM_HandleTypeDef htim8;
TIM_HandleTypeDef htim16;
TIM_HandleTypeDef htim17;

/* USER CODE BEGIN PV */
typedef struct
{
  const char *key;
  GPIO_TypeDef *step_port;
  uint16_t step_pin;
  GPIO_TypeDef *dir_port;
  uint16_t dir_pin;
  GPIO_TypeDef *min_endstop_port;
  uint16_t min_endstop_pin;
  GPIO_TypeDef *max_endstop_port;
  uint16_t max_endstop_pin;
  uint8_t driver_addr;
  uint8_t enabled;
  int32_t position;
  int32_t target;
  int32_t velocity;
  uint8_t moving;
  uint8_t homed;
  uint8_t homing_state;
  int32_t scan_travel_steps;
  uint32_t tuned_travel_steps;
  uint32_t decel_window_steps;
  uint32_t next_step_tick;
  uint32_t step_interval_ticks;
  uint16_t timer_countdown_ticks;
  uint8_t dir_setup_ticks;
  uint8_t step_high_ticks;
  uint8_t state_dirty;
  uint16_t start_interval_us;
  uint16_t cruise_interval_us;
  uint16_t homing_interval_us;
  uint16_t accel_interval_delta_us;
  uint32_t tmc_gconf_shadow;
  uint32_t tmc_chopconf_shadow;
  uint32_t tmc_ihold_irun_shadow;
  uint8_t tmc_shadow_valid;
} AxisState;

#define A_TUNED_TRAVEL_STEPS        49983U
#define B_TUNED_TRAVEL_STEPS         5656U
#define B_TUNED_DECEL_WINDOW_STEPS    300U

static AxisState axes[] = {
  {
    .key = "a",
    .step_port = A_STEP_GPIO_Port,
    .step_pin = A_STEP_Pin,
    .dir_port = A_DIR_GPIO_Port,
    .dir_pin = A_DIR_Pin,
    .min_endstop_port = A_MIN_ENDSTOP_GPIO_Port,
    .min_endstop_pin = A_MIN_ENDSTOP_Pin,
    .max_endstop_port = A_MAX_ENDSTOP_GPIO_Port,
    .max_endstop_pin = A_MAX_ENDSTOP_Pin,
    .driver_addr = 0U,
    .scan_travel_steps = (int32_t)A_TUNED_TRAVEL_STEPS,
    .tuned_travel_steps = A_TUNED_TRAVEL_STEPS,
    .decel_window_steps = 0U,
    .start_interval_us = 600U,
    .cruise_interval_us = 80U,
    .homing_interval_us = 850U,
    .accel_interval_delta_us = 20U,
  },
  {
    .key = "b",
    .step_port = B_STEP_GPIO_Port,
    .step_pin = B_STEP_Pin,
    .dir_port = B_DIR_GPIO_Port,
    .dir_pin = B_DIR_Pin,
    .min_endstop_port = B_MIN_ENDSTOP_GPIO_Port,
    .min_endstop_pin = B_MIN_ENDSTOP_Pin,
    .max_endstop_port = B_MAX_ENDSTOP_GPIO_Port,
    .max_endstop_pin = B_MAX_ENDSTOP_Pin,
    .driver_addr = 3U,
    .scan_travel_steps = (int32_t)B_TUNED_TRAVEL_STEPS,
    .tuned_travel_steps = B_TUNED_TRAVEL_STEPS,
    .decel_window_steps = B_TUNED_DECEL_WINDOW_STEPS,
    .start_interval_us = 2000U,
    .cruise_interval_us = 100U,
    .homing_interval_us = 2000U,
    .accel_interval_delta_us = 10U,
  },
};

typedef struct
{
  volatile uint8_t active;
  volatile uint8_t pulse_high_phase;
  volatile uint32_t cruise_interval_us;
  volatile uint32_t current_interval_us;
  volatile uint32_t start_interval_us;
  volatile uint32_t accel_delta_us;
  volatile uint32_t steps_total;
  volatile uint32_t steps_done;
  volatile uint32_t a_abs_steps;
  volatile uint32_t b_abs_steps;
  volatile uint32_t a_err;
  volatile uint32_t b_err;
  volatile int32_t a_direction;
  volatile int32_t b_direction;
  volatile int32_t a_position;
  volatile int32_t b_position;
  volatile int32_t a_target;
  volatile int32_t b_target;
  volatile uint8_t a_step_pending;
  volatile uint8_t b_step_pending;
} CoordMotionState;

static CoordMotionState coord_motion = {0};

#define AB_COORD_START_INTERVAL_US   1400U
#define AB_COORD_CRUISE_INTERVAL_US   150U
#define AB_COORD_ACCEL_DELTA_US         8U
#define AB_COORD_MIN_RAMP_STEPS       900U

static volatile uint8_t tmc_rx_ring[64];
static volatile uint16_t tmc_rx_head = 0U;
static volatile uint16_t tmc_rx_tail = 0U;
static uint8_t tmc_rx_byte = 0U;
static volatile uint8_t host_rx_ring[128];
static volatile uint16_t host_rx_head = 0U;
static volatile uint16_t host_rx_tail = 0U;
static uint8_t host_rx_byte = 0U;

#define BYJ1_CAL_STEPS_PER_42MM     50000L
#define BYJ1_CAL_MM_X1000_PER_42MM  42000L

typedef struct
{
  const char *key;
  GPIO_TypeDef *step_port;
  uint16_t step_pin;
  GPIO_TypeDef *dir_port;
  uint16_t dir_pin;
  GPIO_TypeDef *en_port;
  uint16_t en_pin;
  GPIO_TypeDef *endstop_port;
  uint16_t endstop_pin;
  uint8_t enabled;
  int32_t position;
  int32_t target;
  int32_t velocity;
  uint8_t moving;
  uint32_t next_step_tick;
  uint32_t step_interval_ticks;
  uint16_t start_interval_us;
  uint16_t cruise_interval_us;
  uint16_t accel_interval_delta_us;
} ByjStepperState;

static ByjStepperState byj_steppers[] = {
  {
    .key = "byj1",
    .step_port = BYJ1_STEP_GPIO_Port,
    .step_pin = BYJ1_STEP_Pin,
    .dir_port = BYJ1_DIR_GPIO_Port,
    .dir_pin = BYJ1_DIR_Pin,
    .en_port = BYJ1_EN_GPIO_Port,
    .en_pin = BYJ1_EN_Pin,
    .endstop_port = BYJ1_ENDSTOP_GPIO_Port,
    .endstop_pin = BYJ1_ENDSTOP_Pin,
    .start_interval_us = 8000U,
    .cruise_interval_us = 4000U,
    .accel_interval_delta_us = 200U,
  },
  {
    .key = "byj2",
    .step_port = BYJ2_STEP_GPIO_Port,
    .step_pin = BYJ2_STEP_Pin,
    .dir_port = BYJ2_DIR_GPIO_Port,
    .dir_pin = BYJ2_DIR_Pin,
    .en_port = BYJ2_EN_GPIO_Port,
    .en_pin = BYJ2_EN_Pin,
    .start_interval_us = 2200U,
    .cruise_interval_us = 900U,
    .accel_interval_delta_us = 40U,
  },
};

static int32_t byj1_mm_x1000_from_steps(int32_t steps)
{
  int64_t scaled = (int64_t)steps * (int64_t)BYJ1_CAL_MM_X1000_PER_42MM;
  if (scaled >= 0)
  {
    scaled += (BYJ1_CAL_STEPS_PER_42MM / 2);
  }
  else
  {
    scaled -= (BYJ1_CAL_STEPS_PER_42MM / 2);
  }
  return (int32_t)(scaled / BYJ1_CAL_STEPS_PER_42MM);
}

/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
static void MX_GPIO_Init(void);
static void MX_USART1_UART_Init(void);
static void MX_USART2_UART_Init(void);
static void MX_TIM1_Init(void);
static void MX_TIM2_Init(void);
static void MX_TIM3_Init(void);
static void MX_TIM4_Init(void);
static void MX_TIM6_Init(void);
static void MX_TIM8_Init(void);
static void MX_TIM16_Init(void);
static void MX_TIM17_Init(void);
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
static uint16_t axis_timer_ticks_from_us(uint32_t us);
static AxisState *find_axis(const char *key);
static uint32_t axis_effective_travel_steps(const AxisState *axis);
static void emit_axis_state(const AxisState *axis);
static void emit_all_axis_states(void);
static void emit_dirty_axis_states(void);
static void emit_tmc_status(void);
static void apply_axis_enable_state(void);
static uint32_t step_ticks_from_us(uint16_t us);
static uint32_t step_tick_now(void);
static void axis_prime_motion_profile(AxisState *axis);
static void axis_begin_motion(AxisState *axis, int32_t velocity, int32_t target, uint8_t homing_state);
static uint8_t tmc_crc8(const uint8_t *bytes, uint8_t len);
static AxisState *find_axis_by_driver_key(const char *key);
static uint32_t tmc_rms_current_ma(uint8_t irun, uint8_t vsense);
static uint8_t tmc_pick_irun_for_ma(uint32_t target_ma, uint8_t *vsense_out);
static uint16_t tmc_microsteps_from_mres(uint8_t mres);
static int8_t tmc_mres_from_microsteps(uint16_t microsteps);
static void tmc_emit_driver_status(AxisState *axis);
static void tmc_set_driver_current(AxisState *axis, uint32_t target_ma);
static void tmc_set_driver_microsteps(AxisState *axis, uint16_t microsteps);
static void tmc_set_driver_stealth(AxisState *axis, uint8_t enable);
static void tmc_uart_rx_start(void);
static void host_uart_rx_start(void);
static HAL_StatusTypeDef tmc_uart_send_bytes(const uint8_t *bytes, uint16_t len);
static HAL_StatusTypeDef tmc_uart_write_bytes(const uint8_t *bytes, uint16_t len);
static HAL_StatusTypeDef tmc_uart_write_reg_checked(uint8_t driver_addr, uint8_t reg_addr, uint32_t reg_value, uint8_t *ifcnt_before_out, uint8_t *ifcnt_after_out);
static int16_t tmc_uart_read_byte(uint32_t timeout_ms);
static HAL_StatusTypeDef tmc_uart_read_exact(uint8_t *bytes, uint16_t len, uint32_t timeout_ms);
static void tmc_uart_flush_rx(void);
static void tmc_uart_dump_raw(uint32_t window_ms);
static void host_uart_clear_errors(void);
static void tmc_uart_boot_probe(void);
static HAL_StatusTypeDef tmc_uart_read_reg(uint8_t driver_addr, uint8_t reg_addr, uint32_t *value_out);
static HAL_StatusTypeDef tmc_uart_write_reg(uint8_t driver_addr, uint8_t reg_addr, uint32_t reg_value);
static void tmc_emit_driver_probe(uint8_t driver_addr);
static uint32_t tim3_input_clock_hz(void);
static ByjStepperState *find_byj_stepper(const char *key);
static void emit_byj_stepper_state(const ByjStepperState *stepper);
static void emit_all_byj_stepper_states(void);
static void apply_byj_stepper_enable(ByjStepperState *stepper);
static void byj_prime_motion_profile(ByjStepperState *stepper);
static void byj_begin_motion(ByjStepperState *stepper, int32_t velocity, int32_t target);
static uint8_t read_byj_endstop_triggered(const ByjStepperState *stepper);
static void step_delay_us(uint16_t us);
static void pulse_byj_step(ByjStepperState *stepper, int32_t direction);
static void tick_byj_steppers(void);
static void process_command_line(char *line);
static void tick_axes(void);
static void axis_stop_motion(AxisState *axis);
static uint8_t coord_motion_active(void);
static void coord_motion_stop(void);
static void coord_motion_irq(void);
static uint8_t coord_motion_start(int32_t a_target, int32_t b_target);
static void coord_motion_emit_state(void);
static uint32_t coord_motion_ease_interval(uint32_t phase_steps, uint32_t ramp_steps, uint32_t start_interval_us, uint32_t cruise_interval_us);
static AxisState *b_axis_state(void);
/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */
static void uart_write_line(const char *text)
{
  printf("%s\r\n", text);
}

static void host_uart_rx_start(void)
{
  HAL_UART_Receive_IT(&huart1, &host_rx_byte, 1U);
}

static uint16_t axis_timer_ticks_from_us(uint32_t us)
{
  uint32_t ticks = (us + 4U) / 5U;
  if (ticks == 0U)
  {
    ticks = 1U;
  }
  if (ticks > 0xFFFFU)
  {
    ticks = 0xFFFFU;
  }
  return (uint16_t)ticks;
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

static AxisState *find_axis_by_driver_key(const char *key)
{
  return find_axis(key);
}

static AxisState *b_axis_state(void)
{
  return &axes[1];
}

static uint32_t tim3_input_clock_hz(void)
{
  RCC_ClkInitTypeDef clk_init = {0};
  uint32_t flash_latency = 0U;
  uint32_t pclk1_hz = HAL_RCC_GetPCLK1Freq();

  HAL_RCC_GetClockConfig(&clk_init, &flash_latency);
  if (clk_init.APB1CLKDivider == RCC_HCLK_DIV1)
  {
    return pclk1_hz;
  }
  return pclk1_hz * 2U;
}

static ByjStepperState *find_byj_stepper(const char *key)
{
  size_t i;
  for (i = 0U; i < sizeof(byj_steppers) / sizeof(byj_steppers[0]); i++)
  {
    if (strcmp(byj_steppers[i].key, key) == 0)
    {
      return &byj_steppers[i];
    }
  }
  return NULL;
}

static void emit_byj_stepper_state(const ByjStepperState *stepper)
{
  if (strcmp(stepper->key, "byj1") == 0)
  {
    Byj1MotionSnapshot snapshot;
    int32_t mm_x1000;
    byj1_motion_get_snapshot(&snapshot);
    mm_x1000 = byj1_mm_x1000_from_steps(snapshot.position);
    printf("byj byj1 enabled %s moving %s pos %ld target %ld vel %ld endstop %s mm %ld.%03ld\r\n",
           snapshot.enabled ? "on" : "off",
           snapshot.moving ? "on" : "off",
           (long)snapshot.position,
           (long)snapshot.target,
           (long)snapshot.velocity,
           byj1_motion_endstop_triggered() ? "trig" : "clear",
           (long)(mm_x1000 / 1000),
           (long)labs(mm_x1000 % 1000));
    return;
  }
  if (strcmp(stepper->key, "byj2") == 0)
  {
    Byj2MotionSnapshot snapshot;
    byj2_motion_get_snapshot(&snapshot);
    printf("byj byj2 enabled %s moving %s pos %ld target %ld vel %ld endstop clear\r\n",
           snapshot.enabled ? "on" : "off",
           snapshot.moving ? "on" : "off",
           (long)snapshot.position,
           (long)snapshot.target,
           (long)snapshot.velocity);
    return;
  }
  printf("byj %s enabled %s moving %s pos %ld target %ld vel %ld endstop %s\r\n",
         stepper->key,
         stepper->enabled ? "on" : "off",
         stepper->moving ? "on" : "off",
         (long)stepper->position,
         (long)stepper->target,
         (long)stepper->velocity,
         read_byj_endstop_triggered(stepper) ? "trig" : "clear");
}

static void emit_all_byj_stepper_states(void)
{
  size_t i;
  for (i = 0U; i < sizeof(byj_steppers) / sizeof(byj_steppers[0]); i++)
  {
    emit_byj_stepper_state(&byj_steppers[i]);
  }
}

static void apply_byj_stepper_enable(ByjStepperState *stepper)
{
  HAL_GPIO_WritePin(stepper->en_port, stepper->en_pin, stepper->enabled ? GPIO_PIN_RESET : GPIO_PIN_SET);
}

static uint8_t read_byj_endstop_triggered(const ByjStepperState *stepper)
{
  if (stepper->endstop_port == NULL || stepper->endstop_pin == 0U)
  {
    return 0U;
  }
  return HAL_GPIO_ReadPin(stepper->endstop_port, stepper->endstop_pin) == GPIO_PIN_SET ? 1U : 0U;
}

static void byj_prime_motion_profile(ByjStepperState *stepper)
{
  stepper->step_interval_ticks = step_ticks_from_us(stepper->start_interval_us);
  stepper->next_step_tick = step_tick_now();
}

static void byj_begin_motion(ByjStepperState *stepper, int32_t velocity, int32_t target)
{
  stepper->velocity = velocity;
  stepper->target = target;
  stepper->moving = velocity == 0 ? 0U : 1U;
  if (stepper->moving)
  {
    byj_prime_motion_profile(stepper);
  }
  else
  {
    stepper->next_step_tick = 0U;
  }
}

static void pulse_byj_step(ByjStepperState *stepper, int32_t direction)
{
  HAL_GPIO_WritePin(stepper->dir_port, stepper->dir_pin, (direction >= 0) ? GPIO_PIN_SET : GPIO_PIN_RESET);
  step_delay_us(20U);
  HAL_GPIO_WritePin(stepper->step_port, stepper->step_pin, GPIO_PIN_SET);
  step_delay_us(40U);
  HAL_GPIO_WritePin(stepper->step_port, stepper->step_pin, GPIO_PIN_RESET);
  step_delay_us(20U);
}

static void tick_byj_steppers(void)
{
  uint32_t now_us = step_tick_now();
  size_t i;

  byj1_motion_tick();
  byj2_motion_tick();

  for (i = 0U; i < sizeof(byj_steppers) / sizeof(byj_steppers[0]); i++)
  {
    ByjStepperState *stepper = &byj_steppers[i];
    int32_t remaining;
    uint32_t decel_steps;

    if (strcmp(stepper->key, "byj1") == 0)
    {
      continue;
    }
    if (strcmp(stepper->key, "byj2") == 0)
    {
      continue;
    }

    if (!stepper->moving || !stepper->enabled)
    {
      continue;
    }
    if (read_byj_endstop_triggered(stepper) && stepper->velocity < 0)
    {
      stepper->moving = 0U;
      stepper->velocity = 0;
      stepper->next_step_tick = 0U;
      if (stepper->target == INT32_MIN)
      {
        stepper->position = 0;
        stepper->target = 0;
        printf("ok byj %s homed\r\n", stepper->key);
      }
      else
      {
        stepper->target = stepper->position;
        printf("ok byj %s min_endstop\r\n", stepper->key);
      }
      emit_byj_stepper_state(stepper);
      continue;
    }
    if ((int32_t)(now_us - stepper->next_step_tick) < 0)
    {
      continue;
    }
    if (stepper->position == stepper->target)
    {
      stepper->moving = 0U;
      stepper->velocity = 0;
      stepper->next_step_tick = 0U;
      emit_byj_stepper_state(stepper);
      continue;
    }

    remaining = stepper->target - stepper->position;
    if (remaining < 0)
    {
      remaining = -remaining;
    }
    decel_steps = (uint32_t)((stepper->start_interval_us - stepper->cruise_interval_us) / stepper->accel_interval_delta_us) + 2U;

    if (stepper->target == INT32_MAX || stepper->target == INT32_MIN)
    {
      if (stepper->step_interval_ticks > step_ticks_from_us(stepper->cruise_interval_us))
      {
        uint32_t next_interval = stepper->step_interval_ticks;
        uint32_t cruise_ticks = step_ticks_from_us(stepper->cruise_interval_us);
        uint32_t delta_ticks = step_ticks_from_us(stepper->accel_interval_delta_us);
        if ((next_interval - cruise_ticks) > delta_ticks)
        {
          next_interval -= delta_ticks;
        }
        else
        {
          next_interval = cruise_ticks;
        }
        stepper->step_interval_ticks = next_interval;
      }
    }
    else if ((uint32_t)remaining <= decel_steps)
    {
      uint32_t start_ticks = step_ticks_from_us(stepper->start_interval_us);
      uint32_t delta_ticks = step_ticks_from_us(stepper->accel_interval_delta_us);
      if (stepper->step_interval_ticks < start_ticks)
      {
        uint32_t next_interval = stepper->step_interval_ticks + delta_ticks;
        stepper->step_interval_ticks = (next_interval > start_ticks) ? start_ticks : next_interval;
      }
    }
    else if (stepper->step_interval_ticks > step_ticks_from_us(stepper->cruise_interval_us))
    {
      uint32_t next_interval = stepper->step_interval_ticks;
      uint32_t cruise_ticks = step_ticks_from_us(stepper->cruise_interval_us);
      uint32_t delta_ticks = step_ticks_from_us(stepper->accel_interval_delta_us);
      if ((next_interval - cruise_ticks) > delta_ticks)
      {
        next_interval -= delta_ticks;
      }
      else
      {
        next_interval = cruise_ticks;
      }
      stepper->step_interval_ticks = next_interval;
    }

    stepper->position += stepper->velocity;
    pulse_byj_step(stepper, stepper->velocity);
    stepper->next_step_tick = now_us + stepper->step_interval_ticks;

    if (stepper->target == INT32_MAX || stepper->target == INT32_MIN)
    {
      continue;
    }
    if (stepper->position == stepper->target)
    {
      stepper->moving = 0U;
      stepper->velocity = 0;
      stepper->next_step_tick = 0U;
      emit_byj_stepper_state(stepper);
    }
  }
}

static uint8_t axis_min_endstop_triggered(const AxisState *axis)
{
  return HAL_GPIO_ReadPin(axis->min_endstop_port, axis->min_endstop_pin) == GPIO_PIN_SET ? 1U : 0U;
}

static uint8_t axis_max_endstop_triggered(const AxisState *axis)
{
  return HAL_GPIO_ReadPin(axis->max_endstop_port, axis->max_endstop_pin) == GPIO_PIN_SET ? 1U : 0U;
}

static const char *axis_homing_state_name(const AxisState *axis)
{
  switch (axis->homing_state)
  {
    case 1U:
      return "seek_min";
    case 2U:
      return "release_min";
    case 3U:
      return "scan_seek_min";
    case 4U:
      return "scan_release_min";
    case 5U:
      return "scan_seek_max";
    case 6U:
      return "scan_release_max";
    default:
      return "idle";
  }
}

static const char *b_snapshot_homing_state_name(const BAxisMotionSnapshot *snapshot)
{
  switch (snapshot->homing_state)
  {
    case 1U:
      return "seek_min";
    case 2U:
      return "release_min";
    case 3U:
      return "scan_seek_min";
    case 4U:
      return "scan_release_min";
    case 5U:
      return "scan_seek_max";
    case 6U:
      return "scan_release_max";
    default:
      return "idle";
  }
}

static const char *a_snapshot_homing_state_name(const AAxisMotionSnapshot *snapshot)
{
  switch (snapshot->homing_state)
  {
    case 1U:
      return "seek_min";
    case 2U:
      return "release_min";
    case 3U:
      return "scan_seek_min";
    case 4U:
      return "scan_release_min";
    case 5U:
      return "scan_seek_max";
    case 6U:
      return "scan_release_max";
    default:
      return "idle";
  }
}

static void emit_axis_state(const AxisState *axis)
{
  if (strcmp(axis->key, "a") == 0)
  {
    AAxisMotionSnapshot snapshot;
    a_axis_motion_get_snapshot(&snapshot);
    printf(
      "axis %s enabled %s moving %s pos %ld target %ld vel %ld homed %s homing %s endstop_min %s endstop_max %s raw_pa9 %u raw_pc0 %u travel %lu decel_window %lu\r\n",
      axis->key,
      snapshot.enabled ? "on" : "off",
      snapshot.moving ? "on" : "off",
      (long)snapshot.position,
      (long)snapshot.target,
      (long)snapshot.velocity,
      snapshot.homed ? "yes" : "no",
      a_snapshot_homing_state_name(&snapshot),
      a_axis_motion_min_endstop_triggered() ? "trig" : "clear",
      a_axis_motion_max_endstop_triggered() ? "trig" : "clear",
      (unsigned)HAL_GPIO_ReadPin(A_MIN_ENDSTOP_GPIO_Port, A_MIN_ENDSTOP_Pin),
      (unsigned)HAL_GPIO_ReadPin(A_MAX_ENDSTOP_GPIO_Port, A_MAX_ENDSTOP_Pin),
      (unsigned long)snapshot.travel_steps,
      (unsigned long)snapshot.decel_window_steps
    );
    return;
  }

  if (axis == b_axis_state())
  {
    BAxisMotionSnapshot snapshot;
    b_axis_motion_get_snapshot(&snapshot);
    printf(
      "axis %s enabled %s moving %s pos %ld target %ld vel %ld homed %s homing %s endstop_min %s endstop_max %s travel %lu decel_window %lu\r\n",
      axis->key,
      snapshot.enabled ? "on" : "off",
      snapshot.moving ? "on" : "off",
      (long)snapshot.position,
      (long)snapshot.target,
      (long)snapshot.velocity,
      snapshot.homed ? "yes" : "no",
      b_snapshot_homing_state_name(&snapshot),
      axis_min_endstop_triggered(axis) ? "trig" : "clear",
      axis_max_endstop_triggered(axis) ? "trig" : "clear",
      (unsigned long)snapshot.travel_steps,
      (unsigned long)snapshot.decel_window_steps
    );
    return;
  }

  printf(
    "axis %s enabled %s moving %s pos %ld target %ld vel %ld homed %s homing %s endstop_min %s endstop_max %s travel %lu decel_window %lu\r\n",
    axis->key,
    axis->enabled ? "on" : "off",
    axis->moving ? "on" : "off",
    (long)axis->position,
    (long)axis->target,
    (long)axis->velocity,
    axis->homed ? "yes" : "no",
    axis_homing_state_name(axis),
    axis_min_endstop_triggered(axis) ? "trig" : "clear",
    axis_max_endstop_triggered(axis) ? "trig" : "clear",
    (unsigned long)axis_effective_travel_steps(axis),
    (unsigned long)axis->decel_window_steps
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

static void emit_dirty_axis_states(void)
{
  size_t i;
  for (i = 0U; i < sizeof(axes) / sizeof(axes[0]); i++)
  {
    if (axes[i].state_dirty)
    {
      axes[i].state_dirty = 0U;
      emit_axis_state(&axes[i]);
    }
  }
}

static void emit_tmc_status(void)
{
  printf("tmc uart tx PB10 rx PB11 ab_en active_low endstops a_min PA9 a_max PC0 b_min PB4 b_max PB5 addr_a %u addr_b %u\r\n",
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

static uint32_t step_ticks_from_us(uint16_t us)
{
  const uint32_t cycles_per_us = HAL_RCC_GetHCLKFreq() / 1000000U;
  return cycles_per_us * (uint32_t)us;
}

static uint32_t axis_effective_travel_steps(const AxisState *axis)
{
  if (axis->tuned_travel_steps > 0U)
  {
    return axis->tuned_travel_steps;
  }
  if (axis->scan_travel_steps > 0)
  {
    return (uint32_t)axis->scan_travel_steps;
  }
  if (strcmp(axis->key, "b") == 0)
  {
    return B_TUNED_TRAVEL_STEPS;
  }
  return 0U;
}

static uint32_t step_tick_now(void)
{
  return DWT->CYCCNT;
}

static void axis_prime_motion_profile(AxisState *axis)
{
  if (axis->homing_state != 0U)
  {
    axis->step_interval_ticks = axis->homing_interval_us;
  }
  else
  {
    axis->step_interval_ticks = axis->start_interval_us;
  }
  axis->timer_countdown_ticks = axis_timer_ticks_from_us(axis->step_interval_ticks);
  axis->dir_setup_ticks = 0U;
  axis->step_high_ticks = 0U;
  axis->next_step_tick = 0U;
}

static void axis_begin_motion(AxisState *axis, int32_t velocity, int32_t target, uint8_t homing_state)
{
  axis->velocity = velocity;
  axis->target = target;
  axis->homing_state = homing_state;
  axis->moving = velocity == 0 ? 0U : 1U;
  if (axis->moving)
  {
    axis_prime_motion_profile(axis);
  }
  else
  {
    axis->timer_countdown_ticks = 0U;
    axis->dir_setup_ticks = 0U;
    axis->step_high_ticks = 0U;
    axis->next_step_tick = 0U;
  }
}

static void axis_stop_motion(AxisState *axis)
{
  axis->target = axis->position;
  axis->velocity = 0;
  axis->moving = 0U;
  axis->homing_state = 0U;
  axis->next_step_tick = 0U;
  axis->timer_countdown_ticks = 0U;
  axis->dir_setup_ticks = 0U;
  axis->step_high_ticks = 0U;
  HAL_GPIO_WritePin(axis->step_port, axis->step_pin, GPIO_PIN_RESET);
}

static void step_delay_us(uint16_t us)
{
  uint32_t start = step_tick_now();
  uint32_t ticks = step_ticks_from_us(us);
  while ((uint32_t)(step_tick_now() - start) < ticks)
  {
  }
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

static void tmc_uart_rx_start(void)
{
  HAL_UART_Receive_IT(&huart2, &tmc_rx_byte, 1U);
}

static HAL_StatusTypeDef tmc_uart_send_bytes(const uint8_t *bytes, uint16_t len)
{
  if (len == 0U)
  {
    return HAL_OK;
  }
  CLEAR_BIT(huart2.Instance->CR1, USART_CR1_RE);
  tmc_uart_flush_rx();
  if (HAL_UART_Transmit(&huart2, (uint8_t *)bytes, len, 50U) != HAL_OK)
  {
    SET_BIT(huart2.Instance->CR1, USART_CR1_RE);
    return HAL_ERROR;
  }
  tmc_uart_flush_rx();
  SET_BIT(huart2.Instance->CR1, USART_CR1_RE);
  return HAL_OK;
}

static HAL_StatusTypeDef tmc_uart_write_bytes(const uint8_t *bytes, uint16_t len)
{
  if (len == 0U)
  {
    return HAL_OK;
  }
  tmc_uart_flush_rx();
  if (HAL_UART_Transmit(&huart2, (uint8_t *)bytes, len, 50U) != HAL_OK)
  {
    return HAL_ERROR;
  }
  HAL_Delay(2U);
  return HAL_OK;
}

static int16_t tmc_uart_read_byte(uint32_t timeout_ms)
{
  uint32_t start_ms = HAL_GetTick();
  while ((HAL_GetTick() - start_ms) < timeout_ms)
  {
    if (tmc_rx_head != tmc_rx_tail)
    {
      uint8_t value = tmc_rx_ring[tmc_rx_tail];
      tmc_rx_tail = (uint16_t)((tmc_rx_tail + 1U) % (sizeof(tmc_rx_ring) / sizeof(tmc_rx_ring[0])));
      return (int16_t)value;
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

static void tmc_uart_flush_rx(void)
{
  __disable_irq();
  tmc_rx_head = 0U;
  tmc_rx_tail = 0U;
  __enable_irq();
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

static void tmc_uart_boot_probe(void)
{
  axes[0].enabled = 1U;
  apply_axis_enable_state();
  HAL_Delay(10U);
  tmc_emit_driver_probe(axes[0].driver_addr);
  tmc_set_driver_stealth(&axes[0], 0U);
  tmc_set_driver_microsteps(&axes[0], 16U);
  tmc_set_driver_current(&axes[0], 600U);
  tmc_emit_driver_status(&axes[0]);
  tmc_emit_driver_probe(axes[1].driver_addr);
  tmc_set_driver_stealth(&axes[1], 0U);
  tmc_set_driver_microsteps(&axes[1], 4U);
  tmc_set_driver_current(&axes[1], 1000U);
  tmc_emit_driver_status(&axes[1]);
  tmc_uart_dump_raw(40U);
}

static void host_uart_clear_errors(void)
{
  if (__HAL_UART_GET_FLAG(&huart1, UART_FLAG_ORE) != RESET)
  {
    __HAL_UART_CLEAR_OREFLAG(&huart1);
  }
  if (__HAL_UART_GET_FLAG(&huart1, UART_FLAG_NE) != RESET)
  {
    __HAL_UART_CLEAR_NEFLAG(&huart1);
  }
  if (__HAL_UART_GET_FLAG(&huart1, UART_FLAG_FE) != RESET)
  {
    __HAL_UART_CLEAR_FEFLAG(&huart1);
  }
  if (__HAL_UART_GET_FLAG(&huart1, UART_FLAG_PE) != RESET)
  {
    __HAL_UART_CLEAR_PEFLAG(&huart1);
  }
}

static HAL_StatusTypeDef tmc_uart_read_reg(uint8_t driver_addr, uint8_t reg_addr, uint32_t *value_out)
{
  uint8_t request[4] = {0x05U, driver_addr, reg_addr, 0U};
  uint8_t reply[8] = {0};
  uint32_t sync = 0U;
  uint32_t sync_target = ((uint32_t)request[0] << 16) | 0xFF00U | reg_addr;
  uint8_t attempt;

  request[3] = tmc_crc8(request, 3U);

  for (attempt = 0U; attempt < 2U; attempt++)
  {
    uint32_t timeout_ms = 5U;
    uint32_t start_ms = HAL_GetTick();

    tmc_uart_flush_rx();

    if (tmc_uart_send_bytes(request, sizeof(request)) != HAL_OK)
    {
      return HAL_ERROR;
    }

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
  if (tmc_uart_write_bytes(datagram, sizeof(datagram)) != HAL_OK)
  {
    return HAL_ERROR;
  }
  return HAL_OK;
}

static HAL_StatusTypeDef tmc_uart_write_reg_checked(uint8_t driver_addr, uint8_t reg_addr, uint32_t reg_value, uint8_t *ifcnt_before_out, uint8_t *ifcnt_after_out)
{
  uint32_t ifcnt_before = 0U;
  uint8_t attempt;

  if (tmc_uart_read_reg(driver_addr, 0x02U, &ifcnt_before) != HAL_OK)
  {
    return HAL_ERROR;
  }

  *ifcnt_before_out = (uint8_t)(ifcnt_before & 0xFFU);
  *ifcnt_after_out = *ifcnt_before_out;

  for (attempt = 0U; attempt < 3U; attempt++)
  {
    uint32_t ifcnt_after = 0U;
    if (tmc_uart_write_reg(driver_addr, reg_addr, reg_value) != HAL_OK)
    {
      return HAL_ERROR;
    }
    if (tmc_uart_read_reg(driver_addr, 0x02U, &ifcnt_after) == HAL_OK)
    {
      *ifcnt_after_out = (uint8_t)(ifcnt_after & 0xFFU);
      if (*ifcnt_after_out != *ifcnt_before_out)
      {
        return HAL_OK;
      }
    }
    HAL_Delay(2U);
  }

  return HAL_TIMEOUT;
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

static void tmc_emit_driver_status(AxisState *axis)
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
  uint16_t microsteps;
  uint32_t current_ma;
  uint8_t stealth;

  if (tmc_uart_read_reg(axis->driver_addr, 0x02U, &ifcnt) != HAL_OK ||
      tmc_uart_read_reg(axis->driver_addr, 0x00U, &gconf) != HAL_OK ||
      tmc_uart_read_reg(axis->driver_addr, 0x6CU, &chopconf) != HAL_OK ||
      tmc_uart_read_reg(axis->driver_addr, 0x06U, &ioin) != HAL_OK)
  {
    printf("err tmc %s status\r\n", axis->key);
    return;
  }

  if (tmc_uart_read_reg(axis->driver_addr, 0x10U, &ihold_irun) != HAL_OK)
  {
    ihold_irun = axis->tmc_ihold_irun_shadow;
  }

  if (axis->tmc_shadow_valid)
  {
    ihold_irun = axis->tmc_ihold_irun_shadow;
  }

  if (axis->tmc_shadow_valid)
  {
    if ((gconf & 1UL) == 0UL)
    {
      axis->tmc_gconf_shadow = gconf;
    }
    gconf = axis->tmc_gconf_shadow ? axis->tmc_gconf_shadow : gconf;
    chopconf = axis->tmc_chopconf_shadow ? axis->tmc_chopconf_shadow : chopconf;
  }

  irun = (uint8_t)((ihold_irun >> 8) & 0x1FU);
  ihold = (uint8_t)(ihold_irun & 0x1FU);
  vsense = (uint8_t)((chopconf >> 17) & 0x01U);
  mres = (uint8_t)((chopconf >> 24) & 0x0FU);
  microsteps = tmc_microsteps_from_mres(mres);
  current_ma = tmc_rms_current_ma(irun, vsense);
  stealth = (uint8_t)(((gconf >> 2) & 0x01U) == 0U);

  printf(
    "tmc %s ifcnt %lu gconf 0x%08lX chopconf 0x%08lX ihold_irun 0x%08lX ioin 0x%08lX current_ma %lu hold_cs %u run_cs %u microsteps %u stealth %s vsense %u\r\n",
    axis->key,
    (unsigned long)(ifcnt & 0xFFU),
    (unsigned long)gconf,
    (unsigned long)chopconf,
    (unsigned long)ihold_irun,
    (unsigned long)ioin,
    (unsigned long)current_ma,
    (unsigned)ihold,
    (unsigned)irun,
    (unsigned)microsteps,
    stealth ? "on" : "off",
    (unsigned)vsense
  );
}

static void tmc_set_driver_current(AxisState *axis, uint32_t target_ma)
{
  uint32_t chopconf = 0U;
  uint32_t ihold_irun = 0U;
  uint32_t chopconf_after = 0U;
  uint8_t vsense = 0U;
  uint8_t irun;
  uint8_t ihold;
  uint32_t applied_ma;
  uint8_t ifcnt_before = 0U;
  uint8_t ifcnt_after = 0U;

  if (target_ma == 0U)
  {
    printf("err tmc %s current\r\n", axis->key);
    return;
  }

  if (tmc_uart_read_reg(axis->driver_addr, 0x6CU, &chopconf) != HAL_OK ||
      tmc_uart_read_reg(axis->driver_addr, 0x10U, &ihold_irun) != HAL_OK)
  {
    printf("err tmc %s current\r\n", axis->key);
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

  axis->tmc_chopconf_shadow = chopconf;
  axis->tmc_ihold_irun_shadow = ihold_irun;
  axis->tmc_shadow_valid = 1U;

  if (tmc_uart_write_reg_checked(axis->driver_addr, 0x6CU, chopconf, &ifcnt_before, &ifcnt_after) != HAL_OK ||
      tmc_uart_write_reg_checked(axis->driver_addr, 0x10U, ihold_irun, &ifcnt_before, &ifcnt_after) != HAL_OK)
  {
    printf("err tmc %s current write ifcnt %u->%u\r\n", axis->key, (unsigned)ifcnt_before, (unsigned)ifcnt_after);
    return;
  }

  applied_ma = tmc_rms_current_ma(irun, vsense);
  (void)tmc_uart_read_reg(axis->driver_addr, 0x6CU, &chopconf_after);
  printf(
    "ok tmc %s current target_ma %lu applied_ma %lu run_cs %u hold_cs %u vsense %u ifcnt %u->%u chopconf 0x%08lX\r\n",
    axis->key,
    (unsigned long)target_ma,
    (unsigned long)applied_ma,
    (unsigned)irun,
    (unsigned)ihold,
    (unsigned)vsense,
    (unsigned)ifcnt_before,
    (unsigned)ifcnt_after,
    (unsigned long)chopconf_after
  );
}

static void tmc_set_driver_microsteps(AxisState *axis, uint16_t microsteps)
{
  uint32_t chopconf = 0U;
  uint32_t chopconf_after = 0U;
  int8_t mres = tmc_mres_from_microsteps(microsteps);
  uint8_t ifcnt_before = 0U;
  uint8_t ifcnt_after = 0U;

  if (mres < 0)
  {
    printf("err tmc %s microsteps\r\n", axis->key);
    return;
  }

  if (tmc_uart_read_reg(axis->driver_addr, 0x6CU, &chopconf) != HAL_OK)
  {
    printf("err tmc %s microsteps\r\n", axis->key);
    return;
  }

  chopconf &= ~((uint32_t)0x0FU << 24);
  chopconf |= ((uint32_t)(uint8_t)mres << 24);
  chopconf |= (1UL << 28);

  axis->tmc_chopconf_shadow = chopconf;
  axis->tmc_shadow_valid = 1U;

  if (tmc_uart_write_reg_checked(axis->driver_addr, 0x6CU, chopconf, &ifcnt_before, &ifcnt_after) != HAL_OK)
  {
    printf("err tmc %s microsteps write ifcnt %u->%u\r\n", axis->key, (unsigned)ifcnt_before, (unsigned)ifcnt_after);
    return;
  }

  (void)tmc_uart_read_reg(axis->driver_addr, 0x6CU, &chopconf_after);
  printf("ok tmc %s microsteps %u ifcnt %u->%u chopconf 0x%08lX\r\n",
         axis->key,
         (unsigned)microsteps,
         (unsigned)ifcnt_before,
         (unsigned)ifcnt_after,
         (unsigned long)chopconf_after);
}

static void tmc_set_driver_stealth(AxisState *axis, uint8_t enable)
{
  uint32_t gconf = 0U;
  uint32_t gconf_after = 0U;
  uint8_t ifcnt_before = 0U;
  uint8_t ifcnt_after = 0U;

  if (tmc_uart_read_reg(axis->driver_addr, 0x00U, &gconf) != HAL_OK)
  {
    printf("err tmc %s stealth\r\n", axis->key);
    return;
  }

  gconf &= ~1UL; /* i_scale_analog = 0, use UART current scaling */
  gconf |= ((1UL << 6) | (1UL << 7));
  if (enable)
  {
    gconf &= ~(1UL << 2);
  }
  else
  {
    gconf |= (1UL << 2);
  }

  axis->tmc_gconf_shadow = gconf;
  axis->tmc_shadow_valid = 1U;

  if (tmc_uart_write_reg_checked(axis->driver_addr, 0x00U, gconf, &ifcnt_before, &ifcnt_after) != HAL_OK)
  {
    printf("err tmc %s stealth write ifcnt %u->%u\r\n", axis->key, (unsigned)ifcnt_before, (unsigned)ifcnt_after);
    return;
  }

  (void)tmc_uart_read_reg(axis->driver_addr, 0x00U, &gconf_after);
  printf("ok tmc %s stealth %s ifcnt %u->%u gconf 0x%08lX\r\n",
         axis->key,
         enable ? "on" : "off",
         (unsigned)ifcnt_before,
         (unsigned)ifcnt_after,
         (unsigned long)gconf_after);
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
    emit_all_byj_stepper_states();
    emit_tmc_status();
    servo_emit_all_states();
    fan_emit_all_states();
    fan_power_relay_emit_all_states();
    magnet_emit_state();
    return;
  }

  if (strcmp(tokens[0], "ab") == 0)
  {
    AAxisMotionSnapshot a_snapshot;
    BAxisMotionSnapshot b_snapshot;
    int32_t a_target;
    int32_t b_target;

    if (token_count < 2U)
    {
      uart_write_line("err invalid ab command");
      return;
    }

    if (strcmp(tokens[1], "status") == 0)
    {
      coord_motion_emit_state();
      return;
    }

    if (strcmp(tokens[1], "stop") == 0)
    {
      a_axis_motion_get_snapshot(&a_snapshot);
      b_axis_motion_get_snapshot(&b_snapshot);
      coord_motion_stop();
      a_axis_motion_stop();
      b_axis_motion_stop();
      a_axis_motion_set_position(a_snapshot.position, 0U);
      b_axis_motion_set_position(b_snapshot.position, 0U);
      uart_write_line("ok ab stop");
      coord_motion_emit_state();
      emit_axis_state(&axes[0]);
      emit_axis_state(&axes[1]);
      return;
    }

    if (token_count < 4U)
    {
      uart_write_line("err invalid ab command");
      return;
    }

    a_axis_motion_get_snapshot(&a_snapshot);
    b_axis_motion_get_snapshot(&b_snapshot);

    axes[0].enabled = 1U;
    axes[1].enabled = 1U;
    apply_axis_enable_state();
    a_axis_motion_set_enabled(1U);
    b_axis_motion_set_enabled(1U);

    a_axis_motion_stop();
    b_axis_motion_stop();
    coord_motion_stop();

    if (strcmp(tokens[1], "goto") == 0)
    {
      a_target = (int32_t)strtol(tokens[2], NULL, 10);
      b_target = (int32_t)strtol(tokens[3], NULL, 10);
      if (coord_motion_start(a_target, b_target) == 0U)
      {
        return;
      }
      printf("ok ab goto %ld %ld\r\n", (long)a_target, (long)b_target);
      coord_motion_emit_state();
      return;
    }

    if (strcmp(tokens[1], "move") == 0)
    {
      a_target = a_snapshot.position + (int32_t)strtol(tokens[2], NULL, 10);
      b_target = b_snapshot.position + (int32_t)strtol(tokens[3], NULL, 10);
      if (coord_motion_start(a_target, b_target) == 0U)
      {
        return;
      }
      printf("ok ab move %ld %ld\r\n",
             (long)(a_target - a_snapshot.position),
             (long)(b_target - b_snapshot.position));
      coord_motion_emit_state();
      return;
    }

    uart_write_line("err unsupported ab command");
    return;
  }

  if (strcmp(tokens[0], "byj") == 0)
  {
    if (token_count >= 2U && strcmp(tokens[1], "status") == 0)
    {
      emit_all_byj_stepper_states();
      return;
    }

    if (token_count < 3U)
    {
      uart_write_line("err invalid byj command");
      return;
    }

    {
      ByjStepperState *stepper = find_byj_stepper(tokens[1]);
      if (stepper == NULL)
      {
        uart_write_line("err unknown byj");
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
        if (strcmp(stepper->key, "byj1") == 0)
        {
          byj1_motion_set_enabled(1U);
          printf("ok byj %s enable on\r\n", stepper->key);
          emit_byj_stepper_state(stepper);
          return;
        }
        if (strcmp(stepper->key, "byj2") == 0)
        {
          byj2_motion_set_enabled(1U);
          printf("ok byj %s enable on\r\n", stepper->key);
          emit_byj_stepper_state(stepper);
          return;
        }
        stepper->enabled = 1U;
        apply_byj_stepper_enable(stepper);
        printf("ok byj %s enable on\r\n", stepper->key);
          emit_byj_stepper_state(stepper);
          return;
        }
        if (strcmp(tokens[3], "off") == 0)
        {
        if (strcmp(stepper->key, "byj1") == 0)
        {
          byj1_motion_set_enabled(0U);
          printf("ok byj %s enable off\r\n", stepper->key);
          emit_byj_stepper_state(stepper);
          return;
        }
        if (strcmp(stepper->key, "byj2") == 0)
        {
          byj2_motion_set_enabled(0U);
          printf("ok byj %s enable off\r\n", stepper->key);
          emit_byj_stepper_state(stepper);
          return;
        }
        stepper->enabled = 0U;
        stepper->moving = 0U;
        stepper->velocity = 0;
          stepper->target = stepper->position;
          stepper->next_step_tick = 0U;
          apply_byj_stepper_enable(stepper);
          printf("ok byj %s enable off\r\n", stepper->key);
          emit_byj_stepper_state(stepper);
          return;
        }
        uart_write_line("err invalid enable state");
        return;
      }

      if (strcmp(tokens[2], "stop") == 0)
      {
        if (strcmp(stepper->key, "byj1") == 0)
        {
          byj1_motion_stop();
          printf("ok byj %s stop\r\n", stepper->key);
          emit_byj_stepper_state(stepper);
          return;
        }
        if (strcmp(stepper->key, "byj2") == 0)
        {
          byj2_motion_stop();
          printf("ok byj %s stop\r\n", stepper->key);
          emit_byj_stepper_state(stepper);
          return;
        }
        stepper->target = stepper->position;
        stepper->velocity = 0;
        stepper->moving = 0U;
        stepper->next_step_tick = 0U;
        printf("ok byj %s stop\r\n", stepper->key);
        emit_byj_stepper_state(stepper);
        return;
      }

      if (strcmp(tokens[2], "home") == 0)
      {
        if (strcmp(stepper->key, "byj1") == 0)
        {
          byj1_motion_home();
          return;
        }
        if (strcmp(stepper->key, "byj2") == 0)
        {
          byj2_motion_home();
          return;
        }
        if (stepper->enabled == 0U)
        {
          uart_write_line("err byj disabled");
          return;
        }
        if (stepper->endstop_port == NULL || stepper->endstop_pin == 0U)
        {
          uart_write_line("err byj no endstop");
          return;
        }
        byj_begin_motion(stepper, -1, INT32_MIN);
        printf("ok byj %s home\r\n", stepper->key);
        emit_byj_stepper_state(stepper);
        return;
      }

      if (strcmp(tokens[2], "jog") == 0)
      {
        if (token_count < 4U)
        {
          uart_write_line("err missing jog direction");
          return;
        }
        if (strcmp(stepper->key, "byj1") == 0)
        {
          if (strcmp(tokens[3], "+") == 0)
          {
            byj1_motion_jog(1);
            return;
          }
          if (strcmp(tokens[3], "-") == 0)
          {
            byj1_motion_jog(-1);
            return;
          }
          uart_write_line("err invalid jog direction");
          return;
        }
        if (strcmp(stepper->key, "byj2") == 0)
        {
          if (strcmp(tokens[3], "+") == 0)
          {
            byj2_motion_jog(1);
            return;
          }
          if (strcmp(tokens[3], "-") == 0)
          {
            byj2_motion_jog(-1);
            return;
          }
          uart_write_line("err invalid jog direction");
          return;
        }
        if (stepper->enabled == 0U)
        {
          uart_write_line("err byj disabled");
          return;
        }
        if (strcmp(tokens[3], "+") == 0)
        {
          byj_begin_motion(stepper, 1, INT32_MAX);
          printf("ok byj %s jog +\r\n", stepper->key);
          emit_byj_stepper_state(stepper);
          return;
        }
        if (strcmp(tokens[3], "-") == 0)
        {
          byj_begin_motion(stepper, -1, INT32_MIN);
          printf("ok byj %s jog -\r\n", stepper->key);
          emit_byj_stepper_state(stepper);
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
        if (strcmp(stepper->key, "byj1") == 0)
        {
          byj1_motion_move_relative((int32_t)delta);
          return;
        }
        if (strcmp(stepper->key, "byj2") == 0)
        {
          byj2_motion_move_relative((int32_t)delta);
          return;
        }
        if (stepper->enabled == 0U)
        {
          uart_write_line("err byj disabled");
          return;
        }
        byj_begin_motion(stepper, (delta == 0) ? 0 : ((delta > 0) ? 1 : -1), stepper->position + (int32_t)delta);
        printf("ok byj %s move %ld\r\n", stepper->key, delta);
        emit_byj_stepper_state(stepper);
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
        if (strcmp(stepper->key, "byj1") == 0)
        {
          byj1_motion_goto((int32_t)target);
          return;
        }
        if (strcmp(stepper->key, "byj2") == 0)
        {
          byj2_motion_goto((int32_t)target);
          return;
        }
        if (stepper->enabled == 0U)
        {
          uart_write_line("err byj disabled");
          return;
        }
        byj_begin_motion(stepper,
                         ((int32_t)target == stepper->position) ? 0 : (((int32_t)target > stepper->position) ? 1 : -1),
                         (int32_t)target);
        printf("ok byj %s goto %ld\r\n", stepper->key, target);
        emit_byj_stepper_state(stepper);
        return;
      }
    }

    uart_write_line("err unsupported byj command");
    return;
  }

  if (strcmp(tokens[0], "magnet") == 0)
  {
    if (token_count >= 2U && strcmp(tokens[1], "status") == 0)
    {
      magnet_emit_state();
      return;
    }
    if (token_count >= 3U && strcmp(tokens[1], "pwm") == 0)
    {
      long pwm = strtol(tokens[2], NULL, 10);
      if (pwm < 0)
      {
        pwm = 0;
      }
      if (pwm > 100)
      {
        pwm = 100;
      }
      magnet_set_pwm((uint8_t)pwm);
      printf("ok magnet pwm %ld\r\n", pwm);
      magnet_emit_state();
      return;
    }
    uart_write_line("err invalid magnet command");
    return;
  }

  if (strcmp(tokens[0], "fanpwr") == 0)
  {
    if (token_count >= 2U && strcmp(tokens[1], "status") == 0)
    {
      fan_power_relay_emit_all_states();
      return;
    }
    if (token_count < 3U)
    {
      uart_write_line("err invalid fanpwr command");
      return;
    }
    {
      FanPowerRelayState *relay = fan_power_relay_find(tokens[1]);
      if (relay == NULL)
      {
        uart_write_line("err unknown fan relay");
        return;
      }
      if (strcmp(tokens[2], "on") == 0)
      {
        relay->on = 1U;
        fan_power_relay_apply(relay);
        printf("ok fanpwr %s on\r\n", relay->key);
        fan_power_relay_emit_all_states();
        return;
      }
      if (strcmp(tokens[2], "off") == 0)
      {
        relay->on = 0U;
        fan_power_relay_apply(relay);
        printf("ok fanpwr %s off\r\n", relay->key);
        fan_power_relay_emit_all_states();
        return;
      }
    }
    uart_write_line("err unsupported fanpwr command");
    return;
  }

  if (strcmp(tokens[0], "fan") == 0)
  {
    if (token_count >= 2U && strcmp(tokens[1], "status") == 0)
    {
      fan_emit_all_states();
      return;
    }

    if (token_count < 4U)
    {
      uart_write_line("err invalid fan command");
      return;
    }

    if (strcmp(tokens[2], "pwm") != 0)
    {
      uart_write_line("err invalid fan field");
      return;
    }

    if (strcmp(tokens[1], "all") == 0)
    {
      uint32_t pwm = strtoul(tokens[3], NULL, 10);
      if (pwm > 100U)
      {
        uart_write_line("err invalid fan pwm");
        return;
      }
      fan_set_all_pwm((uint8_t)pwm);
      printf("ok fan all pwm %lu\r\n", (unsigned long)pwm);
      fan_emit_all_states();
      return;
    }

    {
      IntelFanState *fan = fan_find(tokens[1]);
      uint32_t pwm = strtoul(tokens[3], NULL, 10);
      if (fan == NULL)
      {
        uart_write_line("err unknown fan");
        return;
      }
      if (pwm > 100U)
      {
        uart_write_line("err invalid fan pwm");
        return;
      }
      fan_set_pwm(fan, (uint8_t)pwm);
      printf("ok fan %s pwm %lu\r\n", fan->key, (unsigned long)pwm);
      fan_emit_all_states();
      return;
    }
  }

  if (strcmp(tokens[0], "servo") == 0)
  {
    if (token_count >= 2U && strcmp(tokens[1], "status") == 0)
    {
      servo_emit_all_states();
      return;
    }

    if (token_count < 4U)
    {
      uart_write_line("err invalid servo command");
      return;
    }

    if (strcmp(tokens[1], "all") == 0)
    {
      if (strcmp(tokens[2], "angle") == 0)
      {
        uint16_t angle = (uint16_t)strtoul(tokens[3], NULL, 10);
        servo_set_all_angle(angle);
        printf("ok servo all angle %u\r\n", (unsigned)angle);
        servo_emit_all_states();
        return;
      }
      if (strcmp(tokens[2], "us") == 0)
      {
        uint16_t pulse = (uint16_t)strtoul(tokens[3], NULL, 10);
        if (pulse < 500U || pulse > 2500U)
        {
          uart_write_line("err invalid servo pulse");
          return;
        }
        servo_set_all_pulse_us(pulse);
        printf("ok servo all us %u\r\n", (unsigned)pulse);
        servo_emit_all_states();
        return;
      }
      uart_write_line("err invalid servo field");
      return;
    }

    {
      ServoState *servo = servo_find(tokens[1]);
      if (servo == NULL)
      {
        uart_write_line("err unknown servo");
        return;
      }

      if (strcmp(tokens[2], "angle") == 0)
      {
        uint16_t angle = (uint16_t)strtoul(tokens[3], NULL, 10);
        servo_set_angle(servo, angle);
        printf("ok servo %s angle_req %u angle %u us %u\r\n",
               servo->key,
               (unsigned)angle,
               (unsigned)servo_us_to_angle(servo->pulse_us),
               (unsigned)servo->pulse_us);
        return;
      }

      if (strcmp(tokens[2], "us") == 0)
      {
        uint16_t pulse = (uint16_t)strtoul(tokens[3], NULL, 10);
        if (pulse < 500U || pulse > 2500U)
        {
          uart_write_line("err invalid servo pulse");
          return;
        }
        servo_set_pulse_us(servo, pulse);
        printf("ok servo %s us %u angle %u\r\n",
               servo->key,
               (unsigned)servo->pulse_us,
               (unsigned)servo_us_to_angle(servo->pulse_us));
        return;
      }
    }

    uart_write_line("err invalid servo field");
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
      tmc_emit_driver_probe(axes[0].driver_addr);
      tmc_emit_driver_probe(axes[1].driver_addr);
      tmc_uart_dump_raw(20U);
      return;
    }

    if (token_count >= 3U)
    {
      AxisState *axis = find_axis_by_driver_key(tokens[1]);
      if (axis == NULL)
      {
        uart_write_line("err unknown tmc driver");
        return;
      }

      if (strcmp(tokens[2], "status") == 0)
      {
        tmc_emit_driver_status(axis);
        return;
      }

      if (strcmp(tokens[2], "current") == 0 && token_count >= 4U)
      {
        tmc_set_driver_current(axis, (uint32_t)strtoul(tokens[3], NULL, 10));
        return;
      }

      if (strcmp(tokens[2], "microsteps") == 0 && token_count >= 4U)
      {
        tmc_set_driver_microsteps(axis, (uint16_t)strtoul(tokens[3], NULL, 10));
        return;
      }

      if (strcmp(tokens[2], "stealth") == 0 && token_count >= 4U)
      {
        if (strcmp(tokens[3], "on") == 0)
        {
          tmc_set_driver_stealth(axis, 1U);
          return;
        }
        if (strcmp(tokens[3], "off") == 0)
        {
          tmc_set_driver_stealth(axis, 0U);
          return;
        }
        uart_write_line("err invalid stealth state");
        return;
      }

      if (strcmp(tokens[2], "spread") == 0 && token_count >= 4U)
      {
        if (strcmp(tokens[3], "on") == 0)
        {
          tmc_set_driver_stealth(axis, 0U);
          return;
        }
        if (strcmp(tokens[3], "off") == 0)
        {
          tmc_set_driver_stealth(axis, 1U);
          return;
        }
        uart_write_line("err invalid spread state");
        return;
      }
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
  uint8_t axis_is_a = (strcmp(tokens[1], "a") == 0) ? 1U : 0U;
  uint8_t axis_is_b = 0U;
  if (axis == NULL)
  {
    uart_write_line("err unknown axis");
    return;
  }

  axis_is_b = (axis == b_axis_state()) ? 1U : 0U;

  if (axis_is_a || axis_is_b)
  {
    if (strcmp(tokens[2], "enable") == 0)
    {
      if (token_count < 4U)
      {
        uart_write_line("err missing enable state");
        return;
      }
      if (strcmp(tokens[3], "on") == 0)
      {
        axes[0].enabled = 1U;
        axes[1].enabled = 1U;
        apply_axis_enable_state();
        a_axis_motion_set_enabled(1U);
        b_axis_motion_set_enabled(1U);
        printf("ok axis %s enable on\r\n", axis->key);
        emit_axis_state(axis);
        return;
      }
      if (strcmp(tokens[3], "off") == 0)
      {
        axes[0].enabled = 0U;
        axes[1].enabled = 0U;
        a_axis_motion_set_enabled(0U);
        b_axis_motion_set_enabled(0U);
        apply_axis_enable_state();
        printf("ok axis %s enable off\r\n", axis->key);
        emit_axis_state(axis);
        return;
      }
    }

    if (strcmp(tokens[2], "home") == 0)
    {
      if (axes[0].enabled == 0U || axes[1].enabled == 0U)
      {
        axes[0].enabled = 1U;
        axes[1].enabled = 1U;
        apply_axis_enable_state();
        a_axis_motion_set_enabled(1U);
        b_axis_motion_set_enabled(1U);
      }
      printf("ok axis %s home\r\n", axis->key);
      if (axis_is_a)
      {
        a_axis_motion_home();
      }
      else
      {
        b_axis_motion_home();
      }
      emit_axis_state(axis);
      return;
    }

    if (strcmp(tokens[2], "scan") == 0)
    {
      if (axes[0].enabled == 0U || axes[1].enabled == 0U)
      {
        axes[0].enabled = 1U;
        axes[1].enabled = 1U;
        apply_axis_enable_state();
        a_axis_motion_set_enabled(1U);
        b_axis_motion_set_enabled(1U);
      }
      printf("ok axis %s scan\r\n", axis->key);
      if (axis_is_a)
      {
        a_axis_motion_scan();
      }
      else
      {
        b_axis_motion_scan();
      }
      emit_axis_state(axis);
      return;
    }

    if (strcmp(tokens[2], "stop") == 0)
    {
      if (axis_is_a)
      {
        a_axis_motion_stop();
      }
      else
      {
        b_axis_motion_stop();
      }
      printf("ok axis %s stop\r\n", axis->key);
      emit_axis_state(axis);
      return;
    }

    if (strcmp(tokens[2], "travel") == 0)
    {
      uint32_t travel_steps;
      if (token_count < 4U)
      {
        uart_write_line("err missing travel steps");
        return;
      }
      travel_steps = (uint32_t)strtoul(tokens[3], NULL, 10);
      if (travel_steps < 100U)
      {
        uart_write_line("err invalid travel steps");
        return;
      }
      axis->tuned_travel_steps = travel_steps;
      axis->scan_travel_steps = (int32_t)travel_steps;
      if (axis_is_a)
      {
        a_axis_motion_set_travel(travel_steps);
      }
      else
      {
        b_axis_motion_set_travel(travel_steps);
      }
      printf("ok axis %s travel %lu\r\n", axis->key, (unsigned long)travel_steps);
      emit_axis_state(axis);
      return;
    }

    if (strcmp(tokens[2], "decel_window") == 0)
    {
      uint32_t decel_window_steps;
      if (token_count < 4U)
      {
        uart_write_line("err missing decel window");
        return;
      }
      decel_window_steps = (uint32_t)strtoul(tokens[3], NULL, 10);
      if (decel_window_steps < 10U)
      {
        uart_write_line("err invalid decel window");
        return;
      }
      axis->decel_window_steps = decel_window_steps;
      if (axis_is_a)
      {
        a_axis_motion_set_decel_window(decel_window_steps);
      }
      else
      {
        b_axis_motion_set_decel_window(decel_window_steps);
      }
      printf("ok axis %s decel_window %lu\r\n", axis->key, (unsigned long)decel_window_steps);
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
      if (axes[0].enabled == 0U || axes[1].enabled == 0U)
      {
        axes[0].enabled = 1U;
        axes[1].enabled = 1U;
        apply_axis_enable_state();
        a_axis_motion_set_enabled(1U);
        b_axis_motion_set_enabled(1U);
      }
      if (axis_is_a)
      {
        a_axis_motion_set_cruise_interval(axis->cruise_interval_us);
        a_axis_motion_jog((strcmp(tokens[3], "-") == 0) ? -1 : 1);
      }
      else
      {
        b_axis_motion_set_cruise_interval(axis->cruise_interval_us);
        b_axis_motion_jog((strcmp(tokens[3], "-") == 0) ? -1 : 1);
      }
      printf("ok axis %s jog %s\r\n", axis->key, strcmp(tokens[3], "-") == 0 ? "-" : "+");
      emit_axis_state(axis);
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
      if (axis->homed == 0U)
      {
        uart_write_line("err axis not_homed");
        return;
      }
      if (axes[0].enabled == 0U || axes[1].enabled == 0U)
      {
        axes[0].enabled = 1U;
        axes[1].enabled = 1U;
        apply_axis_enable_state();
        a_axis_motion_set_enabled(1U);
        b_axis_motion_set_enabled(1U);
      }
      delta = strtol(tokens[3], NULL, 10);
      if (axis_is_a)
      {
        a_axis_motion_set_cruise_interval(axis->cruise_interval_us);
        a_axis_motion_move_relative((int32_t)delta);
      }
      else
      {
        b_axis_motion_set_cruise_interval(axis->cruise_interval_us);
        b_axis_motion_move_relative((int32_t)delta);
      }
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
      if (axis->homed == 0U)
      {
        uart_write_line("err axis not_homed");
        return;
      }
      if (axes[0].enabled == 0U || axes[1].enabled == 0U)
      {
        axes[0].enabled = 1U;
        axes[1].enabled = 1U;
        apply_axis_enable_state();
        a_axis_motion_set_enabled(1U);
        b_axis_motion_set_enabled(1U);
      }
      target = strtol(tokens[3], NULL, 10);
      if (axis_is_a)
      {
        a_axis_motion_set_cruise_interval(axis->cruise_interval_us);
        a_axis_motion_goto((int32_t)target);
      }
      else
      {
        b_axis_motion_set_cruise_interval(axis->cruise_interval_us);
        b_axis_motion_goto((int32_t)target);
      }
      printf("ok axis %s goto %ld\r\n", axis->key, target);
      emit_axis_state(axis);
      return;
    }
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
      if (axis->key[0] == 'b' && axis->key[1] == '\0')
      {
        b_axis_motion_set_enabled(1U);
      }
      printf("ok axis %s enable on\r\n", axis->key);
      emit_axis_state(axis);
      return;
    }
    if (strcmp(tokens[3], "off") == 0)
    {
      axis->enabled = 0U;
      axis_stop_motion(axis);
      if (axis->key[0] == 'b' && axis->key[1] == '\0')
      {
        b_axis_motion_set_enabled(0U);
      }
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
    if (axis->key[0] == 'b' && axis->key[1] == '\0')
    {
      if (axis->enabled == 0U)
      {
        axis->enabled = 1U;
        apply_axis_enable_state();
        b_axis_motion_set_enabled(1U);
      }
      printf("ok axis %s home\r\n", axis->key);
      b_axis_motion_home();
      emit_axis_state(axis);
      return;
    }
    if (axis->enabled == 0U)
    {
      uart_write_line("err axis disabled");
      return;
    }
    axis->homed = 0U;
    axis_begin_motion(axis, -1, INT32_MIN, 1U);
    printf("ok axis %s home\r\n", axis->key);
    emit_axis_state(axis);
    return;
  }

  if (strcmp(tokens[2], "scan") == 0)
  {
    if (axis->key[0] == 'b' && axis->key[1] == '\0')
    {
      if (axis->enabled == 0U)
      {
        axis->enabled = 1U;
        apply_axis_enable_state();
        b_axis_motion_set_enabled(1U);
      }
      printf("ok axis %s scan\r\n", axis->key);
      b_axis_motion_scan();
      emit_axis_state(axis);
      return;
    }
    if (axis->enabled == 0U)
    {
      uart_write_line("err axis disabled");
      return;
    }
    axis->homed = 0U;
    axis->scan_travel_steps = 0;
    axis_begin_motion(axis, -1, INT32_MIN, 3U);
    printf("ok axis %s scan\r\n", axis->key);
    emit_axis_state(axis);
    return;
  }

  if (strcmp(tokens[2], "stop") == 0)
  {
    if (axis->key[0] == 'b' && axis->key[1] == '\0')
    {
      b_axis_motion_stop();
      printf("ok axis %s stop\r\n", axis->key);
      emit_axis_state(axis);
      return;
    }
    axis_stop_motion(axis);
    printf("ok axis %s stop\r\n", axis->key);
    emit_axis_state(axis);
    return;
  }

  if (strcmp(tokens[2], "travel") == 0)
  {
    uint32_t travel_steps;
    if (token_count < 4U)
    {
      uart_write_line("err missing travel steps");
      return;
    }
    travel_steps = (uint32_t)strtoul(tokens[3], NULL, 10);
    if (travel_steps < 100U)
    {
      uart_write_line("err invalid travel steps");
      return;
    }
    axis->tuned_travel_steps = travel_steps;
    axis->scan_travel_steps = (int32_t)travel_steps;
    if (axis->key[0] == 'b' && axis->key[1] == '\0')
    {
      b_axis_motion_set_travel(travel_steps);
    }
    printf("ok axis %s travel %lu\r\n", axis->key, (unsigned long)travel_steps);
    emit_axis_state(axis);
    return;
  }

  if (strcmp(tokens[2], "decel_window") == 0)
  {
    uint32_t decel_window_steps;
    if (token_count < 4U)
    {
      uart_write_line("err missing decel window");
      return;
    }
    decel_window_steps = (uint32_t)strtoul(tokens[3], NULL, 10);
    if (decel_window_steps < 10U)
    {
      uart_write_line("err invalid decel window");
      return;
    }
    axis->decel_window_steps = decel_window_steps;
    if (axis->key[0] == 'b' && axis->key[1] == '\0')
    {
      b_axis_motion_set_decel_window(decel_window_steps);
    }
    printf("ok axis %s decel_window %lu\r\n", axis->key, (unsigned long)decel_window_steps);
    emit_axis_state(axis);
    return;
  }

  if (axis->homing_state != 0U)
  {
    uart_write_line("err axis homing");
    return;
  }

  if (strcmp(tokens[2], "jog") == 0)
  {
    if (token_count < 4U)
    {
      uart_write_line("err missing jog direction");
      return;
    }
    if (axis->key[0] == 'b' && axis->key[1] == '\0')
    {
      if (axis->enabled == 0U)
      {
        axis->enabled = 1U;
        apply_axis_enable_state();
        b_axis_motion_set_enabled(1U);
      }
      b_axis_motion_set_cruise_interval(axis->cruise_interval_us);
      if (strcmp(tokens[3], "+") == 0)
      {
        b_axis_motion_jog(1);
        printf("ok axis %s jog +\r\n", axis->key);
        emit_axis_state(axis);
        return;
      }
      if (strcmp(tokens[3], "-") == 0)
      {
        b_axis_motion_jog(-1);
        printf("ok axis %s jog -\r\n", axis->key);
        emit_axis_state(axis);
        return;
      }
      uart_write_line("err invalid jog direction");
      return;
    }
    if (axis->enabled == 0U)
    {
      uart_write_line("err axis disabled");
      return;
    }
    if (strcmp(tokens[3], "+") == 0)
    {
      axis_begin_motion(axis, 1, INT32_MAX, 0U);
      printf("ok axis %s jog +\r\n", axis->key);
      emit_axis_state(axis);
      return;
    }
    if (strcmp(tokens[3], "-") == 0)
    {
      axis_begin_motion(axis, -1, INT32_MIN, 0U);
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
    if (axis->key[0] == 'b' && axis->key[1] == '\0')
    {
      if (axis->enabled == 0U)
      {
        axis->enabled = 1U;
        apply_axis_enable_state();
        b_axis_motion_set_enabled(1U);
      }
      b_axis_motion_set_cruise_interval(axis->cruise_interval_us);
      b_axis_motion_move_relative((int32_t)delta);
      printf("ok axis %s move %ld\r\n", axis->key, delta);
      emit_axis_state(axis);
      return;
    }
    if (axis->enabled == 0U)
    {
      uart_write_line("err axis disabled");
      return;
    }
    axis_begin_motion(axis, (delta == 0) ? 0 : ((delta > 0) ? 1 : -1), axis->position + (int32_t)delta, 0U);
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
    if (axis->key[0] == 'b' && axis->key[1] == '\0')
    {
      if (axis->enabled == 0U)
      {
        axis->enabled = 1U;
        apply_axis_enable_state();
        b_axis_motion_set_enabled(1U);
      }
      b_axis_motion_set_cruise_interval(axis->cruise_interval_us);
      b_axis_motion_goto((int32_t)target);
      printf("ok axis %s goto %ld\r\n", axis->key, target);
      emit_axis_state(axis);
      return;
    }
    if (axis->enabled == 0U)
    {
      uart_write_line("err axis disabled");
      return;
    }
    axis_begin_motion(axis,
                      ((int32_t)target == axis->position) ? 0 : (((int32_t)target > axis->position) ? 1 : -1),
                      (int32_t)target,
                      0U);
    printf("ok axis %s goto %ld\r\n", axis->key, target);
    emit_axis_state(axis);
    return;
  }

  uart_write_line("err unsupported command");
}

static void tick_axes(void)
{
  emit_dirty_axis_states();
}

static uint8_t coord_motion_active(void)
{
  return coord_motion.active;
}

static void coord_motion_stop(void)
{
  HAL_TIM_Base_Stop_IT(&htim6);
  coord_motion.active = 0U;
  coord_motion.pulse_high_phase = 0U;
  coord_motion.a_step_pending = 0U;
  coord_motion.b_step_pending = 0U;
  HAL_GPIO_WritePin(A_STEP_GPIO_Port, A_STEP_Pin, GPIO_PIN_RESET);
  HAL_GPIO_WritePin(B_STEP_GPIO_Port, B_STEP_Pin, GPIO_PIN_RESET);
}

static void coord_motion_emit_state(void)
{
  printf("ab active %s pos_a %ld target_a %ld pos_b %ld target_b %ld steps_done %lu steps_total %lu interval_us %lu cruise_us %lu start_us %lu\r\n",
         coord_motion.active ? "on" : "off",
         (long)coord_motion.a_position,
         (long)coord_motion.a_target,
         (long)coord_motion.b_position,
         (long)coord_motion.b_target,
         (unsigned long)coord_motion.steps_done,
         (unsigned long)coord_motion.steps_total,
         (unsigned long)coord_motion.current_interval_us,
         (unsigned long)coord_motion.cruise_interval_us,
         (unsigned long)coord_motion.start_interval_us);
}

static uint32_t coord_motion_ease_interval(uint32_t phase_steps,
                                           uint32_t ramp_steps,
                                           uint32_t start_interval_us,
                                           uint32_t cruise_interval_us)
{
  uint32_t progress_q16;
  uint64_t t;
  uint64_t t2_q16;
  uint64_t t3_q16;
  uint64_t t4_q16;
  uint64_t t5_q16;
  int64_t ease_q16;
  uint32_t span_us;

  if (ramp_steps == 0U || start_interval_us <= cruise_interval_us)
  {
    return cruise_interval_us;
  }

  if (phase_steps >= ramp_steps)
  {
    return cruise_interval_us;
  }

  progress_q16 = (uint32_t)(((uint64_t)phase_steps << 16) / (uint64_t)ramp_steps);
  t = progress_q16;
  t2_q16 = (t * t) >> 16;
  t3_q16 = (t2_q16 * t) >> 16;
  t4_q16 = (t3_q16 * t) >> 16;
  t5_q16 = (t4_q16 * t) >> 16;

  /* Quintic smootherstep: 6t^5 - 15t^4 + 10t^3 */
  ease_q16 = (int64_t)(10ULL * t3_q16)
           - (int64_t)(15ULL * t4_q16)
           + (int64_t)(6ULL * t5_q16);

  if (ease_q16 < 0)
  {
    ease_q16 = 0;
  }
  else if (ease_q16 > 65536)
  {
    ease_q16 = 65536;
  }

  span_us = start_interval_us - cruise_interval_us;
  return start_interval_us -
         (uint32_t)(((uint64_t)span_us * (uint64_t)ease_q16) >> 16);
}

static uint8_t coord_motion_start(int32_t a_target, int32_t b_target)
{
  AAxisMotionSnapshot a_snapshot;
  BAxisMotionSnapshot b_snapshot;
  uint32_t a_abs_steps;
  uint32_t b_abs_steps;

  a_axis_motion_get_snapshot(&a_snapshot);
  b_axis_motion_get_snapshot(&b_snapshot);

  if (a_snapshot.enabled == 0U || b_snapshot.enabled == 0U)
  {
    uart_write_line("err ab disabled");
    return 0U;
  }

  if (a_snapshot.homed == 0U || b_snapshot.homed == 0U)
  {
    uart_write_line("err ab not_homed");
    return 0U;
  }

  coord_motion.a_position = a_snapshot.position;
  coord_motion.b_position = b_snapshot.position;
  coord_motion.a_target = a_target;
  coord_motion.b_target = b_target;
  coord_motion.a_direction = (a_target > a_snapshot.position) ? 1 : ((a_target < a_snapshot.position) ? -1 : 0);
  coord_motion.b_direction = (b_target > b_snapshot.position) ? 1 : ((b_target < b_snapshot.position) ? -1 : 0);

  a_abs_steps = (uint32_t)((a_target >= a_snapshot.position) ? (a_target - a_snapshot.position) : (a_snapshot.position - a_target));
  b_abs_steps = (uint32_t)((b_target >= b_snapshot.position) ? (b_target - b_snapshot.position) : (b_snapshot.position - b_target));

  coord_motion.a_abs_steps = a_abs_steps;
  coord_motion.b_abs_steps = b_abs_steps;
  coord_motion.steps_total = (a_abs_steps > b_abs_steps) ? a_abs_steps : b_abs_steps;
  coord_motion.steps_done = 0U;
  coord_motion.a_err = 0U;
  coord_motion.b_err = 0U;
  coord_motion.a_step_pending = 0U;
  coord_motion.b_step_pending = 0U;
  coord_motion.pulse_high_phase = 0U;

  if (coord_motion.steps_total == 0U)
  {
    coord_motion.active = 0U;
    return 1U;
  }

  coord_motion.cruise_interval_us = AB_COORD_CRUISE_INTERVAL_US;
  coord_motion.start_interval_us = AB_COORD_START_INTERVAL_US;
  coord_motion.accel_delta_us = AB_COORD_ACCEL_DELTA_US;

  coord_motion.current_interval_us = coord_motion.start_interval_us;
  coord_motion.active = 1U;

  HAL_TIM_Base_Stop_IT(&htim6);
  HAL_GPIO_WritePin(A_DIR_GPIO_Port, A_DIR_Pin, (coord_motion.a_direction >= 0) ? GPIO_PIN_RESET : GPIO_PIN_SET);
  HAL_GPIO_WritePin(B_DIR_GPIO_Port, B_DIR_Pin, (coord_motion.b_direction >= 0) ? GPIO_PIN_SET : GPIO_PIN_RESET);
  __HAL_TIM_SET_AUTORELOAD(&htim6, (coord_motion.current_interval_us > 5U) ? (coord_motion.current_interval_us - 5U) : 10U);
  __HAL_TIM_SET_COUNTER(&htim6, 0U);
  HAL_TIM_Base_Start_IT(&htim6);
  return 1U;
}

static void coord_motion_irq(void)
{
  if (coord_motion.active == 0U)
  {
    coord_motion_stop();
    return;
  }

  if (coord_motion.pulse_high_phase == 0U)
  {
    coord_motion.a_step_pending = 0U;
    coord_motion.b_step_pending = 0U;

    if (coord_motion.steps_done >= coord_motion.steps_total)
    {
      a_axis_motion_set_position(coord_motion.a_position, 1U);
      b_axis_motion_set_position(coord_motion.b_position, 1U);
      coord_motion_stop();
      coord_motion_emit_state();
      emit_axis_state(&axes[0]);
      emit_axis_state(&axes[1]);
      return;
    }

    coord_motion.a_err += coord_motion.a_abs_steps;
    if (coord_motion.a_err >= coord_motion.steps_total && coord_motion.a_direction != 0)
    {
      coord_motion.a_err -= coord_motion.steps_total;
      coord_motion.a_step_pending = 1U;
      HAL_GPIO_WritePin(A_STEP_GPIO_Port, A_STEP_Pin, GPIO_PIN_SET);
    }

    coord_motion.b_err += coord_motion.b_abs_steps;
    if (coord_motion.b_err >= coord_motion.steps_total && coord_motion.b_direction != 0)
    {
      coord_motion.b_err -= coord_motion.steps_total;
      coord_motion.b_step_pending = 1U;
      HAL_GPIO_WritePin(B_STEP_GPIO_Port, B_STEP_Pin, GPIO_PIN_SET);
    }

    coord_motion.pulse_high_phase = 1U;
    __HAL_TIM_SET_AUTORELOAD(&htim6, 5U - 1U);
    __HAL_TIM_SET_COUNTER(&htim6, 0U);
    return;
  }

  HAL_GPIO_WritePin(A_STEP_GPIO_Port, A_STEP_Pin, GPIO_PIN_RESET);
  HAL_GPIO_WritePin(B_STEP_GPIO_Port, B_STEP_Pin, GPIO_PIN_RESET);

  if (coord_motion.a_step_pending != 0U)
  {
    coord_motion.a_position += coord_motion.a_direction;
  }
  if (coord_motion.b_step_pending != 0U)
  {
    coord_motion.b_position += coord_motion.b_direction;
  }

  coord_motion.steps_done++;
  {
    uint32_t remaining_steps = (coord_motion.steps_done >= coord_motion.steps_total)
                                 ? 0U
                                 : (coord_motion.steps_total - coord_motion.steps_done);
    uint32_t ramp_steps = coord_motion.steps_total / 11U;
    uint32_t ramp_span = (coord_motion.start_interval_us > coord_motion.cruise_interval_us)
                           ? (coord_motion.start_interval_us - coord_motion.cruise_interval_us)
                           : 0U;

    if (ramp_steps < AB_COORD_MIN_RAMP_STEPS)
    {
      ramp_steps = AB_COORD_MIN_RAMP_STEPS;
    }
    if (coord_motion.steps_total > 1U && (ramp_steps * 2U) > coord_motion.steps_total)
    {
      ramp_steps = coord_motion.steps_total / 2U;
    }

    if (ramp_steps == 0U || ramp_span == 0U)
    {
      coord_motion.current_interval_us = coord_motion.cruise_interval_us;
    }
    else if (coord_motion.steps_done < ramp_steps)
    {
      coord_motion.current_interval_us =
        coord_motion_ease_interval(coord_motion.steps_done,
                                   ramp_steps,
                                   coord_motion.start_interval_us,
                                   coord_motion.cruise_interval_us);
    }
    else if (remaining_steps < ramp_steps)
    {
      coord_motion.current_interval_us =
        coord_motion_ease_interval(remaining_steps,
                                   ramp_steps,
                                   coord_motion.start_interval_us,
                                   coord_motion.cruise_interval_us);
    }
    else
    {
      coord_motion.current_interval_us = coord_motion.cruise_interval_us;
    }
  }
  coord_motion.pulse_high_phase = 0U;
  __HAL_TIM_SET_AUTORELOAD(&htim6, (coord_motion.current_interval_us > 5U) ? (coord_motion.current_interval_us - 5U) : 10U);
  __HAL_TIM_SET_COUNTER(&htim6, 0U);
}

void HAL_TIM_PeriodElapsedCallback(TIM_HandleTypeDef *htim)
{
  if (htim->Instance == TIM6)
  {
    if (coord_motion_active() != 0U)
    {
      coord_motion_irq();
      return;
    }
    if (a_axis_motion_active() != 0U)
    {
      a_axis_motion_irq();
      return;
    }
    if (b_axis_motion_active() != 0U)
    {
      b_axis_motion_irq();
      return;
    }
  }
}
/* USER CODE END 0 */

int main(void)
{
  static const uint8_t boot_u1[] = "\r\nBOOT U1\r\n";
  static const uint8_t boot_u3[] = "BOOT U3\r\n";
  static const uint8_t boot_t3[] = "BOOT T3\r\n";
  static const uint8_t boot_t4[] = "BOOT T4\r\n";
  HAL_Init();
  SystemClock_Config();
  MX_GPIO_Init();
  MX_USART1_UART_Init();
  HAL_UART_Transmit(&huart1, (uint8_t *)boot_u1, sizeof(boot_u1) - 1U, 0xFFFFU);
  MX_USART2_UART_Init();
  HAL_UART_Transmit(&huart1, (uint8_t *)boot_u3, sizeof(boot_u3) - 1U, 0xFFFFU);
  MX_TIM1_Init();
  MX_TIM2_Init();
  MX_TIM3_Init();
  HAL_UART_Transmit(&huart1, (uint8_t *)boot_t3, sizeof(boot_t3) - 1U, 0xFFFFU);
  MX_TIM4_Init();
  HAL_UART_Transmit(&huart1, (uint8_t *)boot_t4, sizeof(boot_t4) - 1U, 0xFFFFU);
  MX_TIM6_Init();
  MX_TIM8_Init();
  MX_TIM16_Init();
  MX_TIM17_Init();

  printf("\r\nSTM32G431RB #02 boot host_uart=USART1 tmc_uart=USART3 driver_a=%u driver_b=%u\r\n",
         (unsigned)axes[0].driver_addr,
         (unsigned)axes[1].driver_addr);
  CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
  DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
  DWT->CYCCNT = 0U;
  servo_init();
  a_axis_motion_init();
  b_axis_motion_init();
  byj1_motion_init();
  byj2_motion_init();
  {
    AAxisMotionSnapshot a_snapshot = {0};
    BAxisMotionSnapshot b_snapshot = {0};
    uint32_t persisted_a_travel = 0U;
    uint32_t persisted_b_travel = 0U;
    a_axis_motion_get_snapshot(&a_snapshot);
    b_axis_motion_get_snapshot(&b_snapshot);
    axis_travel_store_init(a_snapshot.travel_steps, b_snapshot.travel_steps);
    if (axis_travel_store_get(&persisted_a_travel, &persisted_b_travel) != 0U)
    {
      a_axis_motion_set_travel(persisted_a_travel);
      b_axis_motion_set_travel(persisted_b_travel);
      axes[0].scan_travel_steps = (int32_t)persisted_a_travel;
      axes[0].tuned_travel_steps = persisted_a_travel;
      axes[1].scan_travel_steps = (int32_t)persisted_b_travel;
      axes[1].tuned_travel_steps = persisted_b_travel;
      printf("travel load a %lu b %lu\r\n",
             (unsigned long)persisted_a_travel,
             (unsigned long)persisted_b_travel);
    }
  }
  a_axis_motion_set_enabled(axes[0].enabled);
  a_axis_motion_set_travel(axes[0].tuned_travel_steps);
  a_axis_motion_set_decel_window(axes[0].decel_window_steps);
  a_axis_motion_set_start_interval(axes[0].start_interval_us);
  a_axis_motion_set_cruise_interval(axes[0].cruise_interval_us);
  a_axis_motion_set_homing_interval(axes[0].homing_interval_us);
  a_axis_motion_set_accel_delta(axes[0].accel_interval_delta_us);
  b_axis_motion_set_enabled(axes[1].enabled);
  b_axis_motion_set_travel(axes[1].tuned_travel_steps);
  b_axis_motion_set_decel_window(axes[1].decel_window_steps);
  b_axis_motion_set_start_interval(axes[1].start_interval_us);
  b_axis_motion_set_cruise_interval(axes[1].cruise_interval_us);
  b_axis_motion_set_homing_interval(axes[1].homing_interval_us);
  b_axis_motion_set_accel_delta(axes[1].accel_interval_delta_us);
  host_uart_rx_start();
  tmc_uart_rx_start();

  while (1)
  {
    static char rx_line[96];
    static size_t rx_len = 0U;
    static uint32_t last_led_toggle_ms = 0U;
    const uint32_t now_ms = HAL_GetTick();

    host_uart_clear_errors();

    while (host_rx_tail != host_rx_head)
    {
      uint8_t rx_byte = host_rx_ring[host_rx_tail];
      host_rx_tail = (uint16_t)((host_rx_tail + 1U) % (sizeof(host_rx_ring) / sizeof(host_rx_ring[0])));
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
    a_axis_motion_update();
    b_axis_motion_update();
    tick_byj_steppers();
    servo_tick();

    if (now_ms - last_led_toggle_ms >= 250U)
    {
      HAL_GPIO_TogglePin(LED2_GPIO_Port, LED2_Pin);
      last_led_toggle_ms = now_ms;
    }
  }
}

void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart)
{
  if (huart->Instance == USART1)
  {
    uint16_t next_head = (uint16_t)((host_rx_head + 1U) % (sizeof(host_rx_ring) / sizeof(host_rx_ring[0])));
    if (next_head != host_rx_tail)
    {
      host_rx_ring[host_rx_head] = host_rx_byte;
      host_rx_head = next_head;
    }
    host_uart_rx_start();
    return;
  }
  if (huart->Instance == USART3)
  {
    uint16_t next_head = (uint16_t)((tmc_rx_head + 1U) % (sizeof(tmc_rx_ring) / sizeof(tmc_rx_ring[0])));
    if (next_head != tmc_rx_tail)
    {
      tmc_rx_ring[tmc_rx_head] = tmc_rx_byte;
      tmc_rx_head = next_head;
    }
    tmc_uart_rx_start();
  }
}

void HAL_UART_ErrorCallback(UART_HandleTypeDef *huart)
{
  if (huart->Instance == USART1)
  {
    __HAL_UART_CLEAR_OREFLAG(huart);
    __HAL_UART_CLEAR_NEFLAG(huart);
    __HAL_UART_CLEAR_FEFLAG(huart);
    __HAL_UART_CLEAR_PEFLAG(huart);
    host_uart_rx_start();
    return;
  }
  if (huart->Instance == USART3)
  {
    __HAL_UART_CLEAR_OREFLAG(huart);
    __HAL_UART_CLEAR_NEFLAG(huart);
    __HAL_UART_CLEAR_FEFLAG(huart);
    __HAL_UART_CLEAR_PEFLAG(huart);
    tmc_uart_rx_start();
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

static void servo_timer_init_pwm(TIM_HandleTypeDef *htim, TIM_TypeDef *instance, uint32_t timer_clock_hz)
{
  TIM_OC_InitTypeDef sConfigOC = {0};
  uint32_t prescaler = timer_clock_hz / 1000000U;

  if (prescaler == 0U)
  {
    prescaler = 1U;
  }

  htim->Instance = instance;
  htim->Init.Prescaler = prescaler - 1U;
  htim->Init.CounterMode = TIM_COUNTERMODE_UP;
  htim->Init.Period = 20000U - 1U;
  htim->Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
  htim->Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_DISABLE;

  if (HAL_TIM_PWM_Init(htim) != HAL_OK)
  {
    Error_Handler();
  }

  sConfigOC.OCMode = TIM_OCMODE_PWM1;
  sConfigOC.Pulse = 1500U;
  sConfigOC.OCPolarity = TIM_OCPOLARITY_HIGH;
  sConfigOC.OCFastMode = TIM_OCFAST_DISABLE;
  sConfigOC.OCNPolarity = TIM_OCNPOLARITY_HIGH;
  sConfigOC.OCNIdleState = TIM_OCNIDLESTATE_RESET;
  sConfigOC.OCIdleState = TIM_OCIDLESTATE_RESET;

  if (instance == TIM2)
  {
    if (HAL_TIM_PWM_ConfigChannel(htim, &sConfigOC, TIM_CHANNEL_2) != HAL_OK ||
        HAL_TIM_PWM_ConfigChannel(htim, &sConfigOC, TIM_CHANNEL_3) != HAL_OK)
    {
      Error_Handler();
    }
    if (HAL_TIM_PWM_Start(htim, TIM_CHANNEL_2) != HAL_OK ||
        HAL_TIM_PWM_Start(htim, TIM_CHANNEL_3) != HAL_OK)
    {
      Error_Handler();
    }
    return;
  }

  if (HAL_TIM_PWM_ConfigChannel(htim, &sConfigOC, TIM_CHANNEL_1) != HAL_OK)
  {
    Error_Handler();
  }
  if (HAL_TIM_PWM_Start(htim, TIM_CHANNEL_1) != HAL_OK)
  {
    Error_Handler();
  }
}

static void MX_TIM2_Init(void)
{
  __HAL_RCC_TIM2_CLK_ENABLE();
  servo_timer_init_pwm(&htim2, TIM2, HAL_RCC_GetPCLK1Freq());
}

static void MX_TIM1_Init(void)
{
  TIM_OC_InitTypeDef sConfigOC = {0};
  uint32_t tim_clk_hz = HAL_RCC_GetPCLK2Freq();
  uint32_t prescaler = tim_clk_hz / 1000000U;

  if (prescaler == 0U)
  {
    prescaler = 1U;
  }

  __HAL_RCC_TIM1_CLK_ENABLE();

  htim1.Instance = TIM1;
  htim1.Init.Prescaler = prescaler - 1U;
  htim1.Init.CounterMode = TIM_COUNTERMODE_UP;
  htim1.Init.Period = 20000U - 1U;
  htim1.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
  htim1.Init.RepetitionCounter = 0U;
  htim1.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_DISABLE;
  if (HAL_TIM_PWM_Init(&htim1) != HAL_OK)
  {
    Error_Handler();
  }

  sConfigOC.OCMode = TIM_OCMODE_PWM1;
  sConfigOC.Pulse = 1500U;
  sConfigOC.OCPolarity = TIM_OCPOLARITY_HIGH;
  sConfigOC.OCFastMode = TIM_OCFAST_DISABLE;
  sConfigOC.OCNPolarity = TIM_OCNPOLARITY_HIGH;
  sConfigOC.OCNIdleState = TIM_OCNIDLESTATE_RESET;
  sConfigOC.OCIdleState = TIM_OCIDLESTATE_RESET;

  if (HAL_TIM_PWM_ConfigChannel(&htim1, &sConfigOC, TIM_CHANNEL_3) != HAL_OK)
  {
    Error_Handler();
  }

  if (HAL_TIM_PWM_Start(&htim1, TIM_CHANNEL_3) != HAL_OK)
  {
    Error_Handler();
  }
}

static void MX_TIM3_Init(void)
{
  TIM_OC_InitTypeDef sConfigOC = {0};
  uint32_t tim_clk_hz = tim3_input_clock_hz();
  uint32_t period = tim_clk_hz / 25000U;

  if (period == 0U)
  {
    period = 1U;
  }

  __HAL_RCC_TIM3_CLK_ENABLE();

  htim3.Instance = TIM3;
  htim3.Init.Prescaler = 0U;
  htim3.Init.CounterMode = TIM_COUNTERMODE_UP;
  htim3.Init.Period = period - 1U;
  htim3.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
  htim3.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_DISABLE;
  if (HAL_TIM_PWM_Init(&htim3) != HAL_OK)
  {
    Error_Handler();
  }

  sConfigOC.OCMode = TIM_OCMODE_PWM1;
  sConfigOC.Pulse = 0U;
  sConfigOC.OCPolarity = TIM_OCPOLARITY_HIGH;
  sConfigOC.OCFastMode = TIM_OCFAST_DISABLE;
  sConfigOC.OCNPolarity = TIM_OCNPOLARITY_HIGH;
  sConfigOC.OCNIdleState = TIM_OCNIDLESTATE_RESET;
  sConfigOC.OCIdleState = TIM_OCIDLESTATE_RESET;

  if (HAL_TIM_PWM_ConfigChannel(&htim3, &sConfigOC, TIM_CHANNEL_1) != HAL_OK ||
      HAL_TIM_PWM_ConfigChannel(&htim3, &sConfigOC, TIM_CHANNEL_2) != HAL_OK ||
      HAL_TIM_PWM_ConfigChannel(&htim3, &sConfigOC, TIM_CHANNEL_3) != HAL_OK)
  {
    Error_Handler();
  }

  if (HAL_TIM_PWM_Start(&htim3, TIM_CHANNEL_1) != HAL_OK ||
      HAL_TIM_PWM_Start(&htim3, TIM_CHANNEL_2) != HAL_OK ||
      HAL_TIM_PWM_Start(&htim3, TIM_CHANNEL_3) != HAL_OK)
  {
    Error_Handler();
  }

  fan_apply_initial_pwm();
}

static void MX_TIM4_Init(void)
{
  TIM_OC_InitTypeDef sConfigOC = {0};
  uint32_t period = tim3_input_clock_hz() / 20000U;

  if (period == 0U)
  {
    period = 1U;
  }

  __HAL_RCC_TIM4_CLK_ENABLE();

  htim4.Instance = TIM4;
  htim4.Init.Prescaler = 0U;
  htim4.Init.CounterMode = TIM_COUNTERMODE_UP;
  htim4.Init.Period = period - 1U;
  htim4.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
  htim4.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_DISABLE;
  if (HAL_TIM_PWM_Init(&htim4) != HAL_OK)
  {
    Error_Handler();
  }

  sConfigOC.OCMode = TIM_OCMODE_PWM1;
  sConfigOC.Pulse = 0U;
  sConfigOC.OCPolarity = TIM_OCPOLARITY_HIGH;
  sConfigOC.OCFastMode = TIM_OCFAST_DISABLE;
  sConfigOC.OCNPolarity = TIM_OCNPOLARITY_HIGH;
  sConfigOC.OCNIdleState = TIM_OCNIDLESTATE_RESET;
  sConfigOC.OCIdleState = TIM_OCIDLESTATE_RESET;

  if (HAL_TIM_PWM_ConfigChannel(&htim4, &sConfigOC, TIM_CHANNEL_1) != HAL_OK)
  {
    Error_Handler();
  }

  if (HAL_TIM_PWM_Start(&htim4, TIM_CHANNEL_1) != HAL_OK)
  {
    Error_Handler();
  }

  magnet_set_pwm(0U);
}

static void MX_TIM6_Init(void)
{
  uint32_t tim_clk_hz = tim3_input_clock_hz();
  uint32_t prescaler = (tim_clk_hz / 1000000U);

  if (prescaler == 0U)
  {
    prescaler = 1U;
  }

  __HAL_RCC_TIM6_CLK_ENABLE();

  htim6.Instance = TIM6;
  htim6.Init.Prescaler = prescaler - 1U;
  htim6.Init.CounterMode = TIM_COUNTERMODE_UP;
  htim6.Init.Period = 5U - 1U;
  htim6.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_DISABLE;
  if (HAL_TIM_Base_Init(&htim6) != HAL_OK)
  {
    Error_Handler();
  }

  HAL_NVIC_SetPriority(TIM6_DAC_IRQn, 1, 0);
  HAL_NVIC_EnableIRQ(TIM6_DAC_IRQn);
}

static void MX_TIM8_Init(void)
{
  __HAL_RCC_TIM8_CLK_ENABLE();
  servo_timer_init_pwm(&htim8, TIM8, HAL_RCC_GetPCLK2Freq());
}

static void MX_TIM16_Init(void)
{
  __HAL_RCC_TIM16_CLK_ENABLE();
  servo_timer_init_pwm(&htim16, TIM16, HAL_RCC_GetPCLK2Freq());
}

static void MX_TIM17_Init(void)
{
  __HAL_RCC_TIM17_CLK_ENABLE();
  servo_timer_init_pwm(&htim17, TIM17, HAL_RCC_GetPCLK2Freq());
}

static void MX_GPIO_Init(void)
{
  GPIO_InitTypeDef GPIO_InitStruct = {0};

  __HAL_RCC_GPIOA_CLK_ENABLE();
  __HAL_RCC_GPIOB_CLK_ENABLE();
  __HAL_RCC_GPIOC_CLK_ENABLE();

  HAL_GPIO_WritePin(LED2_GPIO_Port, LED2_Pin, GPIO_PIN_RESET);
  HAL_GPIO_WritePin(GPIOB, A_STEP_Pin|A_DIR_Pin, GPIO_PIN_RESET);
  HAL_GPIO_WritePin(B_STEP_GPIO_Port, B_STEP_Pin, GPIO_PIN_RESET);
  HAL_GPIO_WritePin(B_DIR_GPIO_Port, B_DIR_Pin, GPIO_PIN_RESET);
  HAL_GPIO_WritePin(AB_EN_GPIO_Port, AB_EN_Pin, GPIO_PIN_SET);
  HAL_GPIO_WritePin(GPIOB, BYJ1_STEP_Pin|BYJ1_DIR_Pin, GPIO_PIN_RESET);
  HAL_GPIO_WritePin(BYJ1_EN_GPIO_Port, BYJ1_EN_Pin, GPIO_PIN_SET);
  HAL_GPIO_WritePin(BYJ2_STEP_GPIO_Port, BYJ2_STEP_Pin, GPIO_PIN_RESET);
  HAL_GPIO_WritePin(BYJ2_DIR_GPIO_Port, BYJ2_DIR_Pin, GPIO_PIN_RESET);
  HAL_GPIO_WritePin(BYJ2_EN_GPIO_Port, BYJ2_EN_Pin, GPIO_PIN_SET);
  HAL_GPIO_WritePin(FAN1_POWER_RELAY_GPIO_Port, FAN1_POWER_RELAY_Pin, GPIO_PIN_RESET);
  HAL_GPIO_WritePin(FAN2_POWER_RELAY_GPIO_Port, FAN2_POWER_RELAY_Pin, GPIO_PIN_RESET);

  GPIO_InitStruct.Pin = LED2_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(LED2_GPIO_Port, &GPIO_InitStruct);

  GPIO_InitStruct.Pin = A_STEP_Pin|A_DIR_Pin|AB_EN_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
  HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);

  GPIO_InitStruct.Pin = BYJ1_STEP_Pin|BYJ1_DIR_Pin|BYJ1_EN_Pin;
  HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);

  GPIO_InitStruct.Pin = BYJ1_ENDSTOP_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
  GPIO_InitStruct.Pull = GPIO_PULLUP;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(BYJ1_ENDSTOP_GPIO_Port, &GPIO_InitStruct);

  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;

  GPIO_InitStruct.Pin = B_STEP_Pin;
  HAL_GPIO_Init(B_STEP_GPIO_Port, &GPIO_InitStruct);

  GPIO_InitStruct.Pin = B_DIR_Pin;
  HAL_GPIO_Init(B_DIR_GPIO_Port, &GPIO_InitStruct);

  GPIO_InitStruct.Pin = BYJ2_STEP_Pin;
  HAL_GPIO_Init(BYJ2_STEP_GPIO_Port, &GPIO_InitStruct);

  GPIO_InitStruct.Pin = BYJ2_DIR_Pin|BYJ2_EN_Pin;
  HAL_GPIO_Init(GPIOC, &GPIO_InitStruct);

  GPIO_InitStruct.Pin = A_MIN_ENDSTOP_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
  GPIO_InitStruct.Pull = GPIO_PULLUP;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

  GPIO_InitStruct.Pin = A_MAX_ENDSTOP_Pin;
  HAL_GPIO_Init(GPIOC, &GPIO_InitStruct);

  GPIO_InitStruct.Pin = B_MIN_ENDSTOP_Pin|B_MAX_ENDSTOP_Pin;
  HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);

  GPIO_InitStruct.Pin = FAN1_SERVO_Pin|FAN2_SERVO_Pin|PAN2_SERVO_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
  GPIO_InitStruct.Alternate = GPIO_AF1_TIM2;
  HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

  GPIO_InitStruct.Pin = FAN1_SERVO_Pin;
  GPIO_InitStruct.Alternate = GPIO_AF1_TIM16;
  HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

  GPIO_InitStruct.Pin = PAN1_SERVO_Pin;
  GPIO_InitStruct.Alternate = GPIO_AF5_TIM8;
  HAL_GPIO_Init(PAN1_SERVO_GPIO_Port, &GPIO_InitStruct);

  GPIO_InitStruct.Pin = FAN2_SERVO_Pin;
  GPIO_InitStruct.Alternate = GPIO_AF1_TIM17;
  HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

  GPIO_InitStruct.Pin = PAN2_SERVO_Pin;
  GPIO_InitStruct.Alternate = GPIO_AF1_TIM2;
  HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

  GPIO_InitStruct.Pin = LID_SERVO_Pin;
  GPIO_InitStruct.Alternate = GPIO_AF6_TIM1;
  HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

  GPIO_InitStruct.Pin = INTEL_FAN1_PWM_Pin|INTEL_FAN2_PWM_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_AF_OD;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
  GPIO_InitStruct.Alternate = GPIO_AF2_TIM3;
  HAL_GPIO_Init(GPIOC, &GPIO_InitStruct);

  GPIO_InitStruct.Pin = MAGNET_PWM_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
  GPIO_InitStruct.Pull = GPIO_PULLUP;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
  GPIO_InitStruct.Alternate = GPIO_AF10_TIM4;
  HAL_GPIO_Init(MAGNET_PWM_GPIO_Port, &GPIO_InitStruct);

  GPIO_InitStruct.Pin = FAN1_POWER_RELAY_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(FAN1_POWER_RELAY_GPIO_Port, &GPIO_InitStruct);

  GPIO_InitStruct.Pin = FAN2_POWER_RELAY_Pin;
  HAL_GPIO_Init(FAN2_POWER_RELAY_GPIO_Port, &GPIO_InitStruct);

  GPIO_InitStruct.Pin = INTEL_FAN1_TACH_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
  GPIO_InitStruct.Pull = GPIO_PULLUP;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(INTEL_FAN1_TACH_GPIO_Port, &GPIO_InitStruct);

  GPIO_InitStruct.Pin = INTEL_FAN2_TACH_Pin;
  HAL_GPIO_Init(INTEL_FAN2_TACH_GPIO_Port, &GPIO_InitStruct);

  fan_power_relay_apply_all();
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
