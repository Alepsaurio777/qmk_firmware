/* Copyright 2024 ~ 2025 @ Keychron (https://www.keychron.com)
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

// ===========================================================================
// Capa de POLITICA del analog matrix
// ===========================================================================
// (1-ago) Extraido de analog_matrix.c, donde vivia embebido en 1700 lineas de
// calibracion, EEPROM y despacho de Raw HID. Lo que hay aqui es un bloque
// coherente y auto-contenido:
//
//   - F6 (release-stretch) y F9 (press-stretch): filtros de la capa de REPORTE.
//   - Las mascaras de politica por keycode (continuous RT, RT predictivo).
//   - analog_matrix_resolve_policy_keys(): traduce keycodes a posiciones
//     leyendo el keymap vivo.
//   - El volcado de diagnostico de la politica resuelta.
//
// El motivo de separarlo no es estetico: TODO esto es funcion pura de una serie
// temporal de travel mas un keymap, o sea exactamente lo que se puede compilar
// y ejercitar en el PC. Mientras vivia dentro de analog_matrix.c era intocable
// para un test de host, porque arrastraba EEPROM, I2C, perfiles y raw_hid.
// Ahora hosttest/ lo compila tal cual y las ventanas de F6/F9 —que son la mitad
// del argumento sobre MC 1.8.9— pasan a tener tests deterministas.
//
// No se cambio ni una linea de logica en el traslado. El orden de llamada
// F9 -> F6 sigue siendo load-bearing y sigue viviendo en analog_matrix_scan.c.

#include <string.h>

#include "analog_matrix.h"
// keymap_key_to_keycode() — la resolucion por keycode lee el keymap VIVO (el
// dinamico de VIA en EEPROM, no los defaults de PROGMEM). En analog_matrix.c
// esto colaba via quantum.h; aqui se declara la dependencia real y minima, que
// es lo que permite que hosttest/ la sustituya por un keymap falso.
#include "keymap_common.h"
#include "timer.h"

#if ANALOG_RELEASE_STRETCH_IN_GAMING_MODE
#    if ANALOG_PRESS_STRETCH_IN_GAMING_MODE
static inline int8_t press_stretch_slot(uint8_t row, uint8_t col); // definido mas abajo
#    endif

// El flanco FISICO lo reporta el PRIMER filtro de la cadena (F9 -> F6), que es
// el unico que ve el estado sin alterar. Si F9 cubre esta tecla, F6 se calla:
// lo que F6 recibe ya viene estirado por F9, y reportarlo daria un flanco falso
// desplazado hasta ANALOG_PRESS_STRETCH_MS.
static inline bool physical_edge_owned_by_press_stretch(uint8_t row, uint8_t col) {
#    if ANALOG_PRESS_STRETCH_IN_GAMING_MODE
    return press_stretch_slot(row, col) >= 0;
#    else
    (void)row;
    (void)col;
    return false;
#    endif
}

// F6: minimo de tiempo OFF *reportado* tras un release fisico (racional en
// analog_matrix.h). Vive en la capa de reporte — la FSM del RT y el travel
// siguen intactos — y se aplica en los dos paths de escaneo, que llaman aqui
// en cada barrido aunque el travel no cambie (la FSM no; por eso el press
// diferido no puede implementarse dentro de rapid_trigger_action). Estado por
// slot de whitelist, no por tecla: son 2 teclas fijadas en compile-time.
typedef struct {
    uint16_t deadline;   // timer_read() en el que expira la ventana OFF
    bool     stretching; // ventana activa: el press fisico se reporta OFF
    // Estado previo TAL COMO LO VE ESTE FILTRO — que no siempre es el dedo.
    // (1-ago) Antes decia "estado FISICO previo", y para la unica tecla que
    // lleva los dos filtros (el espacio) era FALSO: la cadena es F9 -> F6
    // (analog_matrix_scan.c), asi que lo que F6 recibe ya viene estirado por F9
    // y esto sigue la SALIDA DE F9, no el flanco fisico. Para W (solo F6) si
    // coincide con el dedo. Es intencionado —es como se apilan las dos ventanas
    // hasta 110 ms— y es justo por eso que existe
    // physical_edge_owned_by_press_stretch(): para que F6 no reporte un flanco
    // "fisico" que ya viene desplazado.
    bool prev_pressed;
} release_stretch_t;

static release_stretch_t release_stretch[2];

// Posicion resuelta de cada slot (0xFF = slot apagado o keycode no encontrado).
// La rellena analog_matrix_resolve_policy_keys() desde el keymap vivo.
static uint8_t release_stretch_row[2] = {0xFF, 0xFF};
static uint8_t release_stretch_col[2] = {0xFF, 0xFF};

// Keycode con el que se declaro cada slot. Fuente unica: lo usan la resolucion
// y el hook de flanco fisico, que necesita identificar la tecla sin volver a
// leer el keymap.
static const uint16_t release_stretch_keycodes[2] = {ANALOG_RELEASE_STRETCH_KEY1_KEYCODE, ANALOG_RELEASE_STRETCH_KEY2_KEYCODE};

static inline int8_t release_stretch_slot(uint8_t row, uint8_t col) {
    // 0xFF nunca es una fila valida, asi que un slot apagado no coincide nunca.
    for (uint8_t i = 0; i < 2; i++) {
        if (release_stretch_row[i] == row && release_stretch_col[i] == col) return (int8_t)i;
    }
    return -1;
}

bool analog_matrix_release_stretch_apply(uint8_t row, uint8_t col, bool pressed) {
    int8_t slot = release_stretch_slot(row, col);
    if (slot < 0) return pressed;

    release_stretch_t *s = &release_stretch[slot];

    // Fuera de Gaming el filtro no aplica. Limpiar aqui evita arrastrar una
    // ventana a medias al volver a Gaming (su deadline de 16 bits, tras >32 s
    // de wrap, volveria a parecer futura y colaria una supresion espuria).
    if (!analog_matrix_is_gaming_mode()) {
        s->stretching   = false;
        s->prev_pressed = pressed;
        return pressed;
    }

    if (s->prev_pressed && !pressed) {
        // Flanco de release fisico: abrir (o re-abrir) la ventana OFF minima.
        s->deadline   = timer_read() + ANALOG_RELEASE_STRETCH_MS;
        s->stretching = true;
    }
    if (s->prev_pressed != pressed && !physical_edge_owned_by_press_stretch(row, col)) {
        analog_matrix_physical_edge_hook(release_stretch_keycodes[slot], pressed, row, col);
    }
    s->prev_pressed = pressed;

    if (s->stretching) {
        if (!timer_expired(timer_read(), s->deadline)) return false;
        s->stretching = false; // expirada: el press fisico (si sigue ahi) pasa ya
    }

    return pressed;
}
#endif

#if ANALOG_POLICY_NEEDED
// Weak por defecto: el binario sin telemetria no paga nada y common/ no aprende
// nada del keymap. Solo se llama en flancos fisicos de teclas con stretch (2 o 3
// teclas), nunca por barrido.
__attribute__((weak)) void analog_matrix_physical_edge_hook(uint16_t keycode, bool pressed, uint8_t row, uint8_t col) {
    (void)keycode;
    (void)pressed;
    (void)row;
    (void)col;
}
#endif

#if ANALOG_PRESS_STRETCH_IN_GAMING_MODE
// F9: minimo de tiempo ON *reportado* tras un press fisico (racional en
// analog_matrix.h). Mismo sitio y mismas reglas que F6 — capa de reporte, la FSM
// y el travel intactos — pero en el flanco opuesto: sostiene el ON en vez de el
// OFF. Dos slots experimentales: espacio y LShift.
typedef struct {
    uint16_t deadline;     // timer_read() en el que expira la ventana ON
    bool     stretching;   // ventana activa: el release fisico se reporta ON
    bool     prev_pressed; // estado FISICO previo (detecta el flanco de press)
} press_stretch_t;

static press_stretch_t press_stretch[PRESS_STRETCH_SLOTS];
static uint8_t         press_stretch_row[PRESS_STRETCH_SLOTS] = {[0 ... PRESS_STRETCH_SLOTS - 1] = 0xFF};
static uint8_t         press_stretch_col[PRESS_STRETCH_SLOTS] = {[0 ... PRESS_STRETCH_SLOTS - 1] = 0xFF};

static const uint16_t press_stretch_keycodes[PRESS_STRETCH_SLOTS] = {ANALOG_PRESS_STRETCH_KEY1_KEYCODE, ANALOG_PRESS_STRETCH_KEY2_KEYCODE};

static inline int8_t press_stretch_slot(uint8_t row, uint8_t col) {
    // 0xFF nunca es una fila valida, asi que un slot apagado no coincide nunca.
    for (uint8_t i = 0; i < PRESS_STRETCH_SLOTS; i++) {
        if (press_stretch_row[i] == row && press_stretch_col[i] == col) return (int8_t)i;
    }
    return -1;
}

bool analog_matrix_press_stretch_apply(uint8_t row, uint8_t col, bool pressed) {
    int8_t slot = press_stretch_slot(row, col);
    if (slot < 0) return pressed;

    press_stretch_t *s = &press_stretch[slot];

    // Fuera de Gaming el filtro no aplica. Limpiar aqui evita arrastrar una
    // ventana a medias al volver (su deadline de 16 bits, tras >32 s de wrap,
    // volveria a parecer futura y colaria un ON espurio).
    if (!analog_matrix_is_gaming_mode()) {
        s->stretching   = false;
        s->prev_pressed = pressed;
        return pressed;
    }

    if (!s->prev_pressed && pressed) {
        // Flanco de press fisico: abrir (o re-abrir) la ventana ON minima.
        s->deadline   = timer_read() + ANALOG_PRESS_STRETCH_MS;
        s->stretching = true;
    }
    if (s->prev_pressed != pressed) analog_matrix_physical_edge_hook(press_stretch_keycodes[slot], pressed, row, col);
    s->prev_pressed = pressed;

    if (s->stretching) {
        if (!timer_expired(timer_read(), s->deadline)) return true;
        s->stretching = false; // expirada: el estado fisico manda ya
    }

    return pressed;
}
#endif

#if ANALOG_POLICY_NEEDED
#    if ANALOG_CONTINUOUS_RAPID_TRIGGER_IN_GAMING_MODE
matrix_row_t analog_continuous_rt_mask[MATRIX_ROWS];
#    endif
#    if ANALOG_PREDICTIVE_ACTUATION_IN_GAMING_MODE
matrix_row_t analog_predictive_press_mask[MATRIX_ROWS];
matrix_row_t analog_predictive_repress_mask[MATRIX_ROWS];

// El orden fija el bit de slot que direccionan las mascaras F7 (bit i = KEYi+1).
static const uint16_t predictive_rt_keycodes[6] = {
    ANALOG_PREDICTIVE_RT_KEY1_KEYCODE, ANALOG_PREDICTIVE_RT_KEY2_KEYCODE, ANALOG_PREDICTIVE_RT_KEY3_KEYCODE,
    ANALOG_PREDICTIVE_RT_KEY4_KEYCODE, ANALOG_PREDICTIVE_RT_KEY5_KEYCODE, ANALOG_PREDICTIVE_RT_KEY6_KEYCODE,
};
#    endif

// Traduce las whitelists declaradas por keycode a posiciones de matriz, leyendo
// el keymap VIVO: keymap_key_to_keycode() cae en keycode_at_keymap_location(),
// que con VIA/Launcher habilitado lee el keymap dinamico de la EEPROM en vez de
// los defaults de PROGMEM. Asi un remap desde Launcher se lleva la politica con
// la tecla en vez de dejarla en el hueco viejo.
//
// Corre en update_travel_configs() — o sea en boot, cambio de perfil y giro del
// interruptor (via housekeeping del keymap), nunca en el barrido. Un remap en
// caliente desde Launcher NO la dispara: la politica se recoloca en el siguiente
// de esos tres eventos.
void analog_matrix_resolve_policy_keys(void) {
#    if ANALOG_CONTINUOUS_RAPID_TRIGGER_IN_GAMING_MODE
    memset(analog_continuous_rt_mask, 0, sizeof(analog_continuous_rt_mask));
#    endif
#    if ANALOG_PREDICTIVE_ACTUATION_IN_GAMING_MODE
    memset(analog_predictive_press_mask, 0, sizeof(analog_predictive_press_mask));
    memset(analog_predictive_repress_mask, 0, sizeof(analog_predictive_repress_mask));
#    endif
#    if ANALOG_RELEASE_STRETCH_IN_GAMING_MODE
    // Reresolver invalida cualquier ventana OFF a medias: sus coordenadas pueden
    // haber cambiado, y un deadline heredado suprimiria un press en otra tecla.
    memset(release_stretch, 0, sizeof(release_stretch));
    for (uint8_t i = 0; i < 2; i++) {
        release_stretch_row[i] = 0xFF;
        release_stretch_col[i] = 0xFF;
    }
#    endif
#    if ANALOG_PRESS_STRETCH_IN_GAMING_MODE
    memset(press_stretch, 0, sizeof(press_stretch));
    for (uint8_t i = 0; i < PRESS_STRETCH_SLOTS; i++) {
        press_stretch_row[i] = 0xFF;
        press_stretch_col[i] = 0xFF;
    }
#    endif

    for (uint8_t row = 0; row < MATRIX_ROWS; row++) {
        for (uint8_t col = 0; col < MATRIX_COLS; col++) {
            const keypos_t pos = {.row = row, .col = col};
            const uint16_t kc  = keymap_key_to_keycode(ANALOG_POLICY_LAYER, pos);
            if (kc == KC_NO || kc == KC_TRANSPARENT) continue; // hueco o herencia: no es una tecla

            const matrix_row_t bit = (matrix_row_t)1 << col;

#    if ANALOG_CONTINUOUS_RAPID_TRIGGER_IN_GAMING_MODE
            if (kc == ANALOG_CONTINUOUS_RT_KEY1_KEYCODE || kc == ANALOG_CONTINUOUS_RT_KEY2_KEYCODE) {
                analog_continuous_rt_mask[row] |= bit;
            }
#    endif

#    if ANALOG_PREDICTIVE_ACTUATION_IN_GAMING_MODE
            for (uint8_t slot = 0; slot < 6; slot++) {
                if (predictive_rt_keycodes[slot] == KC_NO || kc != predictive_rt_keycodes[slot]) continue;
                if ((ANALOG_PREDICTIVE_PRESS_KEY_MASK >> slot) & 1) analog_predictive_press_mask[row] |= bit;
                if ((ANALOG_PREDICTIVE_REPRESS_KEY_MASK >> slot) & 1) analog_predictive_repress_mask[row] |= bit;
            }
#    endif

#    if ANALOG_RELEASE_STRETCH_IN_GAMING_MODE
            for (uint8_t i = 0; i < 2; i++) {
                // Primera coincidencia gana: el estado del stretch es por slot,
                // asi que dos posiciones con el mismo keycode no pueden
                // compartirlo sin pisarse la ventana OFF.
                if (release_stretch_keycodes[i] == KC_NO || kc != release_stretch_keycodes[i]) continue;
                if (release_stretch_row[i] != 0xFF) continue;
                release_stretch_row[i] = row;
                release_stretch_col[i] = col;
            }
#    endif

#    if ANALOG_PRESS_STRETCH_IN_GAMING_MODE
            // Primera coincidencia gana, mismo motivo que F6: el estado vive por
            // slot y dos posiciones no pueden compartir la ventana ON.
            for (uint8_t i = 0; i < PRESS_STRETCH_SLOTS; i++) {
                if (press_stretch_keycodes[i] == KC_NO || kc != press_stretch_keycodes[i]) continue;
                if (press_stretch_row[i] != 0xFF) continue;
                press_stretch_row[i] = row;
                press_stretch_col[i] = col;
            }
#    endif
        }
    }
}

// Empaqueta una coordenada resuelta en un byte. Ver el layout en analog_matrix.h.
static inline uint8_t policy_pack_coord(uint8_t row, uint8_t col) {
    if (row >= MATRIX_ROWS || col >= MATRIX_COLS) return 0xFF;
    return (uint8_t)((row << 4) | col);
}

static inline void policy_pack_mask(uint8_t *out, const matrix_row_t *mask) {
    for (uint8_t r = 0; r < MATRIX_ROWS; r++) {
        const uint16_t v = mask ? (uint16_t)mask[r] : 0;
        out[r * 2]       = (uint8_t)(v & 0xFF);
        out[r * 2 + 1]   = (uint8_t)(v >> 8);
    }
}

void analog_matrix_policy_dump(uint8_t *out) {
    memset(out, 0, ANALOG_POLICY_DUMP_LEN);

    out[0] = (uint8_t)((ANALOG_PREDICTIVE_ACTUATION_IN_GAMING_MODE ? 0x01 : 0) | (ANALOG_RELEASE_STRETCH_IN_GAMING_MODE ? 0x02 : 0) | (ANALOG_PRESS_STRETCH_IN_GAMING_MODE ? 0x04 : 0) | (ANALOG_CONTINUOUS_RAPID_TRIGGER_IN_GAMING_MODE ? 0x08 : 0));

#if ANALOG_PREDICTIVE_ACTUATION_IN_GAMING_MODE
    policy_pack_mask(&out[1], analog_predictive_press_mask);
    policy_pack_mask(&out[13], analog_predictive_repress_mask);
#else
    policy_pack_mask(&out[1], NULL);
    policy_pack_mask(&out[13], NULL);
#endif

#if ANALOG_RELEASE_STRETCH_IN_GAMING_MODE
    out[25] = policy_pack_coord(release_stretch_row[0], release_stretch_col[0]);
    out[26] = policy_pack_coord(release_stretch_row[1], release_stretch_col[1]);
#else
    out[25] = 0xFF;
    out[26] = 0xFF;
#endif

#if ANALOG_PRESS_STRETCH_IN_GAMING_MODE
    out[27] = policy_pack_coord(press_stretch_row[0], press_stretch_col[0]);
    out[28] = policy_pack_coord(press_stretch_row[1], press_stretch_col[1]);
#else
    out[27] = 0xFF;
    out[28] = 0xFF;
#endif
}
#endif
