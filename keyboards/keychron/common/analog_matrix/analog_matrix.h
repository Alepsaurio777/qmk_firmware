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

#include <stdint.h>
#include <stdbool.h>
// (1-ago) size_t lo usa analog_matrix_eeprom_update() mas abajo. En el build de
// firmware colaba porque algun header de QMK lo arrastraba antes; el header no
// era auto-contenido y se vio al compilarlo desde el harness de host.
#include <stddef.h>
#include "action_layer.h"
#include "compiler_support.h"
#include "keycodes.h"
#include "matrix.h"
#include "analog_matrix_eeconfig.h"
#include "analog_matrix_type.h"

#define FULL_TRAVEL_UNIT 40

#ifndef DEFAULT_ACTUATION_POINT
#    define DEFAULT_ACTUATION_POINT 20
#endif

#ifndef DEFAULT_RAPID_TRIGGER_SENSITIVITY
#    define DEFAULT_RAPID_TRIGGER_SENSITIVITY 4
#endif

#ifndef DEFAULT_ZERO_TRAVEL_VALUE
#    define DEFAULT_ZERO_TRAVEL_VALUE 3000
#endif

#ifndef DEFAULT_FULL_RANGE
#    define DEFAULT_FULL_RANGE 900
#endif

#define DEFAULT_FULL_TRAVEL_VALUE (DEFAULT_ZERO_TRAVEL_VALUE - DEFAULT_FULL_RANGE)

#ifndef VALID_ANALOG_RAW_VALUE_MIN
#    define VALID_ANALOG_RAW_VALUE_MIN 1200
#endif

#ifndef VALID_ANALOG_RAW_VALUE_MAX
#    define VALID_ANALOG_RAW_VALUE_MAX 3500
#endif

#ifndef STATIC_HYSTERESIS
#    define STATIC_HYSTERESIS 5
#endif

#ifndef STATIC_HYSTERESIS_GAMING
#    define STATIC_HYSTERESIS_GAMING STATIC_HYSTERESIS
#endif

#ifndef STATIC_HYSTERESIS_TYPING
#    define STATIC_HYSTERESIS_TYPING STATIC_HYSTERESIS
#endif

#ifndef ANALOG_ADAPTIVE_SHALLOW_HYSTERESIS_GAMING
#    define ANALOG_ADAPTIVE_SHALLOW_HYSTERESIS_GAMING 0
#endif

// (24-jul) STATIC_HYSTERESIS_GAMING_FAST_KEY y ANALOG_GAMING_FAST_KEY_ROW/COL
// eliminados: daban histeresis distinta a UNA tecla elegida por coordenada de
// matriz, invisible en Launcher. Y estaban inertes de facto — con
// ANALOG_ADAPTIVE_SHALLOW_HYSTERESIS_GAMING la histeresis ya se capa a
// actn_pt/2, que con actuacion <= 0.4 mm da el mismo valor. Solo divergian a
// partir de 0.6 mm, o sea: se despertaban al mover un slider en Launcher sin
// que nada lo indicase. La histeresis de Gaming es ahora uniforme.

// ----- Politica por tecla: se declara por KEYCODE, no por coordenada -------
// (24-jul) Las whitelists de continuous RT, RT predictivo y release-stretch
// describen MECANICA de juego ("la tecla de sprint", "la de salto"), no una
// posicion fisica. Declararlas por fila/columna las rompia en silencio en
// cuanto Launcher remapeaba una tecla: la politica se quedaba en el hueco
// viejo. Ahora se declaran por keycode y se resuelven contra el keymap VIVO
// (el de VIA/Launcher en EEPROM, no los defaults de PROGMEM) en
// analog_matrix_resolve_policy_keys(), llamada desde update_travel_configs().
//
// Reglas de la resolucion:
//  - Coincidencia EXACTA del keycode. Un KC_W envuelto en mod-tap/layer-tap no
//    coincide: es otra tecla a efectos de la politica.
//  - Si dos posiciones mapean al mismo keycode, las mascaras (sin estado) las
//    marcan a las dos; el release-stretch, que lleva estado por slot, se queda
//    con la PRIMERA en orden de barrido.
//  - Si el MISMO keycode se declara en dos slots predictivos, sus bits F7 se
//    combinan con OR. El codigo por coordenadas se quedaba con el primer slot;
//    con un keycode por slot (el caso de hoy) da igual, pero no es identico.
//  - KC_NO desactiva el slot.
//
// Orden de arranque (verificado en quantum/keyboard.c): via_init() corre ANTES
// de matrix_init() -> matrix_init_custom() -> analog_matrix_init(), asi que el
// keymap dinamico ya esta validado (o reseteado a los defaults de PROGMEM si la
// EEPROM venia en blanco) cuando se resuelve por primera vez.
#ifndef ANALOG_POLICY_LAYER
#    define ANALOG_POLICY_LAYER 0
#endif

