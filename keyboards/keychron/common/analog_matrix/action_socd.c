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

#include "quantum.h"
#include "analog_matrix.h"
#include "profile.h"
#include "action_socd.h"

enum {
    KEY_1_ACTIVE     = 0x01,
    KEY_2_ACTIVE     = 0x02,
    BOTH_KEYS_ACTIVE = 0x03,
};

extern matrix_row_t analog_raw_matrix[MATRIX_ROWS];
extern matrix_row_t raw_matrix[MATRIX_ROWS];
extern matrix_row_t changed_matrix[MATRIX_ROWS];

#ifndef ANALOG_SOCD_RUNTIME_COMPACT
#    define ANALOG_SOCD_RUNTIME_COMPACT 0
#endif

#ifndef ANALOG_SOCD_BOTTOM_OUT_HOLD_WINNER
#    define ANALOG_SOCD_BOTTOM_OUT_HOLD_WINNER 0
#endif

#if ANALOG_SOCD_RUNTIME_COMPACT

typedef struct {
    uint8_t      row1;
    uint8_t      col1;
    uint8_t      row2;
    uint8_t      col2;
    matrix_row_t mask1;
    matrix_row_t mask2;
    uint8_t      type;
    uint8_t      state;
} socd_runtime_pair_t;

static socd_runtime_pair_t active_socd[SOCD_COUNT];
static uint8_t             active_socd_count;

void socd_update_active_state(void) {
    socd_config_t *socd = profile_get_current()->socd;
    active_socd_count   = 0;
    memset(active_socd, 0, sizeof(active_socd));

    for (uint8_t i = 0; i < SOCD_COUNT; i++) {
        if (!socd[i].type) continue;
        if (socd[i].key_1_row >= MATRIX_ROWS || socd[i].key_1_col >= MATRIX_COLS ||
            socd[i].key_2_row >= MATRIX_ROWS || socd[i].key_2_col >= MATRIX_COLS) continue;

        socd_runtime_pair_t *rt = &active_socd[active_socd_count++];
        rt->row1                = socd[i].key_1_row;
        rt->col1                = socd[i].key_1_col;
        rt->row2                = socd[i].key_2_row;
        rt->col2                = socd[i].key_2_col;
        rt->mask1               = (matrix_row_t)1 << rt->col1;
        rt->mask2               = (matrix_row_t)1 << rt->col2;
        rt->type                = socd[i].type;
    }
}

