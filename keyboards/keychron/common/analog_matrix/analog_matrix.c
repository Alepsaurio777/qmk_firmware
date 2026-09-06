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
#include "noise_floor.h"
#include "keymap_introspection.h"
#include "raw_hid.h"
#include "eeprom.h"
#include "eeprom_he.h"
#include "usb_main.h"
#include <stdio.h>
#include "profile.h"
#include "sqrt.h"
#if ANALOG_GAME_CONTROLLER_SUPPORT
#    include "game_controller_common.h"
#endif
#include "nvm_eeprom_eeconfig_internal.h"

/* Calibration may be updated while scanning, but persistence must never be
 * attempted from that path: the external EEPROM driver contains mandatory
 * page-write delays.  Keep one coalesced request for the main-loop task. */
static bool     calibration_save_pending;
static bool     calibration_save_retry_wait;
static uint32_t calibration_save_requested_at;
static uint32_t calibration_save_generation;
static uint32_t calibration_save_attempt_generation;
static bool     calibration_save_active;

static void request_calibration_save(void) {
    calibration_save_pending = true;
    calibration_save_generation++;

    /* A new runtime value supersedes the next attempt, but must not defeat
     * the retry backoff of a page that has already failed. */
    if (!calibration_save_active) {
        calibration_save_retry_wait   = false;
        calibration_save_requested_at = timer_read32();
    } else if (!calibration_save_retry_wait) {
        calibration_save_requested_at = timer_read32();
    }
}

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
    AMC_RESET_SINGLE_KEY = 0x1D,
    AMC_RESET_PROFILE    = 0x1E,
    AMC_SAVE_PROFILE     = 0x1F,
    AMC_GET_CURVE        = 0x20,
    AMC_SET_CURVE,
    AMC_GET_GAME_CONTROLLER_MODE,
    AMC_SET_GAME_CONTROLLER_MODE,

    AMC_GET_REALTIME_TRAVEL = 0x30,

    AMC_CALIBRATE = 0x40,
    AMC_GET_CALIBRATE_STATE,
    AMC_GET_CALIBRATED_VALUE,

    AMC_GET_AXIS_TYPE = 0x50,
    AMC_SET_AXIS_TYPE = 0x51,

    // Diagnostico LAB: lectura de la fase del poll USB medida por el probe de
    // usb_driver.c. Solo entrega datos validos si el binario se compilo con
    // -DUSB_POLL_PHASE_PROBE; si no, responde status=2 (no disponible). Es un
    // GET, asi que la compuerta de Gaming lo deja pasar y se puede leer mientras
    // se juega. ID 0x60: primer hueco libre tras el bloque de switch axis.
    AMC_GET_POLL_PHASE = 0x60,

    // Diagnostico LAB: duracion y fase de fin del scan (relativa al SOF), con
    // min/max. Sirve para elegir ANALOG_SCAN_SOF_START_OFFSET_US: el scan deberia
    // TERMINAR justo antes del poll del host. Solo con -DUSB_SOF_TIMING_PROBE;
    // si no, status=2.
    AMC_GET_SCAN_PHASE = 0x61,
};

// Se intento (19-jul) invertir esto a whitelist de solo-lectura en Gaming
// (bloquear todo SET/SAVE/SELECT). REVERTIDO el mismo dia: el flujo real de
// tuning exige el modo Gaming ACTIVO (el perfil gaming solo corre ahi — se
// ajusta sensibilidad en Launcher y se siente en vivo), y el lockdown lo
// rompia. Queda la blacklist minima original: calibrar y resetear perfil son
// las dos operaciones sin caso de uso legitimo a mitad de partida. Si algun
// dia vuelve el endurecimiento, la via es la "escotilla de tuning" del
// ROADMAP (desbloqueo deliberado por 0xEE con timeout), no esta funcion.
static inline bool analog_matrix_reject_raw_hid_in_gaming(uint8_t cmd) {
    if (analog_matrix_is_gaming_mode()) {
        switch (cmd) {
            case AMC_CALIBRATE:
            case AMC_RESET_PROFILE:
                return true;
            default:
                break;
        }
    }
    return false;
}

extern const matrix_row_t analog_matrix_mask[];
extern const matrix_row_t okmc_matrix[MATRIX_ROWS];
extern matrix_row_t       virtual_matrix[MATRIX_ROWS];

extern bool regular_trigger_action(analog_key_t *key);
extern bool okmc_action(analog_key_t *key);
extern bool rapid_trigger_action(analog_key_t *key);
extern bool toggle_action(analog_key_t *key);
#if ANALOG_GAME_CONTROLLER_SUPPORT
extern bool xinput_update(analog_key_t *key);
extern bool joystick_update(analog_key_t *key);
#endif
extern void socd_action(void);

static calibrated_value_t calib_values[MATRIX_ROWS][MATRIX_COLS];
static calibrated_value_t saved_calib_values[MATRIX_ROWS][MATRIX_COLS];
static calibrated_value_t calibration_save_snapshot[MATRIX_ROWS][MATRIX_COLS];
static he_eeprom_cal_save_t calibration_save_transaction;
analog_key_t       analog_key_matrix[MATRIX_ROWS][MATRIX_COLS];

static uint16_t      calibrate_values[MATRIX_ROWS][MATRIX_COLS][CAL_SAMPL_CNT];
#if ANALOG_AUTO_CALIBRATION_ENABLE
static calibration_t auto_calib[MATRIX_ROWS][MATRIX_COLS];
#endif
static uint8_t       cali_state = CALIB_OFF;
static uint8_t       last_cali_state;
static uint8_t       cur_calib = 0;
static uint8_t       power_on_calibration_retries;
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

static void begin_power_on_calibration(void) {
    power_on_calibration_retries = 0;
    cur_calib                    = 0;
    memset(calibrate_values, 0, sizeof(calibrate_values));
    memset(manual_calib_zero_invalid, 0, sizeof(manual_calib_zero_invalid));
    cali_state = CALIB_ZERO_TRAVEL_POWER_ON;
}

