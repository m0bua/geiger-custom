#include "log_eeprom.h"

#include "board.h"
#include "log_eeprom_layout.h"
#include "spi_log.h"

#include <stddef.h>
#include <string.h>

#define EEPROM_STATE_MAGIC 0x4Cu
#define EEPROM_STATE_VERSION 3u
#define EEPROM_LEGACY_STATE_VERSION 2u
#define EEPROM_TX_MARKER 0xA5u
#define EEPROM_COMMIT 0xA5u
#define EEPROM_STATE_A 0u
#define EEPROM_STATE_B 1u

typedef struct {
    uint8_t head[4];
    uint8_t count[4];
    uint8_t generation;
    uint16_t active_epoch;
    uint8_t active_year;
    uint8_t copy;
    bool valid;
} eeprom_state_t;

static eeprom_state_t state;
static uint32_t read_revision;
typedef struct {
    uint32_t revision, lower, upper;
    uint16_t epoch;
    uint8_t year, age;
    bool bounds;
} eeprom_read_cache_t;
typedef struct {
    uint32_t revision, date_limit, time;
    uint16_t epoch;
    uint8_t age;
    bool ordered;
    bool valid;
} eeprom_find_cache_t;
static eeprom_read_cache_t read_cache[4];
static eeprom_find_cache_t find_cache[4];
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

#define crc16(bytes, length) log_crc16((bytes), (length), 0xFFFFu)

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
    for (uint8_t i = 0u; i < 8u; ++i) {
        uint8_t value = 0u;
        for (uint8_t bit = 0u; bit < 7u; ++bit) {
            uint8_t at = (uint8_t)(i * 7u + bit);
            if (bytes[3u + at / 8u] & (1u << (at % 8u))) value |= (uint8_t)(1u << bit);
        }
        uint8_t capacity = log_eeprom_capacity((log_record_kind_t)(i / 2u));
        if (value > capacity || (i % 2u == 0u && value == capacity)) return false;
        if (i % 2u == 0u) decoded->head[i / 2u] = value;
        else decoded->count[i / 2u] = value;
    }
    decoded->generation = bytes[2];
    decoded->active_epoch = (uint16_t)bytes[10] | (uint16_t)((uint16_t)bytes[11] << 8u);
    decoded->active_year = bytes[12];
    decoded->copy = copy;
    decoded->valid = true;
    return true;
}

static bool legacy_state_present(uint8_t copy)
{
    uint8_t bytes[LOG_EEPROM_STATE_COPY_SIZE];
    const uint16_t address = copy == EEPROM_STATE_A ?
        LOG_EEPROM_STATE_A_BASE : LOG_EEPROM_STATE_B_BASE;
    return read_bytes(address, bytes, sizeof(bytes)) &&
        bytes[0] == EEPROM_STATE_MAGIC &&
        bytes[1] == EEPROM_LEGACY_STATE_VERSION &&
        bytes[14] == EEPROM_COMMIT && crc8(bytes, 13u) == bytes[13];
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
    for (uint8_t i = 0u; i < 8u; ++i) {
        uint8_t value = i % 2u == 0u ? state.head[i / 2u] : state.count[i / 2u];
        for (uint8_t bit = 0u; bit < 7u; ++bit) {
            uint8_t at = (uint8_t)(i * 7u + bit);
            if (value & (1u << bit)) bytes[3u + at / 8u] |= (uint8_t)(1u << (at % 8u));
        }
    }
    bytes[10] = (uint8_t)state.active_epoch;
    bytes[11] = (uint8_t)(state.active_epoch >> 8u);
    bytes[12] = state.active_year;
    bytes[13] = crc8(bytes, 13u);
    if (!board_eeprom_write((uint16_t)(address + 14u), 0u) ||
        !write_bytes(address, bytes, 14u) ||
        !board_eeprom_write((uint16_t)(address + 14u), EEPROM_COMMIT)) {
        return false;
    }
    ++read_revision;
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
    if (crc8(&tx[1], 14u) != tx[15]) return false;
    const log_record_kind_t kind = (log_record_kind_t)(tx[1] & 3u);
    const uint8_t slot = tx[2];
    if (slot >= log_eeprom_capacity(kind)) return false;
    if (state.generation == tx[3]) return true;
    if (!write_bytes(slot_address(kind, slot), &tx[4], ring_record_size(kind))) return false;
    return board_eeprom_write(address, 0u);
}

