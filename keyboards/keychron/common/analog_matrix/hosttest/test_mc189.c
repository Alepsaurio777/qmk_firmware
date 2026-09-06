// Regresion de la configuracion real alex_mc189.
//
// La rama estable alex_mc189 tiene Continuous RT apagado; por eso la suite
// principal comprueba el bottom guard normal y el maximo fisico 245. El mismo
// fichero se compila opt-in con Continuous RT=1 para conservar la cobertura
// experimental del camino que originalmente probaba este regresion.

#include <stdio.h>

#include "harness.h"

// Implementacion real enlazada por el target MC189; la ruta de modo regular
// debe seguir usando su contrato y no la FSM de rapid trigger.
extern bool regular_trigger_action(analog_key_t *key);

// Ambas configuraciones MC189 usan el maximo fisico del K2 HE, no el cap
// estable de 240.
#define MC189_EXPECTED_MAX_TRAVEL     245
#if ANALOG_CONTINUOUS_RAPID_TRIGGER_IN_GAMING_MODE
#    define MC189_EXPECTED_RELEASE_TRAVEL 233
#    define MC189_EXPECTED_PRE_RELEASE    234
#    define MC189_EXPECTED_REPRESS_TRAVEL 245
#else
#    define MC189_EXPECTED_RELEASE_TRAVEL 215
#    define MC189_EXPECTED_PRE_RELEASE    216
#    define MC189_EXPECTED_REPRESS_TRAVEL 233
#endif
#define MC189_EXPECTED_GUARD_RELEASE  215
#define MC189_CYCLES                  100

#define ROW_W     2
#define COL_W     2
#define ROW_A     2
#define COL_A     1
#define ROW_S     2
#define COL_S     3
#define ROW_D     2
#define COL_D     4
#define ROW_SPC   5
#define COL_SPC   6
#define ROW_LSFT  4
#define COL_LSFT  0
#define ROW_LCTRL 4
#define COL_LCTRL 1

typedef struct {
    const char *name;
    uint8_t     row;
    uint8_t     col;
    uint16_t    keycode;
} mc189_key_t;

static const mc189_key_t MC189_WHITELIST[] = {
    {"Space", ROW_SPC, COL_SPC, KC_SPACE},
    {"LShift", ROW_LSFT, COL_LSFT, KC_LEFT_SHIFT},
    {"W", ROW_W, COL_W, KC_W},
    {"A", ROW_A, COL_A, KC_A},
    {"S", ROW_S, COL_S, KC_S},
    {"D", ROW_D, COL_D, KC_D},
};

static const mc189_key_t MC189_REGULAR_KEY = {"LCtrl", ROW_LCTRL, COL_LCTRL, KC_LEFT_CTRL};

static const rt_launcher_cfg_t MC189_RAPID = {
    .act_pt  = HOSTTEST_GAMING_ACT_PT,
    .sen     = HOSTTEST_GAMING_SEN,
    .sen_rls = HOSTTEST_GAMING_SEN_RLS,
    .gaming  = true,
};

// Es el mismo keymap vivo que consulta analog_matrix_resolve_policy_keys().
static void setup_mc189_keymap(void) {
    hosttest_keymap_clear();
    for (uint8_t i = 0; i < (uint8_t)(sizeof(MC189_WHITELIST) / sizeof(MC189_WHITELIST[0])); i++) {
        const mc189_key_t *key = &MC189_WHITELIST[i];
        hosttest_keymap_set(key->row, key->col, key->keycode);
    }
    hosttest_keymap_set(MC189_REGULAR_KEY.row, MC189_REGULAR_KEY.col, MC189_REGULAR_KEY.keycode);
    hosttest_set_gaming();
    hosttest_clock_set(0);
#if ANALOG_POLICY_NEEDED
    analog_matrix_resolve_policy_keys();
#endif
}

#if ANALOG_CONTINUOUS_RAPID_TRIGGER_IN_GAMING_MODE
static bool continuous_policy_at(const mc189_key_t *key) {
    return analog_policy_bit(analog_continuous_rt_mask, key->row, key->col);
}
#endif

