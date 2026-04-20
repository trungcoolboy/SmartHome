#include "a_axis_motion.h"
#include "axis_travel_store.h"

#include <limits.h>
#include <stdio.h>

#define A_STEP_PULSE_HIGH_US     5U
#define A_HOME_SEEK_LIMIT_STEPS  51000U
#define A_HOME_RELEASE_LIMIT     8000U
#define A_SCAN_MAX_LIMIT_STEPS   60000U
#define A_SCAN_INTERVAL_US       850U
#define A_DEFAULT_TRAVEL_STEPS   49983U
#define A_HOME_SEEK_DIRECTION    (-1)
#define A_HOME_RELEASE_DIRECTION 1

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
} AAxisState;

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
} AMotionState;

typedef enum
{
  A_SEQUENCE_IDLE = 0,
  A_SEQUENCE_HOME_SEEK,
  A_SEQUENCE_HOME_RELEASE,
  A_SEQUENCE_SCAN_HOME_SEEK,
  A_SEQUENCE_SCAN_HOME_RELEASE,
  A_SEQUENCE_SCAN_SEEK_MAX,
  A_SEQUENCE_SCAN_RETURN_HOME_SEEK,
  A_SEQUENCE_SCAN_RETURN_HOME_RELEASE
} ASequenceState;

typedef struct
{
  ASequenceState state;
  int32_t phase_start_position;
  uint32_t release_steps;
  uint32_t measured_travel;
} ASequenceControl;

static AAxisState a_axis = {
  .enabled = 0U,
  .moving = 0U,
  .homed = 0U,
  .homing_state = 0U,
  .position = 0,
  .target = 0,
  .velocity = 0,
  .travel_steps = A_DEFAULT_TRAVEL_STEPS,
  .decel_window_steps = 0U,
  .start_interval_us = 600U,
  .cruise_interval_us = 80U,
  .homing_interval_us = 850U,
  .accel_interval_delta_us = 20U,
};

static AMotionState a_motion = {0};
static ASequenceControl a_sequence = {0};

static GPIO_PinState a_dir_level(int32_t direction)
{
  return (direction >= 0) ? GPIO_PIN_RESET : GPIO_PIN_SET;
}

static uint8_t a_min_endstop_triggered(void)
{
  return HAL_GPIO_ReadPin(A_MIN_ENDSTOP_GPIO_Port, A_MIN_ENDSTOP_Pin) == GPIO_PIN_SET ? 1U : 0U;
}

static uint8_t a_max_endstop_triggered(void)
{
  return HAL_GPIO_ReadPin(A_MAX_ENDSTOP_GPIO_Port, A_MAX_ENDSTOP_Pin) == GPIO_PIN_SET ? 1U : 0U;
}

static void a_motion_rearm(uint32_t interval_us)
{
  __HAL_TIM_SET_AUTORELOAD(&htim6, interval_us - 1U);
  __HAL_TIM_SET_COUNTER(&htim6, 0U);
}

static void a_motion_halt(void)
{
  HAL_TIM_Base_Stop_IT(&htim6);
  a_motion.active = 0U;
  a_motion.continuous = 0U;
  a_motion.pulse_high_phase = 0U;
  a_motion.steps_remaining = 0U;
  HAL_GPIO_WritePin(A_STEP_GPIO_Port, A_STEP_Pin, GPIO_PIN_RESET);
}

static void a_axis_mark_stopped(void)
{
  a_axis.target = a_axis.position;
  a_axis.velocity = 0;
  a_axis.moving = 0U;
}

static void a_axis_motion_abort_sequence(void)
{
  a_sequence.state = A_SEQUENCE_IDLE;
  a_sequence.phase_start_position = a_axis.position;
  a_sequence.release_steps = 0U;
  a_sequence.measured_travel = 0U;
  a_axis.homing_state = 0U;
}

static void a_axis_motion_stop_internal(void)
{
  a_motion_halt();
  a_axis_mark_stopped();
  a_axis_motion_abort_sequence();
}

