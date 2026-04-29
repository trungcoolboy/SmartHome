#include "main.h"
#include "servo_control.h"

#include <stdio.h>
#include <string.h>

static ServoState servos[] = {
  {.key = "fan1", .timer = &htim16, .channel = TIM_CHANNEL_1, .pca_channel = 0U, .min_angle_deg = 0U,  .max_angle_deg = 100U, .pulse_us = 1500U, .target_pulse_us = 1500U, .pwm_enabled = 1U},
  {.key = "fan2", .timer = &htim17, .channel = TIM_CHANNEL_1, .pca_channel = 1U, .min_angle_deg = 70U, .max_angle_deg = 175U, .pulse_us = 1500U, .target_pulse_us = 1500U, .pwm_enabled = 1U},
  {.key = "pan1", .timer = &htim8,  .channel = TIM_CHANNEL_1, .pca_channel = 2U, .min_angle_deg = 0U,  .max_angle_deg = 180U, .pulse_us = 1500U, .target_pulse_us = 1500U, .pwm_enabled = 1U},
  {.key = "pan2", .timer = &htim2,  .channel = TIM_CHANNEL_2, .pca_channel = 3U, .min_angle_deg = 50U, .max_angle_deg = 140U, .pulse_us = 1500U, .target_pulse_us = 1500U, .pwm_enabled = 1U},
  {.key = "lid",  .timer = &htim1,  .channel = TIM_CHANNEL_3, .pca_channel = 4U, .min_angle_deg = 0U,  .max_angle_deg = 180U, .pulse_us = 1500U, .target_pulse_us = 1500U, .pwm_enabled = 1U},
};

static uint8_t servo_ready = 0U;
static uint32_t servo_last_slew_ms = 0U;

#define SERVO_SLEW_INTERVAL_MS 10U
#define SERVO_SLEW_STEP_US     15U
#define SERVO_RELEASE_DELAY_MS 3000U

static uint16_t servo_clamp_angle(const ServoState *servo, uint16_t angle_deg)
{
  if (servo == NULL)
  {
    return angle_deg;
  }
  if (angle_deg < servo->min_angle_deg)
  {
    return servo->min_angle_deg;
  }
  if (angle_deg > servo->max_angle_deg)
  {
    return servo->max_angle_deg;
  }
  return angle_deg;
}

static uint16_t servo_clamp_pulse_us(const ServoState *servo, uint16_t pulse_us)
{
  uint16_t min_pulse;
  uint16_t max_pulse;

  if (servo == NULL)
  {
    return pulse_us;
  }

  min_pulse = servo_angle_to_us(servo->min_angle_deg);
  max_pulse = servo_angle_to_us(servo->max_angle_deg);

  if (pulse_us < min_pulse)
  {
    return min_pulse;
  }
  if (pulse_us > max_pulse)
  {
    return max_pulse;
  }
  return pulse_us;
}

static void servo_apply_pulse(ServoState *servo)
{
  if (servo == NULL)
  {
    return;
  }
  if (servo_ready == 0U)
  {
    return;
  }
  __HAL_TIM_SET_COMPARE(servo->timer, servo->channel, servo->pulse_us);
}

static void servo_enable_output(ServoState *servo)
{
  if (servo == NULL)
  {
    return;
  }
  servo->pwm_enabled = 1U;
  if (servo_ready != 0U)
  {
    __HAL_TIM_SET_COMPARE(servo->timer, servo->channel, servo->pulse_us);
  }
}

static void servo_disable_output(ServoState *servo)
{
  if (servo == NULL)
  {
    return;
  }
  servo->pwm_enabled = 0U;
  if (servo_ready != 0U)
  {
    __HAL_TIM_SET_COMPARE(servo->timer, servo->channel, 0U);
  }
}

void servo_init(void)
{
  size_t i;
  servo_ready = 1U;
  servo_last_slew_ms = HAL_GetTick();
  for (i = 0U; i < sizeof(servos) / sizeof(servos[0]); i++)
  {
    servo_apply_pulse(&servos[i]);
    servos[i].release_deadline_ms = HAL_GetTick() + SERVO_RELEASE_DELAY_MS;
  }
}

