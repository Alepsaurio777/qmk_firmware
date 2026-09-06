/* Copyright 2019 Nick Brassel (tzarc)
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 */

#include <stdint.h>
#include <string.h>
#if defined(EXTERNAL_EEPROM_WP_PIN)
#    include "gpio.h"
#endif
#    include "eeprom_he.h"

/*
    Note that the implementations of eeprom_XXXX_YYYY on AVR are normally
    provided by avr-libc. The same functions are reimplemented below and are
    rerouted to the external i2c equivalent.

    Seemingly, as this is compiled from within QMK, the object file generated
    during the build overrides the avr-libc implementation during the linking
    stage.

    On other platforms such as ARM, there are no provided implementations, so
    there is nothing to override during linkage.
*/

#include "wait.h"
#include "i2c_master.h"
#include "eeprom.h"
#include "eeprom_he.h"

// #define DEBUG_EEPROM_OUTPUT

#if defined(CONSOLE_ENABLE) && defined(DEBUG_EEPROM_OUTPUT)
#    include "timer.h"
#    include "debug.h"
#endif // DEBUG_EEPROM_OUTPUT

#ifdef USE_GPIOV1
#    ifndef I2C1_SCL_PAL_MODE
#        define I2C1_SCL_PAL_MODE PAL_MODE_ALTERNATE_OPENDRAIN
#    endif
#    ifndef I2C1_SDA_PAL_MODE
#        define I2C1_SDA_PAL_MODE PAL_MODE_ALTERNATE_OPENDRAIN
#    endif
#else
// The default PAL alternate modes are used to signal that the pins are used for I2C
#    ifndef I2C1_SCL_PAL_MODE
#        define I2C1_SCL_PAL_MODE 4
#    endif
#    ifndef I2C1_SDA_PAL_MODE
#        define I2C1_SDA_PAL_MODE 4
#    endif
#endif

static inline void fill_target_address(uint8_t *buffer, const void *addr) {
    uintptr_t p = (uintptr_t)addr;
    for (int i = 0; i < EXTERNAL_EEPROM_ADDRESS_SIZE; ++i) {
        buffer[EXTERNAL_EEPROM_ADDRESS_SIZE - 1 - i] = p & 0xFF;
        p >>= 8;
    }
}

/* Override i2c_init */
void i2c_init(void) {
    // Try releasing special pins for a short time
    palSetLineMode(I2C1_SCL_PIN, PAL_MODE_INPUT);
    palSetLineMode(I2C1_SDA_PIN, PAL_MODE_INPUT);

    chThdSleepMilliseconds(10);
#if defined(USE_GPIOV1)
    palSetLineMode(I2C1_SCL_PIN, I2C1_SCL_PAL_MODE);
    palSetLineMode(I2C1_SDA_PIN, I2C1_SDA_PAL_MODE);
#else
    palSetLineMode(I2C1_SCL_PIN, PAL_MODE_ALTERNATE(I2C1_SCL_PAL_MODE) | PAL_OUTPUT_TYPE_OPENDRAIN);
    palSetLineMode(I2C1_SDA_PIN, PAL_MODE_ALTERNATE(I2C1_SDA_PAL_MODE) | PAL_OUTPUT_TYPE_OPENDRAIN);
#endif
}

void he_eeprom_driver_init(void) {

    i2c_init();
#if defined(EXTERNAL_EEPROM_WP_PIN)
    /* We are setting the WP pin to high in a way that requires at least two bit-flips to change back to 0 */
    writePin(EXTERNAL_EEPROM_WP_PIN, 1);
    setPinInputHigh(EXTERNAL_EEPROM_WP_PIN);
#endif
}

bool he_eeprom_driver_erase(void) {

#if defined(CONSOLE_ENABLE) && defined(DEBUG_EEPROM_OUTPUT)
    uint32_t start = timer_read32();
#endif

    bool    ok = true;
    uint8_t buf[EXTERNAL_EEPROM_PAGE_SIZE];
    memset(buf, 0x00, EXTERNAL_EEPROM_PAGE_SIZE);
    for (uint32_t addr = 0; addr < EXTERNAL_EEPROM_BYTE_COUNT; addr += EXTERNAL_EEPROM_PAGE_SIZE) {
        if (!he_eeprom_write_block(buf, (void *)(uintptr_t)addr, EXTERNAL_EEPROM_PAGE_SIZE)) ok = false;
    }
    return ok;
}

