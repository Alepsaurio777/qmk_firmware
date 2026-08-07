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
#include "keychron_common.h"
#include "analog_matrix.h"
#include "game_controller_common.h"
#include "profile.h"
#include "action_socd.h"
#include "keymap_common.h"
#include "eeconfig.h"
#include "eeprom.h"
#include "nvm_eeprom_eeconfig_internal.h"

#ifdef ANANLOG_MATRIX
#    ifndef PROF_KEY_COL_OFFSET
#        define PROF_KEY_COL_OFFSET 0
#    endif

#    ifndef PROF_TRIG_KEY_ROW
#        if MATRIX_ROWS == 5
#            define PROF_TRIG_KEY_ROW 1
#        elif MATRIX_ROWS == 6
#            define PROF_TRIG_KEY_ROW 2
#        else
#            error "PROF_TRIG_KEY_ROW is not set"
#        endif
#    endif

#    ifndef PROF_TRIG_KEY_COL
#        define PROF_TRIG_KEY_COL (PROF_KEY_COL_OFFSET + 10)
#    endif
/* Profile 1 Key */
#    ifndef PROF_1_KEY_ROW
#        if MATRIX_ROWS == 5
#            define PROF_1_KEY_ROW 3
#        elif MATRIX_ROWS == 6
#            define PROF_1_KEY_ROW 4
#        else
#            error "PROF_1_KEY_ROW is not set"
#        endif
#    endif

#    ifndef PROF_1_KEY_COL
#        define PROF_1_KEY_COL (PROF_KEY_COL_OFFSET + 2)
#    endif

/* Profile 2 Key */
#    ifndef PROF_2_KEY_ROW
#        if MATRIX_ROWS == 5
#            define PROF_2_KEY_ROW 3
#        elif MATRIX_ROWS == 6
#            define PROF_2_KEY_ROW 4
#        else
#            error "PROF_2_KEY_ROW is not set"
#        endif
#    endif

/* Profile 2 Key */
#    ifndef PROF_2_KEY_COL
#        define PROF_2_KEY_COL (PROF_KEY_COL_OFFSET + 3)
#    endif

#    ifndef PROF_3_KEY_ROW
#        if MATRIX_ROWS == 5
#            define PROF_3_KEY_ROW 3
#        elif MATRIX_ROWS == 6
#            define PROF_3_KEY_ROW 4
#        else
#            error "PROF_3_KEY_ROW is not set"
#        endif
#    endif

#    ifndef PROF_3_KEY_COL
#        define PROF_3_KEY_COL (PROF_KEY_COL_OFFSET + 4)
#    endif

#    define KEY_MASK(r, c) (virtual_matrix[r] & (1 << c))

enum {
    ADV_MODE_CLEAR = 0,
    ADV_MODE_OKMC,
    ADV_MODE_GAME_CONTROLLER,
    ADV_MODE_TOGGLE,
};

extern uint8_t  profile_gobal_mode[PROFILE_COUNT];
extern uint16_t default_profiles[PROFILE_COUNT][MATRIX_ROWS][MATRIX_COLS];

/* Per-profile RT sensitivity defaults (0.1mm units). Release 0 = inherit from
 * press. Boards may override with strong definitions next to
 * profile_gobal_mode. */
__attribute__((weak)) const uint8_t profile_default_rt_sen[PROFILE_COUNT] = {
    [0 ... PROFILE_COUNT - 1] = DEFAULT_RAPID_TRIGGER_SENSITIVITY,
};
__attribute__((weak)) const uint8_t profile_default_rt_sen_rls[PROFILE_COUNT] = {0};

static inline uint8_t profile_default_rt_sen_rls_get(uint8_t prof_idx) {
    uint8_t rls = profile_default_rt_sen_rls[prof_idx];
    return (rls == 0 || rls > 39) ? profile_default_rt_sen[prof_idx] : rls;
}

