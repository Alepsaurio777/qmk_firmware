/* Copyright 2024 @ Keychron (https://www.keychron.com)
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "action_layer.h"
#include "compiler_support.h"
#include "keycodes.h"
#include "matrix.h"
#include "analog_matrix_eeconfig.h"
#include "analog_matrix_type.h"

#define FULL_TRAVEL_UNIT 40

#ifndef DEFAULT_ACTUATION_POINT
#    define DEFAULT_ACTUATION_POINT 20
#endif

#ifndef DEFAULT_RAPID_TRIGGER_SENSITIVITY
#    define DEFAULT_RAPID_TRIGGER_SENSITIVITY 4
#endif

#ifndef DEFAULT_ZERO_TRAVEL_VALUE
#    define DEFAULT_ZERO_TRAVEL_VALUE 3000
#endif

#ifndef DEFAULT_FULL_RANGE
#    define DEFAULT_FULL_RANGE 900
#endif

#define DEFAULT_FULL_TRAVEL_VALUE (DEFAULT_ZERO_TRAVEL_VALUE - DEFAULT_FULL_RANGE)

#ifndef VALID_ANALOG_RAW_VALUE_MIN
#    define VALID_ANALOG_RAW_VALUE_MIN 1200
#endif

#ifndef VALID_ANALOG_RAW_VALUE_MAX
#    define VALID_ANALOG_RAW_VALUE_MAX 3500
#endif

#ifndef STATIC_HYSTERESIS
#    define STATIC_HYSTERESIS 5
#endif

#ifndef STATIC_HYSTERESIS_GAMING
#    define STATIC_HYSTERESIS_GAMING STATIC_HYSTERESIS
#endif

#ifndef STATIC_HYSTERESIS_TYPING
#    define STATIC_HYSTERESIS_TYPING STATIC_HYSTERESIS
#endif

#ifndef STATIC_HYSTERESIS_GAMING_FAST_KEY
#    define STATIC_HYSTERESIS_GAMING_FAST_KEY STATIC_HYSTERESIS_GAMING
#endif

#ifndef ANALOG_ADAPTIVE_SHALLOW_HYSTERESIS_GAMING
#    define ANALOG_ADAPTIVE_SHALLOW_HYSTERESIS_GAMING 0
#endif

#ifndef ANALOG_GAMING_FAST_KEY_ROW
#    define ANALOG_GAMING_FAST_KEY_ROW 0xFF
#endif

#ifndef ANALOG_GAMING_FAST_KEY_COL
#    define ANALOG_GAMING_FAST_KEY_COL 0xFF
#endif

#ifndef ANALOG_CONTINUOUS_RT_KEY1_ROW
#    define ANALOG_CONTINUOUS_RT_KEY1_ROW ANALOG_GAMING_FAST_KEY_ROW
#endif

#ifndef ANALOG_CONTINUOUS_RT_KEY1_COL
#    define ANALOG_CONTINUOUS_RT_KEY1_COL ANALOG_GAMING_FAST_KEY_COL
#endif

#ifndef ANALOG_CONTINUOUS_RT_KEY2_ROW
#    define ANALOG_CONTINUOUS_RT_KEY2_ROW 0xFF
#endif

#ifndef ANALOG_CONTINUOUS_RT_KEY2_COL
#    define ANALOG_CONTINUOUS_RT_KEY2_COL 0xFF
#endif

#ifndef RAPID_TRIGGER_TICK
#    define RAPID_TRIGGER_TICK 10
#endif

#ifndef MIN_ACTUATION
#    define MIN_ACTUATION 5
#endif

#ifndef ZERO_TRAVEL_DEAD_ZONE
#    define ZERO_TRAVEL_DEAD_ZONE 20
#endif

#ifndef TOP_OUT_DEAD_ZONE_GAMING
#    define TOP_OUT_DEAD_ZONE_GAMING 0
#endif

#ifndef TOP_OUT_DEAD_ZONE_TYPING
#    define TOP_OUT_DEAD_ZONE_TYPING 0
#endif

#ifndef ANALOG_RAW_NOISE_FILTER_GAMING
#    define ANALOG_RAW_NOISE_FILTER_GAMING 5
#endif

#ifndef ANALOG_RAW_NOISE_FILTER_TYPING
#    define ANALOG_RAW_NOISE_FILTER_TYPING 5
#endif

#ifndef ANALOG_GAMING_LAYERS_MASK
#    define ANALOG_GAMING_LAYERS_MASK ((layer_state_t)0x03)
#endif

static inline bool analog_matrix_is_gaming_mode(void) {
    // SOLO la capa default (= interruptor fisico). Incluir layer_state aqui
    // reabria un escape del lockdown: con Fn sostenido durante el cambio de
    // interruptor, el bit de la capa Fn mantenia esto en falso ya en Gaming.
    return (default_layer_state & ~ANALOG_GAMING_LAYERS_MASK) == 0;
}

#ifndef BOTTOM_DEAD_ZONE
#    define BOTTOM_DEAD_ZONE 38
#endif

#ifndef BOTTOM_JITTER
#    define BOTTOM_JITTER 80
#endif

#define TRAVEL_SCALE 6

#ifndef ANALOG_DEBOUNCE_TIME
#    define ANALOG_DEBOUNCE_TIME 3
#endif

#ifndef ANALOG_FIXED_POINT_TRAVEL
#    define ANALOG_FIXED_POINT_TRAVEL 0
#endif

#ifndef ANALOG_AUTO_CALIBRATION_ENABLE
#    define ANALOG_AUTO_CALIBRATION_ENABLE 1
#endif

/* Learn per-key bottom-out from real usage, outside the scan hot path.
 * The learned full-travel only ever deepens (never shrinks), so the dynamic
 * range cannot degrade on its own. Runs in analog_matrix_task(). */