static void test_whitelist_resolution(void) {
    setup_mc189_keymap();

    CHECK(ANALOG_CONTINUOUS_RT_REPRESS_MAX_TRAVEL == MC189_EXPECTED_MAX_TRAVEL,
          "MC189 max re-press = %u, esperaba %u", ANALOG_CONTINUOUS_RT_REPRESS_MAX_TRAVEL, MC189_EXPECTED_MAX_TRAVEL);

#if ANALOG_CONTINUOUS_RAPID_TRIGGER_IN_GAMING_MODE
    for (uint8_t i = 0; i < (uint8_t)(sizeof(MC189_WHITELIST) / sizeof(MC189_WHITELIST[0])); i++) {
        const mc189_key_t *key = &MC189_WHITELIST[i];
        CHECK(continuous_policy_at(key), "whitelist MC189: %s no resolvio en (%u,%u)", key->name, key->row, key->col);
    }
    CHECK(!continuous_policy_at(&MC189_REGULAR_KEY), "LCtrl no whitelist: no debe recibir continuous RT");
#else
    // En alex_mc189 la feature esta fuera del binario; no existe bitmap que
    // resolver y ninguna tecla puede entrar accidentalmente en esa politica.
    CHECK(!ANALOG_CONTINUOUS_RAPID_TRIGGER_IN_GAMING_MODE,
          "MC189 estable no debe compilar Continuous RT");
#endif
}

#if ANALOG_CONTINUOUS_RAPID_TRIGGER_IN_GAMING_MODE
static void test_whitelist_follows_remap(void) {
    const uint8_t remap_row = 3;
    const uint8_t remap_col = 9;

    setup_mc189_keymap();
    hosttest_keymap_set(ROW_W, COL_W, KC_NO);
    hosttest_keymap_set(remap_row, remap_col, KC_W);
    analog_matrix_resolve_policy_keys();

    const mc189_key_t old_w = {"W-old", ROW_W, COL_W, KC_W};
    const mc189_key_t new_w = {"W-new", remap_row, remap_col, KC_W};
    CHECK(!continuous_policy_at(&old_w), "remap MC189: la posicion vieja de W sigue whitelist");
    CHECK(continuous_policy_at(&new_w), "remap MC189: la posicion nueva de W no recibio whitelist");
}
#endif