#ifndef ANALOG_CONTINUOUS_RT_KEY1_KEYCODE
#    define ANALOG_CONTINUOUS_RT_KEY1_KEYCODE KC_NO
#endif

#ifndef ANALOG_CONTINUOUS_RT_KEY2_KEYCODE
#    define ANALOG_CONTINUOUS_RT_KEY2_KEYCODE KC_NO
#endif

#ifndef MIN_ACTUATION
#    define MIN_ACTUATION 5
#endif

#ifndef ZERO_TRAVEL_DEAD_ZONE
#    define ZERO_TRAVEL_DEAD_ZONE 20
#endif

#ifndef TOP_OUT_DEAD_ZONE_GAMING
#    define TOP_OUT_DEAD_ZONE_GAMING 0
#endif

#ifndef TOP_OUT_DEAD_ZONE_TYPING
#    define TOP_OUT_DEAD_ZONE_TYPING 0
#endif

#ifndef ANALOG_RAW_NOISE_FILTER_GAMING
#    define ANALOG_RAW_NOISE_FILTER_GAMING 5
#endif

#ifndef ANALOG_RAW_NOISE_FILTER_TYPING
#    define ANALOG_RAW_NOISE_FILTER_TYPING 5
#endif

#ifndef ANALOG_GAMING_LAYERS_MASK
#    define ANALOG_GAMING_LAYERS_MASK ((layer_state_t)0x03)
#endif

static inline bool analog_matrix_is_gaming_mode(void) {
    // SOLO la capa default (= interruptor fisico). Incluir layer_state aqui
    // reabria un escape del lockdown: con Fn sostenido durante el cambio de
    // interruptor, el bit de la capa Fn mantenia esto en falso ya en Gaming.
    return (default_layer_state & ~ANALOG_GAMING_LAYERS_MASK) == 0;
}

#ifndef BOTTOM_DEAD_ZONE
#    define BOTTOM_DEAD_ZONE 38
#endif

#ifndef BOTTOM_JITTER
#    define BOTTOM_JITTER 80
#endif

#define TRAVEL_SCALE 6

#ifndef ANALOG_DEBOUNCE_TIME
#    define ANALOG_DEBOUNCE_TIME 3
#endif

#ifndef ANALOG_FIXED_POINT_TRAVEL
#    define ANALOG_FIXED_POINT_TRAVEL 0
#endif

#ifndef ANALOG_AUTO_CALIBRATION_ENABLE
#    define ANALOG_AUTO_CALIBRATION_ENABLE 1
#endif

/* Learn per-key bottom-out from real usage, outside the scan hot path.
 * The learned full-travel only ever deepens (never shrinks), so the dynamic
 * range cannot degrade on its own. Runs in analog_matrix_task(). */
#ifndef ANALOG_BOTTOM_OUT_LEARN
#    define ANALOG_BOTTOM_OUT_LEARN 0
#endif

/* Minimum improvement (raw ADC counts) before committing a learned bottom-out. */
#ifndef ANALOG_BOTTOM_OUT_LEARN_EPSILON
#    define ANALOG_BOTTOM_OUT_LEARN_EPSILON 30
#endif

/* Depth-compare SOCD (Rappy Snappy): a challenger key must be deeper than the
 * current winner by this many travel units (TRAVEL_SCALE units; 6 = 0.1 mm)
 * to take over. Without it, sensor noise at near-equal depths flips the
 * winner every scan (A/D chatter at scan rate). */
#ifndef ANALOG_SOCD_DEEPER_HYSTERESIS
#    define ANALOG_SOCD_DEEPER_HYSTERESIS 6
#endif

/* SOCD "ambas a fondo": si ambas teclas del par superan este travel, se
 * registran las dos. Derivado del travel maximo (~94%) para no romperse si la
 * escala cambia. Antes era 230 fijo, valido solo porque el maximo actual es
 * (FULL_TRAVEL_UNIT+1)*TRAVEL_SCALE-1 = 245; esta formula da 230 hoy y se
 * adapta si FULL_TRAVEL_UNIT o TRAVEL_SCALE cambian. */