static bool table_recover(void)
{
    uint8_t stored_crc[2], tx[LOG_EEPROM_TRANSACTION_SIZE];
    if (!read_bytes(LOG_EEPROM_SEGMENT_TABLE_BASE, segment_years, LOG_SEGMENT_ID_COUNT) ||
        !read_bytes(LOG_EEPROM_TABLE_CRC_BASE, stored_crc, 2u)) return false;
    if (!state.valid) return true;
    uint8_t active = (uint8_t)(state.active_epoch % LOG_SEGMENT_ID_COUNT);
    uint16_t crc = crc16(segment_years, LOG_SEGMENT_ID_COUNT);
    bool correct = stored_crc[0] == (uint8_t)crc && stored_crc[1] == (uint8_t)(crc >> 8u);
    if (!read_bytes(LOG_EEPROM_TRANSACTION_BASE, tx, sizeof(tx))) return false;
    if (tx[0] == EEPROM_TX_MARKER && tx[3] == state.generation && crc8(&tx[1], 14u) == tx[15]) {
        log_record_t record;
        log_record_kind_t kind = (log_record_kind_t)(tx[1] & 3u);
        uint8_t data[LOG_ALERT_DATA_BYTES];
        if (tx[2] >= log_eeprom_capacity(kind) ||
            !read_bytes(slot_address(kind, tx[2]), data, ring_record_size(kind)) ||
            !log_record_decode(kind, data, ring_record_size(kind), &record)) return false;
        uint8_t year = tx[1] >> 2u;
        if (!correct || segment_years[record.segment_id] != year)
            return update_segment_table(record.segment_id, year);
    }
    return correct && segment_years[active] == state.active_year;
}

static bool recover_all(void)
{
    uint8_t marker;
    if (!transaction_recover() || !table_recover() ||
        !board_eeprom_read(LOG_EEPROM_TRANSACTION_BASE, &marker)) return false;
    return marker != EEPROM_TX_MARKER || board_eeprom_write(LOG_EEPROM_TRANSACTION_BASE, 0u);
}

