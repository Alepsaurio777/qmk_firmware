#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "analog_matrix.h"

#if ANALOG_CONFIDENT_BOTTOM_OUT_ENABLE

STATIC_ASSERT(ANALOG_CONFIDENT_BOTTOM_OUT_SAMPLES >= 5, "bottom-out confidence necesita al menos cinco muestras");
STATIC_ASSERT((ANALOG_CONFIDENT_BOTTOM_OUT_SAMPLES & 1) == 1, "bottom-out confidence necesita un numero impar de muestras");
STATIC_ASSERT(ANALOG_CONFIDENT_BOTTOM_OUT_DEEP_TRAVEL > ANALOG_CONFIDENT_BOTTOM_OUT_RELEASE_TRAVEL, "los umbrales deep/release estan invertidos");

typedef struct {
    uint16_t samples[ANALOG_CONFIDENT_BOTTOM_OUT_SAMPLES];
    uint16_t deepest_raw;
    uint16_t candidate_full;
    uint16_t sample_min;
    uint16_t sample_max;
    uint16_t completed_presses;
    uint16_t rejected_windows;
    uint8_t  sample_count;
    bool     tracking;
    bool     ready;
} bottom_out_confidence_t;

typedef struct {
    uint16_t baseline_full;
    uint16_t active_full;
    uint16_t candidate_full;
    uint16_t sample_min;
    uint16_t sample_max;
    uint16_t completed_presses;
    uint16_t rejected_windows;
    uint8_t  sample_count;
    bool     initialized;
    bool     ready;
    bool     applied;
} analog_confident_bottom_status_t;

void bottom_out_confidence_reset(bottom_out_confidence_t *state);

// Devuelve true al terminar una pulsacion que alcanzo la zona de fondo. Una
// pulsacion sostenida produce una sola muestra, no una muestra por scan.
bool bottom_out_confidence_observe(bottom_out_confidence_t *state, uint16_t raw, uint8_t travel, uint16_t baseline_full);

// Integracion con la calibracion del analog matrix. Apply es deliberadamente
// volatil: cambia calib_values, pero nunca saved_calib_values ni EEPROM.
bool    analog_matrix_confident_bottom_status(uint8_t row, uint8_t col, analog_confident_bottom_status_t *out);
bool    analog_matrix_confident_bottom_apply_key(uint8_t row, uint8_t col);
uint8_t analog_matrix_confident_bottom_revert(void);
void    analog_matrix_confident_bottom_clear(void);

#endif
