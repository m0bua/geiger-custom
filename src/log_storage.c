#include "log_storage.h"

#include "board.h"
#include "spi_nor.h"

#include <stddef.h>
#include <stdint.h>

#define EEPROM_LOG_BASE 205u
#define EEPROM_HOUR_COUNT 72u
#define EEPROM_HOUR_SIZE 11u
#define EEPROM_DAY_BASE (EEPROM_LOG_BASE + EEPROM_HOUR_COUNT * EEPROM_HOUR_SIZE)
#define EEPROM_DAY_COUNT 87u
#define EEPROM_DAY_SIZE 12u
#define EEPROM_CHECKPOINT_BASE 140u
#define EEPROM_HOUR_CHECKPOINT_SIZE 14u
#define EEPROM_DAY_CHECKPOINT_SIZE 15u

#define NOR_SECTOR_SIZE 4096u
#define NOR_SECTOR_HEADER_SIZE 16u
#define NOR_MINUTE_RECORD_SIZE 11u
#define NOR_HOUR_RECORD_SIZE 11u
#define NOR_ALERT_RECORD_SIZE 22u
#define NOR_CUTOFF_RECORD_SIZE 25u
#define NOR_MINUTE_SECTORS 117u
#define NOR_ALERT_SECTORS 55u
#define NOR_CUTOFF_SECTORS 4u
#define NOR_HOUR_BASE_SECTOR (NOR_MINUTE_SECTORS + NOR_ALERT_SECTORS + \
                              NOR_CUTOFF_SECTORS)
#define NOR_HOUR_SECTORS (256u - NOR_HOUR_BASE_SECTOR)
#define NOR_MINUTE_RECORDS_PER_SECTOR ((NOR_SECTOR_SIZE - \
    NOR_SECTOR_HEADER_SIZE) / NOR_MINUTE_RECORD_SIZE)
#define NOR_HOUR_RECORDS_PER_SECTOR ((NOR_SECTOR_SIZE - \
    NOR_SECTOR_HEADER_SIZE) / NOR_HOUR_RECORD_SIZE)

#define NOR_RING_MINUTE 1u
#define NOR_RING_HOUR 2u
#define NOR_RING_CUTOFF 3u
#define NOR_RING_ALERT 4u
#define NOR_ALERT_RECORDS_PER_SECTOR ((NOR_SECTOR_SIZE - \
    NOR_SECTOR_HEADER_SIZE) / NOR_ALERT_RECORD_SIZE)

typedef struct {
    uint8_t type;
    uint16_t base_sector;
    uint16_t sector_count;
    uint16_t record_size;
    uint16_t records_per_sector;
    uint16_t active_sector;
    uint16_t next_slot;
    uint32_t sector_sequence;
    uint16_t record_sequence;
    bool initialized;
} nor_ring_t;

static bool storage_ready;
static bool storage_spi;
static nor_ring_t minute_ring;
static nor_ring_t hour_ring;
static nor_ring_t cutoff_ring;
static nor_ring_t alert_ring;
static uint16_t minute_sequence;
static uint16_t hour_sequence;
static uint16_t day_sequence;
static uint8_t hour_checkpoint_generation;
static uint8_t day_checkpoint_generation;
static uint32_t minute_dose;
static uint32_t minute_min_cpm;
static uint32_t minute_max_cpm;
static uint8_t minute_seconds;
static uint32_t hour_dose;
static uint32_t hour_min_cpm;
static uint32_t hour_max_cpm;
static uint8_t hour_minutes;
static uint32_t day_dose;
static uint32_t day_min_cpm;
static uint32_t day_max_cpm;
static uint16_t day_minutes;
static uint16_t alert_run_id;
static uint32_t alert_start_uptime;
static uint32_t alert_end_uptime;
static uint32_t alert_dose;
static uint32_t alert_max_cpm;
static uint32_t alert_seconds_since_save;
static uint8_t alert_severity;
static bool alert_active;

static uint8_t crc8(const uint8_t *data, uint16_t length)
{
    uint8_t crc = 0u;
    for (uint16_t i = 0u; i < length; ++i) {
        crc ^= data[i];
        for (uint8_t bit = 0u; bit < 8u; ++bit) {
            crc = (crc & 0x80u) != 0u ?
                (uint8_t)((crc << 1u) ^ 0x07u) : (uint8_t)(crc << 1u);
        }
    }
    return crc;
}

static void put_bits(uint8_t *out, uint16_t start, uint32_t value,
                     uint8_t width)
{
    for (uint8_t bit = 0u; bit < width; ++bit) {
        const uint16_t position = (uint16_t)(start + bit);
        if ((value & (1u << bit)) != 0u) {
            out[position / 8u] |= (uint8_t)(1u << (position % 8u));
        }
    }
}

static uint32_t get_bits(const uint8_t *in, uint16_t start, uint8_t width)
{
    uint32_t value = 0u;
    for (uint8_t bit = 0u; bit < width; ++bit) {
        const uint16_t position = (uint16_t)(start + bit);
        if ((in[position / 8u] & (1u << (position % 8u))) != 0u) {
            value |= 1u << bit;
        }
    }
    return value;
}

static void encode_minute(uint8_t record[NOR_MINUTE_RECORD_SIZE],
                          uint16_t sequence, uint32_t dose, uint32_t minimum,
                          uint32_t maximum, bool complete)
{
    for (uint8_t i = 0u; i < NOR_MINUTE_RECORD_SIZE; ++i) record[i] = 0u;
    put_bits(record, 0u, sequence, 16u);
    put_bits(record, 16u, dose > 0x3FFFFu ? 0x3FFFFu : dose, 18u);
    put_bits(record, 34u, minimum > 0x3FFFFu ? 0x3FFFFu : minimum, 18u);
    put_bits(record, 52u, maximum > 0x3FFFFu ? 0x3FFFFu : maximum, 18u);
    record[8] = (uint8_t)((record[8] & 0x3Fu) |
        (complete ? 0x40u : 0xC0u));
    record[9] = crc8(record, 9u);
    record[10] = 0xA1u;
}

static bool decode_minute(const uint8_t record[NOR_MINUTE_RECORD_SIZE],
                          uint16_t *sequence)
{
    if (record[10] != 0xA1u ||
        ((record[8] & 0xC0u) != 0x40u &&
         (record[8] & 0xC0u) != 0xC0u) ||
        record[9] != crc8(record, 9u)) return false;
    *sequence = (uint16_t)get_bits(record, 0u, 16u);
    return true;
}