static void a_motion_start(int32_t direction, uint32_t steps, uint32_t interval_us, uint8_t stop_on_endstop)
{
  if (interval_us < 10U)
  {
    interval_us = 10U;
  }

  a_motion.direction = direction;
  a_motion.steps_remaining = steps;
  a_motion.moved_steps = 0U;
  a_motion.interval_us = interval_us;
  a_motion.current_interval_us = interval_us;
  a_motion.stop_on_endstop = stop_on_endstop;
  a_motion.continuous = 0U;
  a_motion.pulse_high_phase = 0U;
  a_motion.active = (steps > 0U) ? 1U : 0U;

  if (a_motion.active != 0U)
  {
    HAL_TIM_Base_Stop_IT(&htim6);
    HAL_GPIO_WritePin(A_DIR_GPIO_Port, A_DIR_Pin, a_dir_level(direction));
    a_motion_rearm((a_motion.current_interval_us > A_STEP_PULSE_HIGH_US) ? (a_motion.current_interval_us - A_STEP_PULSE_HIGH_US) : 10U);
    HAL_TIM_Base_Start_IT(&htim6);
  }
}

static void a_motion_start_continuous(int32_t direction, uint32_t interval_us)
{
  if (interval_us < 10U)
  {
    interval_us = 10U;
  }

  a_motion.direction = direction;
  a_motion.steps_remaining = 0U;
  a_motion.moved_steps = 0U;
  a_motion.interval_us = interval_us;
  a_motion.current_interval_us = interval_us;
  a_motion.stop_on_endstop = 1U;
  a_motion.continuous = 1U;
  a_motion.pulse_high_phase = 0U;
  a_motion.active = 1U;

  HAL_TIM_Base_Stop_IT(&htim6);
  HAL_GPIO_WritePin(A_DIR_GPIO_Port, A_DIR_Pin, a_dir_level(direction));
  a_motion_rearm((a_motion.current_interval_us > A_STEP_PULSE_HIGH_US) ? (a_motion.current_interval_us - A_STEP_PULSE_HIGH_US) : 10U);
  HAL_TIM_Base_Start_IT(&htim6);
}

static uint32_t a_position_delta_since_phase_start(void)
{
  int32_t delta = a_axis.position - a_sequence.phase_start_position;
  if (delta < 0)
  {
    delta = -delta;
  }
  return (uint32_t)delta;
}

static uint32_t a_target_interval_for_position(void)
{
  uint32_t distance_to_edge;
  uint32_t ramp_span;

  if (a_axis.decel_window_steps == 0U || a_axis.start_interval_us <= a_motion.interval_us || a_axis.travel_steps == 0U)
  {
    return a_motion.interval_us;
  }

  if (a_motion.direction > 0)
  {
    distance_to_edge = ((uint32_t)a_axis.position >= a_axis.travel_steps) ? 0U : (a_axis.travel_steps - (uint32_t)a_axis.position);
  }
  else
  {
    distance_to_edge = (a_axis.position <= 0) ? 0U : (uint32_t)a_axis.position;
  }

  if (distance_to_edge >= a_axis.decel_window_steps)
  {
    return a_motion.interval_us;
  }

  ramp_span = a_axis.start_interval_us - a_motion.interval_us;
  return a_motion.interval_us + (uint32_t)(((uint64_t)ramp_span * (uint64_t)(a_axis.decel_window_steps - distance_to_edge)) / (uint64_t)a_axis.decel_window_steps);
}

void a_axis_motion_irq(void)
{
  uint32_t target_interval;

  if (a_motion.active == 0U || a_axis.enabled == 0U)
  {
    a_motion_halt();
    a_axis_mark_stopped();
    return;
  }

  if (a_motion.pulse_high_phase == 0U)
  {
    if (a_motion.stop_on_endstop != 0U)
    {
      if (a_motion.direction < 0 && a_min_endstop_triggered())
      {
        printf("axis a irq stop min dir %ld pos %ld\r\n", (long)a_motion.direction, (long)a_axis.position);
        a_motion_halt();
        a_axis_mark_stopped();
        return;
      }
      if (a_motion.direction > 0 && a_max_endstop_triggered())
      {
        printf("axis a irq stop max dir %ld pos %ld\r\n", (long)a_motion.direction, (long)a_axis.position);
        a_motion_halt();
        a_axis_mark_stopped();
        return;
      }
    }

    HAL_GPIO_WritePin(A_DIR_GPIO_Port, A_DIR_Pin, a_dir_level(a_motion.direction));
    HAL_GPIO_WritePin(A_STEP_GPIO_Port, A_STEP_Pin, GPIO_PIN_SET);
    a_motion.pulse_high_phase = 1U;
    a_motion_rearm(A_STEP_PULSE_HIGH_US);
    return;
  }

  HAL_GPIO_WritePin(A_STEP_GPIO_Port, A_STEP_Pin, GPIO_PIN_RESET);
  a_motion.pulse_high_phase = 0U;
  a_axis.position += a_motion.direction;
  a_motion.moved_steps++;

  if (a_motion.continuous != 0U)
  {
    target_interval = a_target_interval_for_position();
    if (a_motion.current_interval_us > target_interval)
    {
      uint32_t delta = a_motion.current_interval_us - target_interval;
      uint32_t next_interval = a_motion.current_interval_us - ((delta > a_axis.accel_interval_delta_us) ? a_axis.accel_interval_delta_us : delta);
      a_motion.current_interval_us = next_interval;
    }
    else if (a_motion.current_interval_us < target_interval)
    {
      uint32_t next_interval = a_motion.current_interval_us + a_axis.accel_interval_delta_us;
      a_motion.current_interval_us = (next_interval > target_interval) ? target_interval : next_interval;
    }
  }

  if (a_motion.continuous == 0U && a_motion.steps_remaining > 0U)
  {
    a_motion.steps_remaining--;
    if (a_motion.steps_remaining == 0U)
    {
      a_motion_halt();
      a_axis_mark_stopped();
      return;
    }
  }

  a_motion_rearm((a_motion.current_interval_us > A_STEP_PULSE_HIGH_US) ? (a_motion.current_interval_us - A_STEP_PULSE_HIGH_US) : 10U);
}

