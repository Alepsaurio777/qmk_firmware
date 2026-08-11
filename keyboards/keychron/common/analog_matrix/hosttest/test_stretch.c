// Tests de F6 (release-stretch), F9 (press-stretch) y de la resolucion de la
// politica por keycode — sobre action_stretch.c REAL.
//
// Es el fichero que mas importa de los dos: las ventanas de F6/F9 son la mitad
// del argumento del proyecto sobre MC 1.8.9, y hasta ahora se validaban jugando
// y mirando histogramas a posteriori. Aqui la garantia se comprueba
// directamente: "tras un release fisico, el estado reportado queda OFF al menos
// 55 ms". Eso es una afirmacion sobre codigo, no sobre sensaciones.
//
// Se compila en las DOS configuraciones:
//   - torneo: los stretches son macros passthrough. El test comprueba que de
//     verdad no hacen nada, que es la garantia del binario minimo.
//   - lab: los stretches existen. El test comprueba las ventanas.

#include <stdio.h>

#include "harness.h"

// Coordenadas arbitrarias pero realistas del K2 HE (fila 2 = QWERTY, fila 5 =
// la del espacio). Lo que importa es que el keymap falso y la resolucion
// coincidan.
#define ROW_W 2
#define COL_W 2
#define ROW_SPC 5
#define COL_SPC 6
#define ROW_LSFT 4
#define COL_LSFT 0

// Espejo del encadenamiento de analog_matrix_scan.c. El ORDEN es load-bearing:
// F9 primero, F6 despues. Al reves, F6 veria el release fisico y abriria su
// ventana OFF peleando contra el ON que F9 sostiene.
static bool chain(uint8_t row, uint8_t col, bool physical) {
    bool p = analog_matrix_press_stretch_apply(row, col, physical);
    return analog_matrix_release_stretch_apply(row, col, p);
}

// Mantiene el estado fisico durante `ms` barridos (1 kHz => 1 barrido = 1 ms) y
// devuelve cuantos de ellos se reportaron como ON.
static uint32_t hold_and_count_on(uint8_t row, uint8_t col, bool physical, uint32_t ms) {
    uint32_t on = 0;
    for (uint32_t i = 0; i < ms; i++) {
        if (chain(row, col, physical)) on++;
        hosttest_clock_advance(1);
    }
    return on;
}

static void setup_keymap(void) {
    hosttest_keymap_clear();
    hosttest_keymap_set(ROW_W, COL_W, KC_W);
    hosttest_keymap_set(ROW_SPC, COL_SPC, KC_SPACE);
    hosttest_keymap_set(ROW_LSFT, COL_LSFT, KC_LEFT_SHIFT);
    hosttest_set_gaming();
    hosttest_clock_set(1000); // lejos de 0 para no depender del arranque
    analog_matrix_resolve_policy_keys();
}

// ---------------------------------------------------------------------------
// 1. Resolucion por keycode: la politica SIGUE a la tecla al remapearla
// ---------------------------------------------------------------------------
// Este es el test del diseno que sustituyo al hardcode por coordenadas. Antes,
// remapear W desde Launcher dejaba la politica en el hueco viejo en silencio.
#if ANALOG_POLICY_NEEDED
static void test_policy_follows_remap(void) {
    uint8_t dump[ANALOG_POLICY_DUMP_LEN];

    setup_keymap();
    analog_matrix_policy_dump(dump);

#    if ANALOG_RELEASE_STRETCH_IN_GAMING_MODE
    // out[25] = slot 0 de F6 (KC_W), empaquetado (row << 4) | col
    CHECK(dump[25] == ((ROW_W << 4) | COL_W), "F6 slot0 resuelto a 0x%02X, esperaba 0x%02X", dump[25], (ROW_W << 4) | COL_W);
    // out[26] = slot 1 de F6 (KC_SPACE)
    CHECK(dump[26] == ((ROW_SPC << 4) | COL_SPC), "F6 slot1 resuelto a 0x%02X, esperaba 0x%02X", dump[26], (ROW_SPC << 4) | COL_SPC);
#    endif
#    if ANALOG_PRESS_STRETCH_IN_GAMING_MODE
    // out[27] = slot 0 de F9 (KC_SPACE)
    CHECK(dump[27] == ((ROW_SPC << 4) | COL_SPC), "F9 slot0 resuelto a 0x%02X, esperaba 0x%02X", dump[27], (ROW_SPC << 4) | COL_SPC);
    // out[28] = slot 1 de F9 (KC_LEFT_SHIFT)
    CHECK(dump[28] == ((ROW_LSFT << 4) | COL_LSFT), "F9 slot1 resuelto a 0x%02X, esperaba 0x%02X", dump[28], (ROW_LSFT << 4) | COL_LSFT);
#    endif

    // Remapeo: W se va a otra posicion. Re-resolver debe llevarse la politica.
    hosttest_keymap_set(ROW_W, COL_W, KC_NO);
    hosttest_keymap_set(3, 9, KC_W);
    analog_matrix_resolve_policy_keys();
    analog_matrix_policy_dump(dump);

#    if ANALOG_RELEASE_STRETCH_IN_GAMING_MODE
    CHECK(dump[25] == ((3 << 4) | 9), "tras remapear, F6 slot0 = 0x%02X, esperaba 0x%02X", dump[25], (3 << 4) | 9);
#    endif

    // Keycode ausente del keymap: el slot queda SIN resolver (0xFF), no
    // apuntando a basura.
    hosttest_keymap_set(3, 9, KC_NO);
    analog_matrix_resolve_policy_keys();
    analog_matrix_policy_dump(dump);
#    if ANALOG_RELEASE_STRETCH_IN_GAMING_MODE
    CHECK(dump[25] == 0xFF, "keycode ausente deberia dejar el slot en 0xFF, hay 0x%02X", dump[25]);
#    endif
}
#endif