static analog_matrix_profile_t  profile[PROFILE_COUNT];
static uint8_t                  current_profile_index;
static analog_matrix_profile_t *cur_prof       = &profile[0]; // current profile
uint8_t                         prof_combo     = 0;
static uint8_t                  prof_ind_state = 0;
static uint32_t                 pro_ind_timer  = 0;

// (24-jul) profile_apply_default_rapid_keys eliminado. Al cargar la EEPROM
// convertia el modo AKM_GLOBAL de Espacio y LShift en AKM_RAPID explicito
// (guardado por global.mode == AKM_RAPID, de ahi que hoy no dispare: el perfil
// gaming arranca en AKM_REGULAR). El problema no era el efecto de hoy sino el
// de despues: en cuanto el modo global del perfil pasara a Rapid, esas dos
// teclas quedaban clavadas y un cambio posterior a Regular ya no las movia —
// un hardcode invisible en Launcher. Los defaults de modo por tecla ya viven
// en default_profiles[] (perfil 1 marca WASD/espacio/LShift/LCtrl como Rapid),
// que es una tabla de reset y por tanto pisable desde Launcher.

// Weak: un teclado sin afinado por tecla no paga nada y profile_reset() se
// comporta exactamente como antes. Los define k2_he/ansi/profiles.c.
__attribute__((weak)) const profile_key_tuning_t *profile_key_tuning(uint8_t prof_idx) {
    (void)prof_idx;
    return NULL;
}

__attribute__((weak)) const profile_socd_seed_t *profile_socd_seeds(uint8_t prof_idx) {
    (void)prof_idx;
    return NULL;
}

// Siembra el afinado por tecla y los pares SOCD de la tabla de reset,
// resolviendo los keycodes contra el keymap VIVO. Un solo barrido de la matriz
// con las listas (cortas) por dentro, mismo patron que
// analog_matrix_resolve_policy_keys(). Solo corre en un reset de perfil.
static void profile_apply_seeds(uint8_t prof_index, analog_matrix_profile_t *prof) {
    const profile_key_tuning_t *tuning = profile_key_tuning(prof_index);
    const profile_socd_seed_t  *socd   = profile_socd_seeds(prof_index);
    if (!tuning && !socd) return;

    // Posiciones resueltas de los keycodes que aparecen en los pares SOCD.
    uint8_t socd_row[SOCD_COUNT * 2];
    uint8_t socd_col[SOCD_COUNT * 2];
    uint8_t socd_n = 0;
    if (socd) {
        for (uint8_t i = 0; i < SOCD_COUNT && socd[i].type != 0; i++) socd_n = (uint8_t)(i + 1);
    }
    for (uint8_t i = 0; i < socd_n * 2 && i < SOCD_COUNT * 2; i++) {
        socd_row[i] = 0xFF;
        socd_col[i] = 0xFF;
    }

    for (uint8_t row = 0; row < MATRIX_ROWS; row++) {
        for (uint8_t col = 0; col < MATRIX_COLS; col++) {
            const keypos_t pos = {.row = row, .col = col};
            const uint16_t kc  = keymap_key_to_keycode(ANALOG_POLICY_LAYER, pos);
            if (kc == KC_NO || kc == KC_TRANSPARENT) continue;

            if (tuning) {
                for (uint8_t i = 0; tuning[i].keycode != KC_NO; i++) {
                    if (kc != tuning[i].keycode) continue;
                    // Los campos son bitfields de 6 bits: fuera de rango se
                    // ignora en vez de truncarse en silencio.
                    if (tuning[i].act_pt && tuning[i].act_pt <= 39) prof->key_config[row][col].act_pt = tuning[i].act_pt;
                    if (tuning[i].sen && tuning[i].sen <= 39) prof->key_config[row][col].rpd_trig_sen = tuning[i].sen;
                    if (tuning[i].sen_rls && tuning[i].sen_rls <= 39) prof->key_config[row][col].rpd_trig_sen_deact = tuning[i].sen_rls;
                }
            }

            for (uint8_t i = 0; i < socd_n; i++) {
                // Primera coincidencia gana, igual que en la politica.
                if (kc == socd[i].keycode_1 && socd_row[i * 2] == 0xFF) {
                    socd_row[i * 2] = row;
                    socd_col[i * 2] = col;
                } else if (kc == socd[i].keycode_2 && socd_row[i * 2 + 1] == 0xFF) {
                    socd_row[i * 2 + 1] = row;
                    socd_col[i * 2 + 1] = col;
                }
            }
        }
    }

    // Un par solo se siembra si SUS DOS teclas existen en la capa. Media pareja
    // resuelta seria peor que ninguna: SOCD con una coordenada invalida se
    // descarta en runtime, pero deja el slot ocupado y mintiendo en Launcher.
    for (uint8_t i = 0; i < socd_n; i++) {
        const uint8_t r1 = socd_row[i * 2], c1 = socd_col[i * 2];
        const uint8_t r2 = socd_row[i * 2 + 1], c2 = socd_col[i * 2 + 1];
        if (r1 >= MATRIX_ROWS || r2 >= MATRIX_ROWS) continue;
        if (r1 == r2 && c1 == c2) continue; // misma tecla: invalido

        prof->socd[i].key_1_row = r1;
        prof->socd[i].key_1_col = c1;
        prof->socd[i].key_2_row = r2;
        prof->socd[i].key_2_col = c2;
        prof->socd[i].type      = socd[i].type;
    }
}

