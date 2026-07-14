#pragma once

#include <stdbool.h>
#include <stdint.h>

// Telemetria de profundidad por Raw HID. Ver TELEMETRY.md para el formato de
// paquete y el proposito. Solo opera fuera del modo Gaming.
void telemetry_toggle(void);
void telemetry_stop(void);
void telemetry_task(void);
bool telemetry_is_active(void);

// Logger de eventos (mistype-hunt). Registra cambios de estado (press/release)
// de las teclas de movimiento con el travel del instante. Event-driven, SI
// corre en Gaming (el objetivo es cazar fantasmas durante juego real).
void evlog_toggle(void);
void evlog_stop(void);
void evlog_record_event(uint16_t keycode, bool pressed, uint8_t row, uint8_t col);
void evlog_task(void);
bool evlog_is_active(void);
