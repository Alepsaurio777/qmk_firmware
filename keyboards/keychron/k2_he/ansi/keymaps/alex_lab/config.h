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

// Binario LAB / experimental. Hereda TODO el config estable de `alex` y
// enciende lo experimental encima. Aqui se rompen cosas sin miedo; el binario
// de torneo probado es keymaps/alex. Las directivas experimentales de abajo
// son la unica diferencia de configuracion; la logica es compartida.
#include "../alex/config.h"

// LAB v3: performance-first. El probe de timing no corre por defecto porque
// agrega trabajo al hot path. Se puede reactivar temporalmente al perfilar.
// #define USB_SOF_TIMING_PROBE

// Minecraft-only: elimina funcionalmente Gamepad Analog / Curve del LAB.
// Se conservan los IDs de protocolo y offsets EEPROM para no romper layout,
// pero AKM_GAMEPAD se rechaza, los comandos Curve/Game Controller fallan y el
// scan deja de OR-ear game_controller_matrix. JOYSTICK_ENABLE ya era "no".
#undef ANALOG_GAME_CONTROLLER_SUPPORT
#define ANALOG_GAME_CONTROLLER_SUPPORT 0

// RT predictivo por velocidad: enciende el feed de vel_ema en update_raw_value
// y su consumo en la FSM del rapid trigger. Especulativo (dispara antes del
// cruce fisico) — a evaluar con la telemetria antes de considerarlo estable.
//
// (perfil puro) APAGADO TEMPORAL. Con el flag maestro en 0, todo el predictor
// se compila FUERA por #if: desaparece el feed de vel_ema en update_raw_value()
// y las mascaras F7 de abajo quedan inertes (no hay codigo que las lea; NO se
// comentan porque comentarlas las llevaria a su default 0x3F = predecir en las
// 6 teclas, justo lo contrario). Reactivar = descomentar esta linea.
// #define ANALOG_PREDICTIVE_ACTUATION_IN_GAMING_MODE 1

// Performance-first: sin aprendizaje de bottom-out en background. Si se quiere
// estudiar drift termico se reactiva para una sesion dedicada, no en el LAB de
// juego diario.
#undef ANALOG_BOTTOM_OUT_LEARN
#define ANALOG_BOTTOM_OUT_LEARN 0

// AJ2 / PvP Manual Movement: elimina las ventanas temporales de movimiento.
// El objetivo es que W/S/A/D/Shift reflejen el estado Hall/RT sin una retencion
// artificial de 55 ms que cambie el timing manual de w-tap, s-tap, sidestep,
// counter-strafe o sneak-bridge. Space conserva F9 por ahora porque su problema
// es distinto: un tap fisico demasiado corto puede no atravesar un tick de 1.8.9.
//
// Cambios respecto de AJ1:
//   - F6 OFF global: W y Space ya no tienen ventana OFF forzada.
//   - F9 solo Space: LShift y S dejan de tener minimo-ON artificial.
//   - A/D ya eran passthrough para stretch y siguen asi.
//   - SOCD NO se fuerza ni se siembra aqui: Launcher conserva el control
//     explicito. Sin un par configurado, W/S/A/D pasan naturalmente.
//
// Predictor A/D y el resto del Hall engine NO cambian en AJ2 para aislar esta
// prueba exclusivamente a la politica temporal de reporte.
#define ANALOG_PVP_MANUAL_AJ2 1
#undef ANALOG_RELEASE_STRETCH_IN_GAMING_MODE
#define ANALOG_RELEASE_STRETCH_IN_GAMING_MODE 0

