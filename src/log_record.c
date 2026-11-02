#include "log_record.h"

#define LOG_ALERT_SECONDS_MAX 4096u

typedef struct {
    uint8_t time_bits;
    uint8_t count_bits;
    uint8_t maximum_bits;
    uint8_t duration_bits;
    uint8_t segment_bits;
    uint8_t coverage_bits;
    uint32_t max_period_index;
} log_record_layout_t;

static bool layout_for_kind(log_record_kind_t kind,
                            log_record_layout_t *layout)
{
    if (layout == NULL) return false;

    switch (kind) {
    case LOG_RECORD_MINUTE:
        *layout = (log_record_layout_t){20u, 19u, 19u, 0u, 6u, 8u, 527039u};
        return true;
    case LOG_RECORD_HOUR:
        *layout = (log_record_layout_t){14u, 24u, 19u, 0u, 6u, 8u, 8783u};
        return true;
    case LOG_RECORD_DAY:
        *layout = (log_record_layout_t){9u, 29u, 19u, 0u, 6u, 8u, 365u};
        return true;
    case LOG_RECORD_ALERT:
        *layout = (log_record_layout_t){20u, 25u, 19u, 12u, 6u, 0u, 527039u};
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

static uint8_t missing_coverage_code(log_record_kind_t kind,
                                     uint32_t covered_seconds)
{
    const uint32_t period = log_record_period_seconds(kind);
    if (period == 0u || covered_seconds >= period) return 0u;
    if (covered_seconds == 0u) covered_seconds = 1u;
    const uint64_t missing = (uint64_t)(period - covered_seconds) * 256u;
    uint32_t code = (uint32_t)((missing + period - 1u) / period);
    return code > 255u ? 255u : (uint8_t)code;
}

static uint32_t coverage_from_missing_code(log_record_kind_t kind,
                                           uint8_t code)
{
    const uint32_t period = log_record_period_seconds(kind);
    if (code == 0u) return period;
    uint32_t covered = (uint32_t)(((uint64_t)period * (256u - code) + 128u) / 256u);
    return covered == 0u ? 1u : covered;
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
        record->segment_id >= LOG_SEGMENT_ID_COUNT ||
        (layout.coverage_bits != 0u && record->covered_seconds >
            log_record_period_seconds(kind))) return false;

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
    if (layout.coverage_bits != 0u) {
        put_bits(data, &bit_offset,
            missing_coverage_code(kind, record->covered_seconds),
            layout.coverage_bits);
    }
    return bit_offset <= (uint16_t)(required * 8u);
}

bool log_record_decode(log_record_kind_t kind, const uint8_t *data,
                       size_t length, log_record_t *record)
{
    log_record_layout_t layout;
    const size_t required = log_record_data_size(kind);
    uint16_t bit_offset = 0u;

    if (data == NULL || record == NULL || required == 0u ||
        !layout_for_kind(kind, &layout) ||
        (kind == LOG_RECORD_ALERT ? length != required :
            (length != required && length != LOG_RECORD_LEGACY_DATA_BYTES))) return false;

    record->period_index = get_bits(data, &bit_offset, layout.time_bits);
    record->count = get_bits(data, &bit_offset, layout.count_bits);
    record->maximum_cpm = get_bits(data, &bit_offset, layout.maximum_bits);
    record->duration_seconds = layout.duration_bits == 0u ? 0u :
        (uint16_t)(get_bits(data, &bit_offset, layout.duration_bits) + 1u);
    record->segment_id = (uint8_t)get_bits(data, &bit_offset,
        layout.segment_bits);
    if (layout.coverage_bits != 0u) {
        record->covered_seconds = length == LOG_RECORD_LEGACY_DATA_BYTES ?
            log_record_period_seconds(kind) : coverage_from_missing_code(kind,
                (uint8_t)get_bits(data, &bit_offset, layout.coverage_bits));
    } else {
        record->covered_seconds = 0u;
    }
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

static uint32_t year_days(uint16_t year)
{
    return year % 4u == 0u && (year % 100u != 0u || year % 400u == 0u) ? 366u : 365u;
}

static uint16_t split_year(uint32_t seconds, uint32_t *days)
{
    uint16_t year = 2000u;
    *days = seconds / 86400u;
    while (year < 2100u && *days >= year_days(year)) {
        *days -= year_days(year++);
    }
    return year;
}

bool log_record_location(log_record_kind_t kind, uint32_t seconds,
                          bool valid, uint32_t *index, uint8_t *descriptor)
{
    uint32_t period = kind == LOG_RECORD_ALERT ? 60u : log_record_period_seconds(kind);
    if (period == 0u || index == NULL || descriptor == NULL) return false;
    if (!valid) {
        *index = (seconds % (366u * 86400u)) / period;
        *descriptor = (uint8_t)((seconds / (366u * 86400u)) & LOG_YEAR_MASK);
        return true;
    }
    uint32_t days;
    uint16_t year = split_year(seconds, &days);
    if (year >= 2100u) return false;
    *index = (days * 86400u + seconds % 86400u) / period;
    *descriptor = (uint8_t)(LOG_YEAR_VALID | (year & LOG_YEAR_MASK));
    return true;
}

bool log_record_time(log_record_kind_t kind, uint32_t index, uint8_t descriptor,
                      uint32_t reference, uint32_t *seconds)
{
    uint32_t period = kind == LOG_RECORD_ALERT ? 60u : log_record_period_seconds(kind);
    if (period == 0u || seconds == NULL) return false;
    if ((descriptor & LOG_YEAR_VALID) == 0u) {
        uint64_t time = (uint64_t)(descriptor & LOG_YEAR_MASK) * 366u * 86400u + (uint64_t)index * period;
        if (time > UINT32_MAX) return false;
        *seconds = (uint32_t)time;
        return true;
    }
    uint32_t ignored;
    int year = split_year(reference, &ignored);
    int delta = ((int)(descriptor & LOG_YEAR_MASK) - (year & LOG_YEAR_MASK) + 48) % 32 - 16;
    year += delta;
    if (year < 2000 || year > 2099 || index >= year_days((uint16_t)year) * 86400u / period) return false;
    uint32_t days = 0u;
    for (uint16_t y = 2000u; y < (uint16_t)year; ++y) days += year_days(y);
    *seconds = days * 86400u + index * period;
    return true;
}

uint16_t log_crc16(const uint8_t *data, uint16_t length, uint16_t crc)
{
    for (uint16_t i = 0u; i < length; ++i) {
        crc ^= (uint16_t)data[i] << 8u;
        for (uint8_t bit = 0u; bit < 8u; ++bit)
            crc = crc & 0x8000u ? (uint16_t)((crc << 1u) ^ 0x1021u) : (uint16_t)(crc << 1u);
    }
    return crc;
}