// ---------------------------------------------------------------------------
// 2. F6: la ventana OFF minima tras un release fisico
// ---------------------------------------------------------------------------
static void test_release_stretch_window(void) {
    setup_keymap();

    // W pulsada y estable.
    hold_and_count_on(ROW_W, COL_W, true, 20);
    CHECK(chain(ROW_W, COL_W, true), "W pulsada deberia reportarse ON");

    // Release fisico, e inmediatamente (5 ms) re-press: un w-tap agresivo.
    hold_and_count_on(ROW_W, COL_W, false, 5);

    // Ahora el dedo esta ABAJO otra vez. Sin F6 el estado reportado volveria a
    // ON de inmediato y la ventana OFF habria durado 5 ms — invisible para un
    // tick de 50 ms. Con F6 tiene que seguir OFF hasta completar 55 ms.
#if ANALOG_RELEASE_STRETCH_IN_GAMING_MODE
    const uint32_t on_during = hold_and_count_on(ROW_W, COL_W, true, 45);
    CHECK(on_during == 0, "F6: durante la ventana OFF se reportaron %lu ms en ON, esperaba 0", (unsigned long)on_during);

    // A los 55 ms del release fisico, el press real ya pasa.
    hosttest_clock_advance(6);
    CHECK(chain(ROW_W, COL_W, true), "F6: pasados 55 ms el press fisico deberia pasar");
#else
    // Torneo: passthrough puro. El press vuelve de inmediato.
    CHECK(chain(ROW_W, COL_W, true), "torneo: sin F6 el press deberia pasar de inmediato");
#endif
}

// ---------------------------------------------------------------------------
// 3. F6 no retrasa el release, solo el press siguiente
// ---------------------------------------------------------------------------
// La semantica es de debounce, no de latencia anadida al soltar. Si F6
// retrasara el OFF, en un borde de sumo seria mortal.
static void test_release_stretch_does_not_delay_off(void) {
    setup_keymap();
    hold_and_count_on(ROW_W, COL_W, true, 20);

    const bool reported = chain(ROW_W, COL_W, false); // primer barrido tras soltar
    CHECK(!reported, "el OFF debe reportarse en el MISMO barrido del release fisico");
}

// ---------------------------------------------------------------------------
// 4. F9: la ventana ON minima tras un press fisico
// ---------------------------------------------------------------------------
static void test_press_stretch_window(void) {
    setup_keymap();

    // Espacio en reposo.
    hold_and_count_on(ROW_SPC, COL_SPC, false, 20);

    // Tap de 5 ms: mas rapido que un tick de MC.
    CHECK(chain(ROW_SPC, COL_SPC, true), "el press deberia reportarse ON de inmediato");
    hosttest_clock_advance(1);
    hold_and_count_on(ROW_SPC, COL_SPC, true, 4);

#if ANALOG_PRESS_STRETCH_IN_GAMING_MODE
    // Dedo arriba, pero F9 sostiene el ON. Sin esto el salto no existio.
    const uint32_t on_after = hold_and_count_on(ROW_SPC, COL_SPC, false, 45);
    CHECK(on_after == 45, "F9: tras soltar se reportaron %lu ms en ON de 45 esperados", (unsigned long)on_after);
#else
    const uint32_t on_after = hold_and_count_on(ROW_SPC, COL_SPC, false, 45);
    CHECK(on_after == 0, "torneo: sin F9 el OFF deberia ser inmediato, hubo %lu ms de ON", (unsigned long)on_after);
#endif
}

// El segundo slot usa la misma FSM, pero necesita una prueba propia para que el
// espejo de config, la resolucion y el array de dos slots no puedan divergir.
static void test_press_stretch_lshift_window(void) {
    setup_keymap();
    hold_and_count_on(ROW_LSFT, COL_LSFT, false, 20);

    CHECK(chain(ROW_LSFT, COL_LSFT, true), "LShift deberia reportar el press de inmediato");
    hosttest_clock_advance(1);
    hold_and_count_on(ROW_LSFT, COL_LSFT, true, 4);

#if ANALOG_PRESS_STRETCH_IN_GAMING_MODE
    const uint32_t on_after = hold_and_count_on(ROW_LSFT, COL_LSFT, false, 45);
    CHECK(on_after == 45, "F9 LShift: tras soltar se reportaron %lu ms en ON de 45 esperados", (unsigned long)on_after);
#else
    const uint32_t on_after = hold_and_count_on(ROW_LSFT, COL_LSFT, false, 45);
    CHECK(on_after == 0, "torneo: LShift debe ser passthrough, hubo %lu ms de ON", (unsigned long)on_after);
#endif
}

