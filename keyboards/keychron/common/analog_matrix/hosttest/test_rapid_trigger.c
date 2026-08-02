// Tests de la FSM del rapid trigger — sobre action_rapid_trigger.c REAL.
//
// Cuatro estados y una docena de transiciones que hasta ahora sólo se validaban
// jugando. Lo que se pinea aqui no es "se siente bien", es el contrato: donde
// dispara el press, donde el release, y que hace la zona muerta del fondo.

#include <stdio.h>

#include "harness.h"

// Configuracion real del perfil gaming (ver config_mirror.h).
static const rt_launcher_cfg_t GAMING = {
    .act_pt  = HOSTTEST_GAMING_ACT_PT,
    .sen     = HOSTTEST_GAMING_SEN,
    .sen_rls = HOSTTEST_GAMING_SEN_RLS,
    .gaming  = true,
};

// ---------------------------------------------------------------------------
// 1. Ancla del escalado y la histeresis
// ---------------------------------------------------------------------------
// Este es el test que detecta que config_mirror.h se ha quedado atras respecto
// al config.h del teclado, o que update_key_config() cambio su calculo. Sin el,
// una divergencia se manifestaria como un A/B que miente en vez de un rojo.
static void test_config_anchor(void) {
    rt_sim_t s;
    rt_sim_init(&s, 2, 2, &GAMING);

    // act_pt 20 (2.0 mm) * TRAVEL_SCALE 6 = 120
    CHECK(s.key.regular.actn_pt == 120, "actn_pt escalado = %u, esperaba 120", s.key.regular.actn_pt);

    // Histeresis Gaming: min(STATIC_HYSTERESIS_GAMING=5, act_pt/2=10) = 5.
    // deactn = (20 - 5) * 6 = 90.
    CHECK(s.key.regular.deactn_pt == 90, "deactn_pt escalado = %u, esperaba 90", s.key.regular.deactn_pt);

    CHECK(s.key.rpd_trig_sen == 18, "sen escalado = %u, esperaba 18", s.key.rpd_trig_sen);
    CHECK(s.key.rpd_trig_sen_rls == 12, "sen_rls escalado = %u, esperaba 12", s.key.rpd_trig_sen_rls);

    // Actuacion superficial: la histeresis adaptativa la capa a act_pt/2.
    // act_pt 4 (0.4 mm) -> hyst = min(5, 2) = 2 -> deactn = (4-2)*6 = 12.
    rt_launcher_cfg_t shallow = GAMING;
    shallow.act_pt            = 4;
    rt_sim_init(&s, 2, 2, &shallow);
    CHECK(s.key.regular.actn_pt == 24, "shallow actn_pt = %u, esperaba 24", s.key.regular.actn_pt);
    CHECK(s.key.regular.deactn_pt == 12, "shallow deactn_pt = %u, esperaba 12 (histeresis capada a act/2)", s.key.regular.deactn_pt);
}

// ---------------------------------------------------------------------------
// 2. Press y release basicos
// ---------------------------------------------------------------------------
static void test_basic_press_release(void) {
    rt_sim_t s;
    uint32_t t = 0;
    rt_sim_init(&s, 2, 2, &GAMING);
    hosttest_clock_set(0);

    rt_sim_ramp(&s, &t, 0, 150, 1); // bajar el dedo
    CHECK(s.n_edges == 1, "bajada: %u flancos, esperaba 1", s.n_edges);
    CHECK(s.n_edges >= 1 && s.edges[0].pressed, "el primer flanco deberia ser press");
    // Actua exactamente al cruzar actn_pt (120), no antes.
    CHECK(s.n_edges >= 1 && s.edges[0].t_ms == 120, "press en t=%u, esperaba 120", s.n_edges ? s.edges[0].t_ms : 0);

    rt_sim_ramp(&s, &t, 150, 0, 1); // soltar del todo
    CHECK(s.n_edges == 2, "subida: %u flancos, esperaba 2", s.n_edges);
    CHECK(s.n_edges >= 2 && !s.edges[1].pressed, "el segundo flanco deberia ser release");
}

// ---------------------------------------------------------------------------
// 3. Release por rapid trigger (sin soltar hasta la desactuacion estatica)
// ---------------------------------------------------------------------------
static void test_rapid_release(void) {
    rt_sim_t s;
    uint32_t t = 0;
    rt_sim_init(&s, 2, 2, &GAMING);
    hosttest_clock_set(0);

    rt_sim_ramp(&s, &t, 0, 150, 1);
    CHECK(s.n_edges == 1, "esperaba solo el press inicial, hay %u flancos", s.n_edges);

    // Subir 12 unidades (0.2 mm = sen_rls). Debe soltar AUNQUE 138 siga muy por
    // encima de la desactuacion estatica (90). Eso es el rapid trigger.
    rt_sim_ramp(&s, &t, 150, 138, 1);
    CHECK(s.n_edges == 2, "release RT: %u flancos, esperaba 2", s.n_edges);
    CHECK(s.n_edges >= 2 && !s.edges[1].pressed, "deberia ser un release");
    CHECK(s.key.state == AKS_RAPID_RELEASED, "estado %u, esperaba AKS_RAPID_RELEASED", s.key.state);
    CHECK(s.key.travel > s.key.regular.deactn_pt, "el travel deberia seguir sobre la desactuacion estatica");
}

