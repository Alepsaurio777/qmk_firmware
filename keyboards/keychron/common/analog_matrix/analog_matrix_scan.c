/* Copyright 2024 @ Keychron (https://www.keychron.com)
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

#include "stdint.h"
#include "hal.h"
#include "gpio.h"
#include "quantum.h"
#include "analog_matrix.h"
#include "scan_scheduler.h"
#include "debounce.h"
#ifdef LK_WIRELESS_ENABLE
#    include "lpm.h"
#endif

#ifndef HC164_DS
#    define HC164_DS B3
#endif
#ifndef HC164_CP
#    define HC164_CP B5
#endif
#ifndef HC164_MR
#    define HC164_MR D2
#endif
#ifndef SHIFTER_START_INDEX
#    define SHIFTER_START_INDEX 0
#endif

#define ADC_GRP_NUM_CHANNELS MATRIX_ROWS
#define ADC_GRP_BUF_DEPTH 1
#define UNUSED_DEPTH 0

#ifndef ANALOG_ADC_SAMPLE_TIME
#    define ANALOG_ADC_SAMPLE_TIME ADC_SAMPLE_56
#endif

#ifndef ANALOG_SELECT_SETTLE_US
#    define ANALOG_SELECT_SETTLE_US 30
#endif

#if ANALOG_DEBOUNCE_TIME < 1
#    error "ANALOG_DEBOUNCE_TIME must be >= 1; 0 underflows the analog debounce loop"
#endif

/* Pipelined scan: process the previous column's samples during the analog
 * settle window of the current column, instead of busy-waiting. Recovers up to
 * ANALOG_SELECT_SETTLE_US per column of otherwise dead CPU time. Only valid
 * with single-pass sampling (the multi-pass debounce loop re-reads the ADC). */
#ifndef ANALOG_SCAN_PIPELINE
#    define ANALOG_SCAN_PIPELINE 0
#endif

#if ANALOG_SCAN_PIPELINE && ANALOG_DEBOUNCE_TIME != 1
#    error "ANALOG_SCAN_PIPELINE requires ANALOG_DEBOUNCE_TIME == 1"
#endif

/* SOF-synchronized scan: delay the scan start until a fixed offset after the
 * USB Start-of-Frame so the scan (and the report it produces) lands just
 * before the next host poll. Turns the free-running scan<->poll phase lottery
 * (up to ~1 frame of extra latency, random per keystroke) into a constant.
 * The SOF timestamp (usb_sof_timing_last_cycles) is provided by usb_main.c
 * whenever this or USB_SOF_TIMING_PROBE is active, so no probe is required. */
#ifndef ANALOG_SCAN_SOF_SYNC
#    define ANALOG_SCAN_SOF_SYNC 0
#endif

/* Scan start offset after SOF. Keep small: the tail of the frame must fit the
 * scan itself plus QMK's change processing so the report is armed before the
 * next poll. */
#ifndef ANALOG_SCAN_SOF_START_OFFSET_US
#    define ANALOG_SCAN_SOF_START_OFFSET_US 10
#endif

/* Never spin longer than this waiting for the start offset; if we are further
 * out of phase, run free this frame and let the next one resynchronize. */
#ifndef ANALOG_SCAN_SOF_MAX_WAIT_US
#    define ANALOG_SCAN_SOF_MAX_WAIT_US 400
#endif

/* Strict mode keeps one scan per live SOF timestamp. It is opt-in so stock
 * keyboards retain the historical bounded-wait behavior above. */
#ifndef ANALOG_SCAN_SOF_STRICT_SYNC
#    define ANALOG_SCAN_SOF_STRICT_SYNC 0
#endif

/* RAM-only counters. No reporting, logging, or bus work is done in the scan. */
#ifndef ANALOG_SCAN_SOF_METRICS
#    define ANALOG_SCAN_SOF_METRICS 0
#endif

#ifndef ANALOG_SCAN_SOF_DEADLINE_SYNC
#    define ANALOG_SCAN_SOF_DEADLINE_SYNC 0
#endif
#ifndef ANALOG_SCAN_SOF_DEADLINE_GUARD_US
#    define ANALOG_SCAN_SOF_DEADLINE_GUARD_US 70
#endif
#ifndef ANALOG_SCAN_SOF_INITIAL_SCAN_US
#    define ANALOG_SCAN_SOF_INITIAL_SCAN_US 760
#endif
#ifndef ANALOG_SCAN_SOF_ESTIMATE_SHIFT
#    define ANALOG_SCAN_SOF_ESTIMATE_SHIFT 3
#endif
#ifndef ANALOG_SCAN_SOF_MIN_START_US
#    define ANALOG_SCAN_SOF_MIN_START_US 10
#endif
#ifndef ANALOG_SCAN_SOF_MAX_START_US
#    define ANALOG_SCAN_SOF_MAX_START_US 350
#endif
#ifndef ANALOG_SCAN_PING_PONG_ADC
#    define ANALOG_SCAN_PING_PONG_ADC 0
#endif

#if ANALOG_SCAN_SOF_STRICT_SYNC && ANALOG_SCAN_SOF_DEADLINE_SYNC
#    error "ANALOG_SCAN_SOF_STRICT_SYNC cannot be combined with the experimental deadline scheduler"
#endif

#ifndef HC164_DELAY_NOPS
#    define HC164_DELAY_NOPS 50
#endif

