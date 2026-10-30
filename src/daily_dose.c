#include "daily_dose.h"

#include "board.h"
#include "log_record.h"

#define DAILY_DOSE_BASE 316u
#define DAILY_DOSE_SLOT_COUNT 2u
#define DAILY_DOSE_SLOT_SIZE 16u
#define DAILY_DOSE_MAGIC 0xD3u

static uint16_t current_day = UINT16_MAX;
static uint32_t pulse_count;
static uint32_t total_count;
static uint8_t next_sequence;
static bool dirty;

#define crc16(bytes, length) log_crc16((bytes), (length), 0xFFFFu)

static bool read_slot(uint8_t slot, uint8_t record[DAILY_DOSE_SLOT_SIZE])
{
    bool legacy = slot >= DAILY_DOSE_SLOT_COUNT;
    const uint16_t base = legacy ? (uint16_t)(140u + (uint16_t)(slot - DAILY_DOSE_SLOT_COUNT) * 16u) :
        (uint16_t)(DAILY_DOSE_BASE + (uint16_t)slot * DAILY_DOSE_SLOT_SIZE);
    for (uint8_t i = 0u; i < DAILY_DOSE_SLOT_SIZE; ++i) {
        if (!board_eeprom_read((uint16_t)(base + i), &record[i])) return false;
    }
    uint8_t end = legacy ? 8u : 11u;
    uint8_t magic = legacy ? 0xD2u : DAILY_DOSE_MAGIC;
    const uint16_t crc = crc16(record, end);
    uint8_t inverse = (uint8_t)(255u - magic);
    return record[14] == magic && record[15] == inverse &&
        record[end] == (uint8_t)crc && record[end + 1u] == (uint8_t)(crc >> 8u);
}

void daily_dose_set_day(uint16_t day_key, uint32_t fallback_count)
{
    bool found = false;
    bool found_day = false;
    bool found_new = false;
    uint8_t newest_sequence = 0u;
    uint8_t day_sequence = 0u;
    uint32_t newest_count = fallback_count;
    uint8_t record[DAILY_DOSE_SLOT_SIZE];

    for (uint8_t slot = 0u; slot < DAILY_DOSE_SLOT_COUNT + 8u; ++slot) {
        if (slot == DAILY_DOSE_SLOT_COUNT && found_new) break;
        if (!read_slot(slot, record)) continue;
        if (slot < DAILY_DOSE_SLOT_COUNT) found_new = true;
        if (!found || (int8_t)(record[0] - newest_sequence) > 0) {
            found = true;
            newest_sequence = record[0];
            if (slot < DAILY_DOSE_SLOT_COUNT) total_count = (uint32_t)record[7] | ((uint32_t)record[8] << 8u) |
                ((uint32_t)record[9] << 16u) | ((uint32_t)record[10] << 24u);
        }
        const uint16_t saved_day = (uint16_t)record[1] |
            (uint16_t)((uint16_t)record[2] << 8u);
        if (saved_day == day_key && (!found_day ||
                (int8_t)(record[0] - day_sequence) > 0)) {
            found_day = true;
            day_sequence = record[0];
            newest_count = (uint32_t)record[3] |
                ((uint32_t)record[4] << 8u) |
                ((uint32_t)record[5] << 16u) |
                ((uint32_t)record[6] << 24u);
        }
    }
    if (!found_new) {
        static const uint16_t addresses[2][9] = {
            {10u,11u,12u,13u,14u,15u,30u,31u,62u},
            {63u,72u,73u,74u,75u,76u,77u,78u,79u}
        };
        bool total_found = false;
        uint8_t sequence = 0u;
        for (uint8_t slot = 0u; slot < 2u; ++slot) {
            for (uint8_t i = 0u; i < 9u; ++i) (void)board_eeprom_read(addresses[slot][i], &record[i]);
            uint16_t crc = crc16(record, 5u);
            if (record[7] != 0xC7u || record[8] != (uint8_t)~0xC7u ||
                record[5] != (uint8_t)crc || record[6] != (uint8_t)(crc >> 8u)) continue;
            if (!total_found || (int8_t)(record[0] - sequence) > 0) {
                total_found = true; sequence = record[0];
                total_count = (uint32_t)record[1] | ((uint32_t)record[2] << 8u) |
                    ((uint32_t)record[3] << 16u) | ((uint32_t)record[4] << 24u);
            }
        }
    }
    current_day = day_key;
    pulse_count = newest_count;
    next_sequence = found ? (uint8_t)(newest_sequence + 1u) : 0u;
    dirty = (!found_new && (found || total_count != 0u)) || (!found_day && fallback_count != 0u);
}

void daily_dose_init(uint16_t day_key, uint32_t fallback_count)
{
    daily_dose_set_day(day_key, fallback_count);
}

void daily_dose_add(uint32_t pulses)
{
    if (UINT32_MAX - pulse_count < pulses) pulse_count = UINT32_MAX;
    else pulse_count += pulses;
    if (pulses != 0u) dirty = true;
}

uint32_t daily_dose_count(void)
{
    return pulse_count;
}

bool daily_dose_checkpoint(void)
{
    if (!dirty) return true;
    uint8_t record[DAILY_DOSE_SLOT_SIZE] = {0u};
    const uint8_t sequence = next_sequence++;
    const uint8_t slot = (uint8_t)(sequence % DAILY_DOSE_SLOT_COUNT);
    const uint16_t base = (uint16_t)(DAILY_DOSE_BASE +
        (uint16_t)slot * DAILY_DOSE_SLOT_SIZE);
    uint16_t crc;

    record[0] = sequence;
    record[1] = (uint8_t)current_day;
    record[2] = (uint8_t)(current_day >> 8u);
    record[3] = (uint8_t)pulse_count;
    record[4] = (uint8_t)(pulse_count >> 8u);
    record[5] = (uint8_t)(pulse_count >> 16u);
    record[6] = (uint8_t)(pulse_count >> 24u);
    record[7] = (uint8_t)total_count;
    record[8] = (uint8_t)(total_count >> 8u);
    record[9] = (uint8_t)(total_count >> 16u);
    record[10] = (uint8_t)(total_count >> 24u);
    crc = crc16(record, 11u);
    record[11] = (uint8_t)crc;
    record[12] = (uint8_t)(crc >> 8u);
    record[14] = DAILY_DOSE_MAGIC;
    record[15] = (uint8_t)~DAILY_DOSE_MAGIC;
    if (!board_eeprom_write((uint16_t)(base + 14u), 0u) ||
        !board_eeprom_write((uint16_t)(base + 15u), 0u)) return false;
    for (uint8_t i = 0u; i < 14u; ++i) {
        if (!board_eeprom_write((uint16_t)(base + i), record[i])) return false;
    }
    if (!board_eeprom_write((uint16_t)(base + 14u), record[14]) ||
        !board_eeprom_write((uint16_t)(base + 15u), record[15])) return false;
    dirty = false;
    return true;
}

uint32_t daily_dose_total(void) { return total_count; }
void daily_dose_set_total(uint32_t count)
{
    if (count != total_count) { total_count = count; dirty = true; }
}

void daily_dose_clear(void)
{
    pulse_count = 0u;
    total_count = 0u;
    dirty = true;
}