// F7: whitelist predictiva por camino y por riesgo de mecanica, no "todo
// movimiento". bits: 0=SPC 1=LSFT 2=W 3=A 4=S 5=D.
//  - S fuera de ambos: el fantasma mas caro del juego (corta sprint en chase).
//  - W fuera de ambos: el press fantasma arriesga el borde en sumo, y el
//    re-press predictivo acorta la ventana OFF del w-tap (pelearia contra F6).
//  - LSFT fuera: la prediccion no aporta nada al sneak y su fantasma no es gratis.
//  - PRESS: SPC + A + D (0x29) — fantasmas baratos, unico sitio donde anticipar
//    ~1-3 ms puede pescar un tick anterior.
//  - REPRESS: solo A + D (0x28). AJ2 tampoco cambia esta whitelist: el
//    experimento actual aisla exclusivamente output-stretch de movimiento.
//    Cualquier cambio futuro del predictor se medira en otra rama. OJO bits:
//    A=bit3, D=bit5 -> 0x28 (0x18 seria A+S: re-meteria a S y sacaria a D).
#define ANALOG_PREDICTIVE_PRESS_KEY_MASK 0x29
#define ANALOG_PREDICTIVE_REPRESS_KEY_MASK 0x28

// F9: AJ2 lo reserva exclusivamente a Space. Un tap fisico corto de salto
// conserva >=55 ms ON, pero las teclas de movimiento quedan sin stretching.
//
// (perfil puro) APAGADO TEMPORAL. Su default es 0, asi que comentar la linea
// del flag SI lo apaga (a diferencia de las mascaras F7). Con esto Space deja
// de tener minimo-ON: un tap ultracorto de salto puede no cruzar un tick de
// 1.8.9 (perdida de salto ocasional en bhop/parkour); aceptado a proposito.
// Los #undef/#define de KEY2/KEY3 de abajo quedan inertes (el press-stretch no
// se compila). Reactivar = descomentar la linea del flag.
// #define ANALOG_PRESS_STRETCH_IN_GAMING_MODE 1
#undef ANALOG_PRESS_STRETCH_KEY2_KEYCODE
#define ANALOG_PRESS_STRETCH_KEY2_KEYCODE KC_NO   // LShift directo (inerte con F9 off)
#undef ANALOG_PRESS_STRETCH_KEY3_KEYCODE
#define ANALOG_PRESS_STRETCH_KEY3_KEYCODE KC_NO   // S directo (inerte con F9 off)

// Sensibilidad: knobs finos de la cadena analoga. Documentados (default,
// efecto, riesgo y protocolo de medicion) en keymaps/alex/TUNING_SENSIBILIDAD.md.
// Regla: descomentar UNO a la vez, validar en sesion, y solo subir lo que la
// sesion justifique. En torneo NO van — ahi vive la config probada.
// #define ANALOG_RAW_NOISE_FILTER_GAMING 3   // default 5 — mas micro-movimiento, mas riesgo de chatter
// #define STATIC_HYSTERESIS_GAMING 3         // default 5 — releases antes, riesgo de chatter de release
// #define ANALOG_SOCD_DEEPER_HYSTERESIS 4    // default 6 — SOCD A/D mas rapido, riesgo de rebote

// A: settle de seleccion de columna 20 us -> 12 us. k2_he/config.h define 20
// (Keychron ya lo habia bajado de 30); este keymap lo rebaja para el lab.
// Con pipeline, el settle es la ventana minima entre select_col() y el ADC
// (analog_matrix_scan.c:482-495): en 16 columnas, 8 us ahorrados por columna
// tiran el barrido ~880-925 us hacia ~750-800 us, mas colchon antes del poll
// de 1 ms y menos riesgo de perder el anclaje SOF cuando la instrumentacion
// estira una iteracion.
// Riesgo: si el MUX/trazas no estabilizaron en 12 us, la carga de la columna
// anterior sangra en la siguiente -> teclas fantasma. El RC decide; se valida
// con el probe de fase (duracion estable, sin fantasmas en sesion de stress).
// Si hay ghosts, subir a 15/16; si la fase se mantiene limpia, se promueve.
#undef ANALOG_SELECT_SETTLE_US
#define ANALOG_SELECT_SETTLE_US 12

// B: ADC 28 -> 15 ciclos. A 18 MHz reduce ~4.3 us por columna para 6 canales
// (~69 us por barrido de 16 columnas). Es experimental: si aparece jitter o
// bleed entre columnas, volver a ADC_SAMPLE_28 antes de tocar el settle.
#undef ANALOG_ADC_SAMPLE_TIME
#define ANALOG_ADC_SAMPLE_TIME ADC_SAMPLE_15

