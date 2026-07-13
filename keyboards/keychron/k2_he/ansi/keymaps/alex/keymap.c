/* Copyright 2024 ~ 2025 @ Keychron (https://www.keychron.com)
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

#include QMK_KEYBOARD_H
#include "analog_matrix.h"
#include "keychron_common.h"
#include "profile.h"
#include "telemetry.h"

enum custom_keycodes {
    TELEM_TG = QK_USER_0, // toggle telemetria de profundidad (solo Win/productividad)
};

enum layers {
    GAMING_BASE,
    GAMING_FN,
    WIN_BASE,
    WIN_FN,
};

#define FN_GAMING KC_NO
#define FN_WIN MO(WIN_FN)

// clang-format off
const uint16_t PROGMEM keymaps[][MATRIX_ROWS][MATRIX_COLS] = {
    [GAMING_BASE] = LAYOUT_ansi_84(
        KC_ESC,   KC_F1,    KC_F2,    KC_F3,    KC_F4,    KC_F5,    KC_F6,    KC_F7,    KC_F8,    KC_F9,    KC_F10,   KC_F11,   KC_F12,   KC_PSCR,  KC_DEL,   KC_NO,
        KC_GRV,   KC_1,     KC_2,     KC_3,     KC_4,     KC_5,     KC_6,     KC_7,     KC_8,     KC_9,     KC_0,     KC_MINS,  KC_EQL,   KC_BSPC,            KC_PGUP,
        KC_TAB,   KC_Q,     KC_W,     KC_E,     KC_R,     KC_T,     KC_Y,     KC_U,     KC_I,     KC_O,     KC_P,     KC_LBRC,  KC_RBRC,  KC_BSLS,            KC_PGDN,
        KC_CAPS,  KC_A,     KC_S,     KC_D,     KC_F,     KC_G,     KC_H,     KC_J,     KC_K,     KC_L,     KC_SCLN,  KC_QUOT,            KC_ENT,             KC_HOME,
        KC_LSFT,            KC_Z,     KC_X,     KC_C,     KC_V,     KC_B,     KC_N,     KC_M,     KC_COMM,  KC_DOT,   KC_SLSH,            KC_RSFT,  KC_UP,    KC_END,
        KC_LCTL,  KC_LGUI,  KC_LALT,                                KC_SPC,                                 KC_RALT,  FN_GAMING,KC_RCTL,  KC_LEFT,  KC_DOWN,  KC_RGHT),

    // Capa inalcanzable en Gaming (FN_GAMING = KC_NO y process_record_user
    // bloquea todo layer-switch); se mantiene vacia solo para conservar la
    // numeracion de capas que espera el interruptor fisico.
    [GAMING_FN] = LAYOUT_ansi_84(
        _______,  _______,  _______,  _______,  _______,  _______,  _______,  _______,  _______,  _______,  _______,  _______,  _______,  _______,  _______,  _______,
        _______,  _______,  _______,  _______,  _______,  _______,  _______,  _______,  _______,  _______,  _______,  _______,  _______,  _______,            _______,
        _______,  _______,  _______,  _______,  _______,  _______,  _______,  _______,  _______,  _______,  _______,  _______,  _______,  _______,            _______,
        _______,  _______,  _______,  _______,  _______,  _______,  _______,  _______,  _______,  _______,  _______,  _______,            _______,            _______,
        _______,            _______,  _______,  _______,  _______,  _______,  _______,  _______,  _______,  _______,  _______,            _______,  _______,  _______,
        _______,  _______,  _______,                                _______,                                _______,  _______,  _______,  _______,  _______,  _______),

    [WIN_BASE] = LAYOUT_ansi_84(
        KC_ESC,   KC_F1,    KC_F2,    KC_F3,    KC_F4,    KC_F5,    KC_F6,    KC_F7,    KC_F8,    KC_F9,    KC_F10,   KC_F11,   KC_F12,   KC_PSCR,  KC_DEL,   UG_NEXT,
        KC_GRV,   KC_1,     KC_2,     KC_3,     KC_4,     KC_5,     KC_6,     KC_7,     KC_8,     KC_9,     KC_0,     KC_MINS,  KC_EQL,   KC_BSPC,            KC_PGUP,
        KC_TAB,   KC_Q,     KC_W,     KC_E,     KC_R,     KC_T,     KC_Y,     KC_U,     KC_I,     KC_O,     KC_P,     KC_LBRC,  KC_RBRC,  KC_BSLS,            KC_PGDN,
        KC_CAPS,  KC_A,     KC_S,     KC_D,     KC_F,     KC_G,     KC_H,     KC_J,     KC_K,     KC_L,     KC_SCLN,  KC_QUOT,            KC_ENT,             KC_HOME,
        KC_LSFT,            KC_Z,     KC_X,     KC_C,     KC_V,     KC_B,     KC_N,     KC_M,     KC_COMM,  KC_DOT,   KC_SLSH,            KC_RSFT,  KC_UP,    KC_END,
        KC_LCTL,  KC_LGUI,  KC_LALT,                                KC_SPC,                                 KC_RALT,  FN_WIN,   KC_RCTL,  KC_LEFT,  KC_DOWN,  KC_RGHT),

    [WIN_FN] = LAYOUT_ansi_84(
        _______,  KC_BRID,  KC_BRIU,  KC_TASK,  KC_FILE,  UG_VALD,  UG_VALU,  KC_MPRV,  KC_MPLY,  KC_MNXT,  KC_MUTE,  KC_VOLD,  KC_VOLU,  _______,  _______,  UG_TOGG,
        _______,  BT_HST1,  BT_HST2,  BT_HST3,  P2P4G,    _______,  _______,  _______,  _______,  _______,  _______,  _______,  _______,  _______,            _______,
        UG_TOGG,  UG_NEXT,  UG_VALU,  UG_HUEU,  UG_SATU,  UG_SPDU,  TELEM_TG, _______,  _______,  _______,  _______,  _______,  _______,  _______,            _______,
        _______,  UG_PREV,  UG_VALD,  UG_HUED,  UG_SATD,  UG_SPDD,  _______,  _______,  _______,  _______,  _______,  _______,            _______,            _______,
        _______,            PROF1,    PROF2,    PROF3,    _______,  BAT_LVL,  _______,  _______,  _______,  _______,  _______,            _______,  _______,  _______,
        _______,  _______,  _______,                                _______,                                _______,  _______,  _______,  _______,  _______,  _______)
};
// clang-format on

static inline bool is_layer_switch_keycode(uint16_t keycode) {
    return IS_QK_LAYER_TAP(keycode) ||
           IS_QK_LAYER_MOD(keycode) ||
           IS_QK_TO(keycode) ||
           IS_QK_MOMENTARY(keycode) ||
           IS_QK_DEF_LAYER(keycode) ||
           IS_QK_TOGGLE_LAYER(keycode) ||
           IS_QK_ONE_SHOT_LAYER(keycode) ||
           IS_QK_LAYER_TAP_TOGGLE(keycode);
}

static inline bool is_profile_select_keycode(uint16_t keycode) {
    return keycode >= PROF1 && keycode <= PROF3;
}

static bool    pending_profile_rebuild = false;
static uint8_t pending_profile_index   = 0;

static inline void schedule_profile_rebuild(uint8_t profile_index) {
    pending_profile_index   = profile_index;
    pending_profile_rebuild = true;
}

layer_state_t default_layer_state_set_user(layer_state_t state) {
    if (state & (1UL << WIN_BASE)) {
        // Defer profile rebuild until default_layer_state has been committed.
        schedule_profile_rebuild(0);
    } else if (state & (1UL << GAMING_BASE)) {
        schedule_profile_rebuild(1);
    }
    return state;
}

void housekeeping_task_user(void) {
    telemetry_task();

    if (!pending_profile_rebuild) return;

    pending_profile_rebuild = false;

    const uint8_t profile_index = pending_profile_index;
    const bool    same_profile  = profile_get_current_index() == profile_index;

    if (profile_select(profile_index, false, false) && same_profile) {
        // profile_select() rebuilds configs only when the profile index changes.
        // Force a rebuild here so Win/Gaming hysteresis and advanced-mode
        // fallbacks are computed after the default layer transition settles.
        update_travel_configs();
    }
}

bool process_record_user(uint16_t keycode, keyrecord_t *record) {
    if (keycode == TELEM_TG) {
        if (record->event.pressed) telemetry_toggle();
        return false;
    }

    // Si estamos en modo Gaming (Interruptor fisico en Mac = Capas 0 y 1)
    if (analog_matrix_is_gaming_mode()) {
        // Bloquear todas las Macros de VIA
        if (keycode >= QK_MACRO && keycode <= QK_MACRO_MAX) {
            return false;
        }

        // Bloquear cualquier cambio de capa en Gaming, aunque se remapee desde Launcher.
        if (is_layer_switch_keycode(keycode)) {
            return false;
        }

        // Bloquear cambios de perfil HE en Gaming.
        if (is_profile_select_keycode(keycode)) {
            return false;
        }

        // Bloquear keycodes de sistema que podrian interrumpir o danar la sesion.
        if (keycode == QK_BOOTLOADER || keycode == QK_REBOOT || keycode == QK_CLEAR_EEPROM) {
            return false;
        }

        // Bloquear QK_MAGIC: GU_TOGG, NK_TOGG, swaps de Ctrl/GUI/Alt, etc.
        if (IS_QK_MAGIC(keycode)) {
            return false;
        }

#if defined(LK_WIRELESS_ENABLE) || defined(KC_BLUETOOTH_ENABLE)
        // Bloquear keycodes wireless/battery que no sirven en Gaming.
        if (keycode == BAT_LVL || keycode == BT_HST1 || keycode == BT_HST2 ||
            keycode == BT_HST3 || keycode == P2P4G) {
            return false;
        }
#endif
    }
    return true; // Permitir que QMK procese todo lo demas de forma normal
}