bool he_eeprom_read_block(void *buf, const void *addr, size_t len) {
    uintptr_t target_addr = (uintptr_t)addr;
    if (target_addr > EXTERNAL_EEPROM_BYTE_COUNT || len > EXTERNAL_EEPROM_BYTE_COUNT - target_addr) {
        return false;
    }
    if (len == 0) {
        return true;
    }

    uint8_t complete_packet[EXTERNAL_EEPROM_ADDRESS_SIZE];
    fill_target_address(complete_packet, addr);

    if (i2c_transmit(EXTERNAL_EEPROM_I2C_ADDRESS(target_addr), complete_packet, EXTERNAL_EEPROM_ADDRESS_SIZE, EXTERNAL_EEPROM_I2C_TIMEOUT) != I2C_STATUS_SUCCESS) return false;
    return i2c_receive(EXTERNAL_EEPROM_I2C_ADDRESS(target_addr), buf, len, EXTERNAL_EEPROM_I2C_TIMEOUT) == I2C_STATUS_SUCCESS;
}

/*
 * NOTE (CPU budget / jitter): the per-page wait_ms(EXTERNAL_EEPROM_WRITE_TIME)
 * below is mandated by the EEPROM's internal write cycle and cannot be removed
 * without risking data corruption. It is therefore the caller's responsibility
 * to invoke this function only outside the time-critical matrix scan (i.e. on
 * explicit calibration save or while idle), never from the hot scan path.
 */
bool he_eeprom_write_block(const void *buf, void *addr, size_t len) {

    uint8_t   complete_packet[EXTERNAL_EEPROM_ADDRESS_SIZE + EXTERNAL_EEPROM_PAGE_SIZE];
    uint8_t * read_buf    = (uint8_t *)buf;
    uintptr_t target_addr = (uintptr_t)addr;
    bool      ok          = true;

    if (target_addr > EXTERNAL_EEPROM_BYTE_COUNT || len > EXTERNAL_EEPROM_BYTE_COUNT - target_addr) {
        return false;
    }
    if (len == 0) {
        return true;
    }

#if defined(EXTERNAL_EEPROM_WP_PIN)
    setPinOutput(EXTERNAL_EEPROM_WP_PIN);
    writePin(EXTERNAL_EEPROM_WP_PIN, 0);
#endif

    while (len > 0) {
        uintptr_t page_offset  = target_addr % EXTERNAL_EEPROM_PAGE_SIZE;
        size_t    write_length = EXTERNAL_EEPROM_PAGE_SIZE - page_offset;
        if (write_length > len) {
            write_length = len;
        }

        fill_target_address(complete_packet, (const void *)target_addr);
        for (size_t i = 0; i < write_length; i++) {
            complete_packet[EXTERNAL_EEPROM_ADDRESS_SIZE + i] = read_buf[i];
        }

        if (i2c_transmit(EXTERNAL_EEPROM_I2C_ADDRESS((uintptr_t)target_addr), complete_packet, EXTERNAL_EEPROM_ADDRESS_SIZE + write_length, EXTERNAL_EEPROM_I2C_TIMEOUT) != I2C_STATUS_SUCCESS) {
            /* Boot/migration callers are synchronous, but must not continue
             * issuing pages after the device has already rejected one. */
            ok = false;
            break;
        }
        wait_ms(EXTERNAL_EEPROM_WRITE_TIME);

        read_buf += write_length;
        target_addr += write_length;
        len -= write_length;
    }

#if defined(EXTERNAL_EEPROM_WP_PIN)
    /* We are setting the WP pin to high in a way that requires at least two bit-flips to change back to 0 */
    writePin(EXTERNAL_EEPROM_WP_PIN, 1);
    setPinInputHigh(EXTERNAL_EEPROM_WP_PIN);
#endif

    return ok;
}

static bool he_eeprom_time_reached(uint32_t now, uint32_t deadline) {
    return (int32_t)(now - deadline) >= 0;
}

#if defined(EXTERNAL_EEPROM_WP_PIN)
static void he_eeprom_async_wp_enable(void) {
    setPinOutput(EXTERNAL_EEPROM_WP_PIN);
    writePin(EXTERNAL_EEPROM_WP_PIN, 0);
}

