/* Histograma de ventanas ON/OFF + salud del teclado.
 *
 * Vive exclusivamente en alex_lab. El binario estable deja
 * ANALOG_WINDOW_HISTOGRAM=0 y compila fuera estado, protocolo y hook por tecla.
 * Las funciones que se validen aqui siguen viviendo en common/: promover una
 * al estable es un cambio explicito de flag, no una copia de implementacion.
 *
 * Que mide, y por que asi:
 *   El evlog registra eventos y el analisis se hace en Python. Eso funciona pero
 *   tiene un anillo de 32 y drena 5 por paquete, asi que se degrada justo en la
 *   rafaga de una pelea — la muestra que interesa. El histograma es agregado y
 *   sin perdida: cuenta cuanto duro cada ventana ON y OFF, en cubos cortados
 *   donde esta la fisica del problema (el tick de 50 ms de MC 1.8.9).
 *
 *   Dos capas por tecla, FISICA y REPORTADA. Con stretches apagados coinciden;
 *   con ellos activos, la distancia entre ambas ES exactamente lo que el clamp
 *   de F6/F9 rescato. El A/B se hace en lab cambiando solo esos flags.
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

// Salud: dos medidas DIRECTAS por tecla, minimo y maximo de travel.
//   min alto -> la tecla no vuelve a reposo = candidato a FANTASMA
//   max bajo -> perdio recorrido = candidato a IMAN DEBIL
//
// (1-ago, revisado) Antes esto era un contador de "tecla pegada" que miraba si
// una tecla se reportaba ON con el travel bajo su desactuacion. Se quito: esa
// condicion NO PUEDE DARSE, porque la FSM se autocorrige — suelta en cuanto
// travel <= regular.deactn_pt. Era una alarma incapaz de sonar. El fantasma real
// es la deriva del reposo, y eso se ve en el minimo.
//   [0..2] minimo de travel en W/SPC/LSFT
//   [3..5] maximo de travel en W/SPC/LSFT
//   [6]    peor maximo entre las observadas — iman debil
//   [7]    posicion de esa tecla, (row << 4) | col
//   [8]    peor minimo (el mas alto) — candidato a fantasma
//   [9]    posicion de esa tecla
//   [10]   cuantas teclas se han observado
#define AWH_HEALTH_LEN 11

#if ANALOG_WINDOW_HISTOGRAM

// Resuelve las teclas vigiladas contra el keymap vivo. La llama
// update_travel_configs(), igual que la politica: boot, cambio de perfil y giro
// del interruptor; el keymap tambien la llama tras un remap en caliente.
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
