#pragma once

/* Minimal platform surface needed to compile eeprom_he.c on the host. */
#define I2C1_SCL_PIN 0
#define I2C1_SDA_PIN 1
#define PAL_MODE_INPUT 0
#define PAL_MODE_ALTERNATE(mode) (mode)
#define PAL_OUTPUT_TYPE_OPENDRAIN 0

static inline void palSetLineMode(int pin, int mode) {
    (void)pin;
    (void)mode;
}

static inline void chThdSleepMilliseconds(int ms) {
    (void)ms;
}
