// Shim de host: sustituye a quantum/action_layer.h.
//
// analog_matrix_is_gaming_mode() cuelga de default_layer_state, asi que el test
// lo controla poniendo esta variable. Gaming = capa 0 (bit 0).
#pragma once

#include <stdint.h>

typedef uint32_t layer_state_t;

extern layer_state_t default_layer_state;
extern layer_state_t layer_state;

// Helpers para que el test declare intencion en vez de manipular bits.
#define HOSTTEST_LAYER_GAMING 0
#define HOSTTEST_LAYER_WINDOWS 2

static inline void hosttest_set_gaming(void) {
    default_layer_state = (layer_state_t)1 << HOSTTEST_LAYER_GAMING;
}
static inline void hosttest_set_windows(void) {
    default_layer_state = (layer_state_t)1 << HOSTTEST_LAYER_WINDOWS;
}
