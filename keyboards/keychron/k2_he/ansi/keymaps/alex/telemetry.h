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

#include <stdbool.h>
#include <stdint.h>

// Diagnosticos por Raw HID: telemetria de travel + logger de eventos
// (mistype-hunt). Ver TELEMETRY.md.
//
// Se arrancan y paran desde el cliente por comando HID (0xEE), NO por keycode:
// un keycode depende del keymap, y el keymap de VIA vive en EEPROM, que puede
// pisar el default del firmware y dejar el toggle inalcanzable.
//
// Acoplamiento explicito: alex_lab/rules.mk define ALEX_TELEMETRY_ENABLE y
// keymap.c guarda include/llamadas con ese flag. `alex` lo deja en no desde
// rules-common.mk; no hay que editar logica compartida para aislarlo.

void telemetry_task(void);                                                        // housekeeping
void evlog_task(void);                                                            // housekeeping
void evlog_record_event(uint16_t keycode, bool pressed, uint8_t row, uint8_t col); // process_record_user

// Lo llama el override de kc_raw_hid_rx_user en keymap.c. No es el override en
// si: ese tiene que existir con o sin telemetria, porque tambien dispara la
// re-resolucion de la politica por keycode cuando Launcher remapea.
void telemetry_raw_hid_rx(uint8_t src, uint8_t *data, uint8_t length);

// Resuelve las teclas vigiladas (declaradas por keycode) a posiciones de matriz
// leyendo el keymap vivo. Hay que llamarla en boot y tras cada remap; keymap.c lo
// hace en los mismos dos puntos que la politica del analog matrix.
void telemetry_resolve_keys(void);
