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
#include "keymap_introspection.h"
#include "raw_hid.h"
#include "eeprom.h"
#include "eeprom_he.h"

#if ANALOG_AUTO_CALIBRATION_ENABLE || ANALOG_BOTTOM_OUT_LEARN
static bool calibration_dirty = false;
#endif
#include "usb_main.h"
#include <stdio.h>
#include "profile.h"
#include "sqrt.h"
#include "game_controller_common.h"
#include "nvm_eeprom_eeconfig_internal.h"

#ifndef CAL_SAMPL_CNT
#    define CAL_SAMPL_CNT 8
#endif

#ifndef REF_ZERO_TRAVEL
#    define REF_ZERO_TRAVEL 3121
#endif
#ifndef REF_FULL_TRAVEL
#    define REF_FULL_TRAVEL 1940
#endif
#define REF_RANGE (REF_ZERO_TRAVEL - REF_FULL_TRAVEL)

#ifndef CONST_A1
#    define CONST_A1 426.88962f
#endif
#ifndef CONST_B1
#    define CONST_B1 -0.48358f
#endif
#ifndef CONST_C1
#    define CONST_C1 2.04637e-4f
#endif
#ifndef CONST_D1
#    define CONST_D1 -2.99368e-8f
#endif

#define TRAVEL_POLYNOMIAL(x) (CONST_A1 + ((float)(x)) * (CONST_B1 + ((float)(x)) * (CONST_C1 + ((float)(x)) * CONST_D1)))
#define TRAVEL_POLY_REF_ZERO (TRAVEL_POLYNOMIAL(REF_ZERO_TRAVEL))

#if ANALOG_FIXED_POINT_TRAVEL
#    define TRAVEL_POLY_Q 34
#    define TRAVEL_FIXED_Q 16
#    define TRAVEL_POLY_Q_TO_FIXED_Q_SHIFT (TRAVEL_POLY_Q - TRAVEL_FIXED_Q)
#    define TRAVEL_POLY_Q_TO_FIXED_Q_ROUND (1LL << (TRAVEL_POLY_Q_TO_FIXED_Q_SHIFT - 1))
#    define TRAVEL_SCALE_FACTOR_Q_ONE ((uint32_t)TRAVEL_SCALE << TRAVEL_FIXED_Q)
/*
 * Equivalent to:
 *   TRAVEL_POLYNOMIAL(x) - TRAVEL_POLYNOMIAL(REF_ZERO_TRAVEL)
 * expressed as a cubic in t = REF_ZERO_TRAVEL - x.
 *
 * Coefficients are Q34:
 *   t * (0.0810467104664 - 0.0000756612584*t + 0.0000000299368*t*t)
 */
#    define TRAVEL_POLY_T1_Q34 1392371884LL
#    define TRAVEL_POLY_T2_Q34 (-1299851LL)
#    define TRAVEL_POLY_T3_Q34 514LL

static inline uint32_t travel_polynomial_delta_q16(uint16_t x) {
    const uint32_t t = REF_ZERO_TRAVEL - x;
    int64_t        v = TRAVEL_POLY_T2_Q34 + (int64_t)t * TRAVEL_POLY_T3_Q34;
    v                = TRAVEL_POLY_T1_Q34 + (int64_t)t * v;
    v                = (int64_t)t * v;

    if (v <= 0) return 0;
    return (uint32_t)((v + TRAVEL_POLY_Q_TO_FIXED_Q_ROUND) >> TRAVEL_POLY_Q_TO_FIXED_Q_SHIFT);
}

static inline uint32_t travel_scale_factor_to_q16(float factor) {
    const float scaled = factor * (float)TRAVEL_SCALE * (float)(1UL << TRAVEL_FIXED_Q) + 0.5f;
    return scaled >= (float)UINT32_MAX ? UINT32_MAX : (uint32_t)scaled;
}
#endif

enum {
    CALIB_OFF = 0,
    CALIB_ZERO_TRAVEL_POWER_ON,
    CALIB_ZERO_TRAVEL_MANUAL,
    CALIB_FULL_TRAVEL_MANUAL,
    CALIB_SAVE_AND_EXIT,
    CALIB_CLEAR,
    CALIB_MAX,
};

enum {
    CALI_ZERO_TRAVEL = 0x01 << 0,
    CALI_FULL_TRAVEL = 0x01 << 1,
};

enum {
    AUTO_CALIB_OFF = 0,
    AUTO_CALIB_NEXT_LOOP,
    AUTO_CALIB_ZERO_TRAVEL,
    AUTO_CALIB_FULL_TRAVEL,
    AUTO_CALIB_FINISHED,
};

enum {
    AMC_GET_VERSION = 0x01,

    AMC_GET_PROFILES_INFO = 0x10,
    AMC_SELECT_PROFILE,
    AMC_GET_PROFILE_RAW,
    AMC_SET_PROFILE_NAME,
    AMC_SET_TRAVAL,
    AMC_SET_ADVANCE_MODE,
    AMC_SET_SOCD,
    AMC_RESET_PROFILE = 0x1E,
    AMC_SAVE_PROFILE  = 0x1F,
    AMC_GET_CURVE     = 0x20,
    AMC_SET_CURVE,
    AMC_GET_GAME_CONTROLLER_MODE,
    AMC_SET_GAME_CONTROLLER_MODE,

    AMC_GET_REALTIME_TRAVEL = 0x30,

    AMC_CALIBRATE = 0x40,
    AMC_GET_CALIBRATE_STATE,
    AMC_GET_CALIBRATED_VALUE,
};

static inline bool analog_matrix_reject_raw_hid_in_gaming(uint8_t cmd) {
    if (!analog_matrix_is_gaming_mode()) return false;

    switch (cmd) {
        case AMC_RESET_PROFILE:
        case AMC_CALIBRATE:
            return true;
        default:
            return false;
    }
}

extern const matrix_row_t analog_matrix_mask[];
extern const matrix_row_t okmc_matrix[MATRIX_ROWS];
extern matrix_row_t       virtual_matrix[MATRIX_ROWS];

extern bool regular_trigger_action(analog_key_t *key);
extern bool okmc_action(analog_key_t *key);
extern bool rapid_trigger_action(analog_key_t *key);
extern bool toggle_action(analog_key_t *key);
extern bool xinput_update(analog_key_t *key);
extern bool joystick_update(analog_key_t *key);
extern void socd_action(void);

static calibrated_value_t calib_values[MATRIX_ROWS][MATRIX_COLS];
static calibrated_value_t saved_calib_values[MATRIX_ROWS][MATRIX_COLS];
static analog_key_t       analog_key_matrix[MATRIX_ROWS][MATRIX_COLS];

static uint16_t      calibrate_values[MATRIX_ROWS][MATRIX_COLS][CAL_SAMPL_CNT];
#if ANALOG_AUTO_CALIBRATION_ENABLE
static calibration_t auto_calib[MATRIX_ROWS][MATRIX_COLS];
#endif
static uint8_t       cali_state = CALIB_OFF;
static uint8_t       last_cali_state;
static uint8_t       cur_calib = 0;
traval_config_t      regular;
static float         scale_factor[MATRIX_ROWS][MATRIX_COLS];
#if ANALOG_FIXED_POINT_TRAVEL
static uint32_t      scale_factor_q16[MATRIX_ROWS][MATRIX_COLS];
#endif
static matrix_row_t  calib_state_matrix[MATRIX_ROWS];
// Mark invalid key on abnormal value of manual zero travel calibration, for manufacturing use
static matrix_row_t manual_calib_zero_invalid[MATRIX_ROWS];

