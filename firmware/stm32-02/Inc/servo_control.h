#ifndef SERVO_CONTROL_H
#define SERVO_CONTROL_H

#include "stm32g4xx_hal.h"
#include <stdint.h>

typedef struct
{
  const char *key;
  TIM_HandleTypeDef *timer;
  uint32_t channel;
  uint8_t pca_channel;
  uint16_t min_angle_deg;
  uint16_t max_angle_deg;
  uint16_t pulse_us;
} ServoState;

void servo_init(void);
void servo_tick(void);
ServoState *servo_find(const char *key);
uint16_t servo_angle_to_us(uint16_t angle_deg);
uint16_t servo_us_to_angle(uint16_t pulse_us);
void servo_emit_all_states(void);
void servo_set_angle(ServoState *servo, uint16_t angle_deg);
void servo_set_pulse_us(ServoState *servo, uint16_t pulse_us);
void servo_set_all_angle(uint16_t angle_deg);
void servo_set_all_pulse_us(uint16_t pulse_us);

#endif
