#include "b_axis_motion.h"
#include "axis_travel_store.h"

#include <limits.h>
#include <stdio.h>

#define B_STEP_PULSE_HIGH_US     5U
#define B_HOME_SEEK_LIMIT_STEPS  50000U
#define B_HOME_RELEASE_LIMIT     8000U
#define B_SCAN_MAX_LIMIT_STEPS   60000U
#define B_SCAN_INTERVAL_US       1000U
#define B_DEFAULT_TRAVEL_STEPS   5656U
#define B_HOME_SEEK_DIRECTION    (-1)
#define B_HOME_RELEASE_DIRECTION 1

typedef struct
{
  uint8_t enabled;
  uint8_t moving;
  uint8_t homed;
  uint8_t homing_state;
  int32_t position;
  int32_t target;
  int32_t velocity;
  uint32_t travel_steps;
  uint32_t decel_window_steps;
  uint16_t start_interval_us;
  uint16_t cruise_interval_us;
  uint16_t homing_interval_us;
  uint16_t accel_interval_delta_us;
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
  volatile uint8_t pulse_high_phase;
} BMotionState;

typedef enum
{
  B_SEQUENCE_IDLE = 0,
  B_SEQUENCE_HOME_SEEK,
  B_SEQUENCE_HOME_RELEASE,
  B_SEQUENCE_SCAN_HOME_SEEK,
  B_SEQUENCE_SCAN_HOME_RELEASE,
  B_SEQUENCE_SCAN_SEEK_MAX,
  B_SEQUENCE_SCAN_RETURN_HOME_SEEK,
  B_SEQUENCE_SCAN_RETURN_HOME_RELEASE
} BSequenceState;

typedef struct
{
  BSequenceState state;
  int32_t phase_start_position;
  uint32_t release_steps;
  uint32_t measured_travel;
} BSequenceControl;

static BAxisState b_axis = {
  .enabled = 0U,
  .moving = 0U,
  .homed = 0U,
  .homing_state = 0U,
  .position = 0,
  .target = 0,
  .velocity = 0,
  .travel_steps = B_DEFAULT_TRAVEL_STEPS,
  .decel_window_steps = 300U,
  .start_interval_us = 2000U,
  .cruise_interval_us = 100U,
  .homing_interval_us = 40U,
  .accel_interval_delta_us = 10U,
};

static BMotionState b_motion = {0};
static BSequenceControl b_sequence = {0};

static GPIO_PinState b_dir_level(int32_t direction)
{
  return (direction >= 0) ? GPIO_PIN_SET : GPIO_PIN_RESET;
}

static uint8_t b_min_endstop_triggered(void)
{
  return HAL_GPIO_ReadPin(B_MIN_ENDSTOP_GPIO_Port, B_MIN_ENDSTOP_Pin) == GPIO_PIN_SET ? 1U : 0U;
}

static uint8_t b_max_endstop_triggered(void)
{
  return HAL_GPIO_ReadPin(B_MAX_ENDSTOP_GPIO_Port, B_MAX_ENDSTOP_Pin) == GPIO_PIN_SET ? 1U : 0U;
}

static uint32_t cycles_per_us(void)
{
  return HAL_RCC_GetHCLKFreq() / 1000000U;
}

static void step_delay_us(uint16_t us)
{
  uint32_t start = DWT->CYCCNT;
  uint32_t ticks = cycles_per_us() * (uint32_t)us;
  while ((uint32_t)(DWT->CYCCNT - start) < ticks)
  {
  }
}

static void b_motion_rearm(uint32_t interval_us)
{
  __HAL_TIM_SET_AUTORELOAD(&htim6, interval_us - 1U);
  __HAL_TIM_SET_COUNTER(&htim6, 0U);
}

static void b_motion_halt(void)
{
  HAL_TIM_Base_Stop_IT(&htim6);
  b_motion.active = 0U;
  b_motion.continuous = 0U;
  b_motion.pulse_high_phase = 0U;
  b_motion.steps_remaining = 0U;
  HAL_GPIO_WritePin(B_STEP_GPIO_Port, B_STEP_Pin, GPIO_PIN_RESET);
}

static void b_axis_mark_stopped(void)
{
  b_axis.target = b_axis.position;
  b_axis.velocity = 0;
  b_axis.moving = 0U;
}