// ---------------------------------------------------------------------------
// 4. Re-press por rapid trigger
// ---------------------------------------------------------------------------
static void test_rapid_repress(void) {
    rt_sim_t s;
    uint32_t t = 0;
    rt_sim_init(&s, 2, 2, &GAMING);
    hosttest_clock_set(0);

    rt_sim_ramp(&s, &t, 0, 150, 1);
    rt_sim_ramp(&s, &t, 150, 130, 1); // soltar hasta 130: release RT por el camino
    CHECK(s.n_edges == 2, "esperaba press+release, hay %u", s.n_edges);

    // Desde el minimo de 130, el re-press pide sen (18): 130 + 18 = 148.
    rt_sim_ramp(&s, &t, 130, 147, 1);
    CHECK(s.n_edges == 2, "a 147 todavia NO deberia haber re-press (%u flancos)", s.n_edges);

    rt_sim_ramp(&s, &t, 147, 148, 1);
    CHECK(s.n_edges == 3, "a 148 deberia haber re-press (%u flancos)", s.n_edges);
    CHECK(s.n_edges >= 3 && s.edges[2].pressed, "el tercer flanco deberia ser press");
}

// ---------------------------------------------------------------------------
// 5. Zona muerta del fondo (bottom guard)
// ---------------------------------------------------------------------------
// rt_dynamic_release_ready() exige travel < BOTTOM_DEAD_ZONE*TRAVEL_SCALE -
// sen_rls = 38*6 - 12 = 216. O sea: cerca del fondo, el release por rapid
// trigger NO dispara aunque el dedo haya subido lo suficiente. Es deliberado
// (anti-chatter al bottom-out) y afecta a quien machaca el espacio a fondo.
static void test_bottom_dead_zone(void) {
    rt_sim_t s;
    uint32_t t = 0;
    rt_sim_init(&s, 2, 2, &GAMING);
    hosttest_clock_set(0);

    rt_sim_ramp(&s, &t, 0, 230, 1); // hasta el fondo
    CHECK(s.n_edges == 1, "esperaba solo el press, hay %u", s.n_edges);

    // 230 - 12 = 218: por sensibilidad tocaria soltar, pero 218 >= 216 y el
    // guard lo impide.
    rt_sim_ramp(&s, &t, 230, 218, 1);
    CHECK(s.n_edges == 1, "a 218 el bottom guard deberia impedir el release (%u flancos)", s.n_edges);

    // Al bajar de 216 ya suelta.
    rt_sim_ramp(&s, &t, 218, 215, 1);
    CHECK(s.n_edges == 2, "por debajo de 216 deberia soltar (%u flancos)", s.n_edges);
}

// ---------------------------------------------------------------------------
// 6. Mantener pulsado no genera flancos
// ---------------------------------------------------------------------------
static void test_hold_is_quiet(void) {
    rt_sim_t s;
    uint32_t t = 0;
    rt_sim_init(&s, 2, 2, &GAMING);
    hosttest_clock_set(0);

    rt_sim_ramp(&s, &t, 0, 150, 1);
    const uint16_t after_press = s.n_edges;

    rt_sim_hold(&s, &t, 150, 500); // medio segundo quieto
    CHECK(s.n_edges == after_press, "mantener genero %u flancos de mas", s.n_edges - after_press);
}

// ---------------------------------------------------------------------------
// 7. W-tap: la ventana OFF que MC tiene que muestrear
// ---------------------------------------------------------------------------
// El test que conecta la FSM con la mecanica del juego. Un w-tap rapido produce
// una ventana OFF; si dura menos que el tick de 50 ms, el juego puede no verla y
// el sprint no se resetea. Aqui se mide esa ventana en el binario de torneo (sin
// F6), que es exactamente el numero que el drill D2 va a buscar en el teclado.
static void test_wtap_off_window(void) {
    rt_sim_t s;
    uint32_t t = 0;
    rt_sim_init(&s, 2, 2, &GAMING);
    hosttest_clock_set(0);

    rt_sim_ramp(&s, &t, 0, 150, 5);   // W abajo, rapido
    rt_sim_ramp(&s, &t, 150, 120, 5); // levantar el dedo un pelin
    rt_sim_ramp(&s, &t, 120, 150, 5); // y volver a bajar

    CHECK(s.n_edges >= 3, "un w-tap deberia dar press/release/press, hay %u flancos", s.n_edges);

    if (s.n_edges >= 3) {
        const uint32_t off_ms = s.edges[2].t_ms - s.edges[1].t_ms;
        printf("     [info] ventana OFF del w-tap simulado: %lu ms (tick MC = 50 ms)\n", (unsigned long)off_ms);
        // Sin F6 la ventana la fija el dedo, no el firmware. El test no juzga el
        // valor: documenta que es medible y deja el juicio al drill real.
        CHECK(off_ms > 0, "la ventana OFF deberia ser positiva");
    }
}

// ---------------------------------------------------------------------------
// 8. El early return cuando el travel no cambia
// ---------------------------------------------------------------------------
// update_raw_value() no llama a la FSM si el travel no se movio. Es
// load-bearing: la FSM no debe avanzar en barridos silenciosos.
static void test_unchanged_travel_is_inert(void) {
    rt_sim_t s;
    uint32_t t = 0;
    rt_sim_init(&s, 2, 2, &GAMING);
    hosttest_clock_set(0);

    rt_sim_ramp(&s, &t, 0, 150, 1);
    const uint8_t actn_before  = s.key.rapid.actn_pt;
    const uint8_t deactn_before = s.key.rapid.deactn_pt;

    rt_sim_hold(&s, &t, 150, 100);
    CHECK(s.key.rapid.actn_pt == actn_before, "rapid.actn_pt se movio en barridos sin cambio de travel");
    CHECK(s.key.rapid.deactn_pt == deactn_before, "rapid.deactn_pt se movio en barridos sin cambio de travel");
}

int main(void) {
    test_config_anchor();
    test_basic_press_release();
    test_rapid_release();
    test_rapid_repress();
    test_bottom_dead_zone();
    test_hold_is_quiet();
    test_wtap_off_window();
    test_unchanged_travel_is_inert();

    return hosttest_report("rapid_trigger");
}