uint8_t calibrated, rapid_actuation, rapid_sensitivity;
static uint8_t eeprom_calibrated;

static uint32_t calib_ind_timer = 0;
static uint8_t  last_calib_row  = 0xFF;
static uint8_t  last_calib_col  = 0xFF;

uint32_t debug_interval = 0;

uint8_t analog_matrix_get_travel(uint8_t row, uint8_t col) {
    return analog_key_matrix[row][col].travel;
}

#if ANALOG_DISABLE_OKMC_IN_GAMING_MODE || ANALOG_DISABLE_TOGGLE_IN_GAMING_MODE || ANALOG_DISABLE_GAMEPAD_IN_GAMING_MODE || ANALOG_PREDICTIVE_REGULAR_FORCE_MODE_IN_GAMING
static inline uint8_t analog_matrix_base_mode(uint8_t row, uint8_t col) {
    analog_matrix_profile_t *cur_prof = profile_get_current();
    analog_key_config_t *    key_cfg  = &cur_prof->key_config[row][col];

    return key_cfg->mode == AKM_GLOBAL ? cur_prof->global.mode : key_cfg->mode;
}

static inline uint8_t analog_matrix_apply_gaming_mode_overrides(uint8_t row, uint8_t col, uint8_t mode) {
#    if ANALOG_PREDICTIVE_REGULAR_FORCE_MODE_IN_GAMING
    if (mode == AKM_RAPID && analog_matrix_is_gaming_mode() && analog_matrix_predictive_regular_key_matches(row, col)) {
        return AKM_REGULAR;
    }
#    endif

    return mode;
}

static inline uint8_t analog_matrix_effective_mode(uint8_t row, uint8_t col, uint8_t mode) {
#    if ANALOG_DISABLE_OKMC_IN_GAMING_MODE
    if (mode == AKM_DKS && analog_matrix_is_gaming_mode()) {
        return analog_matrix_apply_gaming_mode_overrides(row, col, analog_matrix_base_mode(row, col));
    }
#    endif
#    if ANALOG_DISABLE_TOGGLE_IN_GAMING_MODE
    if (mode == AKM_TOGGLE && analog_matrix_is_gaming_mode()) {
        return analog_matrix_apply_gaming_mode_overrides(row, col, analog_matrix_base_mode(row, col));
    }
#    endif
#    if ANALOG_DISABLE_GAMEPAD_IN_GAMING_MODE
    if (mode == AKM_GAMEPAD && analog_matrix_is_gaming_mode()) {
        return analog_matrix_apply_gaming_mode_overrides(row, col, analog_matrix_base_mode(row, col));
    }
#    endif
    return analog_matrix_apply_gaming_mode_overrides(row, col, mode);
}
#else
static inline uint8_t analog_matrix_effective_mode(uint8_t row, uint8_t col, uint8_t mode) {
    (void)row;
    (void)col;
    return mode;
}
#endif

static uint8_t convert_to_travel(uint8_t row, uint8_t col, uint16_t value) {
    uint16_t travel;
    calibrated_value_t *p_calib = &calib_values[row][col];

    int32_t x = (int32_t)value - (int32_t)p_calib->zero_travel + REF_ZERO_TRAVEL;
    if (x < 0 || x > REF_ZERO_TRAVEL) return 0;

#if TOP_OUT_DEAD_ZONE_GAMING || TOP_OUT_DEAD_ZONE_TYPING
    const uint8_t top_out_deadzone = analog_matrix_is_gaming_mode() ? TOP_OUT_DEAD_ZONE_GAMING : TOP_OUT_DEAD_ZONE_TYPING;
    if (x > REF_ZERO_TRAVEL - top_out_deadzone) return 0;
    x += top_out_deadzone;
#endif

#if ANALOG_FIXED_POINT_TRAVEL
    const uint32_t travel_curve_q16 = travel_polynomial_delta_q16((uint16_t)x);
    const uint64_t travel_fixed     = ((uint64_t)travel_curve_q16 * scale_factor_q16[row][col] + (1ULL << ((TRAVEL_FIXED_Q * 2) - 1))) >> (TRAVEL_FIXED_Q * 2);
    travel = travel_fixed > UINT16_MAX ? UINT16_MAX : (uint16_t)travel_fixed;
#else
    travel = (uint16_t)((TRAVEL_POLYNOMIAL(x) - TRAVEL_POLY_REF_ZERO) * scale_factor[row][col] * TRAVEL_SCALE + 0.5f);
#endif
    if (travel > (FULL_TRAVEL_UNIT + 1) * TRAVEL_SCALE - 1) travel = (FULL_TRAVEL_UNIT + 1) * TRAVEL_SCALE - 1;

    // Saturate to the uint8_t return range instead of masking with 0xFF, which
    // would silently wrap if FULL_TRAVEL_UNIT/TRAVEL_SCALE ever grow past 255.
    if (travel > UINT8_MAX) travel = UINT8_MAX;
    return (uint8_t)travel;
}

// Scale a travel/sensitivity value by TRAVEL_SCALE, saturating to uint8_t.
// Necessary because rpd_trig_sen is a 6-bit field (up to 63) and 63*TRAVEL_SCALE
// exceeds 255, which would otherwise wrap when stored back into a uint8_t.
static inline uint8_t scale_travel_u8(uint8_t v) {
    uint16_t scaled = (uint16_t)v * TRAVEL_SCALE;
    return scaled > UINT8_MAX ? UINT8_MAX : (uint8_t)scaled;
}

static inline uint8_t analog_raw_noise_filter(void) {
    return analog_matrix_is_gaming_mode() ? ANALOG_RAW_NOISE_FILTER_GAMING : ANALOG_RAW_NOISE_FILTER_TYPING;
}

