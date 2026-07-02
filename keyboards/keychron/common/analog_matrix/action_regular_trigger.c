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

static inline bool regular_predictive_press_ready(const analog_key_t *key) {
#if ANALOG_PREDICTIVE_REGULAR_IN_GAMING_MODE
    if (!analog_matrix_is_gaming_mode() || !analog_matrix_predictive_regular_key_matches(key->r, key->c)) {
        return false;
    }

    if (key->travel <= key->last_travel) return false;

    const uint8_t delta = key->travel - key->last_travel;
    if (delta < ANALOG_PREDICTIVE_ACTUATION_MIN_DELTA) return false;
    if (key->travel < MIN_ACTUATION) return false;

    return (uint16_t)key->travel + ANALOG_PREDICTIVE_ACTUATION_ADVANCE >= key->regular.actn_pt;
#else
    (void)key;
    return false;
#endif
}

bool regular_trigger_action(analog_key_t *key) {
    if (key->state == AKS_REGULAR_PRESSED && (key->travel == 0 || key->travel < key->regular.deactn_pt)) {
        key->state = AKS_REGULAR_RELEASED;
        return true;
    } else if (key->state == AKS_REGULAR_RELEASED && (key->travel >= key->regular.actn_pt || regular_predictive_press_ready(key))) {
        key->state = AKS_REGULAR_PRESSED;
        return true;
    }

    return false;
}