#ifndef ANALOG_SOCD_BOTTOM_OUT_THRESHOLD
#    define ANALOG_SOCD_BOTTOM_OUT_THRESHOLD ((((FULL_TRAVEL_UNIT + 1) * TRAVEL_SCALE) - 1) * 94 / 100)
#endif

#ifndef ANALOG_DISABLE_OKMC_IN_GAMING_MODE
#    define ANALOG_DISABLE_OKMC_IN_GAMING_MODE 0
#endif

#ifndef ANALOG_DISABLE_TOGGLE_IN_GAMING_MODE
#    define ANALOG_DISABLE_TOGGLE_IN_GAMING_MODE 0
#endif

#ifndef ANALOG_DISABLE_GAMEPAD_IN_GAMING_MODE
#    define ANALOG_DISABLE_GAMEPAD_IN_GAMING_MODE 0
#endif

#ifndef ANALOG_DISABLE_SOCD_IN_GAMING_MODE
#    define ANALOG_DISABLE_SOCD_IN_GAMING_MODE 0
#endif

#ifndef ANALOG_DISABLE_PROFILE_COMBO_IN_GAMING_MODE
#    define ANALOG_DISABLE_PROFILE_COMBO_IN_GAMING_MODE 0
#endif

#ifndef ANALOG_CONTINUOUS_RAPID_TRIGGER_IN_GAMING_MODE
#    define ANALOG_CONTINUOUS_RAPID_TRIGGER_IN_GAMING_MODE 0
#endif

#ifndef ANALOG_CONTINUOUS_RT_REPRESS_MAX_TRAVEL
#    define ANALOG_CONTINUOUS_RT_REPRESS_MAX_TRAVEL ((FULL_TRAVEL_UNIT + 1) * TRAVEL_SCALE - 1)
#endif

#ifndef ANALOG_PREDICTIVE_ACTUATION_IN_GAMING_MODE
#    define ANALOG_PREDICTIVE_ACTUATION_IN_GAMING_MODE 0
#endif

// (24-jul) ANALOG_GAMING_DEFAULT_RAPID_PROFILE eliminado. Convertia el modo
// AKM_GLOBAL de Espacio y LShift en un AKM_RAPID explicito al cargar la EEPROM,
// asi que un cambio posterior de modo global en Launcher dejaba esas dos teclas
// clavadas en Rapid. Los defaults de modo por tecla pertenecen a
// default_profiles[] (tabla de reset, que Launcher puede pisar), no a un
// override que corre en cada boot.

// Slots 1..6 del RT predictivo. El indice de slot es lo que direccionan las
// mascaras F7 de abajo (bit i = KEY(i+1)), asi que el ORDEN importa: no
// reordenar sin recalcular ANALOG_PREDICTIVE_{PRESS,REPRESS}_KEY_MASK.
#ifndef ANALOG_PREDICTIVE_RT_KEY1_KEYCODE
#    define ANALOG_PREDICTIVE_RT_KEY1_KEYCODE ANALOG_CONTINUOUS_RT_KEY1_KEYCODE
#endif

#ifndef ANALOG_PREDICTIVE_RT_KEY2_KEYCODE
#    define ANALOG_PREDICTIVE_RT_KEY2_KEYCODE ANALOG_CONTINUOUS_RT_KEY2_KEYCODE
#endif

#ifndef ANALOG_PREDICTIVE_RT_KEY3_KEYCODE
#    define ANALOG_PREDICTIVE_RT_KEY3_KEYCODE KC_NO
#endif

#ifndef ANALOG_PREDICTIVE_RT_KEY4_KEYCODE
#    define ANALOG_PREDICTIVE_RT_KEY4_KEYCODE KC_NO
#endif

#ifndef ANALOG_PREDICTIVE_RT_KEY5_KEYCODE
#    define ANALOG_PREDICTIVE_RT_KEY5_KEYCODE KC_NO
#endif

#ifndef ANALOG_PREDICTIVE_RT_KEY6_KEYCODE
#    define ANALOG_PREDICTIVE_RT_KEY6_KEYCODE KC_NO
#endif

