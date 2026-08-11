/* Implementacion del histograma de ventanas y de los contadores de salud.
 * Racional completo en window_histogram.h. */

#include "window_histogram.h"

#if ANALOG_WINDOW_HISTOGRAM

#    include <string.h>

#    include "keymap_common.h"
#    include "timer.h"

// Keycodes de las teclas vigiladas. El ORDEN fija AWH_KEY_*, asi que no
// reordenar sin tocar el cliente.
static const uint16_t awh_keycodes[AWH_KEY_COUNT] = {
    [AWH_KEY_W]    = KC_W,
    [AWH_KEY_SPC]  = KC_SPACE,
    [AWH_KEY_LSFT] = KC_LEFT_SHIFT,
};

// Posiciones resueltas (0xFF = ese keycode no esta en la capa de politica).
static uint8_t awh_row[AWH_KEY_COUNT];
static uint8_t awh_col[AWH_KEY_COUNT];

// Rechazo rapido en el hot path: un test de bit descarta las ~93 teclas que no
// vigilamos antes de tocar nada mas.
static matrix_row_t awh_mask[MATRIX_ROWS];

typedef struct {
    uint16_t bucket[AWH_BUCKETS];
    uint32_t edges;
    uint16_t last_change; // timer_read() del ultimo flanco
    bool     state;
    bool     observing;  // ya vimos al menos una muestra de esta tecla
    // (1-ago) La PRIMERA ventana tras un reset esta truncada: empezo antes de
    // que empezaramos a mirar, asi que su duracion medida es menor que la real.
    // Contarla sesga los cubos bajos hacia arriba en cada sesion, que es
    // justamente la direccion que haria parecer que hay mas ventanas invisibles
    // de las que hay. Solo se cuenta a partir del PRIMER flanco observado, que
    // es el primer instante en que sabemos de verdad cuando empezo la ventana.
    // Lo cazo un test de host; a ojo en una traza no se habria visto.
    bool measurable;
} awh_layer_t;

static awh_layer_t awh[AWH_KEY_COUNT][AWH_LAYER_COUNT];

// --- Salud -----------------------------------------------------------------
// (1-ago, revisado) Aqui vivia un "detector de tecla pegada" que contaba teclas
// reportadas ON con el travel por debajo de su desactuacion. Se quito porque NO
// PODIA DISPARARSE: la propia FSM se autocorrige — rt_regular_release_ready()
// suelta en cuanto travel <= regular.deactn_pt, y esa comprobacion corre la
// primera en los dos estados PRESSED. O sea, una alarma que no puede sonar,
// ocupando sitio en un binario de torneo que acababa de crecer 1256 B.
//
// Y el fantasma REAL es la condicion contraria: cuando la calibracion de reposo
// deriva, el travel lee ALTO con el dedo fuera, la FSM cree honestamente que la
// tecla esta pulsada y no hay nada incoherente que detectar. Lo que delata eso
// es que la tecla NUNCA VUELVE CERCA DE CERO.
//
// Por eso ahora se guarda el MINIMO de travel por tecla, espejo del maximo:
//   - max bajo  -> iman debil / recorrido perdido
//   - min alto  -> no vuelve a reposo = candidato a fantasma
// Las dos son medidas directas y alcanzables, no inferencias sobre estado.
static uint8_t awh_max_travel[MATRIX_ROWS][MATRIX_COLS];
// BSS arranca a cero, asi que el primer resolve inicializa los minimos al tope.
// Hacerlo en runtime evita guardar una matriz de 0xFF tambien en flash.
static uint8_t awh_min_travel[MATRIX_ROWS][MATRIX_COLS];
static bool    awh_health_initialized;

