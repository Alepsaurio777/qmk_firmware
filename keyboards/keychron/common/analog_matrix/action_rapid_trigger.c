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

#include "analog_matrix.h"

enum {
    RT_MAX_TRAVEL = (FULL_TRAVEL_UNIT + 1) * TRAVEL_SCALE - 1,
};

static int32_t rt_bottom_guard(const analog_key_t *k) {
    int32_t bottom_guard = ((int32_t)BOTTOM_DEAD_ZONE * TRAVEL_SCALE) - (int32_t)k->rpd_trig_sen_rls;
    return bottom_guard < 0 ? 0 : bottom_guard;
}

static inline bool rt_continuous_key_matches(const analog_key_t *key, uint8_t row, uint8_t col) {
    return row != 0xFF && col != 0xFF && key->r == row && key->c == col;
}

static inline bool rt_continuous_enabled(const analog_key_t *key) {
#if ANALOG_CONTINUOUS_RAPID_TRIGGER_IN_GAMING_MODE
    return analog_matrix_is_gaming_mode() &&
           (rt_continuous_key_matches(key, ANALOG_CONTINUOUS_RT_KEY1_ROW, ANALOG_CONTINUOUS_RT_KEY1_COL) ||
            rt_continuous_key_matches(key, ANALOG_CONTINUOUS_RT_KEY2_ROW, ANALOG_CONTINUOUS_RT_KEY2_COL));
#else
    (void)key;
    return false;
#endif
}

static inline bool rt_predictive_enabled(const analog_key_t *key) {
#if ANALOG_PREDICTIVE_ACTUATION_IN_GAMING_MODE
    return analog_matrix_is_gaming_mode() &&
           (rt_continuous_key_matches(key, ANALOG_PREDICTIVE_RT_KEY1_ROW, ANALOG_PREDICTIVE_RT_KEY1_COL) ||
            rt_continuous_key_matches(key, ANALOG_PREDICTIVE_RT_KEY2_ROW, ANALOG_PREDICTIVE_RT_KEY2_COL));
#else
    (void)key;
    return false;
#endif
}

static inline bool rt_predictive_downstroke_ready(const analog_key_t *key, bool predictive_rt, uint8_t target) {
    if (!predictive_rt) return false;

    // Prediction only on a clear downward stroke, close to the target point.
    if (key->travel <= key->last_travel) return false;

    const uint8_t delta = key->travel - key->last_travel;
    if (delta < ANALOG_PREDICTIVE_ACTUATION_MIN_DELTA) return false;
    if (key->travel < MIN_ACTUATION) return false;

    return (uint16_t)key->travel + ANALOG_PREDICTIVE_ACTUATION_ADVANCE >= target;
}

static inline bool rt_predictive_press_ready(const analog_key_t *key, bool predictive_rt) {
    return rt_predictive_downstroke_ready(key, predictive_rt, key->regular.actn_pt);
}

static inline bool rt_predictive_repress_ready(const analog_key_t *key, bool continuous_rt, bool predictive_rt) {
    uint8_t target = key->rapid.actn_pt;
    if (!continuous_rt && target < key->regular.actn_pt) {
        target = key->regular.actn_pt;
    }

    return rt_predictive_downstroke_ready(key, predictive_rt, target);
}

static inline bool rt_regular_release_ready(const analog_key_t *key, bool continuous_rt) {
    return continuous_rt ? key->travel == 0 : key->travel <= key->regular.deactn_pt;
}

static inline bool rt_dynamic_release_ready(const analog_key_t *key, bool continuous_rt) {
    if (key->travel > key->rapid.deactn_pt) return false;

    // For whitelisted Continuous RT keys, do not require the extra bottom guard:
    // Launcher's RT release sensitivity becomes the release threshold. This is
    // intentionally more responsive for Space/Shift spam experiments.
    if (continuous_rt) return true;

    return (int32_t)key->travel < rt_bottom_guard(key);
}

static inline bool rt_repress_ready(const analog_key_t *key, bool continuous_rt) {
    return key->travel >= key->rapid.actn_pt && (continuous_rt || key->travel >= key->regular.actn_pt);
}

bool rapid_trigger_action(analog_key_t *key) {
    bool   changed          = false;
    int8_t update_rapid_pts = 0;
    bool   continuous_rt    = rt_continuous_enabled(key);
    bool   predictive_rt    = rt_predictive_enabled(key);

    switch (key->state) {
        case AKS_REGULAR_RELEASED:
            // Chick first actuation
            if (key->travel >= key->regular.actn_pt || rt_predictive_press_ready(key, predictive_rt)) {
                key->state = AKS_REGULAR_PRESSED;
                changed    = true;
                // First update rapid trigger point
                update_rapid_pts = 1;
            }
            break;

        case AKS_REGULAR_PRESSED:
            // Key releasing
            if (rt_regular_release_ready(key, continuous_rt)) {
                key->state = AKS_REGULAR_RELEASED;
                changed    = true;
            } else if (rt_dynamic_release_ready(key, continuous_rt)) {
                key->state       = AKS_RAPID_RELEASED;
                changed          = true;
                update_rapid_pts = -1;
            }
            // Continue pressing
            else if (key->travel > key->rapid.actn_pt) {
                update_rapid_pts = 1;
            }
            break;

        case AKS_RAPID_RELEASED:
            // Continue releasing
            if (rt_regular_release_ready(key, continuous_rt)) {
                key->state = AKS_REGULAR_RELEASED;
            }
            // Press again
            else if (rt_repress_ready(key, continuous_rt) || rt_predictive_repress_ready(key, continuous_rt, predictive_rt)) {
                key->state       = AKS_RAPID_PRESSED;
                changed          = true;
                update_rapid_pts = 1;
            } else if (key->travel < key->rapid.deactn_pt) {
                update_rapid_pts = -1;
            }
            break;

        case AKS_RAPID_PRESSED:
            // Key releasing
            if (key->travel > FULL_TRAVEL_UNIT * TRAVEL_SCALE) {
                break;
            }
            if (rt_regular_release_ready(key, continuous_rt)) {
                key->state = AKS_REGULAR_RELEASED;
                changed    = true;
            } else if (rt_dynamic_release_ready(key, continuous_rt)) {
                key->state       = AKS_RAPID_RELEASED;
                changed          = true;
                update_rapid_pts = -1;
            }
            // Continue pressing
            else if (key->travel > key->rapid.actn_pt) {
                update_rapid_pts = 1;
            }
            break;

        default:
            break;
    }

    if (update_rapid_pts) {
        if (update_rapid_pts > 0) {
            int16_t deact = (int16_t)key->travel - (int16_t)key->rpd_trig_sen_rls;
            key->rapid.deactn_pt = (deact > 0) ? (uint8_t)deact : 0;
            key->rapid.actn_pt   = key->travel;
        } else {
            key->rapid.deactn_pt = key->travel;
            uint16_t actn_pt = (uint16_t)key->travel + key->rpd_trig_sen;
            key->rapid.actn_pt = actn_pt > RT_MAX_TRAVEL ? RT_MAX_TRAVEL : actn_pt;
        }
    }

    return changed;
}