#ifndef ANALOG_PREDICTIVE_ACTUATION_ADVANCE
#    define ANALOG_PREDICTIVE_ACTUATION_ADVANCE TRAVEL_SCALE
#endif

#ifndef ANALOG_PREDICTIVE_ACTUATION_MIN_DELTA
#    define ANALOG_PREDICTIVE_ACTUATION_MIN_DELTA TRAVEL_SCALE
#endif

// ----- Predictive RT con velocidad real (rt_predictive_downstroke_ready) -----
// vel_ema lleva un EMA del delta descendente por scan (unidades de TRAVEL_SCALE
// = 0.1 mm/scan, scan anclado a SOF ~1 kHz). El EMA suaviza picos de ruido de
// un solo scan sin perder la pendiente de un W-Tap sostenido. La "puerta"
// MIN_VELOCITY descarta roces/escritura lentos que no deben disparar la
// prediccion.
//
// Tunables (override en config.h del keymap). Defaults calibrados para
// reaccion ~1 scan ante un W-Tap agresivo (delta ~0.3 mm/scan) y no disparar
// ante escritura lenta (delta ~0.1 mm/scan):
//
//   ANALOG_PREDICTIVE_EMA_SHIFT     N en
//                                    vel_ema = vel_ema - (vel_ema>>N) + (delta>>N).
//                                    Default 2 = factor 1/4. Buildup en ~4 scans.
//                                    Mayor N = mas smoothing y buildup mas lento.
//   ANALOG_PREDICTIVE_MIN_VELOCITY  EMA minimo para considerar el golpe "rapido".
//                                    Default 4: un golpe rapido (delta 20) lo
//                                    supera en 1 scan (vel_ema = 5); una prensa
//                                    lenta sostenida (delta 6) lo alcanza tras
//                                    ~3 scans (eso es escritura, no typo).
//   ANALOG_PREDICTIVE_LOOKAHEAD     Scans proyectados hacia adelante en
//                                    projected = travel + vel_ema * LOOKAHEAD.
//                                    Default 2. Lookahead 1 = prediccion modesta
//                                    (~0.1 mm); 3 = agresiva (~0.3 mm para vel 20).
#ifndef ANALOG_PREDICTIVE_EMA_SHIFT
#    define ANALOG_PREDICTIVE_EMA_SHIFT 2
#endif

#ifndef ANALOG_PREDICTIVE_MIN_VELOCITY
#    define ANALOG_PREDICTIVE_MIN_VELOCITY 4
#endif

#ifndef ANALOG_PREDICTIVE_LOOKAHEAD
#    define ANALOG_PREDICTIVE_LOOKAHEAD 2
#endif

// ----- F7: mascaras por camino del RT predictivo ---------------------------
// bit i = ANALOG_PREDICTIVE_RT_KEY(i+1). Separan la prediccion del PRIMER
// press (AKS_REGULAR_RELEASED) de la del RE-press (AKS_RAPID_RELEASED). El
// motivo es MC 1.8.9: el juego muestrea el estado de movimiento 1 vez por tick
// (50 ms), y adelantar el re-press ACORTA la ventana OFF que ese muestreo
// tiene que ver (w-tap/sprint-reset) — un re-press predictivo puede ser
// negativo aunque el primer press predictivo sea neutro o positivo. Default
// 0x3F = las 6 teclas en ambos caminos (comportamiento previo). Inertes si
// ANALOG_PREDICTIVE_ACTUATION_IN_GAMING_MODE == 0.
#ifndef ANALOG_PREDICTIVE_PRESS_KEY_MASK
#    define ANALOG_PREDICTIVE_PRESS_KEY_MASK 0x3F
#endif

#ifndef ANALOG_PREDICTIVE_REPRESS_KEY_MASK
#    define ANALOG_PREDICTIVE_REPRESS_KEY_MASK 0x3F
#endif

// ----- F6: release-stretch anclado al tick ---------------------------------
// MC 1.8.9 muestrea el ESTADO de las teclas de movimiento una vez por tick de
// cliente (50 ms). Un release+re-press que viva entero entre dos muestreos no
// existe para el juego: el w-tap no resetea sprint y el tap de espacio no
// resetea jumpTicks (salto retrasado hasta 500 ms bajo combo). Este filtro de
// la capa de REPORTE (la FSM y el travel no se tocan) garantiza que, tras un
// release fisico de una tecla whitelisted, el estado reportado quede OFF al
// menos ANALOG_RELEASE_STRETCH_MS antes de dejar pasar el re-press. Nunca
// sintetiza ni adelanta input — solo retrasa un press real (semantica de
// debounce). Solo actua en modo Gaming.
#ifndef ANALOG_RELEASE_STRETCH_IN_GAMING_MODE
#    define ANALOG_RELEASE_STRETCH_IN_GAMING_MODE 0
#endif