void analog_window_hist_resolve_keys(void) {
    if (!awh_health_initialized) {
        memset(awh_min_travel, 0xFF, sizeof(awh_min_travel));
        awh_health_initialized = true;
    }

    memset(awh_mask, 0, sizeof(awh_mask));
    for (uint8_t i = 0; i < AWH_KEY_COUNT; i++) {
        awh_row[i] = 0xFF;
        awh_col[i] = 0xFF;
    }

    for (uint8_t row = 0; row < MATRIX_ROWS; row++) {
        for (uint8_t col = 0; col < MATRIX_COLS; col++) {
            const keypos_t pos = {.row = row, .col = col};
            const uint16_t kc  = keymap_key_to_keycode(ANALOG_POLICY_LAYER, pos);
            if (kc == KC_NO || kc == KC_TRANSPARENT) continue;

            for (uint8_t i = 0; i < AWH_KEY_COUNT; i++) {
                // Primera coincidencia gana: el estado del histograma es por
                // slot, igual que en los stretches.
                if (kc != awh_keycodes[i] || awh_row[i] != 0xFF) continue;
                awh_row[i] = row;
                awh_col[i] = col;
                awh_mask[row] |= (matrix_row_t)1 << col;
            }
        }
    }
}

static inline int8_t awh_slot(uint8_t row, uint8_t col) {
    for (uint8_t i = 0; i < AWH_KEY_COUNT; i++) {
        if (awh_row[i] == row && awh_col[i] == col) return (int8_t)i;
    }
    return -1;
}

static inline uint8_t awh_bucket_of(uint16_t ms) {
    if (ms < 25) return 0;
    if (ms < ANALOG_TICK_REFERENCE_MS) return 1;
    if (ms <= ANALOG_RELEASE_STRETCH_MS) return 2;
    return 3;
}

// El histograma separa ventanas ON de ventanas OFF, y awh_layer_t solo lleva un
// juego de cubos (el de OFF). Los cubos ON viven en este array paralelo: la
// ventana que se cierra en un flanco es la del estado ANTERIOR, asi que el
// destino es `state_previo ? ON : OFF`.
static uint16_t awh_on_bucket[AWH_KEY_COUNT][AWH_LAYER_COUNT][AWH_BUCKETS];

void analog_window_hist_observe(uint8_t row, uint8_t col, bool physical, bool reported) {
    if (row >= MATRIX_ROWS) return;
    if ((awh_mask[row] & ((matrix_row_t)1 << col)) == 0) return; // rechazo rapido

    const int8_t slot = awh_slot(row, col);
    if (slot < 0) return;

    const uint16_t t = timer_read();

    const bool layer_state_now[AWH_LAYER_COUNT] = {physical, reported};

    for (uint8_t layer = 0; layer < AWH_LAYER_COUNT; layer++) {
        awh_layer_t *l   = &awh[slot][layer];
        const bool   now = layer_state_now[layer];

        if (!l->observing) { // primera muestra: solo fijar el punto de partida
            l->observing   = true;
            l->state       = now;
            l->last_change = t;
            continue;
        }
        if (now == l->state) continue; // sin flanco, nada que cerrar

        if (l->measurable) {
            const uint16_t dur = (uint16_t)(t - l->last_change);
            const uint8_t  b   = awh_bucket_of(dur);
            // La ventana que se cierra es la del estado ANTERIOR: si estaba ON y
            // pasa a OFF, lo que acaba es una ventana ON.
            uint16_t *bucket_ptr = l->state ? &awh_on_bucket[slot][layer][b] : &l->bucket[b];
            if (*bucket_ptr != UINT16_MAX) (*bucket_ptr)++;
            if (l->edges != UINT32_MAX) l->edges++;
        }
        l->measurable  = true; // a partir de aqui sabemos donde empieza cada ventana
        l->state       = now;
        l->last_change = t;
    }

    // --- Salud ---------------------------------------------------------------
    const uint8_t travel = analog_matrix_get_travel(row, col);
    if (travel > awh_max_travel[row][col]) awh_max_travel[row][col] = travel;
    if (travel < awh_min_travel[row][col]) awh_min_travel[row][col] = travel;
}