#ifndef ANALOG_BOTTOM_OUT_LEARN
#    define ANALOG_BOTTOM_OUT_LEARN 0
#endif

/* Minimum improvement (raw ADC counts) before committing a learned bottom-out. */
#ifndef ANALOG_BOTTOM_OUT_LEARN_EPSILON
#    define ANALOG_BOTTOM_OUT_LEARN_EPSILON 30
#endif

/* Depth-compare SOCD (Rappy Snappy): a challenger key must be deeper than the
 * current winner by this many travel units (TRAVEL_SCALE units; 6 = 0.1 mm)
 * to take over. Without it, sensor noise at near-equal depths flips the
 * winner every scan (A/D chatter at scan rate). */
#ifndef ANALOG_SOCD_DEEPER_HYSTERESIS
#    define ANALOG_SOCD_DEEPER_HYSTERESIS 6
#endif

/* SOCD "ambas a fondo": si ambas teclas del par superan este travel, se
 * registran las dos. Derivado del travel maximo (~94%) para no romperse si la
 * escala cambia. Antes era 230 fijo, valido solo porque el maximo actual es
 * (FULL_TRAVEL_UNIT+1)*TRAVEL_SCALE-1 = 245; esta formula da 230 hoy y se
 * adapta si FULL_TRAVEL_UNIT o TRAVEL_SCALE cambian. */
#ifndef ANALOG_SOCD_BOTTOM_OUT_THRESHOLD
#    define ANALOG_SOCD_BOTTOM_OUT_THRESHOLD ((((FULL_TRAVEL_UNIT + 1) * TRAVEL_SCALE) - 1) * 94 / 100)
#endif

#ifndef ANALOG_DISABLE_OKMC_IN_GAMING_MODE
#    define ANALOG_DISABLE_OKMC_IN_GAMING_MODE 0
#endif

#ifndef ANALOG_DISABLE_TOGGLE_IN_GAMING_MODE
#    define ANALOG_DISABLE_TOGGLE_IN_GAMING_MODE 0
#endif

#ifndef ANALOG_DISABLE_GAMEPAD_IN_GAMING_MODE
#    define ANALOG_DISABLE_GAMEPAD_IN_GAMING_MODE 0
#endif

#ifndef ANALOG_DISABLE_SOCD_IN_GAMING_MODE
#    define ANALOG_DISABLE_SOCD_IN_GAMING_MODE 0
#endif

#ifndef ANALOG_DISABLE_PROFILE_COMBO_IN_GAMING_MODE
#    define ANALOG_DISABLE_PROFILE_COMBO_IN_GAMING_MODE 0
#endif

