/*
 * Small, allocation-free FIFO for complete USB HID reports.
 *
 * The caller owns synchronization and must serialize all queue access.
 * A zero-initialized hid_queue_t is ready for use. hid_queue_clear() keeps
 * overflow_count and high_water so they remain useful as diagnostics.
 */
#ifndef USB_HID_QUEUE_H
#define USB_HID_QUEUE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifndef USB_HID_QUEUE_CAPACITY
#    define USB_HID_QUEUE_CAPACITY 32
#endif

#ifndef USB_HID_QUEUE_PACKET_SIZE
#    define USB_HID_QUEUE_PACKET_SIZE 32
#endif

#if USB_HID_QUEUE_CAPACITY < 1 || USB_HID_QUEUE_CAPACITY > 65535
#    error "USB_HID_QUEUE_CAPACITY must be between 1 and 65535"
#endif

#if USB_HID_QUEUE_PACKET_SIZE < 1 || USB_HID_QUEUE_PACKET_SIZE > 255
#    error "USB_HID_QUEUE_PACKET_SIZE must be between 1 and 255"
#endif

typedef struct {
    uint8_t size;
    uint8_t data[USB_HID_QUEUE_PACKET_SIZE];
} hid_queue_packet_t;

typedef struct {
    hid_queue_packet_t packets[USB_HID_QUEUE_CAPACITY];
    uint16_t head;
    uint16_t tail;
    uint16_t count;
    uint16_t high_water;
    uint32_t overflow_count;
} hid_queue_t;

/* Return the number of packets that can still be queued. */
static inline uint16_t hid_queue_free(const hid_queue_t *q) {
    if (q == NULL) {
        return 0;
    }

    return (uint16_t)(USB_HID_QUEUE_CAPACITY - q->count);
}

/* Queue one complete report by copying exactly size payload bytes. */
static inline bool hid_queue_push(hid_queue_t *q, const uint8_t *data, uint8_t size) {
    if (q == NULL || data == NULL || size == 0 || size > USB_HID_QUEUE_PACKET_SIZE) {
        return false;
    }

    if (q->count == USB_HID_QUEUE_CAPACITY) {
        q->overflow_count++;
        return false;
    }

    q->packets[q->tail].size = size;
    for (uint16_t i = 0; i < size; i++) {
        q->packets[q->tail].data[i] = data[i];
    }

    q->tail++;
    if (q->tail == USB_HID_QUEUE_CAPACITY) {
        q->tail = 0;
    }

    q->count++;
    if (q->count > q->high_water) {
        q->high_water = q->count;
    }
    return true;
}

/* Return the oldest complete report, or NULL when the queue is empty. */
static inline const hid_queue_packet_t *hid_queue_front(const hid_queue_t *q) {
    if (q == NULL || q->count == 0) {
        return NULL;
    }

    return &q->packets[q->head];
}

/* Remove the oldest report. This is a no-op for a NULL or empty queue. */
static inline void hid_queue_pop(hid_queue_t *q) {
    if (q == NULL || q->count == 0) {
        return;
    }

    q->head++;
    if (q->head == USB_HID_QUEUE_CAPACITY) {
        q->head = 0;
    }
    q->count--;
}

/* Empty the FIFO while preserving overflow_count and high_water. */
static inline void hid_queue_clear(hid_queue_t *q) {
    if (q == NULL) {
        return;
    }

    q->head = 0;
    q->tail = 0;
    q->count = 0;
}

#endif /* USB_HID_QUEUE_H */
