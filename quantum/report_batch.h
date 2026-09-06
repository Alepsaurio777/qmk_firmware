// Copyright 2026
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <stdbool.h>
#include <stdint.h>

// A batch ends at the matrix boundary or before an action with side effects.
void keyboard_report_batch_begin(bool enabled);
void keyboard_report_batch_end(void);
bool keyboard_report_batch_active(void);
void keyboard_report_batch_before_key(uint8_t code);
void keyboard_report_batch_send(void);

// Implemented by action_util.c. Emit the current state without re-entering a batch.
void keyboard_report_batch_emit(void);
// Opt-in policy supplied by the keyboard/keymap; the default is false.
bool keyboard_report_batch_enabled(void);
