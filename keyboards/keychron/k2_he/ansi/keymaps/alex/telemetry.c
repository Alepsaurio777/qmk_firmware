#include QMK_KEYBOARD_H
#include "analog_matrix.h"
#include "bottom_out_confidence.h"
#include "window_histogram.h"
#include "raw_hid.h"
#include "telemetry.h"

// Formato del paquete (32 bytes, endpoint Raw HID de VIA):
//   [0] 0xED magic  [1] version  [2..3] timer_read16 LE  [4] seq
//   [5] numero de teclas N
//   [6 + i*2]     travel de la tecla i (0..240, unidades de 0.1mm/TRAVEL_SCALE)
//   [6 + i*2 + 1] estado logico (1 = registrada como pulsada)
// v2 (con USB_SOF_TIMING_PROBE):
//   [18..21] contador de barridos completos, LE (el cliente deriva scans/s)
//   [22..23] duracion del ultimo barrido en us, LE
//   [24..25] fase fin-de-barrido -> ultimo SOF en us, LE (0..999 esperado)
#define TELEMETRY_MAGIC 0xED
#if defined(USB_SOF_TIMING_PROBE)
#    define TELEMETRY_VERSION 2
extern volatile uint32_t scan_probe_count;
extern volatile uint16_t scan_probe_duration_us;
extern volatile uint16_t scan_probe_phase_us;
#else
#    define TELEMETRY_VERSION 1
#endif
// 5 ms = 200 Hz: sobra para tunear umbrales y no compite con el trafico VIA.
#define TELEMETRY_INTERVAL_MS 5
// Tamano del endpoint Raw HID de VIA (32 bytes).
#define TELEMETRY_EPSIZE 32

// Teclas criticas de PvP 1.8.9, declaradas por KEYCODE y no por coordenada
// (24-jul). El orden fija el indice que el cliente rotula en KEYS[], asi que no
// reordenar sin tocar tools/telemetry_client.py.
//
// Antes era una tabla de {row, col} fija. Eso mentia en cuanto Launcher
// remapeaba: el stream de travel seguia leyendo el hueco viejo sin avisar. Y con
// la re-resolucion en caliente, remapear a mitad de sesion es una accion
// soportada, asi que la mentira era alcanzable de verdad.
static const uint16_t telemetry_keycodes[] = {
    KC_W, KC_A, KC_S, KC_D, KC_SPC, KC_LSFT,
};
#define TELEMETRY_KEY_COUNT ARRAY_SIZE(telemetry_keycodes)

// Las teclas ocupan [6 .. 6 + N*2), y la metadata v2 empieza en [18]. Con 6
// teclas queda justo a ras (6 + 12 = 18): una septima corromperia el timing en
// silencio y el sintoma apareceria en el cliente, no aqui. Hermano del assert
// del evlog mas abajo.
STATIC_ASSERT(6 + TELEMETRY_KEY_COUNT * 2 <= 18, "Las teclas de travel invaden la metadata v2");

// Posiciones resueltas (0xFF = ese keycode no esta en la capa base de Gaming).
static uint8_t telemetry_key_row[TELEMETRY_KEY_COUNT];
static uint8_t telemetry_key_col[TELEMETRY_KEY_COUNT];

// Se resuelve contra la MISMA capa que la politica del analog matrix
// (ANALOG_POLICY_LAYER = base de Gaming): las teclas que interesan son las de
// PvP, y su identidad la da esa capa aunque el stream de travel corra en Win.
//
// Barre la matriz leyendo el keymap vivo, asi que corre en boot y tras un remap
// —nunca en el barrido. La llama keymap.c.
void telemetry_resolve_keys(void) {
    for (uint8_t i = 0; i < TELEMETRY_KEY_COUNT; i++) {
        telemetry_key_row[i] = 0xFF;
        telemetry_key_col[i] = 0xFF;
    }

    for (uint8_t row = 0; row < MATRIX_ROWS; row++) {
        for (uint8_t col = 0; col < MATRIX_COLS; col++) {
            const keypos_t pos = {.row = row, .col = col};
            const uint16_t kc  = keymap_key_to_keycode(ANALOG_POLICY_LAYER, pos);
            if (kc == KC_NO || kc == KC_TRANSPARENT) continue;
            for (uint8_t i = 0; i < TELEMETRY_KEY_COUNT; i++) {
                // Primera coincidencia gana: un slot es una posicion.
                if (kc != telemetry_keycodes[i] || telemetry_key_row[i] != 0xFF) continue;
                telemetry_key_row[i] = row;
                telemetry_key_col[i] = col;
            }
        }
    }
}

