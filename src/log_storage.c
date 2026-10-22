#include "log_storage.h"

#include "board.h"
#include "spi_nor.h"

#include <stddef.h>
#include <stdint.h>

#define EEPROM_LOG_REGION_BASE 140u
#define EEPROM_CUTOFF_SLOT0_BASE 140u
#define EEPROM_CUTOFF_SLOT1_BASE 181u
#define EEPROM_CUTOFF_SIZE 24u
#define EEPROM_CUTOFF_MAGIC 0xC1u
#define EEPROM_DAY_BASE 205u
#define EEPROM_DAY_COUNT 100u
#define EEPROM_DAY_SIZE 12u
/* Previous firmware placed its 87-day ring after a 72-entry hour ring. */
#define EEPROM_LEGACY_DAY_BASE 997u
#define EEPROM_LEGACY_DAY_COUNT 87u
#define EEPROM_DAY_CHECKPOINT_BASE 1406u
#define EEPROM_DAY_CHECKPOINT_COUNT 8u
#define EEPROM_DAY_CHECKPOINT_SIZE 20u
#define EEPROM_DAY_CHECKPOINT_MAGIC 0xD6u
#define EEPROM_DAY_CHECKPOINT_V5_BASE 1806u
#define EEPROM_DAY_CHECKPOINT_V5_SIZE 17u
#define EEPROM_DAY_CHECKPOINT_V5_MAGIC 0xD5u
#define EEPROM_DAY_CHECKPOINT_LEGACY_MAGIC 0xD4u
#define EEPROM_DAY_CHECKPOINT_LEGACY_BASE 168u
#define EEPROM_DAY_CHECKPOINT_PREVIOUS_BASE 164u
#define EEPROM_DAY_UNKNOWN_KEY UINT16_MAX

#define NOR_SECTOR_SIZE 4096u
#define NOR_SECTOR_HEADER_SIZE 16u
#define NOR_MINUTE_RECORD_SIZE 11u
#define NOR_HOUR_RECORD_SIZE 11u
#define NOR_ALERT_RECORD_SIZE 28u
#define NOR_MINUTE_SECTORS 117u
#define NOR_ALERT_SECTORS 55u
/* Four formerly-unused cutoff sectors now hold wear-levelled RAM snapshots. */
#define NOR_LIVE_MINUTE_BASE_SECTOR (NOR_MINUTE_SECTORS + NOR_ALERT_SECTORS)
#define NOR_LIVE_HOUR_BASE_SECTOR (NOR_LIVE_MINUTE_BASE_SECTOR + 2u)
#define NOR_LIVE_SECTORS 2u
#define NOR_LIVE_RECORD_SIZE 22u
/* Keep the hour ring at its existing sector offset for log compatibility. */
#define NOR_HOUR_BASE_SECTOR (NOR_LIVE_HOUR_BASE_SECTOR + NOR_LIVE_SECTORS)
#define NOR_HOUR_SECTORS (256u - NOR_HOUR_BASE_SECTOR)

#define NOR_RING_MINUTE 1u
#define NOR_RING_HOUR 2u
#define NOR_RING_ALERT 3u
#define NOR_RING_LIVE_MINUTE 5u
#define NOR_RING_LIVE_HOUR 6u

typedef struct {
    uint8_t type;
    uint16_t base_sector;
    uint16_t sector_count;
    uint16_t record_size;
    uint16_t records_per_sector;
    uint16_t active_sector;
    uint16_t next_slot;
    uint32_t sector_sequence;
    uint32_t total_records;
    uint16_t record_sequence;
    bool initialized;
} nor_ring_t;

static bool storage_ready;
static bool storage_spi;
static bool storage_spi_requested;
static uint32_t storage_spi_retry_at;
static nor_ring_t minute_ring;
static nor_ring_t hour_ring;
static nor_ring_t alert_ring;
static nor_ring_t minute_live_ring;
static nor_ring_t hour_live_ring;
static uint16_t day_sequence;
static uint16_t cutoff_sequence;
static uint16_t eeprom_day_count;
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
static uint32_t day_seconds;
static uint16_t day_calendar_key = EEPROM_DAY_UNKNOWN_KEY;
static bool day_calendar_key_valid;
static uint16_t alert_run_id;
static uint32_t alert_start_uptime;
static uint32_t alert_start_calendar;
static uint32_t alert_duration_seconds;
static uint32_t alert_dose_pulses;
static uint32_t alert_max_cpm;
static uint32_t alert_seconds_since_save;
static bool alert_active;
static uint32_t last_live_minute_save;
static bool graph_hours_cover_current_day;
static uint32_t last_live_day_save;

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
         (record[9] & 0xF0u) != 0xB0u &&
         (record[9] & 0xF0u) != 0xC0u) ||
        record[10] != crc8(record, 10u)) return false;
    *sequence = (uint16_t)get_bits(record, 0u, 16u);
    return true;
}

static void encode_alert(uint8_t record[NOR_ALERT_RECORD_SIZE],
                         uint16_t sequence, uint16_t run_id,
                         uint32_t start_uptime, uint32_t start_calendar,
                         uint32_t duration_seconds, uint32_t dose_pulses,
                         uint32_t max_cpm,
                         bool active, bool interrupted)
{
    for (uint8_t i = 0u; i < NOR_ALERT_RECORD_SIZE; ++i) record[i] = 0u;
    record[0] = (uint8_t)sequence;
    record[1] = (uint8_t)(sequence >> 8u);
    for (uint8_t i = 0u; i < 4u; ++i) {
        record[2u + i] = (uint8_t)(start_uptime >> (i * 8u));
        record[6u + i] = (uint8_t)(start_calendar >> (i * 8u));
        record[10u + i] = (uint8_t)(duration_seconds >> (i * 8u));
        record[14u + i] = (uint8_t)(dose_pulses >> (i * 8u));
    }
    record[18] = (uint8_t)max_cpm;
    record[19] = (uint8_t)(max_cpm >> 8u);
    record[20] = (uint8_t)((max_cpm >> 16u) & 0x03u);
    /* Severity is derived later from max_cpm; do not duplicate it on media. */
    record[21] = (uint8_t)((active ? 0x04u : 0u) |
        (interrupted ? 0x08u : 0u));
    record[22] = (uint8_t)run_id;
    record[23] = (uint8_t)(run_id >> 8u);
    record[26] = crc8(record, 26u);
    record[27] = 0xA3u;
}

static bool decode_alert(const uint8_t record[NOR_ALERT_RECORD_SIZE],
                         uint16_t *sequence)
{
    if (record[27] != 0xA3u || record[26] != crc8(record, 26u)) {
        return false;
    }
    *sequence = (uint16_t)record[0] | ((uint16_t)record[1] << 8u);
    return true;
}

static void encode_live(uint8_t record[NOR_LIVE_RECORD_SIZE],
                        uint16_t sequence, uint32_t period_key,
                        uint32_t dose, uint32_t minimum, uint32_t maximum,
                        uint8_t progress, uint8_t type)
{
    for (uint8_t i = 0u; i < NOR_LIVE_RECORD_SIZE; ++i) record[i] = 0u;
    record[0] = (uint8_t)sequence;
    record[1] = (uint8_t)(sequence >> 8u);
    for (uint8_t i = 0u; i < 4u; ++i) {
        record[2u + i] = (uint8_t)(period_key >> (i * 8u));
        record[6u + i] = (uint8_t)(dose >> (i * 8u));
    }
    for (uint8_t i = 0u; i < 3u; ++i) {
        record[10u + i] = (uint8_t)(minimum >> (i * 8u));
        record[13u + i] = (uint8_t)(maximum >> (i * 8u));
    }
    record[16] = progress;
    record[17] = type;
    record[20] = crc8(record, 20u);
    record[21] = 0xA4u;
}