static void encode_hour(uint8_t record[NOR_HOUR_RECORD_SIZE],
                        uint16_t sequence, uint32_t dose, uint32_t minimum,
                        uint32_t maximum, bool complete)
{
    for (uint8_t i = 0u; i < NOR_HOUR_RECORD_SIZE; ++i) record[i] = 0u;
    put_bits(record, 0u, sequence, 16u);
    put_bits(record, 16u, dose > 0xFFFFFFu ? 0xFFFFFFu : dose, 24u);
    put_bits(record, 40u, minimum > 0x3FFFFu ? 0x3FFFFu : minimum, 18u);
    put_bits(record, 58u, maximum > 0x3FFFFu ? 0x3FFFFu : maximum, 18u);
    record[9] = (uint8_t)((record[9] & 0x0Fu) |
        (complete ? 0xB0u : 0xA0u));
    record[10] = crc8(record, 10u);
}

static bool decode_hour(const uint8_t record[NOR_HOUR_RECORD_SIZE],
                        uint16_t *sequence)
{
    if (((record[9] & 0xF0u) != 0xA0u &&
         (record[9] & 0xF0u) != 0xB0u) ||
        record[10] != crc8(record, 10u)) return false;
    *sequence = (uint16_t)get_bits(record, 0u, 16u);
    return true;
}

static void encode_alert(uint8_t record[NOR_ALERT_RECORD_SIZE],
                         uint16_t sequence, uint16_t run_id,
                         uint32_t start_uptime, uint32_t end_uptime,
                         uint32_t dose, uint32_t max_cpm, uint8_t severity,
                         bool active, bool interrupted)
{
    for (uint8_t i = 0u; i < NOR_ALERT_RECORD_SIZE; ++i) record[i] = 0u;
    record[0] = (uint8_t)sequence;
    record[1] = (uint8_t)(sequence >> 8u);
    record[2] = (uint8_t)run_id;
    record[3] = (uint8_t)(run_id >> 8u);
    for (uint8_t i = 0u; i < 4u; ++i) {
        record[4u + i] = (uint8_t)(start_uptime >> (i * 8u));
        record[8u + i] = (uint8_t)(end_uptime >> (i * 8u));
        record[12u + i] = (uint8_t)(dose >> (i * 8u));
    }
    record[16] = (uint8_t)max_cpm;
    record[17] = (uint8_t)(max_cpm >> 8u);
    record[18] = (uint8_t)((max_cpm >> 16u) & 0x03u);
    record[19] = (uint8_t)((severity & 0x03u) |
        (active ? 0x04u : 0u) | (interrupted ? 0x08u : 0u));
    record[20] = crc8(record, 20u);
    record[21] = 0xA2u;
}

static bool decode_alert(const uint8_t record[NOR_ALERT_RECORD_SIZE],
                         uint16_t *sequence)
{
    if (record[21] != 0xA2u || record[20] != crc8(record, 20u)) {
        return false;
    }
    *sequence = (uint16_t)record[0] | ((uint16_t)record[1] << 8u);
    return true;
}

static void encode_day(uint8_t record[EEPROM_DAY_SIZE], uint16_t sequence,
                       uint32_t dose, uint32_t minimum, uint32_t maximum,
                       bool complete)
{
    for (uint8_t i = 0u; i < EEPROM_DAY_SIZE; ++i) record[i] = 0u;
    put_bits(record, 0u, sequence, 16u);
    put_bits(record, 16u, dose > 0x1FFFFFFFu ? 0x1FFFFFFFu : dose, 29u);
    put_bits(record, 45u, minimum > 0x3FFFFu ? 0x3FFFFu : minimum, 18u);
    put_bits(record, 63u, maximum > 0x3FFFFu ? 0x3FFFFu : maximum, 18u);
    record[10] = (uint8_t)((record[10] & 0x01u) |
        (complete ? 0xAAu : 0x54u));
    record[11] = crc8(record, 11u);
}

static bool decode_day(const uint8_t record[EEPROM_DAY_SIZE],
                       uint16_t *sequence)
{
    if (((record[10] & 0xFEu) != 0x54u &&
         (record[10] & 0xFEu) != 0xAAu) ||
        record[11] != crc8(record, 11u)) return false;
    *sequence = (uint16_t)get_bits(record, 0u, 16u);
    return true;
}

static bool sequence_newer(uint16_t candidate, uint16_t current)
{
    return (int16_t)(candidate - current) > 0;
}

static bool eeprom_read_record(uint16_t address, uint8_t *record,
                               uint8_t size)
{
    for (uint8_t i = 0u; i < size; ++i) {
        if (!board_eeprom_read((uint16_t)(address + i), &record[i])) {
            return false;
        }
    }
    return true;
}

static bool eeprom_write_record(uint16_t address, const uint8_t *record,
                                uint8_t size)
{
    if (!board_eeprom_write((uint16_t)(address + size - 1u), 0xFFu)) {
        return false;
    }
    for (uint8_t i = 0u; i < size - 1u; ++i) {
        if (!board_eeprom_write((uint16_t)(address + i), record[i])) {
            return false;
        }
    }
    return board_eeprom_write((uint16_t)(address + size - 1u),
                              record[size - 1u]);
}

static bool nor_program_bytes(uint32_t address, const uint8_t *data,
                              uint16_t length)
{
    while (length != 0u) {
        const uint16_t page_room = (uint16_t)(256u - (address & 0xFFu));
        const uint16_t amount = length < page_room ? length : page_room;
        if (!spi_nor_program_page(address, data, amount)) return false;
        address += amount;
        data += amount;
        length = (uint16_t)(length - amount);
    }
    return true;
}

static void make_sector_header(uint8_t header[NOR_SECTOR_HEADER_SIZE],
                               uint8_t type, uint32_t sequence)
{
    for (uint8_t i = 0u; i < NOR_SECTOR_HEADER_SIZE; ++i) header[i] = 0xFFu;
    header[0] = 'G'; header[1] = '2'; header[2] = 'L'; header[3] = 'G';
    header[4] = type;
    header[5] = (uint8_t)sequence;
    header[6] = (uint8_t)(sequence >> 8u);
    header[7] = (uint8_t)(sequence >> 16u);
    header[8] = (uint8_t)(sequence >> 24u);
    header[9] = crc8(header, 9u);
}