#ifndef ANALOG_CONTINUOUS_RAPID_TRIGGER_IN_GAMING_MODE
#    define ANALOG_CONTINUOUS_RAPID_TRIGGER_IN_GAMING_MODE 0
#endif

#ifndef ANALOG_CONTINUOUS_RT_REPRESS_MAX_TRAVEL
#    define ANALOG_CONTINUOUS_RT_REPRESS_MAX_TRAVEL ((FULL_TRAVEL_UNIT + 1) * TRAVEL_SCALE - 1)
#endif

#ifndef ANALOG_PREDICTIVE_ACTUATION_IN_GAMING_MODE
#    define ANALOG_PREDICTIVE_ACTUATION_IN_GAMING_MODE 0
#endif

#ifndef ANALOG_PREDICTIVE_REGULAR_IN_GAMING_MODE
#    define ANALOG_PREDICTIVE_REGULAR_IN_GAMING_MODE 0
#endif

#ifndef ANALOG_PREDICTIVE_REGULAR_FORCE_MODE_IN_GAMING
#    define ANALOG_PREDICTIVE_REGULAR_FORCE_MODE_IN_GAMING 0
#endif

#ifndef ANALOG_GAMING_DEFAULT_RAPID_PROFILE
#    define ANALOG_GAMING_DEFAULT_RAPID_PROFILE 0xFF
#endif

#ifndef ANALOG_GAMING_DEFAULT_REGULAR_PROFILE
#    define ANALOG_GAMING_DEFAULT_REGULAR_PROFILE 0xFF
#endif

#ifndef ANALOG_PREDICTIVE_RT_KEY1_ROW
#    define ANALOG_PREDICTIVE_RT_KEY1_ROW ANALOG_CONTINUOUS_RT_KEY1_ROW
#endif

#ifndef ANALOG_PREDICTIVE_RT_KEY1_COL
#    define ANALOG_PREDICTIVE_RT_KEY1_COL ANALOG_CONTINUOUS_RT_KEY1_COL
#endif

#ifndef ANALOG_PREDICTIVE_RT_KEY2_ROW
#    define ANALOG_PREDICTIVE_RT_KEY2_ROW ANALOG_CONTINUOUS_RT_KEY2_ROW
#endif

#ifndef ANALOG_PREDICTIVE_RT_KEY2_COL
#    define ANALOG_PREDICTIVE_RT_KEY2_COL ANALOG_CONTINUOUS_RT_KEY2_COL
#endif

#ifndef ANALOG_PREDICTIVE_RT_KEY3_ROW
#    define ANALOG_PREDICTIVE_RT_KEY3_ROW 0xFF
#endif

#ifndef ANALOG_PREDICTIVE_RT_KEY3_COL
#    define ANALOG_PREDICTIVE_RT_KEY3_COL 0xFF
#endif

#ifndef ANALOG_PREDICTIVE_RT_KEY4_ROW
#    define ANALOG_PREDICTIVE_RT_KEY4_ROW 0xFF
#endif

#ifndef ANALOG_PREDICTIVE_RT_KEY4_COL
#    define ANALOG_PREDICTIVE_RT_KEY4_COL 0xFF
#endif

#ifndef ANALOG_PREDICTIVE_RT_KEY5_ROW
#    define ANALOG_PREDICTIVE_RT_KEY5_ROW 0xFF
#endif

#ifndef ANALOG_PREDICTIVE_RT_KEY5_COL
#    define ANALOG_PREDICTIVE_RT_KEY5_COL 0xFF
#endif

#ifndef ANALOG_PREDICTIVE_RT_KEY6_ROW
#    define ANALOG_PREDICTIVE_RT_KEY6_ROW 0xFF
#endif

#ifndef ANALOG_PREDICTIVE_RT_KEY6_COL
#    define ANALOG_PREDICTIVE_RT_KEY6_COL 0xFF
#endif

#ifndef ANALOG_PREDICTIVE_REGULAR_KEY1_ROW
#    define ANALOG_PREDICTIVE_REGULAR_KEY1_ROW 0xFF
#endif

