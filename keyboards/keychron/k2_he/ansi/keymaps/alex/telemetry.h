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
// ACOPLAMIENTO MINIMO — esto es temporal y se quitara al cerrar el proyecto.
// Para eliminarlo por completo bastan 4 cosas:
//   1. borrar telemetry.c / telemetry.h
//   2. quitar "SRC += telemetry.c" de rules.mk
//   3. quitar el #include "telemetry.h" de keymap.c
//   4. quitar las 3 llamadas de keymap.c (las de abajo)
// Nada mas del firmware lo referencia.

void telemetry_task(void);                                                        // housekeeping
void evlog_task(void);                                                            // housekeeping
void evlog_record_event(uint16_t keycode, bool pressed, uint8_t row, uint8_t col); // process_record_user