static bool     telemetry_active = false;
static uint16_t last_send        = 0;
static uint8_t  seq              = 0;

void telemetry_task(void) {
    if (!telemetry_active) return;
    // Corte automatico si el interruptor fisico pasa a Gaming a mitad de stream.
    if (analog_matrix_is_gaming_mode()) {
        telemetry_active = false;
        return;
    }
    if (timer_elapsed(last_send) < TELEMETRY_INTERVAL_MS) return;
    last_send = timer_read();

    uint8_t pkt[TELEMETRY_EPSIZE] = {0};

    pkt[0] = TELEMETRY_MAGIC;
    pkt[1] = TELEMETRY_VERSION;

    uint16_t now = timer_read();
    pkt[2] = now & 0xFF;
    pkt[3] = now >> 8;
    pkt[4] = seq++;
    pkt[5] = TELEMETRY_KEY_COUNT;

    for (uint8_t i = 0; i < TELEMETRY_KEY_COUNT; i++) {
        const uint8_t r = telemetry_key_row[i];
        const uint8_t c = telemetry_key_col[i];
        // Sin resolver: reportar cero en vez de leer fuera de rango. El chequeo
        // se queda aqui en vez de usar analog_matrix_get_travel_checked() porque
        // cubre DOS lecturas (travel y estado) y la salida es "escribe 0 en los
        // dos bytes y sigue", no "sustituye una lectura".
        if (r >= MATRIX_ROWS || c >= MATRIX_COLS) {
            pkt[6 + i * 2]     = 0;
            pkt[6 + i * 2 + 1] = 0;
            continue;
        }
        pkt[6 + i * 2]     = analog_matrix_get_travel(r, c);
        pkt[6 + i * 2 + 1] = analog_matrix_get_key_state(r, c) ? 1 : 0;
    }

#if defined(USB_SOF_TIMING_PROBE)
    uint32_t count = scan_probe_count;
    pkt[18] = count & 0xFF;
    pkt[19] = (count >> 8) & 0xFF;
    pkt[20] = (count >> 16) & 0xFF;
    pkt[21] = (count >> 24) & 0xFF;
    pkt[22] = scan_probe_duration_us & 0xFF;
    pkt[23] = scan_probe_duration_us >> 8;
    pkt[24] = scan_probe_phase_us & 0xFF;
    pkt[25] = scan_probe_phase_us >> 8;
#endif

    raw_hid_send(pkt, TELEMETRY_EPSIZE);
}

// ===========================================================================
// Logger de eventos (mistype-hunt)
// ===========================================================================
// A diferencia del stream de travel (200 Hz, apagado en Gaming), registra solo
// CAMBIOS de estado de las teclas de movimiento, con el travel del instante. Al
// ser event-driven su costo es ~cero cuando no pasa nada, asi que SI corre en
// Gaming — el objetivo es cazar fantasmas en juego real (LShift/LCtrl en
// especial). El analisis (candidatos sub-10 ms y ventanas OFF) lo hace el cliente.
//   Paquete v2 (32 B): [0] 0xEC  [1] version  [2] N eventos
//     luego N x 5 bytes: [t_lo, t_hi, key_idx, pressed, travel]
//     [28] secuencia de paquete  [29..30] eventos descartados, LE
//   key_idx: 0=W 1=A 2=S 3=D 4=SPC 5=LSFT 6=LCTL
#define EVLOG_MAGIC 0xEC
// v3: el byte `pressed` pasa a ser mascara de bits — bit0 = pulsada,
// bit1 = FISICA (flanco crudo, antes de los stretches F6/F9). Sin ese bit, en un
// build lab el evlog solo veia el estado ya clampeado y los eventos que el clamp
// se come eran invisibles; medir fisico y reportado exigia dos sesiones con
// drills distintos, que es la mayor fuente de error del A/B.
#define EVLOG_VERSION 3
#define EVLOG_PRESSED_BIT 0x01
#define EVLOG_PHYSICAL_BIT 0x02
#define EVLOG_RING 32
#define EVLOG_MAX_PER_PKT 5
STATIC_ASSERT(3 + EVLOG_MAX_PER_PKT * 5 <= 28, "Los eventos evlog no deben invadir metadata v2");

typedef struct {
    uint16_t t;
    uint8_t  key_idx;
    uint8_t  pressed;
    uint8_t  travel;
} evlog_event_t;

