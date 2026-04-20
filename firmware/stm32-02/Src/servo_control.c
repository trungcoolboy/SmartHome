#include "main.h"
#include "servo_control.h"

#include <stdio.h>
#include <string.h>

static ServoState servos[] = {
  {.key = "fan1", .timer = &htim16, .channel = TIM_CHANNEL_1, .pca_channel = 0U, .min_angle_deg = 0U,  .max_angle_deg = 100U, .pulse_us = 1500U},
  {.key = "fan2", .timer = &htim17, .channel = TIM_CHANNEL_1, .pca_channel = 1U, .min_angle_deg = 70U, .max_angle_deg = 175U, .pulse_us = 1500U},
  {.key = "pan1", .timer = &htim8,  .channel = TIM_CHANNEL_1, .pca_channel = 2U, .min_angle_deg = 0U,  .max_angle_deg = 180U, .pulse_us = 1500U},
  {.key = "pan2", .timer = &htim2,  .channel = TIM_CHANNEL_2, .pca_channel = 3U, .min_angle_deg = 50U, .max_angle_deg = 140U, .pulse_us = 1500U},
  {.key = "lid",  .timer = &htim1,  .channel = TIM_CHANNEL_3, .pca_channel = 4U, .min_angle_deg = 0U,  .max_angle_deg = 180U, .pulse_us = 1500U},
};

static uint8_t servo_ready = 0U;

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

void servo_init(void)
{
  size_t i;
  servo_ready = 1U;
  for (i = 0U; i < sizeof(servos) / sizeof(servos[0]); i++)
  {
    servo_apply_pulse(&servos[i]);
  }
}

void servo_tick(void)
{
  /* Hardware PWM keeps running; nothing to do in the main loop. */
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
  if (servo == NULL)
  {
    return;
  }
  angle_deg = servo_clamp_angle(servo, angle_deg);
  servo->pulse_us = servo_angle_to_us(angle_deg);
  servo_apply_pulse(servo);
}

void servo_set_pulse_us(ServoState *servo, uint16_t pulse_us)
{
  if (servo == NULL)
  {
    return;
  }
  servo->pulse_us = servo_clamp_pulse_us(servo, pulse_us);
  servo_apply_pulse(servo);
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