uint32_t debug_interval = 0;

#ifndef ANALOG_RUNTIME_CONFIG_CACHE
#    define ANALOG_RUNTIME_CONFIG_CACHE 0
#endif

#if ANALOG_RUNTIME_CONFIG_CACHE
static bool    analog_runtime_gaming_mode;
static uint8_t analog_runtime_raw_noise_filter;
static uint8_t analog_runtime_top_out_deadzone;
#endif

#if ANALOG_STARTUP_NOISE_FLOOR_ENABLE
// Threshold por tecla + bitmap de validez. Una tecla invalida/held durante el
// boot ya no invalida las demas; simplemente usa el filtro estatico. Congelado
// hasta reboot: no hay aprendizaje en background.
static uint8_t      startup_noise_filter[MATRIX_ROWS][MATRIX_COLS];
static matrix_row_t startup_noise_valid_mask[MATRIX_ROWS];

static void analog_capture_startup_noise_floor(void) {
    memset(startup_noise_valid_mask, 0, sizeof(startup_noise_valid_mask));

    for (uint8_t r = 0; r < MATRIX_ROWS; r++) {
        for (uint8_t c = 0; c < MATRIX_COLS; c++) {
            const matrix_row_t bit = (matrix_row_t)1 << c;
            if ((analog_matrix_mask[r] & bit) == 0) continue;

            // saved_calib_values contiene la referencia de release previa a
            // esta calibracion de power-on. zero_travel se almacena despues de
            // restar ZERO_TRAVEL_DEAD_ZONE; las muestras ADC son raw, por eso
            // se suma aqui para comparar en el mismo dominio.
            const uint32_t expected32 = (uint32_t)saved_calib_values[r][c].zero_travel + ZERO_TRAVEL_DEAD_ZONE;
            if (expected32 > UINT16_MAX) continue;

            uint8_t learned = 0;
            if (!analog_startup_noise_filter_if_released(calibrate_values[r][c], CAL_SAMPL_CNT, (uint16_t)expected32, ANALOG_STARTUP_RELEASE_WINDOW_RAW, &learned)) continue;

            startup_noise_filter[r][c] = learned;
            startup_noise_valid_mask[r] |= bit;
        }
    }
}
#endif

uint8_t analog_matrix_get_travel(uint8_t row, uint8_t col) {
    return analog_key_matrix[row][col].travel;
}

// (1-ago) Variante con guardarrail de rango. La cruda de arriba NO valida
// indices, a proposito: el barrido itera por construccion y pagar dos
// comparaciones por tecla y por barrido ahi no tiene sentido.
//
// El problema estaba fuera del barrido. Hay llamantes cuyo indice puede ser 0xFF
// de forma legitima —un slot de whitelist cuyo keycode no esta en la capa, un
// evento virtual de macro con row/col centinela— y cada uno se estaba acordando
// de comprobarlo a mano. Seis sitios que tienen que acordarse; uno que se olvide
// lee fuera de rango. Ya paso una vez y costo un commit dedicado (c42acf7).
//
// Regla: si el indice VIENE de la matriz, usa la cruda; si viene de una
// resolucion que puede fallar, usa esta.
//
// Deliberadamente NO cubre a quien debe RECHAZAR en vez de sustituir por 0:
// socd_action() valida su config y get_realtime_travel() valida el paquete
// entrante. Ahi un 0 silencioso seria peor que el rechazo.
uint8_t analog_matrix_get_travel_checked(uint8_t row, uint8_t col) {
    if (row >= MATRIX_ROWS || col >= MATRIX_COLS) return 0;
    return analog_key_matrix[row][col].travel;
}

#if !ANALOG_RUNTIME_CONFIG_CACHE
#    if ANALOG_DISABLE_OKMC_IN_GAMING_MODE || ANALOG_DISABLE_TOGGLE_IN_GAMING_MODE || ANALOG_DISABLE_GAMEPAD_IN_GAMING_MODE
static inline uint8_t analog_matrix_base_mode(uint8_t row, uint8_t col) {
    analog_matrix_profile_t *cur_prof = profile_get_current();
    analog_key_config_t *    key_cfg  = &cur_prof->key_config[row][col];
    return key_cfg->mode == AKM_GLOBAL ? cur_prof->global.mode : key_cfg->mode;
}

static inline uint8_t analog_matrix_effective_mode(uint8_t row, uint8_t col, uint8_t mode) {
    switch (mode) {
#        if ANALOG_DISABLE_OKMC_IN_GAMING_MODE
        case AKM_DKS:
#        endif
#        if ANALOG_DISABLE_TOGGLE_IN_GAMING_MODE
        case AKM_TOGGLE:
#        endif
#        if ANALOG_DISABLE_GAMEPAD_IN_GAMING_MODE
        case AKM_GAMEPAD:
#        endif
            return analog_matrix_is_gaming_mode() ? analog_matrix_base_mode(row, col) : mode;
        default:
            return mode;
    }
}
#    else
static inline uint8_t analog_matrix_effective_mode(uint8_t row, uint8_t col, uint8_t mode) {
    (void)row;
    (void)col;
    return mode;
}
#    endif
#endif

static uint8_t convert_to_travel(uint8_t row, uint8_t col, uint16_t value) {
    uint16_t travel;
    calibrated_value_t *p_calib = &calib_values[row][col];

    int32_t x = (int32_t)value - (int32_t)p_calib->zero_travel + REF_ZERO_TRAVEL;
    if (x < 0 || x > REF_ZERO_TRAVEL) return 0;

#if TOP_OUT_DEAD_ZONE_GAMING || TOP_OUT_DEAD_ZONE_TYPING
    const uint8_t top_out_deadzone =
#if ANALOG_RUNTIME_CONFIG_CACHE
        analog_runtime_top_out_deadzone;
#else
        analog_matrix_is_gaming_mode() ? TOP_OUT_DEAD_ZONE_GAMING : TOP_OUT_DEAD_ZONE_TYPING;
#endif
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
#if ANALOG_RUNTIME_CONFIG_CACHE
    return analog_runtime_raw_noise_filter;
#else
    return analog_matrix_is_gaming_mode() ? ANALOG_RAW_NOISE_FILTER_GAMING : ANALOG_RAW_NOISE_FILTER_TYPING;
#endif
}