void servo_tick(void)
{
  size_t i;
  uint32_t now_ms;

  if (servo_ready == 0U)
  {
    return;
  }

  now_ms = HAL_GetTick();
  if ((uint32_t)(now_ms - servo_last_slew_ms) < SERVO_SLEW_INTERVAL_MS)
  {
    return;
  }
  servo_last_slew_ms = now_ms;

  for (i = 0U; i < sizeof(servos) / sizeof(servos[0]); i++)
  {
    ServoState *servo = &servos[i];
    uint16_t next_pulse_us = servo->pulse_us;

    if (servo->pulse_us == servo->target_pulse_us)
    {
      if (servo->pwm_enabled != 0U)
      {
        if (servo->release_deadline_ms == 0U)
        {
          servo->release_deadline_ms = now_ms + SERVO_RELEASE_DELAY_MS;
        }
        else if ((int32_t)(now_ms - servo->release_deadline_ms) >= 0)
        {
          servo_disable_output(servo);
          servo->release_deadline_ms = 0U;
        }
      }
      continue;
    }

    if (servo->pwm_enabled == 0U)
    {
      servo_enable_output(servo);
    }
    servo->release_deadline_ms = 0U;

    if (servo->pulse_us < servo->target_pulse_us)
    {
      uint16_t delta_us = (uint16_t)(servo->target_pulse_us - servo->pulse_us);
      next_pulse_us = servo->pulse_us + ((delta_us > SERVO_SLEW_STEP_US) ? SERVO_SLEW_STEP_US : delta_us);
    }
    else
    {
      uint16_t delta_us = (uint16_t)(servo->pulse_us - servo->target_pulse_us);
      next_pulse_us = servo->pulse_us - ((delta_us > SERVO_SLEW_STEP_US) ? SERVO_SLEW_STEP_US : delta_us);
    }

    servo->pulse_us = servo_clamp_pulse_us(servo, next_pulse_us);
    servo_apply_pulse(servo);
  }
}

ServoState *servo_find(const char *key)
{
  size_t i;
  for (i = 0U; i < sizeof(servos) / sizeof(servos[0]); i++)
  {
    if (strcmp(servos[i].key, key) == 0)
    {
      return &servos[i];
    }
  }
  return NULL;
}

uint16_t servo_angle_to_us(uint16_t angle_deg)
{
  if (angle_deg > 180U)
  {
    angle_deg = 180U;
  }
  return (uint16_t)(500U + ((uint32_t)angle_deg * 2000U) / 180U);
}

uint16_t servo_us_to_angle(uint16_t pulse_us)
{
  if (pulse_us <= 500U)
  {
    return 0U;
  }
  if (pulse_us >= 2500U)
  {
    return 180U;
  }
  return (uint16_t)((((uint32_t)(pulse_us - 500U) * 180U) + 1000U) / 2000U);
}

void servo_emit_all_states(void)
{
  size_t i;
  for (i = 0U; i < sizeof(servos) / sizeof(servos[0]); i++)
  {
    printf("servo %s us %u angle %u\r\n",
           servos[i].key,
           (unsigned)servos[i].pulse_us,
           (unsigned)servo_us_to_angle(servos[i].pulse_us));
  }
}

void servo_set_angle(ServoState *servo, uint16_t angle_deg)
{
  uint16_t target_pulse_us;

  if (servo == NULL)
  {
    return;
  }
  angle_deg = servo_clamp_angle(servo, angle_deg);
  target_pulse_us = servo_clamp_pulse_us(servo, servo_angle_to_us(angle_deg));
  servo->target_pulse_us = target_pulse_us;
  servo->release_deadline_ms = 0U;
  servo_enable_output(servo);
  if (servo_ready == 0U)
  {
    servo->pulse_us = target_pulse_us;
  }
}

void servo_set_pulse_us(ServoState *servo, uint16_t pulse_us)
{
  if (servo == NULL)
  {
    return;
  }
  servo->target_pulse_us = servo_clamp_pulse_us(servo, pulse_us);
  servo->release_deadline_ms = 0U;
  servo_enable_output(servo);
  if (servo_ready == 0U)
  {
    servo->pulse_us = servo->target_pulse_us;
  }
}

void servo_set_all_angle(uint16_t angle_deg)
{
  size_t i;
  for (i = 0U; i < sizeof(servos) / sizeof(servos[0]); i++)
  {
    servo_set_angle(&servos[i], angle_deg);
  }
}

void servo_set_all_pulse_us(uint16_t pulse_us)
{
  size_t i;
  for (i = 0U; i < sizeof(servos) / sizeof(servos[0]); i++)
  {
    servo_set_pulse_us(&servos[i], pulse_us);
  }
}