static bool parse_sector_header(const uint8_t header[NOR_SECTOR_HEADER_SIZE],
                                uint8_t type, uint32_t *sequence)
{
    if (header[0] != 'G' || header[1] != '2' || header[2] != 'L' ||
        header[3] != 'G' || header[4] != type ||
        header[9] != crc8(header, 9u)) return false;
    *sequence = (uint32_t)header[5] | ((uint32_t)header[6] << 8u) |
        ((uint32_t)header[7] << 16u) | ((uint32_t)header[8] << 24u);
    return true;
}

static bool ring_read_record(const nor_ring_t *ring, uint16_t slot,
                             uint8_t *record)
{
    const uint32_t address = (uint32_t)(ring->base_sector +
        ring->active_sector) * NOR_SECTOR_SIZE + NOR_SECTOR_HEADER_SIZE +
        (uint32_t)slot * ring->record_size;
    return spi_nor_read(address, record, ring->record_size);
}

static bool alert_find_latest(uint8_t latest[NOR_ALERT_RECORD_SIZE])
{
    uint8_t header[NOR_SECTOR_HEADER_SIZE];
    uint8_t candidate[NOR_ALERT_RECORD_SIZE];
    uint16_t best_sequence = 0u;
    bool found = false;

    if (!alert_ring.initialized) return false;
    for (uint8_t back = 0u; back < 2u; ++back) {
        const uint16_t sector = (uint16_t)((alert_ring.active_sector +
            alert_ring.sector_count - back) % alert_ring.sector_count);
        const uint32_t address = (uint32_t)(alert_ring.base_sector + sector) *
            NOR_SECTOR_SIZE;
        uint32_t sector_sequence;
        if (!spi_nor_read(address, header, sizeof(header)) ||
            !parse_sector_header(header, NOR_RING_ALERT, &sector_sequence)) {
            continue;
        }
        (void)sector_sequence;
        for (uint16_t slot = 0u;
             slot < alert_ring.records_per_sector; ++slot) {
            uint16_t sequence;
            const uint32_t record_address = address + NOR_SECTOR_HEADER_SIZE +
                (uint32_t)slot * NOR_ALERT_RECORD_SIZE;
            if (!spi_nor_read(record_address, candidate, sizeof(candidate))) {
                return false;
            }
            if (!decode_alert(candidate, &sequence)) continue;
            if (!found || (int16_t)(sequence - best_sequence) > 0) {
                found = true;
                best_sequence = sequence;
                for (uint8_t i = 0u; i < NOR_ALERT_RECORD_SIZE; ++i) {
                    latest[i] = candidate[i];
                }
            }
        }
    }
    return found;
}

static bool ring_record_valid(const nor_ring_t *ring, const uint8_t *record,
                              uint16_t *sequence)
{
    if (ring->type == NOR_RING_MINUTE) {
        return decode_minute(record, sequence);
    }
    if (ring->type == NOR_RING_CUTOFF) {
        if (record[24] != 0xC1u || record[23] != crc8(record, 23u)) {
            return false;
        }
        *sequence = (uint16_t)record[0] | ((uint16_t)record[1] << 8u);
        return true;
    }
    if (ring->type == NOR_RING_ALERT) {
        return decode_alert(record, sequence);
    }
    return decode_hour(record, sequence);
}

static bool ring_start_sector(nor_ring_t *ring, uint16_t sector_index,
                              uint32_t sector_sequence)
{
    uint8_t header[NOR_SECTOR_HEADER_SIZE];
    const uint16_t physical_sector = (uint16_t)(ring->base_sector +
        sector_index);
    if (!spi_nor_erase_sector((uint32_t)physical_sector * NOR_SECTOR_SIZE)) {
        return false;
    }
    make_sector_header(header, ring->type, sector_sequence);
    if (!nor_program_bytes((uint32_t)physical_sector * NOR_SECTOR_SIZE,
                           header, NOR_SECTOR_HEADER_SIZE)) return false;
    ring->active_sector = sector_index;
    ring->sector_sequence = sector_sequence;
    ring->next_slot = 0u;
    ring->record_sequence = 0u;
    ring->initialized = true;
    return true;
}

static bool ring_init(nor_ring_t *ring)
{
    uint8_t header[NOR_SECTOR_HEADER_SIZE];
    uint32_t best_sequence = 0u;
    uint16_t best_sector = 0u;
    bool found = false;

    for (uint16_t sector = 0u; sector < ring->sector_count; ++sector) {
        const uint32_t address = (uint32_t)(ring->base_sector + sector) *
            NOR_SECTOR_SIZE;
        uint32_t sequence;
        if (spi_nor_read(address, header, sizeof(header)) &&
            parse_sector_header(header, ring->type, &sequence) &&
            (!found || (int32_t)(sequence - best_sequence) > 0)) {
            found = true;
            best_sequence = sequence;
            best_sector = sector;
        }
    }
    if (!found) {
        ring->initialized = false;
        return true;
    }

    ring->active_sector = best_sector;
    ring->sector_sequence = best_sequence;
    ring->next_slot = 0u;
    ring->record_sequence = 0u;
    ring->initialized = true;
    while (ring->next_slot < ring->records_per_sector) {
        uint8_t record[NOR_CUTOFF_RECORD_SIZE];
        uint16_t sequence;
        if (!ring_read_record(ring, ring->next_slot, record)) return false;
        if (!ring_record_valid(ring, record, &sequence)) {
            bool erased = true;
            for (uint8_t i = 0u; i < ring->record_size; ++i) {
                if (record[i] != 0xFFu) erased = false;
            }
            if (!erased) ring->next_slot = ring->records_per_sector;
            break;
        }
        ring->record_sequence = (uint16_t)(sequence + 1u);
        ++ring->next_slot;
    }
    return true;
}

