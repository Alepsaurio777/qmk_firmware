/* Host tests for scan_scheduler.h. The header is the production helper; the
 * fake clock only models the values matrix_scan_custom supplies to it. */

#include <assert.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdint.h>

#include "../scan_scheduler.h"

static const analog_scan_sof_config_t config = {
    .frame_ticks               = 1000,
    .start_offset_ticks        = 10,
    .max_wait_ticks            = 400,
    .stale_after_ticks         = 2000,
    .next_frame_max_wait_ticks = 1400,
};

static void expect_plan(analog_scan_sof_plan_t plan, analog_scan_sof_action_t action, uint32_t wait, bool skip, bool miss, const char *case_name) {
    if (plan.action != action || plan.wait_ticks != wait || plan.skip_sync != skip || plan.missed_deadline != miss) {
        fprintf(stderr,
                "FAIL %s: action=%d wait=%" PRIu32 " skip=%d miss=%d\n",
                case_name,
                plan.action,
                plan.wait_ticks,
                plan.skip_sync,
                plan.missed_deadline);
        assert(0);
    }
}

static void test_scan_520_us_keeps_one_per_frame(void) {
    analog_scan_sof_state_t state = {0};

    analog_scan_sof_plan_t plan = analog_scan_sof_plan(&state, 0, 0, &config);
    expect_plan(plan, ANALOG_SCAN_SOF_WAIT, 10, false, false, "520 initial offset");
    analog_scan_sof_commit(&state, 0);

    /* 10 us start + 520 us scan + 50 us main work. The old free-running
     * path starts again at 580 us; strict mode waits to frame 1 + 10 us. */
    plan = analog_scan_sof_plan(&state, 580, 0, &config);
    expect_plan(plan, ANALOG_SCAN_SOF_WAIT, 430, false, false, "520 next frame");

    plan = analog_scan_sof_plan(&state, 600, 0, &config);
    expect_plan(plan, ANALOG_SCAN_SOF_WAIT, 410, false, false, "520 still one frame");
}

static void test_scan_650_us_uses_short_next_frame_wait(void) {
    analog_scan_sof_state_t state = {0};
    analog_scan_sof_commit(&state, 0);

    /* 10 us start + 650 us scan: the next aligned start is only 350 us away. */
    analog_scan_sof_plan_t plan = analog_scan_sof_plan(&state, 660, 0, &config);
    expect_plan(plan, ANALOG_SCAN_SOF_WAIT, 350, false, false, "650 next frame");
}

static void test_missed_deadline_runs_now(void) {
    analog_scan_sof_state_t state = {0};

    /* The live SOF is known, but its 10 us start point is gone. Waiting 510 us
     * would add the latency strict mode is meant to avoid. */
    analog_scan_sof_plan_t plan = analog_scan_sof_plan(&state, 500, 0, &config);
    expect_plan(plan, ANALOG_SCAN_SOF_RUN_NOW, 0, false, true, "missed deadline");
}

static void test_stale_sof_runs_without_wait(void) {
    analog_scan_sof_state_t state = {0};
    analog_scan_sof_state_t consumed = {.consumed_sof = 1000, .consumed = true};

    analog_scan_sof_plan_t plan = analog_scan_sof_plan(&state, 3101, 1000, &config);
    expect_plan(plan, ANALOG_SCAN_SOF_RUN_NOW, 0, true, false, "stale SOF");

    plan = analog_scan_sof_plan(&consumed, 3101, 1000, &config);
    expect_plan(plan, ANALOG_SCAN_SOF_RUN_NOW, 0, true, false, "stale consumed SOF");
}

static void test_wrap32bit_is_phase_safe(void) {
    analog_scan_sof_state_t state = {0};
    const uint32_t sof = UINT32_MAX - 5;
    const uint32_t now = 0;

    /* now - sof is 6 across the 32-bit wrap, so the target is tick 5. */
    analog_scan_sof_plan_t plan = analog_scan_sof_plan(&state, now, sof, &config);
    expect_plan(plan, ANALOG_SCAN_SOF_WAIT, 4, false, false, "wrap current frame");
    assert(plan.target_ticks == 4);
    analog_scan_sof_commit(&state, sof);

    /* The next target is sof + 1000 + 10, also safely handled modulo 2^32. */
    plan = analog_scan_sof_plan(&state, 500, sof, &config);
    expect_plan(plan, ANALOG_SCAN_SOF_WAIT, 504, false, false, "wrap next frame");
    assert(plan.target_ticks == 1004);
}

static void test_second_call_same_frame_never_runs_again(void) {
    analog_scan_sof_state_t state = {.consumed_sof = 0, .consumed = true};

    analog_scan_sof_plan_t plan = analog_scan_sof_plan(&state, 100, 0, &config);
    expect_plan(plan, ANALOG_SCAN_SOF_WAIT, 910, false, false, "same frame waits");

    /* If the caller reaches the target but USB never advances, the helper
     * returns immediately and the caller can run free instead of spinning. */
    plan = analog_scan_sof_plan(&state, 1010, 0, &config);
    expect_plan(plan, ANALOG_SCAN_SOF_RUN_NOW, 0, true, true, "lost next SOF deadline");
}

static const analog_scan_sof_config_t config_mc189 = {
    .frame_ticks               = 1000,
    .start_offset_ticks        = 460,
    .max_wait_ticks            = 600,
    .stale_after_ticks         = 2000,
    .next_frame_max_wait_ticks = 1400,
};

static void test_mc189_460_us_offset_keeps_one_per_frame(void) {
    analog_scan_sof_state_t state = {0};

    /* At frame start (now = 0), target is 460 us */
    analog_scan_sof_plan_t plan = analog_scan_sof_plan(&state, 0, 0, &config_mc189);
    expect_plan(plan, ANALOG_SCAN_SOF_WAIT, 460, false, false, "460 mc189 initial offset");
    analog_scan_sof_commit(&state, 0);

    /* Scan finishes near ~900 us. At now = 920 us, next start is at frame 1 + 460 = 1460 us.
     * Wait should be 1460 - 920 = 540 us. */
    plan = analog_scan_sof_plan(&state, 920, 0, &config_mc189);
    expect_plan(plan, ANALOG_SCAN_SOF_WAIT, 540, false, false, "460 mc189 next frame wait");
}

int main(void) {
    test_scan_520_us_keeps_one_per_frame();
    test_scan_650_us_uses_short_next_frame_wait();
    test_missed_deadline_runs_now();
    test_stale_sof_runs_without_wait();
    test_wrap32bit_is_phase_safe();
    test_second_call_same_frame_never_runs_again();
    test_mc189_460_us_offset_keeps_one_per_frame();
    puts("OK   scan_scheduler host tests");
    return 0;
}

