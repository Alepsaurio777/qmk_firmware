// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <stdbool.h>
#include <stdint.h>

static inline uint8_t analog_startup_noise_filter_from_span(uint16_t span) {
    if (span > ANALOG_STARTUP_NOISE_MAX_VALID_SPAN) return ANALOG_STARTUP_NOISE_FILTER_MAX;

    uint16_t filter = span + ANALOG_STARTUP_NOISE_FILTER_MARGIN;
    if (filter < ANALOG_STARTUP_NOISE_FILTER_MIN) filter = ANALOG_STARTUP_NOISE_FILTER_MIN;
    if (filter > ANALOG_STARTUP_NOISE_FILTER_MAX) filter = ANALOG_STARTUP_NOISE_FILTER_MAX;
    return (uint8_t)filter;
}

// V4.2 compatibility helper: derives only the span/noise threshold. Kept for
// the existing tests and for callers that do not have an independent release
// reference.
static inline uint8_t analog_startup_noise_filter_from_samples(const uint16_t *samples, uint8_t count, uint8_t fallback) {
    if (samples == 0 || count < 2) return fallback;

    uint16_t min_v = UINT16_MAX;
    uint16_t max_v = 0;
    for (uint8_t i = 0; i < count; i++) {
        const uint16_t v = samples[i];
        if (v < VALID_ANALOG_RAW_VALUE_MIN || v > VALID_ANALOG_RAW_VALUE_MAX) return fallback;
        if (v < min_v) min_v = v;
        if (v > max_v) max_v = v;
    }

    return analog_startup_noise_filter_from_span(max_v - min_v);
}

// V4.2.3-A: a quiet signal is not enough to call a key "released". A held key
// can be extremely stable. Accept adaptive startup noise only when the samples
// are valid AND their mean is close to an independent, previously established
// release reference. The caller decides the release window; failure leaves the
// key on the static filter for the whole session.
static inline bool analog_startup_noise_filter_if_released(const uint16_t *samples, uint8_t count, uint16_t expected_release_raw, uint16_t release_window, uint8_t *out_filter) {
    if (samples == 0 || out_filter == 0 || count < 2) return false;
    if (expected_release_raw < VALID_ANALOG_RAW_VALUE_MIN || expected_release_raw > VALID_ANALOG_RAW_VALUE_MAX) return false;

    uint16_t min_v = UINT16_MAX;
    uint16_t max_v = 0;
    uint32_t sum   = 0;

    for (uint8_t i = 0; i < count; i++) {
        const uint16_t v = samples[i];
        if (v < VALID_ANALOG_RAW_VALUE_MIN || v > VALID_ANALOG_RAW_VALUE_MAX) return false;
        if (v < min_v) min_v = v;
        if (v > max_v) max_v = v;
        sum += v;
    }

    const uint16_t span = max_v - min_v;
    if (span > ANALOG_STARTUP_NOISE_MAX_VALID_SPAN) return false;

    const uint16_t mean = (uint16_t)(sum / count);
    const uint16_t diff = mean > expected_release_raw ? mean - expected_release_raw : expected_release_raw - mean;
    if (diff > release_window) return false;

    *out_filter = analog_startup_noise_filter_from_span(span);
    return true;
}
