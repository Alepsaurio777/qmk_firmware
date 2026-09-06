#pragma once

#include <stdint.h>

typedef int16_t i2c_status_t;

#define I2C_STATUS_SUCCESS 0
#define I2C_STATUS_ERROR (-1)
#define I2C_STATUS_TIMEOUT (-2)

void           i2c_init(void);
i2c_status_t   i2c_transmit(uint8_t address, const uint8_t *data, uint16_t length, uint16_t timeout);
i2c_status_t   i2c_receive(uint8_t address, uint8_t *data, uint16_t length, uint16_t timeout);
i2c_status_t   i2c_ping_address(uint8_t address, uint16_t timeout);