static void he_eeprom_async_wp_disable(void) {
    /* We are setting the WP pin to high in a way that requires at least two
     * bit-flips to change back to 0. */
    writePin(EXTERNAL_EEPROM_WP_PIN, 1);
    setPinInputHigh(EXTERNAL_EEPROM_WP_PIN);
}
#endif

void he_eeprom_async_write_start(he_eeprom_async_write_t *write, const void *buf, const void *addr, size_t len) {
    memset(write, 0, sizeof(*write));
    uintptr_t target_addr = (uintptr_t)addr;
    if (target_addr > EXTERNAL_EEPROM_BYTE_COUNT || len > EXTERNAL_EEPROM_BYTE_COUNT - target_addr) {
        write->status = HE_EEPROM_ASYNC_FAILED;
        return;
    }
    write->data      = (const uint8_t *)buf;
    write->address   = target_addr;
    write->remaining = len;
    write->status    = len == 0 ? HE_EEPROM_ASYNC_COMPLETE : HE_EEPROM_ASYNC_IDLE;
}

static void he_eeprom_async_prepare_page(he_eeprom_async_write_t *write) {
    uintptr_t page_offset = write->address % EXTERNAL_EEPROM_PAGE_SIZE;
    size_t    page_length = EXTERNAL_EEPROM_PAGE_SIZE - page_offset;
    if (page_length > write->remaining) page_length = write->remaining;

    fill_target_address(write->packet, (const void *)write->address);
    memcpy(write->packet + EXTERNAL_EEPROM_ADDRESS_SIZE, write->data, page_length);
    write->page_address = write->address;
    write->page_length  = (uint8_t)page_length;
}

he_eeprom_async_status_t he_eeprom_async_write_step(he_eeprom_async_write_t *write, uint32_t now) {
    if (write->status == HE_EEPROM_ASYNC_COMPLETE || write->status == HE_EEPROM_ASYNC_FAILED) return write->status;

    if (write->status == HE_EEPROM_ASYNC_WAITING) {
        if (!he_eeprom_time_reached(now, write->ready_at)) return write->status;

        /* ACK polling: if the device is still completing its internal cycle,
         * allow a bounded polling window (up to 6ms beyond ready_at) before
         * declaring failure. This avoids triggering the 5s backoff if the chip
         * takes its maximum 5ms programming time. */
        if (i2c_ping_address(EXTERNAL_EEPROM_I2C_ADDRESS(write->page_address), EXTERNAL_EEPROM_RUNTIME_I2C_TIMEOUT) != I2C_STATUS_SUCCESS) {
            if (!he_eeprom_time_reached(now, write->ready_at + 6)) {
                return write->status;
            }
            write->status = HE_EEPROM_ASYNC_FAILED;
            return write->status;
        }

        write->data += write->page_length;
        write->address += write->page_length;
        write->remaining -= write->page_length;
        write->status = write->remaining == 0 ? HE_EEPROM_ASYNC_COMPLETE : HE_EEPROM_ASYNC_IDLE;
        return write->status;
    }

    he_eeprom_async_prepare_page(write);
#if defined(EXTERNAL_EEPROM_WP_PIN)
    he_eeprom_async_wp_enable();
#endif
    if (i2c_transmit(EXTERNAL_EEPROM_I2C_ADDRESS(write->page_address), write->packet, EXTERNAL_EEPROM_ADDRESS_SIZE + write->page_length, EXTERNAL_EEPROM_RUNTIME_I2C_TIMEOUT) != I2C_STATUS_SUCCESS) {
#if defined(EXTERNAL_EEPROM_WP_PIN)
        he_eeprom_async_wp_disable();
#endif
        write->status = HE_EEPROM_ASYNC_FAILED;
        return write->status;
    }
#if defined(EXTERNAL_EEPROM_WP_PIN)
    he_eeprom_async_wp_disable();
#endif

    write->ready_at = now + EXTERNAL_EEPROM_WRITE_TIME;
    write->status   = HE_EEPROM_ASYNC_WAITING;
    return write->status;
}

void he_eeprom_async_write_retry(he_eeprom_async_write_t *write) {
    if (write->status == HE_EEPROM_ASYNC_FAILED) write->status = HE_EEPROM_ASYNC_IDLE;
}

enum {
    HE_EEPROM_CAL_PHASE_INVALIDATE,
    HE_EEPROM_CAL_PHASE_PAYLOAD,
    HE_EEPROM_CAL_PHASE_FINAL_MARKER,
    HE_EEPROM_CAL_PHASE_COMPLETE,
};