static evlog_event_t evlog_ring[EVLOG_RING];
static uint8_t       evlog_head;
static uint8_t       evlog_count;
static uint8_t       evlog_seq;
static uint16_t      evlog_dropped;
static bool          evlog_active = false;

static int8_t evlog_key_index(uint16_t keycode) {
    switch (keycode) {
        case KC_W:    return 0;
        case KC_A:    return 1;
        case KC_S:    return 2;
        case KC_D:    return 3;
        case KC_SPC:  return 4;
        case KC_LSFT: return 5;
        case KC_LCTL: return 6;
        default:      return -1;
    }
}

static void evlog_record(uint16_t keycode, bool pressed, uint8_t row, uint8_t col, bool physical) {
    if (!evlog_active) return;
    int8_t idx = evlog_key_index(keycode);
    if (idx < 0) return;
    // Un evento virtual (macro/combo/SEND_STRING) llega con row/col centinela
    // (0xFF). analog_matrix_get_travel no valida rango, asi que descartamos:
    // no es una actuacion fisica y no tiene travel real que registrar.
    if (row >= MATRIX_ROWS || col >= MATRIX_COLS) return;
    if (evlog_count >= EVLOG_RING) {
        if (evlog_dropped != UINT16_MAX) evlog_dropped++;
        return; // burst improbable: dropea el mas nuevo y lo hace observable
    }

    evlog_event_t *e = &evlog_ring[(evlog_head + evlog_count) % EVLOG_RING];
    e->t       = timer_read();
    e->key_idx = (uint8_t)idx;
    e->pressed = (uint8_t)((pressed ? EVLOG_PRESSED_BIT : 0) | (physical ? EVLOG_PHYSICAL_BIT : 0));
    e->travel  = analog_matrix_get_travel(row, col);
    evlog_count++;
}

void evlog_record_event(uint16_t keycode, bool pressed, uint8_t row, uint8_t col) {
    evlog_record(keycode, pressed, row, col, false);
}

#if ANALOG_POLICY_NEEDED
// Override de la hook weak de common/: flanco FISICO de una tecla con stretch,
// antes de que F9/F6 lo alteren. Corre dentro del barrido, asi que hace lo
// minimo — el mismo trabajo que un evento normal del evlog, y solo en flancos de
// las 2-3 teclas de la whitelist.
void analog_matrix_physical_edge_hook(uint16_t keycode, bool pressed, uint8_t row, uint8_t col) {
    evlog_record(keycode, pressed, row, col, true);
}
#endif

void evlog_task(void) {
    if (!evlog_active || evlog_count == 0) return;

    uint8_t pkt[TELEMETRY_EPSIZE] = {0};
    pkt[0] = EVLOG_MAGIC;
    pkt[1] = EVLOG_VERSION;

    uint8_t n = evlog_count < EVLOG_MAX_PER_PKT ? evlog_count : EVLOG_MAX_PER_PKT;
    pkt[2]    = n;
    for (uint8_t i = 0; i < n; i++) {
        evlog_event_t *e   = &evlog_ring[evlog_head];
        pkt[3 + i * 5]     = e->t & 0xFF;
        pkt[3 + i * 5 + 1] = e->t >> 8;
        pkt[3 + i * 5 + 2] = e->key_idx;
        pkt[3 + i * 5 + 3] = e->pressed;
        pkt[3 + i * 5 + 4] = e->travel;
        evlog_head = (evlog_head + 1) % EVLOG_RING;
        evlog_count--;
    }

    pkt[28] = evlog_seq++;
    pkt[29] = evlog_dropped & 0xFF;
    pkt[30] = evlog_dropped >> 8;

    raw_hid_send(pkt, TELEMETRY_EPSIZE);
}

// ===========================================================================
// Control por Raw HID (unico punto de entrada — no hay keycodes)
// ===========================================================================
// El cliente arranca/para los diagnosticos con el comando 0xEE. Se eligio un
// comando propio en vez de un keycode porque el keymap de VIA vive en EEPROM y
// puede dejar cualquier tecla desasignada; un comando HID siempre llega.
//
// Cualquier OTRO comando entrante significa que un cliente de configuracion
// (Launcher) esta hablando: apagamos los diagnosticos para no pisarle las
// respuestas — comparten endpoint.
#define DIAG_CMD 0xEE
enum {
    DIAG_EVLOG_OFF   = 0x00,
    DIAG_EVLOG_ON    = 0x01,
    DIAG_TELEM_OFF   = 0x10,
    DIAG_TELEM_ON    = 0x11,
    DIAG_POLICY_DUMP = 0x20,
    // (1-ago) Histograma de ventanas. OJO: estos DOS tienen que estar en el
    // switch de telemetry_raw_hid_rx(), no caer por el default — ese default
    // APAGA los diagnosticos, asi que un comando no reconocido mataria la sesion
    // que intenta leer. Es la misma trampa que obligo a meter DIAG_POLICY_DUMP
    // dentro del switch.
    DIAG_HIST_RESET  = 0x30,
    DIAG_HIST_DUMP   = 0x31, // data[2] = key_idx, data[3] = capa
    DIAG_HEALTH_DUMP = 0x32,
    DIAG_CAL_STATUS  = 0x40, // data[2] = key_idx de telemetry_keycodes
    DIAG_CAL_APPLY   = 0x41, // data[2] = key_idx; aplica solo en RAM
    DIAG_CAL_REVERT  = 0x42, // vuelve al snapshot de arranque
    DIAG_CAL_CLEAR   = 0x43, // borra muestras, conserva baseline/aplicacion
};