static bool decode_live(const uint8_t record[NOR_LIVE_RECORD_SIZE],
                        uint8_t type, uint16_t *sequence)
{
    if (record[21] != 0xA4u || record[17] != type ||
        record[20] != crc8(record, 20u)) return false;
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

static bool ring_read_newest_offset(const nor_ring_t *ring, uint32_t offset,
                                    uint8_t *record)
{
    uint32_t sectors_back;
    uint16_t slot;
    uint16_t sector;
    if (!ring->initialized || offset >= ring->total_records ||
        ring->records_per_sector == 0u) return false;
    if (offset < ring->next_slot) {
        sectors_back = 0u;
        slot = (uint16_t)(ring->next_slot - 1u - offset);
    } else {
        const uint32_t previous_offset = offset - ring->next_slot;
        sectors_back = 1u + previous_offset / ring->records_per_sector;
        slot = (uint16_t)(ring->records_per_sector - 1u -
            previous_offset % ring->records_per_sector);
    }
    if (sectors_back >= ring->sector_count) return false;
    sector = (uint16_t)((ring->active_sector + ring->sector_count -
        sectors_back) % ring->sector_count);
    return spi_nor_read((uint32_t)(ring->base_sector + sector) *
        NOR_SECTOR_SIZE + NOR_SECTOR_HEADER_SIZE +
        (uint32_t)slot * ring->record_size, record, ring->record_size);
}

static bool ring_record_valid(const nor_ring_t *ring, const uint8_t *record,
                              uint16_t *sequence);

/* Sector order is authoritative, including old firmware whose record
 * sequences restarted at each sector. A torn last slot falls back safely. */
static bool ring_find_latest(const nor_ring_t *ring, uint8_t *record)
{
    uint8_t header[NOR_SECTOR_HEADER_SIZE];
    if (!ring->initialized) return false;
    for (uint8_t back = 0u; back < 2u; ++back) {
        const uint16_t sector = (uint16_t)((ring->active_sector +
            ring->sector_count - back) % ring->sector_count);
        const uint32_t base = (uint32_t)(ring->base_sector + sector) *
            NOR_SECTOR_SIZE;
        uint32_t sector_sequence;
        const uint16_t top = back == 0u ? ring->next_slot :
            ring->records_per_sector;
        if (!spi_nor_read(base, header, sizeof(header)) ||
            !parse_sector_header(header, ring->type, &sector_sequence)) continue;
        for (uint16_t slot = top; slot > 0u; --slot) {
            uint16_t sequence;
            if (!spi_nor_read(base + NOR_SECTOR_HEADER_SIZE +
                    (uint32_t)(slot - 1u) * ring->record_size,
                    record, ring->record_size)) return false;
            if (ring_record_valid(ring, record, &sequence)) return true;
        }
    }
    return false;
}

static bool ring_record_valid(const nor_ring_t *ring, const uint8_t *record,
                              uint16_t *sequence)
{
    if (ring->type == NOR_RING_MINUTE) {
        return decode_minute(record, sequence);
    }
    if (ring->type == NOR_RING_ALERT) {
        return decode_alert(record, sequence);
    }
    if (ring->type == NOR_RING_LIVE_MINUTE) {
        return decode_live(record, NOR_RING_LIVE_MINUTE, sequence);
    }
    if (ring->type == NOR_RING_LIVE_HOUR) {
        return decode_live(record, NOR_RING_LIVE_HOUR, sequence);
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
    uint32_t oldest_sequence = 0u;
    uint16_t best_sector = 0u;
    uint16_t valid_sectors = 0u;
    bool found = false;

    for (uint16_t sector = 0u; sector < ring->sector_count; ++sector) {
        const uint32_t address = (uint32_t)(ring->base_sector + sector) *
            NOR_SECTOR_SIZE;
        uint32_t sequence;
        if (spi_nor_read(address, header, sizeof(header)) &&
            parse_sector_header(header, ring->type, &sequence)) {
            if (!found) {
                best_sequence = sequence;
                oldest_sequence = sequence;
                best_sector = sector;
                found = true;
            } else {
                if ((int32_t)(sequence - best_sequence) > 0) {
                    best_sequence = sequence;
                    best_sector = sector;
                }
                if ((int32_t)(oldest_sequence - sequence) > 0) {
                    oldest_sequence = sequence;
                }
            }
            ++valid_sectors;
        }
    }
    if (!found) {
        ring->initialized = false;
        ring->total_records = 0u;
        return true;
    }

    ring->active_sector = best_sector;
    ring->sector_sequence = best_sequence;
    ring->next_slot = 0u;
    ring->record_sequence = 0u;
    ring->initialized = true;
    while (ring->next_slot < ring->records_per_sector) {
        uint8_t record[NOR_ALERT_RECORD_SIZE];
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
    {
        uint32_t occupied = best_sequence - oldest_sequence + 1u;
        const uint32_t sector_capacity = ring->sector_count;
        const uint32_t record_capacity = sector_capacity *
            ring->records_per_sector;
        if (occupied > valid_sectors) occupied = valid_sectors;
        if (occupied > sector_capacity) occupied = sector_capacity;
        ring->total_records = occupied == 0u ? 0u :
            (occupied - 1u) * ring->records_per_sector + ring->next_slot;
        if (ring->total_records > record_capacity) {
            ring->total_records = record_capacity;
        }
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
    if (ring->total_records <
        (uint32_t)ring->sector_count * ring->records_per_sector) {
        ++ring->total_records;
    }
    return true;
}

static void eeprom_find_sequences(void)
{
    uint8_t record[EEPROM_DAY_SIZE];
    uint16_t candidate;
    bool have_day = false;
    uint16_t newest_day = 0u;

    eeprom_day_count = 0u;

    for (uint16_t slot = 0u; slot < EEPROM_DAY_COUNT; ++slot) {
        if (eeprom_read_record((uint16_t)(EEPROM_DAY_BASE +
                slot * EEPROM_DAY_SIZE), record, EEPROM_DAY_SIZE) &&
            decode_day(record, &candidate)) {
            if (!have_day || sequence_newer(candidate, newest_day)) {
                have_day = true;
                newest_day = candidate;
            }
        }
    }
    /* Preserve old daily entries outside the new 100-record window. The
     * overlapping legacy addresses are already covered by the new-ring scan. */
    for (uint16_t slot = 0u; slot < EEPROM_LEGACY_DAY_COUNT; ++slot) {
        const uint16_t address = (uint16_t)(EEPROM_LEGACY_DAY_BASE +
            slot * EEPROM_DAY_SIZE);
        if (address >= EEPROM_DAY_BASE +
                EEPROM_DAY_COUNT * EEPROM_DAY_SIZE &&
            eeprom_read_record(address, record, EEPROM_DAY_SIZE) &&
            decode_day(record, &candidate) &&
            (!have_day || sequence_newer(candidate, newest_day))) {
            have_day = true;
            newest_day = candidate;
        }
    }

    if (have_day) {
        for (uint16_t slot = 0u; slot < EEPROM_DAY_COUNT; ++slot) {
            if (eeprom_read_record((uint16_t)(EEPROM_DAY_BASE +
                    slot * EEPROM_DAY_SIZE), record, EEPROM_DAY_SIZE) &&
                decode_day(record, &candidate) &&
                (uint16_t)(newest_day - candidate) < EEPROM_DAY_COUNT &&
                eeprom_day_count < EEPROM_DAY_COUNT) {
                ++eeprom_day_count;
            }
        }
        for (uint16_t slot = 0u; slot < EEPROM_LEGACY_DAY_COUNT; ++slot) {
            const uint16_t address = (uint16_t)(EEPROM_LEGACY_DAY_BASE +
                slot * EEPROM_DAY_SIZE);
            if (address >= EEPROM_DAY_BASE +
                    EEPROM_DAY_COUNT * EEPROM_DAY_SIZE &&
                eeprom_read_record(address, record, EEPROM_DAY_SIZE) &&
                decode_day(record, &candidate) &&
                (uint16_t)(newest_day - candidate) < EEPROM_DAY_COUNT &&
                eeprom_day_count < EEPROM_DAY_COUNT) {
                ++eeprom_day_count;
            }
        }
    }
    day_sequence = have_day ? (uint16_t)(newest_day + 1u) : 0u;
}

static bool eeprom_write_day(uint32_t dose, uint32_t minimum,
                             uint32_t maximum, bool complete)
{
    uint8_t record[EEPROM_DAY_SIZE];
    const uint16_t slot = (uint16_t)(day_sequence % EEPROM_DAY_COUNT);
    encode_day(record, day_sequence, dose, minimum, maximum, complete);
    if (!eeprom_write_record((uint16_t)(EEPROM_DAY_BASE +
            slot * EEPROM_DAY_SIZE), record, EEPROM_DAY_SIZE)) return false;
    if (eeprom_day_count < EEPROM_DAY_COUNT) ++eeprom_day_count;
    if (complete) ++day_sequence;
    return true;
}

static uint32_t read_le(const uint8_t *record, uint8_t width)
{
    uint32_t value = 0u;
    for (uint8_t i = 0u; i < width; ++i) {
        value |= (uint32_t)record[i] << (i * 8u);
    }
    return value;
}

static void write_le(uint8_t *record, uint32_t value, uint8_t width)
{
    for (uint8_t i = 0u; i < width; ++i) {
        record[i] = (uint8_t)(value >> (i * 8u));
    }
}

static bool day_checkpoint_read(uint16_t address, bool legacy,
                                uint8_t *record)
{
    const uint8_t size = legacy ? EEPROM_DAY_CHECKPOINT_V5_SIZE :
        EEPROM_DAY_CHECKPOINT_SIZE;
    const uint8_t magic = !legacy ? EEPROM_DAY_CHECKPOINT_MAGIC :
        (address == EEPROM_DAY_CHECKPOINT_LEGACY_BASE ||
         address == EEPROM_DAY_CHECKPOINT_LEGACY_BASE +
             EEPROM_DAY_CHECKPOINT_V5_SIZE ?
            EEPROM_DAY_CHECKPOINT_LEGACY_MAGIC :
            EEPROM_DAY_CHECKPOINT_V5_MAGIC);
    return eeprom_read_record(address, record, size) &&
        record[size - 1u] == magic &&
        record[size - 2u] == crc8(record, (uint16_t)(size - 2u));
}

static bool day_checkpoint_write(uint16_t sequence, uint8_t generation,
                                 uint16_t day_key, uint32_t dose,
                                 uint32_t minimum, uint32_t maximum)
{
    uint8_t record[EEPROM_DAY_CHECKPOINT_SIZE];
    const uint16_t address = (uint16_t)(EEPROM_DAY_CHECKPOINT_BASE +
        (uint16_t)(generation % EEPROM_DAY_CHECKPOINT_COUNT) *
            EEPROM_DAY_CHECKPOINT_SIZE);
    write_le(record, sequence, 2u);
    record[2] = generation;
    write_le(&record[3], day_key, 2u);
    write_le(&record[5], dose, 4u);
    write_le(&record[9], minimum, 3u);
    write_le(&record[12], maximum, 3u);
    write_le(&record[15], day_seconds, 3u);
    record[18] = crc8(record, 18u);
    record[19] = EEPROM_DAY_CHECKPOINT_MAGIC;
    return eeprom_write_record(address, record, sizeof(record));
}

static void eeprom_recover_checkpoints(void)
{
    uint8_t candidate[EEPROM_DAY_CHECKPOINT_SIZE];
    uint8_t best[EEPROM_DAY_CHECKPOINT_SIZE];
    bool found = false;
    bool legacy = false;

    /* Read each historical format before writing its migration. New slots
     * live outside the current 100-day history and the former D5 live ring. */
    for (uint8_t format = 0u; format < 3u && !found; ++format) {
        const uint8_t count = format < 2u ? EEPROM_DAY_CHECKPOINT_COUNT : 3u;
        const uint16_t old_addresses[3] = {
            EEPROM_DAY_CHECKPOINT_PREVIOUS_BASE,
            EEPROM_DAY_CHECKPOINT_LEGACY_BASE,
            EEPROM_DAY_CHECKPOINT_LEGACY_BASE + EEPROM_DAY_CHECKPOINT_V5_SIZE
        };
        for (uint8_t slot = 0u; slot < count; ++slot) {
            const uint16_t address = format == 0u ?
                (uint16_t)(EEPROM_DAY_CHECKPOINT_BASE +
                    slot * EEPROM_DAY_CHECKPOINT_SIZE) :
                (format == 1u ?
                    (uint16_t)(EEPROM_DAY_CHECKPOINT_V5_BASE +
                        slot * EEPROM_DAY_CHECKPOINT_V5_SIZE) :
                    old_addresses[slot]);
            if (!day_checkpoint_read(address, format != 0u, candidate)) continue;
            if (!found || (int8_t)(candidate[2] - best[2]) > 0) {
                found = true;
                legacy = format != 0u;
                for (uint8_t i = 0u; i < (legacy ?
                        EEPROM_DAY_CHECKPOINT_V5_SIZE :
                        EEPROM_DAY_CHECKPOINT_SIZE); ++i) best[i] = candidate[i];
            }
        }
    }
    if (!found) return;
    const uint16_t sequence = (uint16_t)read_le(best, 2u);
    day_checkpoint_generation = (uint8_t)(best[2] + 1u);
    day_calendar_key = (uint16_t)read_le(&best[3], 2u);
    day_calendar_key_valid = day_calendar_key != EEPROM_DAY_UNKNOWN_KEY;
    if (sequence_newer(sequence, (uint16_t)(day_sequence - 1u)) ||
        day_sequence == 0u) day_sequence = sequence;
    if (!day_calendar_key_valid) return;
    day_dose = read_le(&best[5], 4u);
    day_min_cpm = read_le(&best[9], 3u);
    day_max_cpm = read_le(&best[12], 3u);
    /* D4/D5 did not store elapsed time; do not invent a one-minute duration. */
    day_seconds = legacy ? 0u : read_le(&best[15], 3u);
    if (legacy && day_checkpoint_write(day_sequence, day_checkpoint_generation,
            day_calendar_key, day_dose, day_min_cpm, day_max_cpm)) {
        ++day_checkpoint_generation;
    }
}

static bool eeprom_cutoff_read(uint16_t address, uint8_t *record,
                               uint16_t *sequence)
{
    if (!eeprom_read_record(address, record, EEPROM_CUTOFF_SIZE) ||
        record[23] != EEPROM_CUTOFF_MAGIC ||
        record[22] != crc8(record, 22u)) return false;
    *sequence = (uint16_t)record[0] | ((uint16_t)record[1] << 8u);
    return true;
}

static void eeprom_cutoff_recover_sequence(void)
{
    uint8_t record[EEPROM_CUTOFF_SIZE];
    uint16_t sequence0 = 0u;
    uint16_t sequence1 = 0u;
    const bool valid0 = eeprom_cutoff_read(EEPROM_CUTOFF_SLOT0_BASE,
        record, &sequence0);
    const bool valid1 = eeprom_cutoff_read(EEPROM_CUTOFF_SLOT1_BASE,
        record, &sequence1);
    if (valid0 || valid1) {
        const uint16_t newest = valid1 && (!valid0 ||
            sequence_newer(sequence1, sequence0)) ? sequence1 : sequence0;
        cutoff_sequence = (uint16_t)(newest + 1u);
    } else {
        cutoff_sequence = 0u;
    }
}

static void eeprom_cutoff_append(uint32_t uptime_seconds,
                                 uint32_t calendar_seconds,
                                 bool calendar_valid, uint8_t reason)
{
    uint8_t record[EEPROM_CUTOFF_SIZE] = {0u};
    const uint16_t address = (cutoff_sequence & 1u) != 0u ?
        EEPROM_CUTOFF_SLOT1_BASE : EEPROM_CUTOFF_SLOT0_BASE;
    record[0] = (uint8_t)cutoff_sequence;
    record[1] = (uint8_t)(cutoff_sequence >> 8u);
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
    for (uint8_t i = 0u; i < 3u; ++i) {
        record[11u + i] = (uint8_t)(minute_ring.sector_sequence >>
            (i * 8u));
        record[16u + i] = (uint8_t)(hour_ring.sector_sequence >>
            (i * 8u));
    }
    record[14] = (uint8_t)minute_ring.next_slot;
    record[15] = (uint8_t)(minute_ring.next_slot >> 8u);
    record[19] = (uint8_t)hour_ring.next_slot;
    record[20] = (uint8_t)(hour_ring.next_slot >> 8u);
    record[21] = storage_spi ? 1u : 0u;
    record[22] = crc8(record, 22u);
    record[23] = EEPROM_CUTOFF_MAGIC;
    if (eeprom_write_record(address, record, EEPROM_CUTOFF_SIZE)) {
        ++cutoff_sequence;
    }
}

static bool eeprom_cutoff_read_pair(uint8_t older[EEPROM_CUTOFF_SIZE],
                                    uint8_t newer[EEPROM_CUTOFF_SIZE])
{
    uint8_t slot0[EEPROM_CUTOFF_SIZE];
    uint8_t slot1[EEPROM_CUTOFF_SIZE];
    uint16_t sequence0;
    uint16_t sequence1;
    const bool valid0 = eeprom_cutoff_read(EEPROM_CUTOFF_SLOT0_BASE,
        slot0, &sequence0);
    const bool valid1 = eeprom_cutoff_read(EEPROM_CUTOFF_SLOT1_BASE,
        slot1, &sequence1);

    if (!valid0 || !valid1) return false;
    const uint8_t *first = sequence_newer(sequence0, sequence1) ?
        slot1 : slot0;
    const uint8_t *last = sequence_newer(sequence0, sequence1) ?
        slot0 : slot1;
    for (uint8_t i = 0u; i < EEPROM_CUTOFF_SIZE; ++i) {
        older[i] = first[i];
        newer[i] = last[i];
    }
    return true;
}

static uint64_t cutoff_ring_position(const nor_ring_t *ring,
                                     const uint8_t *event,
                                     uint8_t sequence_offset,
                                     uint8_t slot_offset)
{
    const uint32_t sequence = read_le(&event[sequence_offset], 3u);
    const uint16_t slot = (uint16_t)read_le(&event[slot_offset], 2u);
    const uint64_t cycle = ((uint64_t)1u << 24u) *
        ring->records_per_sector;
    return ((uint64_t)sequence * ring->records_per_sector + slot) % cycle;
}

static uint32_t cutoff_ring_slots_between(const nor_ring_t *ring,
                                          const uint8_t *from,
                                          const uint8_t *to,
                                          uint8_t sequence_offset,
                                          uint8_t slot_offset,
                                          bool to_is_current)
{
    const uint64_t cycle = ((uint64_t)1u << 24u) *
        ring->records_per_sector;
    const uint64_t start = cutoff_ring_position(ring, from,
        sequence_offset, slot_offset);
    const uint64_t end = to_is_current ?
        (((uint64_t)(ring->sector_sequence & 0xFFFFFFu) *
            ring->records_per_sector + ring->next_slot) % cycle) :
        cutoff_ring_position(ring, to, sequence_offset, slot_offset);
    const uint64_t distance = (end + cycle - start) % cycle;
    const uint32_t ring_capacity = (uint32_t)ring->sector_count *
        ring->records_per_sector;
    return distance > ring_capacity ? UINT32_MAX : (uint32_t)distance;
}

static bool cutoff_previous_record_is_graph_bar(const nor_ring_t *ring,
                                                 const uint8_t *event,
                                                 log_graph_period_t period)
{
    const uint8_t sequence_offset = period == LOG_GRAPH_MINUTES ? 11u : 16u;
    const uint8_t slot_offset = period == LOG_GRAPH_MINUTES ? 14u : 19u;
    uint32_t sequence = read_le(&event[sequence_offset], 3u);
    uint16_t slot = (uint16_t)read_le(&event[slot_offset], 2u);
    if (slot == 0u) {
        sequence = (sequence - 1u) & 0xFFFFFFu;
        slot = ring->records_per_sector;
    }
    if (slot > ring->records_per_sector) return false;

    const uint32_t sector_back = ((ring->sector_sequence & 0xFFFFFFu) -
        sequence) & 0xFFFFFFu;
    if (sector_back >= ring->sector_count) return false;
    const uint16_t sector = (uint16_t)((ring->active_sector +
        ring->sector_count - sector_back) % ring->sector_count);
    const uint32_t address = (uint32_t)(ring->base_sector + sector) *
        NOR_SECTOR_SIZE + NOR_SECTOR_HEADER_SIZE +
        (uint32_t)(slot - 1u) * ring->record_size;
    uint8_t record[NOR_HOUR_RECORD_SIZE];
    uint16_t record_sequence;
    if (!spi_nor_read(address, record, ring->record_size)) return false;
    if (period == LOG_GRAPH_MINUTES) {
        return decode_minute(record, &record_sequence) &&
            (record[8] & 0xC0u) == 0x40u;
    }
    return decode_hour(record, &record_sequence) &&
        (record[9] & 0xF0u) == 0xB0u;
}

static uint32_t calendar_day_boundaries(uint32_t start, uint32_t end)
{
    return end < start ? 0u : end / 86400u - start / 86400u;
}

static bool nor_append_minute(uint32_t dose, uint32_t minimum,
                              uint32_t maximum, bool complete)
{
    uint8_t record[EEPROM_DAY_SIZE];
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

static bool nor_append_hour_day_marker(void)
{
    uint8_t record[NOR_HOUR_RECORD_SIZE];
    encode_hour(record, hour_ring.record_sequence, 0u, 0u, 0u, false);
    record[9] = (uint8_t)((record[9] & 0x0Fu) | 0xC0u);
    record[10] = crc8(record, 10u);
    return ring_append(&hour_ring, record);
}

static void reset_interval(uint32_t *dose, uint32_t *minimum,
                           uint32_t *maximum)
{
    *dose = 0u;
    *minimum = UINT32_MAX;
    *maximum = 0u;
}

static void configure_ring(nor_ring_t *ring, uint8_t type,
                            uint16_t base, uint16_t sectors, uint16_t size)
{
    *ring = (nor_ring_t){ .type = type, .base_sector = base,
        .sector_count = sectors, .record_size = size,
        .records_per_sector = (NOR_SECTOR_SIZE - NOR_SECTOR_HEADER_SIZE) / size };
}

static void configure_nor_rings(void)
{
    configure_ring(&minute_ring, NOR_RING_MINUTE, 0u,
        NOR_MINUTE_SECTORS, NOR_MINUTE_RECORD_SIZE);
    configure_ring(&hour_ring, NOR_RING_HOUR, NOR_HOUR_BASE_SECTOR,
        NOR_HOUR_SECTORS, NOR_HOUR_RECORD_SIZE);
    configure_ring(&alert_ring, NOR_RING_ALERT, NOR_MINUTE_SECTORS,
        NOR_ALERT_SECTORS, NOR_ALERT_RECORD_SIZE);
    configure_ring(&minute_live_ring, NOR_RING_LIVE_MINUTE,
        NOR_LIVE_MINUTE_BASE_SECTOR, NOR_LIVE_SECTORS, NOR_LIVE_RECORD_SIZE);
    configure_ring(&hour_live_ring, NOR_RING_LIVE_HOUR,
        NOR_LIVE_HOUR_BASE_SECTOR, NOR_LIVE_SECTORS, NOR_LIVE_RECORD_SIZE);
}

static bool init_nor_rings(void)
{
    return ring_init(&minute_ring) && ring_init(&hour_ring) &&
        ring_init(&alert_ring) && ring_init(&minute_live_ring) &&
        ring_init(&hour_live_ring);
}

void log_storage_init(bool use_spi)
{
    reset_interval(&minute_dose, &minute_min_cpm, &minute_max_cpm);
    reset_interval(&hour_dose, &hour_min_cpm, &hour_max_cpm);
    reset_interval(&day_dose, &day_min_cpm, &day_max_cpm);
    minute_seconds = 0u;
    hour_minutes = 0u;
    day_seconds = 0u;
    day_calendar_key = EEPROM_DAY_UNKNOWN_KEY;
    day_calendar_key_valid = false;
    last_live_minute_save = 0u;
    last_live_day_save = 0u;
    storage_spi = use_spi && spi_nor_is_ready();
    storage_spi_requested = use_spi;
    storage_spi_retry_at = board_millis + 1000u;
    storage_ready = true;
    eeprom_find_sequences();
    eeprom_recover_checkpoints();
    eeprom_cutoff_recover_sequence();

    configure_nor_rings();

    if (storage_spi) {
        storage_spi = init_nor_rings();
        if (storage_spi) {
            uint8_t record[NOR_ALERT_RECORD_SIZE];
            if (ring_find_latest(&alert_ring, record) &&
                (record[21] & 0x04u) != 0u) {
                const uint32_t start = (uint32_t)record[2] |
                    ((uint32_t)record[3] << 8u) |
                    ((uint32_t)record[4] << 16u) |
                    ((uint32_t)record[5] << 24u);
                const uint32_t calendar = (uint32_t)record[6] |
                    ((uint32_t)record[7] << 8u) |
                    ((uint32_t)record[8] << 16u) |
                    ((uint32_t)record[9] << 24u);
                const uint32_t duration = (uint32_t)record[10] |
                    ((uint32_t)record[11] << 8u) |
                    ((uint32_t)record[12] << 16u) |
                    ((uint32_t)record[13] << 24u);
                const uint32_t dose = (uint32_t)record[14] |
                    ((uint32_t)record[15] << 8u) |
                    ((uint32_t)record[16] << 16u) |
                    ((uint32_t)record[17] << 24u);
                const uint32_t maximum = (uint32_t)record[18] |
                    ((uint32_t)record[19] << 8u) |
                    ((uint32_t)(record[20] & 0x03u) << 16u);
                uint8_t interrupted[NOR_ALERT_RECORD_SIZE];
                encode_alert(interrupted, alert_ring.record_sequence,
                    (uint16_t)record[22] | ((uint16_t)record[23] << 8u),
                    start, calendar, duration, dose, maximum, false, true);
                if (!ring_append(&alert_ring, interrupted)) {
                    storage_spi = false;
                }
            }
        }
    }
}

void log_storage_set_spi_enabled(bool enabled)
{
    if (!storage_ready) return;
    storage_spi_requested = enabled;
    storage_spi_retry_at = board_millis + 60000u;
    if (!enabled) {
        storage_spi = false;
        return;
    }
    if (storage_spi) return;
    if (!spi_nor_is_ready() && !spi_nor_init()) return;
    configure_nor_rings();
    storage_spi = init_nor_rings();
}

void log_storage_sample_cpm(uint32_t cpm)
{
    if (!storage_ready) return;
    if (cpm < minute_min_cpm) minute_min_cpm = cpm;
    if (cpm > minute_max_cpm) minute_max_cpm = cpm;
    if (cpm < day_min_cpm) day_min_cpm = cpm;
    if (cpm > day_max_cpm) day_max_cpm = cpm;
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
    if (min_value < hour_min_cpm) hour_min_cpm = min_value;
    if (minute_max_cpm > hour_max_cpm) hour_max_cpm = minute_max_cpm;

    if (complete) {
        ++hour_minutes;
    }
    {
        const bool hour_complete = hour_minutes >= 60u;
        const uint32_t hour_min = hour_min_cpm == UINT32_MAX ? 0u :
            hour_min_cpm;
        if (hour_complete) {
            if (storage_spi && !nor_append_hour(hour_dose, hour_min,
                    hour_max_cpm, true)) {
                storage_spi = false;
            }
            hour_minutes = 0u;
            reset_interval(&hour_dose, &hour_min_cpm, &hour_max_cpm);
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
        if (storage_spi) {
            if (!nor_append_hour(hour_dose, minimum,
                    hour_max_cpm, false)) storage_spi = false;
        }
        hour_minutes = 0u;
        reset_interval(&hour_dose, &hour_min_cpm, &hour_max_cpm);
    }

}

void log_storage_second(uint32_t pulse_count)
{
    if (!storage_ready) return;
    /* The early JEDEC probe can precede the board-enable settling time.
     * Retry after startup, and after a transient write failure. */
    if (storage_spi_requested && !storage_spi &&
        (int32_t)(board_millis - storage_spi_retry_at) >= 0) {
        log_storage_set_spi_enabled(true);
    }
    minute_dose = add_saturated(minute_dose, pulse_count, 0x3FFFFu);
    day_dose = add_saturated(day_dose, pulse_count, 0x1FFFFFFFu);
    if (day_seconds < 0xFFFFFFu) ++day_seconds;
    if (minute_seconds < 60u) ++minute_seconds;
    if (minute_seconds >= 60u) close_minute(true);
}

static bool append_live_snapshot(nor_ring_t *ring, uint8_t type,
                                 uint32_t period_key, uint32_t dose,
                                 uint32_t minimum, uint32_t maximum,
                                 uint8_t progress)
{
    uint8_t record[NOR_LIVE_RECORD_SIZE];
    encode_live(record, ring->record_sequence, period_key, dose,
        minimum == UINT32_MAX ? 0u : minimum, maximum, progress, type);
    return ring_append(ring, record);
}

static void save_spi_live(uint32_t calendar_seconds)
{
    if (!append_live_snapshot(&minute_live_ring, NOR_RING_LIVE_MINUTE,
            calendar_seconds / 60u, minute_dose, minute_min_cpm,
            minute_max_cpm, minute_seconds) ||
        !append_live_snapshot(&hour_live_ring, NOR_RING_LIVE_HOUR,
            calendar_seconds / 3600u, hour_dose, hour_min_cpm,
            hour_max_cpm, hour_minutes)) {
        storage_spi = false;
    } else {
        last_live_minute_save = calendar_seconds;
    }
}

static void save_day_live(uint32_t calendar_seconds)
{
    if (day_calendar_key_valid && day_checkpoint_write(day_sequence,
            day_checkpoint_generation, day_calendar_key, day_dose,
            day_min_cpm == UINT32_MAX ? 0u : day_min_cpm, day_max_cpm)) {
        ++day_checkpoint_generation;
        last_live_day_save = calendar_seconds;
    }
}

void log_storage_checkpoint(uint32_t calendar_seconds, bool calendar_valid,
                            bool usb_powered, bool screen_woke)
{
    if (!storage_ready || !calendar_valid) return;
    const uint32_t spi_elapsed = calendar_seconds - last_live_minute_save;
    const uint32_t day_elapsed = calendar_seconds - last_live_day_save;
    if (storage_spi && ((usb_powered && spi_elapsed >= 10u) ||
            (screen_woke && spi_elapsed >= 300u))) save_spi_live(calendar_seconds);
    if ((usb_powered && day_elapsed >= 10u) || day_elapsed >= 600u ||
        (screen_woke && day_elapsed >= 300u)) save_day_live(calendar_seconds);
}

static uint32_t live_u32(const uint8_t record[NOR_LIVE_RECORD_SIZE],
                         uint8_t offset)
{
    return (uint32_t)record[offset] | ((uint32_t)record[offset + 1u] << 8u) |
        ((uint32_t)record[offset + 2u] << 16u) |
        ((uint32_t)record[offset + 3u] << 24u);
}

void log_storage_restore_live(uint32_t calendar_seconds, bool calendar_valid)
{
    uint8_t record[NOR_LIVE_RECORD_SIZE];
    uint16_t sequence;

    if (!storage_ready || !calendar_valid) return;
    last_live_minute_save = calendar_seconds;
    last_live_day_save = calendar_seconds;
    if (!storage_spi) return;
    if (ring_find_latest(&minute_live_ring, record) &&
        decode_live(record, NOR_RING_LIVE_MINUTE, &sequence) &&
        live_u32(record, 2u) == calendar_seconds / 60u) {
        minute_dose = live_u32(record, 6u);
        minute_min_cpm = (uint32_t)record[10] |
            ((uint32_t)record[11] << 8u) | ((uint32_t)record[12] << 16u);
        minute_max_cpm = (uint32_t)record[13] |
            ((uint32_t)record[14] << 8u) | ((uint32_t)record[15] << 16u);
        minute_seconds = record[16] > 59u ? 59u : record[16];
    }
    if (ring_find_latest(&hour_live_ring, record) &&
        decode_live(record, NOR_RING_LIVE_HOUR, &sequence) &&
        live_u32(record, 2u) == calendar_seconds / 3600u) {
        hour_dose = live_u32(record, 6u);
        hour_min_cpm = (uint32_t)record[10] |
            ((uint32_t)record[11] << 8u) | ((uint32_t)record[12] << 16u);
        hour_max_cpm = (uint32_t)record[13] |
            ((uint32_t)record[14] << 8u) | ((uint32_t)record[15] << 16u);
        hour_minutes = record[16] > 59u ? 59u : record[16];
    }
    (void)sequence;
}

void log_storage_flush(void)
{
    uint32_t uptime_seconds;
    uint32_t calendar_seconds;
    bool calendar_valid;
    if (!storage_ready || !board_rtc_log_time(&uptime_seconds,
            &calendar_seconds, &calendar_valid) || !calendar_valid) return;
    if (storage_spi) save_spi_live(calendar_seconds);
    save_day_live(calendar_seconds);
}

bool log_storage_calendar_day(uint16_t day_key, bool at_runtime_boundary)
{
    const bool changing_known_day = day_calendar_key_valid &&
        day_calendar_key != day_key;
    if (!storage_ready) return false;
    if (day_calendar_key_valid && day_calendar_key == day_key) return true;

    if (changing_known_day) close_time_segment();
    if (changing_known_day &&
        (day_seconds != 0u || day_dose != 0u ||
         day_min_cpm != UINT32_MAX)) {
        const uint32_t minimum = day_min_cpm == UINT32_MAX ? 0u :
            day_min_cpm;
        const bool complete = day_seconds >= 86400u;
        if (eeprom_write_day(day_dose, minimum, day_max_cpm, complete) &&
            !complete) ++day_sequence;
    }

    day_calendar_key = day_key;
    day_calendar_key_valid = true;
    if (changing_known_day) {
        day_seconds = 0u;
        reset_interval(&day_dose, &day_min_cpm, &day_max_cpm);
        if (at_runtime_boundary && storage_spi &&
            !nor_append_hour_day_marker()) {
            storage_spi = false;
        }
    }
    if (day_checkpoint_write(day_sequence, day_checkpoint_generation,
            day_calendar_key, day_dose,
            day_min_cpm == UINT32_MAX ? 0u : day_min_cpm,
            day_max_cpm)) {
        ++day_checkpoint_generation;
    }
    return false;
}

void log_storage_today_seed(uint32_t pulse_count)
{
    if (!storage_ready) return;
    if (minute_seconds != 0u) close_minute(false);
    day_dose = pulse_count > 0x1FFFFFFFu ? 0x1FFFFFFFu : pulse_count;
}

uint32_t log_storage_today_pulse_count(void)
{
    return day_dose;
}

uint32_t log_storage_today_recover_from_hours(uint8_t completed_hours)
{
    uint8_t header[NOR_SECTOR_HEADER_SIZE];
    uint8_t record[NOR_HOUR_RECORD_SIZE];
    uint32_t recent_total = 0u;
    uint32_t since_marker_total = 0u;
    uint8_t recovered = 0u;
    bool marker_found = false;

    if (!storage_ready || !storage_spi || !hour_ring.initialized) {
        return log_storage_today_pulse_count();
    }
    if (completed_hours > 23u) completed_hours = 23u;

    /* A day marker gives an exact start for new logs. Older log data predates
     * markers, so use the RTC hour as a bounded fallback for that history. */
    for (uint16_t back = 0u; back < 2u && !marker_found; ++back) {
        const uint16_t sector = (uint16_t)((hour_ring.active_sector +
            hour_ring.sector_count - back) % hour_ring.sector_count);
        const uint32_t base = (uint32_t)(hour_ring.base_sector + sector) *
            NOR_SECTOR_SIZE;
        uint32_t sector_sequence;
        const uint16_t top_slot = back == 0u ? hour_ring.next_slot :
            hour_ring.records_per_sector;
        if (!spi_nor_read(base, header, sizeof(header)) ||
            !parse_sector_header(header, NOR_RING_HOUR, &sector_sequence)) {
            continue;
        }
        (void)sector_sequence;
        for (uint16_t slot = top_slot; slot > 0u; --slot) {
            uint16_t sequence;
            const uint32_t address = base + NOR_SECTOR_HEADER_SIZE +
                (uint32_t)(slot - 1u) * NOR_HOUR_RECORD_SIZE;
            if (!spi_nor_read(address, record, sizeof(record))) {
                return log_storage_today_pulse_count();
            }
            if (!decode_hour(record, &sequence)) continue;
            (void)sequence;
            const uint8_t kind = record[9] & 0xF0u;
            if (kind == 0xC0u) {
                marker_found = true;
                break;
            }
            const uint32_t dose = get_bits(record, 16u, 24u);
            since_marker_total = add_saturated(since_marker_total, dose,
                                               0x1FFFFFFFu);
            if (kind == 0xB0u && recovered < completed_hours) {
                recent_total = add_saturated(recent_total, dose,
                                             0x1FFFFFFFu);
                ++recovered;
            }
        }
    }

    uint32_t total = marker_found ? since_marker_total : recent_total;
    /* Open hour/minute have not reached their historical SPI rings yet. */
    total = add_saturated(total, hour_dose, 0x1FFFFFFFu);
    day_dose = add_saturated(total, minute_dose, 0x1FFFFFFFu);
    return log_storage_today_pulse_count();
}

static bool alert_append(bool active, bool interrupted)
{
    uint8_t record[NOR_ALERT_RECORD_SIZE];
    encode_alert(record, alert_ring.record_sequence, alert_run_id,
        alert_start_uptime, alert_start_calendar, alert_duration_seconds,
        alert_dose_pulses, alert_max_cpm,
        active, interrupted);
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
        alert_duration_seconds = 0u;
        alert_dose_pulses = pulse_count;
        bool calendar_valid = false;
        uint32_t calendar_uptime = 0u;
        if (!board_rtc_log_time(&calendar_uptime, &alert_start_calendar,
                &calendar_valid) || !calendar_valid) {
            alert_start_calendar = UINT32_MAX;
        }
        (void)calendar_uptime;
        alert_max_cpm = cpm;
        alert_seconds_since_save = 0u;
        (void)alert_append(true, false);
        return;
    }

    alert_duration_seconds = uptime_seconds >= alert_start_uptime ?
        uptime_seconds - alert_start_uptime : 0u;
    alert_dose_pulses = add_saturated(alert_dose_pulses, pulse_count,
        UINT32_MAX);
    if (cpm > alert_max_cpm) alert_max_cpm = cpm;
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


void log_storage_log_counts(uint16_t *minutes, uint16_t *hours,
                            uint16_t *days)
{
    if (minutes != NULL) *minutes = storage_spi_requested &&
        minute_ring.initialized ?
        (minute_ring.total_records > UINT16_MAX ? UINT16_MAX :
            (uint16_t)minute_ring.total_records) : 0u;
    if (hours != NULL) *hours = storage_spi_requested &&
        hour_ring.initialized ?
        (hour_ring.total_records > UINT16_MAX ? UINT16_MAX :
            (uint16_t)hour_ring.total_records) : 0u;
    /* Like MIN/HOUR, DAY counts only persisted history on its owning medium.
     * RAM aggregates and durable LIVE checkpoints are separate from history. */
    if (days != NULL) *days = eeprom_day_count;
}

uint16_t log_storage_alert_record_count(void)
{
    if (!storage_spi_requested || !alert_ring.initialized) return 0u;
    return alert_ring.total_records > UINT16_MAX ? UINT16_MAX :
        (uint16_t)alert_ring.total_records;
}

uint8_t log_storage_alert_recent(uint32_t *start_calendar,
                                 uint32_t *duration_seconds,
                                 uint32_t *dose_pulses,
                                 uint32_t *maximum_cpm,
                                 uint8_t capacity)
{
    uint8_t record[NOR_ALERT_RECORD_SIZE];
    uint8_t count = 0u;
    uint16_t previous_run = 0u;
    uint32_t previous_start = 0u;
    bool have_previous = false;
    if (!storage_spi || !alert_ring.initialized || start_calendar == NULL ||
        duration_seconds == NULL || dose_pulses == NULL ||
        maximum_cpm == NULL) return 0u;
    for (uint32_t offset = 0u;
         offset < alert_ring.total_records && count < capacity; ++offset) {
        uint16_t sequence;
        if (!ring_read_newest_offset(&alert_ring, offset, record) ||
            !decode_alert(record, &sequence)) continue;
        (void)sequence;
        const uint16_t run = (uint16_t)record[22] |
            ((uint16_t)record[23] << 8u);
        const uint32_t start = (uint32_t)record[2] |
            ((uint32_t)record[3] << 8u) |
            ((uint32_t)record[4] << 16u) |
            ((uint32_t)record[5] << 24u);
        if (have_previous && run == previous_run && start == previous_start) {
            continue;
        }
        previous_run = run;
        previous_start = start;
        have_previous = true;
        start_calendar[count] = (uint32_t)record[6] |
            ((uint32_t)record[7] << 8u) |
            ((uint32_t)record[8] << 16u) |
            ((uint32_t)record[9] << 24u);
        duration_seconds[count] = (uint32_t)record[10] |
            ((uint32_t)record[11] << 8u) |
            ((uint32_t)record[12] << 16u) |
            ((uint32_t)record[13] << 24u);
        dose_pulses[count] = (uint32_t)record[14] |
            ((uint32_t)record[15] << 8u) |
            ((uint32_t)record[16] << 16u) |
            ((uint32_t)record[17] << 24u);
        maximum_cpm[count] = (uint32_t)record[18] |
            ((uint32_t)record[19] << 8u) |
            ((uint32_t)(record[20] & 0x03u) << 16u);
        ++count;
    }
    return count;
}


static void graph_pack_min_max(uint8_t *packed, uint32_t minimum,
                               uint32_t maximum)
{
    const uint64_t pair = (uint64_t)(minimum & 0x3FFFFu) |
        ((uint64_t)(maximum & 0x3FFFFu) << 18u);
    for (uint8_t i = 0u; i < 5u; ++i) {
        packed[i] = (uint8_t)(pair >> (i * 8u));
    }
}

bool log_storage_graph_load(log_graph_period_t period, uint32_t *dose,
                            uint8_t *packed_min_max_cpm, uint8_t capacity,
                            uint8_t *count)
{
    uint8_t used = 0u;
    uint8_t record[EEPROM_DAY_SIZE];
    graph_hours_cover_current_day = false;

    if (count != NULL) *count = 0u;
    if (!storage_ready || dose == NULL || packed_min_max_cpm == NULL ||
        count == NULL || capacity == 0u) return false;

    if ((period == LOG_GRAPH_MINUTES || period == LOG_GRAPH_HOURS) &&
        !storage_spi_requested) return false;

    /* Index zero always belongs to the open RAM period, even before the
     * first pulse or the first persistent record. History follows newest first.
     * Only completed records are read below; LIVE snapshots are never history. */
    if (period <= LOG_GRAPH_DAYS) {
        uint32_t minimum = minute_min_cpm;
        uint32_t maximum = minute_max_cpm;
        uint32_t current_dose = minute_dose;
        if (period == LOG_GRAPH_HOURS) {
            current_dose += hour_dose;
            if (hour_min_cpm < minimum) minimum = hour_min_cpm;
            if (hour_max_cpm > maximum) maximum = hour_max_cpm;
        } else if (period == LOG_GRAPH_DAYS) {
            current_dose = log_storage_today_pulse_count();
            minimum = day_min_cpm;
            maximum = day_max_cpm;
        }
        dose[used] = current_dose;
        graph_pack_min_max(packed_min_max_cpm,
            minimum == UINT32_MAX ? 0u : minimum, maximum);
        ++used;
    }

    if (period == LOG_GRAPH_MINUTES) {
        uint8_t header[NOR_SECTOR_HEADER_SIZE];
        if (!spi_nor_is_ready() || !minute_ring.initialized) {
            *count = used;
            return true;
        }
        /* Recent minute records normally fit in the active sector. Only
         * inspect its immediate predecessor as a rollover fallback; walking
         * the entire NOR ring when there are no records stalls the UI. */
        for (uint16_t back = 0u;
             back < 2u && back < minute_ring.sector_count &&
                 used < capacity; ++back) {
            const uint16_t sector = (uint16_t)((minute_ring.active_sector +
                minute_ring.sector_count - back) % minute_ring.sector_count);
            const uint32_t base = (uint32_t)(minute_ring.base_sector + sector) *
                NOR_SECTOR_SIZE;
            uint32_t sector_sequence;
            const uint16_t top_slot = back == 0u ? minute_ring.next_slot :
                minute_ring.records_per_sector;
            if (!spi_nor_read(base, header, sizeof(header)) ||
                !parse_sector_header(header, NOR_RING_MINUTE,
                    &sector_sequence)) continue;
            (void)sector_sequence;
            for (uint16_t slot = top_slot;
                 slot > 0u && used < capacity; --slot) {
                uint16_t sequence;
                const uint32_t address = base + NOR_SECTOR_HEADER_SIZE +
                    (uint32_t)(slot - 1u) * NOR_MINUTE_RECORD_SIZE;
                if (!spi_nor_read(address, record, NOR_MINUTE_RECORD_SIZE)) {
                    return false;
                }
                if (!decode_minute(record, &sequence) ||
                    (record[8] & 0xC0u) != 0x40u) continue;
                (void)sequence;
                dose[used] = get_bits(record, 16u, 18u);
                graph_pack_min_max(&packed_min_max_cpm[(uint16_t)used * 5u],
                    get_bits(record, 34u, 18u),
                    get_bits(record, 52u, 18u));
                ++used;
            }
        }
    } else if (period == LOG_GRAPH_HOURS) {
        if (!spi_nor_is_ready() || !hour_ring.initialized) {
            *count = used;
            return true;
        }
        uint8_t header[NOR_SECTOR_HEADER_SIZE];
        for (uint8_t back = 0u; back < 2u && used < capacity &&
             !graph_hours_cover_current_day; ++back) {
            const uint16_t sector = (uint16_t)((hour_ring.active_sector +
                hour_ring.sector_count - back) % hour_ring.sector_count);
            const uint32_t base = (uint32_t)(hour_ring.base_sector + sector) *
                NOR_SECTOR_SIZE;
            const uint16_t top_slot = back == 0u ? hour_ring.next_slot :
                hour_ring.records_per_sector;
            uint32_t sector_sequence;
            if (!spi_nor_read(base, header, sizeof(header)) ||
                !parse_sector_header(header, NOR_RING_HOUR,
                    &sector_sequence)) continue;
            (void)sector_sequence;
            for (uint16_t slot = top_slot;
                 slot > 0u && used < capacity; --slot) {
                uint16_t sequence;
                const uint32_t address = base + NOR_SECTOR_HEADER_SIZE +
                    (uint32_t)(slot - 1u) * NOR_HOUR_RECORD_SIZE;
                if (!spi_nor_read(address, record, NOR_HOUR_RECORD_SIZE)) {
                    return false;
                }
                if (!decode_hour(record, &sequence)) continue;
                if ((record[9] & 0xF0u) == 0xC0u) {
                    graph_hours_cover_current_day = true;
                    break;
                }
                /* A0 holds a partial hour closed by reboot/time change. It
                 * still contributes to today's dose and graph statistics. */
                if ((record[9] & 0xF0u) != 0xA0u &&
                    (record[9] & 0xF0u) != 0xB0u) continue;
                dose[used] = get_bits(record, 16u, 24u);
                graph_pack_min_max(
                    &packed_min_max_cpm[(uint16_t)used * 5u],
                    get_bits(record, 40u, 18u),
                    get_bits(record, 58u, 18u));
                ++used;
            }
        }
    } else if (period == LOG_GRAPH_DAYS) {
        const uint16_t history_count = EEPROM_DAY_COUNT;
        const uint8_t record_size = EEPROM_DAY_SIZE;
        const uint16_t base = EEPROM_DAY_BASE;
        const uint16_t newest_sequence = day_sequence;
        for (uint16_t age = 0u; age < history_count && used < capacity;
             ++age) {
            const uint16_t sequence = (uint16_t)(newest_sequence - 1u - age);
            const uint16_t address = (uint16_t)(base +
                (sequence % history_count) * record_size);
            uint16_t stored_sequence;
            if (!eeprom_read_record(address, record, record_size)) return false;
            if (!decode_day(record, &stored_sequence) ||
                stored_sequence != sequence) {
                const uint16_t legacy_address = (uint16_t)(
                    EEPROM_LEGACY_DAY_BASE +
                    (sequence % EEPROM_LEGACY_DAY_COUNT) * EEPROM_DAY_SIZE);
                if (legacy_address == address ||
                    !eeprom_read_record(legacy_address, record,
                        EEPROM_DAY_SIZE) ||
                    !decode_day(record, &stored_sequence) ||
                    stored_sequence != sequence) continue;
            }
            dose[used] = get_bits(record, 16u, 29u);
            graph_pack_min_max(
                &packed_min_max_cpm[(uint16_t)used * 5u],
                get_bits(record, 45u, 18u),
                get_bits(record, 63u, 18u));
            ++used;
        }
    } else {
        return false;
    }

    *count = used;
    return true;
}

uint32_t log_storage_graph_current_seconds(log_graph_period_t period)
{
    if (period == LOG_GRAPH_MINUTES) return minute_seconds;
    if (period == LOG_GRAPH_HOURS) {
        return (uint32_t)hour_minutes * 60u + minute_seconds;
    }
    return day_seconds;
}

bool log_storage_graph_hours_cover_current_day(void)
{
    return graph_hours_cover_current_day;
}

int32_t log_storage_graph_time_correction(log_graph_period_t period,
                                         uint8_t index,
                                         uint32_t current_period_start)
{
    uint8_t older[EEPROM_CUTOFF_SIZE];
    uint8_t newer[EEPROM_CUTOFF_SIZE];
    const nor_ring_t *ring;
    uint8_t sequence_offset;
    uint8_t slot_offset;
    uint32_t period_seconds;
    uint32_t after_event;
    uint32_t between_events;
    uint32_t previous_calendar;
    uint32_t latest_calendar;
    uint32_t previous_uptime;
    uint32_t latest_uptime;
    uint32_t old_timeline_end;
    uint32_t day_markers;
    uint16_t latest_event_type;
    int64_t correction;

    if (index == 0u || period == LOG_GRAPH_DAYS || !storage_spi) return 0;
    if (period == LOG_GRAPH_MINUTES) {
        ring = &minute_ring;
        sequence_offset = 11u;
        slot_offset = 14u;
        period_seconds = 60u;
    } else if (period == LOG_GRAPH_HOURS) {
        ring = &hour_ring;
        sequence_offset = 16u;
        slot_offset = 19u;
        period_seconds = 3600u;
    } else {
        return 0;
    }
    if (!ring->initialized || !eeprom_cutoff_read_pair(older, newer) ||
        (older[10] & 0x80u) == 0u ||
        (newer[10] & 0x80u) == 0u ||
        (older[21] & 0x01u) == 0u ||
        (newer[21] & 0x01u) == 0u) return 0;

    latest_calendar = read_le(&newer[6], 4u);
    previous_calendar = read_le(&older[6], 4u);
    latest_event_type = newer[10] & 0x7Fu;
    if (latest_event_type == LOG_CUTOFF_REASON_BOOT &&
        current_period_start < latest_calendar &&
        current_period_start / period_seconds ==
            latest_calendar / period_seconds) return 0;
    after_event = cutoff_ring_slots_between(ring, newer, NULL,
        sequence_offset, slot_offset, true);
    if (after_event == UINT32_MAX) return 0;
    if (period == LOG_GRAPH_HOURS) {
        const uint32_t markers = calendar_day_boundaries(latest_calendar,
            current_period_start);
        after_event = after_event > markers ? after_event - markers : 0u;
    }
    if (index <= after_event) return 0;

    between_events = cutoff_ring_slots_between(ring, older, newer,
        sequence_offset, slot_offset, false);
    if (between_events == UINT32_MAX) return 0;
    previous_uptime = read_le(&older[2], 4u);
    latest_uptime = read_le(&newer[2], 4u);

    if (latest_event_type == LOG_CUTOFF_REASON_CLOCK_SET) {
        if (latest_uptime < previous_uptime) return 0;
        old_timeline_end = previous_calendar +
            (latest_uptime - previous_uptime);
        if (!cutoff_previous_record_is_graph_bar(ring, newer, period) &&
            between_events != 0u) --between_events;
    } else if (latest_event_type == LOG_CUTOFF_REASON_BOOT) {
        if (latest_calendar < previous_calendar) return 0;
        if (period == LOG_GRAPH_MINUTES) {
            old_timeline_end = previous_calendar +
                between_events * period_seconds;
        } else {
            day_markers = calendar_day_boundaries(previous_calendar,
                latest_calendar);
            between_events = between_events > day_markers ?
                between_events - day_markers : 0u;
            old_timeline_end = previous_calendar +
                between_events * period_seconds;
        }
    } else {
        return 0;
    }

    correction = (int64_t)old_timeline_end - latest_calendar;
    if (correction > INT32_MAX) return INT32_MAX;
    if (correction < INT32_MIN) return INT32_MIN;
    return (int32_t)correction;
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
    const uint16_t current_day = day_calendar_key;
    const bool current_day_valid = day_calendar_key_valid;

    switch (target) {
    case LOG_CLEAR_MINUTE:
        ok = erase_nor_sectors(0u, NOR_MINUTE_SECTORS) &&
            erase_nor_sectors(NOR_LIVE_MINUTE_BASE_SECTOR, NOR_LIVE_SECTORS);
        break;
    case LOG_CLEAR_HOUR:
        ok = erase_nor_sectors(NOR_HOUR_BASE_SECTOR, NOR_HOUR_SECTORS) &&
            erase_nor_sectors(NOR_LIVE_HOUR_BASE_SECTOR, NOR_LIVE_SECTORS);
        break;
    case LOG_CLEAR_DAY:
        ok = clear_eeprom_range(EEPROM_DAY_CHECKPOINT_BASE,
                EEPROM_DAY_CHECKPOINT_BASE +
                EEPROM_DAY_CHECKPOINT_COUNT * EEPROM_DAY_CHECKPOINT_SIZE) &&
            clear_eeprom_range(EEPROM_DAY_BASE,
                EEPROM_DAY_BASE + EEPROM_DAY_COUNT * EEPROM_DAY_SIZE) &&
            clear_eeprom_range(EEPROM_LEGACY_DAY_BASE, 2041u);
        break;
    case LOG_CLEAR_ERROR:
        ok = erase_nor_sectors(NOR_MINUTE_SECTORS, NOR_ALERT_SECTORS);
        break;
    case LOG_CLEAR_LOGS:
        ok = clear_eeprom_range(EEPROM_LOG_REGION_BASE, 2048u) &&
            erase_nor_sectors(0u, 256u);
        break;
    case LOG_CLEAR_EEPROM_LOGS:
        ok = clear_eeprom_range(EEPROM_LOG_REGION_BASE, 2048u);
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
        minute_live_ring.initialized = false;
        reset_interval(&minute_dose, &minute_min_cpm, &minute_max_cpm);
        minute_seconds = 0u;
        break;
    case LOG_CLEAR_HOUR:
        hour_ring.initialized = false;
        hour_live_ring.initialized = false;
        hour_minutes = 0u;
        reset_interval(&hour_dose, &hour_min_cpm, &hour_max_cpm);
        break;
    case LOG_CLEAR_DAY:
        day_sequence = 0u;
        eeprom_day_count = 0u;
        day_checkpoint_generation = 0u;
        day_seconds = 0u;
        day_calendar_key = EEPROM_DAY_UNKNOWN_KEY;
        day_calendar_key_valid = false;
        reset_interval(&day_dose, &day_min_cpm, &day_max_cpm);
        break;
    case LOG_CLEAR_ERROR:
        alert_ring.initialized = false;
        alert_active = false;
        break;
    case LOG_CLEAR_LOGS:
        minute_ring.initialized = false;
        hour_ring.initialized = false;
        alert_ring.initialized = false;
        minute_live_ring.initialized = false;
        hour_live_ring.initialized = false;
        alert_active = false;
        day_sequence = 0u;
        day_checkpoint_generation = 0u;
        eeprom_day_count = 0u;
        minute_seconds = 0u;
        hour_minutes = 0u;
        day_seconds = 0u;
        day_calendar_key = EEPROM_DAY_UNKNOWN_KEY;
        day_calendar_key_valid = false;
        reset_interval(&minute_dose, &minute_min_cpm, &minute_max_cpm);
        reset_interval(&hour_dose, &hour_min_cpm, &hour_max_cpm);
        reset_interval(&day_dose, &day_min_cpm, &day_max_cpm);
        break;
    case LOG_CLEAR_EEPROM_LOGS:
        day_sequence = 0u;
        day_checkpoint_generation = 0u;
        eeprom_day_count = 0u;
        minute_seconds = 0u;
        hour_minutes = 0u;
        day_seconds = 0u;
        day_calendar_key = EEPROM_DAY_UNKNOWN_KEY;
        day_calendar_key_valid = false;
        reset_interval(&minute_dose, &minute_min_cpm, &minute_max_cpm);
        reset_interval(&hour_dose, &hour_min_cpm, &hour_max_cpm);
        reset_interval(&day_dose, &day_min_cpm, &day_max_cpm);
        break;
    case LOG_CLEAR_SPI:
        minute_ring.initialized = false;
        hour_ring.initialized = false;
        alert_ring.initialized = false;
        minute_live_ring.initialized = false;
        hour_live_ring.initialized = false;
        alert_active = false;
        break;
    default:
        return false;
    }
    if (target == LOG_CLEAR_DAY || target == LOG_CLEAR_LOGS ||
        target == LOG_CLEAR_EEPROM_LOGS) {
        day_calendar_key = current_day;
        day_calendar_key_valid = current_day_valid;
        /* A valid empty checkpoint prevents migration resurrecting an old
         * pre-ring day after a targeted DAY clear. It is not a history log. */
        if (target == LOG_CLEAR_DAY) {
            ok = day_checkpoint_write(day_sequence, day_checkpoint_generation,
                current_day_valid ? current_day : EEPROM_DAY_UNKNOWN_KEY,
                0u, 0u, 0u);
            if (ok) ++day_checkpoint_generation;
        }
    }
    return ok;
}

void log_storage_time_cutoff(uint32_t uptime_seconds, uint32_t calendar_seconds,
                             bool calendar_valid, uint8_t reason)
{
    if (!storage_ready) return;
    if (reason == LOG_CUTOFF_REASON_CLOCK_SET) close_time_segment();
    if (reason == LOG_CUTOFF_REASON_BOOT) {
        alert_run_id = cutoff_sequence;
    }
    eeprom_cutoff_append(uptime_seconds, calendar_seconds,
        calendar_valid, reason);
}
