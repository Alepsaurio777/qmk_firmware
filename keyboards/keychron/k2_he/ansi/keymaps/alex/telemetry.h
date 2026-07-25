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
// Acoplamiento explicito: rules-common.mk define ALEX_TELEMETRY_ENABLE y
// keymap.c guarda include/llamadas con ese flag. Para compilarlo fuera basta
// ALEX_TELEMETRY_ENABLE = no; no hay que editar logica compartida.

void telemetry_task(void);                                                        // housekeeping
void evlog_task(void);                                                            // housekeeping
void evlog_record_event(uint16_t keycode, bool pressed, uint8_t row, uint8_t col); // process_record_user

// Lo llama el override de kc_raw_hid_rx_user en keymap.c. No es el override en
// si: ese tiene que existir con o sin telemetria, porque tambien dispara la
// re-resolucion de la politica por keycode cuando Launcher remapea.
void telemetry_raw_hid_rx(uint8_t src, uint8_t *data, uint8_t length);