static bool ring_append(nor_ring_t *ring, const uint8_t *record)
{
    if (!ring->initialized) {
        if (!ring_start_sector(ring, 0u, 0u)) return false;
    } else if (ring->next_slot >= ring->records_per_sector) {
        const uint16_t next_sector = (uint16_t)((ring->active_sector + 1u) %
            ring->sector_count);
        if (!ring_start_sector(ring, next_sector, ring->sector_sequence + 1u)) {
            return false;
        }
    }
    const uint32_t address = (uint32_t)(ring->base_sector +
        ring->active_sector) * NOR_SECTOR_SIZE + NOR_SECTOR_HEADER_SIZE +
        (uint32_t)ring->next_slot * ring->record_size;
    if (!nor_program_bytes(address, record, ring->record_size)) return false;
    ++ring->next_slot;
    ring->record_sequence = (uint16_t)(ring->record_sequence + 1u);
    return true;
}

static void eeprom_find_sequences(void)
{
    uint8_t record[EEPROM_DAY_SIZE];
    uint16_t candidate;
    bool have_hour = false;
    bool have_day = false;
    uint16_t newest_hour = 0u;
    uint16_t newest_day = 0u;

    for (uint16_t slot = 0u; slot < EEPROM_HOUR_COUNT; ++slot) {
        if (eeprom_read_record((uint16_t)(EEPROM_LOG_BASE +
                slot * EEPROM_HOUR_SIZE), record, EEPROM_HOUR_SIZE) &&
            decode_hour(record, &candidate) &&
            (!have_hour || sequence_newer(candidate, newest_hour))) {
            have_hour = true;
            newest_hour = candidate;
        }
    }
    for (uint16_t slot = 0u; slot < EEPROM_DAY_COUNT; ++slot) {
        if (eeprom_read_record((uint16_t)(EEPROM_DAY_BASE +
                slot * EEPROM_DAY_SIZE), record, EEPROM_DAY_SIZE) &&
            decode_day(record, &candidate) &&
            (!have_day || sequence_newer(candidate, newest_day))) {
            have_day = true;
            newest_day = candidate;
        }
    }
    hour_sequence = have_hour ? (uint16_t)(newest_hour + 1u) : 0u;
    day_sequence = have_day ? (uint16_t)(newest_day + 1u) : 0u;
}

static bool eeprom_write_hour(uint32_t dose, uint32_t minimum,
                              uint32_t maximum, bool complete)
{
    uint8_t record[EEPROM_HOUR_SIZE] = {0u};
    const uint16_t slot = (uint16_t)(hour_sequence % EEPROM_HOUR_COUNT);
    put_bits(record, 0u, hour_sequence, 16u);
    put_bits(record, 16u, dose > 0xFFFFFFu ? 0xFFFFFFu : dose, 24u);
    put_bits(record, 40u, minimum > 0x3FFFFu ? 0x3FFFFu : minimum, 18u);
    put_bits(record, 58u, maximum > 0x3FFFFu ? 0x3FFFFu : maximum, 18u);
    record[9] = (uint8_t)((record[9] & 0x0Fu) |
        (complete ? 0xB0u : 0xA0u));
    record[10] = crc8(record, 10u);
    if (!eeprom_write_record((uint16_t)(EEPROM_LOG_BASE +
            slot * EEPROM_HOUR_SIZE), record, EEPROM_HOUR_SIZE)) return false;
    if (complete) ++hour_sequence;
    return true;
}

static bool eeprom_write_day(uint32_t dose, uint32_t minimum,
                             uint32_t maximum, bool complete)
{
    uint8_t record[EEPROM_DAY_SIZE];
    const uint16_t slot = (uint16_t)(day_sequence % EEPROM_DAY_COUNT);
    encode_day(record, day_sequence, dose, minimum, maximum, complete);
    if (!eeprom_write_record((uint16_t)(EEPROM_DAY_BASE +
            slot * EEPROM_DAY_SIZE), record, EEPROM_DAY_SIZE)) return false;
    if (complete) ++day_sequence;
    return true;
}

static bool hour_checkpoint_read(uint8_t slot, uint16_t *sequence,
                                 uint8_t *generation, uint32_t *dose,
                                 uint32_t *minimum, uint32_t *maximum)
{
    uint8_t record[EEPROM_HOUR_CHECKPOINT_SIZE];
    const uint16_t address = (uint16_t)(EEPROM_CHECKPOINT_BASE +
        slot * EEPROM_HOUR_CHECKPOINT_SIZE);
    if (!eeprom_read_record(address, record, sizeof(record)) ||
        record[12] != 0xC3u || record[13] != crc8(record, 13u)) return false;
    *sequence = (uint16_t)record[0] | ((uint16_t)record[1] << 8u);
    *generation = record[2];
    *dose = (uint32_t)record[3] | ((uint32_t)record[4] << 8u) |
        ((uint32_t)record[5] << 16u);
    *minimum = (uint32_t)record[6] | ((uint32_t)record[7] << 8u) |
        ((uint32_t)record[8] << 16u);
    *maximum = (uint32_t)record[9] | ((uint32_t)record[10] << 8u) |
        ((uint32_t)record[11] << 16u);
    return true;
}

static bool day_checkpoint_read(uint8_t slot, uint16_t *sequence,
                                uint8_t *generation, uint32_t *dose,
                                uint32_t *minimum, uint32_t *maximum)
{
    uint8_t record[EEPROM_DAY_CHECKPOINT_SIZE];
    const uint16_t address = (uint16_t)(EEPROM_CHECKPOINT_BASE +
        2u * EEPROM_HOUR_CHECKPOINT_SIZE +
        slot * EEPROM_DAY_CHECKPOINT_SIZE);
    if (!eeprom_read_record(address, record, sizeof(record)) ||
        record[13] != 0xD3u || record[14] != crc8(record, 14u)) return false;
    *sequence = (uint16_t)record[0] | ((uint16_t)record[1] << 8u);
    *generation = record[2];
    *dose = (uint32_t)record[3] | ((uint32_t)record[4] << 8u) |
        ((uint32_t)record[5] << 16u) | ((uint32_t)record[6] << 24u);
    *minimum = (uint32_t)record[7] | ((uint32_t)record[8] << 8u) |
        ((uint32_t)record[9] << 16u);
    *maximum = (uint32_t)record[10] | ((uint32_t)record[11] << 8u) |
        ((uint32_t)record[12] << 16u);
    return true;
}

