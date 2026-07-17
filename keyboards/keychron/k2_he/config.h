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

#include "eeconfig_kb.h"

/* External EEPROM Configuration*/
#define I2C_DRIVER I2CD3
#define I2C1_SCL_PIN A8
#define I2C1_SDA_PIN C9
#define EXTERNAL_EEPROM_WP_PIN B10

/* Analog Matrix Configuration */
#define ANALOG_MATRIX_POWER_PIN C13
#define ANALOG_MATRIX_POWER_ENABLE_LEVEL 1
#define ANALOG_MATRIX_WAKEUP_PIN C5

/* Joystick Configuration */
#ifdef JOYSTICK_ENABLE
#    define JOYSTICK_AXIS_COUNT 6
#    define JOYSTICK_BUTTON_COUNT 16
#endif

/* SPI Configuration */
#if defined(RGB_MATRIX_ENABLE) || defined(LK_WIRELESS_ENABLE)
#    define SPI_DRIVER SPID1
#    define SPI_SCK_PIN A5
#    define SPI_MISO_PIN A6
#    define SPI_MOSI_PIN A7
#endif

/* SNLED27351 Driver Configuration */
#if defined(RGB_MATRIX_ENABLE)
#    define SNLED27351_SELECT_PINS \
        { B8, B9 }
#    define SNLED27351_SDB_PIN B7
#    define SNLED27351_PHASE_CHANNEL SNLED27351_SCAN_PHASE_9_CHANNEL
#    define SNLED27351_SPI_DIVISOR 8
#endif

/* Wireless Configuration */
#ifdef LK_WIRELESS_ENABLE
/* Hardware Configuration */
#    define P24G_MODE_SELECT_PIN A9
#    define BT_MODE_SELECT_PIN A10

#    define LKBT51_RESET_PIN C4
#    define WIRELESS_TO_MCU_INT_PIN B1
#    define MCU_TO_WIRELESS_INT_PIN A4

#    define USB_POWER_SENSE_PIN B0
#    define USB_POWER_CONNECTED_LEVEL 0

#    define BAT_CHARGING_PIN B13
#    define BAT_CHARGING_LEVEL 0

#    if defined(RGB_MATRIX_ENABLE)
#        define BT_INDCATION_LED_MATRIX_LIST \
            { 17, 18, 19 }
#        define P24G_INDICATION_LED_INDEX 20

#        define BAT_LEVEL_LED_LIST \
            { 17, 18, 19, 20, 21, 22, 23, 24, 25, 26 }

/* Reinit LED driver on tranport changed */
#        define LED_DRIVER_REINIT_ON_TRANSPORT_CHANGE
#    endif

/* Keep USB connection in wireless mode */
#    define KEEP_USB_CONNECTION_IN_WIRELESS_MODE

/* Enable wireless NKRO */
#    define WIRELESS_NKRO_ENABLE
#endif

/* Factory Test Keys */
#define FN_KEY_1 MO(1)
#define FN_KEY_2 MO(3)
#define FN_BL_TRIG_KEY KC_END

// Polling rate fijado en compile-time: div 0 = un reporte por frame USB. En
// este STM32F401 el USB es Full-Speed, asi que el techo real es 1 kHz (no 8 kHz,
// que exigiria High-Speed). KEYCHRON_FIXED_REPORT_RATE ignora la EEPROM y bloquea
// cambios en runtime (HID de Launcher y combos Fn): rate deterministico por boot.
#define KEYCHRON_DEFAULT_REPORT_RATE_DIV 0
#define KEYCHRON_FIXED_REPORT_RATE

// Disable QMK core debounce to prevent rapid trigger delay
#define DEBOUNCE 0

// K2 HE gaming build: keep calibration dead zone minimal; top-out filtering is
// handled dynamically in convert_to_travel().
#define ZERO_TRAVEL_DEAD_ZONE 2
#define TOP_OUT_DEAD_ZONE_GAMING 12
#define TOP_OUT_DEAD_ZONE_TYPING 20
#define STATIC_HYSTERESIS_GAMING 5
#define STATIC_HYSTERESIS_GAMING_FAST_KEY 2
#define ANALOG_ADAPTIVE_SHALLOW_HYSTERESIS_GAMING 1
#define ANALOG_GAMING_FAST_KEY_ROW 5
#define ANALOG_GAMING_FAST_KEY_COL 6
#define ANALOG_CONTINUOUS_RT_KEY1_ROW 5
#define ANALOG_CONTINUOUS_RT_KEY1_COL 6
#define ANALOG_CONTINUOUS_RT_KEY2_ROW 4
#define ANALOG_CONTINUOUS_RT_KEY2_COL 0
#define ANALOG_GAMING_DEFAULT_RAPID_PROFILE 1
#define ANALOG_GAMING_DEFAULT_REGULAR_PROFILE 1
#define STATIC_HYSTERESIS_TYPING 5
#define ANALOG_RAW_NOISE_FILTER_GAMING 5
#define ANALOG_RAW_NOISE_FILTER_TYPING 5

