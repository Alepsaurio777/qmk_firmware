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

// Binario ESTABLE / torneo: SOF-sync + pipeline, SIN el
// probe de timing (debug) ni prediccion. El keymap alex_lab hereda este config
// y enciende esas dos cosas para experimentar. El timestamp de SOF lo provee
// usb_main.c mientras SOF_SYNC este activo, asi que el sync no necesita probe.

// Fase 3 (parte 2a): procesar la columna previa durante el settle de la
// siguiente. Medido: 1041-1052 us sin pipeline -> 880-925 us con pipeline.
#define ANALOG_SCAN_PIPELINE 1

// Fase 3 (parte 2b): arrancar el barrido ~10 us despues de cada SOF para que
// el barrido (880-925 us) + el procesado de cambios de QMK quede armado antes
// del siguiente poll. Convierte la loteria de fase (0-1 ms aleatorio por
// pulsacion) en una constante. Verificacion: la fase del barrido debe
// quedar estable ~890-935 us en vez de uniforme 0-1000.
#define ANALOG_SCAN_SOF_SYNC 1

// Digital scheduling/report changes, independent of Hall thresholds and ADC.
#define ANALOG_SCAN_SOF_STRICT_SYNC 1
#define KEYBOARD_REPORT_BATCHING
#define USB_HID_DEFERRED_REPORTS

// Torneo: aprendizaje adaptativo de bottom-out APAGADO. El drift termico se
// descarto empiricamente (sesion de 24.047 eventos, travel de actuacion
// mediano plano en 25.0 los 5 tramos) y un rango dinamico que muta a mitad de
// partida contradice el objetivo de configuracion inmutable. La calibracion de
// reposo por boot (CALIB_ZERO_TRAVEL_POWER_ON) sigue compensando temperatura.
// alex_lab lo re-enciende para seguir experimentando.
#undef ANALOG_BOTTOM_OUT_LEARN
#define ANALOG_BOTTOM_OUT_LEARN 0

// V4.2.3-A: optimizaciones digitales sin cambiar ADC, settle ni la semantica
// de las teclas. La cache resuelve una vez por reconfiguracion el modo efectivo
// de Gaming; SOCD conserva sólo los pares activos; el sanitizer rechaza datos
// imposibles de EEPROM antes de usarlos. En perfiles validos son no-op
// funcionales y reducen trabajo del barrido.
#define ANALOG_RUNTIME_CONFIG_CACHE 1
#define ANALOG_SOCD_RUNTIME_COMPACT 1
#define ANALOG_PROFILE_SANITIZER_ENABLE 1

// --- Optimizaciones de latencia probadas para el binario estable / torneo ---
// 1. Buffer ADC ping-pong: elimina 16 memcpy por barrido (sin copias de memoria en hot path)
#define ANALOG_SCAN_PING_PONG_ADC 1

// 2. Delays de shift register con NOPs exactos en linea y margen seguro (32 NOPs en vez de loop de 50)
#define ANALOG_HC164_EXACT_DELAY 1
#undef HC164_DELAY_NOPS
#define HC164_DELAY_NOPS 32

// 3. Settle de multiplexor optimizado: 20 us -> 14 us (recupera ~96 us por barrido)
#undef ANALOG_SELECT_SETTLE_US
#define ANALOG_SELECT_SETTLE_US 14

// 4. Muestreo ADC rapido: 28 ciclos -> 15 ciclos (recupera ~69 us por barrido)
#undef ANALOG_ADC_SAMPLE_TIME
#define ANALOG_ADC_SAMPLE_TIME ADC_SAMPLE_15

// 5. Reduccion de zona muerta superior: 12 -> 4 (la actuacion fisica comienza ~0.13 mm antes)
#undef TOP_OUT_DEAD_ZONE_GAMING
#define TOP_OUT_DEAD_ZONE_GAMING 4

// RGB_MATRIX_ENABLE = no en rules.mk: sin iluminacion en estos builds. El
// bloque de tuning RGB que vivia aqui (limites de brillo/framerate y #undef de
// efectos) era codigo muerto bajo un #ifdef que nunca se cumplia; esta en el
// historial de git por si algun dia se re-enciende RGB.
