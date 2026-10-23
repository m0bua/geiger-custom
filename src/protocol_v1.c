#include "protocol_v1.h"
#include "board.h"
#include "log_storage.h"
#include "spi_nor.h"

#include <stdbool.h>
#include <stddef.h>

#define REG32(address) (*(volatile uint32_t *)(uintptr_t)(address))
#define RCC_BASE       0x40021000u
#define RCC_APB2ENR    REG32(RCC_BASE + 0x34u)
#define GPIOA_BASE     0x50000000u
#define GPIO_MODER     0x00u
#define GPIO_PUPDR     0x0Cu
#define GPIO_AFRH      0x24u
#define USART1_BASE    0x40013800u
#define USART_CR1      REG32(USART1_BASE + 0x00u)
#define USART_BRR      REG32(USART1_BASE + 0x0Cu)
#define USART_ISR      REG32(USART1_BASE + 0x1Cu)
#define USART_ICR      REG32(USART1_BASE + 0x20u)
#define USART_RDR      REG32(USART1_BASE + 0x24u)
#define USART_TDR      REG32(USART1_BASE + 0x28u)
#define NVIC_ISER0     REG32(0xE000E100u)

#define RX_RING_SIZE       64u
#define MAX_RAW_SIZE       (8u + PROTOCOL_V1_MAX_PAYLOAD + 2u)
#define MAX_ENCODED_SIZE   (MAX_RAW_SIZE + 1u)
#define MAX_WIRE_SIZE      (MAX_ENCODED_SIZE + 1u)

#define FRAME_KIND_REQUEST  1u
#define FRAME_KIND_RESPONSE 2u
#define SPI_DUMP_MAX_DATA 58u

enum {
    PROTOCOL_STATUS_OK = 0u,
    PROTOCOL_STATUS_UNKNOWN_OPCODE = 1u,
    PROTOCOL_STATUS_INVALID_VALUE = 2u
};

static volatile uint8_t rx_ring[RX_RING_SIZE];
static volatile uint8_t rx_head;
static volatile uint8_t rx_tail;
static uint8_t encoded_frame[MAX_ENCODED_SIZE];
static uint8_t tx_frame[MAX_WIRE_SIZE];
static uint8_t encoded_length;
static bool discard_until_delimiter;
static protocol_v1_status_t current_status;
static uint16_t cached_alert_episode_count;
static bool cached_alert_episode_count_valid;

static uint16_t read_u16(const uint8_t *p)
{
    return (uint16_t)p[0] | (uint16_t)((uint16_t)p[1] << 8u);
}

static uint32_t read_u32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8u) |
        ((uint32_t)p[2] << 16u) | ((uint32_t)p[3] << 24u);
}

static void write_u16(uint8_t *p, uint16_t value)
{
    p[0] = (uint8_t)value;
    p[1] = (uint8_t)(value >> 8u);
}

static void write_u32(uint8_t *p, uint32_t value)
{
    p[0] = (uint8_t)value;
    p[1] = (uint8_t)(value >> 8u);
    p[2] = (uint8_t)(value >> 16u);
    p[3] = (uint8_t)(value >> 24u);
}

static uint16_t crc16_ccitt_false(const uint8_t *data, uint16_t length)
{
    uint16_t crc = 0xFFFFu;
    for (uint16_t i = 0u; i < length; ++i) {
        crc ^= (uint16_t)data[i] << 8u;
        for (uint8_t bit = 0u; bit < 8u; ++bit) {
            crc = (crc & 0x8000u) != 0u ?
                (uint16_t)((crc << 1u) ^ 0x1021u) :
                (uint16_t)(crc << 1u);
        }
    }
    return crc;
}

static bool cobs_decode(const uint8_t *input, uint8_t input_length,
                        uint8_t *output, uint8_t *output_length)
{
    uint8_t read_pos = 0u;
    uint8_t write_pos = 0u;

    while (read_pos < input_length) {
        const uint8_t code = input[read_pos++];
        if (code == 0u) return false;
        const uint8_t copy_length = (uint8_t)(code - 1u);
        if ((uint16_t)read_pos + copy_length > input_length ||
            (uint16_t)write_pos + copy_length > MAX_RAW_SIZE) {
            return false;
        }
        for (uint8_t i = 0u; i < copy_length; ++i) {
            output[write_pos++] = input[read_pos++];
        }
        if (code != 0xFFu && read_pos < input_length) {
            if (write_pos >= MAX_RAW_SIZE) return false;
            output[write_pos++] = 0u;
        }
    }
    *output_length = write_pos;
    return true;
}

