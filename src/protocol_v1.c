#include "protocol_v1.h"
#include "board.h"

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
    uint8_t response[32];
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
            (1u << 2u)); /* GET_STATUS, GET_SETTINGS, SET_SETTING */
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
        response_length = 25u;
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
