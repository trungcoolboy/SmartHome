#include "main.h"
#include "fan_control.h"

#include <stdio.h>
#include <string.h>

static IntelFanState intel_fans[] = {
  {.key = "fan1", .pwm_channel = TIM_CHANNEL_1, .tach_port = INTEL_FAN1_TACH_GPIO_Port, .tach_pin = INTEL_FAN1_TACH_Pin, .pwm_percent = 0U},
  {.key = "fan2", .pwm_channel = TIM_CHANNEL_3, .tach_port = INTEL_FAN2_TACH_GPIO_Port, .tach_pin = INTEL_FAN2_TACH_Pin, .pwm_percent = 0U},
};

typedef struct
{
  uint8_t pwm_percent;
} MagnetState;

static MagnetState magnet = {
  .pwm_percent = 0U,
};

static FanPowerRelayState fan_power_relays[] = {
  {.key = "fan1", .port = FAN1_POWER_RELAY_GPIO_Port, .pin = FAN1_POWER_RELAY_Pin, .active_high = 1U, .on = 0U},
  {.key = "fan2", .port = FAN2_POWER_RELAY_GPIO_Port, .pin = FAN2_POWER_RELAY_Pin, .active_high = 1U, .on = 0U},
};

IntelFanState *fan_find(const char *key)
{
  size_t i;
  for (i = 0U; i < sizeof(intel_fans) / sizeof(intel_fans[0]); i++)
  {
    if (strcmp(intel_fans[i].key, key) == 0)
    {
      return &intel_fans[i];
    }
  }
  return NULL;
}

static void fan_apply_pwm(IntelFanState *fan)
{
  uint32_t compare = ((__HAL_TIM_GET_AUTORELOAD(&htim3) + 1U) * (uint32_t)fan->pwm_percent) / 100U;
  __HAL_TIM_SET_COMPARE(&htim3, fan->pwm_channel, compare);
}

void fan_set_pwm(IntelFanState *fan, uint8_t pwm_percent)
{
  if (pwm_percent > 100U)
  {
    pwm_percent = 100U;
  }
  fan->pwm_percent = pwm_percent;
  fan_apply_pwm(fan);
}

void fan_set_all_pwm(uint8_t pwm_percent)
{
  size_t i;
  for (i = 0U; i < sizeof(intel_fans) / sizeof(intel_fans[0]); i++)
  {
    fan_set_pwm(&intel_fans[i], pwm_percent);
  }
}

void fan_emit_all_states(void)
{
  size_t i;
  for (i = 0U; i < sizeof(intel_fans) / sizeof(intel_fans[0]); i++)
  {
    printf("fan %s pwm %u rpm %lu tach_edges %lu\r\n",
           intel_fans[i].key,
           (unsigned)intel_fans[i].pwm_percent,
           (unsigned long)intel_fans[i].rpm,
           (unsigned long)intel_fans[i].tach_edges_total);
  }
}

void fan_apply_initial_pwm(void)
{
  fan_set_all_pwm(0U);
}

static void magnet_apply_pwm(void)
{
  uint32_t compare = ((__HAL_TIM_GET_AUTORELOAD(&htim4) + 1U) * (uint32_t)magnet.pwm_percent) / 100U;
  __HAL_TIM_SET_COMPARE(&htim4, TIM_CHANNEL_1, compare);
}

void magnet_set_pwm(uint8_t pwm_percent)
{
  if (pwm_percent > 100U)
  {
    pwm_percent = 100U;
  }
  magnet.pwm_percent = pwm_percent;
  magnet_apply_pwm();
}

void magnet_emit_state(void)
{
  printf("magnet pwm %u\r\n", (unsigned)magnet.pwm_percent);
}

FanPowerRelayState *fan_power_relay_find(const char *key)
{
  size_t i;
  for (i = 0U; i < sizeof(fan_power_relays) / sizeof(fan_power_relays[0]); i++)
  {
    if (strcmp(fan_power_relays[i].key, key) == 0)
    {
      return &fan_power_relays[i];
    }
  }
  return NULL;
}

void fan_power_relay_apply(FanPowerRelayState *relay)
{
  GPIO_PinState level = relay->on
                        ? (relay->active_high ? GPIO_PIN_SET : GPIO_PIN_RESET)
                        : (relay->active_high ? GPIO_PIN_RESET : GPIO_PIN_SET);
  HAL_GPIO_WritePin(relay->port, relay->pin, level);
}

void fan_power_relay_apply_all(void)
{
  size_t i;
  for (i = 0U; i < sizeof(fan_power_relays) / sizeof(fan_power_relays[0]); i++)
  {
    fan_power_relay_apply(&fan_power_relays[i]);
  }
}

void fan_power_relay_emit_all_states(void)
{
  size_t i;
  for (i = 0U; i < sizeof(fan_power_relays) / sizeof(fan_power_relays[0]); i++)
  {
    printf("fanpwr %s %s active_%s\r\n",
           fan_power_relays[i].key,
           fan_power_relays[i].on ? "on" : "off",
           fan_power_relays[i].active_high ? "high" : "low");
  }
}
