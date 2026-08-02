#include "harness.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

// Definiciones que el firmware pone en QMK y aqui tiene que poner alguien.
layer_state_t default_layer_state = 1; // capa 0 = Gaming por defecto
layer_state_t layer_state         = 0;

// El histograma lee el travel desde el analog_key_matrix global y desde
// analog_matrix_get_travel(). En el firmware los pone analog_matrix.c, que no se
// puede compilar en host (arrastra EEPROM, I2C y raw_hid). Aqui los define el
// harness: es un STUB DE DATOS, no de logica — el test escribe travel en la
// tabla y el codigo real lo lee igual que en el teclado.
analog_key_t analog_key_matrix[MATRIX_ROWS][MATRIX_COLS];

uint8_t analog_matrix_get_travel(uint8_t row, uint8_t col) {
    return analog_key_matrix[row][col].travel;
}

uint8_t analog_matrix_get_travel_checked(uint8_t row, uint8_t col) {
    if (row >= MATRIX_ROWS || col >= MATRIX_COLS) return 0;
    return analog_key_matrix[row][col].travel;
}

// ---------------------------------------------------------------------------
// Keymap falso
// ---------------------------------------------------------------------------
static uint16_t fake_keymap[MATRIX_ROWS][MATRIX_COLS];

void hosttest_keymap_clear(void) {
    memset(fake_keymap, 0, sizeof(fake_keymap)); // KC_NO
}

void hosttest_keymap_set(uint8_t row, uint8_t col, uint16_t keycode) {
    if (row >= MATRIX_ROWS || col >= MATRIX_COLS) return;
    fake_keymap[row][col] = keycode;
}

uint16_t keymap_key_to_keycode(uint8_t layer, keypos_t key) {
    (void)layer; // el harness solo modela una capa: la de politica
    if (key.row >= MATRIX_ROWS || key.col >= MATRIX_COLS) return KC_NO;
    return fake_keymap[key.row][key.col];
}

// ---------------------------------------------------------------------------
// Reloj falso
// ---------------------------------------------------------------------------
static uint16_t fake_now;

void hosttest_clock_set(uint16_t ms) {
    fake_now = ms;
}
void hosttest_clock_advance(uint16_t ms) {
    fake_now = (uint16_t)(fake_now + ms);
}
uint16_t timer_read(void) {
    return fake_now;
}

// Misma semantica que QMK: comparacion con resta sin signo, asi que el wrap de
// 16 bits se comporta igual que en el teclado. Eso importa: las ventanas de
// stretch guardan un deadline de 16 bits y el wrap cada ~65 s es un caso real
// que el firmware comenta explicitamente.
bool timer_expired(uint16_t current, uint16_t future) {
    return (uint16_t)(current - future) < 0x8000;
}

// ---------------------------------------------------------------------------
// Simulador de rapid trigger
// ---------------------------------------------------------------------------

// Escalado saturante, identico a scale_travel_u8() de analog_matrix.c.
static uint8_t scale_u8(uint8_t v) {
    uint16_t scaled = (uint16_t)v * TRAVEL_SCALE;
    return scaled > 255 ? 255 : (uint8_t)scaled;
}

// ESPEJO de update_key_config(). Es la unica duplicacion de logica del harness y
// esta aqui a proposito: los tests se leen en unidades de Launcher (0.1 mm) en
// vez de en unidades escaladas, que es como se razona sobre ellos.
//
// Si update_key_config() cambia su calculo de histeresis, esto hay que moverlo.
// test_rapid_trigger.c ancla el resultado con valores conocidos para que esa
// divergencia salga como test rojo y no como un A/B que miente.
void rt_sim_init(rt_sim_t *s, uint8_t row, uint8_t col, const rt_launcher_cfg_t *cfg) {
    memset(s, 0, sizeof(*s));

    s->key.r     = row;
    s->key.c     = col;
    s->key.mode  = AKM_RAPID;
    s->key.state = AKS_REGULAR_RELEASED;

    uint8_t act = cfg->act_pt;
    uint8_t hyst = cfg->gaming ? STATIC_HYSTERESIS_GAMING : STATIC_HYSTERESIS_TYPING;

#if ANALOG_ADAPTIVE_SHALLOW_HYSTERESIS_GAMING
    if (cfg->gaming) {
        const uint8_t max_shallow = act > 1 ? act / 2 : 0;
        if (hyst > max_shallow) hyst = max_shallow;
    }
#endif
    if (hyst == 0 && act > 0) hyst = 1;

    s->key.regular.actn_pt   = scale_u8(act);
    s->key.regular.deactn_pt = scale_u8(act > hyst ? act - hyst : 0);
    s->key.rpd_trig_sen      = scale_u8(cfg->sen);
    s->key.rpd_trig_sen_rls  = scale_u8(cfg->sen_rls ? cfg->sen_rls : cfg->sen);

    s->reported = false;
}

// ESPEJO de la cola de update_raw_value() + analog_matrix_get_key_state() para
// AKM_RAPID. El early return cuando travel no cambia es load-bearing: en el
// teclado la FSM no corre en esos barridos, y un simulador que la corriera
// siempre daria resultados que el firmware nunca produce.
void rt_sim_step(rt_sim_t *s, uint32_t t_ms, uint8_t travel) {
    s->key.travel = travel;

    if (travel != s->key.last_travel) {
        rapid_trigger_action(&s->key);
        s->key.last_travel = travel;
    }

    const bool now = (s->key.state == AKS_REGULAR_PRESSED || s->key.state == AKS_RAPID_PRESSED);
    if (now != s->reported) {
        if (s->n_edges < HOSTTEST_MAX_EDGES) {
            s->edges[s->n_edges].t_ms    = t_ms;
            s->edges[s->n_edges].pressed = now;
            s->n_edges++;
        } else {
            s->overflow = true;
        }
        s->reported = now;
    }
}

void rt_sim_ramp(rt_sim_t *s, uint32_t *t_ms, uint8_t from, uint8_t to, uint8_t step) {
    if (step == 0) step = 1;
    if (from <= to) {
        for (uint16_t v = from; v <= to; v += step) {
            rt_sim_step(s, (*t_ms)++, (uint8_t)v);
            hosttest_clock_advance(1);
        }
    } else {
        for (int16_t v = from; v >= (int16_t)to; v -= step) {
            rt_sim_step(s, (*t_ms)++, (uint8_t)v);
            hosttest_clock_advance(1);
        }
    }
}

void rt_sim_hold(rt_sim_t *s, uint32_t *t_ms, uint8_t travel, uint32_t ms) {
    for (uint32_t i = 0; i < ms; i++) {
        rt_sim_step(s, (*t_ms)++, travel);
        hosttest_clock_advance(1);
    }
}

// ---------------------------------------------------------------------------
// Aserciones
// ---------------------------------------------------------------------------
int hosttest_failures = 0;
int hosttest_checks   = 0;

void hosttest_check(bool cond, const char *file, int line, const char *fmt, ...) {
    hosttest_checks++;
    if (cond) return;

    hosttest_failures++;

    va_list ap;
    va_start(ap, fmt);
    fprintf(stderr, "  FALLA %s:%d  ", file, line);
    vfprintf(stderr, fmt, ap);
    fprintf(stderr, "\n");
    va_end(ap);
}

int hosttest_report(const char *suite) {
    if (hosttest_failures == 0) {
        printf("OK   %-28s %d comprobaciones\n", suite, hosttest_checks);
        return 0;
    }
    printf("FALLA %-28s %d/%d comprobaciones fallaron\n", suite, hosttest_failures, hosttest_checks);
    return 1;
}
