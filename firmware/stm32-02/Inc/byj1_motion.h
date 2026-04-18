#ifndef BYJ1_MOTION_H
#define BYJ1_MOTION_H

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
} Byj1MotionSnapshot;

void byj1_motion_init(void);
void byj1_motion_tick(void);

void byj1_motion_set_enabled(uint8_t enabled);
void byj1_motion_stop(void);
void byj1_motion_home(void);
void byj1_motion_jog(int32_t direction);
void byj1_motion_move_relative(int32_t delta);
void byj1_motion_goto(int32_t target);
uint8_t byj1_motion_endstop_triggered(void);
void byj1_motion_get_snapshot(Byj1MotionSnapshot *snapshot);

#ifdef __cplusplus
}
#endif

#endif