void socd_action(void) {
#if ANALOG_DISABLE_SOCD_IN_GAMING_MODE
    if (analog_matrix_is_gaming_mode()) {
        for (uint8_t row = 0; row < MATRIX_ROWS; row++) {
            raw_matrix[row] = analog_raw_matrix[row];
        }
        return;
    }
#endif

    if (active_socd_count == 0) {
        for (uint8_t row = 0; row < MATRIX_ROWS; row++) {
            raw_matrix[row] = analog_raw_matrix[row];
        }
        return;
    }

    matrix_row_t socd_mask[MATRIX_ROWS];
    memset(socd_mask, 0xFF, sizeof(socd_mask));

    for (uint8_t i = 0; i < active_socd_count; i++) {
        socd_runtime_pair_t *rt   = &active_socd[i];
        const uint8_t       row1  = rt->row1;
        const uint8_t       col1  = rt->col1;
        const uint8_t       row2  = rt->row2;
        const uint8_t       col2  = rt->col2;
        const matrix_row_t  mask1 = rt->mask1;
        const matrix_row_t  mask2 = rt->mask2;
        bool                keep_last_state = false;

        if ((analog_raw_matrix[row1] & mask1) && (analog_raw_matrix[row2] & mask2)) {
            switch (rt->type) {
                case SOCD_PRI_DEEPER_TRAVEL:
                case SOCD_PRI_DEEPER_TRAVEL_SINGLE: {
                    uint8_t t1 = analog_matrix_get_travel(row1, col1);
                    uint8_t t2 = analog_matrix_get_travel(row2, col2);

                    if (t1 > ANALOG_SOCD_BOTTOM_OUT_THRESHOLD && t2 > ANALOG_SOCD_BOTTOM_OUT_THRESHOLD) {
#if ANALOG_SOCD_BOTTOM_OUT_HOLD_WINNER
                        if (rt->state == 0) rt->state = (t1 >= t2) ? KEY_1_ACTIVE : KEY_2_ACTIVE;
                        keep_last_state = true;
#else
                        if (rt->type == SOCD_PRI_DEEPER_TRAVEL_SINGLE) {
                            if (rt->state == 0) rt->state = (t1 >= t2) ? KEY_1_ACTIVE : KEY_2_ACTIVE;
                            keep_last_state = true;
                        }
#endif
                        break;
                    }

                    if (rt->state == KEY_1_ACTIVE) {
                        if (t2 >= (uint16_t)t1 + ANALOG_SOCD_DEEPER_HYSTERESIS) rt->state = KEY_2_ACTIVE;
                    } else if (rt->state == KEY_2_ACTIVE) {
                        if (t1 >= (uint16_t)t2 + ANALOG_SOCD_DEEPER_HYSTERESIS) rt->state = KEY_1_ACTIVE;
                    } else {
                        rt->state = (t1 >= t2) ? KEY_1_ACTIVE : KEY_2_ACTIVE;
                    }

                    keep_last_state = true;
                    break;
                }

                case SOCD_PRI_LAST_KEYSTROKE:
                    if ((raw_matrix[row1] & mask1) && (changed_matrix[row2] & mask2)) {
                        socd_mask[row1] &= ~mask1;
                        rt->state = KEY_2_ACTIVE;
                    } else if ((raw_matrix[row2] & mask2) && (changed_matrix[row1] & mask1)) {
                        socd_mask[row2] &= ~mask2;
                        rt->state = KEY_1_ACTIVE;
                    } else {
                        if (rt->state == 0) {
                            uint8_t t1 = analog_matrix_get_travel(row1, col1);
                            uint8_t t2 = analog_matrix_get_travel(row2, col2);
                            rt->state  = (t1 >= t2) ? KEY_1_ACTIVE : KEY_2_ACTIVE;
                        }
                        keep_last_state = true;
                    }
                    break;

                case SOCD_PRI_KEY_1:
                    socd_mask[row2] &= ~mask2;
                    rt->state = KEY_1_ACTIVE;
                    break;

                case SOCD_PRI_KEY_2:
                    socd_mask[row1] &= ~mask1;
                    rt->state = KEY_2_ACTIVE;
                    break;

                case SOCD_PRI_NEUTRAL:
                    socd_mask[row1] &= ~mask1;
                    socd_mask[row2] &= ~mask2;
                    rt->state = 0;
                    break;

                default:
                    break;
            }
        } else if (analog_raw_matrix[row1] & mask1) {
            rt->state = KEY_1_ACTIVE;
        } else if (analog_raw_matrix[row2] & mask2) {
            rt->state = KEY_2_ACTIVE;
        }

        if (keep_last_state) {
            if (rt->state == KEY_1_ACTIVE)
                socd_mask[row2] &= ~mask2;
            else if (rt->state == KEY_2_ACTIVE)
                socd_mask[row1] &= ~mask1;
        }
    }

    for (uint8_t row = 0; row < MATRIX_ROWS; row++) {
        raw_matrix[row] = analog_raw_matrix[row] & socd_mask[row];
    }
}

#else
static bool socd_active;
static uint8_t socd_state[SOCD_COUNT];

void socd_update_active_state(void) {
    socd_config_t *socd = profile_get_current()->socd;
    socd_active         = false;
    memset(socd_state, 0, sizeof(socd_state));
    for (uint8_t i = 0; i < SOCD_COUNT; i++) {
        if (socd[i].type) {
            socd_active = true;
            return;
        }
    }
}

