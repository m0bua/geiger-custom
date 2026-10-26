#include "spi_log.h"

#include "spi_nor.h"

#include <stddef.h>

#define SPI_LOG_SECTOR_SIZE 4096u
#define SPI_LOG_HEADER_SIZE 32u
#define SPI_LOG_HEADER_COMMIT_OFFSET 16u
#define SPI_LOG_HEADER_RETIRE_OFFSET 17u
#define SPI_LOG_HEADER_COMMIT 0xA5u
#define SPI_LOG_HEADER_RETIRED 0x00u
#define SPI_LOG_RECORD_COMMIT 0xA5u
#define SPI_LOG_VERSION 1u
#define SPI_LOG_MAGIC_0 0x4Cu
#define SPI_LOG_MAGIC_1 0x47u
#define SPI_LOG_MAGIC_2 0x32u
#define SPI_LOG_MAGIC_3 0x53u

typedef struct {
    uint32_t base_address;
    uint16_t sector_count;
    uint32_t active_address;
    uint32_t active_generation;
    uint32_t newest_generation;
    uint16_t active_epoch;
    uint16_t slot_capacity;
    uint16_t next_slot;
    uint8_t active_segment_id;
    uint8_t active_year_descriptor;
    bool has_generation;
    bool has_active;
    bool appendable;
} spi_log_stream_t;

typedef struct {
    log_record_kind_t kind;
    uint8_t segment_id;
    uint8_t year_descriptor;
    uint32_t generation;
    uint16_t epoch;
} spi_log_header_t;

static spi_log_stream_t streams[3];
static bool log_ready;
static uint16_t count_erased_headers(spi_log_stream_t *stream,
                                    uint32_t *first_address);

static uint16_t read_u16_le(const uint8_t *data)
{
    return (uint16_t)data[0] | (uint16_t)((uint16_t)data[1] << 8u);
}

static uint32_t read_u32_le(const uint8_t *data)
{
    return (uint32_t)data[0] | ((uint32_t)data[1] << 8u) |
        ((uint32_t)data[2] << 16u) | ((uint32_t)data[3] << 24u);
}

static void write_u16_le(uint8_t *data, uint16_t value)
{
    data[0] = (uint8_t)value;
    data[1] = (uint8_t)(value >> 8u);
}

static void write_u32_le(uint8_t *data, uint32_t value)
{
    data[0] = (uint8_t)value;
    data[1] = (uint8_t)(value >> 8u);
    data[2] = (uint8_t)(value >> 16u);
    data[3] = (uint8_t)(value >> 24u);
}

static uint16_t crc16_update(uint16_t crc, uint8_t value)
{
    crc ^= (uint16_t)value << 8u;
    for (uint8_t bit = 0u; bit < 8u; ++bit) {
        crc = (crc & 0x8000u) != 0u ?
            (uint16_t)((crc << 1u) ^ 0x1021u) : (uint16_t)(crc << 1u);
    }
    return crc;
}

static uint16_t crc16(const uint8_t *data, size_t length)
{
    uint16_t crc = 0xFFFFu;
    for (size_t i = 0u; i < length; ++i) crc = crc16_update(crc, data[i]);
    return crc;
}

static uint8_t stream_index(log_record_kind_t kind)
{
    switch (kind) {
    case LOG_RECORD_MINUTE: return 0u;
    case LOG_RECORD_HOUR: return 1u;
    case LOG_RECORD_ALERT: return 2u;
    default: return 0xFFu;
    }
}

static uint8_t stream_kind(uint8_t index)
{
    static const uint8_t kinds[3] = {
        LOG_RECORD_MINUTE, LOG_RECORD_HOUR, LOG_RECORD_ALERT
    };
    return index < 3u ? kinds[index] : 0xFFu;
}

static uint16_t slot_size(log_record_kind_t kind)
{
    const size_t data_size = log_record_data_size(kind);
    return data_size == 0u ? 0u : (uint16_t)(data_size + 3u);
}