#ifndef HC164_RESET_DELAY_NOPS
#    define HC164_RESET_DELAY_NOPS 20
#endif

/* LAB-only optimization.  Keep the historical counted-loop delay in builds
 * that do not opt in, so changing the shared driver cannot silently alter the
 * already-validated tournament timing. */
#ifndef ANALOG_HC164_EXACT_DELAY
#    define ANALOG_HC164_EXACT_DELAY 0
#endif

extern matrix_row_t raw_matrix[MATRIX_ROWS];
extern matrix_row_t matrix[MATRIX_ROWS];
#if ANALOG_GAME_CONTROLLER_SUPPORT
extern matrix_row_t game_controller_matrix[MATRIX_ROWS];
#endif
extern matrix_row_t okmc_matrix[MATRIX_ROWS];
extern const matrix_row_t analog_matrix_mask[MATRIX_ROWS];
matrix_row_t        analog_raw_matrix[MATRIX_ROWS];
matrix_row_t        changed_matrix[MATRIX_ROWS];

pin_t        row_pins[MATRIX_ROWS] = MATRIX_ROW_PINS;
pin_t        col_pins[MATRIX_COLS] = MATRIX_COL_PINS;
matrix_row_t matrix_mask[MATRIX_ROWS];
matrix_row_t virtual_matrix[MATRIX_ROWS] = {0};
static bool  matrix_changed;

#if ANALOG_STARTUP_GUARD_ENABLE
/* The guard is activated from keyboard_post_init/keychron_common_init, after
 * bootmagic and matrix construction have completed. Keeping it inactive until
 * then avoids hiding bootmagic keys while still protecting the normal USB
 * startup window. */
static bool     startup_guard_active;
static bool     startup_ready = true;
static bool     startup_scan_valid;
static uint8_t  startup_stable_scans;
static uint32_t startup_started_at;
#endif

#if ANALOG_SCAN_PING_PONG_ADC
static adcsample_t samples[2][ADC_GRP_NUM_CHANNELS * ADC_GRP_BUF_DEPTH];
#else
static adcsample_t samples[ADC_GRP_NUM_CHANNELS * ADC_GRP_BUF_DEPTH];
#endif

void analog_matrix_startup_begin(void) {
#if ANALOG_STARTUP_GUARD_ENABLE
    startup_guard_active  = true;
    startup_ready         = false;
    startup_scan_valid    = true;
    startup_stable_scans  = 0;
    startup_started_at    = timer_read32();
#endif
}

bool analog_matrix_startup_quiet(void) {
#if ANALOG_STARTUP_GUARD_ENABLE
    return startup_guard_active && !startup_ready;
#else
    return false;
#endif
}

#if ANALOG_STARTUP_GUARD_ENABLE
static void analog_matrix_startup_note_scan(void) {
    if (!startup_guard_active || startup_ready) return;

    if (analog_matrix_calibrating() || !startup_scan_valid || timer_elapsed32(startup_started_at) < ANALOG_STARTUP_WARMUP_MS) {
        startup_stable_scans = 0;
        return;
    }

    if (++startup_stable_scans >= ANALOG_STARTUP_VALID_SCANS) {
        startup_ready = true;
    }
}
#endif

static void adcerrorcallback(ADCDriver *adcp, adcerror_t err) {
    dprintf("err\r\n");

    (void)adcp;
    (void)err;
}

// clang-format off
ADCConversionGroup adcgrpcfg = {
    FALSE,
    ADC_GRP_NUM_CHANNELS,
    NULL,
    adcerrorcallback,
    0,                                          /* CR1 */
    ADC_CR2_SWSTART,                            /* CR2 */
    0,                                          /* SMPR1 */
    0,                                          /* SMPR2 */
    0,                                          /* HTR */
    0,                                          /* LTR */
    0,                                          /* SQR1 */
    0,                                          /* SQR2 */
    0                                           /* SQR3 */
};
// clang-format on

uint8_t pinToAdcChn(pin_t pin) {
    switch (pin) {
        case A0:
            return ADC_CHANNEL_IN0;
        case A1:
            return ADC_CHANNEL_IN1;
        case A2:
            return ADC_CHANNEL_IN2;
        case A3:
            return ADC_CHANNEL_IN3;
        case A4:
            return ADC_CHANNEL_IN4;
        case A5:
            return ADC_CHANNEL_IN5;
        case A6:
            return ADC_CHANNEL_IN6;
        case A7:
            return ADC_CHANNEL_IN7;
        case B0:
            return ADC_CHANNEL_IN8;
        case B1:
            return ADC_CHANNEL_IN9;
        case C0:
            return ADC_CHANNEL_IN10;
        case C1:
            return ADC_CHANNEL_IN11;
        case C2:
            return ADC_CHANNEL_IN12;
        case C3:
            return ADC_CHANNEL_IN13;
        case C4:
            return ADC_CHANNEL_IN14;
        case C5:
            return ADC_CHANNEL_IN15;
    }

    return 0xFF;
}

