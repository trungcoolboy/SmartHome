#include "byj1_motion.h"

#include <limits.h>
#include <stdio.h>

#define BYJ1_STEP_DIR_SETUP_US       20U
#define BYJ1_STEP_HIGH_US            40U
#define BYJ1_STEP_LOW_HOLD_US        20U
#define BYJ1_MIN_INTERVAL_US         15U
#define BYJ1_HOME_BACKOFF_INTERVAL_US 4000U

enum
{
  BYJ1_HOME_STATE_IDLE = 0U,
  BYJ1_HOME_STATE_SEEK_MIN = 1U,
  BYJ1_HOME_STATE_BACKOFF = 2U,
};

typedef struct
{
  uint8_t enabled;
  uint8_t moving;
  uint8_t homed;
  uint8_t home_state;
  int32_t position;
  int32_t target;
  int32_t velocity;
  uint32_t next_step_tick;
  uint32_t step_interval_ticks;
  uint16_t start_interval_us;
  uint16_t cruise_interval_us;
  uint16_t accel_interval_delta_us;
} Byj1MotionState;

static Byj1MotionState byj1 = {
  .enabled = 0U,
  .moving = 0U,
  .homed = 0U,
  .home_state = BYJ1_HOME_STATE_IDLE,
  .position = 0,
  .target = 0,
  .velocity = 0,
  .next_step_tick = 0U,
  .step_interval_ticks = 0U,
  .start_interval_us = 64U,
  .cruise_interval_us = 16U,
  .accel_interval_delta_us = 2U,
};
static uint32_t byj1_tick_now(void)
{
  return DWT->CYCCNT;
}

static uint32_t byj1_ticks_from_us(uint16_t us)
{
  return (HAL_RCC_GetHCLKFreq() / 1000000U) * (uint32_t)us;
}

static void byj1_delay_us(uint16_t us)
{
  uint32_t start = byj1_tick_now();
  uint32_t ticks = byj1_ticks_from_us(us);
  while ((uint32_t)(byj1_tick_now() - start) < ticks)
  {
  }
}

static void byj1_apply_enable(void)
{
  HAL_GPIO_WritePin(BYJ1_EN_GPIO_Port, BYJ1_EN_Pin, byj1.enabled ? GPIO_PIN_RESET : GPIO_PIN_SET);
}

uint8_t byj1_motion_endstop_triggered(void)
{
  return HAL_GPIO_ReadPin(BYJ1_ENDSTOP_GPIO_Port, BYJ1_ENDSTOP_Pin) == GPIO_PIN_SET ? 1U : 0U;
}

static void byj1_emit_state(void)
{
  printf("byj byj1 enabled %s moving %s pos %ld target %ld vel %ld endstop %s\r\n",
         byj1.enabled ? "on" : "off",
         byj1.moving ? "on" : "off",
         (long)byj1.position,
         (long)byj1.target,
         (long)byj1.velocity,
         byj1_motion_endstop_triggered() ? "trig" : "clear");
}

static void byj1_prime_motion(void)
{
  byj1.step_interval_ticks = byj1_ticks_from_us(byj1.start_interval_us);
  byj1.next_step_tick = byj1_tick_now();
}

static void byj1_begin_motion(int32_t velocity, int32_t target)
{
  byj1.velocity = velocity;
  byj1.target = target;
  byj1.moving = velocity == 0 ? 0U : 1U;
  if (byj1.moving)
  {
    byj1_prime_motion();
  }
  else
  {
    byj1.next_step_tick = 0U;
  }
}

static void byj1_pulse_step(int32_t direction)
{
  HAL_GPIO_WritePin(BYJ1_DIR_GPIO_Port, BYJ1_DIR_Pin, (direction >= 0) ? GPIO_PIN_SET : GPIO_PIN_RESET);
  byj1_delay_us(BYJ1_STEP_DIR_SETUP_US);
  HAL_GPIO_WritePin(BYJ1_STEP_GPIO_Port, BYJ1_STEP_Pin, GPIO_PIN_SET);
  byj1_delay_us(BYJ1_STEP_HIGH_US);
  HAL_GPIO_WritePin(BYJ1_STEP_GPIO_Port, BYJ1_STEP_Pin, GPIO_PIN_RESET);
  byj1_delay_us(BYJ1_STEP_LOW_HOLD_US);
}

