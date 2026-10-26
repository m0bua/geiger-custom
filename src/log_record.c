#include "log_record.h"

#define LOG_ALERT_SECONDS_MAX 4096u

typedef struct {
    uint8_t time_bits;
    uint8_t count_bits;
    uint8_t maximum_bits;
    uint8_t duration_bits;
    uint8_t segment_bits;
    uint32_t max_period_index;
} log_record_layout_t;

static bool layout_for_kind(log_record_kind_t kind,
                            log_record_layout_t *layout)
{
    if (layout == NULL) return false;

    switch (kind) {
    case LOG_RECORD_MINUTE:
        *layout = (log_record_layout_t){20u, 19u, 19u, 0u, 6u, 527039u};
        return true;
    case LOG_RECORD_HOUR:
        *layout = (log_record_layout_t){14u, 24u, 19u, 0u, 6u, 8783u};
        return true;
    case LOG_RECORD_DAY:
        *layout = (log_record_layout_t){9u, 29u, 19u, 0u, 6u, 365u};
        return true;
    case LOG_RECORD_ALERT:
        *layout = (log_record_layout_t){20u, 25u, 19u, 12u, 6u, 527039u};
        return true;
    default:
        return false;
    }
}

static void put_bits(uint8_t *data, uint16_t *bit_offset, uint32_t value,
                     uint8_t width)
{
    for (uint8_t bit = 0u; bit < width; ++bit, ++*bit_offset) {
        if ((value & (1u << bit)) != 0u) {
            const uint16_t byte_index = *bit_offset / 8u;
            const uint8_t bit_index = (uint8_t)(*bit_offset % 8u);
            data[byte_index] |= (uint8_t)(1u << bit_index);
        }
    }
}

static uint32_t get_bits(const uint8_t *data, uint16_t *bit_offset,
                         uint8_t width)
{
    uint32_t value = 0u;
    for (uint8_t bit = 0u; bit < width; ++bit, ++*bit_offset) {
        const uint16_t byte_index = *bit_offset / 8u;
        const uint8_t bit_index = (uint8_t)(*bit_offset % 8u);
        if ((data[byte_index] & (uint8_t)(1u << bit_index)) != 0u) {
            value |= 1u << bit;
        }
    }
    return value;
}

size_t log_record_data_size(log_record_kind_t kind)
{
    switch (kind) {
    case LOG_RECORD_MINUTE:
    case LOG_RECORD_HOUR:
    case LOG_RECORD_DAY:
        return LOG_RECORD_DATA_BYTES;
    case LOG_RECORD_ALERT:
        return LOG_ALERT_DATA_BYTES;
    default:
        return 0u;
    }
}

uint32_t log_record_period_seconds(log_record_kind_t kind)
{
    switch (kind) {
    case LOG_RECORD_MINUTE: return 60u;
    case LOG_RECORD_HOUR: return 3600u;
    case LOG_RECORD_DAY: return 86400u;
    default: return 0u;
    }
}

uint32_t log_record_count_ceiling(log_record_kind_t kind)
{
    switch (kind) {
    case LOG_RECORD_MINUTE: return LOG_RECORD_CPM_CEILING;
    case LOG_RECORD_HOUR: return LOG_RECORD_CPM_CEILING * 60u;
    case LOG_RECORD_DAY: return LOG_RECORD_CPM_CEILING * 1440u;
    case LOG_RECORD_ALERT:
        return (LOG_RECORD_CPM_CEILING * LOG_ALERT_SECONDS_MAX) / 60u;
    default: return 0u;
    }
}

bool log_record_encode(log_record_kind_t kind, const log_record_t *record,
                       uint8_t *data, size_t capacity)
{
    log_record_layout_t layout;
    const size_t required = log_record_data_size(kind);
    uint16_t bit_offset = 0u;
    const uint32_t count_max = log_record_count_ceiling(kind);
    uint32_t duration_code = 0u;

    if (record == NULL || data == NULL || required == 0u ||
        capacity < required || !layout_for_kind(kind, &layout) ||
        record->period_index > layout.max_period_index || count_max == 0u ||
        record->maximum_cpm > LOG_RECORD_CPM_CEILING ||
        record->segment_id >= LOG_SEGMENT_ID_COUNT) return false;

    if (kind == LOG_RECORD_ALERT) {
        if (record->duration_seconds == 0u ||
            record->duration_seconds > LOG_ALERT_SECONDS_MAX) return false;
        duration_code = (uint32_t)record->duration_seconds - 1u;
    }

    /* Refuse out-of-range values instead of silently encoding false data. */
    if (record->count > count_max) return false;

    for (size_t i = 0u; i < required; ++i) data[i] = 0u;
    put_bits(data, &bit_offset, record->period_index, layout.time_bits);
    put_bits(data, &bit_offset, record->count, layout.count_bits);
    put_bits(data, &bit_offset, record->maximum_cpm,
        layout.maximum_bits);
    if (layout.duration_bits != 0u) {
        put_bits(data, &bit_offset, duration_code, layout.duration_bits);
    }
    put_bits(data, &bit_offset, record->segment_id, layout.segment_bits);
    return bit_offset <= (uint16_t)(required * 8u);
}

bool log_record_decode(log_record_kind_t kind, const uint8_t *data,
                       size_t length, log_record_t *record)
{
    log_record_layout_t layout;
    const size_t required = log_record_data_size(kind);
    uint16_t bit_offset = 0u;

    if (data == NULL || record == NULL || required == 0u ||
        length != required || !layout_for_kind(kind, &layout)) return false;

    record->period_index = get_bits(data, &bit_offset, layout.time_bits);
    record->count = get_bits(data, &bit_offset, layout.count_bits);
    record->maximum_cpm = get_bits(data, &bit_offset, layout.maximum_bits);
    record->duration_seconds = layout.duration_bits == 0u ? 0u :
        (uint16_t)(get_bits(data, &bit_offset, layout.duration_bits) + 1u);
    record->segment_id = (uint8_t)get_bits(data, &bit_offset,
        layout.segment_bits);
    if (bit_offset > (uint16_t)(required * 8u) ||
        record->period_index > layout.max_period_index ||
        log_record_count_ceiling(kind) == 0u ||
        record->maximum_cpm > LOG_RECORD_CPM_CEILING ||
        record->count > log_record_count_ceiling(kind)) {
        return false;
    }
    for (; bit_offset < (uint16_t)(required * 8u); ++bit_offset) {
        if ((data[bit_offset / 8u] &
                (uint8_t)(1u << (bit_offset % 8u))) != 0u) return false;
    }
    return true;
}

uint32_t log_record_average_cpm(uint32_t count, uint32_t duration_seconds)
{
    if (duration_seconds == 0u) return 0u;
    const uint64_t numerator = (uint64_t)count * 60u +
        duration_seconds / 2u;
    const uint64_t average = numerator / duration_seconds;
    return average > UINT32_MAX ? UINT32_MAX : (uint32_t)average;
}