static bool hour_checkpoint_write(uint16_t sequence, uint8_t generation,
                                  uint32_t dose, uint32_t minimum,
                                  uint32_t maximum)
{
    uint8_t record[EEPROM_HOUR_CHECKPOINT_SIZE];
    const uint8_t slot = generation & 1u;
    const uint16_t address = (uint16_t)(EEPROM_CHECKPOINT_BASE +
        slot * EEPROM_HOUR_CHECKPOINT_SIZE);
    record[0] = (uint8_t)sequence;
    record[1] = (uint8_t)(sequence >> 8u);
    record[2] = generation;
    for (uint8_t i = 0u; i < 3u; ++i) {
        record[3u + i] = (uint8_t)(dose >> (i * 8u));
        record[6u + i] = (uint8_t)(minimum >> (i * 8u));
        record[9u + i] = (uint8_t)(maximum >> (i * 8u));
    }
    record[12] = 0xC3u;
    record[13] = crc8(record, 13u);
    return eeprom_write_record(address, record, sizeof(record));
}

static bool day_checkpoint_write(uint16_t sequence, uint8_t generation,
                                 uint32_t dose, uint32_t minimum,
                                 uint32_t maximum)
{
    uint8_t record[EEPROM_DAY_CHECKPOINT_SIZE];
    const uint8_t slot = generation & 1u;
    const uint16_t address = (uint16_t)(EEPROM_CHECKPOINT_BASE +
        2u * EEPROM_HOUR_CHECKPOINT_SIZE +
        slot * EEPROM_DAY_CHECKPOINT_SIZE);
    record[0] = (uint8_t)sequence;
    record[1] = (uint8_t)(sequence >> 8u);
    record[2] = generation;
    for (uint8_t i = 0u; i < 4u; ++i) {
        record[3u + i] = (uint8_t)(dose >> (i * 8u));
    }
    for (uint8_t i = 0u; i < 3u; ++i) {
        record[7u + i] = (uint8_t)(minimum >> (i * 8u));
        record[10u + i] = (uint8_t)(maximum >> (i * 8u));
    }
    record[13] = 0xD3u;
    record[14] = crc8(record, 14u);
    return eeprom_write_record(address, record, sizeof(record));
}

static void eeprom_recover_checkpoints(void)
{
    uint16_t seq0, seq1;
    uint8_t generation0, generation1;
    uint32_t dose0, dose1, min0, min1, max0, max1;
    const bool hour0 = hour_checkpoint_read(0u, &seq0, &generation0,
        &dose0, &min0, &max0);
    const bool hour1 = hour_checkpoint_read(1u, &seq1, &generation1,
        &dose1, &min1, &max1);
    bool choose1 = hour1 && (!hour0 ||
        (int8_t)(generation1 - generation0) > 0);
    if (hour0 || hour1) {
        const uint16_t sequence = choose1 ? seq1 : seq0;
        hour_checkpoint_generation = (uint8_t)((choose1 ? generation1 :
            generation0) + 1u);
        if (sequence_newer(sequence, (uint16_t)(hour_sequence - 1u)) ||
            hour_sequence == 0u) {
            hour_sequence = sequence;
            (void)eeprom_write_hour(choose1 ? dose1 : dose0,
                choose1 ? min1 : min0, choose1 ? max1 : max0, false);
            hour_sequence = (uint16_t)(sequence + 1u);
        }
    }

    const bool day0 = day_checkpoint_read(0u, &seq0, &generation0,
        &dose0, &min0, &max0);
    const bool day1 = day_checkpoint_read(1u, &seq1, &generation1,
        &dose1, &min1, &max1);
    choose1 = day1 && (!day0 ||
        (int8_t)(generation1 - generation0) > 0);
    if (day0 || day1) {
        const uint16_t sequence = choose1 ? seq1 : seq0;
        day_checkpoint_generation = (uint8_t)((choose1 ? generation1 :
            generation0) + 1u);
        if (sequence_newer(sequence, (uint16_t)(day_sequence - 1u)) ||
            day_sequence == 0u) {
            day_sequence = sequence;
            (void)eeprom_write_day(choose1 ? dose1 : dose0,
                choose1 ? min1 : min0, choose1 ? max1 : max0, false);
            day_sequence = (uint16_t)(sequence + 1u);
        }
    }
}

static bool nor_append_minute(uint32_t dose, uint32_t minimum,
                              uint32_t maximum, bool complete)
{
    uint8_t record[NOR_MINUTE_RECORD_SIZE];
    encode_minute(record, minute_ring.record_sequence, dose, minimum, maximum,
        complete);
    return ring_append(&minute_ring, record);
}

static bool nor_append_hour(uint32_t dose, uint32_t minimum,
                            uint32_t maximum, bool complete)
{
    uint8_t record[NOR_HOUR_RECORD_SIZE];
    encode_hour(record, hour_ring.record_sequence, dose, minimum, maximum,
        complete);
    return ring_append(&hour_ring, record);
}

static void reset_interval(uint32_t *dose, uint32_t *minimum,
                           uint32_t *maximum)
{
    *dose = 0u;
    *minimum = UINT32_MAX;
    *maximum = 0u;
}

