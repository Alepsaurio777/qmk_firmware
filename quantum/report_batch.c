// Copyright 2026
// SPDX-License-Identifier: GPL-2.0-or-later
#include "report_batch.h"
#include <string.h>

static bool active;
static bool pending;
static uint8_t changed_codes[32];

static void flush(void) {
    if (pending) {
        pending = false;
        keyboard_report_batch_emit();
    }
    memset(changed_codes, 0, sizeof(changed_codes));
}

__attribute__((weak)) bool keyboard_report_batch_enabled(void) {
    return false;
}

void keyboard_report_batch_begin(bool enabled) {
    keyboard_report_batch_end();
    active = enabled;
}

void keyboard_report_batch_end(void) {
    active = false;
    flush();
}

bool keyboard_report_batch_active(void) {
    return active;
}

void keyboard_report_batch_before_key(uint8_t code) {
    if (!active) return;
    const uint8_t mask = (uint8_t)1 << (code & 7);
    // Called BEFORE mutating the report. Never merge two edges of one usage,
    // including deliberate retriggers from two physical owners of a keycode.
    if (changed_codes[code >> 3] & mask) flush();
    changed_codes[code >> 3] |= mask;
}

void keyboard_report_batch_send(void) {
    if (active) {
        pending = true;
    } else {
        keyboard_report_batch_emit();
    }
}
