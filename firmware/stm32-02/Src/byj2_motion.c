#include "byj2_motion.h"

#include <limits.h>
#include <stdio.h>

#define BYJ2_STEP_DIR_SETUP_US       20U
#define BYJ2_STEP_HIGH_US            40U
#define BYJ2_STEP_LOW_HOLD_US        20U
#define BYJ2_MIN_INTERVAL_US         15U

typedef struct
{
  uint8_t enabled;
  uint8_t moving;
  uint8_t homed;
  int32_t position;
  int32_t target;
  int32_t velocity;
  uint32_t next_step_tick;
  uint32_t step_interval_ticks;
  uint16_t start_interval_us;
  uint16_t cruise_interval_us;
  uint16_t accel_interval_delta_us;
} Byj2MotionState;

static Byj2MotionState byj2 = {
  .enabled = 0U,
  .moving = 0U,
  .homed = 0U,
  .position = 0,
  .target = 0,
  .velocity = 0,
  .next_step_tick = 0U,
  .step_interval_ticks = 0U,
  .start_interval_us = 64U,
  .cruise_interval_us = 16U,
  .accel_interval_delta_us = 2U,
};
static uint32_t byj2_tick_now(void)
{
  return DWT->CYCCNT;
}

static uint32_t byj2_ticks_from_us(uint16_t us)
{
  return (HAL_RCC_GetHCLKFreq() / 1000000U) * (uint32_t)us;
}

static void byj2_delay_us(uint16_t us)
{
  uint32_t start = byj2_tick_now();
  uint32_t ticks = byj2_ticks_from_us(us);
  while ((uint32_t)(byj2_tick_now() - start) < ticks)
  {
  }
}

static void byj2_apply_enable(void)
{
  HAL_GPIO_WritePin(BYJ2_EN_GPIO_Port, BYJ2_EN_Pin, byj2.enabled ? GPIO_PIN_RESET : GPIO_PIN_SET);
}

uint8_t byj2_motion_endstop_triggered(void)
{
  return 0U;
}

static void byj2_emit_state(void)
{
  printf("byj byj2 enabled %s moving %s pos %ld target %ld vel %ld endstop clear\r\n",
         byj2.enabled ? "on" : "off",
         byj2.moving ? "on" : "off",
         (long)byj2.position,
         (long)byj2.target,
         (long)byj2.velocity);
}

static void byj2_prime_motion(void)
{
  byj2.step_interval_ticks = byj2_ticks_from_us(byj2.start_interval_us);
  byj2.next_step_tick = byj2_tick_now();
}

static void byj2_begin_motion(int32_t velocity, int32_t target)
{
  byj2.velocity = velocity;
  byj2.target = target;
  byj2.moving = velocity == 0 ? 0U : 1U;
  if (byj2.moving)
  {
    byj2_prime_motion();
  }
  else
  {
    byj2.next_step_tick = 0U;
  }
}

static void byj2_pulse_step(int32_t direction)
{
  HAL_GPIO_WritePin(BYJ2_DIR_GPIO_Port, BYJ2_DIR_Pin, (direction >= 0) ? GPIO_PIN_SET : GPIO_PIN_RESET);
  byj2_delay_us(BYJ2_STEP_DIR_SETUP_US);
  HAL_GPIO_WritePin(BYJ2_STEP_GPIO_Port, BYJ2_STEP_Pin, GPIO_PIN_SET);
  byj2_delay_us(BYJ2_STEP_HIGH_US);
  HAL_GPIO_WritePin(BYJ2_STEP_GPIO_Port, BYJ2_STEP_Pin, GPIO_PIN_RESET);
  byj2_delay_us(BYJ2_STEP_LOW_HOLD_US);
}

void byj2_motion_init(void)
{
  byj2.enabled = 0U;
  byj2.moving = 0U;
  byj2.homed = 0U;
  byj2.position = 0;
  byj2.target = 0;
  byj2.velocity = 0;
  byj2.next_step_tick = 0U;
  byj2.step_interval_ticks = 0U;
  byj2_apply_enable();
}

void byj2_motion_set_enabled(uint8_t enabled)
{
  byj2.enabled = enabled ? 1U : 0U;
  if (byj2.enabled == 0U)
  {
    byj2.moving = 0U;
    byj2.velocity = 0;
    byj2.target = byj2.position;
    byj2.next_step_tick = 0U;
  }
  byj2_apply_enable();
}

void byj2_motion_stop(void)
{
  byj2.target = byj2.position;
  byj2.velocity = 0;
  byj2.moving = 0U;
  byj2.next_step_tick = 0U;
}

void byj2_motion_home(void)
{
  printf("err byj no endstop\r\n");
}