bool log_eeprom_init(void)
{
    ++read_revision;
    eeprom_state_t first = {0};
    eeprom_state_t second = {0};
    if (!decode_state(EEPROM_STATE_A, &first)) first.valid = false;
    if (!decode_state(EEPROM_STATE_B, &second)) second.valid = false;
    if (!first.valid && !second.valid &&
        (legacy_state_present(EEPROM_STATE_A) ||
         legacy_state_present(EEPROM_STATE_B))) {
        /* The v3 record layout changes ring offsets and carries coverage.
         * Keep settings/checkpoints intact; discard only legacy log indexes. */
        if (!board_eeprom_write(LOG_EEPROM_TRANSACTION_BASE, 0u)) return false;
        for (uint8_t i = 0u; i < LOG_SEGMENT_ID_COUNT; ++i) {
            segment_years[i] = 0xFFu;
            if (!board_eeprom_write((uint16_t)(LOG_EEPROM_SEGMENT_TABLE_BASE + i),
                    0xFFu)) return false;
        }
        uint16_t table_crc = crc16(segment_years, LOG_SEGMENT_ID_COUNT);
        if (!board_eeprom_write(LOG_EEPROM_TABLE_CRC_BASE,
                (uint8_t)table_crc) ||
            !board_eeprom_write(LOG_EEPROM_TABLE_CRC_BASE + 1u,
                (uint8_t)(table_crc >> 8u))) return false;
        state = (eeprom_state_t){0};
        state.active_year = 0xFFu;
        state.valid = false;
        return save_state() && save_state();
    }
    if (first.valid && second.valid) {
        state = (int8_t)(second.generation - first.generation) > 0 ?
            second : first;
    } else if (first.valid) {
        state = first;
    } else if (second.valid) {
        state = second;
    } else {
        state = (eeprom_state_t){0};
        state.active_year = 0xFFu;
    }
    return recover_all();
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

bool log_eeprom_active_epoch(uint16_t *epoch)
{
    if (epoch == NULL || !state.valid) return false;
    *epoch = state.active_epoch;
    return true;
}

uint16_t log_eeprom_record_epoch(uint8_t segment)
{
    return (uint16_t)(state.active_epoch - ((state.active_epoch - segment) & 63u));
}

bool log_eeprom_reset(bool keep_days)
{
    if (!transaction_recover()) return false;
    for (uint8_t i = 0u; i < 4u; ++i) {
        if (keep_days && i == LOG_RECORD_DAY) continue;
        state.head[i] = 0u;
        state.count[i] = 0u;
    }
    return save_state();
}

static bool write_transaction(log_record_kind_t kind, uint8_t slot,
                              uint8_t next_generation, uint8_t year)
{
    uint8_t tx[LOG_EEPROM_TRANSACTION_SIZE];
    const uint16_t base = LOG_EEPROM_TRANSACTION_BASE;
    const uint8_t size = ring_record_size(kind);
    for (uint8_t i = 0u; i < sizeof(tx); ++i) tx[i] = 0u;
    tx[1] = (uint8_t)((uint8_t)kind | (year << 2u));
    tx[2] = slot;
    tx[3] = next_generation;
    if (!read_bytes(slot_address(kind, slot), &tx[4], size)) return false;
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

static bool eeprom_exact_match(log_record_kind_t kind, uint8_t age,
                                uint16_t epoch, uint8_t year, uint32_t index,
                                log_record_t *record)
{
    log_record_t candidate;
    if (!log_eeprom_read(kind, age, &candidate) || candidate.period_index != index ||
        log_eeprom_record_epoch(candidate.segment_id) != epoch || segment_years[candidate.segment_id] != year) return false;
    *record = candidate;
    return true;
}

bool log_eeprom_query_read(void *context, log_record_kind_t kind,
                          uint16_t epoch, uint32_t period_start,
                          log_record_t *record, bool *live)
{
    const log_query_time_context_t *time_context = context;
    bool calendar = time_context == NULL || time_context->calendar_valid;
    uint32_t index;
    uint8_t year;
    if ((uint8_t)kind > LOG_RECORD_ALERT || record == NULL || live == NULL ||
        !log_record_location(kind, period_start, calendar, &index, &year)) return false;
    eeprom_read_cache_t *cache = &read_cache[(uint8_t)kind];
    if (cache->revision != read_revision || cache->epoch != epoch || cache->year != year) {
        *cache = (eeprom_read_cache_t){.revision=read_revision, .epoch=epoch, .year=year, .lower=UINT32_MAX, .age=UINT8_MAX};
    }
    uint8_t count = log_eeprom_count(kind);
    if (!cache->bounds) {
        for (uint8_t age = 0u; age < count; ++age) {
            log_record_t candidate;
            if (!log_eeprom_read(kind, age, &candidate) ||
                log_eeprom_record_epoch(candidate.segment_id) != epoch || segment_years[candidate.segment_id] != year) continue;
            if (candidate.period_index < cache->lower) cache->lower = candidate.period_index;
            if (candidate.period_index > cache->upper) cache->upper = candidate.period_index;
        }
        cache->bounds = true;
    }
    if (index < cache->lower || index > cache->upper) return false;
    *live = false;
    if (cache->age < count) {
        if (eeprom_exact_match(kind, cache->age, epoch, year, index, record)) return true;
        if (cache->age > 0u && eeprom_exact_match(kind, (uint8_t)(cache->age - 1u), epoch, year, index, record)) {
            --cache->age; return true;
        }
    }
    for (uint8_t age = 0u; age < count; ++age) {
        if (eeprom_exact_match(kind, age, epoch, year, index, record)) {
            cache->age = age; return true;
        }
    }
    return false;
}

/* Rotate survivors through the ring. Each move has the same rollback
 * protection as an append; a reset may leave the purge unfinished but never
 * changes the meaning of any still-addressed slot. */
static bool purge_segment(uint8_t segment)
{
    for (uint8_t ring = 0u; ring < 4u; ++ring) {
        log_record_kind_t kind = (log_record_kind_t)ring;
        uint8_t remaining = state.count[ring];
        bool present = false;
        for (uint8_t age = 0u; age < remaining; ++age) {
            log_record_t item;
            if (!log_eeprom_read(kind, age, &item)) return false;
            if (item.segment_id == segment) present = true;
        }
        if (!present) continue;
        while (remaining-- != 0u) {
            log_record_t item;
            eeprom_state_t before = state;
            uint8_t bytes[LOG_ALERT_DATA_BYTES];
            if (!log_eeprom_read(kind, (uint8_t)(state.count[ring] - 1u), &item)) return false;
            if (item.segment_id == segment) {
                --state.count[ring];
            } else {
                uint8_t slot = state.head[ring];
                if (!log_record_encode(kind, &item, bytes, sizeof(bytes)) ||
                    !write_transaction(kind, slot, (uint8_t)(state.generation + 1u), segment_years[item.segment_id]) ||
                    !write_bytes(slot_address(kind, slot), bytes, ring_record_size(kind))) return false;
                state.head[ring] = (uint8_t)((slot + 1u) % log_eeprom_capacity(kind));
            }
            if (!save_state()) { state = before; (void)transaction_recover(); return false; }
            if (!board_eeprom_write(LOG_EEPROM_TRANSACTION_BASE, 0u)) return false;
        }
    }
    return true;
}

bool log_eeprom_append(log_record_kind_t kind, uint16_t epoch, const log_record_t *record,
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
        record->segment_id != epoch % LOG_SEGMENT_ID_COUNT ||
        !log_record_encode(kind, record, bytes, sizeof(bytes))) return false;
    if (!recover_all()) return false;
    if (state.valid && epoch < state.active_epoch && (uint16_t)(state.active_epoch - epoch) >= 64u) return false;
    if (state.valid && epoch > state.active_epoch &&
        !purge_segment(record->segment_id)) return false;
    previous_state = state;
    slot = state.head[index];
    next_generation = (uint8_t)(state.generation + 1u);
    if (!write_transaction(kind, slot, next_generation, year_descriptor) ||
        !write_bytes(slot_address(kind, slot), bytes,
            ring_record_size(kind)) ||
        !read_bytes(slot_address(kind, slot), verify,
            ring_record_size(kind)) ||
        !log_record_decode(kind, verify, ring_record_size(kind),
            &(log_record_t){0}) ||
        memcmp(bytes, verify, ring_record_size(kind)) != 0) return false;

    state.head[index] = (uint8_t)((slot + 1u) % capacity);
    if (state.count[index] < capacity) ++state.count[index];
    if (!state.valid || epoch >= state.active_epoch) {
        state.active_epoch = epoch;
        state.active_year = year_descriptor;
    }
    if (!save_state()) {
        state = previous_state;
        (void)transaction_recover();
        return false;
    }
    if (segment_years[record->segment_id] != year_descriptor &&
        !update_segment_table(record->segment_id, year_descriptor)) return false;
    return board_eeprom_write(LOG_EEPROM_TRANSACTION_BASE, 0u);
}

bool log_eeprom_find(log_record_kind_t kind, uint16_t before_epoch,
                      uint32_t before_time, uint16_t skip, bool inclusive,
                      uint32_t date_limit, uint32_t reference, log_stored_t *item)
{
    const uint8_t index = ring_index(kind);
    const uint8_t count = log_eeprom_count(kind);
    eeprom_find_cache_t *cache;
    if (index == 0xFFu || item == NULL) return false;
    cache = &find_cache[index];

    /* Ring age follows append order. For monotonic non-alert logs, continue
     * from the next older slot instead of rescanning newer EEPROM entries. */
    if (kind != LOG_RECORD_ALERT && skip == 0u && !inclusive &&
        cache->valid && cache->revision == read_revision &&
        cache->ordered && cache->date_limit == date_limit &&
        (before_epoch < cache->epoch ||
         (before_epoch == cache->epoch && before_time <= cache->time)) &&
        (uint16_t)cache->age + 1u < count) {
        for (uint8_t age = (uint8_t)(cache->age + 1u); age < count; ++age) {
            log_record_t record;
            uint8_t year;
            uint32_t time;
            if (!log_eeprom_read(kind, age, &record) ||
                !log_eeprom_segment_year(record.segment_id, &year) ||
                !log_record_time(kind, record.period_index, year, reference, &time)) continue;
            uint16_t epoch = log_eeprom_record_epoch(record.segment_id);
            if (epoch > before_epoch || (date_limit && time > date_limit) ||
                (epoch == before_epoch && time >= before_time)) continue;
            *item = (log_stored_t){record, time, epoch, year,
                (uint16_t)((state.head[index] + log_eeprom_capacity(kind) - 1u - age) % log_eeprom_capacity(kind))};
            *cache = (eeprom_find_cache_t){read_revision, date_limit,
                time, epoch, age, true, true};
            return true;
        }
        return false;
    }

    bool found = false;
    bool ordered = true;
    bool have_previous = false;
    uint16_t previous_epoch = 0u;
    uint32_t previous_time = 0u;
    uint8_t best_age = 0u;
    for (uint8_t age = 0u; age < count; ++age) {
        log_record_t record;
        uint8_t year;
        uint32_t time;
        if (!log_eeprom_read(kind, age, &record) ||
            !log_eeprom_segment_year(record.segment_id, &year) ||
            !log_record_time(kind, record.period_index, year, reference, &time)) continue;
        uint16_t epoch = log_eeprom_record_epoch(record.segment_id);
        if (have_previous && (epoch > previous_epoch ||
            (epoch == previous_epoch && time > previous_time))) ordered = false;
        previous_epoch = epoch;
        previous_time = time;
        have_previous = true;
        if (epoch > before_epoch || (date_limit && time > date_limit) ||
            (epoch == before_epoch && (time > before_time || (!inclusive && time == before_time)))) continue;
        if (epoch == before_epoch && time == before_time && skip) { --skip; continue; }
        if (!found || epoch > item->epoch || (epoch == item->epoch && time > item->time)) {
            *item = (log_stored_t){record, time, epoch, year, (uint16_t)((state.head[index] + log_eeprom_capacity(kind) - 1u - age) % log_eeprom_capacity(kind))};
            best_age = age;
            found = true;
        }
    }
    if (found && kind != LOG_RECORD_ALERT && skip == 0u && !inclusive) {
        *cache = (eeprom_find_cache_t){read_revision, date_limit,
            item->time, item->epoch, best_age, ordered, true};
    } else {
        cache->valid = false;
    }
    return found;
}

bool log_eeprom_expire_alerts(uint8_t year)
{
    while (state.count[LOG_RECORD_ALERT]) {
        log_record_t record;
        if (!log_eeprom_read(LOG_RECORD_ALERT, (uint8_t)(state.count[LOG_RECORD_ALERT] - 1u), &record)) return false;
        uint8_t saved = segment_years[record.segment_id];
        if (!(saved & LOG_YEAR_VALID) || ((year - saved) & LOG_YEAR_MASK) < 15u) break;
        eeprom_state_t before = state;
        --state.count[LOG_RECORD_ALERT];
        if (!save_state()) { state = before; return false; }
    }
    return true;
}