void log_storage_init(bool use_spi)
{
    reset_interval(&minute_dose, &minute_min_cpm, &minute_max_cpm);
    reset_interval(&hour_dose, &hour_min_cpm, &hour_max_cpm);
    reset_interval(&day_dose, &day_min_cpm, &day_max_cpm);
    minute_seconds = 0u;
    hour_minutes = 0u;
    day_minutes = 0u;
    minute_sequence = 0u;
    storage_spi = use_spi && spi_nor_is_ready();
    storage_ready = true;
    eeprom_find_sequences();
    eeprom_recover_checkpoints();

    minute_ring.type = NOR_RING_MINUTE;
    minute_ring.base_sector = 0u;
    minute_ring.sector_count = NOR_MINUTE_SECTORS;
    minute_ring.record_size = NOR_MINUTE_RECORD_SIZE;
    minute_ring.records_per_sector = NOR_MINUTE_RECORDS_PER_SECTOR;
    minute_ring.initialized = false;
    hour_ring.type = NOR_RING_HOUR;
    hour_ring.base_sector = NOR_HOUR_BASE_SECTOR;
    hour_ring.sector_count = NOR_HOUR_SECTORS;
    hour_ring.record_size = NOR_HOUR_RECORD_SIZE;
    hour_ring.records_per_sector = NOR_HOUR_RECORDS_PER_SECTOR;
    hour_ring.initialized = false;

    cutoff_ring.type = NOR_RING_CUTOFF;
    cutoff_ring.base_sector = (uint16_t)(NOR_MINUTE_SECTORS +
        NOR_ALERT_SECTORS);
    cutoff_ring.sector_count = NOR_CUTOFF_SECTORS;
    cutoff_ring.record_size = NOR_CUTOFF_RECORD_SIZE;
    cutoff_ring.records_per_sector = (uint16_t)((NOR_SECTOR_SIZE -
        NOR_SECTOR_HEADER_SIZE) / NOR_CUTOFF_RECORD_SIZE);
    cutoff_ring.initialized = false;
    alert_ring.type = NOR_RING_ALERT;
    alert_ring.base_sector = NOR_MINUTE_SECTORS;
    alert_ring.sector_count = NOR_ALERT_SECTORS;
    alert_ring.record_size = NOR_ALERT_RECORD_SIZE;
    alert_ring.records_per_sector = NOR_ALERT_RECORDS_PER_SECTOR;
    alert_ring.initialized = false;

    if (storage_spi) {
        storage_spi = ring_init(&minute_ring) && ring_init(&hour_ring) &&
            ring_init(&cutoff_ring) && ring_init(&alert_ring);
        if (storage_spi) {
            uint8_t record[NOR_ALERT_RECORD_SIZE];
            if (alert_find_latest(record) && (record[19] & 0x04u) != 0u) {
                const uint32_t start = (uint32_t)record[4] |
                    ((uint32_t)record[5] << 8u) |
                    ((uint32_t)record[6] << 16u) |
                    ((uint32_t)record[7] << 24u);
                const uint32_t end = (uint32_t)record[8] |
                    ((uint32_t)record[9] << 8u) |
                    ((uint32_t)record[10] << 16u) |
                    ((uint32_t)record[11] << 24u);
                const uint32_t dose = (uint32_t)record[12] |
                    ((uint32_t)record[13] << 8u) |
                    ((uint32_t)record[14] << 16u) |
                    ((uint32_t)record[15] << 24u);
                const uint32_t maximum = (uint32_t)record[16] |
                    ((uint32_t)record[17] << 8u) |
                    ((uint32_t)(record[18] & 0x03u) << 16u);
                uint8_t interrupted[NOR_ALERT_RECORD_SIZE];
                encode_alert(interrupted, alert_ring.record_sequence,
                    (uint16_t)record[2] | ((uint16_t)record[3] << 8u),
                    start, end, dose, maximum,
                    (uint8_t)(record[19] & 0x03u), false, true);
                if (!ring_append(&alert_ring, interrupted)) {
                    storage_spi = false;
                }
            }
        }
    }
}

void log_storage_sample_cpm(uint32_t cpm)
{
    if (!storage_ready) return;
    if (cpm < minute_min_cpm) minute_min_cpm = cpm;
    if (cpm > minute_max_cpm) minute_max_cpm = cpm;
    if (alert_active && cpm > alert_max_cpm) alert_max_cpm = cpm;
}

static uint32_t add_saturated(uint32_t current, uint32_t increment,
                              uint32_t maximum)
{
    return increment > maximum - current ? maximum : current + increment;
}

static void close_minute(bool complete)
{
    const uint32_t min_value = minute_min_cpm == UINT32_MAX ? 0u :
        minute_min_cpm;
    if (storage_spi) {
        if (!nor_append_minute(minute_dose, min_value, minute_max_cpm,
                complete)) {
            storage_spi = false;
        }
    }

    hour_dose = add_saturated(hour_dose, minute_dose, 0xFFFFFFu);
    day_dose = add_saturated(day_dose, minute_dose, 0x1FFFFFFFu);
    if (min_value < hour_min_cpm) hour_min_cpm = min_value;
    if (minute_max_cpm > hour_max_cpm) hour_max_cpm = minute_max_cpm;
    if (min_value < day_min_cpm) day_min_cpm = min_value;
    if (minute_max_cpm > day_max_cpm) day_max_cpm = minute_max_cpm;

    if (complete) {
        ++hour_minutes;
        ++day_minutes;
    }
    {
        const bool hour_complete = hour_minutes >= 60u;
        const uint32_t hour_min = hour_min_cpm == UINT32_MAX ? 0u :
            hour_min_cpm;
        if (hour_checkpoint_write(hour_sequence, hour_checkpoint_generation,
                hour_dose, hour_min, hour_max_cpm)) {
            ++hour_checkpoint_generation;
        }
        if (hour_complete) {
            (void)eeprom_write_hour(hour_dose, hour_min, hour_max_cpm, true);
        }
        if (storage_spi && hour_complete &&
            !nor_append_hour(hour_dose, hour_min, hour_max_cpm, true)) {
            storage_spi = false;
        }
        if (hour_complete) {
            hour_minutes = 0u;
            reset_interval(&hour_dose, &hour_min_cpm, &hour_max_cpm);
        }
    }
    {
        const bool day_complete = day_minutes >= 1440u;
        const uint32_t day_min = day_min_cpm == UINT32_MAX ? 0u :
            day_min_cpm;
        if (day_checkpoint_write(day_sequence, day_checkpoint_generation,
                day_dose, day_min, day_max_cpm)) {
            ++day_checkpoint_generation;
        }
        if (day_complete) {
            (void)eeprom_write_day(day_dose, day_min, day_max_cpm, true);
        }
        if (day_complete) {
            day_minutes = 0u;
            reset_interval(&day_dose, &day_min_cpm, &day_max_cpm);
        }
    }

    reset_interval(&minute_dose, &minute_min_cpm, &minute_max_cpm);
    minute_seconds = 0u;
}

