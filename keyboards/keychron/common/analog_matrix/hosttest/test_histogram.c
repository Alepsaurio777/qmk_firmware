// Tests del histograma de ventanas — sobre window_histogram.c REAL.
//
// La atribucion ON/OFF y las fronteras de cubo son justo el tipo de logica que
// sale silenciosamente mal: un off-by-one en un limite no rompe nada, sólo
// desplaza el veredicto del drill. Y el drill es lo que decide si se construye
// una feature. Asi que se pinea aqui.

#include <stdio.h>
#include <string.h>

#include "harness.h"
#include "window_histogram.h"

#define ROW_W 2
#define COL_W 2
#define ROW_SPC 5
#define COL_SPC 6

// Lectura comoda del volcado: cubos OFF en [2..9], ON en [10..17], todos uint16 LE.
static uint16_t off_bucket(const uint8_t *d, int b) {
    return (uint16_t)(d[2 + b * 2] | (d[2 + b * 2 + 1] << 8));
}
static uint16_t on_bucket(const uint8_t *d, int b) {
    return (uint16_t)(d[10 + b * 2] | (d[10 + b * 2 + 1] << 8));
}

static void setup(void) {
    hosttest_keymap_clear();
    hosttest_keymap_set(ROW_W, COL_W, KC_W);
    hosttest_keymap_set(ROW_SPC, COL_SPC, KC_SPACE);
    hosttest_set_gaming();
    hosttest_clock_set(1000);
    analog_window_hist_resolve_keys();
    analog_window_hist_reset();
}

// Sostiene un estado durante `ms` barridos de 1 ms. Las dos capas iguales:
// aqui se prueba el histograma, no los stretches.
static void hold(uint8_t row, uint8_t col, bool state, uint16_t ms) {
    for (uint16_t i = 0; i < ms; i++) {
        analog_window_hist_observe(row, col, state, state);
        hosttest_clock_advance(1);
    }
}

// ---------------------------------------------------------------------------
// 1. Las ventanas caen en el cubo que les toca
// ---------------------------------------------------------------------------
// Fronteras: [0] <25, [1] [25,50), [2] [50,55], [3] >55.
static void test_bucket_boundaries(void) {
    uint8_t d[AWH_DUMP_LEN];

    struct {
        uint16_t ms;
        int      expect;
        const char *why;
    } cases[] = {
        {10, 0, "10 ms: invisible seguro"},
        {24, 0, "24 ms: ultimo del cubo 0"},
        {25, 1, "25 ms: primero de la loteria de fase"},
        {49, 1, "49 ms: ultimo antes del tick"},
        {50, 2, "50 ms: justo el tick"},
        {55, 2, "55 ms: ultimo sin margen"},
        {56, 3, "56 ms: visible seguro"},
        {200, 3, "200 ms: visible seguro"},
    };

    for (unsigned i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        setup();
        // Semilla: un primer flanco para que la ventana siguiente sea medible.
        hold(ROW_W, COL_W, false, 5);
        hold(ROW_W, COL_W, true, cases[i].ms); // ventana ON de duracion conocida
        hold(ROW_W, COL_W, false, 2);          // el flanco de bajada la cierra

        CHECK(analog_window_hist_dump(AWH_KEY_W, AWH_LAYER_PHYSICAL, d), "el volcado deberia aceptar (W, fisica)");
        CHECK(on_bucket(d, cases[i].expect) == 1, "%s -> cubo ON %d = %u, esperaba 1", cases[i].why, cases[i].expect, on_bucket(d, cases[i].expect));
    }
}

// ---------------------------------------------------------------------------
// 2. Las ventanas ON y OFF no se mezclan
// ---------------------------------------------------------------------------
static void test_on_off_not_mixed(void) {
    uint8_t d[AWH_DUMP_LEN];
    setup();

    hold(ROW_W, COL_W, false, 5); // semilla
    hold(ROW_W, COL_W, true, 10); // ON  de 10 ms  -> cubo ON 0
    hold(ROW_W, COL_W, false, 60);// OFF de 60 ms  -> cubo OFF 3
    hold(ROW_W, COL_W, true, 5);  // cierra la OFF

    analog_window_hist_dump(AWH_KEY_W, AWH_LAYER_PHYSICAL, d);

    CHECK(on_bucket(d, 0) == 1, "cubo ON 0 = %u, esperaba 1", on_bucket(d, 0));
    CHECK(off_bucket(d, 0) == 0, "cubo OFF 0 = %u, esperaba 0 (no debe contaminarse con la ON)", off_bucket(d, 0));
    CHECK(off_bucket(d, 3) == 1, "cubo OFF 3 = %u, esperaba 1", off_bucket(d, 3));
    CHECK(on_bucket(d, 3) == 0, "cubo ON 3 = %u, esperaba 0", on_bucket(d, 3));
}

