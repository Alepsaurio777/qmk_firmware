// Harness de host para la logica analogica del K2 HE.
//
// La idea de todo esto: la FSM del rapid trigger y los filtros de ventana son
// FUNCIONES PURAS de una serie temporal de travel. No tocan hardware. Asi que se
// pueden compilar en el PC y ejercitar de forma determinista — que es lo que el
// criterio de promocion #1 ("validada con datos, no con sensacion") presupone
// que existe.
//
// Regla dura: aqui NO se copia logica del firmware. Se compilan los .c REALES
// (action_rapid_trigger.c, action_stretch.c) contra los shims de shim/. Si un
// test pasa aqui, es sobre el mismo codigo que corre en el teclado.
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "analog_matrix.h"

// Las acciones por modo no estan declaradas en analog_matrix.h: analog_matrix.c
// las declara como `extern` locales suyas. Aqui hace falta la declaracion, y se
// pone con la firma EXACTA — si el firmware la cambia, esto deja de enlazar, que
// es la senal correcta.
bool rapid_trigger_action(analog_key_t *key);

// Tabla global de teclas. La define el harness (ver harness.c) y la consume el
// codigo real del histograma; los tests escriben travel aqui.
extern analog_key_t analog_key_matrix[MATRIX_ROWS][MATRIX_COLS];

// ---------------------------------------------------------------------------
// Keymap falso
// ---------------------------------------------------------------------------
// Las whitelists por keycode se resuelven contra el keymap vivo. En el teclado
// eso es la EEPROM de VIA; aqui lo sirve esta tabla, que el test rellena para
// decir "W esta en (2,2)". Asi se puede probar tambien el caso que rompio el
// diseno por coordenadas: remapear una tecla y ver si la politica la sigue.
void     hosttest_keymap_clear(void);
void     hosttest_keymap_set(uint8_t row, uint8_t col, uint16_t keycode);
uint16_t keymap_key_to_keycode(uint8_t layer, keypos_t key);

// ---------------------------------------------------------------------------
// Reloj falso
// ---------------------------------------------------------------------------
// Los stretches usan timer_read()/timer_expired. En host el tiempo lo mueve el
// test, no el reloj de pared: sin esto, probar una ventana de 55 ms costaria
// 55 ms de test y seria flaky.
void     hosttest_clock_set(uint16_t ms);
void     hosttest_clock_advance(uint16_t ms);
uint16_t timer_read(void);
bool     timer_expired(uint16_t current, uint16_t future);

// ---------------------------------------------------------------------------
// Simulador de una tecla bajo rapid trigger
// ---------------------------------------------------------------------------

#define HOSTTEST_MAX_EDGES 512

typedef struct {
    uint32_t t_ms;
    bool     pressed;
} hosttest_edge_t;

typedef struct {
    analog_key_t    key;
    hosttest_edge_t edges[HOSTTEST_MAX_EDGES];
    uint16_t        n_edges;
    bool            reported;      // ultimo estado reportado
    bool            overflow;      // se lleno el buffer de flancos
} rt_sim_t;

// Configuracion tal como la expresa Launcher: unidades de 0.1 mm, SIN escalar.
// rt_sim_init aplica el mismo escalado que update_key_config().
typedef struct {
    uint8_t act_pt;      // punto de actuacion (0.1 mm)
    uint8_t sen;         // sensibilidad RT de press (0.1 mm)
    uint8_t sen_rls;     // sensibilidad RT de release (0.1 mm); 0 = hereda sen
    bool    gaming;      // afecta a la histeresis (adaptativa en Gaming)
} rt_launcher_cfg_t;

void rt_sim_init(rt_sim_t *s, uint8_t row, uint8_t col, const rt_launcher_cfg_t *cfg);

// Un barrido: mete un travel y deja que la FSM reaccione. Replica el orden de
// update_raw_value() — incluido el early return cuando el travel no cambia, que
// es load-bearing: la FSM NO corre en esos barridos.
void rt_sim_step(rt_sim_t *s, uint32_t t_ms, uint8_t travel);

// Azucar: rampa lineal de travel entre dos valores, un paso por ms (el scan va
// anclado a SOF ~1 kHz, asi que 1 paso = 1 ms es la escala real).
void rt_sim_ramp(rt_sim_t *s, uint32_t *t_ms, uint8_t from, uint8_t to, uint8_t step);
void rt_sim_hold(rt_sim_t *s, uint32_t *t_ms, uint8_t travel, uint32_t ms);

// ---------------------------------------------------------------------------
// Aserciones
// ---------------------------------------------------------------------------
extern int hosttest_failures;
extern int hosttest_checks;

void hosttest_check(bool cond, const char *file, int line, const char *fmt, ...);

#define CHECK(cond, ...) hosttest_check((cond), __FILE__, __LINE__, __VA_ARGS__)

int hosttest_report(const char *suite);