static void close_time_segment(void)
{
    if (minute_seconds != 0u) close_minute(false);

    if (hour_minutes != 0u || hour_dose != 0u || hour_min_cpm != UINT32_MAX) {
        const uint32_t minimum = hour_min_cpm == UINT32_MAX ? 0u :
            hour_min_cpm;
        if (eeprom_write_hour(hour_dose, minimum, hour_max_cpm, false)) {
            ++hour_sequence;
        }
        if (storage_spi && !nor_append_hour(hour_dose, minimum, hour_max_cpm,
                false)) {
            storage_spi = false;
        }
        hour_minutes = 0u;
        reset_interval(&hour_dose, &hour_min_cpm, &hour_max_cpm);
    }

    if (day_minutes != 0u || day_dose != 0u || day_min_cpm != UINT32_MAX) {
        const uint32_t minimum = day_min_cpm == UINT32_MAX ? 0u :
            day_min_cpm;
        if (eeprom_write_day(day_dose, minimum, day_max_cpm, false)) {
            ++day_sequence;
        }
        day_minutes = 0u;
        reset_interval(&day_dose, &day_min_cpm, &day_max_cpm);
    }
}

void log_storage_second(uint32_t pulse_count)
{
    if (!storage_ready) return;
    minute_dose = add_saturated(minute_dose, pulse_count, 0x3FFFFu);
    if (minute_seconds < 60u) ++minute_seconds;
    if (minute_seconds >= 60u) close_minute(true);
}

static bool alert_append(bool active, bool interrupted)
{
    uint8_t record[NOR_ALERT_RECORD_SIZE];
    encode_alert(record, alert_ring.record_sequence, alert_run_id,
        alert_start_uptime, alert_end_uptime, alert_dose, alert_max_cpm,
        alert_severity, active, interrupted);
    if (!ring_append(&alert_ring, record)) {
        storage_spi = false;
        return false;
    }
    return true;
}

void log_storage_alert_sample(uint32_t cpm, uint32_t pulse_count,
                              uint32_t uptime_seconds, bool rate_valid)
{
    const uint64_t rate_scaled = (uint64_t)cpm * 666u;
    const bool above_alert_threshold = rate_valid &&
        rate_scaled >= 40000u;
    if (!storage_ready || !storage_spi) return;

    if (!alert_active) {
        if (!above_alert_threshold) return;
        alert_active = true;
        alert_start_uptime = uptime_seconds;
        alert_end_uptime = uptime_seconds;
        alert_dose = pulse_count;
        alert_max_cpm = cpm;
        alert_severity = rate_scaled > 90000u ? 2u : 1u;
        alert_seconds_since_save = 0u;
        (void)alert_append(true, false);
        return;
    }

    alert_end_uptime = uptime_seconds;
    alert_dose = add_saturated(alert_dose, pulse_count, UINT32_MAX);
    if (cpm > alert_max_cpm) alert_max_cpm = cpm;
    if (rate_scaled > 90000u) alert_severity = 2u;
    if (alert_seconds_since_save < UINT32_MAX) ++alert_seconds_since_save;

    if (!above_alert_threshold) {
        (void)alert_append(false, false);
        alert_active = false;
        alert_seconds_since_save = 0u;
    } else if (alert_seconds_since_save >= 60u) {
        (void)alert_append(true, false);
        alert_seconds_since_save = 0u;
    }
}

bool log_storage_spi_available(void)
{
    return storage_spi;
}

bool log_storage_minute_history_load(uint32_t *cpm, uint8_t capacity,
                                    uint8_t *count)
{
    uint8_t header[NOR_SECTOR_HEADER_SIZE];
    uint8_t record[NOR_MINUTE_RECORD_SIZE];
    uint8_t used = 0u;

    if (count != NULL) *count = 0u;
    if (!storage_spi || cpm == NULL || count == NULL || capacity == 0u ||
        !minute_ring.initialized) return false;

    for (uint8_t back = 0u; back < 2u && used < capacity; ++back) {
        const uint16_t sector = (uint16_t)((minute_ring.active_sector +
            minute_ring.sector_count - back) % minute_ring.sector_count);
        const uint32_t base = (uint32_t)(minute_ring.base_sector + sector) *
            NOR_SECTOR_SIZE;
        uint32_t sector_sequence;
        const uint16_t top_slot = back == 0u ? minute_ring.next_slot :
            minute_ring.records_per_sector;
        if (!spi_nor_read(base, header, sizeof(header)) ||
            !parse_sector_header(header, NOR_RING_MINUTE, &sector_sequence)) {
            continue;
        }
        (void)sector_sequence;
        for (uint16_t slot = top_slot;
             slot > 0u && used < capacity; --slot) {
            uint16_t sequence;
            const uint32_t address = base + NOR_SECTOR_HEADER_SIZE +
                (uint32_t)(slot - 1u) * NOR_MINUTE_RECORD_SIZE;
            if (!spi_nor_read(address, record, sizeof(record))) return false;
            if (!decode_minute(record, &sequence) ||
                (record[8] & 0xC0u) != 0x40u) continue;
            (void)sequence;
            cpm[used++] = get_bits(record, 16u, 18u);
        }
    }

    for (uint8_t left = 0u; left < used / 2u; ++left) {
        const uint8_t right = (uint8_t)(used - 1u - left);
        const uint32_t temporary = cpm[left];
        cpm[left] = cpm[right];
        cpm[right] = temporary;
    }
    *count = used;
    return true;
}

static bool erase_nor_sectors(uint16_t first, uint16_t count)
{
    if (!spi_nor_is_ready() && !spi_nor_init()) return false;
    for (uint16_t i = 0u; i < count; ++i) {
        if (!spi_nor_erase_sector((uint32_t)(first + i) * NOR_SECTOR_SIZE)) {
            return false;
        }
    }
    return true;
}

static bool clear_eeprom_range(uint16_t first, uint16_t end)
{
    for (uint16_t address = first; address < end; ++address) {
        if (!board_eeprom_write(address, 0xFFu)) return false;
    }
    return true;
}