// Volcado de la politica resuelta (whitelists por keycode -> posiciones). Es
// una consulta sin estado: responde y ya, no arranca ningun stream.
//
// Solo existe en builds con politica activa (lab). En torneo ANALOG_POLICY_NEEDED
// es 0, no hay nada que volcar, y compilarlo romperia el invariante de que el
// binario de torneo no crece por diagnostico opcional. En ese build el 0x20 cae
// al camino de "comando desconocido" y apaga los diagnosticos, que es correcto.
#if ANALOG_POLICY_NEEDED
#    define POLICY_MAGIC 0xEB
#    define POLICY_VERSION 1
STATIC_ASSERT(2 + ANALOG_POLICY_DUMP_LEN <= TELEMETRY_EPSIZE, "El volcado de politica no cabe en un paquete Raw HID");

static void policy_dump_send(void) {
    uint8_t pkt[TELEMETRY_EPSIZE] = {0};
    pkt[0]                        = POLICY_MAGIC;
    pkt[1]                        = POLICY_VERSION;
    analog_matrix_policy_dump(&pkt[2]);
    raw_hid_send(pkt, TELEMETRY_EPSIZE);
}
#endif

#if ANALOG_CONFIDENT_BOTTOM_OUT_ENABLE
#    define CAL_MAGIC 0xE8
#    define CAL_VERSION 1
enum {
    CAL_REPLY_STATUS,
    CAL_REPLY_ACTION,
};

static void cal_status_send(uint8_t key_idx) {
    uint8_t pkt[TELEMETRY_EPSIZE] = {0};
    pkt[0]                        = CAL_MAGIC;
    pkt[1]                        = CAL_VERSION;
    pkt[2]                        = CAL_REPLY_STATUS;
    pkt[3]                        = key_idx;

    if (key_idx >= TELEMETRY_KEY_COUNT) {
        pkt[6] = 0x80;
        raw_hid_send(pkt, TELEMETRY_EPSIZE);
        return;
    }

    const uint8_t row = telemetry_key_row[key_idx];
    const uint8_t col = telemetry_key_col[key_idx];
    pkt[4]            = row;
    pkt[5]            = col;

    analog_confident_bottom_status_t status;
    if (!analog_matrix_confident_bottom_status(row, col, &status)) {
        pkt[6] = 0x80;
        raw_hid_send(pkt, TELEMETRY_EPSIZE);
        return;
    }

    pkt[6] = (uint8_t)((status.initialized ? 0x01 : 0) | (status.ready ? 0x02 : 0) | (status.applied ? 0x04 : 0) | (analog_matrix_is_gaming_mode() ? 0x08 : 0));
    pkt[7] = status.sample_count;
#    define CAL_PUT16(offset, value)      \
        do {                              \
            pkt[offset]     = (value);    \
            pkt[offset + 1] = (value) >> 8; \
        } while (0)
    CAL_PUT16(8, status.baseline_full);
    CAL_PUT16(10, status.active_full);
    CAL_PUT16(12, status.candidate_full);
    CAL_PUT16(14, status.sample_min);
    CAL_PUT16(16, status.sample_max);
    CAL_PUT16(18, status.completed_presses);
    CAL_PUT16(20, status.rejected_windows);
#    undef CAL_PUT16
    raw_hid_send(pkt, TELEMETRY_EPSIZE);
}