// ---------------------------------------------------------------------------
// 3. Las dos capas se cuentan por separado
// ---------------------------------------------------------------------------
// Es la razon de ser del histograma en el binario de lab: la distancia entre la
// capa fisica y la reportada ES lo que el clamp rescato.
static void test_layers_are_independent(void) {
    uint8_t dp[AWH_DUMP_LEN], dr[AWH_DUMP_LEN];
    setup();

    // Semilla en ambas capas.
    for (int i = 0; i < 5; i++) {
        analog_window_hist_observe(ROW_SPC, COL_SPC, false, false);
        hosttest_clock_advance(1);
    }
    // ON fisico de 10 ms pero ON reportado de 60 ms: exactamente lo que hace F9.
    for (int i = 0; i < 10; i++) {
        analog_window_hist_observe(ROW_SPC, COL_SPC, true, true);
        hosttest_clock_advance(1);
    }
    for (int i = 0; i < 50; i++) {
        analog_window_hist_observe(ROW_SPC, COL_SPC, false, true);
        hosttest_clock_advance(1);
    }
    for (int i = 0; i < 5; i++) {
        analog_window_hist_observe(ROW_SPC, COL_SPC, false, false);
        hosttest_clock_advance(1);
    }

    analog_window_hist_dump(AWH_KEY_SPC, AWH_LAYER_PHYSICAL, dp);
    analog_window_hist_dump(AWH_KEY_SPC, AWH_LAYER_REPORTED, dr);

    CHECK(on_bucket(dp, 0) == 1, "fisica: ON de 10 ms deberia caer en el cubo 0 (hay %u)", on_bucket(dp, 0));
    CHECK(on_bucket(dr, 3) == 1, "reportada: ON de 60 ms deberia caer en el cubo 3 (hay %u)", on_bucket(dr, 3));
    CHECK(on_bucket(dr, 0) == 0, "reportada: no deberia haber nada en el cubo 0");
}

// ---------------------------------------------------------------------------
// 4. Solo se vigilan las teclas resueltas
// ---------------------------------------------------------------------------
static void test_only_watched_keys(void) {
    uint8_t d[AWH_DUMP_LEN];
    setup();

    // (0,0) no esta en el keymap falso: no debe contar nada ni tocar memoria.
    hold(0, 0, false, 5);
    hold(0, 0, true, 30);
    hold(0, 0, false, 5);

    for (uint8_t k = 0; k < AWH_KEY_COUNT; k++) {
        analog_window_hist_dump(k, AWH_LAYER_PHYSICAL, d);
        uint32_t edges = (uint32_t)d[18] | ((uint32_t)d[19] << 8) | ((uint32_t)d[20] << 16) | ((uint32_t)d[21] << 24);
        CHECK(edges == 0, "tecla %u deberia tener 0 flancos, tiene %lu", k, (unsigned long)edges);
    }
}

// ---------------------------------------------------------------------------
// 5. Indices fuera de rango se rechazan en vez de leer basura
// ---------------------------------------------------------------------------
static void test_dump_rejects_bad_index(void) {
    uint8_t d[AWH_DUMP_LEN];
    setup();

    CHECK(!analog_window_hist_dump(AWH_KEY_COUNT, AWH_LAYER_PHYSICAL, d), "key_idx fuera de rango deberia rechazarse");
    CHECK(!analog_window_hist_dump(AWH_KEY_W, AWH_LAYER_COUNT, d), "capa fuera de rango deberia rechazarse");
    CHECK(analog_window_hist_dump(AWH_KEY_W, AWH_LAYER_PHYSICAL, d), "indices validos deberian aceptarse");
}

// ---------------------------------------------------------------------------
// 6. Salud: maximo de travel y deteccion del peor
// ---------------------------------------------------------------------------
static void test_health_max_travel(void) {
    uint8_t h[AWH_HEALTH_LEN];
    setup();

    // El histograma lee el travel del analog_key_matrix global.
    analog_key_matrix[ROW_W][COL_W].travel     = 200;
    analog_key_matrix[ROW_SPC][COL_SPC].travel = 150;

    hold(ROW_W, COL_W, false, 3);
    hold(ROW_SPC, COL_SPC, false, 3);

    analog_window_hist_health(h);

    CHECK(h[3] == 200, "max travel de W = %u, esperaba 200", h[3]);
    CHECK(h[4] == 150, "max travel de SPC = %u, esperaba 150", h[4]);
    // El peor entre las pulsadas es el candidato a iman debil.
    CHECK(h[6] == 150, "peor max travel = %u, esperaba 150 (SPC)", h[6]);
    CHECK(h[7] == ((ROW_SPC << 4) | COL_SPC), "posicion del peor = 0x%02X, esperaba 0x%02X", h[7], (ROW_SPC << 4) | COL_SPC);

    // Solo-crece: un travel menor despues no debe bajar el maximo.
    analog_key_matrix[ROW_W][COL_W].travel = 50;
    hold(ROW_W, COL_W, false, 3);
    analog_window_hist_health(h);
    CHECK(h[3] == 200, "el maximo no debe encogerse (es %u)", h[3]);
}

int main(void) {
    test_bucket_boundaries();
    test_on_off_not_mixed();
    test_layers_are_independent();
    test_only_watched_keys();
    test_dump_rejects_bad_index();
    test_health_max_travel();

    return hosttest_report("window_histogram");
}