// Disable analog ADC debounce loop to ensure absolute minimum latency (scans 1 time instead of 3)
#define ANALOG_DEBOUNCE_TIME 1

// Competition scan path: 18 MHz ADC clock (halconf) with 28-cycle sample time.
// Six channels convert in about 13.3 us instead of about 45.3 us at DIV8/56 cycles.
#define ANALOG_ADC_SAMPLE_TIME ADC_SAMPLE_28
#define ANALOG_SELECT_SETTLE_US 20

// Remove FPU work and background auto-calibration saves from the scan hot path.
#define ANALOG_FIXED_POINT_TRAVEL 1
#define ANALOG_AUTO_CALIBRATION_ENABLE 0

// Fase 2c: aprender bottom-out por tecla desde uso real, en housekeeping (no
// en el scan). Solo-crece: el rango dinamico nunca puede degradarse solo.
// Complementa la calibracion de reposo de arranque (CALIB_ZERO_TRAVEL_POWER_ON)
// que ya compensa drift termico en cada boot.
// Default overridable por keymap: alex (torneo) lo APAGA — el drift se descarto
// con datos (sesion de 24k eventos, actuacion mediana plana en 25.0) y un
// aprendedor adaptativo contradice el objetivo "nada cambia durante la partida".
// alex_lab lo mantiene en 1 para experimentar con drift termico/outliers.
#ifndef ANALOG_BOTTOM_OUT_LEARN
#    define ANALOG_BOTTOM_OUT_LEARN 1
#endif

// In Gaming layers, ignore Launcher advanced mappings that can synthesize or
// latch input and fall back to the key's base profile mode.
#define ANALOG_DISABLE_OKMC_IN_GAMING_MODE 1
#define ANALOG_DISABLE_TOGGLE_IN_GAMING_MODE 1
#define ANALOG_DISABLE_GAMEPAD_IN_GAMING_MODE 1
// SOCD habilitado para pruebas en MC 1.8.9 (baneado en CS2; verificar reglas
// del server antes de usar en ranked/torneos).
#define ANALOG_DISABLE_SOCD_IN_GAMING_MODE 0
#define ANALOG_DISABLE_PROFILE_COMBO_IN_GAMING_MODE 1
#define ANALOG_CONTINUOUS_RAPID_TRIGGER_IN_GAMING_MODE 0
#define ANALOG_CONTINUOUS_RT_REPRESS_MAX_TRAVEL 240
// Toggles de prediccion controlados por keymap (alex = off, alex_lab = on).
// No se fuerzan aqui: analog_matrix.h los deja en 0 por defecto y el config.h
// del keymap los sube a 1 si aplica. Las coordenadas/parametros de abajo son
// compartidos e inertes cuando la prediccion esta apagada.
#define ANALOG_PREDICTIVE_ACTUATION_ADVANCE TRAVEL_SCALE
#define ANALOG_PREDICTIVE_ACTUATION_MIN_DELTA TRAVEL_SCALE
#define ANALOG_PREDICTIVE_RT_KEY3_ROW 2 // W
#define ANALOG_PREDICTIVE_RT_KEY3_COL 2
#define ANALOG_PREDICTIVE_RT_KEY4_ROW 3 // A
#define ANALOG_PREDICTIVE_RT_KEY4_COL 1
#define ANALOG_PREDICTIVE_RT_KEY5_ROW 3 // S
#define ANALOG_PREDICTIVE_RT_KEY5_COL 2
#define ANALOG_PREDICTIVE_RT_KEY6_ROW 3 // D
#define ANALOG_PREDICTIVE_RT_KEY6_COL 3
#define ANALOG_PREDICTIVE_REGULAR_KEY1_ROW 2 // W
#define ANALOG_PREDICTIVE_REGULAR_KEY1_COL 2
#define ANALOG_PREDICTIVE_REGULAR_KEY2_ROW 3 // A
#define ANALOG_PREDICTIVE_REGULAR_KEY2_COL 1
#define ANALOG_PREDICTIVE_REGULAR_KEY3_ROW 3 // S
#define ANALOG_PREDICTIVE_REGULAR_KEY3_COL 2
#define ANALOG_PREDICTIVE_REGULAR_KEY4_ROW 3 // D
#define ANALOG_PREDICTIVE_REGULAR_KEY4_COL 3

// Tap-hold configurations to make spacebar/other keys feel responsive if mapped as layer-taps or mod-taps
#define TAPPING_TERM 175
#define QUICK_TAP_TERM 120
#define PERMISSIVE_HOLD