void a_axis_motion_init(void)
{
  a_axis_motion_stop_internal();
}

uint8_t a_axis_motion_active(void)
{
  return a_motion.active;
}

void a_axis_motion_set_enabled(uint8_t enabled)
{
  a_axis.enabled = enabled ? 1U : 0U;
  if (a_axis.enabled == 0U)
  {
    a_axis_motion_stop_internal();
  }
}

void a_axis_motion_set_travel(uint32_t travel_steps)
{
  if (travel_steps >= 100U)
  {
    a_axis.travel_steps = travel_steps;
  }
}

void a_axis_motion_set_decel_window(uint32_t decel_window_steps)
{
  if (decel_window_steps >= 10U)
  {
    a_axis.decel_window_steps = decel_window_steps;
  }
}

void a_axis_motion_set_cruise_interval(uint16_t cruise_interval_us)
{
  a_axis.cruise_interval_us = cruise_interval_us;
}

void a_axis_motion_set_homing_interval(uint16_t homing_interval_us)
{
  a_axis.homing_interval_us = homing_interval_us;
}

void a_axis_motion_set_start_interval(uint16_t start_interval_us)
{
  a_axis.start_interval_us = start_interval_us;
}

void a_axis_motion_set_accel_delta(uint16_t accel_interval_delta_us)
{
  a_axis.accel_interval_delta_us = accel_interval_delta_us;
}

void a_axis_motion_stop(void)
{
  a_axis_motion_stop_internal();
}

void a_axis_motion_home(void)
{
  if (a_axis.enabled == 0U)
  {
    printf("err axis a disabled\r\n");
    return;
  }

  a_motion_halt();
  a_axis_mark_stopped();
  a_axis.homed = 0U;
  a_axis.moving = 1U;
  a_axis.velocity = A_HOME_SEEK_DIRECTION;
  a_axis.target = (A_HOME_SEEK_DIRECTION > 0) ? INT32_MAX : INT32_MIN;
  a_axis.homing_state = 1U;
  a_sequence.state = A_SEQUENCE_HOME_SEEK;
  a_sequence.phase_start_position = a_axis.position;
  a_sequence.release_steps = 0U;
  a_sequence.measured_travel = 0U;
  printf("axis a home begin v2 seek_dir %ld release_dir %ld endstop_min %s endstop_max %s\r\n",
         (long)A_HOME_SEEK_DIRECTION,
         (long)A_HOME_RELEASE_DIRECTION,
         a_min_endstop_triggered() ? "trig" : "clear",
         a_max_endstop_triggered() ? "trig" : "clear");
  a_motion_start_continuous(A_HOME_SEEK_DIRECTION, a_axis.homing_interval_us);
}

void a_axis_motion_scan(void)
{
  if (a_axis.enabled == 0U)
  {
    printf("err axis a disabled\r\n");
    return;
  }

  a_motion_halt();
  a_axis_mark_stopped();
  a_axis.homed = 0U;
  a_axis.moving = 1U;
  a_axis.velocity = A_HOME_SEEK_DIRECTION;
  a_axis.target = (A_HOME_SEEK_DIRECTION > 0) ? INT32_MAX : INT32_MIN;
  a_axis.homing_state = 3U;
  a_sequence.state = A_SEQUENCE_SCAN_HOME_SEEK;
  a_sequence.phase_start_position = a_axis.position;
  a_sequence.release_steps = 0U;
  a_sequence.measured_travel = 0U;
  printf("axis a scan begin dir 1 endstop_min %s endstop_max %s pos %ld\r\n",
         a_min_endstop_triggered() ? "trig" : "clear",
         a_max_endstop_triggered() ? "trig" : "clear",
         (long)a_axis.position);
  a_motion_start_continuous(A_HOME_SEEK_DIRECTION, a_axis.homing_interval_us);
}

