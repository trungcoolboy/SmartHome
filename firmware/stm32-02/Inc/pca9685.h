#ifndef PCA9685_H
#define PCA9685_H

#include "stm32g4xx_hal.h"
#include <stdint.h>

#define PCA9685_I2C_ADDR        (0x40U << 1)
#define PCA9685_SERVO_FREQ_HZ   50U

HAL_StatusTypeDef pca9685_init(void);
HAL_StatusTypeDef pca9685_set_pulse_us(uint8_t channel, uint16_t pulse_us);

#endif
