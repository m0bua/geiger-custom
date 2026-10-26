#include "log_eeprom.h"

#include "board.h"
#include "log_eeprom_layout.h"
#include "spi_log.h"

#include <stddef.h>
#include <string.h>

#define EEPROM_STATE_MAGIC 0x4Cu
#define EEPROM_STATE_VERSION 1u
#define EEPROM_TX_MARKER 0xA5u
#define EEPROM_TX_MAGIC 0xE7u
#define EEPROM_COMMIT 0xA5u
#define EEPROM_STATE_A 0u
#define EEPROM_STATE_B 1u

typedef struct {
    uint8_t head[4];
    uint8_t count[4];
    uint8_t generation;
    uint8_t active_segment;
    uint8_t active_year;
    uint8_t copy;
    bool valid;
} eeprom_state_t;

static eeprom_state_t state;
static uint8_t segment_years[LOG_SEGMENT_ID_COUNT];
static bool update_segment_table(uint8_t segment_id,
                                 uint8_t year_descriptor);

static uint8_t crc8(const uint8_t *bytes, uint8_t length)
{
    uint8_t crc = 0u;
    for (uint8_t i = 0u; i < length; ++i) {
        crc ^= bytes[i];
        for (uint8_t bit = 0u; bit < 8u; ++bit) {
            crc = (crc & 0x80u) != 0u ?
                (uint8_t)((crc << 1u) ^ 0x07u) : (uint8_t)(crc << 1u);
        }
    }
    return crc;
}

static uint16_t crc16(const uint8_t *bytes, uint8_t length)
{
    uint16_t crc = 0xFFFFu;
    for (uint8_t i = 0u; i < length; ++i) {
        crc ^= (uint16_t)bytes[i] << 8u;
        for (uint8_t bit = 0u; bit < 8u; ++bit) {
            crc = (crc & 0x8000u) != 0u ?
                (uint16_t)((crc << 1u) ^ 0x1021u) : (uint16_t)(crc << 1u);
        }
    }
    return crc;
}

static uint8_t ring_index(log_record_kind_t kind)
{
    return kind <= LOG_RECORD_ALERT ? (uint8_t)kind : 0xFFu;
}

uint8_t log_eeprom_capacity(log_record_kind_t kind)
{
    static const uint8_t capacities[4] = {
        LOG_EEPROM_MINUTE_COUNT, LOG_EEPROM_HOUR_COUNT,
        LOG_EEPROM_DAY_COUNT, LOG_EEPROM_ALERT_COUNT
    };
    const uint8_t index = ring_index(kind);
    return index == 0xFFu ? 0u : capacities[index];
}

static uint16_t ring_base(log_record_kind_t kind)
{
    switch (kind) {
    case LOG_RECORD_MINUTE: return LOG_EEPROM_MINUTE_BASE;
    case LOG_RECORD_HOUR: return LOG_EEPROM_HOUR_BASE;
    case LOG_RECORD_DAY: return LOG_EEPROM_DAY_BASE;
    case LOG_RECORD_ALERT: return LOG_EEPROM_ALERT_BASE;
    default: return 0u;
    }
}

static uint8_t ring_record_size(log_record_kind_t kind)
{
    return kind == LOG_RECORD_ALERT ? LOG_EEPROM_ALERT_SIZE :
        LOG_EEPROM_RECORD_SIZE;
}

static uint16_t slot_address(log_record_kind_t kind, uint8_t slot)
{
    return (uint16_t)(ring_base(kind) +
        (uint16_t)slot * ring_record_size(kind));
}

static bool read_bytes(uint16_t address, uint8_t *bytes, uint8_t length)
{
    for (uint8_t i = 0u; i < length; ++i) {
        if (!board_eeprom_read((uint16_t)(address + i), &bytes[i])) return false;
    }
    return true;
}

static bool write_bytes(uint16_t address, const uint8_t *bytes,
                        uint8_t length)
{
    for (uint8_t i = 0u; i < length; ++i) {
        if (!board_eeprom_write((uint16_t)(address + i), bytes[i])) return false;
    }
    return true;
}

