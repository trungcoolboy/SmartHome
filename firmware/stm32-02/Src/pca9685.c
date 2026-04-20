#include "main.h"
#include "pca9685.h"

#define PCA9685_MODE1_REG            0x00U
#define PCA9685_MODE2_REG            0x01U
#define PCA9685_PRESCALE_REG         0xFEU
#define PCA9685_LED0_ON_L_REG        0x06U

#define PCA9685_MODE1_SLEEP          0x10U
#define PCA9685_MODE1_AI             0x20U
#define PCA9685_MODE1_RESTART        0x80U
#define PCA9685_MODE2_OUTDRV         0x04U

static HAL_StatusTypeDef pca9685_write_reg(uint8_t reg, uint8_t value)
{
  uint8_t packet[2] = {reg, value};
  return HAL_I2C_Master_Transmit(&hi2c1, PCA9685_I2C_ADDR, packet, sizeof(packet), 50U);
}

static HAL_StatusTypeDef pca9685_write_regs(uint8_t reg, const uint8_t *data, uint16_t len)
{
  uint8_t packet[5];

  if (len > 4U)
  {
    return HAL_ERROR;
  }

  packet[0] = reg;
  for (uint16_t i = 0U; i < len; i++)
  {
    packet[i + 1U] = data[i];
  }

  return HAL_I2C_Master_Transmit(&hi2c1, PCA9685_I2C_ADDR, packet, (uint16_t)(len + 1U), 50U);
}

HAL_StatusTypeDef pca9685_init(void)
{
  uint8_t prescale = 121U;

  if (pca9685_write_reg(PCA9685_MODE1_REG, PCA9685_MODE1_SLEEP) != HAL_OK)
  {
    return HAL_ERROR;
  }
  if (pca9685_write_reg(PCA9685_PRESCALE_REG, prescale) != HAL_OK)
  {
    return HAL_ERROR;
  }
  if (pca9685_write_reg(PCA9685_MODE2_REG, PCA9685_MODE2_OUTDRV) != HAL_OK)
  {
    return HAL_ERROR;
  }
  if (pca9685_write_reg(PCA9685_MODE1_REG, PCA9685_MODE1_AI) != HAL_OK)
  {
    return HAL_ERROR;
  }
  HAL_Delay(1U);
  if (pca9685_write_reg(PCA9685_MODE1_REG, (uint8_t)(PCA9685_MODE1_AI | PCA9685_MODE1_RESTART)) != HAL_OK)
  {
    return HAL_ERROR;
  }

  return HAL_OK;
}

HAL_StatusTypeDef pca9685_set_pulse_us(uint8_t channel, uint16_t pulse_us)
{
  uint32_t off_count;
  uint8_t reg;
  uint8_t payload[4];

  if (channel > 15U)
  {
    return HAL_ERROR;
  }

  if (pulse_us < 500U)
  {
    pulse_us = 500U;
  }
  if (pulse_us > 2500U)
  {
    pulse_us = 2500U;
  }

  off_count = (((uint32_t)pulse_us * 4096U) + 10000U) / 20000U;
  if (off_count > 4095U)
  {
    off_count = 4095U;
  }

  reg = (uint8_t)(PCA9685_LED0_ON_L_REG + (4U * channel));
  payload[0] = 0x00U;
  payload[1] = 0x00U;
  payload[2] = (uint8_t)(off_count & 0xFFU);
  payload[3] = (uint8_t)((off_count >> 8) & 0x0FU);

  return pca9685_write_regs(reg, payload, sizeof(payload));
}