static void he_eeprom_cal_start_phase(he_eeprom_cal_save_t *save) {
    switch (save->phase) {
        case HE_EEPROM_CAL_PHASE_INVALIDATE:
            he_eeprom_async_write_start(&save->write, &save->invalid_marker, (const void *)save->marker_address, 1);
            break;
        case HE_EEPROM_CAL_PHASE_PAYLOAD:
            he_eeprom_async_write_start(&save->write, save->payload, (const void *)save->payload_address, save->payload_length);
            break;
        case HE_EEPROM_CAL_PHASE_FINAL_MARKER:
            he_eeprom_async_write_start(&save->write, &save->final_marker, (const void *)save->marker_address, 1);
            break;
        default:
            break;
    }
}

void he_eeprom_cal_save_begin(he_eeprom_cal_save_t *save, const void *payload, size_t payload_length, const void *payload_address, const void *marker_address, bool invalidate_first, bool write_final_marker, uint8_t invalid_marker, uint8_t final_marker) {
    memset(save, 0, sizeof(*save));
    save->payload            = (const uint8_t *)payload;
    save->payload_length     = payload_length;
    save->payload_address    = (uintptr_t)payload_address;
    save->marker_address     = (uintptr_t)marker_address;
    save->invalid_marker     = invalid_marker;
    save->final_marker       = final_marker;
    save->invalidate_first   = invalidate_first;
    save->write_final_marker = write_final_marker;
    save->phase              = invalidate_first ? HE_EEPROM_CAL_PHASE_INVALIDATE : (payload_length ? HE_EEPROM_CAL_PHASE_PAYLOAD : (write_final_marker ? HE_EEPROM_CAL_PHASE_FINAL_MARKER : HE_EEPROM_CAL_PHASE_COMPLETE));
    save->status             = save->phase == HE_EEPROM_CAL_PHASE_COMPLETE ? HE_EEPROM_CAL_SAVE_COMPLETE : HE_EEPROM_CAL_SAVE_IN_PROGRESS;
    if (save->phase != HE_EEPROM_CAL_PHASE_COMPLETE) {
        he_eeprom_cal_start_phase(save);
    }
}

he_eeprom_cal_save_status_t he_eeprom_cal_save_step(he_eeprom_cal_save_t *save, bool permitted, uint32_t now) {
    if (save->status == HE_EEPROM_CAL_SAVE_COMPLETE || save->status == HE_EEPROM_CAL_SAVE_FAILED) return save->status;
    if (!permitted) return save->status;

    he_eeprom_async_status_t status = he_eeprom_async_write_step(&save->write, now);
    if (status == HE_EEPROM_ASYNC_FAILED) {
        save->status = HE_EEPROM_CAL_SAVE_FAILED;
        return save->status;
    }
    if (status != HE_EEPROM_ASYNC_COMPLETE) return save->status;

    if (save->phase == HE_EEPROM_CAL_PHASE_INVALIDATE) {
        save->phase = save->payload_length ? HE_EEPROM_CAL_PHASE_PAYLOAD : (save->write_final_marker ? HE_EEPROM_CAL_PHASE_FINAL_MARKER : HE_EEPROM_CAL_PHASE_COMPLETE);
    } else if (save->phase == HE_EEPROM_CAL_PHASE_PAYLOAD) {
        save->phase = save->write_final_marker ? HE_EEPROM_CAL_PHASE_FINAL_MARKER : HE_EEPROM_CAL_PHASE_COMPLETE;
    } else {
        save->phase = HE_EEPROM_CAL_PHASE_COMPLETE;
    }

    if (save->phase == HE_EEPROM_CAL_PHASE_COMPLETE) {
        save->status = HE_EEPROM_CAL_SAVE_COMPLETE;
    } else {
        /* The next phase starts on a later scheduler turn, keeping one I2C
         * transaction as the upper bound for every invocation. */
        he_eeprom_cal_start_phase(save);
    }
    return save->status;
}

void he_eeprom_cal_save_retry(he_eeprom_cal_save_t *save) {
    if (save->status == HE_EEPROM_CAL_SAVE_FAILED) {
        he_eeprom_async_write_retry(&save->write);
        save->status = HE_EEPROM_CAL_SAVE_IN_PROGRESS;
    }
}