static bool decode_state(uint8_t copy, eeprom_state_t *decoded)
{
    uint8_t bytes[LOG_EEPROM_STATE_COPY_SIZE];
    const uint16_t address = copy == EEPROM_STATE_A ?
        LOG_EEPROM_STATE_A_BASE : LOG_EEPROM_STATE_B_BASE;
    if (!read_bytes(address, bytes, sizeof(bytes)) ||
        bytes[0] != EEPROM_STATE_MAGIC ||
        bytes[1] != EEPROM_STATE_VERSION ||
        bytes[14] != EEPROM_COMMIT || crc8(bytes, 13u) != bytes[13]) {
        return false;
    }
    for (uint8_t i = 0u; i < 4u; ++i) {
        const uint8_t capacity = log_eeprom_capacity((log_record_kind_t)i);
        if (bytes[3u + i * 2u] >= capacity ||
            bytes[4u + i * 2u] > capacity) return false;
        decoded->head[i] = bytes[3u + i * 2u];
        decoded->count[i] = bytes[4u + i * 2u];
    }
    decoded->generation = bytes[2];
    decoded->active_segment = bytes[11] & 0x3Fu;
    decoded->active_year = bytes[12];
    decoded->copy = copy;
    decoded->valid = true;
    return true;
}

static bool save_state(void)
{
    uint8_t bytes[LOG_EEPROM_STATE_COPY_SIZE] = {0u};
    const uint8_t target = state.valid ? (uint8_t)(state.copy ^ 1u) :
        EEPROM_STATE_A;
    const uint16_t address = target == EEPROM_STATE_A ?
        LOG_EEPROM_STATE_A_BASE : LOG_EEPROM_STATE_B_BASE;
    bytes[0] = EEPROM_STATE_MAGIC;
    bytes[1] = EEPROM_STATE_VERSION;
    bytes[2] = (uint8_t)(state.generation + 1u);
    for (uint8_t i = 0u; i < 4u; ++i) {
        bytes[3u + i * 2u] = state.head[i];
        bytes[4u + i * 2u] = state.count[i];
    }
    bytes[11] = state.active_segment & 0x3Fu;
    bytes[12] = state.active_year;
    bytes[13] = crc8(bytes, 13u);
    if (!board_eeprom_write((uint16_t)(address + 14u), 0u) ||
        !write_bytes(address, bytes, 14u) ||
        !board_eeprom_write((uint16_t)(address + 14u), EEPROM_COMMIT)) {
        return false;
    }
    state.generation = bytes[2];
    state.copy = target;
    state.valid = true;
    return true;
}

static bool transaction_recover(void)
{
    uint8_t tx[LOG_EEPROM_TRANSACTION_SIZE];
    const uint16_t address = LOG_EEPROM_TRANSACTION_BASE;
    const uint8_t marker = EEPROM_TX_MARKER;
    if (!read_bytes(address, tx, sizeof(tx))) return false;
    if (tx[0] != marker) return true;
    if (tx[1] != EEPROM_TX_MAGIC || crc8(&tx[1], 14u) != tx[15]) {
        return board_eeprom_write(address, 0u);
    }
    const log_record_kind_t kind = (log_record_kind_t)(tx[2] & 3u);
    const uint8_t slot = tx[3];
    if (slot >= log_eeprom_capacity(kind)) return false;
    if (state.generation != tx[4]) {
        if (!write_bytes(slot_address(kind, slot), &tx[5],
                ring_record_size(kind))) return false;
    }
    return board_eeprom_write(address, 0u);
}

static bool table_recover(void)
{
    uint8_t stored_crc[2];
    const uint16_t base = LOG_EEPROM_SEGMENT_TABLE_BASE;
    if (!read_bytes(base, segment_years, LOG_SEGMENT_ID_COUNT)) return false;
    const uint16_t calculated = crc16(segment_years,
        LOG_SEGMENT_ID_COUNT);
    if (!read_bytes(LOG_EEPROM_TABLE_CRC_BASE, stored_crc, 2u)) return false;
    if (stored_crc[0] != (uint8_t)calculated ||
        stored_crc[1] != (uint8_t)(calculated >> 8u)) {
        return board_eeprom_write(LOG_EEPROM_TABLE_CRC_BASE,
                (uint8_t)calculated) &&
            board_eeprom_write(LOG_EEPROM_TABLE_CRC_BASE + 1u,
                (uint8_t)(calculated >> 8u));
    }
    return true;
}

