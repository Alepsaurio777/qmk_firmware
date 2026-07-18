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

// OKMC action type
enum {
    OKMC_ACTION_RELEASE  = 0b001,
    OKMC_ACTION_PRESS    = 0b010,
    OKMC_ACTION_TAP      = 0b110,
    OKMC_ACTION_RE_PRESS = 0b111,
};

enum {
    OKMC_RELEASED = AKS_REGULAR_RELEASED,
    OKMC_SHALLOW_ACTUATED,
    OKMC_DEEP_ACTUATED,
    OKMC_DEEP_DEACT_READY,
    OKMC_DEEP_DEACTUATED,
    OKMC_MAX,
};

matrix_row_t okmc_matrix[MATRIX_ROWS] = {0};

static void report_action(bool add, uint16_t keycode) {
    if (add) {
        if (IS_BASIC_KEYCODE(keycode)) {
            add_key(keycode);
        } else if (IS_MODIFIER_KEYCODE(keycode)) {
            add_mods(MOD_BIT(keycode));
        }
    } else {
        if (IS_BASIC_KEYCODE(keycode)) {
            del_key(keycode);
        } else if (IS_MODIFIER_KEYCODE(keycode)) {
            del_mods(MOD_BIT(keycode));
        }
    }
}

static void release_okmc_keys(okmc_config_t *okmc) {
    // Relase all keys in the OKMC settings
    for (uint8_t i = 0; i < 4; ++i) {
        if (okmc->keycode[i]) {
            report_action(0, okmc->keycode[i]);
        }
    }
    send_keyboard_report();
}

/* Deferred OKMC action queue.
 *
 * The original stage functions ran send_keyboard_report() + wait_ms(1) up to
 * three times INSIDE the matrix scan, freezing all key sampling for up to
 * 3 ms whenever an OKMC fired. Instead, stages are queued here and exactly
 * one bit-group is executed per scan pass. With the SOF-synchronized scan
 * (1000 scans/s) that reproduces the original 1 ms spacing between reports
 * -- one report per USB frame -- without ever blocking the scan. */

enum {
    OKMC_FIELD_SHALLOW_ACT,
    OKMC_FIELD_SHALLOW_DEACT, // groups + release_okmc_keys at the end
    OKMC_FIELD_DEEP_ACT,
    OKMC_FIELD_DEEP_DEACT,
    OKMC_FIELD_RELEASE_ONLY, // release_okmc_keys, no groups
};

typedef struct {
    uint8_t okmc_idx;
    uint8_t field;
} okmc_pending_t;

#define OKMC_QUEUE_DEPTH 16
static okmc_pending_t okmc_queue[OKMC_QUEUE_DEPTH];
static uint8_t        okmc_q_head, okmc_q_count;
static uint8_t        okmc_q_bit; // bit-group progress of the head entry

static inline bool okmc_field_releases(uint8_t field) {
    return field == OKMC_FIELD_RELEASE_ONLY || field == OKMC_FIELD_SHALLOW_DEACT;
}

static void okmc_enqueue(uint8_t okmc_idx, uint8_t field) {
    if (okmc_q_count >= OKMC_QUEUE_DEPTH) {
        // Cola llena (burst improbable): descartar un PRESS es tolerable (tecla
        // perdida), pero descartar un RELEASE dejaria la tecla/modificador
        // pegado. Ejecutar el release inline garantiza que nunca quede pegado,
        // a costa de un reporte sincrono solo en overflow.
        if (okmc_field_releases(field)) {
            release_okmc_keys(&profile_get_current()->okmc[okmc_idx]);
        }
        return;
    }
    okmc_queue[(okmc_q_head + okmc_q_count) % OKMC_QUEUE_DEPTH] = (okmc_pending_t){okmc_idx, field};
    okmc_q_count++;
}

static uint8_t okmc_field_actions(const okmc_config_t *okmc, uint8_t field, uint8_t i) {
    switch (field) {
        case OKMC_FIELD_SHALLOW_ACT:
            return okmc->action[i].shallow_act;
        case OKMC_FIELD_SHALLOW_DEACT:
            return okmc->action[i].shallow_deact;
        case OKMC_FIELD_DEEP_ACT:
            return okmc->action[i].deep_act;
        case OKMC_FIELD_DEEP_DEACT:
            return okmc->action[i].deep_deact;
        default:
            return 0;
    }
}

// Runs once per scan from analog_matrix_task(): executes at most one
// bit-group (one HID report) and returns.
void okmc_deferred_task(void) {
    if (okmc_q_count == 0) return;

    okmc_pending_t *pend = &okmc_queue[okmc_q_head];
    okmc_config_t  *okmc = &profile_get_current()->okmc[pend->okmc_idx];

    while (pend->field != OKMC_FIELD_RELEASE_ONLY && okmc_q_bit < 3) {
        bool    any_action = false;
        uint8_t bit        = okmc_q_bit++;

        for (uint8_t i = 0; i < 4; ++i) {
            if (okmc->keycode[i] && (okmc_field_actions(okmc, pend->field, i) & (0x01 << bit))) {
                report_action(bit % 2, okmc->keycode[i]);
                any_action = true;
            }
        }
        if (any_action) {
            send_keyboard_report();
            return; // one report per scan; next group on the next pass
        }
    }

    // Groups exhausted (or release-only entry): run the epilogue and pop.
    if (pend->field == OKMC_FIELD_SHALLOW_DEACT || pend->field == OKMC_FIELD_RELEASE_ONLY) {
        release_okmc_keys(okmc);
    }
    okmc_q_head = (okmc_q_head + 1) % OKMC_QUEUE_DEPTH;
    okmc_q_count--;
    okmc_q_bit = 0;
}

