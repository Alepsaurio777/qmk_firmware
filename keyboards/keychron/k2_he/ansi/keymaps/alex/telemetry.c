#include QMK_KEYBOARD_H
#include "analog_matrix.h"
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
        // Sin resolver: reportar cero en vez de leer fuera de rango
        // (analog_matrix_get_travel no valida indices).
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

// El override de kc_raw_hid_rx_user vive en keymap.c (siempre compilado) y
// delega aqui: la re-resolucion de la politica por keycode no puede depender de
// ALEX_TELEMETRY_ENABLE, o apagar los diagnosticos la perderia en silencio.
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
        }
        return; // es nuestro comando: no apagar nada
    }

    telemetry_active = false;
    evlog_active     = false;
}