// Tick de cliente de MC 1.8.9. No es un parametro que se ajuste: es un hecho
// del juego, y de el se derivan los pisos de las dos ventanas de stretch.
#ifndef ANALOG_TICK_REFERENCE_MS
#    define ANALOG_TICK_REFERENCE_MS 50
#endif

// 55 ms > tick de 50 ms: garantiza >=1 muestreo del estado OFF con el peor
// alineamiento de fase respecto al tick del cliente.
#ifndef ANALOG_RELEASE_STRETCH_MS
#    define ANALOG_RELEASE_STRETCH_MS 55
#endif

// ----- F9: minimo-ON anclado al tick ---------------------------------------
// El espejo de F6, y la otra mitad de lo que el espacio necesita. `jumpTicks`
// falla de DOS formas distintas y cada una pide una garantia distinta:
//
//   press no visto (ON < 1 tick)   -> el salto no existio         -> F9
//   release no visto (OFF < 1 tick)-> jumpTicks no se resetea,
//                                     siguiente salto hasta 500 ms tarde -> F6
//
// Por eso el espacio lleva las DOS y no hay que elegir. Se encadenan F9 -> F6
// (ver el punto de llamada en analog_matrix_scan.c: el orden es load-bearing).
// Coste combinado en el peor caso: 55 + 55 = 110 ms de ciclo para un tap, que
// es la comparacion que importa contra los 500 ms del salto perdido de hoy.
//
// Tras un flanco de press fisico, el estado reportado se sostiene en ON durante
// >= ANALOG_PRESS_STRETCH_MS aunque el dedo suelte. No sintetiza un press que no
// hiciste: sostiene uno que si hiciste. Solo actua en modo Gaming.
//
// Un slot, no dos: la unica tecla cuya MECANICA es el ON es el espacio. W no lo
// necesita (su mecanica la dispara que se vea el OFF, que es F6) y extender el
// ON de W seria movimiento no pedido — mortal en un borde de sumo.
#ifndef ANALOG_PRESS_STRETCH_IN_GAMING_MODE
#    define ANALOG_PRESS_STRETCH_IN_GAMING_MODE 0
#endif

#ifndef ANALOG_PRESS_STRETCH_MS
#    define ANALOG_PRESS_STRETCH_MS 55
#endif

#ifndef ANALOG_PRESS_STRETCH_KEY1_KEYCODE
#    define ANALOG_PRESS_STRETCH_KEY1_KEYCODE KC_NO
#endif

// (1-ago) Segundo slot de F9. El comentario de arriba decia "un slot, no dos:
// la unica tecla cuya MECANICA es el ON es el espacio". Sigue siendo cierto para
// el espacio, pero deja fuera el caso ESPEJO y peligroso: en 1.8.9 un *unshift*
// no visto al bridgear es un fallo seguro (te quedas agachado, lento), mientras
// que un *press* de shift no visto en un borde es una CAIDA. F9 sostiene un
// press real sin sintetizar ninguno, asi que pasa el criterio #3 igual que en el
// espacio, y cambia si el juego ve el input, asi que pasa el #4.
//
// Su coste es real y medible: en 1.8.9 el sneak cancela el sprint, asi que 55 ms
// de shift forzado obligan a re-doble-tap de W. Por eso es un experimento de
// alex_lab con el histograma delante, NO una suposicion de torneo. El drill que
// decide si merece la pena construirlo siquiera es medir, en el binario de
// TORNEO, que fraccion de presses de LSHIFT al bridgear duran menos de un tick.
#ifndef ANALOG_PRESS_STRETCH_KEY2_KEYCODE
#    define ANALOG_PRESS_STRETCH_KEY2_KEYCODE KC_NO
#endif

#define PRESS_STRETCH_SLOTS 2