void update_key_config(uint8_t row, uint8_t col) {
    analog_key_t *           p_key;
    analog_key_config_t *    p_key_cfg;
    analog_matrix_profile_t *cur_prof = profile_get_current();

    p_key     = &analog_key_matrix[row][col];
    p_key_cfg = &cur_prof->key_config[row][col];
    // For debug purpose
    p_key->r = row;
    p_key->c = col;

    // Update basic mode
    if (p_key_cfg->mode == AKM_GLOBAL)
        p_key->mode = cur_prof->global.mode;
    else
        p_key->mode = p_key_cfg->mode;

    //  Override mode if advance mode setting exists
    if (p_key_cfg->adv_mode != 0) {
        p_key->mode = p_key_cfg->adv_mode;
    }

    // Update actuaction point
    if (p_key_cfg->act_pt == 0)
        p_key->regular.actn_pt = cur_prof->global.act_pt;
    else
        p_key->regular.actn_pt = p_key_cfg->act_pt;

    // Update deactuaction point
    const bool gaming_mode = analog_matrix_is_gaming_mode();
    uint8_t    static_hysteresis = gaming_mode ? STATIC_HYSTERESIS_GAMING : STATIC_HYSTERESIS_TYPING;
    if (gaming_mode && row == ANALOG_GAMING_FAST_KEY_ROW && col == ANALOG_GAMING_FAST_KEY_COL) {
        static_hysteresis = STATIC_HYSTERESIS_GAMING_FAST_KEY;
    }

#if ANALOG_ADAPTIVE_SHALLOW_HYSTERESIS_GAMING
    if (gaming_mode) {
        const uint8_t max_shallow_hysteresis = p_key->regular.actn_pt > 1 ? p_key->regular.actn_pt / 2 : 0;
        if (static_hysteresis > max_shallow_hysteresis) {
            static_hysteresis = max_shallow_hysteresis;
        }
    }
#endif

    if (static_hysteresis == 0 && p_key->regular.actn_pt > 0) {
        static_hysteresis = 1;
    }

    p_key->regular.deactn_pt = p_key->regular.actn_pt > static_hysteresis ? p_key->regular.actn_pt - static_hysteresis : 0;

    // Update rapid trigger sensitivity
    if (p_key_cfg->rpd_trig_sen == 0)
        p_key->rpd_trig_sen = cur_prof->global.rpd_trig_sen;
    else
        p_key->rpd_trig_sen = p_key_cfg->rpd_trig_sen;

    // Update rapid trigger release sensitivity
    if (p_key_cfg->rpd_trig_sen_deact == 0)
        p_key->rpd_trig_sen_rls = cur_prof->global.rpd_trig_sen_deact == 0 ? cur_prof->global.rpd_trig_sen : cur_prof->global.rpd_trig_sen_deact;
    else
        p_key->rpd_trig_sen_rls = p_key_cfg->rpd_trig_sen_deact;

    /* Scale by TRAVEL_SCALE (saturating to uint8_t to avoid silent overflow) */
    p_key->regular.actn_pt   = scale_travel_u8(p_key->regular.actn_pt);
    p_key->regular.deactn_pt = scale_travel_u8(p_key->regular.deactn_pt);
    p_key->rpd_trig_sen      = scale_travel_u8(p_key->rpd_trig_sen);
    p_key->rpd_trig_sen_rls  = scale_travel_u8(p_key->rpd_trig_sen_rls);

    // Save scaled RT sensitivity before advance-mode union writes may
    // overwrite it (rpd_trig_sen shares storage with okmc_idx/js_axis/hold).
    const uint8_t saved_rpd_trig_sen = p_key->rpd_trig_sen;

    // Update advance mode information
    if (p_key_cfg->adv_mode == AKM_DKS && p_key_cfg->okmc_idx < OKMC_COUNT) {
        p_key->okmc_idx = p_key_cfg->okmc_idx;
    } else if (p_key_cfg->adv_mode == AKM_GAMEPAD && p_key_cfg->js_axis < GC_BUTTON_MAX && p_key_cfg->js_axis != GC_MAX) {
        p_key->js_axis = p_key_cfg->js_axis;
    } else if (p_key_cfg->adv_mode == AKM_TOGGLE) {
        p_key->hold = 0;
    }

    // When gaming mode overrides an advanced mode back to the base mode
    // (Regular or Rapid), the union writes above clobber rpd_trig_sen.
    // Restore it so Rapid Trigger keeps the correct sensitivity.
    if (p_key_cfg->adv_mode != 0 &&
        analog_matrix_effective_mode(row, col, p_key->mode) != p_key_cfg->adv_mode) {
        p_key->rpd_trig_sen = saved_rpd_trig_sen;
    }
}

void update_travel_configs(void) {
    for (uint8_t r = 0; r < MATRIX_ROWS; r++) {
        for (uint8_t c = 0; c < MATRIX_COLS; c++) {
            update_key_config(r, c);
        }
    }
}

static void update_default_travel(void) {
    // clang-format off
    for (uint8_t r = 0; r < MATRIX_ROWS; r++) {
        for (uint8_t c = 0; c < MATRIX_COLS; c++) {
            if ((analog_matrix_mask[r] & (0x01<<c)) == 0) continue;

            if (saved_calib_values[r][c].zero_travel == 0)
                saved_calib_values[r][c].zero_travel = DEFAULT_ZERO_TRAVEL_VALUE;

            if (saved_calib_values[r][c].full_travel == 0)
                saved_calib_values[r][c].full_travel = saved_calib_values[r][c].zero_travel - DEFAULT_FULL_RANGE;
        }
    }
    // clang-format off
}

static inline void update_scale_factor(uint8_t row, uint8_t col) {
    calibrated_value_t *p_calib = &calib_values[row][col];

    if (calibrated & CALI_FULL_TRAVEL) {
        int16_t offset   = p_calib->zero_travel - REF_ZERO_TRAVEL;
        // Compute x in a signed 32-bit type and bound it to the polynomial's
        // valid domain. A bad offset/full_travel pair could otherwise underflow
        // the uint16_t subtraction and feed a garbage x into the polynomial.
        int32_t x_signed = (int32_t)p_calib->full_travel - offset;
        if (x_signed < 0 || x_signed > REF_ZERO_TRAVEL) {
            scale_factor[row][col] = 1.0f;
#if ANALOG_FIXED_POINT_TRAVEL
            scale_factor_q16[row][col] = TRAVEL_SCALE_FACTOR_Q_ONE;
#endif
            return;
        }
        uint16_t x = (uint16_t)x_signed;

        float    full_travel = (TRAVEL_POLYNOMIAL(x) - TRAVEL_POLY_REF_ZERO);
        if (full_travel > 1.0f) {
            scale_factor[row][col] = FULL_TRAVEL_UNIT / full_travel;
        } else {
            scale_factor[row][col] = 1.0f;
        }
    } else {
        scale_factor[row][col] = 1.0f;
    }

#if ANALOG_FIXED_POINT_TRAVEL
    scale_factor_q16[row][col] = travel_scale_factor_to_q16(scale_factor[row][col]);
#endif

}

static void update_scale_factors(void) {
    for (uint8_t r = 0; r < MATRIX_ROWS; r++) {
        for (uint8_t c = 0; c < MATRIX_COLS; c++) {
            update_scale_factor(r, c);

        }
    }
}

void analog_matrix_eeprom_update(const void *buf, void *addr, size_t len) {
    addr += EECONFIG_BASE_ANALOG_MATRIX;
    eeprom_update_block(buf, addr, len);
}

// Removed save_calibration_value as it is now handled asynchronously

