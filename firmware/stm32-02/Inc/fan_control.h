#ifndef FAN_CONTROL_H
#define FAN_CONTROL_H

#include "stm32g4xx_hal.h"
#include <stdint.h>

typedef struct
{
  const char *key;
  uint32_t pwm_channel;
  GPIO_TypeDef *tach_port;
  uint16_t tach_pin;
  uint8_t pwm_percent;
  uint8_t tach_last_level;
  uint32_t tach_edges_total;
  uint32_t tach_edges_sample;
  uint32_t rpm;
} IntelFanState;

typedef struct
{
  const char *key;
  GPIO_TypeDef *port;
  uint16_t pin;
  uint8_t active_high;
  uint8_t on;
} FanPowerRelayState;

IntelFanState *fan_find(const char *key);
void fan_set_pwm(IntelFanState *fan, uint8_t pwm_percent);
void fan_set_all_pwm(uint8_t pwm_percent);
void fan_emit_all_states(void);
void fan_apply_initial_pwm(void);

FanPowerRelayState *fan_power_relay_find(const char *key);
void fan_power_relay_apply(FanPowerRelayState *relay);
void fan_power_relay_apply_all(void);
void fan_power_relay_emit_all_states(void);

void magnet_set_pwm(uint8_t pwm_percent);
void magnet_emit_state(void);

#endif