void profile_init(bool reset) {
    if (reset) {
        // Write default profile setting
        for (uint8_t i = 0; i < PROFILE_COUNT; i++)
            profile_reset(i);
    } else {
        uint8_t *buf = (uint8_t *)malloc(EECONFIG_SIZE_ANALOG_MATRIX);
        if (!buf) {
            for (uint8_t i = 0; i < PROFILE_COUNT; i++)
                profile_reset(i);
            return;
        }
        memset(buf, 0, EECONFIG_SIZE_ANALOG_MATRIX);

        eeprom_read_block(buf, (void *)EECONFIG_BASE_ANALOG_MATRIX, EECONFIG_SIZE_ANALOG_MATRIX);

        current_profile_index = buf[OFFSET_CURRENT_PROFILE];
        if (current_profile_index >= PROFILE_COUNT) current_profile_index = 0;

        cur_prof = &profile[current_profile_index];

        // Load profile data
        memcpy(profile, buf + OFFSET_PROFILES_START, PROFILE_SIZE * PROFILE_COUNT);

        for (uint8_t i = 0; i < PROFILE_COUNT; i++) {
            if (profile[i].global.mode == 0 || profile[i].global.mode > AKM_RAPID) profile[i].global.mode = profile_gobal_mode[i];

            // Resotre to default if not in valid range
            if (profile[i].global.act_pt == 0 || profile[i].global.act_pt > 39) profile[i].global.act_pt = DEFAULT_ACTUATION_POINT;
            if (profile[i].global.rpd_trig_sen == 0 || profile[i].global.rpd_trig_sen > 39) profile[i].global.rpd_trig_sen = profile_default_rt_sen[i];
            if (profile[i].global.rpd_trig_sen_deact == 0 || profile[i].global.rpd_trig_sen_deact > 39) profile[i].global.rpd_trig_sen_deact = profile_default_rt_sen_rls_get(i);
        }

        free(buf);
    }

    socd_update_active_state();
}

analog_matrix_profile_t *profile_get(uint8_t index) {
    return &profile[index];
}

analog_matrix_profile_t *profile_get_current(void) {
    return cur_prof;
}

uint8_t profile_get_current_index(void) {
    return current_profile_index;
}

