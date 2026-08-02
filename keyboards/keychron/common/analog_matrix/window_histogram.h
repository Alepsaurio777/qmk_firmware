/* Histograma de ventanas ON/OFF + salud del teclado.
 *
 * (1-ago) Vive en LOS DOS binarios, y eso es una decision deliberada con coste:
 * el binario de torneo crece de forma permanente por diagnostico, que es una
 * desviacion de "torneo minimo". Lo que la hace defendible es el mismo argumento
 * que ya justifica la telemetria en DEVELOPMENT.md: esta en los dos porque la
 * comparacion torneo/lab necesita medir LOS DOS. Un histograma que solo existe
 * en lab mide el binario equivocado.
 *
 * Que mide, y por que asi:
 *   El evlog registra eventos y el analisis se hace en Python. Eso funciona pero
 *   tiene un anillo de 32 y drena 5 por paquete, asi que se degrada justo en la
 *   rafaga de una pelea — la muestra que interesa. El histograma es agregado y
 *   sin perdida: cuenta cuanto duro cada ventana ON y OFF, en cubos cortados
 *   donde esta la fisica del problema (el tick de 50 ms de MC 1.8.9).
 *
 *   Dos capas por tecla, FISICA y REPORTADA. En torneo coinciden (no hay
 *   stretches) y una es redundante; se mantiene igual a proposito, porque
 *   asi el formato de paquete es identico en los dos binarios y el A/B es una
 *   resta directa. En lab, la distancia entre las dos capas ES exactamente lo
 *   que el clamp de F6/F9 rescato.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "analog_matrix.h"

#ifndef ANALOG_WINDOW_HISTOGRAM
#    define ANALOG_WINDOW_HISTOGRAM 0
#endif

// Teclas vigiladas. Son las que tienen o podrian tener stretch; A/S/D importan
// para SOCD y fantasmas, no para visibilidad de tick, y esos van por el evlog.
#define AWH_KEY_W 0
#define AWH_KEY_SPC 1
#define AWH_KEY_LSFT 2
#define AWH_KEY_COUNT 3

#define AWH_LAYER_PHYSICAL 0
#define AWH_LAYER_REPORTED 1
#define AWH_LAYER_COUNT 2

// Cubos, cortados en las fronteras que deciden si el juego ve la ventana:
//   0: < 25 ms      invisible casi seguro (menos de medio tick)
//   1: [25, 50)     loteria de fase
//   2: [50, 55]     visible, sin margen
//   3: > 55 ms      visible seguro
#define AWH_BUCKETS 4

// Un paquete por tecla y capa. Sin paginacion: para tres teclas no compensa el
// estado de secuenciacion que luego habria que depurar.
//   [0] key_idx  [1] layer
//   [2..9]   4 x uint16 LE — cubos de ventanas OFF
//   [10..17] 4 x uint16 LE — cubos de ventanas ON
//   [18..21] uint32 LE — total de flancos vistos (detecta saturacion)
#define AWH_DUMP_LEN 22

// Salud: lo que hoy no tiene alarma.
//   [0..2] contadores de tecla pegada (W/SPC/LSFT), saturantes
//   [3..5] maximo de travel observado en W/SPC/LSFT
//   [6]    peor maximo entre las teclas ya pulsadas — candidato a iman debil
//   [7]    posicion de esa tecla, (row << 4) | col
//   [8]    cuantas teclas se han pulsado al menos una vez
#define AWH_HEALTH_LEN 9

#if ANALOG_WINDOW_HISTOGRAM

// Resuelve las teclas vigiladas contra el keymap vivo. La llama
// update_travel_configs(), igual que la politica: boot, cambio de perfil y giro
// del interruptor. Separada de analog_matrix_resolve_policy_keys() a proposito —
// asi el histograma no arrastra el volcado de politica al binario de torneo.
void analog_window_hist_resolve_keys(void);

// Un barrido. La llaman los dos caminos de escaneo con el estado FISICO (antes
// de los stretches) y el REPORTADO (despues). Rechaza en un test de bit para las
// ~93 teclas que no vigila.
void analog_window_hist_observe(uint8_t row, uint8_t col, bool physical, bool reported);

void analog_window_hist_reset(void);
bool analog_window_hist_dump(uint8_t key_idx, uint8_t layer, uint8_t *out);
void analog_window_hist_health(uint8_t *out);

#else
#    define analog_window_hist_resolve_keys() ((void)0)
#    define analog_window_hist_observe(row, col, physical, reported) ((void)0)
#    define analog_window_hist_reset() ((void)0)
#endif
