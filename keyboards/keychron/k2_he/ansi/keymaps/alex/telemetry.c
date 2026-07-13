#include QMK_KEYBOARD_H
#include "analog_matrix.h"
#include "raw_hid.h"
#include "telemetry.h"

// Formato del paquete (32 bytes, endpoint Raw HID de VIA):
//   [0] 0xED magic  [1] version  [2..3] timer_read16 LE  [4] seq
//   [5] numero de teclas N
//   [6 + i*2]     travel de la tecla i (0..240, unidades de TRAVEL_SCALE*0.1mm)
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

typedef struct {
    uint8_t row;
    uint8_t col;
} telemetry_key_t;

// Teclas criticas de PvP 1.8.9: W, A, S, D, espacio (fast key), LShift.
static const telemetry_key_t telemetry_keys[] = {
    {2, 2}, {3, 1}, {3, 2}, {3, 3}, {5, 6}, {4, 0},
};
#define TELEMETRY_KEY_COUNT ARRAY_SIZE(telemetry_keys)

static bool     telemetry_active = false;
static uint16_t last_send        = 0;
static uint8_t  seq              = 0;

bool telemetry_is_active(void) {
    return telemetry_active;
}

void telemetry_toggle(void) {
    // Nunca en Gaming: el streaming compite por CPU/USB con el input.
    if (analog_matrix_is_gaming_mode()) return;
    telemetry_active = !telemetry_active;
    if (telemetry_active) last_send = timer_read();
}

void telemetry_stop(void) {
    telemetry_active = false;
}

// El stream comparte endpoint con VIA/Launcher: si un cliente de configuracion
// empieza a hablar, apagar la telemetria para no pisarle las respuestas
// (aprendido el 13-jul: el stream activo hacia fallar el handshake de Launcher).
void kc_raw_hid_rx_user(uint8_t src, uint8_t *data, uint8_t length) {
    (void)src;
    (void)data;
    (void)length;
    telemetry_stop();
}

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
        pkt[6 + i * 2]     = analog_matrix_get_travel(telemetry_keys[i].row, telemetry_keys[i].col);
        pkt[6 + i * 2 + 1] = analog_matrix_get_key_state(telemetry_keys[i].row, telemetry_keys[i].col) ? 1 : 0;
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