bool profile_select(uint8_t prof_idx, bool indication, bool save_eeprom) {
    if (prof_idx >= PROFILE_COUNT) return false;

    if (prof_idx != current_profile_index) {
        extern void okmc_release_all_active(void);
        okmc_release_all_active();

        current_profile_index = prof_idx;

        cur_prof = &profile[prof_idx];
        analog_matrix_clear();
        update_travel_configs();

        if (save_eeprom) {
            eeprom_update_dword(EECONFIG_KEYBOARD, (EECONFIG_KB_DATA_VERSION));
            analog_matrix_eeprom_update(&prof_idx, (void *)OFFSET_CURRENT_PROFILE, 1);
        }
        analog_matrix_clear_advance_keys();
        socd_update_active_state();
    }
    if (indication) {
#    ifdef LED_MATRIX_ENABLE
        if (!led_matrix_is_enabled()) led_matrix_enable_noeeprom();
#    endif
#    ifdef RGB_MATRIX_ENABLE
        if (!rgb_matrix_is_enabled()) rgb_matrix_enable_noeeprom();
#    endif
        pro_ind_timer  = timer_read32();
        prof_ind_state = 0x86;
    }

    return true;
}

bool profile_get_raw_data(uint8_t prof_idx, uint16_t offset, uint8_t size, uint8_t *data) {
    if (prof_idx >= PROFILE_COUNT || offset >= PROFILE_SIZE) return false;

    memset(data, 0, size);

    if (offset + size > PROFILE_SIZE) size = PROFILE_SIZE - offset;
    memcpy(data, (uint8_t *)(&profile[prof_idx]) + offset, size);

    return true;
}

bool profile_set_traval(uint8_t prof_idx, uint8_t mode, uint8_t act_pt, uint8_t sens, uint8_t rls_sens, bool global, uint32_t row[]) {
    // Check validity
    if (prof_idx >= PROFILE_COUNT || mode > AKM_RAPID || act_pt > 39 || (global && mode == AKM_GLOBAL)) return false;

    analog_matrix_profile_t *prof = profile_get(prof_idx);

    if (global) {
        prof->global.mode               = mode;
        prof->global.act_pt             = act_pt;
        prof->global.rpd_trig_sen       = sens;
        prof->global.rpd_trig_sen_deact = rls_sens;
        memset(row, 0xFF, sizeof(row[0]) * MATRIX_ROWS);
        if (prof_idx == profile_get_current_index()) update_travel_configs();
    } else {
        for (uint8_t r = 0; r < MATRIX_ROWS; r++)
            for (uint8_t c = 0; c < MATRIX_COLS; c++) {
                if (row[r] & (0x01 << c)) {
                    prof->key_config[r][c].mode               = mode;
                    prof->key_config[r][c].act_pt             = act_pt;
                    prof->key_config[r][c].rpd_trig_sen       = sens;
                    prof->key_config[r][c].rpd_trig_sen_deact = rls_sens;

                    if (prof_idx == profile_get_current_index()) update_key_config(r, c);
                }
            }
    }

    return true;
}

bool profile_set_adv_mode(uint8_t *data) {
    uint8_t prof_idx = data[0];
    if (prof_idx >= PROFILE_COUNT) return false;

    uint8_t mode  = data[1];
    uint8_t row   = data[2];
    uint8_t col   = data[3];
    uint8_t index = data[4];

    if (row >= MATRIX_ROWS || col >= MATRIX_COLS) return false;

    okmc_config_t okmc_config;
    memset(&okmc_config, 0, sizeof(okmc_config));
    analog_matrix_profile_t *prof      = profile_get(prof_idx);
    analog_key_config_t *    p_key_cfg = &prof->key_config[row][col];

    switch (mode) {
        case ADV_MODE_CLEAR:
            p_key_cfg->adv_mode      = 0;
            p_key_cfg->adv_mode_data = 0;
            break;

        case ADV_MODE_OKMC:
            if (index >= OKMC_COUNT) return false;

            okmc_config.travel.shallow_act   = data[5];
            okmc_config.travel.shallow_deact = data[6];
            okmc_config.travel.deep_act      = data[7];
            okmc_config.travel.deep_deact    = data[8];
            memcpy(okmc_config.keycode, &data[9], sizeof(okmc_config.keycode));
            memcpy(okmc_config.action, &data[17], sizeof(okmc_config.action));

            prof->okmc[index]   = okmc_config;
            p_key_cfg->adv_mode = AKM_DKS;
            p_key_cfg->okmc_idx = index;
            break;

        case ADV_MODE_GAME_CONTROLLER:
            if (index >= GC_BUTTON_MAX) return false;

            p_key_cfg->adv_mode = AKM_GAMEPAD;
            p_key_cfg->js_axis  = index;
            break;

        case ADV_MODE_TOGGLE:
            p_key_cfg->adv_mode      = AKM_TOGGLE;
            p_key_cfg->adv_mode_data = 0;
            break;

        default:
            return false;
    }

    if (prof_idx == profile_get_current_index()) update_key_config(row, col);

    return true;
}

