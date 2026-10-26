#include "protocol_v1.h"
#include "board.h"
#include "second_log.h"
#include "log_history.h"
#include "log_writer.h"
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

#define crc16_ccitt_false(bytes, length) log_crc16((bytes), (length), 0xFFFFu)

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
    bool deferred_action = false;
    uint8_t deferred_action_id = 0u;
    uint8_t deferred_action_value = 0u;

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
            (1u << 2u) | (1u << 3u) | (1u << 4u) | (1u << 5u) |
            (1u << 6u) | (1u << 7u) | (1u << 8u));
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
        write_u16(&response[29], current_status.dose_fraction_3600);
        write_u32(&response[31], current_status.dose_count_total);
        {
            const uint32_t *usage = &current_status.screen_seconds;
            for (uint8_t i = 0u; i < 3u; ++i) {
                write_u32(&response[35u + (uint16_t)i * 4u], usage[i]);
            }
        }
        response_length = 47u;
    } else if (opcode == PROTOCOL_V1_OP_GET_SETTINGS &&
               (payload_length == 0u ||
                (payload_length == 1u && frame[8] == 1u))) {
        const bool include_hidden_mask = payload_length == 1u;
        const uint8_t count = board_display_settings_remote_count();
        response[1] = count;
        if (count > PROTOCOL_V1_MAX_PAYLOAD -
                (include_hidden_mask ? 6u : 2u) ||
            !board_display_settings_remote_read(&response[2], count)) {
            response[0] = PROTOCOL_STATUS_INVALID_VALUE;
            response_length = 1u;
        } else {
            response_length = (uint16_t)count + 2u;
            if (include_hidden_mask) {
                write_u32(&response[2u + count],
                    board_display_settings_hidden_mask());
                response_length += 4u;
            }
        }
    } else if (opcode == PROTOCOL_V1_OP_SET_SETTING &&
               payload_length == 2u) {
        if (!board_display_setting_remote_write(frame[8], frame[9])) {
            response[0] = PROTOCOL_STATUS_INVALID_VALUE;
        }
    } else if (opcode == PROTOCOL_V1_OP_SET_RTC) {
        if (payload_length != 4u ||
            !board_rtc_set_time(read_u32(&frame[8]))) {
            response[0] = PROTOCOL_STATUS_INVALID_VALUE;
        }
    } else if (opcode == PROTOCOL_V1_OP_DEVICE_ACTION &&
               payload_length == 2u) {
        deferred_action_id = frame[8];
        deferred_action_value = frame[9];
        deferred_action = board_display_remote_action_valid(
            deferred_action_id, deferred_action_value);
        if (!deferred_action) response[0] = PROTOCOL_STATUS_INVALID_VALUE;
    } else if (opcode == PROTOCOL_V1_OP_GET_LOG_INFO &&
               payload_length == 0u) {
        response[1] = board_display_log_memory();
        for (uint8_t type = 0u; type < 4u; ++type) {
            uint32_t count = log_history_count(type);
            write_u16(&response[2u + type * 2u], (uint16_t)(count > UINT16_MAX ? UINT16_MAX : count));
        }
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
        response[26] = 2u;
        response[27] = board_display_log_source();
        response[28] = log_writer_fault() ? 1u : 0u;
        response[29] = second_log_count();
        response_length = 30u;
    } else if (opcode == PROTOCOL_V1_OP_GET_LOG_PAGE && payload_length == 18u) {
        const uint8_t *request = &frame[8];
        uint8_t type = request[0], requested = request[1], count = 0u;
        uint32_t period = read_u32(&request[2]);
        log_cursor_t cursor = {read_u32(&request[6]), read_u16(&request[10]), read_u16(&request[12])};
        uint32_t limit = read_u32(&request[14]);
        if (type > 3u || requested == 0u || requested > 2u || period < 60u || period % 60u || period > LOG_QUERY_MAX_PERIOD_SECONDS) {
            response[0] = PROTOCOL_STATUS_INVALID_VALUE;
        } else {
            log_point_t point;
            while (count < requested && log_history_next(type, period, &cursor, limit, &point)) {
                uint8_t *entry = &response[12u + (uint16_t)count * 25u];
                write_u32(&entry[0], point.period_start);
                write_u32(&entry[4], point.count);
                write_u32(&entry[8], point.average_cpm);
                write_u32(&entry[12], point.maximum_cpm);
                write_u32(&entry[16], point.covered_seconds);
                write_u16(&entry[20], point.epoch);
                entry[22] = (point.available ? 1u : 0u) | (point.partial ? 2u : 0u) |
                    (point.live ? 4u : 0u) | (point.calendar_valid ? 8u : 0u);
                write_u16(&entry[23], point.id);
                ++count;
            }
            log_cursor_t peek = cursor;
            response[1] = type; response[2] = count;
            response[3] = count == requested && log_history_next(type, period, &peek, limit, &point) ? 1u : 0u;
            write_u32(&response[4], cursor.time);
            write_u16(&response[8], cursor.epoch);
            write_u16(&response[10], cursor.skip);
            response_length = (uint16_t)(12u + (uint16_t)count * 25u);
        }
    } else if (opcode == PROTOCOL_V1_OP_GET_SPI_DUMP_PAGE &&
               payload_length == 5u) {
        const uint32_t offset = read_u32(&frame[8]);
        const uint8_t requested = frame[12];
        const uint32_t capacity = spi_nor_size_bytes();
        if (board_display_log_source() != BOARD_LOG_MEMORY_SPI || !spi_nor_is_ready() ||
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
    if (deferred_action) {
        while ((USART_ISR & (1u << 6u)) == 0u) { }
        board_display_remote_action_execute(deferred_action_id,
            deferred_action_value);
    }
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