bool log_eeprom_init(void)
{
    eeprom_state_t first = {0};
    eeprom_state_t second = {0};
    if (!decode_state(EEPROM_STATE_A, &first)) first.valid = false;
    if (!decode_state(EEPROM_STATE_B, &second)) second.valid = false;
    if (first.valid && second.valid) {
        state = (int8_t)(second.generation - first.generation) > 0 ?
            second : first;
    } else if (first.valid) {
        state = first;
    } else if (second.valid) {
        state = second;
    } else {
        state = (eeprom_state_t){0};
        state.active_segment = 0xFFu;
        state.active_year = 0xFFu;
    }
    if (!transaction_recover() || !table_recover()) return false;
    if (state.valid && state.active_segment < LOG_SEGMENT_ID_COUNT &&
        segment_years[state.active_segment] != state.active_year &&
        !update_segment_table(state.active_segment, state.active_year)) {
        return false;
    }
    return true;
}

uint8_t log_eeprom_count(log_record_kind_t kind)
{
    const uint8_t index = ring_index(kind);
    return index == 0xFFu ? 0u : state.count[index];
}

bool log_eeprom_read(log_record_kind_t kind, uint8_t age,
                     log_record_t *record)
{
    const uint8_t index = ring_index(kind);
    const uint8_t capacity = log_eeprom_capacity(kind);
    uint8_t bytes[LOG_ALERT_DATA_BYTES];
    uint8_t slot;
    if (!state.valid || index == 0xFFu || record == NULL ||
        age >= state.count[index]) return false;
    slot = (uint8_t)((state.head[index] + capacity - 1u - age) % capacity);
    return read_bytes(slot_address(kind, slot), bytes, ring_record_size(kind)) &&
        log_record_decode(kind, bytes, ring_record_size(kind), record);
}

bool log_eeprom_segment_year(uint8_t segment_id, uint8_t *year_descriptor)
{
    if (segment_id >= LOG_SEGMENT_ID_COUNT || year_descriptor == NULL) {
        return false;
    }
    *year_descriptor = segment_years[segment_id];
    return true;
}

bool log_eeprom_active_segment(uint8_t *segment_id)
{
    if (segment_id == NULL || !state.valid ||
        state.active_segment >= LOG_SEGMENT_ID_COUNT) return false;
    *segment_id = state.active_segment;
    return true;
}

static bool write_transaction(log_record_kind_t kind, uint8_t slot,
                              uint8_t next_generation)
{
    uint8_t tx[LOG_EEPROM_TRANSACTION_SIZE];
    const uint16_t base = LOG_EEPROM_TRANSACTION_BASE;
    const uint8_t size = ring_record_size(kind);
    for (uint8_t i = 0u; i < sizeof(tx); ++i) tx[i] = 0u;
    tx[1] = EEPROM_TX_MAGIC;
    tx[2] = (uint8_t)kind;
    tx[3] = slot;
    tx[4] = next_generation;
    if (!read_bytes(slot_address(kind, slot), &tx[5], size)) return false;
    tx[15] = crc8(&tx[1], 14u);
    return board_eeprom_write(base, 0u) &&
        write_bytes((uint16_t)(base + 1u), &tx[1], 15u) &&
        board_eeprom_write(base, EEPROM_TX_MARKER);
}

static bool update_segment_table(uint8_t segment_id, uint8_t year_descriptor)
{
    uint8_t verify;
    const uint16_t table_address = (uint16_t)(
        LOG_EEPROM_SEGMENT_TABLE_BASE + segment_id);
    uint16_t crc;
    if (!board_eeprom_write(table_address, year_descriptor) ||
        !board_eeprom_read(table_address, &verify) ||
        verify != year_descriptor) return false;
    segment_years[segment_id] = verify;
    crc = crc16(segment_years, LOG_SEGMENT_ID_COUNT);
    return board_eeprom_write(LOG_EEPROM_TABLE_CRC_BASE, (uint8_t)crc) &&
        board_eeprom_write(LOG_EEPROM_TABLE_CRC_BASE + 1u,
            (uint8_t)(crc >> 8u));
}

