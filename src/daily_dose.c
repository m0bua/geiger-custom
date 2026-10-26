#include "daily_dose.h"

#include "board.h"

#define DAILY_DOSE_BASE 140u
#define DAILY_DOSE_SLOT_COUNT 8u
#define DAILY_DOSE_SLOT_SIZE 16u
#define DAILY_DOSE_MAGIC 0xD2u

static uint16_t current_day = UINT16_MAX;
static uint32_t pulse_count;
static uint8_t next_sequence;
static bool dirty;

static uint16_t crc16(const uint8_t *bytes, uint8_t length)
{
    uint16_t crc = 0xFFFFu;
    for (uint8_t i = 0u; i < length; ++i) {
        crc ^= (uint16_t)bytes[i] << 8u;
        for (uint8_t bit = 0u; bit < 8u; ++bit) {
            crc = (crc & 0x8000u) != 0u ?
                (uint16_t)((crc << 1u) ^ 0x1021u) :
                (uint16_t)(crc << 1u);
        }
    }
    return crc;
}

static bool read_slot(uint8_t slot, uint8_t record[DAILY_DOSE_SLOT_SIZE])
{
    const uint16_t base = (uint16_t)(DAILY_DOSE_BASE +
        (uint16_t)slot * DAILY_DOSE_SLOT_SIZE);
    for (uint8_t i = 0u; i < DAILY_DOSE_SLOT_SIZE; ++i) {
        if (!board_eeprom_read((uint16_t)(base + i), &record[i])) return false;
    }
    const uint16_t crc = crc16(record, 8u);
    return record[14] == DAILY_DOSE_MAGIC &&
        record[15] == (uint8_t)~DAILY_DOSE_MAGIC &&
        record[8] == (uint8_t)crc && record[9] == (uint8_t)(crc >> 8u);
}

void daily_dose_set_day(uint16_t day_key, uint32_t fallback_count)
{
    bool found = false;
    bool found_day = false;
    uint8_t newest_sequence = 0u;
    uint8_t day_sequence = 0u;
    uint32_t newest_count = fallback_count;
    uint8_t record[DAILY_DOSE_SLOT_SIZE];

    for (uint8_t slot = 0u; slot < DAILY_DOSE_SLOT_COUNT; ++slot) {
        if (!read_slot(slot, record)) continue;
        if (!found || (int8_t)(record[0] - newest_sequence) > 0) {
            found = true;
            newest_sequence = record[0];
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
    current_day = day_key;
    pulse_count = newest_count;
    next_sequence = found ? (uint8_t)(newest_sequence + 1u) : 0u;
    dirty = !found_day && fallback_count != 0u;
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
    record[7] = 0u;
    crc = crc16(record, 8u);
    record[8] = (uint8_t)crc;
    record[9] = (uint8_t)(crc >> 8u);
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