static void save_calibration_values(void) {
    if (calibrated) {
        uint8_t invalid_calibration = 0;
        bool    flag_invalidated    = true;

        // Treat the external EEPROM flag as a commit marker: invalidate it,
        // write the payload, then mark it valid again. If either write fails,
        // the next boot will not trust stale calibration data.
        if (eeprom_calibrated) {
            flag_invalidated = he_eeprom_write_block(&invalid_calibration, (void *)(EXTERNAL_EEPROM_OFFSET + OFFSET_CALIBRATION), 1);
            if (flag_invalidated) {
                eeprom_calibrated = invalid_calibration;
            }
        }

        if (flag_invalidated &&
            he_eeprom_write_block(saved_calib_values, (void *)(EXTERNAL_EEPROM_OFFSET + OFFSET_CALIBRATED_DATA_START), sizeof(saved_calib_values)) &&
            he_eeprom_write_block(&calibrated, (void *)(EXTERNAL_EEPROM_OFFSET + OFFSET_CALIBRATION), 1)) {
            eeprom_calibrated = calibrated;
        }
    } else {
        if (eeprom_calibrated != calibrated) {
            if (he_eeprom_write_block(&calibrated, (void *)(EXTERNAL_EEPROM_OFFSET + OFFSET_CALIBRATION), 1)) {
                eeprom_calibrated = calibrated;
            }
        }
    }

    // Save a copy to emulate EEPROM
    if (!eeconfig_is_kb_datablock_valid()) eeprom_update_dword(EECONFIG_KEYBOARD, (EECONFIG_KB_DATA_VERSION));

    if (calibrated) {
        uint8_t invalid_calibration = 0;
        analog_matrix_eeprom_update(&invalid_calibration, OFFSET_CALIBRATION, 1);
        analog_matrix_eeprom_update(saved_calib_values, (uint8_t *)OFFSET_CALIBRATED_DATA_START, sizeof(saved_calib_values));
        analog_matrix_eeprom_update(&calibrated, OFFSET_CALIBRATION, 1);
    } else {
        analog_matrix_eeprom_update(&calibrated, OFFSET_CALIBRATION, 1);
    }

    update_default_travel();
    update_travel_configs();
    update_scale_factors();
}

static bool calibrate(void) {
    static uint32_t calib_timer = 0;

    if (cali_state == CALIB_OFF) return false;

    bool update = false;

    if (cali_state == CALIB_FULL_TRAVEL_MANUAL) {
        bool done = true;

        for (uint8_t r = 0; r < MATRIX_ROWS; r++)
            for (uint8_t c = 0; c < MATRIX_COLS; c++) {
                if ((analog_matrix_mask[r] & (0x01<<c)) == 0) continue;

                uint32_t sum = 0;
                for (uint8_t i=0; i<CAL_SAMPL_CNT; i++) {
                    if (calibrate_values[r][c][i] > VALID_ANALOG_RAW_VALUE_MIN)
                        sum += calibrate_values[r][c][i];
                    else {
                        sum = 0;
                        break;
                    }
                }

                if (sum) {
                    uint16_t avg = sum/CAL_SAMPL_CNT;
                    // clang-format off
                    if ((calib_values[r][c].full_travel == 0 && avg < calib_values[r][c].zero_travel - DEFAULT_FULL_RANGE + 100) ||
                        (calib_values[r][c].full_travel != 0 && avg + BOTTOM_JITTER < calib_values[r][c].full_travel)) {
                        // clang-format on
                        saved_calib_values[r][c].zero_travel = calib_values[r][c].zero_travel;
                        saved_calib_values[r][c].full_travel = calib_values[r][c].full_travel = avg + BOTTOM_JITTER;

                        calib_state_matrix[r] |= (0x01 << c);
                        calib_ind_timer = timer_read32();
                        last_calib_row  = r;
                        last_calib_col  = c;
                    }
                }

                // Check if calibration is finished
                if (calib_values[r][c].full_travel == 0) {
                    done = false;
                }
            }

        if (++cur_calib >= CAL_SAMPL_CNT) cur_calib = 0;

        // Set the timer to delay exit calibrating so that we can get enough samples of last key
        if (done && calib_timer == 0) calib_timer = timer_read32();

        if (calib_timer && timer_elapsed32(calib_timer) > 3000) {
            calib_timer = 0;
            update      = true;
            calibrated |= CALI_FULL_TRAVEL;
            profile_indication_enable();
        }
        if (!update) return false;
    } else if (cali_state == CALIB_ZERO_TRAVEL_POWER_ON || cali_state == CALIB_ZERO_TRAVEL_MANUAL) {
        if (cur_calib + 1 < CAL_SAMPL_CNT) {
            ++cur_calib;
            return false;
        }

        bool valid = true;

        for (uint8_t r = 0; r < MATRIX_ROWS; r++)
            for (uint8_t c = 0; c < MATRIX_COLS; c++) {
                if ((analog_matrix_mask[r] & (0x01 << c)) == 0) continue;

                uint32_t sum = 0;
                for (uint8_t i = 0; i < CAL_SAMPL_CNT; i++)
                    sum += calibrate_values[r][c][i];

                uint16_t avg_val = sum / CAL_SAMPL_CNT;

                // Check validity
                if (avg_val > VALID_ANALOG_RAW_VALUE_MAX || avg_val < DEFAULT_ZERO_TRAVEL_VALUE - DEFAULT_FULL_RANGE / 5) {
                    if (cali_state == CALIB_ZERO_TRAVEL_POWER_ON) valid = false;
                    manual_calib_zero_invalid[r] |= 0x01 << c;
                    if (avg_val < DEFAULT_ZERO_TRAVEL_VALUE - DEFAULT_FULL_RANGE / 5 && cali_state == CALIB_ZERO_TRAVEL_POWER_ON) {
#if ANALOG_AUTO_CALIBRATION_ENABLE
                        auto_calib[r][c].pressed = true;
#endif
                    }
                } else {
                    // new_calibrated_value[r][c] = avg_val;
                    avg_val -= ZERO_TRAVEL_DEAD_ZONE;
                    // Force update if it's the power-on calibration to compensate for temperature
                    // drift, otherwise use the 30-unit threshold to filter noise.
                    int16_t delta = avg_val - calib_values[r][c].zero_travel;
                    if (cali_state == CALIB_ZERO_TRAVEL_POWER_ON || abs(delta) > 30) {
#if ANALOG_AUTO_CALIBRATION_ENABLE
                        auto_calib[r][c].new_calib_value = true;
#endif
                        calib_values[r][c].zero_travel = avg_val;

                        if (cali_state == CALIB_ZERO_TRAVEL_POWER_ON) {
                            if (calibrated & CALI_FULL_TRAVEL) {
                                // Preserve user's manual calibration by shifting it with temperature
                                int32_t shifted_full = (int32_t)calib_values[r][c].full_travel + delta;
                                if (shifted_full < VALID_ANALOG_RAW_VALUE_MIN)
                                    shifted_full = VALID_ANALOG_RAW_VALUE_MIN;
                                else if (shifted_full > VALID_ANALOG_RAW_VALUE_MAX)
                                    shifted_full = VALID_ANALOG_RAW_VALUE_MAX;
                                calib_values[r][c].full_travel = (uint16_t)shifted_full;
                            } else {
                                // No manual calibration, use generic fallback
                                calib_values[r][c].full_travel = avg_val - DEFAULT_FULL_RANGE;
                            }
                        }
                    }
                }
            }

        if (cali_state == CALIB_ZERO_TRAVEL_MANUAL) {
            cali_state = CALIB_FULL_TRAVEL_MANUAL;
        } else if (valid && (calibrated & CALI_ZERO_TRAVEL) == 0) {
            for (uint8_t r = 0; r < MATRIX_ROWS; r++)
                for (uint8_t c = 0; c < MATRIX_COLS; c++) {
                    saved_calib_values[r][c].zero_travel = calib_values[r][c].zero_travel;
                }

            update = true;
        }

        if (valid)
            calibrated |= CALI_ZERO_TRAVEL;
        else
            cali_state = CALIB_OFF;
    } else if (cali_state == CALIB_SAVE_AND_EXIT) {
        if (last_cali_state == CALIB_FULL_TRAVEL_MANUAL) {
            update = true;
            for (uint8_t r = 0; r < MATRIX_ROWS; r++) {
                for (uint8_t c = 0; c < MATRIX_COLS; c++) {
                    if ((analog_matrix_mask[r] & (0x01 << c)) && calib_values[r][c].full_travel == 0) {
                        calib_values[r][c].full_travel = saved_calib_values[r][c].full_travel;
                    }
                }
            }
        }
        cali_state = CALIB_OFF;
    }

    if (update) {
        save_calibration_values();

        switch (cali_state) {
            case CALIB_ZERO_TRAVEL_POWER_ON:
            case CALIB_FULL_TRAVEL_MANUAL:
                cali_state = CALIB_OFF;
                break;

            default:
                break;
        }

    } else if (cali_state == CALIB_ZERO_TRAVEL_POWER_ON) {
        // Recalculate scale factors after temperature correction even though
        // the delta-shift preserves the zero-full range in the ideal case;
        // the clamp above can break that invariant, and this only runs once
        // at boot so the cost is negligible.
        update_scale_factors();
        cali_state = CALIB_OFF;
    }

    return true;
}