static void b_axis_motion_abort_sequence(void)
{
  b_sequence.state = B_SEQUENCE_IDLE;
  b_sequence.phase_start_position = b_axis.position;
  b_sequence.release_steps = 0U;
  b_sequence.measured_travel = 0U;
  b_axis.homing_state = 0U;
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
  b_motion.pulse_high_phase = 0U;
  b_motion.active = (steps > 0U) ? 1U : 0U;

  if (b_motion.active != 0U)
  {
    HAL_TIM_Base_Stop_IT(&htim6);
    HAL_GPIO_WritePin(B_DIR_GPIO_Port, B_DIR_Pin, b_dir_level(direction));
    b_motion_rearm((b_motion.current_interval_us > B_STEP_PULSE_HIGH_US) ? (b_motion.current_interval_us - B_STEP_PULSE_HIGH_US) : 10U);
    HAL_TIM_Base_Start_IT(&htim6);
  }
}

static void b_motion_start_continuous(int32_t direction, uint32_t interval_us)
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
  b_motion.pulse_high_phase = 0U;
  b_motion.active = 1U;

  HAL_TIM_Base_Stop_IT(&htim6);
  HAL_GPIO_WritePin(B_DIR_GPIO_Port, B_DIR_Pin, b_dir_level(direction));
  b_motion_rearm((b_motion.current_interval_us > B_STEP_PULSE_HIGH_US) ? (b_motion.current_interval_us - B_STEP_PULSE_HIGH_US) : 10U);
  HAL_TIM_Base_Start_IT(&htim6);
}

void b_axis_motion_stop(void)
{
  b_motion_halt();
  b_axis_mark_stopped();
  b_axis_motion_abort_sequence();
}

static uint32_t b_target_interval_for_position(void)
{
  uint32_t distance_to_edge;
  uint32_t ramp_span;

  if (b_axis.decel_window_steps == 0U || b_axis.start_interval_us <= b_motion.interval_us)
  {
    return b_motion.interval_us;
  }

  if (b_motion.direction > 0)
  {
    distance_to_edge = ((uint32_t)b_axis.position >= b_axis.travel_steps) ? 0U : (b_axis.travel_steps - (uint32_t)b_axis.position);
  }
  else
  {
    distance_to_edge = (b_axis.position <= 0) ? 0U : (uint32_t)b_axis.position;
  }

  if (distance_to_edge >= b_axis.decel_window_steps)
  {
    return b_motion.interval_us;
  }

  ramp_span = b_axis.start_interval_us - b_motion.interval_us;
  return b_motion.interval_us + (uint32_t)(((uint64_t)ramp_span * (uint64_t)(b_axis.decel_window_steps - distance_to_edge)) / (uint64_t)b_axis.decel_window_steps);
}

static uint32_t b_position_delta_since_phase_start(void)
{
  int32_t delta = b_axis.position - b_sequence.phase_start_position;
  if (delta < 0)
  {
    delta = -delta;
  }
  return (uint32_t)delta;
}

void b_axis_motion_irq(void)
{
  uint32_t target_interval;

  if (b_motion.active == 0U || b_axis.enabled == 0U)
  {
    b_motion_halt();
    b_axis_mark_stopped();
    return;
  }

  if (b_motion.pulse_high_phase == 0U)
  {
    if (b_motion.stop_on_endstop != 0U)
    {
      if (b_motion.direction < 0 && b_min_endstop_triggered())
      {
        printf("axis b irq stop min dir %ld pos %ld\r\n", (long)b_motion.direction, (long)b_axis.position);
        b_motion_halt();
        b_axis_mark_stopped();
        return;
      }
      if (b_motion.direction > 0 && b_max_endstop_triggered())
      {
        printf("axis b irq stop max dir %ld pos %ld\r\n", (long)b_motion.direction, (long)b_axis.position);
        b_motion_halt();
        b_axis_mark_stopped();
        return;
      }
    }

    HAL_GPIO_WritePin(B_DIR_GPIO_Port, B_DIR_Pin, b_dir_level(b_motion.direction));
    HAL_GPIO_WritePin(B_STEP_GPIO_Port, B_STEP_Pin, GPIO_PIN_SET);
    b_motion.pulse_high_phase = 1U;
    b_motion_rearm(B_STEP_PULSE_HIGH_US);
    return;
  }

  HAL_GPIO_WritePin(B_STEP_GPIO_Port, B_STEP_Pin, GPIO_PIN_RESET);
  b_motion.pulse_high_phase = 0U;
  b_axis.position += b_motion.direction;
  b_motion.moved_steps++;

  if (b_motion.continuous != 0U)
  {
    target_interval = b_target_interval_for_position();
    if (b_motion.current_interval_us > target_interval)
    {
      uint32_t delta = b_motion.current_interval_us - target_interval;
      uint32_t next_interval = b_motion.current_interval_us - ((delta > b_axis.accel_interval_delta_us) ? b_axis.accel_interval_delta_us : delta);
      b_motion.current_interval_us = next_interval;
    }
    else if (b_motion.current_interval_us < target_interval)
    {
      uint32_t next_interval = b_motion.current_interval_us + b_axis.accel_interval_delta_us;
      b_motion.current_interval_us = (next_interval > target_interval) ? target_interval : next_interval;
    }
  }

  if (b_motion.continuous == 0U && b_motion.steps_remaining > 0U)
  {
    b_motion.steps_remaining--;
    if (b_motion.steps_remaining == 0U)
    {
      b_motion_halt();
      b_axis_mark_stopped();
      return;
    }
  }

  b_motion_rearm((b_motion.current_interval_us > B_STEP_PULSE_HIGH_US) ? (b_motion.current_interval_us - B_STEP_PULSE_HIGH_US) : 10U);
}

