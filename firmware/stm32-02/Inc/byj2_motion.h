#ifndef BYJ2_MOTION_H
#define BYJ2_MOTION_H

#include "main.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct
{
  uint8_t enabled;
  uint8_t moving;
  uint8_t homed;
  int32_t position;
  int32_t target;
  int32_t velocity;
} Byj2MotionSnapshot;

void byj2_motion_init(void);
void byj2_motion_tick(void);

void byj2_motion_set_enabled(uint8_t enabled);
void byj2_motion_stop(void);
void byj2_motion_home(void);
void byj2_motion_jog(int32_t direction);
void byj2_motion_move_relative(int32_t delta);
void byj2_motion_goto(int32_t target);
uint8_t byj2_motion_endstop_triggered(void);
void byj2_motion_get_snapshot(Byj2MotionSnapshot *snapshot);

#ifdef __cplusplus
}
#endif

#endif