static inline uint8_t analog_raw_noise_filter_for_key(uint8_t row, uint8_t col) {
#if ANALOG_STARTUP_NOISE_FLOOR_ENABLE
    const bool gaming =
#    if ANALOG_RUNTIME_CONFIG_CACHE
        analog_runtime_gaming_mode;
#    else
        analog_matrix_is_gaming_mode();
#    endif
    if (gaming) {
        const matrix_row_t bit = (matrix_row_t)1 << col;
        if (startup_noise_valid_mask[row] & bit) return startup_noise_filter[row][col];
    }
#else
    (void)row;
    (void)col;
#endif
    return analog_raw_noise_filter();
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

    // Update basic mode. base_mode se conserva para resolver una sola vez el
    // fallback de modos avanzados en Gaming cuando la cache V4 esta activa.
    const uint8_t base_mode = p_key_cfg->mode == AKM_GLOBAL ? cur_prof->global.mode : p_key_cfg->mode;
    p_key->mode = base_mode;

    // Update actuaction point
    if (p_key_cfg->act_pt == 0)
        p_key->regular.actn_pt = cur_prof->global.act_pt;
    else
        p_key->regular.actn_pt = p_key_cfg->act_pt;

    // Update deactuaction point
    const bool gaming_mode =
#if ANALOG_RUNTIME_CONFIG_CACHE
        analog_runtime_gaming_mode;
#else
        analog_matrix_is_gaming_mode();
#endif
    uint8_t    static_hysteresis = gaming_mode ? STATIC_HYSTERESIS_GAMING : STATIC_HYSTERESIS_TYPING;

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

    // Update advance mode information.
    //
    // (1-ago) Aqui vivia un guardado+restauracion de rpd_trig_sen: estos tres
    // campos compartian byte con el en una union, asi que escribir okmc_idx /
    // js_axis / hold pisaba la sensibilidad del rapid trigger cuando Gaming
    // degradaba un modo avanzado a su modo base. Al des-unionarlos
    // (analog_matrix_type.h) el problema deja de existir en la estructura y la
    // curita sobra. Bonus: se ahorra la llamada a analog_matrix_effective_mode()
    // que la condicion de restauracion hacia en cada reconfiguracion de tecla.
    if (p_key_cfg->adv_mode == AKM_DKS && p_key_cfg->okmc_idx < OKMC_COUNT) {
        p_key->mode = AKM_DKS;
        p_key->okmc_idx = p_key_cfg->okmc_idx;
#if ANALOG_GAME_CONTROLLER_SUPPORT
    } else if (p_key_cfg->adv_mode == AKM_GAMEPAD && p_key_cfg->js_axis < GC_BUTTON_MAX && p_key_cfg->js_axis != GC_MAX) {
        p_key->mode = AKM_GAMEPAD;
        p_key->js_axis = p_key_cfg->js_axis;
#endif
    } else if (p_key_cfg->adv_mode == AKM_TOGGLE) {
        p_key->mode = AKM_TOGGLE;
        p_key->hold = 0;
    }

#if ANALOG_RUNTIME_CONFIG_CACHE
    p_key->effective_mode = p_key->mode;
    if (analog_runtime_gaming_mode) {
        switch (p_key->mode) {
#    if ANALOG_DISABLE_OKMC_IN_GAMING_MODE
            case AKM_DKS:
#    endif
#    if ANALOG_DISABLE_TOGGLE_IN_GAMING_MODE
            case AKM_TOGGLE:
#    endif
#    if ANALOG_DISABLE_GAMEPAD_IN_GAMING_MODE
            case AKM_GAMEPAD:
#    endif
                p_key->effective_mode = base_mode;
                break;
            default:
                break;
        }
    }
#endif
}