void calibration_validate(void) {
    for (uint8_t r = 0; r < MATRIX_ROWS; r++)
        for (uint8_t c = 0; c < MATRIX_COLS; c++) {
            if ((analog_matrix_mask[r] & (0x01 << c)) == 0) continue;

            if (saved_calib_values[r][c].zero_travel < DEFAULT_ZERO_TRAVEL_VALUE - DEFAULT_FULL_RANGE / 5) {
                saved_calib_values[r][c].zero_travel = DEFAULT_ZERO_TRAVEL_VALUE;
                saved_calib_values[r][c].full_travel = saved_calib_values[r][c].zero_travel - DEFAULT_FULL_RANGE;
            }
        }
}

#if ANALOG_AUTO_CALIBRATION_ENABLE
void auto_calibration_init(void) {
    for (uint8_t r = 0; r < MATRIX_ROWS; r++)
        for (uint8_t c = 0; c < MATRIX_COLS; c++) {
            memset(&auto_calib[r][c], 0, sizeof(auto_calib[0][0]));
        }
}

void auto_caliration_check(uint8_t row, uint8_t col, uint16_t value) {
    calibration_t *p = &auto_calib[row][col];

    switch (p->state) {
        case AUTO_CALIB_OFF:
            if (p->pressed) {
                if (value > DEFAULT_ZERO_TRAVEL_VALUE - DEFAULT_FULL_RANGE / 5) {
                    p->pressed = false;
                } else
                    return;
            }
            p->state      = AUTO_CALIB_NEXT_LOOP; // fall through
            p->confidence = 0;
            break;

        case AUTO_CALIB_NEXT_LOOP:
            if (value < AUTO_CALIB_FULL_TRAVEL_THRESHOLD_VALUE) {
                p->value.full_travel = value;
                p->state             = AUTO_CALIB_FULL_TRAVEL;
                p->full_travel_time  = timer_read32();
            }
            break;

        case AUTO_CALIB_ZERO_TRAVEL:
            if (value > p->value.zero_travel) {
                // Key continues releasing
                p->value.zero_travel = value;
                p->cycle             = 0;
            } else if (abs(p->value.zero_travel - value) <= AUTO_CALIB_ZERO_TRAVEL_JITTER_VALUE) {
                //  Add the data to buffer
                calibrate_values[row][col][p->cycle++] = value;
                if (p->cycle >= CAL_SAMPL_CNT) {
                    if (timer_elapsed32(p->full_travel_time) < AUTO_CALIB_VALID_RELASING_TIME) {
                        uint32_t avg_val = 0;
                        for (uint8_t i = 0; i < CAL_SAMPL_CNT; i++)
                            avg_val += calibrate_values[row][col][i];

                        avg_val /= CAL_SAMPL_CNT;
                        avg_val -= ZERO_TRAVEL_DEAD_ZONE;

                        p->value.zero_travel = avg_val;

                        // Update confidence. Use a signed delta so that an
                        // anomalous avg_val < full_travel does not underflow the
                        // unsigned subtraction into a huge value and falsely
                        // boost confidence (committing corrupt calibration).
                        int32_t travel_delta = (int32_t)avg_val - (int32_t)p->value.full_travel;
                        if (travel_delta > 1100) {
                            p->confidence += p->calibrated ? 12 : 6;
                        } else if (travel_delta > 1000) {
                            p->confidence += p->calibrated ? 4 : 3;
                        } else if (travel_delta > 900) {
                            p->confidence += p->calibrated ? 3 : 2;
                        }
                    }

                    p->state = AUTO_CALIB_NEXT_LOOP;
                }
            } else {
                p->state = AUTO_CALIB_NEXT_LOOP;
            }

            if (p->confidence >= 12) {
                p->state = AUTO_CALIB_FINISHED;

                if (abs(p->value.zero_travel - calib_values[row][col].zero_travel) > 10 || abs(p->value.full_travel + BOTTOM_JITTER - calib_values[row][col].full_travel) > 30) {
                    calib_values[row][col].zero_travel = p->value.zero_travel;
                    calib_values[row][col].full_travel = p->value.full_travel + BOTTOM_JITTER;

                    if (abs(saved_calib_values[row][col].zero_travel - calib_values[row][col].zero_travel) > 15 || abs(saved_calib_values[row][col].full_travel - calib_values[row][col].full_travel) > 50) {
                        /* Save */
                        saved_calib_values[row][col].zero_travel = calib_values[row][col].zero_travel;
                        saved_calib_values[row][col].full_travel = calib_values[row][col].full_travel;
                        calibration_dirty = true;
                    } else {
                        calib_values[row][col].zero_travel = saved_calib_values[row][col].zero_travel;
                        calib_values[row][col].full_travel = saved_calib_values[row][col].full_travel;
                    }

                    update_default_travel();
                    update_travel_configs();
                    update_scale_factors();
                }
            }
            break;

        case AUTO_CALIB_FULL_TRAVEL:
            if (value < p->value.full_travel) {
                // Key continues pressing
                p->value.full_travel = value;
                p->full_travel_time  = timer_read32();
            } else if (value - p->value.full_travel <= AUTO_CALIB_FULL_TRAVEL_JITTER_VALUE) {
                // Key is still pressed, we encounter system noise value jitter, just update the time
                p->full_travel_time = timer_read32();
            } else if (value - p->value.full_travel > AUTO_CALIB_ZERO_TRAVEL_THRESHOLD_VALUE) {
                // Key releasing detected
                p->value.zero_travel = value;
                p->state             = AUTO_CALIB_ZERO_TRAVEL;
                p->cycle             = 0;
            }
            break;

        case AUTO_CALIB_FINISHED:
            if (value > p->value.zero_travel + 50) p->state = AUTO_CALIB_OFF;
            break;

        default:
            break;
    }
}
#else
void auto_calibration_init(void) {}
#endif