static uint16_t slot_capacity(log_record_kind_t kind)
{
    const uint16_t size = slot_size(kind);
    return size == 0u ? 0u :
        (uint16_t)((SPI_LOG_SECTOR_SIZE - SPI_LOG_HEADER_SIZE) / size);
}

static uint32_t sector_address(const spi_log_stream_t *stream, uint16_t index)
{
    return stream->base_address + (uint32_t)index * SPI_LOG_SECTOR_SIZE;
}

/* Returns 1 for a committed valid header, 0 for erased, -1 for corrupt,
 * and -2 for a sector retired before erasure. */
static int8_t read_header(uint32_t address, spi_log_header_t *header)
{
    uint8_t bytes[SPI_LOG_HEADER_SIZE];
    bool erased = true;
    if (header == NULL || !spi_nor_read(address, bytes, sizeof(bytes))) {
        return -1;
    }
    for (uint8_t i = 0u; i < sizeof(bytes); ++i) {
        if (bytes[i] != 0xFFu) erased = false;
    }
    if (erased) return 0;
    if (bytes[SPI_LOG_HEADER_RETIRE_OFFSET] == SPI_LOG_HEADER_RETIRED) {
        return -2;
    }

    if (bytes[0] != SPI_LOG_MAGIC_0 || bytes[1] != SPI_LOG_MAGIC_1 ||
        bytes[2] != SPI_LOG_MAGIC_2 || bytes[3] != SPI_LOG_MAGIC_3 ||
        bytes[4] != SPI_LOG_VERSION ||
        bytes[SPI_LOG_HEADER_COMMIT_OFFSET] != SPI_LOG_HEADER_COMMIT ||
        read_u16_le(&bytes[14]) != crc16(bytes, 14u)) return -1;

    header->kind = (log_record_kind_t)bytes[5];
    header->segment_id = bytes[6];
    header->year_descriptor = bytes[7];
    header->generation = read_u32_le(&bytes[8]);
    header->epoch = read_u16_le(&bytes[12]);
    if (stream_index(header->kind) == 0xFFu ||
        header->segment_id >= LOG_SEGMENT_ID_COUNT ||
        (header->year_descriptor & 0xC0u) != 0u) return -1;
    return 1;
}

static bool sector_fully_erased(uint32_t address)
{
    uint8_t buffer[64];
    for (uint16_t offset = 0u; offset < SPI_LOG_SECTOR_SIZE;
         offset = (uint16_t)(offset + sizeof(buffer))) {
        if (!spi_nor_read(address + offset, buffer, sizeof(buffer))) return false;
        for (uint8_t i = 0u; i < sizeof(buffer); ++i) {
            if (buffer[i] != 0xFFu) return false;
        }
    }
    return true;
}

static bool erase_and_verify(uint32_t address)
{
    if (!spi_nor_erase_sector(address)) return false;
    return sector_fully_erased(address);
}

static uint16_t slot_crc(log_record_kind_t kind,
                         const spi_log_header_t *header, uint16_t index,
                         const uint8_t *data, size_t length)
{
    uint16_t crc = 0xFFFFu;
    crc = crc16_update(crc, (uint8_t)kind);
    crc = crc16_update(crc, header->segment_id);
    crc = crc16_update(crc, header->year_descriptor);
    crc = crc16_update(crc, (uint8_t)header->epoch);
    crc = crc16_update(crc, (uint8_t)(header->epoch >> 8u));
    for (uint8_t i = 0u; i < 4u; ++i) {
        crc = crc16_update(crc, (uint8_t)(header->generation >>
            (i * 8u)));
    }
    crc = crc16_update(crc, (uint8_t)index);
    crc = crc16_update(crc, (uint8_t)(index >> 8u));
    for (size_t i = 0u; i < length; ++i) crc = crc16_update(crc, data[i]);
    return crc;
}

