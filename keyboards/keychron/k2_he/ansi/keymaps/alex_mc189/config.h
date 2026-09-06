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

// MC 1.8.9 CALM / CONTINUOUS RT branch.
// Parte del binario estable `alex`, no de alex_lab/AJ2. El objetivo es aislar
// una respuesta Hall directa (actuacion superficial + RT continuo) sin
// predictores, F6/F9, noise learning ni timings analogicos agresivos.
#include "../alex/config.h"

#define ANALOG_MC189_CALM_BRANCH 1


// Continuous RT: activo en Gaming Mode (Perfil 1) para todas las teclas cuyo
// modo efectivo sea AKM_RAPID. Elimina la trampa de fondo (bottom guard trap)
// y permite liberacion y reactivacion inmediata siguiendo el pico dinamico
// hasta 245. En modo REGULAR esta politica no interviene.
#undef ANALOG_CONTINUOUS_RAPID_TRIGGER_IN_GAMING_MODE
#define ANALOG_CONTINUOUS_RAPID_TRIGGER_IN_GAMING_MODE 1


// MC189-v2: ya no usamos el cap experimental de 240 para re-press.
// Con el bug de peak >240 corregido, el umbral vuelve al maximo fisico normal
// (245 en K2 HE). Esto evita una aceleracion artificial adicional del re-press.
#undef ANALOG_CONTINUOUS_RT_REPRESS_MAX_TRAVEL
#define ANALOG_CONTINUOUS_RT_REPRESS_MAX_TRAVEL ((FULL_TRAVEL_UNIT + 1) * TRAVEL_SCALE - 1)

// --- EXPERIMENTAL: Polish de SOCD / Snap Tap ---
// 1. Histeresis de Rappy Snappy reducida: 6 -> 3 (0.05 mm) para alternar strafe A/D con menor recorrido.
#undef ANALOG_SOCD_DEEPER_HYSTERESIS
#define ANALOG_SOCD_DEEPER_HYSTERESIS 3

// 2. Retener ganador en bottom-out: en combate PvP evita la cancelacion de velocidad a 0 si ambas teclas tocan fondo.
#define ANALOG_SOCD_BOTTOM_OUT_HOLD_WINNER 1

// --- Sincronizacion de Tick MC 1.8.9 (Press Stretch F9) ---
// Garantiza >= 55 ms de pulso ON en Space para que el tick de 50 ms de 1.8.9 nunca pierda saltos en bhop/parkour.
#undef ANALOG_PRESS_STRETCH_IN_GAMING_MODE
#define ANALOG_PRESS_STRETCH_IN_GAMING_MODE 1
#undef ANALOG_PRESS_STRETCH_KEY2_KEYCODE
#define ANALOG_PRESS_STRETCH_KEY2_KEYCODE KC_NO
#undef ANALOG_PRESS_STRETCH_KEY3_KEYCODE
#define ANALOG_PRESS_STRETCH_KEY3_KEYCODE KC_NO

// --- EXPERIMENTAL: Actuacion Predictiva por Velocidad (F7) ---
// Anticipa 1-2 ms en counter-strafe A y D basandose en la aceleracion del dedo.
#undef ANALOG_PREDICTIVE_ACTUATION_IN_GAMING_MODE
#define ANALOG_PREDICTIVE_ACTUATION_IN_GAMING_MODE 0
#undef ANALOG_PREDICTIVE_PRESS_KEY_MASK
#define ANALOG_PREDICTIVE_PRESS_KEY_MASK 0x28    // Bits: A + D
#undef ANALOG_PREDICTIVE_REPRESS_KEY_MASK
#define ANALOG_PREDICTIVE_REPRESS_KEY_MASK 0x28  // Bits: A + D

#undef ANALOG_RELEASE_STRETCH_IN_GAMING_MODE
#define ANALOG_RELEASE_STRETCH_IN_GAMING_MODE 0

// Sin noise-floor adaptativo de LAB: usa el filtro estable del K2 HE/alex.
#undef ANALOG_STARTUP_NOISE_FLOOR_ENABLE
#define ANALOG_STARTUP_NOISE_FLOOR_ENABLE 0



// IMPORTANTE: al heredar `alex` incorpora las optimizaciones de latencia
// validadas para torneo (pipeline + SOF sync, buffer ping-pong ADC,
// ANALOG_ADC_SAMPLE_TIME = ADC_SAMPLE_15, ANALOG_SELECT_SETTLE_US = 14 us,
// HC164_DELAY_NOPS = 20 con delay exacto, TOP_OUT_DEAD_ZONE_GAMING = 4).
// Mantiene fixed-point y refcount, sin predictores ni noise learning de LAB.

// Hardware & Sensitivity Tuning:
// 1. Elimina la trampa de fondo (bottom guard trap) en switches de 4.0mm
#undef BOTTOM_DEAD_ZONE
#define BOTTOM_DEAD_ZONE 41

// 2. Shift register delay optimizado a 20 NOPs (277 ns a 72 MHz)
#undef HC164_DELAY_NOPS
#define HC164_DELAY_NOPS 20

// 3. Filtro de ruido optimizado (aprovechando VDD limpio sin RGB ni Wireless)
#undef ANALOG_RAW_NOISE_FILTER_GAMING
#define ANALOG_RAW_NOISE_FILTER_GAMING 3

// 4. Histeresis estatica rapida (0.3 mm en vez de 0.5 mm)
#undef STATIC_HYSTERESIS_GAMING
#define STATIC_HYSTERESIS_GAMING 3

// 5. Just-In-Time Scan Alignment (estilo Wooting Tachyon):
// Inicia el barrido desfasado 460 us del SOF para que concluya a ~920 us,
// entregando datos con frescura maxima (0 us de edad) justo ante el poll de Windows.
#undef ANALOG_SCAN_SOF_START_OFFSET_US
#define ANALOG_SCAN_SOF_START_OFFSET_US 460
#undef ANALOG_SCAN_SOF_MAX_WAIT_US
#define ANALOG_SCAN_SOF_MAX_WAIT_US 600