#if ANALOG_HC164_EXACT_DELAY
/*
 * Fixed instruction-count delays for the LAB path.
 *
 * Do NOT use assembler `.rept` here. GCC cannot account for the expanded
 * byte length of a `.rept N` block while laying out Thumb branches during
 * LTO. With three `.rept 32` blocks inside HC164_output(), GCC selected a
 * short CBZ whose target became >126 bytes away after GNU as expanded the
 * directives, causing `Error: branch out of range`.
 *
 * Expanding the NOPs in the C preprocessor gives GCC one visible asm line
 * per instruction, so its branch-distance estimate matches the assembled
 * code. This preserves the existing no-call-overhead timing: 32 requested
 * NOP instructions still means 32 inline NOP instructions.
 */
#    define HC164_NOP_1  "nop\n\t"
#    define HC164_NOP_2  HC164_NOP_1 HC164_NOP_1
#    define HC164_NOP_4  HC164_NOP_2 HC164_NOP_2
#    define HC164_NOP_8  HC164_NOP_4 HC164_NOP_4
#    define HC164_NOP_12 HC164_NOP_8 HC164_NOP_4
#    define HC164_NOP_16 HC164_NOP_8 HC164_NOP_8
#    define HC164_NOP_20 HC164_NOP_16 HC164_NOP_4
#    define HC164_NOP_24 HC164_NOP_16 HC164_NOP_8
#    define HC164_NOP_28 HC164_NOP_16 HC164_NOP_8 HC164_NOP_4
#    define HC164_NOP_32 HC164_NOP_16 HC164_NOP_16
#    define HC164_NOP_40 HC164_NOP_32 HC164_NOP_8
#    define HC164_NOP_48 HC164_NOP_32 HC164_NOP_16
#    define HC164_NOP_50 HC164_NOP_48 HC164_NOP_2

#    if HC164_DELAY_NOPS == 8
#        define HC164_DATA_NOP_SEQUENCE HC164_NOP_8
#    elif HC164_DELAY_NOPS == 12
#        define HC164_DATA_NOP_SEQUENCE HC164_NOP_12
#    elif HC164_DELAY_NOPS == 16
#        define HC164_DATA_NOP_SEQUENCE HC164_NOP_16
#    elif HC164_DELAY_NOPS == 20
#        define HC164_DATA_NOP_SEQUENCE HC164_NOP_20
#    elif HC164_DELAY_NOPS == 24
#        define HC164_DATA_NOP_SEQUENCE HC164_NOP_24
#    elif HC164_DELAY_NOPS == 28
#        define HC164_DATA_NOP_SEQUENCE HC164_NOP_28
#    elif HC164_DELAY_NOPS == 32
#        define HC164_DATA_NOP_SEQUENCE HC164_NOP_32
#    elif HC164_DELAY_NOPS == 40
#        define HC164_DATA_NOP_SEQUENCE HC164_NOP_40
#    elif HC164_DELAY_NOPS == 50
#        define HC164_DATA_NOP_SEQUENCE HC164_NOP_50
#    else
#        error "ANALOG_HC164_EXACT_DELAY: unsupported HC164_DELAY_NOPS"
#    endif

#    if HC164_RESET_DELAY_NOPS == 8
#        define HC164_RESET_NOP_SEQUENCE HC164_NOP_8
#    elif HC164_RESET_DELAY_NOPS == 12
#        define HC164_RESET_NOP_SEQUENCE HC164_NOP_12
#    elif HC164_RESET_DELAY_NOPS == 16
#        define HC164_RESET_NOP_SEQUENCE HC164_NOP_16
#    elif HC164_RESET_DELAY_NOPS == 20
#        define HC164_RESET_NOP_SEQUENCE HC164_NOP_20
#    elif HC164_RESET_DELAY_NOPS == 24
#        define HC164_RESET_NOP_SEQUENCE HC164_NOP_24
#    elif HC164_RESET_DELAY_NOPS == 28
#        define HC164_RESET_NOP_SEQUENCE HC164_NOP_28
#    elif HC164_RESET_DELAY_NOPS == 32
#        define HC164_RESET_NOP_SEQUENCE HC164_NOP_32
#    elif HC164_RESET_DELAY_NOPS == 40
#        define HC164_RESET_NOP_SEQUENCE HC164_NOP_40
#    elif HC164_RESET_DELAY_NOPS == 50
#        define HC164_RESET_NOP_SEQUENCE HC164_NOP_50
#    else
#        error "ANALOG_HC164_EXACT_DELAY: unsupported HC164_RESET_DELAY_NOPS"
#    endif

static inline void shifter_data_delay_exact(void) {
    __asm__ volatile(HC164_DATA_NOP_SEQUENCE ::: "memory");
}

static inline void shifter_reset_delay_exact(void) {
    __asm__ volatile(HC164_RESET_NOP_SEQUENCE ::: "memory");
}
#else
/* Stable path: verbatim historical delay primitive. */
static inline void shifter_delay(uint16_t n) {
    while (n-- > 0) {
        asm volatile("nop" ::: "memory");
    }
}
#endif

static void HC164_output(uint16_t data, bool bit_flag) {
#if !ANALOG_HC164_EXACT_DELAY
    uint8_t n = HC164_DELAY_NOPS;
#endif

    ATOMIC_BLOCK_FORCEON {
        for (uint8_t i = 0; i < 15; i++) {
            if (data & 0x1) {
                writePinHigh(HC164_DS);
            } else {
                writePinLow(HC164_DS);
            }
#if ANALOG_HC164_EXACT_DELAY
            shifter_data_delay_exact();
#else
            shifter_delay(n);
#endif
            writePinHigh(HC164_CP);
#if ANALOG_HC164_EXACT_DELAY
            shifter_data_delay_exact();
#else
            shifter_delay(n);
#endif
            writePinLow(HC164_CP);
#if ANALOG_HC164_EXACT_DELAY
            shifter_data_delay_exact();
#else
            shifter_delay(n);
#endif
            if (bit_flag) {
                break;
            } else {
                data = data >> 1;
            }
        }
    }
}