void update_travel_configs(void) {
#if ANALOG_RUNTIME_CONFIG_CACHE
    analog_runtime_gaming_mode      = analog_matrix_is_gaming_mode();
    analog_runtime_raw_noise_filter = analog_runtime_gaming_mode ? ANALOG_RAW_NOISE_FILTER_GAMING : ANALOG_RAW_NOISE_FILTER_TYPING;
    analog_runtime_top_out_deadzone = analog_runtime_gaming_mode ? TOP_OUT_DEAD_ZONE_GAMING : TOP_OUT_DEAD_ZONE_TYPING;
#endif
    for (uint8_t r = 0; r < MATRIX_ROWS; r++) {
        for (uint8_t c = 0; c < MATRIX_COLS; c++) {
            update_key_config(r, c);
        }
    }

    // Las whitelists por keycode (continuous RT, RT predictivo, release-stretch)
    // se recolocan aqui: es el unico punto que ya corre en boot, cambio de
    // perfil y cambio de modo. No-op textual si ninguna esta compilada.
    analog_matrix_resolve_policy_keys();
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

bool analog_matrix_eeprom_update(const void *buf, void *addr, size_t len) {
    if ((uintptr_t)addr + len > EECONFIG_SIZE_ANALOG_MATRIX) {
        return false;
    }
    uint8_t *dst = (uint8_t *)addr + EECONFIG_BASE_ANALOG_MATRIX;
    eeprom_update_block(buf, dst, len);
    return true;
}

/* The internal EEPROM mirror is committed only after the external marker has
 * been restored.  Use the same stable snapshot that was sent to the external
 * device so a later runtime update cannot create a mixed payload. */
static void commit_calibration_snapshot(const calibrated_value_t *snapshot, uint8_t snapshot_calibrated) {
    if (!eeconfig_is_kb_datablock_valid()) eeprom_update_dword(EECONFIG_KEYBOARD, (EECONFIG_KB_DATA_VERSION));

    if (snapshot_calibrated) {
        uint8_t invalid_calibration = 0;
        analog_matrix_eeprom_update(&invalid_calibration, OFFSET_CALIBRATION, 1);
        analog_matrix_eeprom_update(snapshot, (uint8_t *)OFFSET_CALIBRATED_DATA_START, sizeof(calibration_save_snapshot));
        analog_matrix_eeprom_update(&snapshot_calibrated, OFFSET_CALIBRATION, 1);
    } else {
        analog_matrix_eeprom_update(&snapshot_calibrated, OFFSET_CALIBRATION, 1);
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

#if ANALOG_STARTUP_NOISE_FLOOR_ENABLE
        // Per-key acceptance: one held/invalid key must not discard good noise
        // measurements from the rest of the Hall matrix. Each key still needs
        // an independent plausible-release check before becoming adaptive.
        if (cali_state == CALIB_ZERO_TRAVEL_POWER_ON) {
            analog_capture_startup_noise_floor();
        }
#endif

        if (cali_state == CALIB_ZERO_TRAVEL_MANUAL) {
            cali_state = CALIB_FULL_TRAVEL_MANUAL;
        } else if (valid && (calibrated & CALI_ZERO_TRAVEL) == 0) {
            for (uint8_t r = 0; r < MATRIX_ROWS; r++)
                for (uint8_t c = 0; c < MATRIX_COLS; c++) {
                    saved_calib_values[r][c].zero_travel = calib_values[r][c].zero_travel;
                }

            update = true;
        }

        if (valid) {
            calibrated |= CALI_ZERO_TRAVEL;
        } else if (cali_state == CALIB_ZERO_TRAVEL_POWER_ON && power_on_calibration_retries < ANALOG_POWER_ON_CALIBRATION_RETRY_COUNT) {
            /* A key can still be settling or physically held when Windows
             * brings USB up. Retry a complete release window instead of
             * accepting a bad baseline or aborting calibration permanently. */
            power_on_calibration_retries++;
            cur_calib = 0;
            memset(calibrate_values, 0, sizeof(calibrate_values));
            memset(manual_calib_zero_invalid, 0, sizeof(manual_calib_zero_invalid));
            /* The validation above may have updated otherwise-good keys before
             * discovering the bad one. Never let that partial window become
             * the baseline used by the next attempt. */
            memcpy(calib_values, saved_calib_values, sizeof(calib_values));
            update_scale_factors();
            return false;
        } else {
            /* Bounded fallback: retain the last known-good/default calibration
             * and let the startup guard decide when reports may be emitted. */
            memcpy(calib_values, saved_calib_values, sizeof(calib_values));
            update_scale_factors();
            cali_state = CALIB_OFF;
        }
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
        /* Apply new values immediately, but defer all persistence.  In
         * particular, power-on zero calibration is a runtime temperature
         * correction and must not cause an EEPROM write on every boot. */
        if (cali_state != CALIB_ZERO_TRAVEL_POWER_ON) request_calibration_save();
        update_default_travel();
        update_travel_configs();
        update_scale_factors();

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
                        /* Persist later from housekeeping; this path runs for
                         * every ADC sample and must remain scan-safe. */
                        saved_calib_values[row][col].zero_travel = calib_values[row][col].zero_travel;
                        saved_calib_values[row][col].full_travel = calib_values[row][col].full_travel;
                        request_calibration_save();
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

    // Migracion de layout de EEPROM: si la version del datablock guardado no
    // coincide con la del firmware (p.ej. tras el desplazamiento de offset de
    // ead3fa0), cargar los perfiles guardados los interpretaria desalineados y
    // los corromperia en silencio — y los perfiles no tienen la red de clamps
    // que sanea la calibracion mas abajo. Tratarlo como reset: cargar perfiles
    // default y sellar la version nueva. Una calibracion desalineada la sanean
    // los clamps de rango + la recalibracion de reposo por boot; recalibrar en
    // Launcher tras un cambio de version es lo recomendado.
    bool migrate_layout = false;
    if (!eeconfig_is_kb_datablock_valid()) {
        reset_profiles = true;
        migrate_layout = true;
    }

    profile_init(reset_profiles);

    // Sellar la version DESPUES de escribir los perfiles default (atomicidad):
    // si se corta la alimentacion a mitad de la migracion, la version vieja
    // sigue en EEPROM y el siguiente boot repite la migracion completa, en vez
    // de aceptar como valida una mezcla de perfiles a medio escribir.
    if (migrate_layout) eeprom_update_dword(EECONFIG_KEYBOARD, (EECONFIG_KB_DATA_VERSION));

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
        begin_power_on_calibration();
        return;
    }
    memset(buf, 0, EECONFIG_SIZE_ANALOG_MATRIX);

    eeprom_read_block(buf, (void *)EECONFIG_BASE_ANALOG_MATRIX, EECONFIG_SIZE_ANALOG_MATRIX);

#if ANALOG_GAME_CONTROLLER_SUPPORT
    // Load curve points / game-controller mode only in builds that expose it.
    point_t curve[CURVE_POINTS_COUNT];
    memcpy(curve, buf + OFFSET_CURVE_PTS_START, CURVE_POINTS_COUNT * SIZE_OF_POINT_T);
    game_controller_curve_init(curve);
    game_controller_mode_init(buf[OFFSET_GAME_CONTROLLER_MODE_START]);
#endif

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
    begin_power_on_calibration();

    free(buf);
}

void analog_matrix_init(void) {
    he_eeprom_driver_init();

    analog_matrix_eeconfig_init();

    calibration_save_pending       = false;
    calibration_save_retry_wait    = false;
    calibration_save_requested_at  = 0;
    calibration_save_generation    = 0;
    calibration_save_attempt_generation = 0;
    calibration_save_active        = false;
    memset(&calibration_save_transaction, 0, sizeof(calibration_save_transaction));
    cur_calib = 0;
    memset(calibrate_values, 0, MATRIX_ROWS * MATRIX_COLS * CAL_SAMPL_CNT * sizeof(calibrate_values[0][0][0]));

    // 解决 boot magic 扫描无效, TODO: 这里会增加启动时间
    for (uint8_t i = 0; i < CAL_SAMPL_CNT; i++)
        matrix_scan();
}

bool analog_matrix_calibrating(void) {
    return cali_state != CALIB_OFF;
}

void update_raw_value(uint8_t row, uint8_t col, uint16_t value) {
    static uint8_t invalid_adc_count[MATRIX_ROWS][MATRIX_COLS];

    if (value < VALID_ANALOG_RAW_VALUE_MIN || value > VALID_ANALOG_RAW_VALUE_MAX) {
        // Descartar muestras transitorias fuera de rango. Si persisten muestras
        // invalidas consecutivas en una tecla activa, liberarla de forma segura
        // para prevenir teclas atascadas (ghost presses) ante fallo o ruido de sensor.
        if (analog_matrix_get_key_state(row, col)) {
            if (++invalid_adc_count[row][col] >= 8) {
                analog_key_t *k = &analog_key_matrix[row][col];
                k->state        = AKS_REGULAR_RELEASED;
                k->travel       = 0;
                k->last_travel  = 0;
                k->vel_ema      = 0;
                invalid_adc_count[row][col] = 0;
            }
        }
        return;
    }
    invalid_adc_count[row][col] = 0;

    if (cali_state) {
        calibrate_values[row][col][cur_calib] = value;
        analog_key_matrix[row][col].value     = value; // for debug
        return;
    }

#if ANALOG_STARTUP_GUARD_ENABLE
    /* During the post-init warm-up, keep tracking a plausible sensor value but
     * do not let a transient sample advance any trigger FSM or virtual output.
     * Reset last_travel so a key held through the guard is evaluated normally
     * on the first released scan. */
    if (analog_matrix_startup_quiet()) {
        analog_key_t *k = &analog_key_matrix[row][col];
        k->last_val     = 0;
        k->value        = value;
        k->travel       = convert_to_travel(row, col, value);
        k->last_travel  = 0;
        k->state        = AKS_REGULAR_RELEASED;
        k->hold         = 0;
        k->vel_ema      = 0;
        return;
    }
#endif

#if ANALOG_AUTO_CALIBRATION_ENABLE
    auto_caliration_check(row, col, value);
#endif

    analog_key_t *k = &analog_key_matrix[row][col];

    const uint8_t raw_noise_filter = analog_raw_noise_filter_for_key(row, col);
    if (raw_noise_filter) {
        const uint16_t last_val = k->last_val;
        const uint16_t delta    = value > last_val ? value - last_val : last_val - value;
        if (delta < raw_noise_filter) {
#if ANALOG_PREDICTIVE_ACTUATION_IN_GAMING_MODE
            // Scan silencioso (delta bajo el filtro): decaer vel_ema igual.
            // Sin esto, la velocidad del ultimo golpe quedaba congelada durante
            // el hold/reposo y un movimiento pequeño posterior la heredaba,
            // anticipando la actuacion predictiva de mas. Decay minimo de 1:
            // con v -= v>>N, los valores 1..(2^N - 1) nunca decaen (3>>2 == 0)
            // y el residuo empuja el umbral (3 + delta chico cruza
            // MIN_VELOCITY=4 cuando desde cero no lo haria).
            {
                uint8_t vel_dec = k->vel_ema >> ANALOG_PREDICTIVE_EMA_SHIFT;
                if (vel_dec == 0 && k->vel_ema) vel_dec = 1;
                k->vel_ema -= vel_dec;
            }
#endif
            return;
        }
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
        // Decay minimo de 1 cuando vel_ema>0 (mismo motivo que en el filtro de
        // ruido): drena los residuos 1..3 que v>>2 dejaria vivos para siempre.
        uint8_t vel_dec = k->vel_ema >> ANALOG_PREDICTIVE_EMA_SHIFT;
        if (vel_dec == 0 && k->vel_ema) vel_dec = 1;
        k->vel_ema = (uint8_t)(k->vel_ema - vel_dec + (delta_down >> ANALOG_PREDICTIVE_EMA_SHIFT));
    }
#endif

    if (k->travel == k->last_travel) return;

    // (1-ago) Movido DEBAJO del early return. Estaba encima, asi que se calculaba
    // para CADA tecla y CADA barrido solo para tirarlo: `mode` unicamente se usa
    // en el switch de aqui abajo, y en la inmensa mayoria de barridos el travel
    // no cambia. Y no es gratis: con los tres ANALOG_DISABLE_*_IN_GAMING_MODE a 1
    // esta es la version real de la funcion — tres ramas mas una posible llamada
    // a analog_matrix_base_mode(), que a su vez llama a profile_get_current(),
    // funcion externa que el compilador no puede demostrar libre de efectos y
    // por tanto probablemente no hundia por si solo.
    const uint8_t mode =
#if ANALOG_RUNTIME_CONFIG_CACHE
        k->effective_mode;
#else
        analog_matrix_effective_mode(row, col, k->mode);
#endif

    switch (mode) {
        case AKM_RAPID:
            rapid_trigger_action(k);
            break;
        case AKM_DKS:
            okmc_action(k);
            break;
#if ANALOG_GAME_CONTROLLER_SUPPORT
        case AKM_GAMEPAD:
#    if defined(XINPUT_ENABLE)
#        if defined(JOYSTICK_ENABLE)
            if (game_controller_xinput_enabled())
#        endif
                xinput_update(k);

#        if defined(JOYSTICK_ENABLE)
            else
#        endif
#    endif
#    ifdef JOYSTICK_ENABLE
                joystick_update(k);
#    endif
            break;
#endif
        case AKM_TOGGLE:
            toggle_action(k);
            break;
        default:
            regular_trigger_action(k);
            break;
    }

    k->last_travel = k->travel;
}

uint8_t analog_matrix_get_key_mode(uint8_t row, uint8_t col) {
    return analog_key_matrix[row][col].mode;
}

bool analog_matrix_get_key_state(uint8_t row, uint8_t col) {
    analog_key_t *k = &analog_key_matrix[row][col];

    switch (
#if ANALOG_RUNTIME_CONFIG_CACHE
        k->effective_mode
#else
        analog_matrix_effective_mode(row, col, k->mode)
#endif
    ) {
        case AKM_REGULAR: // fall through
            return (k->state == AKS_REGULAR_PRESSED);

        case AKM_RAPID:
            return (k->state == AKS_REGULAR_PRESSED || k->state == AKS_RAPID_PRESSED);

#if ANALOG_GAME_CONTROLLER_SUPPORT
        case AKM_GAMEPAD:
            return (game_controller_type_enabled() && k->state == AKS_REGULAR_PRESSED);

#endif
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
 * filtered raw values already produced by the scan. It runs from housekeeping,
 * never from the scan itself. A learned value only replaces the current one
 * when it is deeper by at least ANALOG_BOTTOM_OUT_LEARN_EPSILON, clamped to the
 * valid sensor range, so noise or a corrupt sample can never shrink the dynamic
 * range. */
static void bottom_out_learn_task(void) {
    // Limitar el aprendizaje a una pasada cada 50 ms para acotar el trabajo de
    // housekeeping (la pasada completa cuesta ~10-15 us).
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
                request_calibration_save();
            }
        }
    }
}
#endif

