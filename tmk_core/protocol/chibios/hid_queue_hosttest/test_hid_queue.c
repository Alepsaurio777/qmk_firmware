#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "../usb_hid_queue.h"

#define CHECK(condition)                                                                 \
    do {                                                                                 \
        if (!(condition)) {                                                              \
            fprintf(stderr, "FAIL: %s:%d: %s\n", __FILE__, __LINE__, #condition);      \
            return 1;                                                                    \
        }                                                                                \
    } while (0)

static int check_packet(const hid_queue_packet_t *packet, const uint8_t *data, uint8_t size) {
    CHECK(packet != NULL);
    CHECK(packet->size == size);
    CHECK(memcmp(packet->data, data, size) == 0);
    return 0;
}

static int test_zero_initialization(void) {
    hid_queue_t q = {0};

    CHECK(q.count == 0);
    CHECK(q.high_water == 0);
    CHECK(q.overflow_count == 0);
    CHECK(hid_queue_free(&q) == USB_HID_QUEUE_CAPACITY);
    CHECK(hid_queue_front(&q) == NULL);
    return 0;
}

static int test_multiple_sizes_and_source_copy(void) {
    hid_queue_t q = {0};
    uint8_t source[USB_HID_QUEUE_PACKET_SIZE];
    uint8_t expected[USB_HID_QUEUE_PACKET_SIZE];

    for (uint16_t size = 1; size <= USB_HID_QUEUE_PACKET_SIZE; size++) {
        for (uint16_t i = 0; i < size; i++) {
            source[i] = (uint8_t)(size + i);
            expected[i] = source[i];
        }

        CHECK(hid_queue_push(&q, source, (uint8_t)size));
        memset(source, 0, size);
        CHECK(check_packet(hid_queue_front(&q), expected, (uint8_t)size) == 0);
        hid_queue_pop(&q);
        CHECK(hid_queue_front(&q) == NULL);
    }

    CHECK(q.high_water == 1);
    CHECK(q.count == 0);
    return 0;
}

static int test_fifo_wrap(void) {
    hid_queue_t q = {0};
    uint8_t expected[USB_HID_QUEUE_CAPACITY][USB_HID_QUEUE_PACKET_SIZE];
    uint8_t sizes[USB_HID_QUEUE_CAPACITY];
    uint8_t wrap_data[USB_HID_QUEUE_PACKET_SIZE];

    for (uint16_t i = 0; i < USB_HID_QUEUE_CAPACITY; i++) {
        sizes[i] = (uint8_t)((i % USB_HID_QUEUE_PACKET_SIZE) + 1);
        for (uint16_t j = 0; j < sizes[i]; j++) {
            expected[i][j] = (uint8_t)(0x20 + i + j);
        }
        CHECK(hid_queue_push(&q, expected[i], sizes[i]));
    }

    CHECK(q.count == USB_HID_QUEUE_CAPACITY);
    CHECK(q.high_water == USB_HID_QUEUE_CAPACITY);
    CHECK(hid_queue_free(&q) == 0);

    CHECK(check_packet(hid_queue_front(&q), expected[0], sizes[0]) == 0);
    hid_queue_pop(&q);

    for (uint16_t i = 0; i < USB_HID_QUEUE_PACKET_SIZE; i++) {
        wrap_data[i] = (uint8_t)(0xa0 + i);
    }
    CHECK(hid_queue_push(&q, wrap_data, USB_HID_QUEUE_PACKET_SIZE));

    for (uint16_t i = 1; i < USB_HID_QUEUE_CAPACITY; i++) {
        CHECK(check_packet(hid_queue_front(&q), expected[i], sizes[i]) == 0);
        hid_queue_pop(&q);
    }
    CHECK(check_packet(hid_queue_front(&q), wrap_data, USB_HID_QUEUE_PACKET_SIZE) == 0);
    hid_queue_pop(&q);
    CHECK(hid_queue_front(&q) == NULL);
    CHECK(hid_queue_free(&q) == USB_HID_QUEUE_CAPACITY);
    return 0;
}

static int test_overflow_preserves_existing_frames(void) {
    hid_queue_t q = {0};
    uint8_t expected[USB_HID_QUEUE_CAPACITY][USB_HID_QUEUE_PACKET_SIZE];
    uint8_t sizes[USB_HID_QUEUE_CAPACITY];
    uint8_t overflow_data[USB_HID_QUEUE_PACKET_SIZE];
    uint16_t count_before;
    uint16_t high_water_before;

    for (uint16_t i = 0; i < USB_HID_QUEUE_CAPACITY; i++) {
        sizes[i] = (uint8_t)((i % USB_HID_QUEUE_PACKET_SIZE) + 1);
        for (uint16_t j = 0; j < sizes[i]; j++) {
            expected[i][j] = (uint8_t)(0x40 + i + j);
        }
        CHECK(hid_queue_push(&q, expected[i], sizes[i]));
    }
    count_before = q.count;
    high_water_before = q.high_water;
    memset(overflow_data, 0xee, sizeof(overflow_data));

    CHECK(!hid_queue_push(&q, overflow_data, USB_HID_QUEUE_PACKET_SIZE));
    CHECK(q.count == count_before);
    CHECK(q.high_water == high_water_before);
    CHECK(q.overflow_count == 1);
    CHECK(hid_queue_free(&q) == 0);

    for (uint16_t i = 0; i < USB_HID_QUEUE_CAPACITY; i++) {
        CHECK(check_packet(hid_queue_front(&q), expected[i], sizes[i]) == 0);
        hid_queue_pop(&q);
    }
    CHECK(hid_queue_front(&q) == NULL);
    return 0;
}

static int test_invalid_sizes_do_not_change_queue(void) {
    hid_queue_t q = {0};
    uint8_t valid[USB_HID_QUEUE_PACKET_SIZE];
    uint8_t oversize[USB_HID_QUEUE_PACKET_SIZE + 1];
    const uint8_t valid_size = (uint8_t)(USB_HID_QUEUE_PACKET_SIZE < 3 ? USB_HID_QUEUE_PACKET_SIZE : 3);
    uint16_t count_before;
    uint16_t high_water_before;
    uint32_t overflow_before;

    for (uint16_t i = 0; i < valid_size; i++) {
        valid[i] = (uint8_t)(0x11 + i);
    }
    memset(oversize, 0x55, sizeof(oversize));
    CHECK(hid_queue_push(&q, valid, valid_size));
    count_before = q.count;
    high_water_before = q.high_water;
    overflow_before = q.overflow_count;

    CHECK(!hid_queue_push(&q, valid, 0));
    CHECK(!hid_queue_push(&q, oversize, USB_HID_QUEUE_PACKET_SIZE + 1));
    CHECK(q.count == count_before);
    CHECK(q.high_water == high_water_before);
    CHECK(q.overflow_count == overflow_before);
    CHECK(check_packet(hid_queue_front(&q), valid, valid_size) == 0);
    return 0;
}

static int test_clear_preserves_diagnostics(void) {
    hid_queue_t q = {0};
    const uint8_t packet[] = {0x91};
    uint16_t high_water_before;

    for (uint16_t i = 0; i < USB_HID_QUEUE_CAPACITY; i++) {
        CHECK(hid_queue_push(&q, packet, sizeof(packet)));
    }
    CHECK(!hid_queue_push(&q, packet, sizeof(packet)));
    high_water_before = q.high_water;

    hid_queue_pop(&q);
    CHECK(hid_queue_push(&q, packet, sizeof(packet)));
    CHECK(q.count == USB_HID_QUEUE_CAPACITY);

    hid_queue_clear(&q);
    CHECK(q.count == 0);
    CHECK(q.high_water == high_water_before);
    CHECK(q.overflow_count == 1);
    CHECK(hid_queue_free(&q) == USB_HID_QUEUE_CAPACITY);
    CHECK(hid_queue_front(&q) == NULL);

    CHECK(hid_queue_push(&q, packet, sizeof(packet)));
    CHECK(check_packet(hid_queue_front(&q), packet, sizeof(packet)) == 0);
    CHECK(q.high_water == high_water_before);
    return 0;
}

int main(void) {
    CHECK(test_zero_initialization() == 0);
    CHECK(test_multiple_sizes_and_source_copy() == 0);
    CHECK(test_fifo_wrap() == 0);
    CHECK(test_overflow_preserves_existing_frames() == 0);
    CHECK(test_invalid_sizes_do_not_change_queue() == 0);
    CHECK(test_clear_preserves_diagnostics() == 0);
    puts("usb_hid_queue host tests: PASS");
    return 0;
}