#ifndef ANALOG_PREDICTIVE_REGULAR_KEY1_COL
#    define ANALOG_PREDICTIVE_REGULAR_KEY1_COL 0xFF
#endif

#ifndef ANALOG_PREDICTIVE_REGULAR_KEY2_ROW
#    define ANALOG_PREDICTIVE_REGULAR_KEY2_ROW 0xFF
#endif

#ifndef ANALOG_PREDICTIVE_REGULAR_KEY2_COL
#    define ANALOG_PREDICTIVE_REGULAR_KEY2_COL 0xFF
#endif

#ifndef ANALOG_PREDICTIVE_REGULAR_KEY3_ROW
#    define ANALOG_PREDICTIVE_REGULAR_KEY3_ROW 0xFF
#endif

#ifndef ANALOG_PREDICTIVE_REGULAR_KEY3_COL
#    define ANALOG_PREDICTIVE_REGULAR_KEY3_COL 0xFF
#endif

#ifndef ANALOG_PREDICTIVE_REGULAR_KEY4_ROW
#    define ANALOG_PREDICTIVE_REGULAR_KEY4_ROW 0xFF
#endif

#ifndef ANALOG_PREDICTIVE_REGULAR_KEY4_COL
#    define ANALOG_PREDICTIVE_REGULAR_KEY4_COL 0xFF
#endif

#ifndef ANALOG_PREDICTIVE_ACTUATION_ADVANCE
#    define ANALOG_PREDICTIVE_ACTUATION_ADVANCE TRAVEL_SCALE
#endif

#ifndef ANALOG_PREDICTIVE_ACTUATION_MIN_DELTA
#    define ANALOG_PREDICTIVE_ACTUATION_MIN_DELTA TRAVEL_SCALE
#endif

// ----- Predictive RT con velocidad real (rt_predictive_downstroke_ready) -----
// vel_ema lleva un EMA del delta descendente por scan (unidades de TRAVEL_SCALE
// = 0.1 mm/scan, scan anclado a SOF ~1 kHz). El EMA suaviza picos de ruido de
// un solo scan sin perder la pendiente de un W-Tap sostenido. La "puerta"
// MIN_VELOCITY descarta roces/escritura lentos que no deben disparar la
// prediccion.
//
// Tunables (override en config.h del keymap). Defaults calibrados para
// reaccion ~1 scan ante un W-Tap agresivo (delta ~0.3 mm/scan) y no disparar
// ante escritura lenta (delta ~0.1 mm/scan):
//
//   ANALOG_PREDICTIVE_EMA_SHIFT     N en
//                                    vel_ema = vel_ema - (vel_ema>>N) + (delta>>N).
//                                    Default 2 = factor 1/4. Buildup en ~4 scans.
//                                    Mayor N = mas smoothing y buildup mas lento.
//   ANALOG_PREDICTIVE_MIN_VELOCITY  EMA minimo para considerar el golpe "rapido".
//                                    Default 4: un golpe rapido (delta 20) lo
//                                    supera en 1 scan (vel_ema = 5); una prensa
//                                    lenta sostenida (delta 6) lo alcanza tras
//                                    ~3 scans (eso es escritura, no typo).
//   ANALOG_PREDICTIVE_LOOKAHEAD     Scans proyectados hacia adelante en
//                                    projected = travel + vel_ema * LOOKAHEAD.
//                                    Default 2. Lookahead 1 = prediccion modesta
//                                    (~0.1 mm); 3 = agresiva (~0.3 mm para vel 20).
#ifndef ANALOG_PREDICTIVE_EMA_SHIFT
#    define ANALOG_PREDICTIVE_EMA_SHIFT 2
#endif

#ifndef ANALOG_PREDICTIVE_MIN_VELOCITY
#    define ANALOG_PREDICTIVE_MIN_VELOCITY 4
#endif

#ifndef ANALOG_PREDICTIVE_LOOKAHEAD
#    define ANALOG_PREDICTIVE_LOOKAHEAD 2
#endif

#define ANALOG_COORD_DISABLED(row, col) ((row) == 0xFF && (col) == 0xFF)
#define ANALOG_COORD_IN_MATRIX(row, col) ((row) < MATRIX_ROWS && (col) < MATRIX_COLS)
#define ANALOG_COORD_VALID(row, col) (ANALOG_COORD_DISABLED(row, col) || ANALOG_COORD_IN_MATRIX(row, col))