static bool calibration_save_allowed(void) {
    if (analog_matrix_calibrating()) return false;
    if (analog_matrix_is_gaming_mode()) return false;

    extern uint32_t last_input_activity_time(void);
    if (timer_elapsed32(last_input_activity_time()) < ANALOG_CALIBRATION_SAVE_IDLE_MS) return false;

    extern matrix_row_t analog_raw_matrix[MATRIX_ROWS];
    for (uint8_t i = 0; i < MATRIX_ROWS; i++) {
        if (analog_raw_matrix[i] != 0) return false;
    }
    return true;
}

static void calibration_save_task(void) {
    if (!calibration_save_pending && !calibration_save_active) return;

    const uint32_t now = timer_read32();

    if (!calibration_save_active) {
        if (timer_elapsed32(calibration_save_requested_at) < ANALOG_CALIBRATION_SAVE_DELAY_MS) return;
        if (!calibration_save_allowed()) return;

        /* This snapshot remains untouched until every page and the final
         * marker have succeeded.  Runtime calibration may continue updating
         * saved_calib_values while the cooperative transaction is suspended. */
        memcpy(calibration_save_snapshot, saved_calib_values, sizeof(calibration_save_snapshot));
        const uint8_t snapshot_calibrated = calibrated;
        calibration_save_attempt_generation = calibration_save_generation;
        he_eeprom_cal_save_begin(&calibration_save_transaction, snapshot_calibrated ? calibration_save_snapshot : NULL, snapshot_calibrated ? sizeof(calibration_save_snapshot) : 0, (const void *)(EXTERNAL_EEPROM_OFFSET + OFFSET_CALIBRATED_DATA_START), (const void *)(EXTERNAL_EEPROM_OFFSET + OFFSET_CALIBRATION), snapshot_calibrated, snapshot_calibrated || eeprom_calibrated != snapshot_calibrated, 0, snapshot_calibrated);
        calibration_save_active = true;
    }

    if (calibration_save_retry_wait) {
        if (timer_elapsed32(calibration_save_requested_at) < ANALOG_CALIBRATION_SAVE_RETRY_MS) return;
        if (!calibration_save_allowed()) return;
        he_eeprom_cal_save_retry(&calibration_save_transaction);
        calibration_save_retry_wait = false;
    }

    /* The permission gate is checked again for every page/ACK continuation.
     * A gaming transition or a fresh input therefore pauses the transaction
     * before the next I2C operation instead of merely blocking its start. */
    if (!calibration_save_allowed()) return;

    const he_eeprom_cal_save_status_t status = he_eeprom_cal_save_step(&calibration_save_transaction, true, now);
    if (status == HE_EEPROM_CAL_SAVE_FAILED) {
        calibration_save_retry_wait   = true;
        calibration_save_requested_at = timer_read32();
        return;
    }
    if (status != HE_EEPROM_CAL_SAVE_COMPLETE) return;

    const bool snapshot_is_current = calibration_save_attempt_generation == calibration_save_generation;
    eeprom_calibrated               = calibration_save_transaction.final_marker;
    calibration_save_active         = false;
    calibration_save_retry_wait     = false;

    if (snapshot_is_current) {
        commit_calibration_snapshot(&calibration_save_snapshot[0][0], calibration_save_transaction.final_marker);
        calibration_save_pending = false;
    } else {
        /* A request that arrived during the write remains pending and will
         * take a fresh snapshot on the next coalescing delay. */
        calibration_save_requested_at = timer_read32();
    }
}