// La razon de existir de las dos ventanas es SUPERAR el tick del cliente. Un
// valor <= 50 no las hace mas rapidas: las deja pagando la latencia entera y
// sin comprar el muestreo. Falla el build en vez de degradarse en silencio.
STATIC_ASSERT(ANALOG_RELEASE_STRETCH_MS > ANALOG_TICK_REFERENCE_MS, "ANALOG_RELEASE_STRETCH_MS debe superar el tick del cliente o la garantia no existe");
STATIC_ASSERT(ANALOG_PRESS_STRETCH_MS > ANALOG_TICK_REFERENCE_MS, "ANALOG_PRESS_STRETCH_MS debe superar el tick del cliente o la garantia no existe");

#ifndef ANALOG_RELEASE_STRETCH_KEY1_KEYCODE
#    define ANALOG_RELEASE_STRETCH_KEY1_KEYCODE KC_NO
#endif

#ifndef ANALOG_RELEASE_STRETCH_KEY2_KEYCODE
#    define ANALOG_RELEASE_STRETCH_KEY2_KEYCODE KC_NO
#endif

// ----- Politica resuelta: un bitmap por fila --------------------------------
// analog_matrix_resolve_policy_keys() traduce los keycodes de arriba a
// posiciones de matriz leyendo el keymap vivo, y pliega aqui las mascaras F7.
// El hot path solo hace un test de bit y ya no sabe nada de slots ni de
// keycodes. ANALOG_POLICY_NEEDED es 0 en el binario de torneo, asi que todo
// esto (incluido el barrido de resolucion) se compila fuera.
#define ANALOG_POLICY_NEEDED (ANALOG_CONTINUOUS_RAPID_TRIGGER_IN_GAMING_MODE || ANALOG_PREDICTIVE_ACTUATION_IN_GAMING_MODE || ANALOG_RELEASE_STRETCH_IN_GAMING_MODE || ANALOG_PRESS_STRETCH_IN_GAMING_MODE)

#if ANALOG_POLICY_NEEDED
void analog_matrix_resolve_policy_keys(void);

// ----- Volcado de diagnostico de la politica resuelta -----------------------
// Hace observable lo que antes solo se podia razonar: a que posiciones fisicas
// aterrizaron las whitelists declaradas por keycode. Sin esto, verificar un
// remap exige una sesion de evlog; con esto es una consulta.
//
// Layout de los ANALOG_POLICY_DUMP_LEN bytes que rellena:
//   [0]      flags: bit0 predictivo, bit1 release-stretch (F6),
//            bit2 press-stretch (F9), bit3 continuous RT
//   [1..12]  analog_predictive_press_mask,   6 filas x uint16 LE
//   [13..24] analog_predictive_repress_mask, 6 filas x uint16 LE
//   [25]     slot 0 de F6, empaquetado (row << 4) | col — 0xFF sin resolver
//   [26]     slot 1 de F6, idem
//   [27]     slot 0 de F9, idem
//   [28]     reservado (0)
// Continuous RT reporta solo su bit de flags: esta apagado y su mascara no
// justifica 12 bytes hasta que se use.
#define ANALOG_POLICY_DUMP_LEN 29
void analog_matrix_policy_dump(uint8_t *out);

STATIC_ASSERT(MATRIX_ROWS <= 15 && MATRIX_COLS <= 16, "El empaquetado (row << 4) | col del volcado necesita row <= 15 y col <= 15");

static inline bool analog_policy_bit(const matrix_row_t *mask, uint8_t row, uint8_t col) {
    if (row >= MATRIX_ROWS || col >= MATRIX_COLS) return false;
    return (mask[row] & ((matrix_row_t)1 << col)) != 0;
}
#else
// Passthrough textual: coste cero cuando no hay ninguna politica encendida.
#    define analog_matrix_resolve_policy_keys() ((void)0)
#endif

#if ANALOG_CONTINUOUS_RAPID_TRIGGER_IN_GAMING_MODE
extern matrix_row_t analog_continuous_rt_mask[MATRIX_ROWS];
#endif

#if ANALOG_PREDICTIVE_ACTUATION_IN_GAMING_MODE
extern matrix_row_t analog_predictive_press_mask[MATRIX_ROWS];
extern matrix_row_t analog_predictive_repress_mask[MATRIX_ROWS];
#endif

STATIC_ASSERT(MATRIX_COLS <= (int)(sizeof(matrix_row_t) * 8), "Los bitmaps de politica necesitan un bit por columna en matrix_row_t");
STATIC_ASSERT(ANALOG_CONTINUOUS_RT_REPRESS_MAX_TRAVEL <= ((FULL_TRAVEL_UNIT + 1) * TRAVEL_SCALE - 1), "ANALOG_CONTINUOUS_RT_REPRESS_MAX_TRAVEL cannot exceed max travel");

