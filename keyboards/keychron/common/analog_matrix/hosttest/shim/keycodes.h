// Shim de host: sustituye a quantum/keycodes.h.
//
// Solo los keycodes que la logica analogica menciona. Los valores son los REALES
// de QMK: las whitelists por keycode comparan contra estos, asi que un valor
// inventado haria pasar tests que en firmware fallarian.
#pragma once

#include <stdint.h>
// analog_matrix.h incluye keycodes.h ANTES que matrix.h, asi que keypos_t
// todavia no existe cuando declaramos keymap_key_to_keycode() aqui abajo.
#include "matrix.h"

enum hosttest_keycodes {
    KC_NO          = 0x0000,
    KC_TRANSPARENT = 0x0001,

    KC_A = 0x0004,
    KC_D = 0x0007,
    KC_S = 0x0016,
    KC_W = 0x001A,

    KC_SPACE = 0x002C,

    KC_LEFT_CTRL  = 0x00E0,
    KC_LEFT_SHIFT = 0x00E1,
};

#define KC_SPC KC_SPACE
#define KC_LSFT KC_LEFT_SHIFT
#define KC_LCTL KC_LEFT_CTRL

// La resolucion por keycode lee el keymap vivo. En host lo sirve el harness.
uint16_t keymap_key_to_keycode(uint8_t layer, keypos_t key);