void byj1_motion_init(void)
{
  byj1.enabled = 0U;
  byj1.moving = 0U;
  byj1.homed = 0U;
  byj1.home_state = BYJ1_HOME_STATE_IDLE;
  byj1.position = 0;
  byj1.target = 0;
  byj1.velocity = 0;
  byj1.next_step_tick = 0U;
  byj1.step_interval_ticks = 0U;
  byj1_apply_enable();
}

void byj1_motion_set_enabled(uint8_t enabled)
{
  byj1.enabled = enabled ? 1U : 0U;
  if (byj1.enabled == 0U)
  {
    byj1.moving = 0U;
    byj1.velocity = 0;
    byj1.target = byj1.position;
    byj1.next_step_tick = 0U;
    byj1.home_state = BYJ1_HOME_STATE_IDLE;
  }
  byj1_apply_enable();
}

void byj1_motion_stop(void)
{
  byj1.target = byj1.position;
  byj1.velocity = 0;
  byj1.moving = 0U;
  byj1.next_step_tick = 0U;
  byj1.home_state = BYJ1_HOME_STATE_IDLE;
}

void byj1_motion_home(void)
{
  if (!byj1.enabled)
  {
    printf("err byj disabled\r\n");
    return;
  }
  byj1.homed = 0U;
  byj1.home_state = BYJ1_HOME_STATE_SEEK_MIN;
  byj1_begin_motion(-1, INT32_MIN);
  printf("ok byj byj1 home\r\n");
  byj1_emit_state();
}

void byj1_motion_jog(int32_t direction)
{
  if (!byj1.enabled)
  {
    printf("err byj disabled\r\n");
    return;
  }
  byj1.home_state = BYJ1_HOME_STATE_IDLE;
  byj1_begin_motion((direction < 0) ? -1 : 1, (direction < 0) ? INT32_MIN : INT32_MAX);
  printf("ok byj byj1 jog %s\r\n", (direction < 0) ? "-" : "+");
  byj1_emit_state();
}

void byj1_motion_move_relative(int32_t delta)
{
  if (!byj1.enabled)
  {
    printf("err byj disabled\r\n");
    return;
  }
  byj1.home_state = BYJ1_HOME_STATE_IDLE;
  byj1_begin_motion((delta == 0) ? 0 : ((delta > 0) ? 1 : -1), byj1.position + delta);
  printf("ok byj byj1 move %ld\r\n", (long)delta);
  byj1_emit_state();
}

void byj1_motion_goto(int32_t target)
{
  if (!byj1.enabled)
  {
    printf("err byj disabled\r\n");
    return;
  }
  byj1.home_state = BYJ1_HOME_STATE_IDLE;
  byj1_begin_motion((target == byj1.position) ? 0 : ((target > byj1.position) ? 1 : -1), target);
  printf("ok byj byj1 goto %ld\r\n", (long)target);
  byj1_emit_state();
}

