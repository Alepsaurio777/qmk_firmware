// Copyright 2026
// SPDX-License-Identifier: GPL-2.0-or-later
#include "usb_deferred.h"
#include "usb_hid_deferred.h"
#include "usb_driver.h"
#include "usb_main.h"
#include "action_util.h"

extern usb_endpoint_in_t usb_endpoints_in[USB_ENDPOINT_IN_COUNT];
void send_keyboard(report_keyboard_t *report);
void send_nkro(report_nkro_t *report);

// Only HID keyboard/shared and RAW use this path. Bulk/console streams retain
// their own framing. Externally visible in ELF for diagnostics, no scan I/O.
hid_deferred_t usb_hid_pending[USB_ENDPOINT_IN_COUNT];
static volatile uint32_t bus_generation;
static uint32_t seen_generation;
static uint8_t seen_protocol;
static bool restore_keyboard;

void usb_deferred_bus_changed(void) {
    ++bus_generation;
}

static bool try_send(void *ctx, const uint8_t *data, size_t size) {
    return usb_endpoint_in_try_send(ctx, data, size);
}

static void update_bus_state(void) {
    const uint32_t generation = bus_generation;
    const uint8_t protocol = usb_device_state_get_protocol();
    if (generation == seen_generation && protocol == seen_protocol) return;
    seen_generation = generation;
    seen_protocol = protocol;
    for (unsigned i = 0; i < USB_ENDPOINT_IN_COUNT; ++i) hid_deferred_reset(&usb_hid_pending[i]);
    // QMK deduplicates against the last generated report. After bus reset or a
    // protocol change it need not generate a new edge for a held key; explicitly
    // resend its current state once active, using the new protocol's framing.
    restore_keyboard = true;
}

bool usb_deferred_send(usb_endpoint_in_lut_t ep, const uint8_t *data, size_t size) {
    if (!IS_VALID_USB_ENDPOINT_IN_LUT(ep)) return false;
    update_bus_state();
    unsigned id = 0;
#ifdef RAW_ENABLE
    if (ep == USB_ENDPOINT_IN_RAW) {
        id = HID_STATELESS_REPORT;
    } else
#endif
#ifdef SHARED_EP_ENABLE
    if (ep == USB_ENDPOINT_IN_SHARED && !(ep == USB_ENDPOINT_IN_KEYBOARD && usb_device_state_get_protocol() == USB_PROTOCOL_BOOT)) {
        if (!size) return false;
        id = data[0];
    }
#else
    { }
#endif
    return hid_deferred_submit(&usb_hid_pending[ep], id, data, size, try_send, &usb_endpoints_in[ep]);
}

void usb_deferred_task(void) {
    update_bus_state();
    if (USB_DRIVER.state != USB_ACTIVE) return;
    if (restore_keyboard) {
        restore_keyboard = false;
        report_keyboard_t keyboard = *keyboard_report;
#ifdef KEYBOARD_SHARED_EP
        keyboard.report_id = REPORT_ID_KEYBOARD;
#endif
        send_keyboard(&keyboard);
#ifdef NKRO_ENABLE
        if (usb_device_state_get_protocol() == USB_PROTOCOL_REPORT) {
            report_nkro_t nkro = *nkro_report;
            nkro.report_id = REPORT_ID_NKRO;
            send_nkro(&nkro);
        }
#endif
    }
    // Four transport buffers per endpoint by default. Budget bounds main-loop
    // work even if an ISR frees buffers while this task is draining.
    for (unsigned i = 0; i < USB_ENDPOINT_IN_COUNT; ++i) {
        hid_deferred_drain(&usb_hid_pending[i], try_send, &usb_endpoints_in[i], 4);
    }
}

bool usb_deferred_raw_ready(void) {
#ifdef RAW_ENABLE
    return hid_queue_free(&usb_hid_pending[USB_ENDPOINT_IN_RAW].queue) > 0;
#else
    return false;
#endif
}