void analog_matrix_eeconfig_init(void) {
    bool reset_profiles = false;
    if (!eeconfig_is_enabled()) {
        eeconfig_init();
        reset_profiles = true;
    }

    profile_init(reset_profiles);

    uint8_t *buf = (uint8_t *)malloc(EECONFIG_SIZE_ANALOG_MATRIX);
    if (!buf) {
        // Cannot load saved config from EEPROM; initialize all critical
        // data structures with safe defaults so the keyboard boots
        // functional (all keys at default actuation) instead of dead.
        memset(calib_values, 0, sizeof(calib_values));
        memset(saved_calib_values, 0, sizeof(saved_calib_values));
        calibrated = 0;
        calibration_validate();
        update_default_travel();
        memcpy(calib_values, saved_calib_values, sizeof(saved_calib_values));
        memset(analog_key_matrix, 0, sizeof(analog_key_matrix));
        update_travel_configs();
        update_scale_factors();
        auto_calibration_init();
        cali_state = CALIB_ZERO_TRAVEL_POWER_ON;
        return;
    }
    memset(buf, 0, EECONFIG_SIZE_ANALOG_MATRIX);

    eeprom_read_block(buf, (void *)EECONFIG_BASE_ANALOG_MATRIX, EECONFIG_SIZE_ANALOG_MATRIX);

    // Load curve points
    point_t curve[CURVE_POINTS_COUNT];
    memcpy(curve, buf + OFFSET_CURVE_PTS_START, CURVE_POINTS_COUNT * SIZE_OF_POINT_T);
    game_controller_curve_init(curve);
    game_controller_mode_init(buf[OFFSET_GAME_CONTROLLER_MODE_START]);

    // Load calibration data
    calibrated = buf[OFFSET_CALIBRATION];
    memset(calib_values, 0, sizeof(calib_values));
    memset(saved_calib_values, 0, sizeof(saved_calib_values));

    bool i2c_fallback = false;
    if (calibrated) {
        memcpy(saved_calib_values, buf + OFFSET_CALIBRATED_DATA_START, sizeof(saved_calib_values));
    } else {
        uint32_t magic;
        if (!he_eeprom_read_block(&magic, 0, 4)) {
            // I2C read failed, fallback to defaults (still run the common
            // finalization below so calib_values/scale_factor/cali_state get
            // initialized and buf is freed).
            calibrated    = 0;
            i2c_fallback  = true;
        } else if (magic != VENDOR_ID) {
            he_eeprom_driver_erase();
            magic = VENDOR_ID;
            he_eeprom_write_block(&magic, 0, 4);
            // cali_state = CALIB_ZERO_TRAVEL;
        } else {
            if (!he_eeprom_read_block(&calibrated, (void *)(EXTERNAL_EEPROM_OFFSET + OFFSET_CALIBRATION), 1)) {
                calibrated   = 0;
                i2c_fallback = true;
            } else {
                eeprom_calibrated = calibrated;
            }

            if (calibrated) {
                if (!he_eeprom_read_block(saved_calib_values, (void *)(EXTERNAL_EEPROM_OFFSET + OFFSET_CALIBRATED_DATA_START), sizeof(saved_calib_values))) {
                    calibrated   = 0;
                    i2c_fallback = true;
                } else {
                    // Save to emulated EEPROM
                    analog_matrix_eeprom_update(&calibrated, OFFSET_CALIBRATION, 1);
                    analog_matrix_eeprom_update(saved_calib_values, (uint8_t *)OFFSET_CALIBRATED_DATA_START, sizeof(saved_calib_values));
                }
            }
        }
    }

    if (i2c_fallback) {
        // I2C unavailable at cold boot: seed sane defaults into saved_calib_values
        // before the shared finalization below copies them into calib_values and
        // recomputes scale factors.
        calibration_validate();
        update_default_travel();
    }

    // Reset to default if calibrated data is invalid
    for (uint8_t r = 0; r < MATRIX_ROWS; r++)
        for (uint8_t c = 0; c < MATRIX_COLS; c++) {
            if ((analog_matrix_mask[r] & (0x01U << c)) == 0) continue;
            if (saved_calib_values[r][c].zero_travel > VALID_ANALOG_RAW_VALUE_MAX || saved_calib_values[r][c].zero_travel < DEFAULT_ZERO_TRAVEL_VALUE - 300) saved_calib_values[r][c].zero_travel = DEFAULT_ZERO_TRAVEL_VALUE;

            if (saved_calib_values[r][c].full_travel > DEFAULT_FULL_TRAVEL_VALUE + 400 || saved_calib_values[r][c].full_travel < VALID_ANALOG_RAW_VALUE_MIN) saved_calib_values[r][c].full_travel = saved_calib_values[r][c].zero_travel - DEFAULT_FULL_RANGE;
        }

    if (calibrated) {
        calibration_validate();
        update_default_travel();
    }
    memcpy(calib_values, saved_calib_values, sizeof(saved_calib_values));

    // Load actuation/deacuation dat
    memset(analog_key_matrix, 0, sizeof(analog_key_matrix));
    update_travel_configs();
    update_scale_factors();

    auto_calibration_init();
    cali_state = CALIB_ZERO_TRAVEL_POWER_ON;

    free(buf);
}

void analog_matrix_init(void) {
    he_eeprom_driver_init();

    analog_matrix_eeconfig_init();

    cur_calib = 0;
    memset(calibrate_values, 0, MATRIX_ROWS * MATRIX_COLS * CAL_SAMPL_CNT * sizeof(calibrate_values[0][0][0]));

    // 解决 boot magic 扫描无效, TODO: 这里会增加启动时间
    for (uint8_t i = 0; i < CAL_SAMPL_CNT; i++)
        matrix_scan();
}

bool update_raw_value(uint8_t row, uint8_t col, uint16_t value) {
    if (value < VALID_ANALOG_RAW_VALUE_MIN || value > VALID_ANALOG_RAW_VALUE_MAX) return false;

    if (cali_state) {
        calibrate_values[row][col][cur_calib] = value;
        analog_key_matrix[row][col].value     = value; // for debug
        return false;
    }
#if ANALOG_AUTO_CALIBRATION_ENABLE
    auto_caliration_check(row, col, value);
#endif

    analog_key_t *k = &analog_key_matrix[row][col];

    const uint8_t raw_noise_filter = analog_raw_noise_filter();
    if (raw_noise_filter) {
        const uint16_t last_val = k->last_val;
        const uint16_t delta    = value > last_val ? value - last_val : last_val - value;
        if (delta < raw_noise_filter) return false;
    }

    k->last_val = value;
    k->value    = value;
    k->travel   = convert_to_travel(row, col, value);

#if ANALOG_PREDICTIVE_ACTUATION_IN_GAMING_MODE
    // EMA del delta descendente por scan (velocidad del dedo). Solo crece para
    // movimiento hacia abajo (travel crece = golpe); decae (factor 1/2^N) en
    // scans sin delta positivo. Sin division: shift derecho N bits. vel_ema
    // solo lo consume la prediccion, asi que el feed se compila fuera cuando
    // ANALOG_PREDICTIVE_ACTUATION_IN_GAMING_MODE == 0 (binario estable).
    {
        const uint8_t delta_down = (k->travel > k->last_travel) ? (uint8_t)(k->travel - k->last_travel) : 0;
        k->vel_ema = (uint8_t)(k->vel_ema - (k->vel_ema >> ANALOG_PREDICTIVE_EMA_SHIFT) + (delta_down >> ANALOG_PREDICTIVE_EMA_SHIFT));
    }
#endif

    const uint8_t mode = analog_matrix_effective_mode(row, col, k->mode);

    if (k->travel == k->last_travel) return false;

    bool ret = false;

    switch (mode) {
        case AKM_RAPID:
            ret = rapid_trigger_action(k);
            break;
        case AKM_DKS:
            ret = okmc_action(k);
            break;
        case AKM_GAMEPAD:
#if defined(XINPUT_ENABLE)
#    if defined(JOYSTICK_ENABLE)
            if (game_controller_xinput_enabled())
#    endif
                ret = xinput_update(k);

#    if defined(JOYSTICK_ENABLE)
            else
#    endif
#endif
#ifdef JOYSTICK_ENABLE
                ret = joystick_update(k);
#endif
            break;
        case AKM_TOGGLE:
            ret = toggle_action(k);
            break;
        default:
            ret = regular_trigger_action(k);
            break;
    }

    k->last_travel = k->travel;

    return ret;
}