static bool select_col(uint8_t col) {
    if (col == 0) {
        writePinLow(HC164_MR);
#if ANALOG_HC164_EXACT_DELAY
        shifter_reset_delay_exact();
#else
        shifter_delay(HC164_RESET_DELAY_NOPS);
#endif
        writePinHigh(HC164_MR);
#if ANALOG_HC164_EXACT_DELAY
        shifter_reset_delay_exact();
#else
        shifter_delay(HC164_RESET_DELAY_NOPS);
#endif
        HC164_output(0x01, true);
#if (SHIFTER_START_INDEX != 0)
        for (uint8_t i = 0; i < SHIFTER_START_INDEX; i++) {
            HC164_output(0x00, true);
        }
#endif
    }
    return true;
}

static void unselect_col(uint8_t col) {
    HC164_output(0x00, true);
    return;
}

void        select_all_cols(void) {}
static void unselect_cols(void) {}

// (1-ago) UNA sola copia del procesado de una tecla. Antes esta secuencia estaba
// duplicada literalmente en matrix_read_rows_on_col() y en process_col_samples()
// —comentario de "ORDEN LOAD-BEARING" incluido— y un arreglo aplicado a una copia
// y no a la otra era invisible hasta que alguien tocaba ANALOG_SCAN_PIPELINE.
//
// El ORDEN de la cadena es load-bearing y por eso vive aqui, en un solo sitio:
// F9 (minimo-ON) ANTES de F6 (minimo-OFF). Al reves, F6 veria el release FISICO,
// abriria su ventana OFF en t=0 y pelearia contra el ON que F9 esta sosteniendo.
// Encadenados en este orden se apilan, que es lo correcto: el OFF tiene que
// verse DESPUES de que el ON se viera, porque son dos muestreos de tick
// distintos.
static inline bool analog_process_key_sample(uint8_t row, uint8_t col, uint16_t sample) {
    update_raw_value(row, col, sample);

    const bool physical = analog_matrix_get_key_state(row, col);
    return analog_matrix_stretch_apply(row, col, physical);
}