bool log_storage_clear(log_clear_target_t target)
{
    bool ok = true;

    switch (target) {
    case LOG_CLEAR_MINUTE:
        ok = erase_nor_sectors(0u, NOR_MINUTE_SECTORS);
        break;
    case LOG_CLEAR_HOUR:
        ok = clear_eeprom_range(EEPROM_CHECKPOINT_BASE,
                EEPROM_CHECKPOINT_BASE + 2u * EEPROM_HOUR_CHECKPOINT_SIZE) &&
            clear_eeprom_range(EEPROM_LOG_BASE,
                EEPROM_LOG_BASE + EEPROM_HOUR_COUNT * EEPROM_HOUR_SIZE) &&
            erase_nor_sectors(NOR_HOUR_BASE_SECTOR, NOR_HOUR_SECTORS);
        break;
    case LOG_CLEAR_DAY:
        ok = clear_eeprom_range(EEPROM_CHECKPOINT_BASE +
                2u * EEPROM_HOUR_CHECKPOINT_SIZE,
                EEPROM_CHECKPOINT_BASE + 2u * EEPROM_HOUR_CHECKPOINT_SIZE +
                    2u * EEPROM_DAY_CHECKPOINT_SIZE) &&
            clear_eeprom_range(EEPROM_DAY_BASE,
                EEPROM_DAY_BASE + EEPROM_DAY_COUNT * EEPROM_DAY_SIZE);
        break;
    case LOG_CLEAR_ERROR:
        ok = erase_nor_sectors(NOR_MINUTE_SECTORS, NOR_ALERT_SECTORS);
        break;
    case LOG_CLEAR_LOGS:
        ok = clear_eeprom_range(EEPROM_CHECKPOINT_BASE, 2048u) &&
            erase_nor_sectors(0u, 256u);
        break;
    case LOG_CLEAR_EEPROM_LOGS:
        ok = clear_eeprom_range(EEPROM_CHECKPOINT_BASE, 2048u);
        break;
    case LOG_CLEAR_SPI:
        ok = erase_nor_sectors(0u,
            (uint16_t)(spi_nor_size_bytes() / NOR_SECTOR_SIZE));
        break;
    default:
        return false;
    }

    if (!ok) return false;
    switch (target) {
    case LOG_CLEAR_MINUTE:
        minute_ring.initialized = false;
        reset_interval(&minute_dose, &minute_min_cpm, &minute_max_cpm);
        minute_seconds = 0u;
        break;
    case LOG_CLEAR_HOUR:
        hour_ring.initialized = false;
        hour_sequence = 0u;
        hour_checkpoint_generation = 0u;
        hour_minutes = 0u;
        reset_interval(&hour_dose, &hour_min_cpm, &hour_max_cpm);
        break;
    case LOG_CLEAR_DAY:
        day_sequence = 0u;
        day_checkpoint_generation = 0u;
        day_minutes = 0u;
        reset_interval(&day_dose, &day_min_cpm, &day_max_cpm);
        break;
    case LOG_CLEAR_ERROR:
        alert_ring.initialized = false;
        alert_active = false;
        break;
    case LOG_CLEAR_LOGS:
        minute_ring.initialized = false;
        hour_ring.initialized = false;
        cutoff_ring.initialized = false;
        alert_ring.initialized = false;
        alert_active = false;
        minute_sequence = 0u;
        hour_sequence = 0u;
        day_sequence = 0u;
        hour_checkpoint_generation = 0u;
        day_checkpoint_generation = 0u;
        minute_seconds = 0u;
        hour_minutes = 0u;
        day_minutes = 0u;
        reset_interval(&minute_dose, &minute_min_cpm, &minute_max_cpm);
        reset_interval(&hour_dose, &hour_min_cpm, &hour_max_cpm);
        reset_interval(&day_dose, &day_min_cpm, &day_max_cpm);
        break;
    case LOG_CLEAR_EEPROM_LOGS:
        hour_sequence = 0u;
        day_sequence = 0u;
        hour_checkpoint_generation = 0u;
        day_checkpoint_generation = 0u;
        minute_seconds = 0u;
        hour_minutes = 0u;
        day_minutes = 0u;
        reset_interval(&minute_dose, &minute_min_cpm, &minute_max_cpm);
        reset_interval(&hour_dose, &hour_min_cpm, &hour_max_cpm);
        reset_interval(&day_dose, &day_min_cpm, &day_max_cpm);
        break;
    case LOG_CLEAR_SPI:
        minute_ring.initialized = false;
        hour_ring.initialized = false;
        cutoff_ring.initialized = false;
        alert_ring.initialized = false;
        alert_active = false;
        break;
    default:
        return false;
    }
    return true;
}

void log_storage_time_cutoff(uint32_t uptime_seconds, uint32_t calendar_seconds,
                             bool calendar_valid, uint8_t reason)
{
    uint8_t record[NOR_CUTOFF_RECORD_SIZE] = {0u};
    if (!storage_ready) return;
    if (reason == LOG_CUTOFF_REASON_CLOCK_SET) close_time_segment();
    if (!storage_spi) return;
    record[0] = (uint8_t)cutoff_ring.record_sequence;
    record[1] = (uint8_t)(cutoff_ring.record_sequence >> 8u);
    record[2] = (uint8_t)uptime_seconds;
    record[3] = (uint8_t)(uptime_seconds >> 8u);
    record[4] = (uint8_t)(uptime_seconds >> 16u);
    record[5] = (uint8_t)(uptime_seconds >> 24u);
    record[6] = (uint8_t)calendar_seconds;
    record[7] = (uint8_t)(calendar_seconds >> 8u);
    record[8] = (uint8_t)(calendar_seconds >> 16u);
    record[9] = (uint8_t)(calendar_seconds >> 24u);
    record[10] = (uint8_t)((reason & 0x7Fu) |
        (calendar_valid ? 0x80u : 0u));
    record[11] = (uint8_t)minute_ring.sector_sequence;
    record[12] = (uint8_t)(minute_ring.sector_sequence >> 8u);
    record[13] = (uint8_t)(minute_ring.sector_sequence >> 16u);
    record[14] = (uint8_t)(minute_ring.sector_sequence >> 24u);
    record[15] = (uint8_t)minute_ring.next_slot;
    record[16] = (uint8_t)(minute_ring.next_slot >> 8u);
    record[17] = (uint8_t)hour_ring.sector_sequence;
    record[18] = (uint8_t)(hour_ring.sector_sequence >> 8u);
    record[19] = (uint8_t)(hour_ring.sector_sequence >> 16u);
    record[20] = (uint8_t)(hour_ring.sector_sequence >> 24u);
    record[21] = (uint8_t)hour_ring.next_slot;
    record[22] = (uint8_t)(hour_ring.next_slot >> 8u);
    record[23] = crc8(record, 23u);
    record[24] = 0xC1u;
    if (reason == LOG_CUTOFF_REASON_BOOT) {
        alert_run_id = cutoff_ring.record_sequence;
    }
    (void)ring_append(&cutoff_ring, record);
}