uint8_t analog_matrix_get_key_mode(uint8_t row, uint8_t col) {
    return analog_key_matrix[row][col].mode;
}

bool analog_matrix_get_key_state(uint8_t row, uint8_t col) {
    analog_key_t *k = &analog_key_matrix[row][col];

    switch (analog_matrix_effective_mode(row, col, k->mode)) {
        case AKM_REGULAR: // fall through
            return (k->state == AKS_REGULAR_PRESSED);

        case AKM_RAPID:
            return (k->state == AKS_REGULAR_PRESSED || k->state == AKS_RAPID_PRESSED);

        case AKM_GAMEPAD:
            return (game_controller_type_enabled() && k->state == AKS_REGULAR_PRESSED);

        case AKM_TOGGLE:
            return k->hold;

        default:
            return false;
    }
}


bool set_calibrate(uint8_t *data) {
    uint8_t new_cali_state = data[0];

    switch (new_cali_state) {
        case CALIB_ZERO_TRAVEL_MANUAL:
            cali_state = data[0];
            cur_calib  = 0;
            memset(calibrate_values, 0, MATRIX_ROWS * MATRIX_COLS * CAL_SAMPL_CNT * sizeof(calibrate_values[0][0][0]));
            calib_ind_timer = 0;

            memset(calib_values, 0, sizeof(calib_values));
            memset(calib_state_matrix, 0, sizeof(calib_state_matrix));
            memset(manual_calib_zero_invalid, 0, sizeof(manual_calib_zero_invalid));
            break;

        case CALIB_SAVE_AND_EXIT:
            if (cali_state == CALIB_ZERO_TRAVEL_MANUAL || cali_state == CALIB_FULL_TRAVEL_MANUAL) {
                last_cali_state = cali_state;
                cali_state      = data[0];
            }
            break;

        default:
            return false;
    }

    return true;
}

bool get_realtime_travel(uint8_t *data) {
    uint8_t             i   = 1;
    uint8_t             row = data[0];
    uint8_t             col = data[1];
    extern matrix_row_t analog_raw_matrix[MATRIX_ROWS];

    if (row >= MATRIX_ROWS || col >= MATRIX_COLS) return false;

    data[i++] = row;
    data[i++] = col;
    data[i++] = analog_key_matrix[row][col].travel / TRAVEL_SCALE;
    data[i++] = analog_key_matrix[row][col].travel;
    data[i++] = analog_key_matrix[row][col].value & 0xFF;
    data[i++] = (analog_key_matrix[row][col].value >> 8) & 0xFF;
    data[i++] = calib_values[row][col].zero_travel & 0xFF;
    data[i++] = (calib_values[row][col].zero_travel >> 8) & 0xFF;
    data[i++] = calib_values[row][col].full_travel & 0xFF;
    data[i++] = (calib_values[row][col].full_travel >> 8) & 0xFF;
    data[i++] = analog_raw_matrix[row] & (0x01 << col) ? 0x11 : 0x10;

    return true;
}

#if ANALOG_BOTTOM_OUT_LEARN
/* Only-grow bottom-out learning (deeper raw ADC = lower value). Reads the
 * filtered raw values already produced by the scan; never runs in the scan
 * itself. A learned value only replaces the current one when it is deeper by
 * at least ANALOG_BOTTOM_OUT_LEARN_EPSILON, clamped to the valid sensor range,
 * so noise or a corrupt sample can never shrink the dynamic range. */
static void bottom_out_learn_task(void) {
    // analog_matrix_task() corre dentro del barrido de matriz (hot path), no
    // en housekeeping: limitar el aprendizaje a una pasada cada 50 ms para no
    // alargar el barrido (medido: la pasada completa cuesta ~10-15 us).
    static uint32_t last_learn = 0;
    if (timer_elapsed32(last_learn) < 50) return;
    last_learn = timer_read32();

    for (uint8_t r = 0; r < MATRIX_ROWS; r++) {
        if (analog_matrix_mask[r] == 0) continue;
        for (uint8_t c = 0; c < MATRIX_COLS; c++) {
            if ((analog_matrix_mask[r] & (0x01U << c)) == 0) continue;

            uint16_t raw = analog_key_matrix[r][c].value;
            if (raw == 0 || raw < VALID_ANALOG_RAW_VALUE_MIN) continue;

            uint16_t learned = raw + BOTTOM_JITTER;
            if (learned + ANALOG_BOTTOM_OUT_LEARN_EPSILON < calib_values[r][c].full_travel) {
                calib_values[r][c].full_travel       = learned;
                saved_calib_values[r][c].full_travel = learned;
                update_scale_factor(r, c);
                calibration_dirty = true;
            }
        }
    }
}
#endif

void analog_matrix_task(void) {
    calibrate();

#if ANALOG_BOTTOM_OUT_LEARN
    bottom_out_learn_task();
#endif

#if ANALOG_AUTO_CALIBRATION_ENABLE || ANALOG_BOTTOM_OUT_LEARN
    extern uint32_t last_input_activity_time(void);
    // Skip the (blocking, I2C) EEPROM flush while in gaming mode; the learned
    // values stay in RAM and get persisted on the next idle window outside it.
    if (calibration_dirty && !analog_matrix_is_gaming_mode() && timer_elapsed32(last_input_activity_time()) > 1000) {
        extern matrix_row_t analog_raw_matrix[MATRIX_ROWS];
        bool has_key = false;
        for (uint8_t i = 0; i < MATRIX_ROWS; i++) {
            if (analog_raw_matrix[i] != 0) {
                has_key = true;
                break;
            }
        }
        if (!has_key) {
            save_calibration_values();
            calibration_dirty = false;
        }
    }
#endif

    profile_indication_timer_check();
    socd_action();

    // Drain at most one OKMC action group per scan (see action_okmc.c).
    extern void okmc_deferred_task(void);
    okmc_deferred_task();
#ifdef JOYSTICK_ENABLE
    extern void joystick_action_task(void);
    joystick_action_task();
#endif
#ifdef XINPUT_ENABLE
    extern void xinput_task(void);
    xinput_task();
#endif
}

