// Shim de host. Sustituye a quantum/matrix.h para poder compilar la logica
// analogica REAL (los .c del firmware, sin copiarlos) en el PC.
//
// Dimensiones del K2 HE. Si algun dia se prueba otro teclado, esto es lo unico
// que cambia.
#pragma once

#include <stdint.h>

#ifndef MATRIX_ROWS
#    define MATRIX_ROWS 6
#endif
#ifndef MATRIX_COLS
#    define MATRIX_COLS 16
#endif

typedef uint16_t matrix_row_t;

typedef struct {
    uint8_t col;
    uint8_t row;
} keypos_t;