static uint8_t cobs_encode(const uint8_t *input, uint8_t input_length,
                           uint8_t *output)
{
    uint8_t read_pos = 0u;
    uint8_t write_pos = 1u;
    uint8_t code_pos = 0u;
    uint8_t code = 1u;

    while (read_pos < input_length) {
        if (input[read_pos] == 0u) {
            output[code_pos] = code;
            code_pos = write_pos++;
            code = 1u;
            ++read_pos;
        } else {
            output[write_pos++] = input[read_pos++];
            ++code;
            if (code == 0xFFu) {
                output[code_pos] = code;
                code_pos = write_pos++;
                code = 1u;
            }
        }
    }
    output[code_pos] = code;
    output[write_pos++] = 0u;
    return write_pos;
}

static void uart_send(const uint8_t *data, uint8_t length)
{
    for (uint8_t i = 0u; i < length; ++i) {
        while ((USART_ISR & (1u << 7u)) == 0u) { }
        USART_TDR = data[i];
    }
}

static void send_response(uint16_t request_id, uint16_t opcode,
                          const uint8_t *payload, uint16_t payload_length,
                          uint8_t *frame)
{
    const uint16_t raw_length = (uint16_t)(8u + payload_length);
    frame[0] = PROTOCOL_V1_VERSION;
    frame[1] = FRAME_KIND_RESPONSE;
    write_u16(&frame[2], request_id);
    write_u16(&frame[4], opcode);
    write_u16(&frame[6], payload_length);
    for (uint16_t i = 0u; i < payload_length; ++i) {
        frame[8u + i] = payload[i];
    }
    write_u16(&frame[raw_length], crc16_ccitt_false(frame, raw_length));
    uart_send(tx_frame, cobs_encode(frame,
        (uint8_t)(raw_length + 2u), tx_frame));
}

