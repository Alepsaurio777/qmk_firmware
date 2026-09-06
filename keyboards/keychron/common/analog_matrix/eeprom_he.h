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

#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifndef EXTERNAL_EEPROM_BYTE_COUNT
#    define EXTERNAL_EEPROM_BYTE_COUNT 8192
#endif
#define EXTERNAL_EEPROM_PAGE_SIZE 32
#define EXTERNAL_EEPROM_ADDRESS_SIZE 2
#define EXTERNAL_EEPROM_WRITE_TIME 5
#ifndef EXTERNAL_EEPROM_I2C_TIMEOUT
#    define EXTERNAL_EEPROM_I2C_TIMEOUT 100
#endif
#ifndef EXTERNAL_EEPROM_RUNTIME_I2C_TIMEOUT
#    define EXTERNAL_EEPROM_RUNTIME_I2C_TIMEOUT 5
#endif
#define EXTERNAL_EEPROM_I2C_BASE_ADDRESS 0b10100010

#ifndef EXTERNAL_EEPROM_I2C_ADDRESS
#    define EXTERNAL_EEPROM_I2C_ADDRESS(loc) (EXTERNAL_EEPROM_I2C_BASE_ADDRESS)
#endif

void he_eeprom_driver_init(void);
bool he_eeprom_driver_erase(void);
bool he_eeprom_read_block(void *buf, const void *addr, size_t len);
bool he_eeprom_write_block(const void *buf, void *addr, size_t len);

/*
 * Cooperative writes for runtime data.  A step performs at most one I2C
 * transaction.  The mandatory EEPROM programming time is represented by a
 * timestamp; callers must invoke the step again after the timestamp expires.
 * The payload pointer must remain stable until the transaction completes.
 */
typedef enum {
    HE_EEPROM_ASYNC_IDLE,
    HE_EEPROM_ASYNC_WAITING,
    HE_EEPROM_ASYNC_COMPLETE,
    HE_EEPROM_ASYNC_FAILED,
} he_eeprom_async_status_t;

typedef struct {
    const uint8_t *data;
    uintptr_t      address;
    size_t         remaining;
    uintptr_t      page_address;
    uint8_t        page_length;
    uint8_t        packet[EXTERNAL_EEPROM_ADDRESS_SIZE + EXTERNAL_EEPROM_PAGE_SIZE];
    uint32_t       ready_at;
    he_eeprom_async_status_t status;
} he_eeprom_async_write_t;

void                       he_eeprom_async_write_start(he_eeprom_async_write_t *write, const void *buf, const void *addr, size_t len);
he_eeprom_async_status_t   he_eeprom_async_write_step(he_eeprom_async_write_t *write, uint32_t now);
void                       he_eeprom_async_write_retry(he_eeprom_async_write_t *write);

/* Atomic marker -> payload -> marker transaction driven one page at a time. */
typedef enum {
    HE_EEPROM_CAL_SAVE_IDLE,
    HE_EEPROM_CAL_SAVE_IN_PROGRESS,
    HE_EEPROM_CAL_SAVE_COMPLETE,
    HE_EEPROM_CAL_SAVE_FAILED,
} he_eeprom_cal_save_status_t;

typedef struct {
    const uint8_t            *payload;
    size_t                    payload_length;
    uintptr_t                 payload_address;
    uintptr_t                 marker_address;
    uint8_t                   invalid_marker;
    uint8_t                   final_marker;
    bool                      invalidate_first;
    bool                      write_final_marker;
    uint8_t                   phase;
    he_eeprom_async_write_t   write;
    he_eeprom_cal_save_status_t status;
} he_eeprom_cal_save_t;

void                     he_eeprom_cal_save_begin(he_eeprom_cal_save_t *save, const void *payload, size_t payload_length, const void *payload_address, const void *marker_address, bool invalidate_first, bool write_final_marker, uint8_t invalid_marker, uint8_t final_marker);
he_eeprom_cal_save_status_t he_eeprom_cal_save_step(he_eeprom_cal_save_t *save, bool permitted, uint32_t now);
void                     he_eeprom_cal_save_retry(he_eeprom_cal_save_t *save);