void socd_action(void) {
#if ANALOG_DISABLE_SOCD_IN_GAMING_MODE
    if (analog_matrix_is_gaming_mode()) {
        for (uint8_t row = 0; row < MATRIX_ROWS; row++) {
            raw_matrix[row] = analog_raw_matrix[row];
        }
        return;
    }
#endif

    if (!socd_active) {
        for (uint8_t row = 0; row < MATRIX_ROWS; row++) {
            raw_matrix[row] = analog_raw_matrix[row];
        }
        return;
    }

    matrix_row_t socd_mask[MATRIX_ROWS];
    memset(socd_mask, 0xFF, sizeof(socd_mask));

    socd_config_t *socd = profile_get_current()->socd;
    bool           keep_last_state;
    uint8_t        row1, row2, col1, col2;
    for (uint8_t i = 0; i < SOCD_COUNT; i++) {
        if (socd[i].type) {
            keep_last_state = false;
            row1 = socd[i].key_1_row;
            col1 = socd[i].key_1_col;
            row2 = socd[i].key_2_row;
            col2 = socd[i].key_2_col;

            if (row1 >= MATRIX_ROWS || col1 >= MATRIX_COLS || row2 >= MATRIX_ROWS || col2 >= MATRIX_COLS) continue;

            if ((analog_raw_matrix[row1] & (0x01 << col1)) && (analog_raw_matrix[row2] & (0x01 << col2))) {
                switch (socd[i].type) {
                    case SOCD_PRI_DEEPER_TRAVEL:
                    case SOCD_PRI_DEEPER_TRAVEL_SINGLE: {
                        uint8_t t1 = analog_matrix_get_travel(row1, col1);
                        uint8_t t2 = analog_matrix_get_travel(row2, col2);

                        if (t1 > ANALOG_SOCD_BOTTOM_OUT_THRESHOLD && t2 > ANALOG_SOCD_BOTTOM_OUT_THRESHOLD) {
                            // Both bottomed out: register both, or keep the
                            // current winner only in SINGLE mode.
#if ANALOG_SOCD_BOTTOM_OUT_HOLD_WINNER
                            if (socd_state[i] == 0) socd_state[i] = (t1 >= t2) ? KEY_1_ACTIVE : KEY_2_ACTIVE;
                            keep_last_state = true;
#else
                            if (socd[i].type == SOCD_PRI_DEEPER_TRAVEL_SINGLE) {
                                // Sin ganador previo (estado recien reseteado
                                // por boot/cambio de perfil con ambas al
                                // fondo): resolver por profundidad en vez de
                                // dejar pasar ambas direcciones.
                                if (socd_state[i] == 0) socd_state[i] = (t1 >= t2) ? KEY_1_ACTIVE : KEY_2_ACTIVE;
                                keep_last_state = true;
                            }
#endif
                            break;
                        }

                        // Winner tracking with hysteresis: the challenger only
                        // takes over when deeper than the current winner by
                        // ANALOG_SOCD_DEEPER_HYSTERESIS. Prevents the output
                        // flipping every scan on sensor noise at equal depths.
                        if (socd_state[i] == KEY_1_ACTIVE) {
                            if (t2 >= (uint16_t)t1 + ANALOG_SOCD_DEEPER_HYSTERESIS) socd_state[i] = KEY_2_ACTIVE;
                        } else if (socd_state[i] == KEY_2_ACTIVE) {
                            if (t1 >= (uint16_t)t2 + ANALOG_SOCD_DEEPER_HYSTERESIS) socd_state[i] = KEY_1_ACTIVE;
                        } else {
                            socd_state[i] = (t1 >= t2) ? KEY_1_ACTIVE : KEY_2_ACTIVE;
                        }

                        keep_last_state = true; // mask the loser via socd_state
                        break;
                    }

                    case SOCD_PRI_LAST_KEYSTROKE:
                        if ((raw_matrix[row1] & (0x01 << col1)) && (analog_raw_matrix[row2] & (0x01 << col2)) && (changed_matrix[row2] & (0x01 << col2))) {
                            socd_mask[row1] &= ~(0x01 << col1);
                            socd_state[i] = KEY_2_ACTIVE;

                        } else if ((raw_matrix[row2] & (0x01 << col2)) && (analog_raw_matrix[row1] & (0x01 << col1)) && (changed_matrix[row1] & (0x01 << col1))) {
                            socd_mask[row2] &= ~(0x01 << col2);
                            socd_state[i] = KEY_1_ACTIVE;

                        } else {
                            // Ambas cruzaron actuacion en el MISMO scan: no hay
                            // "ultima pulsacion" real que preferir. Si ademas no
                            // hay ganador previo (socd_state==0, primer uso tras
                            // boot/cambio de perfil), sin esto pasaban AMBAS
                            // direcciones hasta soltar una. Resolver por
                            // profundidad como desempate deterministico.
                            if (socd_state[i] == 0) {
                                uint8_t t1 = analog_matrix_get_travel(row1, col1);
                                uint8_t t2 = analog_matrix_get_travel(row2, col2);
                                socd_state[i] = (t1 >= t2) ? KEY_1_ACTIVE : KEY_2_ACTIVE;
                            }
                            keep_last_state = true;
                        }
                        break;

                    case SOCD_PRI_KEY_1:
                        socd_mask[row2] &= ~(0x01 << col2);
                        socd_state[i] = KEY_1_ACTIVE;
                        break;

                    case SOCD_PRI_KEY_2:
                        socd_mask[row1] &= ~(0x01 << col1);
                        socd_state[i] = KEY_2_ACTIVE;
                        break;

                    case SOCD_PRI_NEUTRAL:
                        socd_mask[row1] &= ~(0x01 << col1);
                        socd_mask[row2] &= ~(0x01 << col2);
                        socd_state[i] = 0;
                        break;

                    default:
                        break;
                }
            } else if (analog_raw_matrix[row1] & (0x01 << col1)) {
                socd_state[i] = KEY_1_ACTIVE;
            } else if (analog_raw_matrix[row2] & (0x01 << col2)) {
                socd_state[i] = KEY_2_ACTIVE;
            }

            if (keep_last_state) {
                if (socd_state[i] == KEY_1_ACTIVE)
                    socd_mask[row2] &= ~(0x01 << col2);
                else if (socd_state[i] == KEY_2_ACTIVE)
                    socd_mask[row1] &= ~(0x01 << col1);
            }
        }
    }

    for (uint8_t row = 0; row < MATRIX_ROWS; row++) {
        raw_matrix[row] = analog_raw_matrix[row] & socd_mask[row];
    }
}
#endif // ANALOG_SOCD_RUNTIME_COMPACT