/* Returns 1 for valid, 0 for erased, and -1 for an incomplete/corrupt slot. */
static int8_t read_slot(log_record_kind_t kind,
                        const spi_log_header_t *header, uint32_t sector,
                        uint16_t index,
                        log_record_t *record)
{
    uint8_t bytes[LOG_ALERT_DATA_BYTES + 3u];
    const size_t data_size = log_record_data_size(kind);
    const uint16_t size = slot_size(kind);
    if (header == NULL || record == NULL || data_size == 0u || size == 0u ||
        index >= slot_capacity(kind)) return -1;
    const uint32_t address = sector + SPI_LOG_HEADER_SIZE +
        (uint32_t)index * size;
    if (!spi_nor_read(address, bytes, size)) return -1;

    {
        bool erased = true;
        for (uint16_t i = 0u; i < size; ++i) {
            if (bytes[i] != 0xFFu) erased = false;
        }
        if (erased) return 0;
    }

    if (bytes[data_size + 2u] != SPI_LOG_RECORD_COMMIT ||
        read_u16_le(&bytes[data_size]) != slot_crc(kind, header, index,
            bytes, data_size) ||
        !log_record_decode(kind, bytes, data_size, record) ||
        record->segment_id != header->segment_id) return -1;
    return 1;
}

static uint16_t count_sector_records(log_record_kind_t kind,
                                     const spi_log_header_t *header,
                                     uint32_t address)
{
    uint16_t count = 0u;
    log_record_t record;
    const uint16_t limit = slot_capacity(kind);
    while (count < limit && read_slot(kind, header, address, count,
            &record) == 1) ++count;
    return count;
}

static bool write_header(uint32_t address, log_record_kind_t kind,
                         uint16_t epoch, uint8_t year_descriptor,
                         uint32_t generation)
{
    uint8_t bytes[SPI_LOG_HEADER_SIZE];
    spi_log_header_t verify;
    const uint8_t marker = SPI_LOG_HEADER_COMMIT;

    if (!sector_fully_erased(address)) return false;
    for (uint8_t i = 0u; i < sizeof(bytes); ++i) bytes[i] = 0xFFu;
    bytes[0] = SPI_LOG_MAGIC_0;
    bytes[1] = SPI_LOG_MAGIC_1;
    bytes[2] = SPI_LOG_MAGIC_2;
    bytes[3] = SPI_LOG_MAGIC_3;
    bytes[4] = SPI_LOG_VERSION;
    bytes[5] = (uint8_t)kind;
    bytes[6] = (uint8_t)(epoch % LOG_SEGMENT_ID_COUNT);
    bytes[7] = year_descriptor;
    write_u32_le(&bytes[8], generation);
    write_u16_le(&bytes[12], epoch);
    write_u16_le(&bytes[14], crc16(bytes, 14u));

    if (!spi_nor_program_page(address, bytes, 16u) ||
        !spi_nor_program_page(address + SPI_LOG_HEADER_COMMIT_OFFSET,
            &marker, 1u) || read_header(address, &verify) != 1) return false;
    return verify.kind == kind && verify.epoch == epoch &&
        verify.segment_id == (uint8_t)(epoch % LOG_SEGMENT_ID_COUNT) &&
        verify.year_descriptor == year_descriptor &&
        verify.generation == generation;
}

static bool sector_info_at(spi_log_stream_t *stream, uint16_t sector_index,
                           spi_log_sector_info_t *info)
{
    spi_log_header_t header;
    const uint32_t address = sector_address(stream, sector_index);
    if (read_header(address, &header) != 1) return false;
    info->address = address;
    info->generation = header.generation;
    info->epoch = header.epoch;
    info->segment_id = header.segment_id;
    info->year_descriptor = header.year_descriptor;
    info->record_count = count_sector_records(header.kind, &header, address);
    return true;
}

