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

#include "stdint.h"
#include "analog_matrix.h"
#include "action.h"

// ---------------------------------------------------------------------------
// Afinado por tecla de la tabla de RESET, declarado por KEYCODE
// ---------------------------------------------------------------------------
// (1-ago) Hasta ahora profile_reset() sembraba SOLO el modo por tecla; act_pt,
// rpd_trig_sen y rpd_trig_sen_deact quedaban a 0 heredando el global. La
// consecuencia: la afinacion real de torneo vivia UNICAMENTE en la EEPROM que
// escribe Launcher — no en git, no en el .bin, no revisable en un diff — y un
// "Reset Profile" o una migracion de layout de EEPROM (que analog_matrix.c ya
// contempla y fuerza) la revertian en silencio a un unico punto de actuacion
// global para todas las teclas.
//
// Se declara por KEYCODE y no por coordenada, por la misma razon que las
// whitelists de politica: describe MECANICA ("la tecla de saltar"), no
// geometria, y sobrevive a un remap desde Launcher. La resolucion es legitima
// aqui porque via_init() corre antes que matrix_init_custom() -> profile_init().
//
// keycode == KC_NO termina la lista.
typedef struct {
    uint16_t keycode;
    uint8_t  act_pt;  // 0.1 mm; 0 = hereda el global del perfil
    uint8_t  sen;     // sensibilidad RT de press;  0 = hereda
    uint8_t  sen_rls; // sensibilidad RT de release; 0 = hereda
} profile_key_tuning_t;

// Definido por teclado (k2_he/ansi/profiles.c). Weak por defecto = lista vacia.
extern const profile_key_tuning_t *profile_key_tuning(uint8_t prof_idx);

// Par SOCD sembrado en la tabla de reset. Sin esto, tras un reset el Rappy
// Snappy queda APAGADO aunque ANALOG_DISABLE_SOCD_IN_GAMING_MODE sugiera lo
// contrario: default_profiles[] nunca sembro ninguno.
typedef struct {
    uint16_t keycode_1;
    uint16_t keycode_2;
    uint8_t  type; // socd_type_t; 0 = fin de lista
} profile_socd_seed_t;

extern const profile_socd_seed_t *profile_socd_seeds(uint8_t prof_idx);

void profile_init(bool reset);
analog_matrix_profile_t *profile_get(uint8_t index);
analog_matrix_profile_t* profile_get_current(void);
uint8_t profile_get_current_index(void);
bool profile_select(uint8_t prof_idx, bool indication, bool save_eeprom);
bool profile_get_raw_data(uint8_t prof_idx, uint16_t offset, uint8_t size, uint8_t *data);
bool profile_set_traval(uint8_t prof_idx, uint8_t mode, uint8_t act_pt, uint8_t sens, uint8_t rls_sens, bool global, uint32_t row[]);
bool profile_set_name(uint8_t prof_idx, uint8_t len, uint8_t *name);
bool profile_set_socd(uint8_t *data);
bool profile_reset(uint8_t prof_index);
bool profile_save(uint8_t prof_index);
bool profile_set_adv_mode(uint8_t *data);
void process_profile_select_combo(void);
bool process_record_profile(uint16_t keycode, keyrecord_t *record);

void profile_indication_enable(void);
void profile_indication_timer_check(void);
void profile_indication(void);