void matrix_read_rows_on_col(uint8_t current_col, matrix_row_t row_shifter) {
    // Select col
    if (!select_col(current_col)) {
        return; // skip NO_PIN col
    }

    wait_us(ANALOG_SELECT_SETTLE_US);

    uint8_t debounce_times = ANALOG_DEBOUNCE_TIME;
    uint8_t row_value     = 0;
    bool    changed       = false;

    do {
        // Bail out on ADC failure instead of processing stale samples from the
        // previous column (samples[] is static). Leaving the column untouched
        // avoids phantom key state changes caused by EMI/bus faults.
#if ANALOG_SCAN_PING_PONG_ADC
        if (adcConvert(&ADCD1, &adcgrpcfg, samples[0], ADC_GRP_BUF_DEPTH) != MSG_OK) {
#else
        if (adcConvert(&ADCD1, &adcgrpcfg, samples, ADC_GRP_BUF_DEPTH) != MSG_OK) {
#endif
#if ANALOG_STARTUP_GUARD_ENABLE
            startup_scan_valid = false;
#endif
            changed = false;
            break;
        }

        uint8_t row_value_recheck = 0;
        matrix_row_t row_mask = 0x01 << current_col;
        for (uint8_t row_index = 0; row_index < MATRIX_ROWS; row_index++) {
            if ((analog_matrix_mask[row_index] & row_mask) == 0) continue;

#if ANALOG_SCAN_PING_PONG_ADC
            const bool pressed = analog_process_key_sample(row_index, current_col, samples[0][row_index]);
#else
            const bool pressed = analog_process_key_sample(row_index, current_col, samples[row_index]);
#endif
            if (pressed) {
                if ((analog_raw_matrix[row_index] & row_mask) == 0) changed = true;

                if (debounce_times == ANALOG_DEBOUNCE_TIME) {
                    row_value |= (0x01 << row_index);
                } else {
                    row_value_recheck |= (0x01 << row_index);
                }
            } else if (analog_raw_matrix[row_index] & row_mask) {
                changed = true;
            }
        }

        if (debounce_times != ANALOG_DEBOUNCE_TIME && row_value != row_value_recheck) {
            // Clear state when bounce occurs
            changed = false;
        }

    } while (--debounce_times && changed);

    if (changed) {
        matrix_changed = true;
        for (uint8_t row_index = 0; row_index < MATRIX_ROWS; row_index++) {
            if (row_value & (0x01 << row_index)) {
                if ((analog_raw_matrix[row_index] & row_shifter) == 0) changed_matrix[row_index] |= row_shifter; // Mark changed matrix position
                analog_raw_matrix[row_index] |= row_shifter;                                                     // Update matrix
            } else {
                if ((analog_raw_matrix[row_index] & row_shifter)) changed_matrix[row_index] |= row_shifter;
                analog_raw_matrix[row_index] &= ~row_shifter;
            }
        }
    }

    // Unselect col
    unselect_col(current_col);
}

#if ANALOG_SCAN_PIPELINE
// Identico al procesado por-fila de matrix_read_rows_on_col con una sola
// pasada (ANALOG_DEBOUNCE_TIME == 1): la rama de recheck del debounce es
// inalcanzable y se omite.
static void process_col_samples(uint8_t col, matrix_row_t row_shifter, const adcsample_t *smp) {
    uint8_t row_value = 0;
    bool    changed   = false;

    for (uint8_t row_index = 0; row_index < MATRIX_ROWS; row_index++) {
        if ((analog_matrix_mask[row_index] & row_shifter) == 0) continue;

        const bool pressed = analog_process_key_sample(row_index, col, smp[row_index]);
        if (pressed) {
            if ((analog_raw_matrix[row_index] & row_shifter) == 0) changed = true;
            row_value |= (0x01 << row_index);
        } else if (analog_raw_matrix[row_index] & row_shifter) {
            changed = true;
        }
    }

    if (changed) {
        matrix_changed = true;
        for (uint8_t row_index = 0; row_index < MATRIX_ROWS; row_index++) {
            if (row_value & (0x01 << row_index)) {
                if ((analog_raw_matrix[row_index] & row_shifter) == 0) changed_matrix[row_index] |= row_shifter;
                analog_raw_matrix[row_index] |= row_shifter;
            } else {
                if ((analog_raw_matrix[row_index] & row_shifter)) changed_matrix[row_index] |= row_shifter;
                analog_raw_matrix[row_index] &= ~row_shifter;
            }
        }
    }
}
#endif

void matrix_init_custom(void) {
    uint32_t smpr[2] = {0, 0};
    uint32_t sqr[3]  = {0, 0, 0};
    uint8_t  chn;
    uint8_t  chn_cnt = 0;

#ifdef ANALOG_MATRIX_POWER_PIN
    setPinOutput(ANALOG_MATRIX_POWER_PIN);
    writePin(ANALOG_MATRIX_POWER_PIN, ANALOG_MATRIX_POWER_ENABLE_LEVEL);
#endif
#ifdef ANALOG_MATRIX_WAKEUP_PIN
    setPinInputHigh(ANALOG_MATRIX_WAKEUP_PIN);
#endif
#ifdef ENCODER_SWITCH_PIN
    setPinInputHigh(ENCODER_SWITCH_PIN);
#endif

    // Init shift register control pins
    setPinOutput(HC164_DS);
    setPinOutput(HC164_CP);
    setPinOutput(HC164_MR);
    writePinLow(HC164_MR);

    for (uint8_t x = 0; x < MATRIX_ROWS; x++) {
        if (row_pins[x] != NO_PIN) {
            palSetLineMode(row_pins[x], PAL_MODE_INPUT_ANALOG);
            palWriteLine(row_pins[x], 0);
        }

        chn = pinToAdcChn(row_pins[x]);
        if (chn < 0xFF) {
            if (chn > 9)
                smpr[0] |= ANALOG_ADC_SAMPLE_TIME << ((chn - 10) * 3);
            else
                smpr[1] |= ANALOG_ADC_SAMPLE_TIME << (chn * 3);

            sqr[chn_cnt / 6] |= chn << ((chn_cnt % 6) * 5);
            chn_cnt++;
        }
    }

    adcgrpcfg.smpr1 = smpr[0];
    adcgrpcfg.smpr2 = smpr[1];

    adcgrpcfg.sqr3 = sqr[0];
    adcgrpcfg.sqr2 = sqr[1];
    adcgrpcfg.sqr1 = sqr[2];

    unselect_cols();
    adcStart(&ADCD1, NULL);

    // Refer to STM32 AN4073 Option 2
    SYSCFG->PMC |= SYSCFG_PMC_ADC1DC2;

    // Value of initial ADC seems abnormal, scan to skip/drop i
    for (uint8_t i = 0; i < 5; i++)
        for (uint8_t current_col = 0; current_col < MATRIX_COLS; current_col++) {
            matrix_read_rows_on_col(current_col, 0);
        }

    for (uint8_t i = 0; i < MATRIX_ROWS; i++) {
        analog_raw_matrix[i] = 0;
        changed_matrix[i]    = 0;
    }

    analog_matrix_init();
}

#if defined(USB_SOF_TIMING_PROBE)
// Fase 3 (instrumentacion): medir duracion del barrido completo y su fase
// respecto al ultimo SOF USB. Solo lecturas del contador de ciclos; no anade
// trabajo apreciable al scan.
extern volatile uint32_t usb_sof_timing_last_cycles;
volatile uint32_t        scan_probe_count       = 0;
volatile uint16_t        scan_probe_duration_us = 0;
volatile uint16_t        scan_probe_phase_us    = 0;
// min/max acumulados para ver el jitter (leidos por Raw HID 0xA9 0x51).
volatile uint16_t        scan_probe_duration_min_us = 0xFFFF;
volatile uint16_t        scan_probe_duration_max_us = 0;
volatile uint16_t        scan_probe_phase_min_us    = 0xFFFF;
volatile uint16_t        scan_probe_phase_max_us    = 0;
#    define SCAN_PROBE_CYC_PER_US (STM32_SYSCLK / 1000000)
#endif

#if ANALOG_SCAN_SOF_METRICS
extern volatile uint32_t usb_sof_timing_last_cycles;
volatile uint32_t analog_scan_sof_sync_skip_count  = 0;
volatile uint32_t analog_scan_sof_sync_miss_count  = 0;
volatile uint32_t analog_scan_sof_sync_wait_us     = 0;
volatile uint32_t analog_scan_sof_duration_us      = 0;
volatile uint32_t analog_scan_sof_duration_max_us = 0;
#    define ANALOG_SCAN_METRICS_CYC_PER_US (STM32_SYSCLK / 1000000)
#endif

#if ANALOG_SCAN_SOF_SYNC && ANALOG_SCAN_SOF_STRICT_SYNC
static analog_scan_sof_state_t analog_scan_sof_state;
#endif

bool matrix_scan_custom(matrix_row_t current_matrix[]) {
    matrix_row_t last_raw_matrix[MATRIX_ROWS];

#if ANALOG_STARTUP_GUARD_ENABLE
    startup_scan_valid = true;
#endif

#if ANALOG_SCAN_SOF_METRICS
    uint32_t sync_wait_cycles = 0;
#endif

#if ANALOG_SCAN_SOF_SYNC
#    if ANALOG_SCAN_SOF_STRICT_SYNC
    {
        extern volatile uint32_t usb_sof_timing_last_cycles;
        const uint32_t cyc_per_us = STM32_SYSCLK / 1000000;
        const uint32_t frame_cyc  = 1000 * cyc_per_us;
        const analog_scan_sof_config_t config = {
            .frame_ticks               = frame_cyc,
            .start_offset_ticks        = (uint32_t)ANALOG_SCAN_SOF_START_OFFSET_US * cyc_per_us,
            .max_wait_ticks            = (uint32_t)ANALOG_SCAN_SOF_MAX_WAIT_US * cyc_per_us,
            .stale_after_ticks         = 2 * frame_cyc,
            .next_frame_max_wait_ticks = frame_cyc + (uint32_t)ANALOG_SCAN_SOF_MAX_WAIT_US * cyc_per_us,
        };
        analog_scan_sof_plan_t plan;
        uint32_t               sof_ticks;

        /* Re-plan after every bounded wait. This makes the normal fast-scan
         * case phase-stable, while a stale timestamp eventually falls through
         * to a free scan without an unbounded wait. */
        for (;;) {
            sof_ticks                 = usb_sof_timing_last_cycles;
            const uint32_t now_ticks = chSysGetRealtimeCounterX();
            plan                     = analog_scan_sof_plan(&analog_scan_sof_state, now_ticks, sof_ticks, &config);
            if (plan.action != ANALOG_SCAN_SOF_WAIT) break;

#if ANALOG_SCAN_SOF_METRICS
            const uint32_t wait_start = now_ticks;
#endif
            while ((int32_t)(chSysGetRealtimeCounterX() - plan.target_ticks) < 0) {
            }
#if ANALOG_SCAN_SOF_METRICS
            sync_wait_cycles += chSysGetRealtimeCounterX() - wait_start;
#endif
        }

        analog_scan_sof_commit(&analog_scan_sof_state, sof_ticks);

#if ANALOG_SCAN_SOF_METRICS
        if (plan.skip_sync) analog_scan_sof_sync_skip_count++;
        if (plan.missed_deadline) analog_scan_sof_sync_miss_count++;
#endif
    }
#    else
#        if ANALOG_SCAN_SOF_DEADLINE_SYNC
    static uint32_t scan_estimate_cyc = (uint32_t)ANALOG_SCAN_SOF_INITIAL_SCAN_US * (STM32_SYSCLK / 1000000);
#        endif
    {
        extern volatile uint32_t usb_sof_timing_last_cycles;
        const uint32_t cyc_per_us = STM32_SYSCLK / 1000000;
        const uint32_t frame_cyc  = 1000 * cyc_per_us;
#        if ANALOG_SCAN_SOF_DEADLINE_SYNC
        const uint32_t guard_cyc  = (uint32_t)ANALOG_SCAN_SOF_DEADLINE_GUARD_US * cyc_per_us;
        uint32_t desired_us;
        if (scan_estimate_cyc + guard_cyc >= frame_cyc) {
            desired_us = ANALOG_SCAN_SOF_MIN_START_US;
        } else {
            desired_us = (frame_cyc - scan_estimate_cyc - guard_cyc) / cyc_per_us;
            if (desired_us < ANALOG_SCAN_SOF_MIN_START_US) desired_us = ANALOG_SCAN_SOF_MIN_START_US;
            if (desired_us > ANALOG_SCAN_SOF_MAX_START_US) desired_us = ANALOG_SCAN_SOF_MAX_START_US;
        }
        const uint32_t offset_cyc = desired_us * cyc_per_us;
#        else
        const uint32_t offset_cyc = ANALOG_SCAN_SOF_START_OFFSET_US * cyc_per_us;
#        endif

        uint32_t sof   = usb_sof_timing_last_cycles;
        uint32_t now   = chSysGetRealtimeCounterX();
        uint32_t since = now - sof;

        // Solo sincronizar con SOF vivo (bus activo); en suspension/arranque
        // el barrido corre libre y no hay riesgo de deadlock.
        if (since < 2 * frame_cyc) {
            uint32_t phase = since % frame_cyc;
            uint32_t wait  = (phase <= offset_cyc) ? (offset_cyc - phase) : (frame_cyc - phase + offset_cyc);

#if ANALOG_SCAN_SOF_METRICS
            if (phase > offset_cyc) analog_scan_sof_sync_miss_count++;
#endif
            if (wait <= (uint32_t)ANALOG_SCAN_SOF_MAX_WAIT_US * cyc_per_us) {
#if ANALOG_SCAN_SOF_METRICS
                const uint32_t wait_start = now;
#endif
                uint32_t target = now + wait;
                while ((int32_t)(chSysGetRealtimeCounterX() - target) < 0) {
                }
#if ANALOG_SCAN_SOF_METRICS
                sync_wait_cycles += chSysGetRealtimeCounterX() - wait_start;
#endif
#if ANALOG_SCAN_SOF_METRICS
            } else {
                analog_scan_sof_sync_skip_count++;
#endif
            }
#if ANALOG_SCAN_SOF_METRICS
        } else {
            analog_scan_sof_sync_skip_count++;
#endif
        }
    }
#    endif
#endif

#if ANALOG_SCAN_SOF_SYNC && !ANALOG_SCAN_SOF_STRICT_SYNC && ANALOG_SCAN_SOF_DEADLINE_SYNC
    const uint32_t scan_runtime_start = chSysGetRealtimeCounterX();
#endif

#if ANALOG_SCAN_SOF_METRICS
    const uint32_t scan_metrics_start = chSysGetRealtimeCounterX();
#endif

#if defined(USB_SOF_TIMING_PROBE)
    uint32_t probe_start = chSysGetRealtimeCounterX();
#endif

    memcpy(last_raw_matrix, raw_matrix, sizeof(raw_matrix));
    memcpy(virtual_matrix, matrix, sizeof(matrix));
    memset(changed_matrix, 0, sizeof(changed_matrix));
    matrix_changed = false;

    // Set col, read rows
#if ANALOG_SCAN_PIPELINE
    {
#if ANALOG_SCAN_PING_PONG_ADC
        const adcsample_t *pend        = NULL;
        uint8_t            fill_buffer = 0;
#else
        static adcsample_t pend[ADC_GRP_NUM_CHANNELS];
#endif
        bool               pend_valid   = false;
        uint8_t            pend_col     = 0;
        matrix_row_t       pend_shifter = 0;
        const uint32_t     settle_cyc   = (uint32_t)ANALOG_SELECT_SETTLE_US * (STM32_SYSCLK / 1000000);

        matrix_row_t row_shifter = MATRIX_ROW_SHIFTER;
        for (uint8_t current_col = 0; current_col < MATRIX_COLS; current_col++, row_shifter <<= 1) {
            // La columna queda seleccionada al avanzar el shift register (en
            // unselect_col de la iteracion previa; col 0 en select_col).
            select_col(current_col);
            uint32_t settle_t0 = chSysGetRealtimeCounterX();

            // Trabajo util durante la ventana de settle analogico.
            if (pend_valid) process_col_samples(pend_col, pend_shifter, pend);

            while ((uint32_t)(chSysGetRealtimeCounterX() - settle_t0) < settle_cyc) {
            }

#if ANALOG_SCAN_PING_PONG_ADC
            adcsample_t *fill = samples[fill_buffer];
            if (adcConvert(&ADCD1, &adcgrpcfg, fill, ADC_GRP_BUF_DEPTH) == MSG_OK) {
                pend        = fill;
                fill_buffer ^= 1;
#else
            if (adcConvert(&ADCD1, &adcgrpcfg, samples, ADC_GRP_BUF_DEPTH) == MSG_OK) {
                memcpy(pend, samples, sizeof(pend));
#endif
                pend_col     = current_col;
                pend_shifter = row_shifter;
                pend_valid   = true;
            } else {
                // Igual que el camino clasico: columna sin procesar ante fallo
                // de ADC, se conserva el estado previo.
#if ANALOG_STARTUP_GUARD_ENABLE
                startup_scan_valid = false;
#endif
                pend_valid = false;
            }

            unselect_col(current_col);
        }
        if (pend_valid) process_col_samples(pend_col, pend_shifter, pend);
    }
#else
    matrix_row_t row_shifter = MATRIX_ROW_SHIFTER;
    for (uint8_t current_col = 0; current_col < MATRIX_COLS; current_col++, row_shifter <<= 1) {
        matrix_read_rows_on_col(current_col, row_shifter);
    }
#endif

    analog_matrix_scan_task();
    for (uint8_t row = 0; row < MATRIX_ROWS; row++) {
        raw_matrix[row] &= analog_matrix_mask[row];
    }

#if defined(ENCODER_MATRIX_ROW) && defined(ENCODER_MATROX_COL)
    if (readPin(ENCODER_SWITCH_PIN) == 0) {
        if ((raw_matrix[ENCODER_MATRIX_ROW] & (1 << ENCODER_MATROX_COL)) == 0) {
            matrix_changed = true;
            raw_matrix[ENCODER_MATRIX_ROW] |= (1 << ENCODER_MATROX_COL);
        }
    } else {
        if ((raw_matrix[ENCODER_MATRIX_ROW] & (1 << ENCODER_MATROX_COL))) {
            matrix_changed = true;
            raw_matrix[ENCODER_MATRIX_ROW] &= ~(1 << ENCODER_MATROX_COL);
        }
    }
#endif

    for (uint8_t row = 0; row < MATRIX_ROWS; row++) {
#if ANALOG_GAME_CONTROLLER_SUPPORT
        virtual_matrix[row] |= (game_controller_matrix[row] | okmc_matrix[row]);
#else
        virtual_matrix[row] |= okmc_matrix[row];
#endif
    }

#if ANALOG_STARTUP_GUARD_ENABLE
    const bool was_startup_quiet = analog_matrix_startup_quiet();
    analog_matrix_startup_note_scan();
    if (was_startup_quiet || analog_matrix_startup_quiet()) {
        /* Keep the HID device responsive during warm-up, but expose a fully
         * released matrix until calibration and a run of valid scans agree.
         * This also clears virtual outputs generated by a transient sample. If
         * this scan is the one that completes the guard, keep it quiet too so
         * no stale edge can leak through the transition frame. */
        memset(raw_matrix, 0, sizeof(raw_matrix));
        memset(virtual_matrix, 0, sizeof(virtual_matrix));
        memset(changed_matrix, 0, sizeof(changed_matrix));
        matrix_changed = false;
    }
#endif

    bool changed = memcmp(raw_matrix, last_raw_matrix, sizeof(last_raw_matrix)) != 0;
    // changed = debounce(raw_matrix, matrix, MATRIX_ROWS, changed);

#if defined(USB_SOF_TIMING_PROBE)
    {
        uint32_t probe_end = chSysGetRealtimeCounterX();
        uint32_t dur_us    = (probe_end - probe_start) / SCAN_PROBE_CYC_PER_US;
        uint32_t phase_us  = (probe_end - usb_sof_timing_last_cycles) / SCAN_PROBE_CYC_PER_US;

        scan_probe_duration_us = dur_us > UINT16_MAX ? UINT16_MAX : (uint16_t)dur_us;
        scan_probe_phase_us    = phase_us > 9999 ? 9999 : (uint16_t)phase_us;
        if (scan_probe_duration_us < scan_probe_duration_min_us) scan_probe_duration_min_us = scan_probe_duration_us;
        if (scan_probe_duration_us > scan_probe_duration_max_us) scan_probe_duration_max_us = scan_probe_duration_us;
        if (scan_probe_phase_us < scan_probe_phase_min_us) scan_probe_phase_min_us = scan_probe_phase_us;
        if (scan_probe_phase_us > scan_probe_phase_max_us) scan_probe_phase_max_us = scan_probe_phase_us;
        scan_probe_count++;
    }
#endif

#if ANALOG_SCAN_SOF_METRICS
    {
        const uint32_t metrics_end = chSysGetRealtimeCounterX();
        const uint32_t wait_us     = sync_wait_cycles / ANALOG_SCAN_METRICS_CYC_PER_US;
        const uint32_t duration_us = (metrics_end - scan_metrics_start) / ANALOG_SCAN_METRICS_CYC_PER_US;

        analog_scan_sof_sync_wait_us += wait_us;
        analog_scan_sof_duration_us = duration_us;
        if (duration_us > analog_scan_sof_duration_max_us) analog_scan_sof_duration_max_us = duration_us;
    }
#endif

#if ANALOG_SCAN_SOF_SYNC && !ANALOG_SCAN_SOF_STRICT_SYNC && ANALOG_SCAN_SOF_DEADLINE_SYNC
    {
        const uint32_t cyc_per_us = STM32_SYSCLK / 1000000;
        const uint32_t min_cyc    = 300 * cyc_per_us;
        const uint32_t max_cyc    = 980 * cyc_per_us;
        uint32_t       actual     = chSysGetRealtimeCounterX() - scan_runtime_start;
        if (actual < min_cyc) actual = min_cyc;
        if (actual > max_cyc) actual = max_cyc;

        // EMA signed: converge sin jitter de offset frame-a-frame. SHIFT=3
        // equivale a 1/8 de correccion por scan (~8 ms hasta asentarse).
        int32_t error = (int32_t)actual - (int32_t)scan_estimate_cyc;
        scan_estimate_cyc = (uint32_t)((int32_t)scan_estimate_cyc + (error >> ANALOG_SCAN_SOF_ESTIMATE_SHIFT));
    }
#endif

    return matrix_changed | changed;
}

void matrix_enter_low_power(void) {
    adcStop(&ADCD1);

#ifdef HC164_DS
    setPinInputLow(HC164_DS);
#endif
#ifdef HC164_CP
    setPinInputLow(HC164_CP);
#endif
#ifdef HC164_MR
    setPinInputLow(HC164_MR);
#endif

#ifdef ANALOG_MATRIX_POWER_PIN
    writePin(ANALOG_MATRIX_POWER_PIN, !ANALOG_MATRIX_POWER_ENABLE_LEVEL);
#endif

#ifdef ANALOG_MATRIX_WAKEUP_PIN
    palEnableLineEvent(ANALOG_MATRIX_WAKEUP_PIN, PAL_EVENT_MODE_FALLING_EDGE);
#endif

    // Set all row to input low
    pin_t pins_row[MATRIX_ROWS] = MATRIX_ROW_PINS;
    for (uint8_t x = 0; x < MATRIX_ROWS; x++) {
        if (pins_row[x] != NO_PIN) {
            setPinInputLow(pins_row[x]);
        }
    }
}
