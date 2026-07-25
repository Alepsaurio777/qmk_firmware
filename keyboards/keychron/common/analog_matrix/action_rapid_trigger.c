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

// F7: indice 0..5 de la tecla en la whitelist predictiva (KEY1..KEY6), o -1 si
// no esta o la prediccion no aplica. Las mascaras PRESS/REPRESS (bit i = KEYi+1,
// ver analog_matrix.h) eligen por separado que camino de la FSM puede predecir
// para cada tecla: adelantar el re-press acorta la ventana OFF que el tick de
// 50 ms de MC debe muestrear, asi que re-press y primer press son decisiones
// distintas por mecanica.
static inline int8_t rt_predictive_key_index(const analog_key_t *key) {
#if ANALOG_PREDICTIVE_ACTUATION_IN_GAMING_MODE
    if (analog_matrix_is_gaming_mode()) {
        if (rt_continuous_key_matches(key, ANALOG_PREDICTIVE_RT_KEY1_ROW, ANALOG_PREDICTIVE_RT_KEY1_COL)) return 0;
        if (rt_continuous_key_matches(key, ANALOG_PREDICTIVE_RT_KEY2_ROW, ANALOG_PREDICTIVE_RT_KEY2_COL)) return 1;
        if (rt_continuous_key_matches(key, ANALOG_PREDICTIVE_RT_KEY3_ROW, ANALOG_PREDICTIVE_RT_KEY3_COL)) return 2;
        if (rt_continuous_key_matches(key, ANALOG_PREDICTIVE_RT_KEY4_ROW, ANALOG_PREDICTIVE_RT_KEY4_COL)) return 3;
        if (rt_continuous_key_matches(key, ANALOG_PREDICTIVE_RT_KEY5_ROW, ANALOG_PREDICTIVE_RT_KEY5_COL)) return 4;
        if (rt_continuous_key_matches(key, ANALOG_PREDICTIVE_RT_KEY6_ROW, ANALOG_PREDICTIVE_RT_KEY6_COL)) return 5;
    }
#else
    (void)key;
#endif
    return -1;
}

static inline bool rt_predictive_downstroke_ready(const analog_key_t *key, bool predictive_rt, uint8_t target) {
    if (!predictive_rt) return false;

    // Prediction only on a clear downward stroke, close to the target point.
    if (key->travel <= key->last_travel) return false;

    const uint8_t delta = key->travel - key->last_travel;

    // Freshness gate: el scan actual debe seguir moviendose hacia abajo (filtra
    // scans donde el dedo ya freno pero vel_ema todavia no ha decaido).
    if (delta < ANALOG_PREDICTIVE_ACTUATION_MIN_DELTA) return false;

    // Velocity gate (puerta anti-typo): vel_ema es la velocidad sostenida del
    // dedo, prefiltrada por EMA en update_raw_value. Debajo del umbral, NO se
    // predice. Reemplaza el offset fijo ADVANCE (TRAVEL_SCALE = 0.1 mm):
    //   antes:   travel + ADVANCE_FIJO         >= target  (igual rapido/lento)
    //   ahora:   travel + vel_ema * LOOKAHEAD  >= target  (escala con velocidad)
    if (key->vel_ema < ANALOG_PREDICTIVE_MIN_VELOCITY) return false;

    // Piso de prediccion: el dedo debe haber recorrido ya al menos la MITAD
    // del objetivo antes de especular el resto. Sin esto, con vel_ema alta la
    // proyeccion permitia disparar desde MIN_ACTUATION (5 = ~0.08 mm) y un
    // roce rapido superficial podia registrar un press que el RT fisico nunca
    // daria. Proporcional a la config: con actuacion 24 (0.4 mm) el piso queda
    // en 12 (0.2 mm) y la anticipacion maxima acotada a medio recorrido.
    if (key->travel < MIN_ACTUATION || key->travel < (uint8_t)(target >> 1)) return false;

    const uint16_t projected = (uint16_t)key->travel + (uint16_t)key->vel_ema * ANALOG_PREDICTIVE_LOOKAHEAD;
    return projected >= target;
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

static inline uint8_t rt_repress_max_travel(bool continuous_rt) {
    return continuous_rt ? ANALOG_CONTINUOUS_RT_REPRESS_MAX_TRAVEL : RT_MAX_TRAVEL;
}

bool rapid_trigger_action(analog_key_t *key) {
    bool   changed            = false;
    int8_t update_rapid_pts   = 0;
    bool   continuous_rt      = rt_continuous_enabled(key);
    const int8_t pred_idx     = rt_predictive_key_index(key);
    bool   predictive_press   = pred_idx >= 0 && ((ANALOG_PREDICTIVE_PRESS_KEY_MASK >> pred_idx) & 1);
    bool   predictive_repress = pred_idx >= 0 && ((ANALOG_PREDICTIVE_REPRESS_KEY_MASK >> pred_idx) & 1);

    switch (key->state) {
        case AKS_REGULAR_RELEASED:
            // Chick first actuation
            if (key->travel >= key->regular.actn_pt || rt_predictive_press_ready(key, predictive_press)) {
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
            else if (rt_repress_ready(key, continuous_rt) || rt_predictive_repress_ready(key, continuous_rt, predictive_repress)) {
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
            uint8_t  max_actn_pt = rt_repress_max_travel(continuous_rt);
            key->rapid.actn_pt = actn_pt > max_actn_pt ? max_actn_pt : actn_pt;
        }
    }

    return changed;
}