bool spi_log_init(void)
{
    const uint32_t capacity = spi_nor_size_bytes();
    const uint32_t sector_count = capacity / SPI_LOG_SECTOR_SIZE;
    uint32_t base = 0u;
    uint32_t partition_base_count;
    uint32_t remainder;

    log_ready = false;
    if (!spi_nor_is_ready() || sector_count < 6u ||
        capacity % SPI_LOG_SECTOR_SIZE != 0u) return false;
    partition_base_count = sector_count / 3u;
    remainder = sector_count % 3u;

    for (uint8_t index = 0u; index < 3u; ++index) {
        spi_log_stream_t *stream = &streams[index];
        stream->base_address = base;
        stream->sector_count = (uint16_t)(partition_base_count +
            (index == 2u ? remainder : 0u));
        stream->active_address = 0u;
        stream->active_generation = 0u;
        stream->newest_generation = 0u;
        stream->active_epoch = 0u;
        stream->slot_capacity = slot_capacity(
            (log_record_kind_t)stream_kind(index));
        stream->next_slot = 0u;
        stream->active_segment_id = 0u;
        stream->active_year_descriptor = 0u;
        stream->has_generation = false;
        stream->has_active = false;
        stream->appendable = false;

        for (uint16_t sector = 0u; sector < stream->sector_count; ++sector) {
            const uint32_t address = sector_address(stream, sector);
            spi_log_header_t header;
            const int8_t status = read_header(address, &header);
            if (status == 0) continue;
            if (status < 0) {
                uint8_t prefix[SPI_LOG_HEADER_COMMIT_OFFSET + 1u];
                if (!spi_nor_read(address, prefix, sizeof(prefix))) return false;
                /* The header commit byte is always programmed before records.
                 * An uncommitted sector is safe to reclaim after a reset. */
                if (status == -2 ||
                    prefix[SPI_LOG_HEADER_COMMIT_OFFSET] !=
                        SPI_LOG_HEADER_COMMIT) {
                    if (!erase_and_verify(address)) return false;
                }
                continue;
            }
            if (stream_index(header.kind) != index) continue;
            if (!stream->has_generation ||
                header.generation > stream->newest_generation) {
                stream->has_generation = true;
                stream->newest_generation = header.generation;
                stream->has_active = true;
                stream->active_address = address;
                stream->active_generation = header.generation;
                stream->active_epoch = header.epoch;
                stream->active_segment_id = header.segment_id;
                stream->active_year_descriptor = header.year_descriptor;
            }
        }
        base += (uint32_t)stream->sector_count * SPI_LOG_SECTOR_SIZE;

        if (stream->has_active) {
            spi_log_header_t active_header;
            log_record_t record;
            if (read_header(stream->active_address, &active_header) != 1) {
                return false;
            }
            stream->next_slot = 0u;
            stream->appendable = true;
            while (stream->next_slot < stream->slot_capacity) {
                const int8_t status = read_slot(
                    (log_record_kind_t)stream_kind(index), &active_header,
                    stream->active_address, stream->next_slot, &record);
                if (status == 0) break;
                if (status < 0) {
                    stream->next_slot = stream->slot_capacity;
                    stream->appendable = false;
                    break;
                }
                ++stream->next_slot;
            }
            if (stream->next_slot == stream->slot_capacity) {
                stream->appendable = false;
            }
        }
    }

    log_ready = true;
    return true;
}

bool spi_log_latest_epoch(uint16_t *epoch)
{
    bool found = false;
    uint16_t newest = 0u;
    if (!log_ready || epoch == NULL) return false;
    for (uint8_t i = 0u; i < 3u; ++i) {
        if (!streams[i].has_active) continue;
        if (!found || streams[i].active_epoch > newest) {
            newest = streams[i].active_epoch;
            found = true;
        }
    }
    if (found) *epoch = newest;
    return found;
}