/* Work that must happen before matrix_common compares/debounces raw_matrix.
 * Keep this function bounded and free of EEPROM/I2C or other deferred output
 * work. */
void analog_matrix_scan_task(void) {
    calibrate();

    /* SOCD masks raw_matrix and therefore has to run before matrix_scan_custom
     * returns to QMK's debounce/report path. */
    socd_action();
}

/* Work scheduled from the main loop after the matrix scan.  In particular,
 * EEPROM page writes live here, never in the scan hot path. */
void analog_matrix_housekeeping_task(void) {

#if ANALOG_BOTTOM_OUT_LEARN
    bottom_out_learn_task();
#endif

    calibration_save_task();

    profile_indication_timer_check();

    // Drain at most one OKMC action group per main-loop turn (see
    // action_okmc.c). Keeping this out of the scan prevents virtual HID work
    // from extending the analog deadline.
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

/* Kept for callers in older keymaps. The old all-in-one entry point is now
 * scan-safe; the deferred half is wired through keychron_task.c. */
void analog_matrix_task(void) {
    analog_matrix_scan_task();
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
    // Todos los comandos usan data[2] como primer byte de payload o estado.
    // Raw HID normal entrega reportes de tamano fijo, pero validar aqui evita
    // lecturas/escrituras fuera de rango si esta funcion recibe un buffer corto
    // desde un transporte futuro, un test o un host malformado.
    if (length < 3 || data[0] != 0xA9) return;

    uint8_t cmd     = data[1];
    bool    success = true;

    if (analog_matrix_reject_raw_hid_in_gaming(cmd)) {
        data[2] = 1;
        raw_hid_send(data, length);
        return;
    }

    switch (cmd) {
        case AMC_GET_VERSION:
            if (length < 5) {
                success = false;
                break;
            }
            data[2] = KC_ANALOG_MATRIX_VERSION & 0xFF;
            data[3] = 0;
            data[4] = 0x12; // feature bitmask: resetSingleKey (0x02), gamepadDisable (0x10). DKR and Turbo (bits 2, 3) not supported in profile_set_adv_mode.
            break;

        case AMC_GET_PROFILES_INFO:
            if (length < 8) {
                success = false;
                break;
            }
            data[2] = profile_get_current_index();
            data[3] = PROFILE_COUNT;
            data[4] = PROFILE_SIZE & 0xFF;
            data[5] = (PROFILE_SIZE >> 8) & 0xFF;
            data[6] = OKMC_COUNT;
            data[7] = SOCD_COUNT;
            break;

        case AMC_GET_PROFILE_RAW: {
            if (length < 6) {
                success = false;
                break;
            }
            uint8_t  index       = data[2];
            uint16_t offset      = ((uint16_t)data[4] << 8) | data[3];
            uint8_t  size        = data[5];
            uint8_t  max_payload = (length >= 6) ? (length - 6) : 0;
            uint8_t  copy_size   = (size > max_payload) ? max_payload : size;
            success = profile_get_raw_data(index, offset, copy_size, &data[6]);
            if (max_payload > copy_size) {
                memset(&data[6 + copy_size], 0, max_payload - copy_size);
            }
            // Retain data[5] (requested size, e.g. 30) for Keychron Launcher pipe filter compatibility.
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
            if (length < 3) {
                success = false;
                break;
            }
            success = profile_select(data[2], false, true);
            data[2] = success ? 0 : 1;
            if (length > 3) {
                data[3] = profile_get_current_index();
            }
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
            if (length < 14) {
                success = false;
            } else {
                success = get_realtime_travel(&data[2]);
            }
            data[2] = success ? 0 : 1;
            break;

        case AMC_RESET_SINGLE_KEY: {
            uint8_t prof_idx = data[2];
            if (prof_idx < PROFILE_COUNT && length >= 3 + MATRIX_ROWS * 3) {
                analog_matrix_profile_t *prof = profile_get(prof_idx);
                for (uint8_t r = 0; r < MATRIX_ROWS; r++) {
                    uint32_t mask = 0;
                    memcpy(&mask, &data[3 + r * 3], 3);
                    for (uint8_t c = 0; c < MATRIX_COLS; c++) {
                        if (mask & (1U << c)) {
                            memset(&prof->key_config[r][c], 0, sizeof(analog_key_config_t));
                            if (prof_idx == profile_get_current_index()) {
                                update_key_config(r, c);
                            }
                        }
                    }
                }
                data[2] = 0;
            } else {
                data[2] = 1;
            }
        } break;

        case AMC_RESET_PROFILE:
            if (length < 3) {
                success = false;
                break;
            }
            success = profile_reset(data[2]);
            update_travel_configs();
            data[2] = success ? 0 : 1;
            break;

        case AMC_SAVE_PROFILE:
            if (length < 3) {
                success = false;
                break;
            }
            success = profile_save(data[2]);
            if (success) {
                profile_select(data[2], false, true);
            }
            data[2] = success ? 0 : 1;
            if (length > 3) {
                data[3] = profile_get_current_index();
            }
            break;

        case AMC_GET_CURVE:
#if ANALOG_GAME_CONTROLLER_SUPPORT
            if (length < 2 + CURVE_POINTS_COUNT * SIZE_OF_POINT_T) {
                success = false;
            } else {
                success = game_controller_get_curve(&data[2]);
            }
#else
            success = false;
#endif
            break;

        case AMC_SET_CURVE:
#if ANALOG_GAME_CONTROLLER_SUPPORT
            if (length < 10) {
                success = false;
            } else {
                success = game_controller_set_curve((point_t *)&data[2]);
            }
#else
            success = false;
#endif
            if (length >= 3) data[2] = success ? 0 : 1;
            break;

        case AMC_GET_GAME_CONTROLLER_MODE:
#if ANALOG_GAME_CONTROLLER_SUPPORT
            if (length < 4) {
                success = false;
            } else {
                success = game_controller_mode_get(&data[2]);
            }
#else
            success = false;
#endif
            if (length >= 3) data[2] = success ? 0 : 1;
            break;

        case AMC_SET_GAME_CONTROLLER_MODE:
#if ANALOG_GAME_CONTROLLER_SUPPORT
            success = game_controller_mode_set(data[2]);
#else
            success = false;
#endif
            if (length >= 3) data[2] = success ? 0 : 1;
            break;

        case AMC_CALIBRATE:
            if (length < 3) {
                success = false;
                break;
            }
            success = set_calibrate(&data[2]);
            data[2] = success ? 0 : 1;
            break;

        case AMC_GET_CALIBRATE_STATE:
            if (length < 4 + MATRIX_ROWS * 3) {
                success = false;
            } else {
                get_calibrate_state(&data[2]);
            }
            break;

        case AMC_GET_CALIBRATED_VALUE:
            if (length < 5 + 4 + sizeof(float)) {
                success = false;
            } else {
                success = get_calibrated_value(data[2], data[3], &data[5]);
            }
            if (length >= 5) data[4] = success ? 0 : 1;
            break;

        case AMC_GET_AXIS_TYPE:
            if (length < 5) {
                success = false;
                break;
            }
            data[2] = 1;  // Standard Magnetic
            data[3] = 40; // 4.0 mm max travel (40 * 0.1 mm)
            data[4] = 0;
            break;

        case AMC_SET_AXIS_TYPE:
            if (length >= 3) {
                data[2] = 0;  // Success
            } else {
                success = false;
            }
            break;

        case AMC_GET_POLL_PHASE: {
            // Respuesta (LE):
            //   data[2]      status: 0 = probe activo y datos validos
            //                        1 = buffer corto
            //                        2 = binario sin -DUSB_POLL_PHASE_PROBE
            //   data[3..6]   usb_poll_phase_last_us  (uint32)
            //   data[7..10]  usb_poll_phase_min_us   (uint32)
            //   data[11..14] usb_poll_phase_max_us   (uint32)
            //   data[15..18] usb_poll_phase_count    (uint32)
            // Entrada: data[2] == 1 => leer y luego resetear min/max/count.
#if defined(USB_POLL_PHASE_PROBE)
            extern volatile uint32_t usb_poll_phase_last_us;
            extern volatile uint32_t usb_poll_phase_min_us;
            extern volatile uint32_t usb_poll_phase_max_us;
            extern volatile uint32_t usb_poll_phase_count;
            if (length < 19) {
                data[2] = 1;
                break;
            }
            uint8_t  do_reset = data[2];
            uint32_t last     = usb_poll_phase_last_us;
            uint32_t vmin     = usb_poll_phase_min_us;
            uint32_t vmax     = usb_poll_phase_max_us;
            uint32_t cnt      = usb_poll_phase_count;
            data[2]  = 0;
            data[3]  = last & 0xFF; data[4]  = (last >> 8) & 0xFF; data[5]  = (last >> 16) & 0xFF; data[6]  = (last >> 24) & 0xFF;
            data[7]  = vmin & 0xFF; data[8]  = (vmin >> 8) & 0xFF; data[9]  = (vmin >> 16) & 0xFF; data[10] = (vmin >> 24) & 0xFF;
            data[11] = vmax & 0xFF; data[12] = (vmax >> 8) & 0xFF; data[13] = (vmax >> 16) & 0xFF; data[14] = (vmax >> 24) & 0xFF;
            data[15] = cnt  & 0xFF; data[16] = (cnt  >> 8) & 0xFF; data[17] = (cnt  >> 16) & 0xFF; data[18] = (cnt  >> 24) & 0xFF;
            if (do_reset == 1) {
                usb_poll_phase_min_us = 0xFFFFFFFFU;
                usb_poll_phase_max_us = 0;
                usb_poll_phase_count  = 0;
            }
#else
            data[2] = 2;
#endif
        } break;

        case AMC_GET_SCAN_PHASE: {
            // Respuesta (LE):
            //   data[2]      status: 0 = OK - 1 = buffer corto - 2 = sin -DUSB_SOF_TIMING_PROBE
            //   data[3..4]   scan_probe_duration_us       (uint16, ultimo)
            //   data[5..6]   scan_probe_duration_min_us   (uint16)
            //   data[7..8]   scan_probe_duration_max_us   (uint16)
            //   data[9..10]  scan_probe_phase_us          (uint16, ultimo, fin de scan vs SOF)
            //   data[11..12] scan_probe_phase_min_us      (uint16)
            //   data[13..14] scan_probe_phase_max_us      (uint16)
            //   data[15..18] scan_probe_count             (uint32)
            // Entrada: data[2] == 1 => leer y luego resetear min/max/count.
#if defined(USB_SOF_TIMING_PROBE)
            extern volatile uint32_t scan_probe_count;
            extern volatile uint16_t scan_probe_duration_us;
            extern volatile uint16_t scan_probe_phase_us;
            extern volatile uint16_t scan_probe_duration_min_us;
            extern volatile uint16_t scan_probe_duration_max_us;
            extern volatile uint16_t scan_probe_phase_min_us;
            extern volatile uint16_t scan_probe_phase_max_us;
            if (length < 19) {
                data[2] = 1;
                break;
            }
            uint8_t  do_reset = data[2];
            uint16_t d_last = scan_probe_duration_us,  d_min = scan_probe_duration_min_us, d_max = scan_probe_duration_max_us;
            uint16_t p_last = scan_probe_phase_us,     p_min = scan_probe_phase_min_us,    p_max = scan_probe_phase_max_us;
            uint32_t cnt    = scan_probe_count;
            data[2]  = 0;
            data[3]  = d_last & 0xFF; data[4]  = (d_last >> 8) & 0xFF;
            data[5]  = d_min  & 0xFF; data[6]  = (d_min  >> 8) & 0xFF;
            data[7]  = d_max  & 0xFF; data[8]  = (d_max  >> 8) & 0xFF;
            data[9]  = p_last & 0xFF; data[10] = (p_last >> 8) & 0xFF;
            data[11] = p_min  & 0xFF; data[12] = (p_min  >> 8) & 0xFF;
            data[13] = p_max  & 0xFF; data[14] = (p_max  >> 8) & 0xFF;
            data[15] = cnt & 0xFF; data[16] = (cnt >> 8) & 0xFF; data[17] = (cnt >> 16) & 0xFF; data[18] = (cnt >> 24) & 0xFF;
            if (do_reset == 1) {
                scan_probe_duration_min_us = 0xFFFF;
                scan_probe_duration_max_us = 0;
                scan_probe_phase_min_us    = 0xFFFF;
                scan_probe_phase_max_us    = 0;
                scan_probe_count           = 0;
            }
#else
            data[2] = 2;
#endif
        } break;
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
#if ANALOG_GAME_CONTROLLER_SUPPORT
    game_controller_clear();
#endif
}