// Ejecuta 100 ciclos completos. El paso intermedio a 240 es intencional: el
// re-press debe esperar al maximo fisico 245 en MC189. En la configuracion real
// estable, el release sigue respetando el bottom guard y ocurre en 215; en la
// variante experimental Continuous RT lo evita y ocurre en 233.
static void run_bottom_out_cycles(const mc189_key_t *key) {
    rt_sim_t  sim;
    uint32_t  t = 0;
    const uint8_t first_press = (uint8_t)(HOSTTEST_GAMING_ACT_PT * TRAVEL_SCALE);

    rt_sim_init(&sim, key->row, key->col, &MC189_RAPID);
    hosttest_clock_set(0);

    rt_sim_step(&sim, t++, first_press);
    CHECK(sim.n_edges == 1 && sim.edges[0].pressed,
          "%s: el press inicial no ocurrio en %u", key->name, first_press);
    CHECK(sim.key.state == AKS_REGULAR_PRESSED,
          "%s: estado inicial %u, esperaba AKS_REGULAR_PRESSED", key->name, sim.key.state);

    for (uint16_t cycle = 0; cycle < MC189_CYCLES; cycle++) {
        const uint16_t edges_before = sim.n_edges;

        if (cycle > 0) {
            // Continuous RT vuelve a exigir 245. En la configuracion real
            // apagada el release del guard ocurre en 215, asi que el umbral
            // siguiente es 215 + 18 = 233 y 240 ya produce el re-press.
            rt_sim_step(&sim, t++, 240);
#if ANALOG_CONTINUOUS_RAPID_TRIGGER_IN_GAMING_MODE
            CHECK(sim.key.state == AKS_RAPID_RELEASED,
                  "%s ciclo %u: re-press prematuro a 240, estado %u", key->name, cycle, sim.key.state);
            CHECK(sim.n_edges == edges_before,
                  "%s ciclo %u: 240 genero un flanco inesperado", key->name, cycle);
#else
            CHECK(sim.key.state == AKS_RAPID_PRESSED,
                  "%s ciclo %u: re-press a 240 no ocurrio, estado %u", key->name, cycle, sim.key.state);
            CHECK(sim.n_edges == (uint16_t)(edges_before + 1),
                  "%s ciclo %u: 240 no genero el re-press esperado", key->name, cycle);
#endif
        }

        const uint32_t bottom_t = t++;
        rt_sim_step(&sim, bottom_t, MC189_EXPECTED_MAX_TRAVEL);
        const uint8_t expected_peak =
#if ANALOG_CONTINUOUS_RAPID_TRIGGER_IN_GAMING_MODE
            MC189_EXPECTED_MAX_TRAVEL;
#else
            cycle == 0 ? MC189_EXPECTED_MAX_TRAVEL : 240;
#endif
        CHECK(sim.key.state == AKS_RAPID_PRESSED || sim.key.state == AKS_REGULAR_PRESSED,
              "%s ciclo %u: bottom-out no quedo presionado (estado %u)", key->name, cycle, sim.key.state);
        CHECK(sim.key.rapid.actn_pt == expected_peak,
              "%s ciclo %u: peak = %u, esperaba %u", key->name, cycle, sim.key.rapid.actn_pt, expected_peak);

        rt_sim_step(&sim, t++, MC189_EXPECTED_PRE_RELEASE);
        CHECK(sim.key.state == AKS_RAPID_PRESSED || sim.key.state == AKS_REGULAR_PRESSED,
              "%s ciclo %u: solto antes de 233 (estado %u)", key->name, cycle, sim.key.state);

        const uint32_t release_t = t++;
        rt_sim_step(&sim, release_t, MC189_EXPECTED_RELEASE_TRAVEL);
        CHECK(sim.key.state == AKS_RAPID_RELEASED,
              "%s ciclo %u: estado tras release %u, esperaba RAPID_RELEASED", key->name, cycle, sim.key.state);
        CHECK(sim.key.rapid.deactn_pt == MC189_EXPECTED_RELEASE_TRAVEL,
              "%s ciclo %u: release point = %u, esperaba %u", key->name, cycle, sim.key.rapid.deactn_pt, MC189_EXPECTED_RELEASE_TRAVEL);
        CHECK(sim.key.rapid.actn_pt == MC189_EXPECTED_REPRESS_TRAVEL,
              "%s ciclo %u: siguiente re-press = %u, esperaba %u", key->name, cycle, sim.key.rapid.actn_pt, MC189_EXPECTED_REPRESS_TRAVEL);
        CHECK(sim.n_edges == (uint16_t)(edges_before + (cycle == 0 ? 1 : 2)),
              "%s ciclo %u: flancos = %u, esperaba delta %u", key->name, cycle, sim.n_edges,
              (uint16_t)(edges_before + (cycle == 0 ? 1 : 2)));
        CHECK(sim.edges[sim.n_edges - 1].t_ms == release_t,
              "%s ciclo %u: release en t=%lu, esperaba %lu", key->name, cycle,
              (unsigned long)sim.edges[sim.n_edges - 1].t_ms, (unsigned long)release_t);
    }

    CHECK(sim.n_edges == 200, "%s: 100 ciclos produjeron %u flancos, esperaba 200", key->name, sim.n_edges);
}

static void test_non_whitelist_guard(void) {
    rt_sim_t sim;
    uint32_t t = 0;

    setup_mc189_keymap();
    rt_sim_init(&sim, MC189_REGULAR_KEY.row, MC189_REGULAR_KEY.col, &MC189_RAPID);
    hosttest_clock_set(0);

    rt_sim_step(&sim, t++, 120);
    rt_sim_step(&sim, t++, MC189_EXPECTED_MAX_TRAVEL);
    rt_sim_step(&sim, t++, MC189_EXPECTED_PRE_RELEASE);
    CHECK(sim.n_edges == 1 && sim.key.state == AKS_REGULAR_PRESSED,
          "no-whitelist: %u debe conservar el bottom guard (flancos=%u, estado=%u)", MC189_EXPECTED_PRE_RELEASE, sim.n_edges, sim.key.state);

    rt_sim_step(&sim, t++, MC189_EXPECTED_GUARD_RELEASE);
    CHECK(sim.n_edges == 2 && sim.key.state == AKS_RAPID_RELEASED,
          "no-whitelist: 215 debe liberar por debajo del guard (flancos=%u, estado=%u)", sim.n_edges, sim.key.state);
}