void a_axis_motion_jog(int32_t direction)
{
  if (direction == 0 || a_axis.enabled == 0U)
  {
    return;
  }
  a_axis.homing_state = 0U;
  a_axis.moving = 1U;
  a_axis.velocity = (direction < 0) ? -1 : 1;
  a_axis.target = (a_axis.velocity > 0) ? INT32_MAX : INT32_MIN;
  a_motion_start_continuous(a_axis.velocity, a_axis.cruise_interval_us);
}

void a_axis_motion_move_relative(int32_t delta)
{
  if (delta == 0 || a_axis.enabled == 0U)
  {
    return;
  }
  a_axis.homing_state = 0U;
  a_axis.moving = 1U;
  a_axis.velocity = (delta < 0) ? -1 : 1;
  a_axis.target = a_axis.position + delta;
  a_motion_start(a_axis.velocity, (uint32_t)((delta < 0) ? -delta : delta), a_axis.cruise_interval_us, 1U);
}

void a_axis_motion_goto(int32_t target)
{
  int32_t delta = target - a_axis.position;
  a_axis_motion_move_relative(delta);
}

void a_axis_motion_set_position(int32_t position, uint8_t homed)
{
  a_motion_halt();
  a_axis.position = position;
  a_axis.target = position;
  a_axis.velocity = 0;
  a_axis.moving = 0U;
  a_axis.homed = homed ? 1U : 0U;
  a_axis_motion_abort_sequence();
}

void a_axis_motion_get_snapshot(AAxisMotionSnapshot *snapshot)
{
  if (snapshot == NULL)
  {
    return;
  }

  snapshot->enabled = a_axis.enabled;
  snapshot->moving = a_axis.moving;
  snapshot->homed = a_axis.homed;
  snapshot->homing_state = a_axis.homing_state;
  snapshot->position = a_axis.position;
  snapshot->target = a_axis.target;
  snapshot->velocity = a_axis.velocity;
  snapshot->travel_steps = a_axis.travel_steps;
  snapshot->decel_window_steps = a_axis.decel_window_steps;
}

uint8_t a_axis_motion_min_endstop_triggered(void)
{
  return a_min_endstop_triggered();
}

uint8_t a_axis_motion_max_endstop_triggered(void)
{
  return a_max_endstop_triggered();
}