bool profile_set_socd(uint8_t *data) {
    uint8_t prof_idx = data[0];
    uint8_t row1     = data[1];
    uint8_t col1     = data[2];
    uint8_t row2     = data[3];
    uint8_t col2     = data[4];
    uint8_t index    = data[5];
    uint8_t type     = data[6];

    if (prof_idx >= PROFILE_COUNT || row1 >= MATRIX_ROWS || col1 >= MATRIX_COLS || row2 >= MATRIX_ROWS || col2 >= MATRIX_COLS || index >= SOCD_COUNT || type >= SOCD_PRI_MAX) return false;
    // Same key is not allowed
    if (type && row1 == row2 && col1 == col2) return false;

    analog_matrix_profile_t *prof = &profile[prof_idx];

    if (type) {
        prof->socd[index].key_1_row = row1;
        prof->socd[index].key_1_col = col1;
        prof->socd[index].key_2_row = row2;
        prof->socd[index].key_2_col = col2;
        prof->socd[index].type      = type;
    } else {
        memset(&prof->socd[index], 0, sizeof(socd_config_t));
    }

    if (prof_idx == profile_get_current_index()) socd_update_active_state();

    return true;
}

bool profile_save(uint8_t prof_index) {
    if (prof_index >= PROFILE_COUNT) return false;

    analog_matrix_profile_t *prof = &profile[prof_index];

    // Sin sello de version aqui: tras el init la version siempre es valida
    // (analog_matrix_eeconfig_init la sella al FINAL de la migracion), y
    // sellarla desde aqui rompia la atomicidad — profile_reset(0) llama a
    // profile_save durante la migracion y sellaba ANTES de que los perfiles
    // 1 y 2 se escribieran (corte de luz = version valida + perfiles a medias).
    analog_matrix_eeprom_update(prof, (void *)OFFSET_PROFILES_START + prof_index * sizeof(analog_matrix_profile_t), sizeof(analog_matrix_profile_t));

    return true;
}

bool profile_reset(uint8_t prof_index) {
    if (prof_index >= PROFILE_COUNT) return false;

    analog_matrix_profile_t *prof = &profile[prof_index];

    memset(prof, 0, sizeof(profile[0]));
    // Default
    prof->global.mode               = profile_gobal_mode[prof_index];
    prof->global.act_pt             = DEFAULT_ACTUATION_POINT;
    prof->global.rpd_trig_sen       = profile_default_rt_sen[prof_index];
    prof->global.rpd_trig_sen_deact = profile_default_rt_sen_rls_get(prof_index);

    for (uint8_t r = 0; r < MATRIX_ROWS; r++)
        for (uint8_t c = 0; c < MATRIX_COLS; c++) {
            prof->key_config[r][c].mode     = default_profiles[prof_index][r][c] & 0x3;
            prof->key_config[r][c].adv_mode = (default_profiles[prof_index][r][c] >> 2) & 0x07;
            if (prof->key_config[r][c].adv_mode == AKM_GAMEPAD) {
                prof->key_config[r][c].js_axis = default_profiles[prof_index][r][c] >> 5;
            }
        }

    profile_apply_seeds(prof_index, prof);

    profile_save(prof_index);
    if (prof_index == profile_get_current_index()) socd_update_active_state();

    return true;
}