#if ANALOG_CONTINUOUS_RAPID_TRIGGER_IN_GAMING_MODE
static void test_continuous_only_in_gaming(void) {
    rt_sim_t sim;
    uint32_t t = 0;
    const mc189_key_t *key = &MC189_WHITELIST[2]; // W

    setup_mc189_keymap();
    hosttest_set_windows();
    rt_sim_init(&sim, key->row, key->col, &MC189_RAPID);
    hosttest_clock_set(0);

    rt_sim_step(&sim, t++, 120);
    rt_sim_step(&sim, t++, MC189_EXPECTED_MAX_TRAVEL);
    rt_sim_step(&sim, t++, MC189_EXPECTED_RELEASE_TRAVEL);
    CHECK(sim.n_edges == 1 && sim.key.state == AKS_REGULAR_PRESSED,
          "fuera de Gaming: W no debe usar continuous RT a 233 (flancos=%u, estado=%u)", sim.n_edges, sim.key.state);

    hosttest_set_gaming();
}
#endif

static bool regular_step(analog_key_t *key, uint8_t travel) {
    key->travel = travel;
    if (travel == key->last_travel) return false;

    const bool changed = regular_trigger_action(key);
    key->last_travel = travel;
    return changed;
}

static void test_regular_mode_unchanged(void) {
    const mc189_key_t *keys[] = {&MC189_WHITELIST[2], &MC189_REGULAR_KEY};

    setup_mc189_keymap();
    for (uint8_t i = 0; i < (uint8_t)(sizeof(keys) / sizeof(keys[0])); i++) {
        const mc189_key_t *source = keys[i];
        analog_key_t key = {0};
        key.r              = source->row;
        key.c              = source->col;
        key.mode           = AKM_REGULAR;
        key.state          = AKS_REGULAR_RELEASED;
        key.regular.actn_pt   = 120;
        key.regular.deactn_pt = 90;

        CHECK(regular_step(&key, 245), "%s regular: press inicial no ocurrio", source->name);
        CHECK(key.state == AKS_REGULAR_PRESSED, "%s regular: estado tras press %u", source->name, key.state);
        CHECK(!regular_step(&key, 233), "%s regular: no debe liberar por el umbral continuous RT", source->name);
        CHECK(key.state == AKS_REGULAR_PRESSED, "%s regular: cambio a release en 233", source->name);
        CHECK(!regular_step(&key, 90), "%s regular: cambio en el limite deactn inesperado", source->name);
        CHECK(regular_step(&key, 89), "%s regular: no libero por debajo de deactn", source->name);
        CHECK(key.state == AKS_REGULAR_RELEASED, "%s regular: estado final %u", source->name, key.state);
    }
}

int main(void) {
    test_whitelist_resolution();
#if ANALOG_CONTINUOUS_RAPID_TRIGGER_IN_GAMING_MODE
    test_whitelist_follows_remap();
#endif

    setup_mc189_keymap();
    for (uint8_t i = 0; i < (uint8_t)(sizeof(MC189_WHITELIST) / sizeof(MC189_WHITELIST[0])); i++) {
        run_bottom_out_cycles(&MC189_WHITELIST[i]);
    }

    test_non_whitelist_guard();
#if ANALOG_CONTINUOUS_RAPID_TRIGGER_IN_GAMING_MODE
    test_continuous_only_in_gaming();
#endif
    test_regular_mode_unchanged();

#if ANALOG_CONTINUOUS_RAPID_TRIGGER_IN_GAMING_MODE
    printf("     [info] MC189 experimental: %u teclas whitelist x %u ciclos, peak 245, release 233\n",
           (unsigned)(sizeof(MC189_WHITELIST) / sizeof(MC189_WHITELIST[0])), MC189_CYCLES);
    return hosttest_report("mc189 continuous RT experimental");
#else
    printf("     [info] MC189 real: %u teclas profile x %u ciclos, peak inicial 245/repetido 240, release 215\n",
           (unsigned)(sizeof(MC189_WHITELIST) / sizeof(MC189_WHITELIST[0])), MC189_CYCLES);
    return hosttest_report("mc189 alex_mc189 stable");
#endif
}