static void handle_frame(uint8_t *frame, uint8_t length)
{
    uint16_t request_id;
    uint16_t opcode;
    uint16_t payload_length;
    uint8_t response[PROTOCOL_V1_MAX_PAYLOAD];
    uint16_t response_length = 1u;

    if (length < 10u || frame[0] != PROTOCOL_V1_VERSION ||
        frame[1] != FRAME_KIND_REQUEST) return;
    request_id = read_u16(&frame[2]);
    opcode = read_u16(&frame[4]);
    payload_length = read_u16(&frame[6]);
    if (payload_length > PROTOCOL_V1_MAX_PAYLOAD ||
        length != (uint8_t)(10u + payload_length) ||
        read_u16(&frame[8u + payload_length]) !=
            crc16_ccitt_false(frame, (uint16_t)(8u + payload_length))) {
        return;
    }

    response[0] = PROTOCOL_STATUS_OK;
    if (opcode == PROTOCOL_V1_OP_HELLO && payload_length == 0u) {
        /* model_id, protocol version, max payload, capability mask */
        write_u16(&response[1], 1u);
        response[3] = PROTOCOL_V1_VERSION;
        write_u16(&response[4], PROTOCOL_V1_MAX_PAYLOAD);
        write_u32(&response[6], (1u << 0u) | (1u << 1u) |
            (1u << 2u) | (1u << 3u) | (1u << 4u) | (1u << 5u));
        response_length = 10u;
    } else if (opcode == PROTOCOL_V1_OP_GET_STATUS && payload_length == 0u) {
        /* status, uptime, today's CNT, CPM, dose in nSv, ADC raw, flags */
        write_u32(&response[1], current_status.uptime_seconds);
        write_u32(&response[5], current_status.count_today);
        write_u32(&response[9], current_status.rate_cpm);
        write_u32(&response[13], current_status.dose_nsv);
        write_u16(&response[17], current_status.battery_raw);
        write_u16(&response[19], current_status.solar_raw);
        write_u16(&response[21], current_status.usb_raw);
        write_u16(&response[23], current_status.flags);
        write_u32(&response[25], current_status.count_total);
        response_length = 29u;
    } else if (opcode == PROTOCOL_V1_OP_GET_SETTINGS &&
               payload_length == 0u) {
        const uint8_t count = board_display_settings_remote_count();
        response[1] = count;
        if (count > PROTOCOL_V1_MAX_PAYLOAD - 2u ||
            !board_display_settings_remote_read(&response[2], count)) {
            response[0] = PROTOCOL_STATUS_INVALID_VALUE;
            response_length = 1u;
        } else {
            response_length = (uint16_t)count + 2u;
        }
    } else if (opcode == PROTOCOL_V1_OP_SET_SETTING &&
               payload_length == 2u) {
        if (!board_display_setting_remote_write(frame[8], frame[9])) {
            response[0] = PROTOCOL_STATUS_INVALID_VALUE;
        }
    } else if (opcode == PROTOCOL_V1_OP_GET_LOG_INFO &&
               payload_length == 0u) {
        uint16_t minutes;
        uint16_t hours;
        uint16_t days;
        log_storage_log_counts(&minutes, &hours, &days);
        response[1] = board_display_use_spi() ? 1u : 0u;
        write_u16(&response[2], minutes);
        write_u16(&response[4], hours);
        write_u16(&response[6], days);
        write_u16(&response[8], log_storage_alert_record_count());
        uint32_t uptime_seconds = 0u;
        uint32_t calendar_seconds = 0u;
        bool calendar_valid = false;
        (void)board_rtc_log_time(&uptime_seconds, &calendar_seconds,
            &calendar_valid);
        response[10] = calendar_valid ? 1u : 0u;
        write_u32(&response[11], calendar_valid ? calendar_seconds : 0u);
        const uint32_t spi_bytes = spi_nor_size_bytes();
        const spi_nor_jedec_id_t jedec = spi_nor_jedec_id();
        response[15] = (uint8_t)(spi_bytes / (1024u * 1024u));
        response[16] = spi_nor_is_ready() ? 1u : 0u;
        response[17] = jedec.manufacturer;
        response[18] = jedec.memory_type;
        response[19] = jedec.capacity;
        response[20] = board_rtc_debug_status();
        response[21] = board_rtc_clock_source();
        write_u32(&response[22], uptime_seconds);
        response_length = 26u;
    } else if (opcode == PROTOCOL_V1_OP_GET_LOG_PAGE &&
               payload_length == 4u) {
        const uint8_t type = frame[8];
        const uint16_t offset = read_u16(&frame[9]);
        const uint8_t requested = frame[11];
        uint16_t total = 0u;
        uint8_t count = 0u;
        uint32_t doses[3];
        uint8_t packed_min_max[15];
        uint32_t timestamps[3] = {0u, 0u, 0u};
        if (requested == 0u || requested > 3u || type > 3u ||
            (type != 0u && !board_display_use_spi())) {
            response[0] = PROTOCOL_STATUS_INVALID_VALUE;
            response_length = 1u;
        } else {
            response[1] = type;
            write_u16(&response[2], offset);
            if (type == 3u) {
                uint32_t starts[3];
                uint32_t durations[3];
                uint32_t alert_doses[3];
                uint32_t alert_maxima[3];
                if (offset == 0u || !cached_alert_episode_count_valid) {
                    cached_alert_episode_count =
                        log_storage_alert_episode_count();
                    cached_alert_episode_count_valid = true;
                }
                total = cached_alert_episode_count;
                count = offset >= total ? 0u :
                    log_storage_alert_page(offset, starts, durations,
                        alert_doses, alert_maxima, requested);
                for (uint8_t i = 0u; i < count; ++i) {
                    uint8_t *entry = &response[7u + (uint16_t)i * 16u];
                    write_u32(&entry[0], starts[i]);
                    write_u32(&entry[4], durations[i]);
                    write_u32(&entry[8], alert_doses[i]);
                    write_u32(&entry[12], alert_maxima[i]);
                }
            } else {
                uint16_t minutes;
                uint16_t hours;
                uint16_t days;
                uint32_t uptime_seconds = 0u;
                uint32_t calendar_seconds = 0u;
                bool calendar_valid = false;
                const log_graph_period_t period = type == 0u ?
                    LOG_GRAPH_DAYS : (type == 1u ? LOG_GRAPH_MINUTES :
                    LOG_GRAPH_HOURS);
                const uint32_t period_seconds = type == 0u ? 86400u :
                    (type == 1u ? 60u : 3600u);
                log_storage_log_counts(&minutes, &hours, &days);
                total = (type == 0u ? days :
                    (type == 1u ? minutes : hours));
                if (total != UINT16_MAX) ++total; /* current RAM period */
                const uint8_t actual = offset >= total ? 0u :
                    (uint8_t)(((uint16_t)(total - offset) < requested) ?
                    (total - offset) : requested);
                if (actual != 0u && !log_storage_graph_page(period, offset,
                        doses, packed_min_max, actual, &count)) {
                    response[0] = PROTOCOL_STATUS_INVALID_VALUE;
                    response_length = 1u;
                    goto log_page_done;
                }
                (void)board_rtc_log_time(&uptime_seconds, &calendar_seconds,
                    &calendar_valid);
                if (calendar_valid) {
                    const uint32_t progress =
                        log_storage_graph_current_seconds(period);
                    const uint32_t period_start = calendar_seconds >= progress ?
                        calendar_seconds - progress : 0u;
                    for (uint8_t i = 0u; i < count; ++i) {
                        const uint16_t index = (uint16_t)(offset + i);
                        const int64_t nominal = (int64_t)period_start -
                            (int64_t)index * period_seconds;
                        int32_t correction = 0;
                        if (index != 0u && type != 0u) {
                            correction = log_storage_graph_time_correction(
                                period, index, period_start);
                        }
                        const int64_t adjusted = nominal + correction;
                        timestamps[i] = adjusted < 0 ? 0u :
                            (uint32_t)adjusted;
                    }
                }
                for (uint8_t i = 0u; i < count; ++i) {
                    uint8_t *entry = &response[7u + (uint16_t)i * 16u];
                    const uint8_t *pair = &packed_min_max[(uint16_t)i * 5u];
                    uint64_t packed = 0u;
                    for (uint8_t b = 0u; b < 5u; ++b) {
                        packed |= (uint64_t)pair[b] << (b * 8u);
                    }
                    write_u32(&entry[0], timestamps[i]);
                    write_u32(&entry[4], doses[i]);
                    write_u32(&entry[8], (uint32_t)(packed & 0x3FFFFu));
                    write_u32(&entry[12], (uint32_t)((packed >> 18u) & 0x3FFFFu));
                }
            }
            write_u16(&response[4], total);
            response[6] = count;
            response_length = (uint16_t)(7u + (uint16_t)count * 16u);
        }
log_page_done:
        ;
    } else if (opcode == PROTOCOL_V1_OP_GET_SPI_DUMP_PAGE &&
               payload_length == 5u) {
        const uint32_t offset = read_u32(&frame[8]);
        const uint8_t requested = frame[12];
        const uint32_t capacity = spi_nor_size_bytes();
        if (!board_display_use_spi() || !spi_nor_is_ready() ||
            requested == 0u || requested > SPI_DUMP_MAX_DATA ||
            offset > capacity || requested > capacity - offset ||
            !spi_nor_read(offset, &response[6], requested)) {
            response[0] = PROTOCOL_STATUS_INVALID_VALUE;
            response_length = 1u;
        } else {
            write_u32(&response[1], offset);
            response[5] = requested;
            response_length = (uint16_t)(6u + requested);
        }
    } else {
        response[0] = PROTOCOL_STATUS_UNKNOWN_OPCODE;
    }
    send_response(request_id, opcode, response, response_length, frame);
}