void a_axis_motion_update(void)
{
  switch (a_sequence.state)
  {
    case A_SEQUENCE_IDLE:
      return;

    case A_SEQUENCE_HOME_SEEK:
      if (a_motion.active != 0U)
      {
        if (a_position_delta_since_phase_start() >= A_HOME_SEEK_LIMIT_STEPS)
        {
          printf("err axis a home seek timeout\r\n");
          a_axis_motion_stop_internal();
        }
        return;
      }
      if (!a_min_endstop_triggered())
      {
        return;
      }
      a_axis.homing_state = 2U;
      a_sequence.state = A_SEQUENCE_HOME_RELEASE;
      a_sequence.release_steps = 0U;
      return;

    case A_SEQUENCE_HOME_RELEASE:
      if (a_motion.active != 0U)
      {
        return;
      }
      if (!a_min_endstop_triggered())
      {
        a_axis.position = 0;
        a_axis.homed = 1U;
        a_axis_mark_stopped();
        printf("ok axis a homed release_steps %lu\r\n", (unsigned long)a_sequence.release_steps);
        a_axis_motion_abort_sequence();
        return;
      }
      if (a_sequence.release_steps >= A_HOME_RELEASE_LIMIT)
      {
        printf("err axis a home release timeout\r\n");
        a_axis_motion_stop_internal();
        return;
      }
      a_axis.moving = 1U;
      a_axis.velocity = A_HOME_RELEASE_DIRECTION;
      a_axis.target = INT32_MAX;
      a_motion_start(A_HOME_RELEASE_DIRECTION, 1U, a_axis.homing_interval_us, 0U);
      a_sequence.release_steps++;
      return;

    case A_SEQUENCE_SCAN_HOME_SEEK:
      if (a_motion.active != 0U)
      {
        if (a_position_delta_since_phase_start() >= A_HOME_SEEK_LIMIT_STEPS)
        {
          printf("err axis a home seek timeout\r\n");
          a_axis_motion_stop_internal();
        }
        return;
      }
      if (!a_min_endstop_triggered())
      {
        return;
      }
      a_axis.homing_state = 4U;
      a_sequence.state = A_SEQUENCE_SCAN_HOME_RELEASE;
      a_sequence.release_steps = 0U;
      return;

    case A_SEQUENCE_SCAN_HOME_RELEASE:
      if (a_motion.active != 0U)
      {
        return;
      }
      if (!a_min_endstop_triggered())
      {
        a_axis.position = 0;
        a_axis.homing_state = 5U;
        a_axis.moving = 1U;
        a_axis.velocity = 1;
        a_axis.target = INT32_MAX;
        a_sequence.state = A_SEQUENCE_SCAN_SEEK_MAX;
        a_sequence.phase_start_position = a_axis.position;
        a_motion_start_continuous(1, A_SCAN_INTERVAL_US);
        return;
      }
      if (a_sequence.release_steps >= A_HOME_RELEASE_LIMIT)
      {
        printf("err axis a home release timeout\r\n");
        a_axis_motion_stop_internal();
        return;
      }
      a_axis.moving = 1U;
      a_axis.velocity = A_HOME_RELEASE_DIRECTION;
      a_axis.target = INT32_MAX;
      a_motion_start(A_HOME_RELEASE_DIRECTION, 1U, a_axis.homing_interval_us, 0U);
      a_sequence.release_steps++;
      return;

    case A_SEQUENCE_SCAN_SEEK_MAX:
      if (a_motion.active != 0U)
      {
        if (a_position_delta_since_phase_start() >= A_SCAN_MAX_LIMIT_STEPS)
        {
          printf("err axis a scan seek_max timeout\r\n");
          a_axis_motion_stop_internal();
        }
        return;
      }
      if (!a_max_endstop_triggered())
      {
        return;
      }
      a_sequence.measured_travel = (a_axis.position > 0) ? (uint32_t)a_axis.position : a_axis.travel_steps;
      a_axis.travel_steps = a_sequence.measured_travel;
      a_axis.homed = 0U;
      a_axis.homing_state = 3U;
      a_axis.moving = 1U;
      a_axis.velocity = A_HOME_SEEK_DIRECTION;
      a_axis.target = (A_HOME_SEEK_DIRECTION > 0) ? INT32_MAX : INT32_MIN;
      a_sequence.state = A_SEQUENCE_SCAN_RETURN_HOME_SEEK;
      a_sequence.phase_start_position = a_axis.position;
      a_motion_start_continuous(A_HOME_SEEK_DIRECTION, a_axis.homing_interval_us);
      return;

    case A_SEQUENCE_SCAN_RETURN_HOME_SEEK:
      if (a_motion.active != 0U)
      {
        if (a_position_delta_since_phase_start() >= A_HOME_SEEK_LIMIT_STEPS)
        {
          printf("err axis a home seek timeout\r\n");
          a_axis_motion_stop_internal();
        }
        return;
      }
      if (!a_min_endstop_triggered())
      {
        return;
      }
      a_axis.homing_state = 4U;
      a_sequence.state = A_SEQUENCE_SCAN_RETURN_HOME_RELEASE;
      a_sequence.release_steps = 0U;
      return;

    case A_SEQUENCE_SCAN_RETURN_HOME_RELEASE:
      if (a_motion.active != 0U)
      {
        return;
      }
      if (!a_min_endstop_triggered())
      {
        a_axis.position = 0;
        a_axis.homed = 1U;
        a_axis_mark_stopped();
        if (axis_travel_store_save_a(a_sequence.measured_travel) == 0U)
        {
          printf("err axis a travel save\r\n");
        }
        printf("ok axis a scan travel_steps %lu returned_home yes\r\n", (unsigned long)a_sequence.measured_travel);
        a_axis_motion_abort_sequence();
        return;
      }
      if (a_sequence.release_steps >= A_HOME_RELEASE_LIMIT)
      {
        printf("err axis a home release timeout\r\n");
        a_axis_motion_stop_internal();
        return;
      }
      a_axis.moving = 1U;
      a_axis.velocity = A_HOME_RELEASE_DIRECTION;
      a_axis.target = INT32_MAX;
      a_motion_start(A_HOME_RELEASE_DIRECTION, 1U, a_axis.homing_interval_us, 0U);
      a_sequence.release_steps++;
      return;
  }
}
