#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "eeprom_he.h"
#include "i2c_master.h"

#define CHECK(condition, message, ...)                                                                 \
    do {                                                                                                \
        checks++;                                                                                      \
        if (!(condition)) {                                                                            \
            failures++;                                                                                \
            fprintf(stderr, "FAIL:%d: " message "\n", __LINE__, ##__VA_ARGS__);                     \
        }                                                                                               \
    } while (0)

typedef struct {
    bool     ping;
    uint8_t  address;
    uint16_t length;
    uint16_t timeout;
    uint8_t  data[EXTERNAL_EEPROM_ADDRESS_SIZE + EXTERNAL_EEPROM_PAGE_SIZE];
} transaction_t;

static transaction_t transactions[64];
static unsigned      transaction_count;
static unsigned      transmit_count;
static unsigned      ping_count;
static unsigned      fail_transmit_call;
static unsigned      fail_ping_call;
static unsigned      wait_calls;
static unsigned      checks;
static unsigned      failures;

void wait_ms(uint16_t ms) {
    (void)ms;
    wait_calls++;
}


i2c_status_t i2c_receive(uint8_t address, uint8_t *data, uint16_t length, uint16_t timeout) {
    (void)address;
    (void)data;
    (void)length;
    (void)timeout;
    return I2C_STATUS_SUCCESS;
}

i2c_status_t i2c_transmit(uint8_t address, const uint8_t *data, uint16_t length, uint16_t timeout) {
    (void)timeout;
    transmit_count++;
    CHECK(transaction_count < sizeof(transactions) / sizeof(transactions[0]), "registro de I2C agotado");
    if (transaction_count < sizeof(transactions) / sizeof(transactions[0])) {
        transaction_t *record = &transactions[transaction_count++];
        record->ping          = false;
        record->address       = address;
        record->length        = length;
        record->timeout       = timeout;
        if (length <= sizeof(record->data)) memcpy(record->data, data, length);
    }
    return fail_transmit_call && transmit_count == fail_transmit_call ? I2C_STATUS_ERROR : I2C_STATUS_SUCCESS;
}

i2c_status_t i2c_ping_address(uint8_t address, uint16_t timeout) {
    (void)timeout;
    ping_count++;
    CHECK(transaction_count < sizeof(transactions) / sizeof(transactions[0]), "registro de ACK agotado");
    if (transaction_count < sizeof(transactions) / sizeof(transactions[0])) {
        transaction_t *record = &transactions[transaction_count++];
        record->ping          = true;
        record->address       = address;
        record->length        = 0;
        record->timeout       = timeout;
    }
    return fail_ping_call && ping_count == fail_ping_call ? I2C_STATUS_ERROR : I2C_STATUS_SUCCESS;
}

static void reset_bus(void) {
    memset(transactions, 0, sizeof(transactions));
    transaction_count = 0;
    transmit_count    = 0;
    ping_count        = 0;
    fail_transmit_call = 0;
    fail_ping_call     = 0;
    wait_calls         = 0;
}

static uint16_t recorded_address(const transaction_t *record) {
    return (uint16_t)((record->data[0] << 8) | record->data[1]);
}

static bool drive(he_eeprom_cal_save_t *save, uint32_t *now, bool permitted, unsigned limit) {
    for (unsigned i = 0; i < limit; i++) {
        he_eeprom_cal_save_status_t status = he_eeprom_cal_save_step(save, permitted, *now);
        if (status == HE_EEPROM_CAL_SAVE_COMPLETE) return true;
        if (status == HE_EEPROM_CAL_SAVE_FAILED) return false;
        (*now)++;
    }
    return false;
}

static void test_marker_payload_marker_sequence(void) {
    uint8_t payload[288];
    he_eeprom_cal_save_t save;
    uint32_t now = 1000;
    for (unsigned i = 0; i < sizeof(payload); i++) payload[i] = (uint8_t)i;

    reset_bus();
    he_eeprom_cal_save_begin(&save, payload, sizeof(payload), (const void *)10, (const void *)4, true, true, 0, 1);
    CHECK(drive(&save, &now, true, 300), "la transaccion completa debe terminar");
    CHECK(wait_calls == 0, "el camino cooperativo no debe llamar wait_ms");
    CHECK(transmit_count == 12, "esperaba 12 escrituras (2 marcadores + 10 paginas), obtuvo %u", transmit_count);
    CHECK(ping_count == 12, "cada pagina debe esperar confirmacion ACK");

    CHECK(!transactions[0].ping && recorded_address(&transactions[0]) == 4 && transactions[0].data[2] == 0, "primera escritura debe invalidar marcador");
    for (unsigned page = 0; page < 10; page++) {
        const unsigned index = 2 + page * 2;
        CHECK(!transactions[index].ping, "el registro %u debe ser escritura de payload", index);
        CHECK(recorded_address(&transactions[index]) == 10 + (page == 0 ? 0 : page * 32 - 10), "direccion de pagina %u incorrecta", page);
        CHECK(transactions[index].length <= EXTERNAL_EEPROM_ADDRESS_SIZE + EXTERNAL_EEPROM_PAGE_SIZE, "una llamada I2C no debe exceder una pagina");
        CHECK(transactions[index].timeout == EXTERNAL_EEPROM_RUNTIME_I2C_TIMEOUT, "el timeout runtime debe ser acotado");
    }
    CHECK(!transactions[22].ping && recorded_address(&transactions[22]) == 4 && transactions[22].data[2] == 1, "ultima escritura debe validar marcador");
}

static void test_first_failure_stops_and_retry_resumes_page(void) {
    uint8_t payload[64] = {0xA5};
    he_eeprom_cal_save_t save;
    uint32_t now = 2000;

    reset_bus();
    fail_transmit_call = 3; /* invalidate, first payload, second payload fails */
    he_eeprom_cal_save_begin(&save, payload, sizeof(payload), (const void *)10, (const void *)4, true, true, 0, 1);
    CHECK(!drive(&save, &now, true, 100), "el fallo de pagina debe propagarse");
    CHECK(transmit_count == 3, "debe detenerse en la primera pagina fallida");

    he_eeprom_cal_save_retry(&save);
    CHECK(drive(&save, &now, true, 100), "la reanudacion debe completar");
    /* The injected failure is one-shot by call number; the retry must start at
     * the failed page, so the first payload address is never sent again. */
    CHECK(transmit_count == 6, "la reanudacion debe escribir pagina fallida, pagina final y marcador: hubo %u TX", transmit_count);
    CHECK(recorded_address(&transactions[5]) == 32, "la reanudacion debe conservar la pagina fallida (offset 32)");
    CHECK(recorded_address(&transactions[2]) == 10, "la primera pagina debe haberse escrito una sola vez");
    CHECK(save.status == HE_EEPROM_CAL_SAVE_COMPLETE, "la reanudacion debe completar");
}

static void test_activity_gate_pauses_every_continuation(void) {
    uint8_t payload[32] = {0x3C};
    he_eeprom_cal_save_t save;
    uint32_t now = 3000;

    reset_bus();
    he_eeprom_cal_save_begin(&save, payload, sizeof(payload), (const void *)10, (const void *)4, true, true, 0, 1);
    for (unsigned i = 0; i < 20; i++) {
        he_eeprom_cal_save_step(&save, false, now++);
    }
    CHECK(transaction_count == 0, "actividad debe impedir iniciar paginas");
    he_eeprom_cal_save_step(&save, true, now);
    CHECK(transaction_count == 1, "al quedar idle debe iniciar una sola escritura");
    now += EXTERNAL_EEPROM_WRITE_TIME;
    he_eeprom_cal_save_step(&save, false, now);
    CHECK(transaction_count == 1, "actividad debe impedir reanudar con ACK");
    he_eeprom_cal_save_step(&save, true, now);
    CHECK(transaction_count == 2 && transactions[1].ping, "al quedar idle debe hacer el ACK pendiente");
}

static void test_stable_snapshot_and_follow_up_request(void) {
    uint8_t live[32];
    uint8_t snapshot_a[32];
    uint8_t snapshot_b[32];
    he_eeprom_cal_save_t save;
    uint32_t now = 4000;
    memset(live, 0x11, sizeof(live));
    memcpy(snapshot_a, live, sizeof(snapshot_a));
    memset(snapshot_b, 0x22, sizeof(snapshot_b));

    reset_bus();
    he_eeprom_cal_save_begin(&save, snapshot_a, sizeof(snapshot_a), (const void *)10, (const void *)4, false, true, 0, 1);
    live[0] = 0x99; /* runtime data changes while the stable snapshot is active */
    CHECK(drive(&save, &now, true, 100), "el snapshot A debe completar");
    CHECK(transactions[0].data[2] == snapshot_a[0], "la escritura debe usar el snapshot estable");

    reset_bus();
    now = 5000;
    he_eeprom_cal_save_begin(&save, snapshot_b, sizeof(snapshot_b), (const void *)10, (const void *)4, false, true, 0, 1);
    CHECK(drive(&save, &now, true, 100), "una solicitud posterior debe poder iniciar otra transaccion");
    CHECK(transactions[0].data[2] == snapshot_b[0], "la solicitud posterior no debe perderse");
}

int main(void) {
    test_marker_payload_marker_sequence();
    test_first_failure_stops_and_retry_resumes_page();
    test_activity_gate_pauses_every_continuation();
    test_stable_snapshot_and_follow_up_request();

    if (failures == 0) {
        printf("OK   eeprom cooperative save (%u checks)\n", checks);
        return 0;
    }
    printf("FAIL eeprom cooperative save (%u/%u checks failed)\n", failures, checks);
    return 1;
}
