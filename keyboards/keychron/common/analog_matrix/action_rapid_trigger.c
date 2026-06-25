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

static int32_t rt_bottom_guard(const analog_key_t *k) {
    int32_t bottom_guard = ((int32_t)BOTTOM_DEAD_ZONE * TRAVEL_SCALE) - (int32_t)k->rpd_trig_sen_rls;
    return bottom_guard < 0 ? 0 : bottom_guard;
}

bool rapid_trigger_action(analog_key_t *key) {
    bool   changed          = false;
    int8_t update_rapid_pts = 0;

    switch (key->state) {
        case AKS_REGULAR_RELEASED:
            // Chick first actuation
            if (key->travel >= key->regular.actn_pt) {
                key->state = AKS_REGULAR_PRESSED;
                changed    = true;
                // First update rapid trigger point
                update_rapid_pts = 1;
            }
            break;

        case AKS_REGULAR_PRESSED:
            // Key releasing
            if (key->travel <= key->regular.deactn_pt) {
                key->state = AKS_REGULAR_RELEASED;
                changed    = true;
            } else if (key->travel <= key->rapid.deactn_pt && (int32_t)key->travel < rt_bottom_guard(key)) {
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
            if (key->travel <= key->regular.deactn_pt) {
                key->state = AKS_REGULAR_RELEASED;
            }
            // Press again
            else if (key->travel >= key->rapid.actn_pt && key->travel >= key->regular.actn_pt) {
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
            if (key->travel <= key->regular.deactn_pt) {
                key->state = AKS_REGULAR_RELEASED;
                changed    = true;
            } else if (key->travel <= key->rapid.deactn_pt && (int32_t)key->travel < rt_bottom_guard(key)) {
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
            key->rapid.actn_pt = actn_pt > 255 ? 255 : actn_pt;
        }
    }

    return changed;
}