void analog_window_hist_reset(void) {
    memset(awh, 0, sizeof(awh));
    memset(awh_on_bucket, 0, sizeof(awh_on_bucket));
    memset(awh_max_travel, 0, sizeof(awh_max_travel));
    // El minimo arranca en el tope para que la primera muestra lo baje.
    memset(awh_min_travel, 0xFF, sizeof(awh_min_travel));
    awh_health_initialized = true;
}

bool analog_window_hist_dump(uint8_t key_idx, uint8_t layer, uint8_t *out) {
    if (key_idx >= AWH_KEY_COUNT || layer >= AWH_LAYER_COUNT) return false;

    memset(out, 0, AWH_DUMP_LEN);
    const awh_layer_t *l = &awh[key_idx][layer];

    out[0] = key_idx;
    out[1] = layer;
    for (uint8_t b = 0; b < AWH_BUCKETS; b++) {
        out[2 + b * 2]      = (uint8_t)(l->bucket[b] & 0xFF); // ventanas OFF
        out[2 + b * 2 + 1]  = (uint8_t)(l->bucket[b] >> 8);
        out[10 + b * 2]     = (uint8_t)(awh_on_bucket[key_idx][layer][b] & 0xFF); // ON
        out[10 + b * 2 + 1] = (uint8_t)(awh_on_bucket[key_idx][layer][b] >> 8);
    }
    out[18] = (uint8_t)(l->edges & 0xFF);
    out[19] = (uint8_t)((l->edges >> 8) & 0xFF);
    out[20] = (uint8_t)((l->edges >> 16) & 0xFF);
    out[21] = (uint8_t)((l->edges >> 24) & 0xFF);

    return true;
}

void analog_window_hist_health(uint8_t *out) {
    memset(out, 0, AWH_HEALTH_LEN);

    for (uint8_t i = 0; i < AWH_KEY_COUNT; i++) {
        const uint8_t r = awh_row[i];
        const uint8_t c = awh_col[i];
        const bool    ok = (r < MATRIX_ROWS && c < MATRIX_COLS);
        out[i]     = ok ? awh_min_travel[r][c] : 0;
        out[3 + i] = ok ? awh_max_travel[r][c] : 0;
    }

    // Peor MAXIMO entre las teclas pulsadas: candidato a iman debilitado. Con
    // AUTO_CALIBRATION y BOTTOM_OUT_LEARN a 0 en torneo, nada mas en el firmware
    // se entera de que una tecla perdio recorrido.
    uint8_t worst_max     = 0xFF;
    uint8_t worst_max_pos = 0xFF;
    // Peor MINIMO (el mas alto) entre todas: candidato a fantasma, porque una
    // tecla que nunca vuelve cerca de cero tiene el reposo derivado.
    uint8_t worst_min     = 0;
    uint8_t worst_min_pos = 0xFF;
    uint8_t seen          = 0;

    for (uint8_t r = 0; r < MATRIX_ROWS; r++) {
        for (uint8_t c = 0; c < MATRIX_COLS; c++) {
            if (awh_min_travel[r][c] == 0xFF) continue; // nunca observada
            if (seen != UINT8_MAX) seen++;

            const uint8_t mx = awh_max_travel[r][c];
            if (mx > 0 && mx < worst_max) {
                worst_max     = mx;
                worst_max_pos = (uint8_t)((r << 4) | c);
            }
            if (awh_min_travel[r][c] > worst_min) {
                worst_min     = awh_min_travel[r][c];
                worst_min_pos = (uint8_t)((r << 4) | c);
            }
        }
    }

    out[6] = (worst_max == 0xFF) ? 0 : worst_max;
    out[7] = worst_max_pos;
    out[8] = worst_min;
    out[9] = worst_min_pos;
    out[10] = seen;
}

#endif // ANALOG_WINDOW_HISTOGRAM
