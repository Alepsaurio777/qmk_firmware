#pragma once

// Binario de MEDICION (Tier 2). Igual que alex_lab_timed (SOF-sync + pipeline +
// telemetria) MAS los recortes de tiempo del barrido que propuse, para medir si
// mueven la aguja de scan dur_us frente a alex_lab_timed:
//   - HC164_DELAY_NOPS: delay del shift-register. 50 NOPs (~700 ns/flanco) es
//     ~1000x el setup/hold del 74HC164 (nanosegundos). 3 flancos/columna x 15
//     columnas => ~30 us/barrido de puro delay. Bajarlo a 15 recupera ~21 us.
//   - ANALOG_SELECT_SETTLE_US: settle RC del mux analogico tras conmutar columna.
//     En el path pipeline la restriccion es max(settle, procesado_prev). 20->14.
//
// EXPERIMENTAL: si estos recortes son demasiado agresivos apareceran lecturas
// erroneas / selects de columna fantasma. Validar con --events (sin candidatos
// sub-10 ms nuevos) ANTES de creer cualquier ganancia de dur_us. Es un lab.
#include "../alex_lab/config.h"

#define ANALOG_SCAN_SOF_SYNC 1
#define ANALOG_SCAN_PIPELINE 1

// Tier 2a: delay del shift-register HC164 (default 50 en analog_matrix_scan.c).
#define HC164_DELAY_NOPS 15

// Tier 2b: settle del mux. k2_he/config.h lo fija a 20 con #define duro, asi que
// hay que undef antes de bajarlo (mismo patron que usa alex/config.h con otros).
#undef ANALOG_SELECT_SETTLE_US
#define ANALOG_SELECT_SETTLE_US 14
