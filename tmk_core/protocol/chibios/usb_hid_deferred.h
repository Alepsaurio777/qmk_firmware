// Copyright 2026
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "usb_hid_queue.h"
#include <string.h>

#ifndef USB_HID_STATE_SLOTS
#    define USB_HID_STATE_SLOTS 10
#endif
#if USB_HID_STATE_SLOTS < 1 || USB_HID_STATE_SLOTS > 16
#    error "USB_HID_STATE_SLOTS must fit the 16-bit resynchronization mask"
#endif
#define HID_STATELESS_REPORT 255

typedef bool (*hid_try_send_t)(void *context, const uint8_t *data, size_t size);

typedef struct {
    hid_queue_t queue;
    hid_queue_packet_t latest[USB_HID_STATE_SLOTS];
    uint16_t resync;
    uint32_t recovered_reports;
} hid_deferred_t;

// Call from one producer thread. The transport callback owns its ISR lock.
static inline void hid_deferred_drain(hid_deferred_t *s, hid_try_send_t send, void *ctx, unsigned budget) {
    while (budget) {
        const hid_queue_packet_t *p = hid_queue_front(&s->queue);
        if (!p) break;
        if (!send(ctx, p->data, p->size)) return;
        hid_queue_pop(&s->queue);
        --budget;
    }
    if (hid_queue_front(&s->queue)) return;
    for (unsigned id = 0; id < USB_HID_STATE_SLOTS && budget; ++id) {
        if (!(s->resync & ((uint16_t)1 << id))) continue;
        const hid_queue_packet_t *p = &s->latest[id];
        if (!send(ctx, p->data, p->size)) return;
        s->resync &= ~((uint16_t)1 << id);
        ++s->recovered_reports;
        --budget;
    }
}

static inline bool hid_deferred_submit(hid_deferred_t *s, unsigned id, const uint8_t *data, size_t size, hid_try_send_t send, void *ctx) {
    if (!data || !size || size > USB_HID_QUEUE_PACKET_SIZE || (id >= USB_HID_STATE_SLOTS && id != HID_STATELESS_REPORT)) return false;
    if (id != HID_STATELESS_REPORT) {
        s->latest[id].size = size;
        memcpy(s->latest[id].data, data, size);
    }
    // Once capacity is exhausted, finish the accepted FIFO first and then
    // restore the latest state of every affected report ID. This bounds RAM
    // and scan time even if a host stops polling indefinitely. Intermediate
    // edges beyond capacity cannot be retained; overflow_count exposes this.
    if (s->resync) {
        if (id != HID_STATELESS_REPORT) s->resync |= (uint16_t)1 << id;
        ++s->queue.overflow_count;
        return false;
    }
    if (!hid_queue_front(&s->queue) && send(ctx, data, size)) return true;
    if (hid_queue_push(&s->queue, data, size)) return true;
    if (id != HID_STATELESS_REPORT) s->resync |= (uint16_t)1 << id;
    return false;
}

static inline void hid_deferred_reset(hid_deferred_t *s) {
    hid_queue_clear(&s->queue);
    s->resync = 0;
    memset(s->latest, 0, sizeof(s->latest));
}