void b_axis_motion_init(void)
{
  b_axis_motion_stop();
}

uint8_t b_axis_motion_active(void)
{
  return b_motion.active;
}

void b_axis_motion_set_enabled(uint8_t enabled)
{
  b_axis.enabled = enabled ? 1U : 0U;
  if (b_axis.enabled == 0U)
  {
    b_axis_motion_stop();
  }
}

void b_axis_motion_set_travel(uint32_t travel_steps)
{
  if (travel_steps >= 100U)
  {
    b_axis.travel_steps = travel_steps;
  }
}

void b_axis_motion_set_decel_window(uint32_t decel_window_steps)
{
  if (decel_window_steps >= 10U)
  {
    b_axis.decel_window_steps = decel_window_steps;
  }
}

void b_axis_motion_set_cruise_interval(uint16_t cruise_interval_us)
{
  b_axis.cruise_interval_us = cruise_interval_us;
}

void b_axis_motion_set_homing_interval(uint16_t homing_interval_us)
{
  b_axis.homing_interval_us = homing_interval_us;
}

void b_axis_motion_set_start_interval(uint16_t start_interval_us)
{
  b_axis.start_interval_us = start_interval_us;
}

void b_axis_motion_set_accel_delta(uint16_t accel_interval_delta_us)
{
  b_axis.accel_interval_delta_us = accel_interval_delta_us;
}

void b_axis_motion_home(void)
{
  if (b_axis.enabled == 0U)
  {
    printf("err axis b disabled\r\n");
    return;
  }

  b_motion_halt();
  b_axis_mark_stopped();
  b_axis.homed = 0U;
  b_axis.moving = 1U;
  b_axis.velocity = B_HOME_SEEK_DIRECTION;
  b_axis.target = (B_HOME_SEEK_DIRECTION > 0) ? INT32_MAX : INT32_MIN;
  b_axis.homing_state = 1U;
  b_sequence.state = B_SEQUENCE_HOME_SEEK;
  b_sequence.phase_start_position = b_axis.position;
  b_sequence.release_steps = 0U;
  b_sequence.measured_travel = 0U;
  printf("axis b home begin seek_dir %ld release_dir %ld endstop_min %s endstop_max %s\r\n",
         (long)B_HOME_SEEK_DIRECTION,
         (long)B_HOME_RELEASE_DIRECTION,
         b_min_endstop_triggered() ? "trig" : "clear",
         b_max_endstop_triggered() ? "trig" : "clear");
  b_motion_start_continuous(B_HOME_SEEK_DIRECTION, b_axis.homing_interval_us);
}

void b_axis_motion_scan(void)
{
  if (b_axis.enabled == 0U)
  {
    printf("err axis b disabled\r\n");
    return;
  }

  b_motion_halt();
  b_axis_mark_stopped();
  b_axis.homed = 0U;
  b_axis.moving = 1U;
  b_axis.velocity = B_HOME_SEEK_DIRECTION;
  b_axis.target = (B_HOME_SEEK_DIRECTION > 0) ? INT32_MAX : INT32_MIN;
  b_axis.homing_state = 3U;
  b_sequence.state = B_SEQUENCE_SCAN_HOME_SEEK;
  b_sequence.phase_start_position = b_axis.position;
  b_sequence.release_steps = 0U;
  b_sequence.measured_travel = 0U;
  printf("axis b scan begin dir 1 endstop_min %s endstop_max %s pos %ld\r\n",
         b_min_endstop_triggered() ? "trig" : "clear",
         b_max_endstop_triggered() ? "trig" : "clear",
         (long)b_axis.position);
  b_motion_start_continuous(B_HOME_SEEK_DIRECTION, b_axis.homing_interval_us);
}

