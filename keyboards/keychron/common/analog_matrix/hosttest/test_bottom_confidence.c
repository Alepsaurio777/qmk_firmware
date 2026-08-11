// Tests del learner robusto de bottom-out. Se compila el modulo real; el test
// solo genera secuencias raw/travel equivalentes a pulsaciones fisicas.

#include <stdio.h>

#include "bottom_out_confidence.h"
#include "harness.h"

#define BASELINE_FULL 2000

static void full_press(bottom_out_confidence_t *state, uint16_t deepest_raw) {
    bottom_out_confidence_observe(state, deepest_raw + 10, ANALOG_CONFIDENT_BOTTOM_OUT_DEEP_TRAVEL, BASELINE_FULL);
    bottom_out_confidence_observe(state, deepest_raw, ANALOG_CONFIDENT_BOTTOM_OUT_DEEP_TRAVEL + 10, BASELINE_FULL);
    bottom_out_confidence_observe(state, deepest_raw + 100, ANALOG_CONFIDENT_BOTTOM_OUT_RELEASE_TRAVEL, BASELINE_FULL);
}

static void test_requires_distinct_presses(void) {
    bottom_out_confidence_t state;
    bottom_out_confidence_reset(&state);

    for (uint8_t i = 0; i < 50; i++) bottom_out_confidence_observe(&state, 1800, 235, BASELINE_FULL);
    CHECK(state.sample_count == 0, "un hold no debe contar antes del release");
    bottom_out_confidence_observe(&state, 1900, 0, BASELINE_FULL);
    CHECK(state.sample_count == 1, "un hold largo debe ser una muestra, dio %u", state.sample_count);

    for (uint8_t i = 1; i < ANALOG_CONFIDENT_BOTTOM_OUT_SAMPLES; i++) full_press(&state, 1800 + (i & 1));
    CHECK(state.sample_count == ANALOG_CONFIDENT_BOTTOM_OUT_SAMPLES, "debe reunir siete muestras");
    CHECK(state.ready, "siete bottom-outs coherentes y mas profundos deben quedar listos");
    CHECK(state.candidate_full == 1880, "mediana 1800 + jitter 80 = 1880, dio %u", state.candidate_full);
}

static void test_one_outlier_is_trimmed(void) {
    bottom_out_confidence_t state;
    bottom_out_confidence_reset(&state);

    full_press(&state, 1500); // extremo imposible, debe quedar fuera del span
    for (uint8_t i = 1; i < ANALOG_CONFIDENT_BOTTOM_OUT_SAMPLES; i++) full_press(&state, 1800);

    CHECK(state.ready, "un solo extremo no debe invalidar seis pulsaciones coherentes");
    CHECK(state.sample_min == 1800 && state.sample_max == 1800, "span recortado esperado 1800..1800, dio %u..%u", state.sample_min, state.sample_max);
    CHECK(state.candidate_full == 1880, "el outlier no debe mover la mediana");
}

static void test_incoherent_window_is_rejected(void) {
    static const uint16_t raw[] = {1700, 1740, 1780, 1820, 1860, 1900, 1940};
    bottom_out_confidence_t state;
    bottom_out_confidence_reset(&state);
    for (uint8_t i = 0; i < ANALOG_CONFIDENT_BOTTOM_OUT_SAMPLES; i++) full_press(&state, raw[i]);

    CHECK(!state.ready, "una dispersion central grande debe rechazarse");
    CHECK(state.rejected_windows == 1, "debe contar la ventana rechazada");
}

static void test_no_material_improvement_is_rejected(void) {
    bottom_out_confidence_t state;
    bottom_out_confidence_reset(&state);
    for (uint8_t i = 0; i < ANALOG_CONFIDENT_BOTTOM_OUT_SAMPLES; i++) full_press(&state, 1900);

    CHECK(state.candidate_full == 1980, "candidato esperado 1980");
    CHECK(!state.ready, "20 raw de mejora no superan epsilon 30");
}

int main(void) {
    test_requires_distinct_presses();
    test_one_outlier_is_trimmed();
    test_incoherent_window_is_rejected();
    test_no_material_improvement_is_rejected();
    return hosttest_report("bottom_out_confidence");
}