uint32_t spi_log_free_records(log_record_kind_t kind)
{
    const uint8_t index = stream_index(kind);
    spi_log_stream_t *stream;
    uint32_t free_records = 0u;
    uint16_t erased_count = 0u;
    uint32_t first_address = 0u;
    if (!log_ready || index == 0xFFu) return 0u;
    stream = &streams[index];
    erased_count = count_erased_headers(stream, &first_address);
    free_records = (uint32_t)erased_count * stream->slot_capacity;
    if (stream->has_active && stream->appendable) {
        free_records += stream->slot_capacity - stream->next_slot;
    }
    /* Keep one erased sector aside for power-loss recovery and rotation. */
    if (erased_count != 0u && free_records >= stream->slot_capacity) {
        free_records -= stream->slot_capacity;
    }
    return free_records;
}

bool spi_log_find_oldest(log_record_kind_t kind,
                         spi_log_sector_info_t *sector)
{
    const uint8_t index = stream_index(kind);
    spi_log_stream_t *stream;
    bool found = false;
    spi_log_sector_info_t oldest = {0u, 0u, 0u, 0u, 0u, 0u};

    if (!log_ready || index == 0xFFu || sector == NULL) return false;
    stream = &streams[index];
    for (uint16_t i = 0u; i < stream->sector_count; ++i) {
        spi_log_sector_info_t candidate;
        if (!sector_info_at(stream, i, &candidate)) continue;
        if (!found || candidate.generation < oldest.generation) {
            oldest = candidate;
            found = true;
        }
    }
    if (found) *sector = oldest;
    return found;
}

static uint16_t count_erased_headers(spi_log_stream_t *stream,
                                    uint32_t *first_address)
{
    uint16_t erased_count = 0u;
    for (uint16_t i = 0u; i < stream->sector_count; ++i) {
        spi_log_header_t header;
        const uint32_t address = sector_address(stream, i);
        int8_t status = read_header(address, &header);
        if (status == -2) {
            if (erase_and_verify(address)) status = 0;
        } else if (status < 0) {
            uint8_t marker = SPI_LOG_HEADER_COMMIT;
            if (spi_nor_read(address + SPI_LOG_HEADER_COMMIT_OFFSET,
                    &marker, 1u) && marker != SPI_LOG_HEADER_COMMIT &&
                erase_and_verify(address)) status = 0;
        }
        if (status == 0) {
            if (erased_count == 0u && first_address != NULL) {
                *first_address = address;
            }
            ++erased_count;
        }
    }
    return erased_count;
}

spi_log_prepare_result_t spi_log_prepare(log_record_kind_t kind,
                                         uint16_t epoch,
                                         uint8_t year_descriptor,
                                         spi_log_sector_info_t *oldest)
{
    const uint8_t index = stream_index(kind);
    spi_log_stream_t *stream;
    uint32_t free_address = 0u;
    uint16_t erased_count;
    uint32_t generation;
    const uint8_t segment_id =
        (uint8_t)(epoch % LOG_SEGMENT_ID_COUNT);

    if (!log_ready || index == 0xFFu ||
        (year_descriptor & 0xC0u) != 0u) return SPI_LOG_ERROR;
    stream = &streams[index];
    if (stream->has_active && stream->appendable &&
        stream->active_epoch == epoch &&
        stream->active_segment_id == segment_id &&
        stream->active_year_descriptor == year_descriptor) {
        return SPI_LOG_READY;
    }

    erased_count = count_erased_headers(stream, &free_address);
    if (erased_count <= 1u) {
        return spi_log_find_oldest(kind, oldest) ?
            SPI_LOG_NEEDS_EVICTION : SPI_LOG_ERROR;
    }
    if (!sector_fully_erased(free_address) &&
        !erase_and_verify(free_address)) return SPI_LOG_ERROR;

    generation = stream->has_generation ? stream->newest_generation + 1u : 0u;
    if (!write_header(free_address, kind, epoch, year_descriptor,
            generation)) return SPI_LOG_ERROR;

    stream->active_address = free_address;
    stream->active_generation = generation;
    stream->active_epoch = epoch;
    stream->newest_generation = generation;
    stream->active_segment_id = segment_id;
    stream->active_year_descriptor = year_descriptor;
    stream->next_slot = 0u;
    stream->has_active = true;
    stream->has_generation = true;
    stream->appendable = true;
    return SPI_LOG_READY;
}