void byj1_motion_tick(void)
{
  int32_t remaining;
  uint32_t now_tick;
  uint32_t decel_steps;

  if (!byj1.moving || !byj1.enabled)
  {
    return;
  }

  if (byj1.home_state == BYJ1_HOME_STATE_BACKOFF)
  {
    if (!byj1_motion_endstop_triggered())
    {
      byj1.moving = 0U;
      byj1.velocity = 0;
      byj1.next_step_tick = 0U;
      byj1.position = 0;
      byj1.target = 0;
      byj1.homed = 1U;
      byj1.home_state = BYJ1_HOME_STATE_IDLE;
      printf("ok byj byj1 homed\r\n");
      byj1_emit_state();
      return;
    }
  }
  else if (byj1_motion_endstop_triggered() && byj1.velocity < 0)
  {
    if (byj1.home_state == BYJ1_HOME_STATE_SEEK_MIN && byj1.target == INT32_MIN)
    {
      byj1.velocity = 1;
      byj1.target = INT32_MAX;
      byj1.step_interval_ticks = byj1_ticks_from_us(BYJ1_HOME_BACKOFF_INTERVAL_US);
      byj1.next_step_tick = byj1_tick_now();
      byj1.home_state = BYJ1_HOME_STATE_BACKOFF;
      byj1_emit_state();
      return;
    }
    byj1.moving = 0U;
    byj1.velocity = 0;
    byj1.next_step_tick = 0U;
    byj1.target = byj1.position;
    byj1.home_state = BYJ1_HOME_STATE_IDLE;
    printf("ok byj byj1 min_endstop\r\n");
    byj1_emit_state();
    return;
  }

  now_tick = byj1_tick_now();
  if ((int32_t)(now_tick - byj1.next_step_tick) < 0)
  {
    return;
  }

  if (byj1.position == byj1.target)
  {
    byj1.moving = 0U;
    byj1.velocity = 0;
    byj1.next_step_tick = 0U;
    byj1_emit_state();
    return;
  }

  remaining = byj1.target - byj1.position;
  if (remaining < 0)
  {
    remaining = -remaining;
  }

  decel_steps = (uint32_t)((byj1.start_interval_us - byj1.cruise_interval_us) / byj1.accel_interval_delta_us) + 2U;

  if (byj1.target == INT32_MAX || byj1.target == INT32_MIN)
  {
    if (byj1.step_interval_ticks > byj1_ticks_from_us(byj1.cruise_interval_us))
    {
      uint32_t next_interval = byj1.step_interval_ticks;
      uint32_t cruise_ticks = byj1_ticks_from_us(byj1.cruise_interval_us);
      uint32_t delta_ticks = byj1_ticks_from_us(byj1.accel_interval_delta_us);
      if ((next_interval - cruise_ticks) > delta_ticks)
      {
        next_interval -= delta_ticks;
      }
      else
      {
        next_interval = cruise_ticks;
      }
      byj1.step_interval_ticks = next_interval;
    }
  }
  else if ((uint32_t)remaining <= decel_steps)
  {
    uint32_t start_ticks = byj1_ticks_from_us(byj1.start_interval_us);
    uint32_t delta_ticks = byj1_ticks_from_us(byj1.accel_interval_delta_us);
    if (byj1.step_interval_ticks < start_ticks)
    {
      uint32_t next_interval = byj1.step_interval_ticks + delta_ticks;
      byj1.step_interval_ticks = (next_interval > start_ticks) ? start_ticks : next_interval;
    }
  }
  else if (byj1.step_interval_ticks > byj1_ticks_from_us(byj1.cruise_interval_us))
  {
    uint32_t next_interval = byj1.step_interval_ticks;
    uint32_t cruise_ticks = byj1_ticks_from_us(byj1.cruise_interval_us);
    uint32_t delta_ticks = byj1_ticks_from_us(byj1.accel_interval_delta_us);
    if ((next_interval - cruise_ticks) > delta_ticks)
    {
      next_interval -= delta_ticks;
    }
    else
    {
      next_interval = cruise_ticks;
    }
    byj1.step_interval_ticks = next_interval;
  }

  if (byj1.step_interval_ticks < byj1_ticks_from_us(BYJ1_MIN_INTERVAL_US))
  {
    byj1.step_interval_ticks = byj1_ticks_from_us(BYJ1_MIN_INTERVAL_US);
  }

  byj1.position += byj1.velocity;
  byj1_pulse_step(byj1.velocity);
  byj1.next_step_tick = now_tick + byj1.step_interval_ticks;

  if (byj1.target == INT32_MAX || byj1.target == INT32_MIN)
  {
    return;
  }

  if (byj1.position == byj1.target)
  {
    byj1.moving = 0U;
    byj1.velocity = 0;
    byj1.next_step_tick = 0U;
    byj1_emit_state();
  }
}

void byj1_motion_get_snapshot(Byj1MotionSnapshot *snapshot)
{
  if (snapshot == NULL)
  {
    return;
  }
  snapshot->enabled = byj1.enabled;
  snapshot->moving = byj1.moving;
  snapshot->homed = byj1.homed;
  snapshot->position = byj1.position;
  snapshot->target = byj1.target;
  snapshot->velocity = byj1.velocity;
}