void b_axis_motion_jog(int32_t direction)
{
  if (direction == 0 || b_axis.enabled == 0U)
  {
    return;
  }
  b_axis.homing_state = 0U;
  b_axis.moving = 1U;
  b_axis.velocity = (direction < 0) ? -1 : 1;
  b_axis.target = (b_axis.velocity > 0) ? INT32_MAX : INT32_MIN;
  b_motion_start_continuous(b_axis.velocity, b_axis.cruise_interval_us);
}

void b_axis_motion_move_relative(int32_t delta)
{
  if (delta == 0 || b_axis.enabled == 0U)
  {
    return;
  }
  b_axis.homing_state = 0U;
  b_axis.moving = 1U;
  b_axis.velocity = (delta < 0) ? -1 : 1;
  b_axis.target = b_axis.position + delta;
  b_motion_start(b_axis.velocity, (uint32_t)((delta < 0) ? -delta : delta), b_axis.cruise_interval_us, 1U);
}

void b_axis_motion_goto(int32_t target)
{
  int32_t delta = target - b_axis.position;
  b_axis_motion_move_relative(delta);
}

void b_axis_motion_set_position(int32_t position, uint8_t homed)
{
  b_motion_halt();
  b_axis.position = position;
  b_axis.target = position;
  b_axis.velocity = 0;
  b_axis.moving = 0U;
  b_axis.homed = homed ? 1U : 0U;
  b_axis_motion_abort_sequence();
}

void b_axis_motion_get_snapshot(BAxisMotionSnapshot *snapshot)
{
  if (snapshot == NULL)
  {
    return;
  }

  snapshot->enabled = b_axis.enabled;
  snapshot->moving = b_axis.moving;
  snapshot->homed = b_axis.homed;
  snapshot->homing_state = b_axis.homing_state;
  snapshot->position = b_axis.position;
  snapshot->target = b_axis.target;
  snapshot->velocity = b_axis.velocity;
  snapshot->travel_steps = b_axis.travel_steps;
  snapshot->decel_window_steps = b_axis.decel_window_steps;
}

