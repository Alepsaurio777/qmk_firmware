// Copyright 2026
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "usb_endpoints.h"

bool usb_deferred_send(usb_endpoint_in_lut_t endpoint, const uint8_t *data, size_t size);
void usb_deferred_task(void);
bool usb_deferred_raw_ready(void);
// ISR-safe: only increments a generation counter; main owns all packet data.
void usb_deferred_bus_changed(void);