bool spi_log_append(log_record_kind_t kind, uint16_t epoch,
                    const log_record_t *record)
{
    const uint8_t index = stream_index(kind);
    spi_log_stream_t *stream;
    spi_log_header_t header;
    uint8_t data[LOG_ALERT_DATA_BYTES];
    uint8_t payload[LOG_ALERT_DATA_BYTES + 2u];
    uint8_t marker = SPI_LOG_RECORD_COMMIT;
    uint8_t verify_bytes[LOG_ALERT_DATA_BYTES + 3u];
    log_record_t decoded;
    const size_t data_size = log_record_data_size(kind);
    const uint16_t size = slot_size(kind);
    uint32_t address;
    uint16_t crc;

    if (!log_ready || index == 0xFFu || record == NULL) return false;
    stream = &streams[index];
    if (!stream->has_active || !stream->appendable ||
        stream->active_epoch != epoch ||
        record->segment_id != stream->active_segment_id ||
        read_header(stream->active_address, &header) != 1 ||
        !log_record_encode(kind, record, data, sizeof(data))) return false;

    address = stream->active_address + SPI_LOG_HEADER_SIZE +
        (uint32_t)stream->next_slot * size;
    if (read_slot(kind, &header, stream->active_address, stream->next_slot,
            &decoded) != 0) {
        stream->appendable = false;
        return false;
    }

    crc = slot_crc(kind, &header, stream->next_slot, data, data_size);
    for (size_t i = 0u; i < data_size; ++i) payload[i] = data[i];
    write_u16_le(&payload[data_size], crc);
    if (!spi_nor_program_page(address, payload, data_size + 2u) ||
        !spi_nor_program_page(address + data_size + 2u, &marker, 1u) ||
        !spi_nor_read(address, verify_bytes, size) ||
        verify_bytes[data_size + 2u] != SPI_LOG_RECORD_COMMIT ||
        read_u16_le(&verify_bytes[data_size]) != crc ||
        !log_record_decode(kind, verify_bytes, data_size, &decoded) ||
        decoded.segment_id != record->segment_id ||
        decoded.period_index != record->period_index ||
        decoded.count != record->count ||
        decoded.maximum_cpm != record->maximum_cpm ||
        decoded.duration_seconds != record->duration_seconds) {
        stream->appendable = false;
        return false;
    }

    ++stream->next_slot;
    if (stream->next_slot >= stream->slot_capacity) {
        stream->appendable = false;
    }
    return true;
}

bool spi_log_read_sector_record(log_record_kind_t kind,
                                const spi_log_sector_info_t *sector,
                                uint16_t index, log_record_t *record)
{
    spi_log_header_t header;
    if (!log_ready || sector == NULL || record == NULL ||
        stream_index(kind) == 0xFFu ||
        read_header(sector->address, &header) != 1 ||
        header.kind != kind || header.generation != sector->generation ||
        header.segment_id != sector->segment_id ||
        header.epoch != sector->epoch) return false;
    return read_slot(kind, &header, sector->address, index, record) == 1;
}

bool spi_log_erase_oldest(log_record_kind_t kind,
                          const spi_log_sector_info_t *sector)
{
    spi_log_sector_info_t oldest;
    const uint8_t index = stream_index(kind);
    const uint8_t retired = SPI_LOG_HEADER_RETIRED;
    uint8_t verify_retired = 0xFFu;
    if (!log_ready || index == 0xFFu || sector == NULL ||
        !spi_log_find_oldest(kind, &oldest) ||
        oldest.address != sector->address ||
        oldest.generation != sector->generation ||
        oldest.epoch != sector->epoch ||
        !spi_nor_program_page(sector->address +
            SPI_LOG_HEADER_RETIRE_OFFSET, &retired, 1u)) return false;

    if (streams[index].has_active &&
        streams[index].active_address == sector->address) {
        streams[index].has_active = false;
        streams[index].appendable = false;
        streams[index].next_slot = 0u;
    }
    if (!spi_nor_read(sector->address + SPI_LOG_HEADER_RETIRE_OFFSET,
            &verify_retired, 1u) || verify_retired != retired) return false;
    return erase_and_verify(sector->address);
}