// ---------------------------------------------------------------------------
// 5. La cadena F9 -> F6 en el espacio: 55 ms ON y luego 55 ms OFF
// ---------------------------------------------------------------------------
// El numero que el propio DEVELOPMENT.md cita como "110 ms de ciclo en el peor
// caso". Aqui deja de ser un razonamiento y pasa a ser una comprobacion.
#if ANALOG_PRESS_STRETCH_IN_GAMING_MODE && ANALOG_RELEASE_STRETCH_IN_GAMING_MODE
static void test_press_then_release_stretch_chain(void) {
    setup_keymap();
    hold_and_count_on(ROW_SPC, COL_SPC, false, 20);

    // Tap fisico de 5 ms.
    uint32_t on_total = 0;
    if (chain(ROW_SPC, COL_SPC, true)) on_total++;
    hosttest_clock_advance(1);
    on_total += hold_and_count_on(ROW_SPC, COL_SPC, true, 4);

    // Dedo arriba el resto del tiempo. F9 sostiene el ON hasta 55 ms desde el
    // press; despues F6 abre su ventana OFF.
    on_total += hold_and_count_on(ROW_SPC, COL_SPC, false, 60);

    CHECK(on_total == 55, "cadena F9->F6: %lu ms reportados en ON, esperaba 55", (unsigned long)on_total);

    // Y durante los 55 ms siguientes, un re-press fisico no debe pasar.
    const uint32_t on_in_off_window = hold_and_count_on(ROW_SPC, COL_SPC, true, 40);
    CHECK(on_in_off_window == 0, "cadena F9->F6: %lu ms de ON dentro de la ventana OFF, esperaba 0", (unsigned long)on_in_off_window);

    printf("     [info] tap de 5 ms -> 55 ms ON + >=55 ms OFF (ciclo de 110 ms, tick MC = 50)\n");
}

// ---------------------------------------------------------------------------
// 6. Consecuencia documentada: dos taps muy rapidos se FUNDEN en uno
// ---------------------------------------------------------------------------
// No es un bug, es aritmetica de F9, y conviene tenerlo pinado para que nadie se
// sorprenda: dos saltos separados por menos de la ventana ON salen como un solo
// ON continuo. En 1.8.9 no cuesta nada (jumpTicks limita el ritmo de salto muy
// por debajo), pero si alguna vez se sube ANALOG_PRESS_STRETCH_MS, este test es
// el que avisa de lo que se esta comprando.
static void test_fast_double_tap_merges(void) {
    setup_keymap();
    hold_and_count_on(ROW_SPC, COL_SPC, false, 20);

    bool saw_off_between = false;

    chain(ROW_SPC, COL_SPC, true); // tap 1
    hosttest_clock_advance(1);
    hold_and_count_on(ROW_SPC, COL_SPC, true, 4);

    for (int i = 0; i < 5; i++) { // dedo arriba 5 ms
        if (!chain(ROW_SPC, COL_SPC, false)) saw_off_between = true;
        hosttest_clock_advance(1);
    }

    chain(ROW_SPC, COL_SPC, true); // tap 2
    hosttest_clock_advance(1);
    hold_and_count_on(ROW_SPC, COL_SPC, true, 4);

    CHECK(!saw_off_between, "F9 deberia haber tapado el hueco entre dos taps rapidos");
}
#endif

// ---------------------------------------------------------------------------
// 7. Fuera de Gaming los filtros no aplican
// ---------------------------------------------------------------------------
static void test_inert_outside_gaming(void) {
    setup_keymap();
    hosttest_set_windows(); // interruptor a Windows

    hold_and_count_on(ROW_SPC, COL_SPC, true, 10);
    const uint32_t on_after = hold_and_count_on(ROW_SPC, COL_SPC, false, 30);
    CHECK(on_after == 0, "fuera de Gaming no deberia haber stretch, hubo %lu ms de ON", (unsigned long)on_after);

    hosttest_set_gaming();
}

int main(void) {
#if ANALOG_POLICY_NEEDED
    test_policy_follows_remap();
#endif
    test_release_stretch_window();
    test_release_stretch_does_not_delay_off();
    test_press_stretch_window();
    test_press_stretch_lshift_window();
#if ANALOG_PRESS_STRETCH_IN_GAMING_MODE && ANALOG_RELEASE_STRETCH_IN_GAMING_MODE
    test_press_then_release_stretch_chain();
    test_fast_double_tap_merges();
#endif
    test_inert_outside_gaming();

    return hosttest_report("stretch/policy");
}