static void cal_action_send(uint8_t subcmd, uint8_t affected) {
    uint8_t pkt[TELEMETRY_EPSIZE] = {0};
    pkt[0]                        = CAL_MAGIC;
    pkt[1]                        = CAL_VERSION;
    pkt[2]                        = CAL_REPLY_ACTION;
    pkt[3]                        = subcmd;
    pkt[4]                        = affected;
    pkt[5]                        = analog_matrix_is_gaming_mode() ? 1 : 0;
    raw_hid_send(pkt, TELEMETRY_EPSIZE);
}
#endif

#if ANALOG_WINDOW_HISTOGRAM
// Histograma de ventanas: un paquete por tecla y capa, pedido individualmente.
// Sin paginacion a proposito — para tres teclas no compensa el estado de
// secuenciacion que luego habria que depurar.
#    define HIST_MAGIC 0xEA
#    define HIST_VERSION 1
#    define HEALTH_MAGIC 0xE9
#    define HEALTH_VERSION 2
STATIC_ASSERT(3 + AWH_DUMP_LEN <= TELEMETRY_EPSIZE, "El volcado del histograma no cabe en un paquete Raw HID");
STATIC_ASSERT(2 + AWH_HEALTH_LEN <= TELEMETRY_EPSIZE, "El volcado de salud no cabe en un paquete Raw HID");

static void hist_dump_send(uint8_t key_idx, uint8_t layer) {
    uint8_t pkt[TELEMETRY_EPSIZE] = {0};
    pkt[0]                        = HIST_MAGIC;
    pkt[1]                        = HIST_VERSION;
    // pkt[2] = 0 (ok) / 1 (indice fuera de rango). El cliente no tiene que
    // adivinar si un volcado a cero es "sin datos" o "pediste mal".
    pkt[2] = analog_window_hist_dump(key_idx, layer, &pkt[3]) ? 0 : 1;
    raw_hid_send(pkt, TELEMETRY_EPSIZE);
}

static void health_dump_send(void) {
    uint8_t pkt[TELEMETRY_EPSIZE] = {0};
    pkt[0]                        = HEALTH_MAGIC;
    pkt[1]                        = HEALTH_VERSION;
    analog_window_hist_health(&pkt[2]);
    raw_hid_send(pkt, TELEMETRY_EPSIZE);
}
#endif

// El override de kc_raw_hid_rx_user vive en el keymap compartido y delega aqui
// en lab. En alex se compila fuera junto con telemetria/politica/histograma.
void telemetry_raw_hid_rx(uint8_t src, uint8_t *data, uint8_t length) {
    (void)src;

    if (length >= 2 && data[0] == DIAG_CMD) {
        switch (data[1]) {
            case DIAG_EVLOG_ON:
                evlog_head    = 0;
                evlog_count   = 0;
                evlog_seq     = 0;
                evlog_dropped = 0;
                evlog_active  = true;
                break;
            case DIAG_EVLOG_OFF:
                evlog_active = false;
                break;
            case DIAG_TELEM_ON:
                // El stream de travel compite con el input: nunca en Gaming.
                if (!analog_matrix_is_gaming_mode()) {
                    last_send        = timer_read();
                    telemetry_active = true;
                }
                break;
            case DIAG_TELEM_OFF:
                telemetry_active = false;
                break;
#if ANALOG_POLICY_NEEDED
            case DIAG_POLICY_DUMP:
                policy_dump_send();
                break;
#endif
#if ANALOG_WINDOW_HISTOGRAM
            case DIAG_HIST_RESET:
                analog_window_hist_reset();
                break;
            case DIAG_HIST_DUMP:
                if (length >= 4) hist_dump_send(data[2], data[3]);
                break;
            case DIAG_HEALTH_DUMP:
                health_dump_send();
                break;
#endif
#if ANALOG_CONFIDENT_BOTTOM_OUT_ENABLE
            case DIAG_CAL_STATUS:
                if (length >= 3) cal_status_send(data[2]);
                break;
            case DIAG_CAL_APPLY:
                if (length >= 3 && data[2] < TELEMETRY_KEY_COUNT) {
                    const uint8_t row = telemetry_key_row[data[2]];
                    const uint8_t col = telemetry_key_col[data[2]];
                    cal_action_send(data[1], analog_matrix_confident_bottom_apply_key(row, col) ? 1 : 0);
                } else {
                    cal_action_send(data[1], 0);
                }
                break;
            case DIAG_CAL_REVERT:
                cal_action_send(data[1], analog_matrix_confident_bottom_revert());
                break;
            case DIAG_CAL_CLEAR:
                analog_matrix_confident_bottom_clear();
                cal_action_send(data[1], 0);
                break;
#endif
        }
        return; // es nuestro comando: no apagar nada
    }

    telemetry_active = false;
    evlog_active     = false;
}