void b_axis_motion_update(void)
{
  switch (b_sequence.state)
  {
    case B_SEQUENCE_IDLE:
      return;

    case B_SEQUENCE_HOME_SEEK:
      if (b_motion.active != 0U)
      {
        if (b_position_delta_since_phase_start() >= B_HOME_SEEK_LIMIT_STEPS)
        {
          printf("err axis b home seek timeout\r\n");
          b_axis_motion_stop();
        }
        return;
      }
      if (!b_min_endstop_triggered())
      {
        return;
      }
      b_axis.homing_state = 2U;
      b_sequence.state = B_SEQUENCE_HOME_RELEASE;
      b_sequence.release_steps = 0U;
      return;

    case B_SEQUENCE_HOME_RELEASE:
      if (b_motion.active != 0U)
      {
        return;
      }
      if (!b_min_endstop_triggered())
      {
        b_axis.position = 0;
        b_axis.homed = 1U;
        b_axis_mark_stopped();
        printf("ok axis b homed release_steps %lu\r\n", (unsigned long)b_sequence.release_steps);
        b_axis_motion_abort_sequence();
        return;
      }
      if (b_sequence.release_steps >= B_HOME_RELEASE_LIMIT)
      {
        printf("err axis b home release timeout\r\n");
        b_axis_motion_stop();
        return;
      }
      b_axis.moving = 1U;
      b_axis.velocity = B_HOME_RELEASE_DIRECTION;
      b_axis.target = INT32_MAX;
      b_motion_start(B_HOME_RELEASE_DIRECTION, 1U, b_axis.homing_interval_us, 0U);
      b_sequence.release_steps++;
      return;

    case B_SEQUENCE_SCAN_HOME_SEEK:
      if (b_motion.active != 0U)
      {
        if (b_position_delta_since_phase_start() >= B_HOME_SEEK_LIMIT_STEPS)
        {
          printf("err axis b home seek timeout\r\n");
          b_axis_motion_stop();
        }
        return;
      }
      if (!b_min_endstop_triggered())
      {
        return;
      }
      b_axis.homing_state = 4U;
      b_sequence.state = B_SEQUENCE_SCAN_HOME_RELEASE;
      b_sequence.release_steps = 0U;
      return;

    case B_SEQUENCE_SCAN_HOME_RELEASE:
      if (b_motion.active != 0U)
      {
        return;
      }
      if (!b_min_endstop_triggered())
      {
        b_axis.position = 0;
        b_axis.homing_state = 5U;
        b_axis.moving = 1U;
        b_axis.velocity = 1;
        b_axis.target = INT32_MAX;
        b_sequence.state = B_SEQUENCE_SCAN_SEEK_MAX;
        b_sequence.phase_start_position = b_axis.position;
        b_motion_start_continuous(1, B_SCAN_INTERVAL_US);
        return;
      }
      if (b_sequence.release_steps >= B_HOME_RELEASE_LIMIT)
      {
        printf("err axis b home release timeout\r\n");
        b_axis_motion_stop();
        return;
      }
      b_axis.moving = 1U;
      b_axis.velocity = B_HOME_RELEASE_DIRECTION;
      b_axis.target = INT32_MAX;
      b_motion_start(B_HOME_RELEASE_DIRECTION, 1U, b_axis.homing_interval_us, 0U);
      b_sequence.release_steps++;
      return;

    case B_SEQUENCE_SCAN_SEEK_MAX:
      if (b_motion.active != 0U)
      {
        if (b_position_delta_since_phase_start() >= B_SCAN_MAX_LIMIT_STEPS)
        {
          printf("err axis b scan seek_max timeout\r\n");
          b_axis_motion_stop();
        }
        return;
      }
      if (!b_max_endstop_triggered())
      {
        return;
      }
      b_sequence.measured_travel = (b_axis.position > 0) ? (uint32_t)b_axis.position : b_axis.travel_steps;
      b_axis.travel_steps = b_sequence.measured_travel;
      b_axis.homed = 0U;
      b_axis.homing_state = 3U;
      b_axis.moving = 1U;
      b_axis.velocity = B_HOME_SEEK_DIRECTION;
      b_axis.target = (B_HOME_SEEK_DIRECTION > 0) ? INT32_MAX : INT32_MIN;
      b_sequence.state = B_SEQUENCE_SCAN_RETURN_HOME_SEEK;
      b_sequence.phase_start_position = b_axis.position;
      b_motion_start_continuous(B_HOME_SEEK_DIRECTION, b_axis.homing_interval_us);
      return;

    case B_SEQUENCE_SCAN_RETURN_HOME_SEEK:
      if (b_motion.active != 0U)
      {
        if (b_position_delta_since_phase_start() >= B_HOME_SEEK_LIMIT_STEPS)
        {
          printf("err axis b home seek timeout\r\n");
          b_axis_motion_stop();
        }
        return;
      }
      if (!b_min_endstop_triggered())
      {
        return;
      }
      b_axis.homing_state = 4U;
      b_sequence.state = B_SEQUENCE_SCAN_RETURN_HOME_RELEASE;
      b_sequence.release_steps = 0U;
      return;

    case B_SEQUENCE_SCAN_RETURN_HOME_RELEASE:
      if (b_motion.active != 0U)
      {
        return;
      }
      if (!b_min_endstop_triggered())
      {
        b_axis.position = 0;
        b_axis.homed = 1U;
        b_axis_mark_stopped();
        if (axis_travel_store_save_b(b_sequence.measured_travel) == 0U)
        {
          printf("err axis b travel save\r\n");
        }
        printf("ok axis b scan travel_steps %lu returned_home yes\r\n", (unsigned long)b_sequence.measured_travel);
        b_axis_motion_abort_sequence();
        return;
      }
      if (b_sequence.release_steps >= B_HOME_RELEASE_LIMIT)
      {
        printf("err axis b home release timeout\r\n");
        b_axis_motion_stop();
        return;
      }
      b_axis.moving = 1U;
      b_axis.velocity = B_HOME_RELEASE_DIRECTION;
      b_axis.target = INT32_MAX;
      b_motion_start(B_HOME_RELEASE_DIRECTION, 1U, b_axis.homing_interval_us, 0U);
      b_sequence.release_steps++;
      return;
  }
}
