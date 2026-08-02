// Shim de host: sustituye a quantum/keymap_common.h.
//
// En el teclado esto lee el keymap dinamico de VIA desde EEPROM. Aqui lo sirve
// la tabla falsa del harness, que es justo lo que permite probar el caso que el
// diseno por coordenadas no cubria: remapear una tecla y comprobar que la
// politica la SIGUE en vez de quedarse en el hueco viejo.
#pragma once

#include <stdint.h>

#include "matrix.h"

uint16_t keymap_key_to_keycode(uint8_t layer, keypos_t key);
