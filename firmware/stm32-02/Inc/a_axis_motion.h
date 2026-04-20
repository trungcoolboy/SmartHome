#ifndef A_AXIS_MOTION_H
#define A_AXIS_MOTION_H

#include "main.h"

#ifdef __cplusplus
extern "C" {
#endif

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
} AAxisMotionSnapshot;

void a_axis_motion_init(void);
void a_axis_motion_irq(void);
void a_axis_motion_update(void);
uint8_t a_axis_motion_active(void);

void a_axis_motion_set_enabled(uint8_t enabled);
void a_axis_motion_set_travel(uint32_t travel_steps);
void a_axis_motion_set_decel_window(uint32_t decel_window_steps);
void a_axis_motion_set_cruise_interval(uint16_t cruise_interval_us);
void a_axis_motion_set_homing_interval(uint16_t homing_interval_us);
void a_axis_motion_set_start_interval(uint16_t start_interval_us);
void a_axis_motion_set_accel_delta(uint16_t accel_interval_delta_us);

void a_axis_motion_stop(void);
void a_axis_motion_home(void);
void a_axis_motion_scan(void);
void a_axis_motion_jog(int32_t direction);
void a_axis_motion_move_relative(int32_t delta);
void a_axis_motion_goto(int32_t target);
void a_axis_motion_set_position(int32_t position, uint8_t homed);

void a_axis_motion_get_snapshot(AAxisMotionSnapshot *snapshot);
uint8_t a_axis_motion_min_endstop_triggered(void);
uint8_t a_axis_motion_max_endstop_triggered(void);

#ifdef __cplusplus
}
#endif

#endif
