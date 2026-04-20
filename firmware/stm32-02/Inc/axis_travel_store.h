#ifndef AXIS_TRAVEL_STORE_H
#define AXIS_TRAVEL_STORE_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void axis_travel_store_init(uint32_t default_a_travel_steps, uint32_t default_b_travel_steps);
uint8_t axis_travel_store_get(uint32_t *a_travel_steps, uint32_t *b_travel_steps);
uint8_t axis_travel_store_save_a(uint32_t a_travel_steps);
uint8_t axis_travel_store_save_b(uint32_t b_travel_steps);

#ifdef __cplusplus
}
#endif

#endif