// Threshold value when the magnet switch is not installed
#ifndef ABNORMAL_ANALOG_RAW_THRESHOLD_VALUE
#    define ABNORMAL_ANALOG_RAW_THRESHOLD_VALUE 3250
#endif

//
#ifndef AUTO_CALIB_FULL_TRAVEL_THRESHOLD_VALUE
#    define AUTO_CALIB_FULL_TRAVEL_THRESHOLD_VALUE (DEFAULT_ZERO_TRAVEL_VALUE - DEFAULT_FULL_RANGE + 100)
#endif

#ifndef AUTO_CALIB_ZERO_TRAVEL_JITTER_VALUE
#    define AUTO_CALIB_ZERO_TRAVEL_JITTER_VALUE 50
#endif

#ifndef AUTO_CALIB_FULL_TRAVEL_JITTER_VALUE
#    define AUTO_CALIB_FULL_TRAVEL_JITTER_VALUE 100
#endif

#ifndef AUTO_CALIB_ZERO_TRAVEL_THRESHOLD_VALUE
#    define AUTO_CALIB_ZERO_TRAVEL_THRESHOLD_VALUE (DEFAULT_FULL_RANGE - AUTO_CALIB_FULL_TRAVEL_JITTER_VALUE)
#endif

#ifndef AUTO_CALIB_VALID_RELASING_TIME
#    define AUTO_CALIB_VALID_RELASING_TIME 1000
#endif

void analog_matrix_init(void);
void analog_matrix_eeconfig_init(void);
bool update_raw_value(uint8_t row, uint8_t col, uint16_t value);
void update_travel_configs(void);
void update_key_config(uint8_t row, uint8_t col);
void analog_matrix_eeprom_update(const void *buf, void *addr, size_t len);

void analog_matrix_set_mins(uint16_t *min);
void analog_matrix_set_maxs(uint16_t *max);

// Cruda: NO valida indices. Para llamantes que iteran la matriz por construccion.
uint8_t      analog_matrix_get_travel(uint8_t row, uint8_t col);
// Con guardarrail: devuelve 0 fuera de rango. Para indices que vienen de una
// resolucion por keycode que puede fallar (0xFF) o de un evento virtual.
uint8_t      analog_matrix_get_travel_checked(uint8_t row, uint8_t col);
uint8_t      analog_matrix_get_key_mode(uint8_t row, uint8_t col);
bool         analog_matrix_get_key_state(uint8_t row, uint8_t col);

#if ANALOG_RELEASE_STRETCH_IN_GAMING_MODE
bool analog_matrix_release_stretch_apply(uint8_t row, uint8_t col, bool pressed);
#else
// Compilado fuera en el binario estable: passthrough textual, costo cero.
#    define analog_matrix_release_stretch_apply(row, col, pressed) (pressed)
#endif

#if ANALOG_PRESS_STRETCH_IN_GAMING_MODE
bool analog_matrix_press_stretch_apply(uint8_t row, uint8_t col, bool pressed);
#else
#    define analog_matrix_press_stretch_apply(row, col, pressed) (pressed)
#endif

// Flanco FISICO de una tecla con stretch, antes de que el filtro lo altere.
// Weak y vacia por defecto: la telemetria del keymap la override para poder
// medir el estado fisico y el reportado en la MISMA sesion (el evlog cuelga de
// process_record_user, o sea aguas abajo de los dos stretches, y por si solo no
// puede ver lo que el clamp se come).
//
// Lleva el keycode CON EL QUE SE DECLARO el slot, no solo la coordenada: el
// consumidor necesita identificar la tecla y resolverla desde el keymap aqui
// significaria leer la EEPROM dentro del barrido. La firma coincide a proposito
// con la del registrador del evlog.
void analog_matrix_physical_edge_hook(uint16_t keycode, bool pressed, uint8_t row, uint8_t col);
bool         analog_matrix_calibrating(void);
matrix_row_t analog_matrix_get_row(uint8_t row);
void         analog_matrix_rx(uint8_t *data, uint8_t length);
void         analog_matrix_task(void);
void         analog_matrix_indicator(void);
void         analog_matrix_clear(void);
void         analog_matrix_clear_advance_keys(void);