bool okmc_action(analog_key_t *key) {
    if (key->okmc_idx >= OKMC_COUNT) return false;
    bool                     changed    = false;
    analog_matrix_profile_t *cur_prof   = profile_get_current();
    okmc_traval_config_t    *travel_cfg = &cur_prof->okmc[key->okmc_idx].travel;

    switch (key->state) {
        case OKMC_RELEASED:
            // Check shallow actuation
            if (key->travel >= travel_cfg->shallow_act * TRAVEL_SCALE) {
                key->state = OKMC_SHALLOW_ACTUATED;
                okmc_enqueue(key->okmc_idx, OKMC_FIELD_SHALLOW_ACT);
                changed = true;
            }
            break;

        case OKMC_SHALLOW_ACTUATED:
            // Key releasing
            if (key->travel < travel_cfg->shallow_deact * TRAVEL_SCALE && key->travel < (travel_cfg->shallow_act - 1) * TRAVEL_SCALE) {
                key->state = OKMC_RELEASED;
                okmc_enqueue(key->okmc_idx, OKMC_FIELD_RELEASE_ONLY);
                changed = true;
            }
            // Continue pressing
            else if (key->travel >= travel_cfg->deep_act * TRAVEL_SCALE) {
                key->state = OKMC_DEEP_ACTUATED;
                okmc_enqueue(key->okmc_idx, OKMC_FIELD_DEEP_ACT);
                changed = true;
            }
            break;

        case OKMC_DEEP_ACTUATED:
            if (key->travel > travel_cfg->deep_deact * TRAVEL_SCALE) {
                key->state = OKMC_DEEP_DEACT_READY; // make su
            } else if (key->travel < travel_cfg->shallow_deact * TRAVEL_SCALE && key->travel < (travel_cfg->shallow_act - 1) * TRAVEL_SCALE) {
                key->state = OKMC_RELEASED;
                okmc_enqueue(key->okmc_idx, OKMC_FIELD_RELEASE_ONLY);
                changed = true;
            }
            break;

        case OKMC_DEEP_DEACT_READY:
            if (key->travel <= travel_cfg->deep_deact * TRAVEL_SCALE) {
                key->state = OKMC_DEEP_DEACTUATED;
                okmc_enqueue(key->okmc_idx, OKMC_FIELD_DEEP_DEACT);
                changed = true;
            }
            break;

        case OKMC_DEEP_DEACTUATED:
            // If we miss the deep deacuation point
            if (key->travel <= travel_cfg->shallow_deact * TRAVEL_SCALE && key->travel < (travel_cfg->shallow_act - 1) * TRAVEL_SCALE) {
                key->state = OKMC_RELEASED;
                okmc_enqueue(key->okmc_idx, OKMC_FIELD_SHALLOW_DEACT);
                changed = true;
            }
            break;

        default:
            break;
    }

    if (changed) {
        if (key->state >= OKMC_SHALLOW_ACTUATED && key->state <= OKMC_DEEP_DEACT_READY)
            okmc_matrix[key->r] |= 0x01 << key->c;
        else
            okmc_matrix[key->r] &= ~(0x01 << key->c);
    }

    return changed;
}

void okmc_release_all_active(void) {
    analog_matrix_profile_t *cur = profile_get_current();
    if (!cur) return;
    extern analog_key_t analog_key_matrix[MATRIX_ROWS][MATRIX_COLS];
    for (uint8_t r = 0; r < MATRIX_ROWS; ++r) {
        for (uint8_t c = 0; c < MATRIX_COLS; ++c) {
            analog_key_t *k = &analog_key_matrix[r][c];
            // Cualquier estado != RELEASED puede tener salidas aun presionadas.
            // En particular DEEP_DEACTUATED: el deep-deact ya corrio pero el
            // shallow-deact no, asi que las salidas del shallow-act siguen
            // abajo — excluirlo dejaria esas teclas pegadas al cambiar perfil.
            if (k->mode == AKM_DKS && k->state != OKMC_RELEASED) {
                if (k->okmc_idx < OKMC_COUNT) {
                    release_okmc_keys(&cur->okmc[k->okmc_idx]);
                }
            }
        }
    }
}

void okmc_clear(void) {
   memset(okmc_matrix, 0, sizeof(okmc_matrix));
   okmc_q_head  = 0;
   okmc_q_count = 0;
   okmc_q_bit   = 0;
}
