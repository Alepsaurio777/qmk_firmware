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

#pragma once

#ifndef PROFILE_COUNT
#    define PROFILE_COUNT 3
#endif

#ifndef OKMC_COUNT
#    define OKMC_COUNT 20
#endif

#ifndef SOCD_COUNT
#    define SOCD_COUNT 20
#endif

#define CURVE_POINTS_COUNT 4

// Version del PROTOCOLO HID que Launcher consulta (AMC_GET_VERSION responde
// con el byte bajo). Launcher solo entiende las versiones que conoce: subirla
// degrada su UI analogica (sin visualizacion de presion, paneles RT/DKS
// apagados — comprobado 17-jul con 0x...05). NO cambiarla por cambios de
// layout de EEPROM; para eso esta KC_ANALOG_MATRIX_EEPROM_VERSION.
#define KC_ANALOG_MATRIX_VERSION 0x34340004

// Version del LAYOUT del datablock EEPROM (independiente del protocolo HID).
// 0x34340005 (17-jul-2026): ead3fa0 desplazo OFFSET_CALIBRATED_DATA_START +1
// (y con el, OFFSET_PROFILES_START) sin subir version; el guard de
// analog_matrix_eeconfig_init() usa esta para detectar EEPROM de layout viejo
// y resetear perfiles a default en vez de cargarlos desalineados.
// 0x34340006 (04-sep-2026): reset limpio de perfiles para deshacerse de ajustes
// residuales de Rapid/Continuous RT dejados por firmware experimentales.
// 0x34340007 (06-sep-2026): Perfil 1 (Gaming) activo por defecto y persistente.
#define KC_ANALOG_MATRIX_EEPROM_VERSION 0x34340007
#define SIZE_OF_CALIB_VALUE_T 3       // Size of calibrated_value_t
#define SIZE_OF_ANALOG_KEY_CONFIG_T 4 // Size of analog_key_config_t
#define SIZE_OF_OKMC_CONFIG_T 19      // Size of okmc_config_t
#define SIZE_OF_SOCD_CONFIG_T 3       // Size of socd_config_t
#define SIZE_OF_POINT_T 2             // Size of point_t
#define PROFILE_NAME_LEN 30

// clang-format off
#define PROFILE_SIZE (                                                  \
    2 +                                                                 \
    (SIZE_OF_ANALOG_KEY_CONFIG_T) * (MATRIX_ROWS * MATRIX_COLS + 1) +   \
    (PROFILE_NAME_LEN) +                                                \
    (SIZE_OF_OKMC_CONFIG_T) * (OKMC_COUNT) +                            \
    (SIZE_OF_SOCD_CONFIG_T) * (SOCD_COUNT)                              \
)

// clang-format on
#define OFFSET_CALIBRATION 0
#define OFFSET_CURRENT_PROFILE (OFFSET_CALIBRATION + 1)

#define OFFSET_CALIBRATED_DATA_START (OFFSET_CURRENT_PROFILE + 1)
#define OFFSET_CALIBRATED_DATA_END (OFFSET_CALIBRATED_DATA_START + MATRIX_ROWS * MATRIX_COLS * SIZE_OF_CALIB_VALUE_T)

#define OFFSET_PROFILES_START (OFFSET_CALIBRATED_DATA_END)
#define OFFSET_PROFILES_END (OFFSET_PROFILES_START + PROFILE_SIZE * PROFILE_COUNT)

#define OFFSET_CURVE_PTS_START (OFFSET_PROFILES_END )
#define OFFSET_CURVE_PTS_END (OFFSET_CURVE_PTS_START + CURVE_POINTS_COUNT * SIZE_OF_POINT_T)

#define OFFSET_GAME_CONTROLLER_MODE_START OFFSET_CURVE_PTS_END
#define OFFSET_GAME_CONTROLLER_MODE_END (OFFSET_GAME_CONTROLLER_MODE_START + 1)

/* Size of analog matrix eeconfig */
#define EECONFIG_SIZE_ANALOG_MATRIX OFFSET_GAME_CONTROLLER_MODE_END
/* EE config data version */
#define EECONFIG_KB_DATA_VERSION KC_ANALOG_MATRIX_EEPROM_VERSION

#define EXTERNAL_EEPROM_OFFSET 4