static inline bool analog_matrix_coord_matches(uint8_t row, uint8_t col, uint8_t cfg_row, uint8_t cfg_col) {
    return cfg_row != 0xFF && cfg_col != 0xFF && row == cfg_row && col == cfg_col;
}

static inline bool analog_matrix_predictive_regular_key_matches(uint8_t row, uint8_t col) {
    return analog_matrix_coord_matches(row, col, ANALOG_PREDICTIVE_REGULAR_KEY1_ROW, ANALOG_PREDICTIVE_REGULAR_KEY1_COL) ||
           analog_matrix_coord_matches(row, col, ANALOG_PREDICTIVE_REGULAR_KEY2_ROW, ANALOG_PREDICTIVE_REGULAR_KEY2_COL) ||
           analog_matrix_coord_matches(row, col, ANALOG_PREDICTIVE_REGULAR_KEY3_ROW, ANALOG_PREDICTIVE_REGULAR_KEY3_COL) ||
           analog_matrix_coord_matches(row, col, ANALOG_PREDICTIVE_REGULAR_KEY4_ROW, ANALOG_PREDICTIVE_REGULAR_KEY4_COL);
}

STATIC_ASSERT(ANALOG_COORD_VALID(ANALOG_GAMING_FAST_KEY_ROW, ANALOG_GAMING_FAST_KEY_COL), "ANALOG_GAMING_FAST_KEY must be disabled or inside the matrix");
STATIC_ASSERT(ANALOG_COORD_VALID(ANALOG_CONTINUOUS_RT_KEY1_ROW, ANALOG_CONTINUOUS_RT_KEY1_COL), "ANALOG_CONTINUOUS_RT_KEY1 must be disabled or inside the matrix");
STATIC_ASSERT(ANALOG_COORD_VALID(ANALOG_CONTINUOUS_RT_KEY2_ROW, ANALOG_CONTINUOUS_RT_KEY2_COL), "ANALOG_CONTINUOUS_RT_KEY2 must be disabled or inside the matrix");
STATIC_ASSERT(ANALOG_COORD_VALID(ANALOG_PREDICTIVE_RT_KEY1_ROW, ANALOG_PREDICTIVE_RT_KEY1_COL), "ANALOG_PREDICTIVE_RT_KEY1 must be disabled or inside the matrix");
STATIC_ASSERT(ANALOG_COORD_VALID(ANALOG_PREDICTIVE_RT_KEY2_ROW, ANALOG_PREDICTIVE_RT_KEY2_COL), "ANALOG_PREDICTIVE_RT_KEY2 must be disabled or inside the matrix");
STATIC_ASSERT(ANALOG_COORD_VALID(ANALOG_PREDICTIVE_RT_KEY3_ROW, ANALOG_PREDICTIVE_RT_KEY3_COL), "ANALOG_PREDICTIVE_RT_KEY3 must be disabled or inside the matrix");
STATIC_ASSERT(ANALOG_COORD_VALID(ANALOG_PREDICTIVE_RT_KEY4_ROW, ANALOG_PREDICTIVE_RT_KEY4_COL), "ANALOG_PREDICTIVE_RT_KEY4 must be disabled or inside the matrix");
STATIC_ASSERT(ANALOG_COORD_VALID(ANALOG_PREDICTIVE_RT_KEY5_ROW, ANALOG_PREDICTIVE_RT_KEY5_COL), "ANALOG_PREDICTIVE_RT_KEY5 must be disabled or inside the matrix");
STATIC_ASSERT(ANALOG_COORD_VALID(ANALOG_PREDICTIVE_RT_KEY6_ROW, ANALOG_PREDICTIVE_RT_KEY6_COL), "ANALOG_PREDICTIVE_RT_KEY6 must be disabled or inside the matrix");
STATIC_ASSERT(ANALOG_COORD_VALID(ANALOG_PREDICTIVE_REGULAR_KEY1_ROW, ANALOG_PREDICTIVE_REGULAR_KEY1_COL), "ANALOG_PREDICTIVE_REGULAR_KEY1 must be disabled or inside the matrix");
STATIC_ASSERT(ANALOG_COORD_VALID(ANALOG_PREDICTIVE_REGULAR_KEY2_ROW, ANALOG_PREDICTIVE_REGULAR_KEY2_COL), "ANALOG_PREDICTIVE_REGULAR_KEY2 must be disabled or inside the matrix");
STATIC_ASSERT(ANALOG_COORD_VALID(ANALOG_PREDICTIVE_REGULAR_KEY3_ROW, ANALOG_PREDICTIVE_REGULAR_KEY3_COL), "ANALOG_PREDICTIVE_REGULAR_KEY3 must be disabled or inside the matrix");
STATIC_ASSERT(ANALOG_COORD_VALID(ANALOG_PREDICTIVE_REGULAR_KEY4_ROW, ANALOG_PREDICTIVE_REGULAR_KEY4_COL), "ANALOG_PREDICTIVE_REGULAR_KEY4 must be disabled or inside the matrix");
STATIC_ASSERT(ANALOG_CONTINUOUS_RT_REPRESS_MAX_TRAVEL <= ((FULL_TRAVEL_UNIT + 1) * TRAVEL_SCALE - 1), "ANALOG_CONTINUOUS_RT_REPRESS_MAX_TRAVEL cannot exceed max travel");
STATIC_ASSERT(!ANALOG_PREDICTIVE_REGULAR_FORCE_MODE_IN_GAMING || ANALOG_PREDICTIVE_REGULAR_IN_GAMING_MODE, "ANALOG_PREDICTIVE_REGULAR_FORCE_MODE_IN_GAMING requires ANALOG_PREDICTIVE_REGULAR_IN_GAMING_MODE");

