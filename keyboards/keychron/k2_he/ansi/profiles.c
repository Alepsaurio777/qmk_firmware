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
#include "analog_matrix_type.h"
#include "profile.h"
#include "mc189_profile_defaults.h"

uint8_t profile_gobal_mode[PROFILE_COUNT] = {
    AKM_REGULAR, // perfil 0 (Win/productividad): actuacion estatica
    AKM_RAPID,   // perfil 1 (gaming): Rapid Trigger global para todas las teclas
    AKM_REGULAR, // perfil 3 (antes gamepad, sin uso): sin modo forzado por hardware.
};

// Sensibilidad RT por perfil, unidades de 0.1 mm (release 0 = hereda press).
// Perfil 1 (gaming) asimetrico: release 0.2 mm (telemetria 12-jul: ruido
// post-filtro con dedos apoyados = 0, margen sobrado), re-press 0.3 mm para
// que la vibracion del dedo tras jump-reset/w-tap no re-dispare.
const uint8_t profile_default_rt_sen[PROFILE_COUNT] = {
    4, // typing: 0.4 mm simetrico (default de fabrica)
    3, // gaming: press 0.3 mm
    3,
};
const uint8_t profile_default_rt_sen_rls[PROFILE_COUNT] = {
    0, // typing: hereda (0.4 mm)
    2, // gaming: release 0.2 mm
    2,
};

#if ANALOG_MC189_CALM_BRANCH
#    define PROFILE1_LCTRL_MODE MC189_GAMING_LCTRL_PROFILE_MODE
#else
#    define PROFILE1_LCTRL_MODE 2
#endif

// clang-format off
const uint16_t PROGMEM default_profiles[PROFILE_COUNT][MATRIX_ROWS][MATRIX_COLS] = {
    [0] = LAYOUT_ansi_84(
        0,       0,       0,       0,       0,       0,       0,       0,       0,       0,       0,       0,       0,       0,       0,       0,
        0,       0,       0,       0,       0,       0,       0,       0,       0,       0,       0,       0,       0,       0,                0,
        0,       0,       0,       0,       0,       0,       0,       0,       0,       0,       0,       0,       0,       0,                0,
        0,       0,       0,       0,       0,       0,       0,       0,       0,       0,       0,       0,                0,                0,
        0,                0,       0,       0,       0,       0,       0,       0,       0,       0,       0,                0,       0,       0,
        0,       0,       0,                                  0,                                  0,       0,       0,       0,       0,       0),

    // Perfil gaming: 0 = regular por defecto (limpio, sin perfil forzado por hardware).
    // Keychron Launcher y la EEPROM del usuario tienen el control total.
    [1] = LAYOUT_ansi_84(
        0,       0,       0,       0,       0,       0,       0,       0,       0,       0,       0,       0,       0,       0,       0,       0,
        0,       0,       0,       0,       0,       0,       0,       0,       0,       0,       0,       0,       0,       0,                0,
        0,       0,       0,       0,       0,       0,       0,       0,       0,       0,       0,       0,       0,       0,                0,
        0,       0,       0,       0,       0,       0,       0,       0,       0,       0,       0,       0,                0,                0,
        0,                0,       0,       0,       0,       0,       0,       0,       0,       0,       0,                0,       0,       0,
        0,       0,       0,                                  0,                                  0,       0,       0,       0,       0,       0),

    // Perfil 3 sin uso: era el gamepad Xbox de fabrica, eliminado.
    [2] = LAYOUT_ansi_84(
        0,       0,       0,       0,       0,       0,       0,       0,       0,       0,       0,       0,       0,       0,       0,       0,
        0,       0,       0,       0,       0,       0,       0,       0,       0,       0,       0,       0,       0,       0,                0,
        0,       0,       0,       0,       0,       0,       0,       0,       0,       0,       0,       0,       0,       0,                0,
        0,       0,       0,       0,       0,       0,       0,       0,       0,       0,       0,       0,                0,                0,
        0,                0,       0,       0,       0,       0,       0,       0,       0,       0,       0,                0,       0,       0,
        0,       0,       0,                                  0,                                  0,       0,       0,       0,       0,       0)
};
#undef PROFILE1_LCTRL_MODE

const profile_key_tuning_t *profile_key_tuning(uint8_t prof_idx) {
    (void)prof_idx;
    return NULL;
}

// SOCD no se siembra desde firmware. El motor permanece disponible, pero los
// pares y su prioridad son configuracion explicita del usuario en Keychron
// Launcher. Un Reset Profile debe volver a un perfil sin pares SOCD implicitos.
