/* Copyright 2026 @ Keychron (https://www.keychron.com)
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 2 of the License, or
 * (at your option) any later version.
 */

#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/*
 * Small, clock-source-independent policy helper for SOF anchored scans.
 *
 * All arithmetic is unsigned except for the final target comparison. The
 * caller must keep frame_ticks, stale_after_ticks and all waits below 2^31;
 * that is true for the USB/ChibiOS realtime counter and makes the decisions
 * valid across a 32-bit wrap.
 *
 * A state consumes one scan per SOF timestamp. If the scan returns before the
 * next SOF, the WAIT result targets the next frame's offset. That wait is
 * deliberately allowed to span one frame: it is the bounded wait needed to
 * preserve phase after a fast scan. Once the target is reached, the caller
 * calls the helper again, so a disconnected or suspended USB bus cannot make
 * the scan spin forever.
 *
 * Tradeoff: strict mode can spend the remaining part of a frame in the
 * realtime-counter loop, trading CPU occupancy for a stable scan age and one
 * scan per frame. A missed live deadline runs immediately, so it does not pay
 * a second full-frame latency merely to recover alignment.
 */

typedef struct {
    uint32_t frame_ticks;
    uint32_t start_offset_ticks;
    uint32_t max_wait_ticks;
    uint32_t stale_after_ticks;
    uint32_t next_frame_max_wait_ticks;
} analog_scan_sof_config_t;

typedef struct {
    uint32_t consumed_sof;
    bool     consumed;
} analog_scan_sof_state_t;

typedef enum {
    ANALOG_SCAN_SOF_RUN_NOW = 0,
    ANALOG_SCAN_SOF_WAIT,
} analog_scan_sof_action_t;

typedef struct {
    analog_scan_sof_action_t action;
    uint32_t                 wait_ticks;
    uint32_t                 target_ticks;
    bool                     skip_sync;
    bool                     missed_deadline;
} analog_scan_sof_plan_t;

/* Optional RAM-only observability exported by analog_matrix_scan.c. The
 * counters are intentionally not updated through this helper: the scan owns
 * the clock reads and can keep the disabled path at zero cost. */
extern volatile uint32_t analog_scan_sof_sync_skip_count;
extern volatile uint32_t analog_scan_sof_sync_miss_count;
extern volatile uint32_t analog_scan_sof_sync_wait_us;
extern volatile uint32_t analog_scan_sof_duration_us;
extern volatile uint32_t analog_scan_sof_duration_max_us;

static inline analog_scan_sof_plan_t analog_scan_sof_run(bool skip_sync, bool missed_deadline) {
    analog_scan_sof_plan_t plan = {
        .action           = ANALOG_SCAN_SOF_RUN_NOW,
        .wait_ticks       = 0,
        .target_ticks     = 0,
        .skip_sync        = skip_sync,
        .missed_deadline  = missed_deadline,
    };
    return plan;
}

static inline analog_scan_sof_plan_t analog_scan_sof_plan(const analog_scan_sof_state_t *state, uint32_t now_ticks, uint32_t sof_ticks, const analog_scan_sof_config_t *config) {
    if (config == NULL || state == NULL || config->frame_ticks == 0 || config->start_offset_ticks >= config->frame_ticks || config->stale_after_ticks == 0) {
        return analog_scan_sof_run(true, false);
    }

    const uint32_t age_ticks = now_ticks - sof_ticks;
    if (age_ticks >= config->stale_after_ticks) {
        return analog_scan_sof_run(true, false);
    }

    if (state->consumed && state->consumed_sof == sof_ticks) {
        const uint32_t target_ticks = sof_ticks + config->frame_ticks + config->start_offset_ticks;
        const int32_t  until_target = (int32_t)(target_ticks - now_ticks);

        if (until_target <= 0) {
            /* The SOF timestamp did not advance by the bounded target. Run
             * now and let the next live timestamp restore the phase. */
            return analog_scan_sof_run(true, true);
        }

        const uint32_t wait_ticks = (uint32_t)until_target;
        if (wait_ticks > config->next_frame_max_wait_ticks) {
            return analog_scan_sof_run(true, true);
        }

        analog_scan_sof_plan_t plan = {
            .action          = ANALOG_SCAN_SOF_WAIT,
            .wait_ticks      = wait_ticks,
            .target_ticks    = target_ticks,
            .skip_sync       = false,
            .missed_deadline = false,
        };
        return plan;
    }

    const uint32_t phase_ticks = age_ticks % config->frame_ticks;
    if (phase_ticks > config->start_offset_ticks) {
        /* The preferred start in this live frame is already behind us. Do
         * not wait almost a millisecond just to rediscover the same phase. */
        return analog_scan_sof_run(false, true);
    }

    const uint32_t wait_ticks = config->start_offset_ticks - phase_ticks;
    if (wait_ticks == 0) {
        return analog_scan_sof_run(false, false);
    }
    if (wait_ticks > config->max_wait_ticks) {
        return analog_scan_sof_run(true, true);
    }

    analog_scan_sof_plan_t plan = {
        .action          = ANALOG_SCAN_SOF_WAIT,
        .wait_ticks      = wait_ticks,
        .target_ticks    = now_ticks + wait_ticks,
        .skip_sync       = false,
        .missed_deadline = false,
    };
    return plan;
}

static inline void analog_scan_sof_commit(analog_scan_sof_state_t *state, uint32_t sof_ticks) {
    if (state == NULL) return;
    state->consumed_sof = sof_ticks;
    state->consumed     = true;
}