void protocol_v1_transport_init(void)
{
    /* USART1 is clocked from PCLK2=32 MHz: 115200 baud, 8N1. */
    RCC_APB2ENR |= 1u << 14u;
    (void)RCC_APB2ENR;
    REG32(GPIOA_BASE + GPIO_AFRH) =
        (REG32(GPIOA_BASE + GPIO_AFRH) & ~((0xFu << 4u) | (0xFu << 8u))) |
        (4u << 4u) | (4u << 8u);
    REG32(GPIOA_BASE + GPIO_PUPDR) =
        (REG32(GPIOA_BASE + GPIO_PUPDR) & ~(3u << 20u)) | (1u << 20u);
    REG32(GPIOA_BASE + GPIO_MODER) =
        (REG32(GPIOA_BASE + GPIO_MODER) & ~((3u << 18u) | (3u << 20u))) |
        (2u << 18u) | (2u << 20u);

    USART_CR1 = 0u;
    USART_BRR = 0x0116u;
    USART_ICR = 0x0Fu;
    rx_head = 0u;
    rx_tail = 0u;
    encoded_length = 0u;
    discard_until_delimiter = false;
    USART_CR1 = (1u << 5u) | (1u << 3u) | (1u << 2u) | 1u;
    NVIC_ISER0 = 1u << 27u; /* USART1_IRQn */
}

void protocol_v1_set_status(const protocol_v1_status_t *status)
{
    current_status = *status;
}

void protocol_v1_service(void)
{
    while (rx_tail != rx_head) {
        const uint8_t value = rx_ring[rx_tail];
        rx_tail = (uint8_t)((rx_tail + 1u) & (RX_RING_SIZE - 1u));
        if (value == 0u) {
            if (!discard_until_delimiter && encoded_length != 0u) {
                uint8_t raw_length = 0u;
                if (cobs_decode(encoded_frame, encoded_length, encoded_frame,
                        &raw_length)) {
                    handle_frame(encoded_frame, raw_length);
                }
            }
            encoded_length = 0u;
            discard_until_delimiter = false;
        } else if (!discard_until_delimiter) {
            if (encoded_length < MAX_ENCODED_SIZE) {
                encoded_frame[encoded_length++] = value;
            } else {
                encoded_length = 0u;
                discard_until_delimiter = true;
            }
        }
    }
}

void USART1_IRQHandler(void)
{
    const uint32_t status = USART_ISR;
    if ((status & (1u << 5u)) != 0u) {
        const uint8_t value = (uint8_t)USART_RDR;
        const uint8_t next = (uint8_t)((rx_head + 1u) & (RX_RING_SIZE - 1u));
        if (next != rx_tail) {
            rx_ring[rx_head] = value;
            rx_head = next;
        }
    }
    if ((status & 0x0Fu) != 0u) USART_ICR = status & 0x0Fu;
}
