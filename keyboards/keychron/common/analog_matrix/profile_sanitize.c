// SPDX-License-Identifier: GPL-2.0-or-later
#include "profile_sanitize.h"

#if ANALOG_PROFILE_SANITIZER_ENABLE

#    include <string.h>

#    include "action_socd.h"
#    if ANALOG_GAME_CONTROLLER_SUPPORT
#        include "game_controller_common.h"
#    endif

static inline void clear_advanced_mode(analog_key_config_t *cfg) {
    cfg->adv_mode      = 0;
    cfg->adv_mode_data = 0;
}

bool analog_profile_sanitize(analog_matrix_profile_t *prof, uint8_t default_mode, uint8_t default_act_pt, uint8_t default_rt_sen, uint8_t default_rt_sen_rls) {
    if (!prof) return false;

    bool changed = false;

    // Global: aqui 0 no significa "hereda"; el perfil necesita un modo y
    // thresholds efectivos. Conserva exactamente la politica de profile_init()
    // de V4.2.2, pero centralizada.
    if (prof->global.mode != AKM_REGULAR && prof->global.mode != AKM_RAPID) {
        prof->global.mode = default_mode;
        changed           = true;
    }
    if (prof->global.act_pt == 0 || prof->global.act_pt > 39) {
        prof->global.act_pt = default_act_pt;
        changed             = true;
    }
    if (prof->global.rpd_trig_sen == 0 || prof->global.rpd_trig_sen > 39) {
        prof->global.rpd_trig_sen = default_rt_sen;
        changed                   = true;
    }
    if (prof->global.rpd_trig_sen_deact == 0 || prof->global.rpd_trig_sen_deact > 39) {
        prof->global.rpd_trig_sen_deact = default_rt_sen_rls;
        changed                         = true;
    }
    if (prof->global.adv_mode != 0 || prof->global.adv_mode_data != 0) {
        prof->global.adv_mode      = 0;
        prof->global.adv_mode_data = 0;
        changed                    = true;
    }

    for (uint8_t r = 0; r < MATRIX_ROWS; r++) {
        for (uint8_t c = 0; c < MATRIX_COLS; c++) {
            analog_key_config_t *cfg = &prof->key_config[r][c];

            if (cfg->mode > AKM_RAPID) {
                cfg->mode = AKM_GLOBAL;
                changed   = true;
            }
            if (cfg->act_pt > 39) {
                cfg->act_pt = 0;
                changed     = true;
            }
            if (cfg->rpd_trig_sen > 39) {
                cfg->rpd_trig_sen = 0;
                changed            = true;
            }
            if (cfg->rpd_trig_sen_deact > 39) {
                cfg->rpd_trig_sen_deact = 0;
                changed                  = true;
            }

            switch (cfg->adv_mode) {
                case 0:
                    // Canonical representation: no advanced mode => no stale
                    // union payload. Esto hace futura migracion+CRC determinista.
                    if (cfg->adv_mode_data != 0) {
                        cfg->adv_mode_data = 0;
                        changed            = true;
                    }
                    break;

                case AKM_DKS:
                    if (cfg->okmc_idx >= OKMC_COUNT) {
                        clear_advanced_mode(cfg);
                        changed = true;
                    }
                    break;

                case AKM_GAMEPAD:
#    if ANALOG_GAME_CONTROLLER_SUPPORT
                    if (cfg->js_axis >= GC_BUTTON_MAX || cfg->js_axis == GC_MAX) {
                        clear_advanced_mode(cfg);
                        changed = true;
                    }
#    else
                    // LAB no compila runtime Gamepad: una EEPROM antigua no
                    // puede reactivarlo por datos persistidos.
                    clear_advanced_mode(cfg);
                    changed = true;
#    endif
                    break;

                case AKM_TOGGLE:
                    if (cfg->adv_mode_data != 0) {
                        cfg->adv_mode_data = 0;
                        changed            = true;
                    }
                    break;

                default:
                    clear_advanced_mode(cfg);
                    changed = true;
                    break;
            }
        }
    }

    for (uint8_t i = 0; i < SOCD_COUNT; i++) {
        socd_config_t *s = &prof->socd[i];
        bool invalid = false;

        if (s->type == SOCD_PRI_NONE) {
            // Disabled slots are canonical all-zero. Avoid stale coordinates
            // becoming authenticated later when CRC is enabled.
            if (s->key_1_row || s->key_1_col || s->key_2_row || s->key_2_col) {
                memset(s, 0, sizeof(*s));
                changed = true;
            }
            continue;
        }

        if (s->type >= SOCD_PRI_MAX) invalid = true;
        if (s->key_1_row >= MATRIX_ROWS || s->key_2_row >= MATRIX_ROWS) invalid = true;
        if (s->key_1_col >= MATRIX_COLS || s->key_2_col >= MATRIX_COLS) invalid = true;
        if (s->key_1_row == s->key_2_row && s->key_1_col == s->key_2_col) invalid = true;

        if (invalid) {
            memset(s, 0, sizeof(*s));
            changed = true;
        }
    }

    return changed;
}
#endif