// Threshold value when the magnet switch is not installed
#ifndef ABNORMAL_ANALOG_RAW_THRESHOLD_VALUE
#    define ABNORMAL_ANALOG_RAW_THRESHOLD_VALUE 3250
#endif

//
#ifndef AUTO_CALIB_FULL_TRAVEL_THRESHOLD_VALUE
#    define AUTO_CALIB_FULL_TRAVEL_THRESHOLD_VALUE (DEFAULT_ZERO_TRAVEL_VALUE - DEFAULT_FULL_RANGE + 100)
#endif

#ifndef AUTO_CALIB_ZERO_TRAVEL_JITTER_VALUE
#    define AUTO_CALIB_ZERO_TRAVEL_JITTER_VALUE 50
#endif

#ifndef AUTO_CALIB_FULL_TRAVEL_JITTER_VALUE
#    define AUTO_CALIB_FULL_TRAVEL_JITTER_VALUE 100
#endif

#ifndef AUTO_CALIB_ZERO_TRAVEL_THRESHOLD_VALUE
#    define AUTO_CALIB_ZERO_TRAVEL_THRESHOLD_VALUE (DEFAULT_FULL_RANGE - AUTO_CALIB_FULL_TRAVEL_JITTER_VALUE)
#endif

#ifndef AUTO_CALIB_VALID_RELASING_TIME
#    define AUTO_CALIB_VALID_RELASING_TIME 1000
#endif

void analog_matrix_init(void);
void analog_matrix_eeconfig_init(void);
bool update_raw_value(uint8_t row, uint8_t col, uint16_t value);
void update_travel_configs(void);
void update_key_config(uint8_t row, uint8_t col);
void analog_matrix_eeprom_update(const void *buf, void *addr, size_t len);

void analog_matrix_set_mins(uint16_t *min);
void analog_matrix_set_maxs(uint16_t *max);

uint8_t      analog_matrix_get_travel(uint8_t row, uint8_t col);
uint8_t      analog_matrix_get_key_mode(uint8_t row, uint8_t col);
bool         analog_matrix_get_key_state(uint8_t row, uint8_t col);
bool         analog_matrix_calibrating(void);
matrix_row_t analog_matrix_get_row(uint8_t row);
void         analog_matrix_rx(uint8_t *data, uint8_t length);
void         analog_matrix_task(void);
void         analog_matrix_indicator(void);
void         analog_matrix_clear(void);
void         analog_matrix_clear_advance_keys(void);
