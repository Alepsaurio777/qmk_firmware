#pragma once

#include <stdbool.h>

// Telemetria de profundidad por Raw HID. Ver TELEMETRY.md para el formato de
// paquete y el proposito. Solo opera fuera del modo Gaming.
void telemetry_toggle(void);
void telemetry_stop(void);
void telemetry_task(void);
bool telemetry_is_active(void);