static bool eeprom_period_index(log_record_kind_t kind, uint32_t seconds,
                                bool calendar_valid, uint32_t *period_index,
                                uint8_t *year_descriptor)
{
    if (!calendar_valid) {
        switch (kind) {
        case LOG_RECORD_MINUTE:
        case LOG_RECORD_ALERT:
            *period_index = (seconds / 60u) % 527040u;
            break;
        case LOG_RECORD_HOUR:
            *period_index = (seconds / 3600u) % 8784u;
            break;
        case LOG_RECORD_DAY:
            *period_index = (seconds / 86400u) % 366u;
            break;
        default: return false;
        }
        *year_descriptor = 0u;
        return true;
    }

    uint32_t days = seconds / 86400u;
    uint16_t year = 2000u;
    while (year <= 2099u) {
        const uint16_t year_days =
            ((year % 4u) == 0u && (year % 100u) != 0u) ||
                (year % 400u) == 0u ? 366u : 365u;
        if (days < year_days) break;
        days -= year_days;
        ++year;
    }
    if (year > 2099u) return false;
    const uint32_t day_seconds = seconds % 86400u;
    *year_descriptor = SPI_LOG_YEAR_DESCRIPTOR(year, true);
    switch (kind) {
    case LOG_RECORD_MINUTE:
    case LOG_RECORD_ALERT:
        *period_index = days * 1440u + day_seconds / 60u;
        return true;
    case LOG_RECORD_HOUR:
        *period_index = days * 24u + day_seconds / 3600u;
        return true;
    case LOG_RECORD_DAY:
        *period_index = days;
        return true;
    default: return false;
    }
}

bool log_eeprom_query_read(void *context, log_record_kind_t kind,
                          uint16_t epoch, uint32_t period_start,
                          log_record_t *record, bool *live)
{
    const log_query_time_context_t *time_context = context;
    const bool calendar_valid = time_context != NULL &&
        time_context->calendar_valid;
    const uint8_t segment_id = (uint8_t)(epoch % LOG_SEGMENT_ID_COUNT);
    uint32_t requested_index;
    uint8_t requested_year;
    if (record == NULL || live == NULL ||
        !eeprom_period_index(kind, period_start, calendar_valid,
            &requested_index, &requested_year)) return false;
    for (uint8_t age = 0u; age < log_eeprom_count(kind); ++age) {
        log_record_t candidate;
        uint8_t stored_year;
        if (!log_eeprom_read(kind, age, &candidate) ||
            candidate.segment_id != segment_id ||
            candidate.period_index != requested_index ||
            !log_eeprom_segment_year(candidate.segment_id, &stored_year) ||
            stored_year != requested_year) continue;
        *record = candidate;
        *live = false;
        return true;
    }
    return false;
}

bool log_eeprom_append(log_record_kind_t kind, const log_record_t *record,
                       uint8_t year_descriptor)
{
    uint8_t bytes[LOG_ALERT_DATA_BYTES];
    uint8_t verify[LOG_ALERT_DATA_BYTES];
    uint8_t index = ring_index(kind);
    uint8_t capacity = log_eeprom_capacity(kind);
    uint8_t next_generation;
    uint8_t slot;
    eeprom_state_t previous_state;
    if (index == 0xFFu || record == NULL || capacity == 0u ||
        !log_record_encode(kind, record, bytes, sizeof(bytes))) return false;
    if (!transaction_recover()) return false;
    previous_state = state;
    slot = state.head[index];
    next_generation = (uint8_t)(state.generation + 1u);
    if (!write_transaction(kind, slot, next_generation) ||
        !write_bytes(slot_address(kind, slot), bytes,
            ring_record_size(kind)) ||
        !read_bytes(slot_address(kind, slot), verify,
            ring_record_size(kind)) ||
        !log_record_decode(kind, verify, ring_record_size(kind),
            &(log_record_t){0}) ||
        memcmp(bytes, verify, ring_record_size(kind)) != 0) return false;

    state.head[index] = (uint8_t)((slot + 1u) % capacity);
    if (state.count[index] < capacity) ++state.count[index];
    state.active_segment = record->segment_id;
    state.active_year = year_descriptor;
    if (!save_state()) {
        state = previous_state;
        (void)transaction_recover();
        return false;
    }
    if (!board_eeprom_write(LOG_EEPROM_TRANSACTION_BASE, 0u)) return false;
    if (segment_years[record->segment_id] != year_descriptor &&
        !update_segment_table(record->segment_id, year_descriptor)) return false;
    return true;
}
