#include "bottom_out_confidence.h"

#if ANALOG_CONFIDENT_BOTTOM_OUT_ENABLE

#include <limits.h>
#include <string.h>

static void sort_samples(uint16_t *values) {
    for (uint8_t i = 1; i < ANALOG_CONFIDENT_BOTTOM_OUT_SAMPLES; i++) {
        uint16_t value = values[i];
        uint8_t  j     = i;
        while (j > 0 && values[j - 1] > value) {
            values[j] = values[j - 1];
            j--;
        }
        values[j] = value;
    }
}

static void evaluate_window(bottom_out_confidence_t *state, uint16_t baseline_full) {
    uint16_t ordered[ANALOG_CONFIDENT_BOTTOM_OUT_SAMPLES];
    memcpy(ordered, state->samples, sizeof(ordered));
    sort_samples(ordered);

    // Ignorar un extremo por lado: una pulsacion anomala no invalida por si
    // sola las otras cinco. La mediana sigue siendo la estimacion aplicada.
    state->sample_min = ordered[1];
    state->sample_max = ordered[ANALOG_CONFIDENT_BOTTOM_OUT_SAMPLES - 2];

    uint32_t candidate = (uint32_t)ordered[ANALOG_CONFIDENT_BOTTOM_OUT_SAMPLES / 2] + BOTTOM_JITTER;
    if (candidate > UINT16_MAX) candidate = UINT16_MAX;
    state->candidate_full = (uint16_t)candidate;

    const bool coherent = state->sample_max - state->sample_min <= ANALOG_CONFIDENT_BOTTOM_OUT_MAX_SPREAD;
    const bool deeper   = (uint32_t)state->candidate_full + ANALOG_BOTTOM_OUT_LEARN_EPSILON < baseline_full;
    state->ready        = coherent && deeper;
    if (!state->ready && state->rejected_windows != UINT16_MAX) state->rejected_windows++;
}

void bottom_out_confidence_reset(bottom_out_confidence_t *state) {
    memset(state, 0, sizeof(*state));
    state->deepest_raw = UINT16_MAX;
}

bool bottom_out_confidence_observe(bottom_out_confidence_t *state, uint16_t raw, uint8_t travel, uint16_t baseline_full) {
    if (travel >= ANALOG_CONFIDENT_BOTTOM_OUT_DEEP_TRAVEL) {
        if (!state->tracking) {
            state->tracking    = true;
            state->deepest_raw = raw;
        } else if (raw < state->deepest_raw) {
            state->deepest_raw = raw;
        }
        return false;
    }

    if (!state->tracking || travel > ANALOG_CONFIDENT_BOTTOM_OUT_RELEASE_TRAVEL) return false;

    state->tracking = false;
    if (state->completed_presses != UINT16_MAX) state->completed_presses++;

    if (state->sample_count < ANALOG_CONFIDENT_BOTTOM_OUT_SAMPLES) {
        state->samples[state->sample_count++] = state->deepest_raw;
    } else {
        memmove(&state->samples[0], &state->samples[1], sizeof(state->samples[0]) * (ANALOG_CONFIDENT_BOTTOM_OUT_SAMPLES - 1));
        state->samples[ANALOG_CONFIDENT_BOTTOM_OUT_SAMPLES - 1] = state->deepest_raw;
    }
    state->deepest_raw = UINT16_MAX;

    if (state->sample_count == ANALOG_CONFIDENT_BOTTOM_OUT_SAMPLES) evaluate_window(state, baseline_full);
    return true;
}

#endif
