// Shim de host: sustituye a platforms/timer.h.
//
// El reloj lo mueve el test (harness.c), no la pared: probar una ventana de
// 55 ms no debe costar 55 ms ni depender del scheduler.
#pragma once

#include <stdbool.h>
#include <stdint.h>

uint16_t timer_read(void);
bool     timer_expired(uint16_t current, uint16_t future);
