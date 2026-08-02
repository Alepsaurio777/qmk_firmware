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
#include "action_socd.h"

uint8_t profile_gobal_mode[PROFILE_COUNT] = {
    AKM_REGULAR, // perfil 0 (Win/productividad): actuacion estatica
    AKM_REGULAR, // perfil 1 (gaming): estatico por defecto; rapid trigger solo
                 // en las teclas de movimiento marcadas en default_profiles[1]
                 // (WASD, espacio, LShift, LCtrl). El resto no necesita RT.
    AKM_RAPID,
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

// clang-format off
const uint16_t PROGMEM default_profiles[PROFILE_COUNT][MATRIX_ROWS][MATRIX_COLS] = {
    [0] = LAYOUT_ansi_84(
        0,       0,       0,       0,       0,       0,       0,       0,       0,       0,       0,       0,       0,       0,       0,       0,
        0,       0,       0,       0,       0,       0,       0,       0,       0,       0,       0,       0,       0,       0,                0,
        0,       0,       0,       0,       0,       0,       0,       0,       0,       0,       0,       0,       0,       0,                0,
        0,       0,       0,       0,       0,       0,       0,       0,       0,       0,       0,       0,                0,                0,
        0,                0,       0,       0,       0,       0,       0,       0,       0,       0,       0,                0,       0,       0,
        0,       0,       0,                                  0,                                  0,       0,       0,       0,       0,       0),

    // Perfil gaming: 2 = AKM_RAPID por tecla. Solo movimiento: W, A, S, D,
    // espacio, LShift, LCtrl. El resto hereda el global REGULAR (estatico).
    [1] = LAYOUT_ansi_84(
        0,       0,       0,       0,       0,       0,       0,       0,       0,       0,       0,       0,       0,       0,       0,       0,
        0,       0,       0,       0,       0,       0,       0,       0,       0,       0,       0,       0,       0,       0,                0,
        0,       0,       2,       0,       0,       0,       0,       0,       0,       0,       0,       0,       0,       0,                0,
        0,       2,       2,       2,       0,       0,       0,       0,       0,       0,       0,       0,                0,                0,
        2,                0,       0,       0,       0,       0,       0,       0,       0,       0,       0,                0,       0,       0,
        2,       0,       0,                                  2,                                  0,       0,       0,       0,       0,       0),

    // Perfil 3 sin uso: era el gamepad Xbox de fabrica, eliminado.
    [2] = LAYOUT_ansi_84(
        0,       0,       0,       0,       0,       0,       0,       0,       0,       0,       0,       0,       0,       0,       0,       0,
        0,       0,       0,       0,       0,       0,       0,       0,       0,       0,       0,       0,       0,       0,                0,
        0,       0,       0,       0,       0,       0,       0,       0,       0,       0,       0,       0,       0,       0,                0,
        0,       0,       0,       0,       0,       0,       0,       0,       0,       0,       0,       0,                0,                0,
        0,                0,       0,       0,       0,       0,       0,       0,       0,       0,       0,                0,       0,       0,
        0,       0,       0,                                  0,                                  0,       0,       0,       0,       0,       0)
};

// ===========================================================================
// Afinado de la tabla de RESET, por keycode
// ===========================================================================
// (1-ago) profile_reset() sembraba solo el MODO por tecla. act_pt,
// rpd_trig_sen y rpd_trig_sen_deact quedaban a 0 heredando el global, asi que
// la afinacion fina de torneo vivia unicamente en la EEPROM de Launcher: no en
// git, no en el .bin, no revisable en un diff. Un "Reset Profile" o la
// migracion de layout de EEPROM que analog_matrix.c ya contempla la revertian
// en silencio a un unico punto de actuacion global.
//
// Esto es el MECANISMO. Los numeros son tuyos.
//
// COMO RELLENARLO: mira en Launcher la actuacion y las sensibilidades que de
// verdad usas por tecla en el perfil gaming, y escribelas aqui. A partir de ese
// commit, un reset aterriza en tu configuracion de torneo en vez de en los
// defaults genericos, y cualquier cambio futuro se ve en un diff.
//
// Deliberadamente se deja VACIO en vez de inventar valores: sembrar una
// actuacion equivocada seria peor que no sembrar ninguna — un reset dejaria el
// teclado en una config que nadie eligio, con la apariencia de ser la buena.
//
// Unidades: 0.1 mm. 0 = hereda el global del perfil. Formato:
//   {KC_W,   4, 3, 2},   // W: actuacion 0.4 mm, press 0.3, release 0.2
static const profile_key_tuning_t tuning_gaming[] = {
    // {KC_W,     0, 0, 0},
    // {KC_A,     0, 0, 0},
    // {KC_S,     0, 0, 0},
    // {KC_D,     0, 0, 0},
    // {KC_SPACE, 0, 0, 0},
    // {KC_LSFT,  0, 0, 0},
    {KC_NO, 0, 0, 0}, // fin de lista
};

const profile_key_tuning_t *profile_key_tuning(uint8_t prof_idx) {
    return (prof_idx == 1) ? tuning_gaming : NULL;
}

// ---------------------------------------------------------------------------
// Par SOCD sembrado (Rappy Snappy en A/D)
// ---------------------------------------------------------------------------
// Este SI se siembra, porque el hueco era claro: k2_he/config.h dice
// ANALOG_DISABLE_SOCD_IN_GAMING_MODE 0 y DEVELOPMENT.md habla del Rappy Snappy
// como una feature viva, pero default_profiles[] nunca sembro ningun par — asi
// que tras un reset SOCD quedaba apagado y la config mentia.
//
// SOCD_PRI_DEEPER_TRAVEL: gana la tecla mas hundida, con la histeresis de
// ANALOG_SOCD_DEEPER_HYSTERESIS contra el chatter A/D. Es el modo que el
// firmware ya implementa con mas cuidado (ver action_socd.c).
//
// ZONA GRIS, y conviene tenerlo presente: no esta prohibido explicitamente en
// Minemen/Hypixel, pero SI esta baneado en CS2/ESL. Si eso cambia, se quita de
// aqui y el reset deja de sembrarlo.
static const profile_socd_seed_t socd_gaming[] = {
    {KC_A, KC_D, SOCD_PRI_DEEPER_TRAVEL},
    {KC_NO, KC_NO, 0}, // fin de lista
};

const profile_socd_seed_t *profile_socd_seeds(uint8_t prof_idx) {
    return (prof_idx == 1) ? socd_gaming : NULL;
}