void byj2_motion_jog(int32_t direction)
{
  if (!byj2.enabled)
  {
    printf("err byj disabled\r\n");
    return;
  }
  byj2_begin_motion((direction < 0) ? -1 : 1, (direction < 0) ? INT32_MIN : INT32_MAX);
  printf("ok byj byj2 jog %s\r\n", (direction < 0) ? "-" : "+");
  byj2_emit_state();
}

void byj2_motion_move_relative(int32_t delta)
{
  if (!byj2.enabled)
  {
    printf("err byj disabled\r\n");
    return;
  }
  byj2_begin_motion((delta == 0) ? 0 : ((delta > 0) ? 1 : -1), byj2.position + delta);
  printf("ok byj byj2 move %ld\r\n", (long)delta);
  byj2_emit_state();
}

void byj2_motion_goto(int32_t target)
{
  if (!byj2.enabled)
  {
    printf("err byj disabled\r\n");
    return;
  }
  byj2_begin_motion((target == byj2.position) ? 0 : ((target > byj2.position) ? 1 : -1), target);
  printf("ok byj byj2 goto %ld\r\n", (long)target);
  byj2_emit_state();
}

void byj2_motion_tick(void)
{
  int32_t remaining;
  uint32_t now_tick;
  uint32_t decel_steps;

  if (!byj2.moving || !byj2.enabled)
  {
    return;
  }

  now_tick = byj2_tick_now();
  if ((int32_t)(now_tick - byj2.next_step_tick) < 0)
  {
    return;
  }

  if (byj2.position == byj2.target)
  {
    byj2.moving = 0U;
    byj2.velocity = 0;
    byj2.next_step_tick = 0U;
    byj2_emit_state();
    return;
  }

  remaining = byj2.target - byj2.position;
  if (remaining < 0)
  {
    remaining = -remaining;
  }

  decel_steps = (uint32_t)((byj2.start_interval_us - byj2.cruise_interval_us) / byj2.accel_interval_delta_us) + 2U;

  if (byj2.target == INT32_MAX || byj2.target == INT32_MIN)
  {
    if (byj2.step_interval_ticks > byj2_ticks_from_us(byj2.cruise_interval_us))
    {
      uint32_t next_interval = byj2.step_interval_ticks;
      uint32_t cruise_ticks = byj2_ticks_from_us(byj2.cruise_interval_us);
      uint32_t delta_ticks = byj2_ticks_from_us(byj2.accel_interval_delta_us);
      if ((next_interval - cruise_ticks) > delta_ticks)
      {
        next_interval -= delta_ticks;
      }
      else
      {
        next_interval = cruise_ticks;
      }
      byj2.step_interval_ticks = next_interval;
    }
  }
  else if ((uint32_t)remaining <= decel_steps)
  {
    uint32_t start_ticks = byj2_ticks_from_us(byj2.start_interval_us);
    uint32_t delta_ticks = byj2_ticks_from_us(byj2.accel_interval_delta_us);
    if (byj2.step_interval_ticks < start_ticks)
    {
      uint32_t next_interval = byj2.step_interval_ticks + delta_ticks;
      byj2.step_interval_ticks = (next_interval > start_ticks) ? start_ticks : next_interval;
    }
  }
  else if (byj2.step_interval_ticks > byj2_ticks_from_us(byj2.cruise_interval_us))
  {
    uint32_t next_interval = byj2.step_interval_ticks;
    uint32_t cruise_ticks = byj2_ticks_from_us(byj2.cruise_interval_us);
    uint32_t delta_ticks = byj2_ticks_from_us(byj2.accel_interval_delta_us);
    if ((next_interval - cruise_ticks) > delta_ticks)
    {
      next_interval -= delta_ticks;
    }
    else
    {
      next_interval = cruise_ticks;
    }
    byj2.step_interval_ticks = next_interval;
  }

  if (byj2.step_interval_ticks < byj2_ticks_from_us(BYJ2_MIN_INTERVAL_US))
  {
    byj2.step_interval_ticks = byj2_ticks_from_us(BYJ2_MIN_INTERVAL_US);
  }

  byj2.position += byj2.velocity;
  byj2_pulse_step(byj2.velocity);
  byj2.next_step_tick = now_tick + byj2.step_interval_ticks;

  if (byj2.target == INT32_MAX || byj2.target == INT32_MIN)
  {
    return;
  }

  if (byj2.position == byj2.target)
  {
    byj2.moving = 0U;
    byj2.velocity = 0;
    byj2.next_step_tick = 0U;
    byj2_emit_state();
  }
}

void byj2_motion_get_snapshot(Byj2MotionSnapshot *snapshot)
{
  if (snapshot == NULL)
  {
    return;
  }
  snapshot->enabled = byj2.enabled;
  snapshot->moving = byj2.moving;
  snapshot->homed = byj2.homed;
  snapshot->position = byj2.position;
  snapshot->target = byj2.target;
  snapshot->velocity = byj2.velocity;
}