static void get_calibrate_state(uint8_t *data) {
    uint32_t rows[MATRIX_ROWS] = {0};

    for (uint8_t r = 0; r < MATRIX_ROWS; r++) {
        rows[r] = calib_state_matrix[r];
    }

    data[0] = calibrated;
    data[1] = cali_state;
    for (uint8_t i = 0; i < MATRIX_ROWS; i++)
        memcpy(data + 2 + i * 3, &rows[i], 3);
}

static bool get_calibrated_value(uint8_t row, uint8_t col, uint8_t *data) {
    uint8_t i = 0;

    if (row == 0xFF && col == 0xFF) {
        data[i++] = MATRIX_ROWS;
        data[i++] = 0;
        data[i++] = MATRIX_COLS;
        data[i++] = 0;
        return true;
    } else if (row >= MATRIX_ROWS || col >= MATRIX_COLS)
        return false;

    if (analog_matrix_mask[row] & (0x01 << col)) {
        data[i++] = saved_calib_values[row][col].zero_travel & 0xFF;
        data[i++] = (saved_calib_values[row][col].zero_travel >> 8) & 0xFF;
        data[i++] = saved_calib_values[row][col].full_travel & 0xFF;
        data[i++] = (saved_calib_values[row][col].full_travel >> 8) & 0xFF;
    } else {
        memset(data + i, 0, 4);
        i += 4;
    }
    memcpy(&data[i], &scale_factor[row][col], sizeof(float));
    i += sizeof(float);

    return true;
}

void analog_matrix_rx(uint8_t *data, uint8_t length) {
    if (length < 2 || data[0] != 0xA9) return;

    uint8_t cmd     = data[1];
    bool    success = true;

    if (analog_matrix_reject_raw_hid_in_gaming(cmd)) {
        data[2] = 1;
        raw_hid_send(data, length);
        return;
    }

    switch (cmd) {
        case AMC_GET_VERSION:
            data[2] = KC_ANALOG_MATRIX_VERSION & 0xFF;
            break;

        case AMC_GET_PROFILES_INFO:
            data[2] = profile_get_current_index();
            data[3] = PROFILE_COUNT;
            data[4] = PROFILE_SIZE & 0xFF;
            data[5] = (PROFILE_SIZE >> 8) & 0xFF;
            data[6] = OKMC_COUNT;
            data[7] = SOCD_COUNT;
            break;

        case AMC_GET_PROFILE_RAW: {
            uint8_t  index  = data[2];
            uint16_t offset = (data[4] << 8) | data[3];
            uint8_t  size   = data[5];
            if (length < 6 || size > length - 6) {
                success = false;
            } else {
                success = profile_get_raw_data(index, offset, size, &data[6]);
            }
        } break;

        case AMC_SET_PROFILE_NAME:
            if (length < 4 || length < 4 + data[3]) {
                success = false;
            } else {
                success = profile_set_name(data[2], data[3], &data[4]);
            }
            data[2] = success ? 0 : 1;
            break;

        case AMC_SELECT_PROFILE:
            success = profile_select(data[2], false, true);
            data[2] = success ? 0 : 1;
            break;

        case AMC_SET_TRAVAL: {
            if (length < 8) {
                success = false;
                data[2] = 1;
                break;
            }
            uint8_t  profile               = data[2];
            uint8_t  mode                  = data[3];
            uint8_t  act_pt                = data[4];
            uint8_t  sens                  = data[5];
            uint8_t  rls_sens              = data[6];
            bool     entire                = data[7];
            uint32_t row_mask[MATRIX_ROWS] = {0};
            if (!entire) {
                if (length < 8 + MATRIX_ROWS * 3) {
                    success = false;
                    data[2] = 1;
                    break;
                }
                for (uint8_t i = 0, j = 8; i < MATRIX_ROWS; i++, j += 3) {
                    memcpy(&row_mask[i], &data[j], 3);
                }
            }

            success = profile_set_traval(profile, mode, act_pt, sens, rls_sens, entire, row_mask);
            data[2] = success ? 0 : 1;
        } break;

        case AMC_SET_ADVANCE_MODE:
            if (length < 27) {
                success = false;
            } else {
                success = profile_set_adv_mode(&data[2]);
            }
            data[2] = success ? 0 : 1;
            break;

        case AMC_SET_SOCD:
            if (length < 9) {
                success = false;
            } else {
                success = profile_set_socd(&data[2]);
            }
            data[2] = success ? 0 : 1;
            break;

        case AMC_GET_REALTIME_TRAVEL:
            success = get_realtime_travel(&data[2]);
            data[2] = success ? 0 : 1;
            break;

        case AMC_RESET_PROFILE:
            success = profile_reset(data[2]);
            update_travel_configs();
            data[2] = success ? 0 : 1;
            break;

        case AMC_SAVE_PROFILE:
            success = profile_save(data[2]);
            data[2] = success ? 0 : 1;
            break;

        case AMC_GET_CURVE:
            game_controller_get_curve(&data[2]);
            break;

        case AMC_SET_CURVE:
            if (length < 10) {
                success = false;
            } else {
                success = game_controller_set_curve((point_t *)&data[2]);
            }
            data[2] = success ? 0 : 1;
            break;

        case AMC_GET_GAME_CONTROLLER_MODE:
            success = game_controller_mode_get(&data[2]);
            data[2] = success ? 0 : 1;
            break;

        case AMC_SET_GAME_CONTROLLER_MODE:
            success = game_controller_mode_set(data[2]);
            data[2] = success ? 0 : 1;
            break;

        case AMC_CALIBRATE:
            success = set_calibrate(&data[2]);
            data[2] = success ? 0 : 1;
            break;

        case AMC_GET_CALIBRATE_STATE:
            get_calibrate_state(&data[2]);
            break;

        case AMC_GET_CALIBRATED_VALUE:
            success = get_calibrated_value(data[2], data[3], &data[5]);
            data[4] = success ? 0 : 1;
            break;
    }

    raw_hid_send(data, length);
}

void analog_matrix_indicator(void) {
#ifdef RGB_MATRIX_ENABLE
    if (cali_state == CALIB_FULL_TRAVEL_MANUAL) {
        rgb_matrix_set_color_all(150, 0, 150);

        for (uint8_t r = 0; r < MATRIX_ROWS; r++)
            for (uint8_t c = 0; c < MATRIX_COLS; c++) {
                if ((analog_matrix_mask[r] & (0x01 << c)) == 0) continue;

                // clang-format off
                if (calib_state_matrix[r] & (0x01 << c)) {
                    // clang-format on
                    if (last_calib_row == r && last_calib_col == c && timer_elapsed32(calib_ind_timer) < 500) continue;

                    uint8_t index = g_led_config.matrix_co[r][c];
                    rgb_matrix_set_color(index, 0, 255, 0);
                } else if (manual_calib_zero_invalid[r] & (0x01 << c)) {
                    uint8_t index = g_led_config.matrix_co[r][c];
                    rgb_matrix_set_color(index, 255, 0, 0);
                }
            }
        return;
    }
#endif
    profile_indication();
}

inline matrix_row_t analog_matrix_get_row(uint8_t row) {
    return virtual_matrix[row];
}

void analog_matrix_clear(void) {
    memset(analog_key_matrix, 0, sizeof(analog_key_matrix));
}

void analog_matrix_clear_advance_keys(void) {
    extern void okmc_clear(void);
    okmc_clear();
    game_controller_clear();
}