void process_profile_select_combo(void) {
    extern matrix_row_t virtual_matrix[MATRIX_ROWS];

#if ANALOG_DISABLE_PROFILE_COMBO_IN_GAMING_MODE
    if (analog_matrix_is_gaming_mode()) {
        prof_combo = 0;
        return;
    }
#endif

    if (prof_combo & KEY_PRESS_FN) {
        if ((prof_combo & KEY_PRESS_P) == 0 && KEY_MASK(PROF_TRIG_KEY_ROW, PROF_TRIG_KEY_COL)) {
            prof_combo |= KEY_PRESS_P;
        } else if ((prof_combo & KEY_PRESS_P) && KEY_MASK(PROF_TRIG_KEY_ROW, PROF_TRIG_KEY_COL) == 0) {
            prof_combo &= ~KEY_PRESS_P;
        }

        if ((prof_combo & KEY_PRESS_PROF_COMBO) == KEY_PRESS_PROF_COMBO) {
            if (KEY_MASK(PROF_1_KEY_ROW, PROF_1_KEY_COL)) {
                profile_select(0, true, true);
            } else if (KEY_MASK(PROF_2_KEY_ROW, PROF_2_KEY_COL)) {
                profile_select(1, true, true);
            } else if (KEY_MASK(PROF_3_KEY_ROW, PROF_3_KEY_COL)) {
                profile_select(2, true, true);
            }
        }
    }
}

bool profile_set_name(uint8_t prof_idx, uint8_t len, uint8_t *name) {
    if (prof_idx >= PROFILE_COUNT || len > 28) return false;

    memset(profile[prof_idx].name, 0, PROFILE_NAME_LEN);
    memcpy(profile[prof_idx].name, name, len);

    return true;
}

bool process_record_profile(uint16_t keycode, keyrecord_t *record) {
    switch (keycode) {
        case PROF1:
        case PROF2:
        case PROF3:
            if (record->event.pressed) profile_select(keycode - PROF1, true, true);
            return false; // Skip all further processing of this key

        case MO(0)... MO(15):
            if (record->event.pressed)
                prof_combo |= KEY_PRESS_FN;
            else
                prof_combo &= ~KEY_PRESS_FN;
            break;

        default:
            break;
    }

    return true;
}

void profile_indication_enable(void) {
    pro_ind_timer  = timer_read32();
    prof_ind_state = 1;
}

void profile_indication_timer_check(void) {
    if (pro_ind_timer && timer_elapsed32(pro_ind_timer) > 500) {
        if ((prof_ind_state++ & 0xF) > 6) {
            pro_ind_timer = prof_ind_state = 0;

#    ifdef LED_MATRIX_ENABLE
            if (!led_matrix_is_enabled()) led_matrix_disable_noeeprom();
#    endif
#    ifdef RGB_MATRIX_ENABLE
            eeprom_read_block(&rgb_matrix_config, EECONFIG_RGB_MATRIX, sizeof(rgb_matrix_config));
            if (!rgb_matrix_config.mode) {
                eeconfig_update_rgb_matrix_default();
            }

            if (!rgb_matrix_is_enabled()) rgb_matrix_disable_noeeprom();
#    endif
        } else {
            pro_ind_timer = timer_read32();
        }
    }
}

void profile_indication(void) {
#ifdef RGB_MATRIX_ENABLE
    if (prof_ind_state) {
        static uint8_t prof_led_list[3] = PROFILE_LED_MATRIX_LIST;
        rgb_matrix_set_color_all(prof_ind_state % 2 ? 0 : 255, 0, 0);
        if (prof_ind_state & 0x80) {
            rgb_matrix_set_color(prof_led_list[current_profile_index], prof_ind_state % 2 ? 0 : 255, prof_ind_state % 2 ? 0 : 255, prof_ind_state % 2 ? 0 : 255);
        }
    }
#endif
}
#endif