// C: el 74HC164 ya no necesita 50 NOP conservadores en el LAB. 32 conserva
// margen amplio y recorta espera digital repetida en cada seleccion de columna.
#undef HC164_DELAY_NOPS
#define HC164_DELAY_NOPS 32


// ---------------------------------------------------------------------------
// V4 Minecraft Performance: optimizaciones digitales del hot path.
// Mantienen ADC_SAMPLE_15 y settle=12 us de V3; no hacen el frente analogico
// mas agresivo. Los switches del frente analogico siguen LAB-only; la cache de
// configuracion y la tabla compacta de SOCD se comparten con `alex`.
// ---------------------------------------------------------------------------

// HC164: usa NOPs explicitos de conteo fijo solo en LAB; `alex` conserva el
// loop historico para no cambiar su timing validado desde este driver compartido.
#define ANALOG_HC164_EXACT_DELAY 1

// SOCD compacto y cache de modo/configuracion se heredan de `alex`; las rutas
// estables se ejercitan también en este binario junto con los experimentos.

// El ADC escribe alternadamente en A/B. La columna previa se procesa durante
// el settle de la actual y no se copia con memcpy por cada columna.
#define ANALOG_SCAN_PING_PONG_ADC 1

// V4.1: deadline scheduler DESACTIVADO por defecto. SOF marca el inicio del
// frame, no demuestra el instante del Interrupt-IN del host; terminar "cerca"
// de 1 ms podria perder un poll y costar ~1 ms. Conservamos el sync fijo
// probado (SOF + 10 us) y dejamos el scheduler en el driver para A/B futuro.
#undef ANALOG_SCAN_SOF_DEADLINE_SYNC
#define ANALOG_SCAN_SOF_DEADLINE_SYNC 0
#undef ANALOG_SCAN_SOF_START_OFFSET_US
#define ANALOG_SCAN_SOF_START_OFFSET_US 504
#undef ANALOG_SCAN_SOF_MAX_WAIT_US
#define ANALOG_SCAN_SOF_MAX_WAIT_US 600


// ---------------------------------------------------------------------------
// V4.2: ideas tomadas como conceptos de otros proyectos HE, implementadas de
// forma nativa sobre el pipeline Keychron/QMK existente.
// ---------------------------------------------------------------------------

// Noise-floor por tecla: reutiliza la calibracion de reposo de boot, calcula
// peak-to-peak ADC por tecla y congela un raw-delta gate para toda la sesion.
// No hay aprendizaje durante juego ni escrituras EEPROM. Reutiliza las 8
// muestras de calibracion que el firmware ya almacenaba: no aumenta la RAM
// permanente de calibrate_values solo para estimar ruido.
#define ANALOG_STARTUP_NOISE_FLOOR_ENABLE 1
#define ANALOG_STARTUP_NOISE_FILTER_MIN 3
#define ANALOG_STARTUP_NOISE_FILTER_MAX 12
#define ANALOG_STARTUP_NOISE_FILTER_MARGIN 1
#define ANALOG_STARTUP_NOISE_MAX_VALID_SPAN 24

// ---------------------------------------------------------------------------
// V4.2.3-A: mejoras no especulativas. No cambian ADC/settle/RT.
// ---------------------------------------------------------------------------

// F6/F9: una mascara conjunta descarta en un AND a las ~80 teclas que no
// participan en stretch; solo candidatos pagan los lookups de slots.
#define ANALOG_STRETCH_BITMAP_FAST_REJECT 1

// Startup noise: una tecla solo aprende si, ademas de estable, su promedio esta
// cerca de la referencia de release PREVIA. 60 raw es deliberadamente
// conservador: si hay drift mayor no rompe la tecla, simplemente cae al filtro
// estatico para esa sesion.
#define ANALOG_STARTUP_RELEASE_WINDOW_RAW 60

// El sanitizer de perfiles se hereda de `alex`; no cambia layout ni version
// EEPROM y sólo corrige datos imposibles en RAM.