static uint8_t leap_year(uint16_t year)
{
    return (uint8_t)((year % 4u) == 0u);
}

static bool period_for_time(log_record_kind_t kind, uint32_t seconds,
                            uint32_t *period_index,
                            uint8_t *year_descriptor)
{
    uint32_t days = seconds / 86400u;
    uint16_t year = 2000u;
    uint32_t day_in_year;
    uint32_t day_seconds = seconds % 86400u;

    while (year <= 2099u) {
        const uint16_t days_in_year = leap_year(year) != 0u ? 366u : 365u;
        if (days < days_in_year) break;
        days -= days_in_year;
        ++year;
    }
    if (year > 2099u || period_index == NULL || year_descriptor == NULL) {
        return false;
    }
    day_in_year = days;
    *year_descriptor = SPI_LOG_YEAR_DESCRIPTOR(year, true);
    switch (kind) {
    case LOG_RECORD_MINUTE:
    case LOG_RECORD_ALERT:
        *period_index = day_in_year * 1440u + day_seconds / 60u;
        return true;
    case LOG_RECORD_HOUR:
        *period_index = day_in_year * 24u + day_seconds / 3600u;
        return true;
    case LOG_RECORD_DAY:
        *period_index = day_in_year;
        return true;
    default:
        return false;
    }
}

bool spi_log_query_read(void *context, log_record_kind_t kind,
                        uint16_t epoch, uint32_t period_start,
                        log_record_t *record, bool *live)
{
    const log_query_time_context_t *time_context = context;
    const uint8_t index = stream_index(kind);
    spi_log_stream_t *stream;
    uint32_t requested_period;
    uint8_t requested_year;
    bool found = false;
    uint32_t newest_generation = 0u;
    uint16_t newest_slot = 0u;
    log_record_t newest_record;

    if (!log_ready || index == 0xFFu || record == NULL || live == NULL) {
        return false;
    }
    if (time_context != NULL && !time_context->calendar_valid) {
        switch (kind) {
        case LOG_RECORD_MINUTE:
        case LOG_RECORD_ALERT:
            requested_period = (period_start / 60u) % 527040u;
            break;
        case LOG_RECORD_HOUR:
            requested_period = (period_start / 3600u) % 8784u;
            break;
        case LOG_RECORD_DAY:
            requested_period = (period_start / 86400u) % 366u;
            break;
        default: return false;
        }
        requested_year = 0u;
    } else if (!period_for_time(kind, period_start, &requested_period,
            &requested_year)) {
        return false;
    }
    stream = &streams[index];

    for (uint16_t sector_index = 0u;
         sector_index < stream->sector_count; ++sector_index) {
        const uint32_t address = sector_address(stream, sector_index);
        spi_log_header_t header;
        if (read_header(address, &header) != 1 || header.kind != kind ||
            header.epoch != epoch ||
            header.year_descriptor != requested_year) continue;
        for (uint16_t slot = 0u; slot < slot_capacity(kind); ++slot) {
            log_record_t candidate;
            const int8_t status = read_slot(kind, &header, address, slot,
                &candidate);
            if (status != 1) break;
            if (candidate.period_index == requested_period &&
                (!found || header.generation > newest_generation ||
                    (header.generation == newest_generation &&
                     slot > newest_slot))) {
                newest_record = candidate;
                newest_generation = header.generation;
                newest_slot = slot;
                found = true;
            }
        }
    }
    if (!found) return false;
    *record = newest_record;
    *live = false;
    return true;
}

