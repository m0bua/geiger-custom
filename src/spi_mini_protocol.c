#include "board.h"
#include "spi_nor.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define REG32(address) (*(volatile uint32_t *)(uintptr_t)(address))
#define RCC_BASE 0x40021000u
#define RCC_APB2ENR REG32(RCC_BASE + 0x34u)
#define GPIOA_BASE 0x50000000u
#define USART1_BASE 0x40013800u
#define USART_CR1 REG32(USART1_BASE + 0x00u)
#define USART_BRR REG32(USART1_BASE + 0x0Cu)
#define USART_ISR REG32(USART1_BASE + 0x1Cu)
#define USART_ICR REG32(USART1_BASE + 0x20u)
#define USART_RDR REG32(USART1_BASE + 0x24u)
#define USART_TDR REG32(USART1_BASE + 0x28u)
#define NVIC_ISER0 REG32(0xE000E100u)
#define GPIO_MODER 0x00u
#define GPIO_PUPDR 0x0Cu
#define GPIO_AFRH 0x24u

#define VERSION 1u
#define MAX_PAYLOAD 137u
#define MAX_RAW (10u + MAX_PAYLOAD)
#define MAX_ENCODED (MAX_RAW + 2u)
#define RX_SIZE 256u
#define DUMP_CHUNK 58u
#define OP_HELLO 1u
#define OP_STATUS 2u
#define OP_LOG_INFO 5u
#define OP_LOG_PAGE 6u
#define OP_SPI_DUMP 7u
#define OP_SPI_PROGRAM_PAGE 10u
#define OP_SPI_ERASE_SECTOR 11u

static volatile uint8_t rx[RX_SIZE];
static volatile uint8_t head;
static volatile uint8_t tail;
static uint8_t encoded[MAX_ENCODED];
static uint8_t tx[MAX_ENCODED];
static uint8_t tx_raw[MAX_RAW];
static uint8_t encoded_len;
static bool discard_frame;

static uint16_t get16(const uint8_t *p)
{
    return (uint16_t)p[0] | (uint16_t)((uint16_t)p[1] << 8u);
}

static uint32_t get32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8u) |
        ((uint32_t)p[2] << 16u) | ((uint32_t)p[3] << 24u);
}

static void put16(uint8_t *p, uint16_t n)
{
    p[0] = (uint8_t)n;
    p[1] = (uint8_t)(n >> 8u);
}

static void put32(uint8_t *p, uint32_t n)
{
    p[0] = (uint8_t)n;
    p[1] = (uint8_t)(n >> 8u);
    p[2] = (uint8_t)(n >> 16u);
    p[3] = (uint8_t)(n >> 24u);
}

static uint16_t crc16(const uint8_t *data, uint16_t length)
{
    uint16_t crc = 0xFFFFu;
    for (uint16_t i = 0u; i < length; ++i) {
        crc ^= (uint16_t)data[i] << 8u;
        for (uint8_t bit = 0u; bit < 8u; ++bit) {
            crc = (crc & 0x8000u) != 0u ?
                (uint16_t)((crc << 1u) ^ 0x1021u) : (uint16_t)(crc << 1u);
        }
    }
    return crc;
}

static bool decode(const uint8_t *in, uint8_t length, uint8_t *out,
                   uint8_t *out_len)
{
    uint8_t r = 0u, w = 0u;
    while (r < length) {
        const uint8_t code = in[r++];
        if (code == 0u || (uint16_t)r + code - 1u > length) return false;
        for (uint8_t i = 1u; i < code; ++i) {
            if (w >= MAX_RAW) return false;
            out[w++] = in[r++];
        }
        if (code != 0xFFu && r < length) {
            if (w >= MAX_RAW) return false;
            out[w++] = 0u;
        }
    }
    *out_len = w;
    return true;
}

static uint8_t encode(const uint8_t *in, uint8_t length, uint8_t *out)
{
    uint8_t r = 0u, w = 1u, code_pos = 0u, code = 1u;
    while (r < length) {
        if (in[r] == 0u) {
            out[code_pos] = code;
            code_pos = w++;
            code = 1u;
            ++r;
        } else {
            out[w++] = in[r++];
            if (++code == 0xFFu) {
                out[code_pos] = code;
                code_pos = w++;
                code = 1u;
            }
        }
    }
    out[code_pos] = code;
    out[w++] = 0u;
    return w;
}

static void send_bytes(const uint8_t *data, uint8_t length)
{
    for (uint8_t i = 0u; i < length; ++i) {
        while ((USART_ISR & (1u << 7u)) == 0u) { }
        USART_TDR = data[i];
    }
}

static void respond(uint16_t id, uint16_t opcode, const uint8_t *payload,
                    uint16_t length)
{
    const uint16_t raw_len = (uint16_t)(8u + length);
    tx_raw[0] = VERSION;
    tx_raw[1] = 2u;
    put16(&tx_raw[2], id);
    put16(&tx_raw[4], opcode);
    put16(&tx_raw[6], length);
    for (uint16_t i = 0u; i < length; ++i) tx_raw[8u + i] = payload[i];
    put16(&tx_raw[raw_len], crc16(tx_raw, raw_len));
    send_bytes(tx, encode(tx_raw, (uint8_t)(raw_len + 2u), tx));
}

static void handle(uint8_t *f, uint8_t n)
{
    if (n < 10u || f[0] != VERSION || f[1] != 1u) return;
    const uint16_t plen = get16(&f[6]);
    if (plen > MAX_PAYLOAD || n != (uint8_t)(10u + plen) ||
        get16(&f[8u + plen]) != crc16(f, (uint16_t)(8u + plen))) return;
    const uint16_t id = get16(&f[2]), op = get16(&f[4]);
    uint8_t p[MAX_PAYLOAD] = {0u};
    uint16_t out = 1u;
    if (op == OP_HELLO && plen == 0u) {
        put16(&p[1], 1u); p[3] = VERSION; put16(&p[4], MAX_PAYLOAD);
        put32(&p[6], (1u << 0u) | (1u << 5u));
        out = 10u;
    } else if (op == OP_STATUS && plen == 0u) {
        put32(&p[1], board_uptime_seconds_get());
        out = 47u;
    } else if (op == OP_LOG_INFO && plen == 0u) {
        const spi_nor_jedec_id_t jedec = spi_nor_jedec_id();
        p[1] = 1u; /* SPI selected; all log counts remain zero. */
        p[15] = (uint8_t)(spi_nor_size_bytes() / (1024u * 1024u));
        p[16] = spi_nor_is_ready() ? 1u : 0u;
        p[17] = jedec.manufacturer; p[18] = jedec.memory_type;
        p[19] = jedec.capacity; p[20] = 7u; /* RTC not initialized here. */
        p[21] = 0u; put32(&p[22], board_uptime_seconds_get());
        p[26] = 2u; p[27] = 1u; p[29] = 0u;
        out = 30u;
    } else if (op == OP_LOG_PAGE && plen == 18u) {
        p[1] = f[8]; p[2] = 0u; p[3] = 0u;
        for (uint8_t i = 0u; i < 8u; ++i) p[4u + i] = f[14u + i];
        out = 12u;
    } else if (op == OP_SPI_DUMP && plen == 5u) {
        const uint32_t address = get32(&f[8]);
        const uint8_t amount = f[12];
        if (spi_nor_is_ready() && amount != 0u && amount <= DUMP_CHUNK &&
            address <= spi_nor_size_bytes() &&
            amount <= spi_nor_size_bytes() - address &&
            spi_nor_read(address, &p[6], amount)) {
            put32(&p[1], address); p[5] = amount;
            out = (uint16_t)(6u + amount);
        } else p[0] = 2u;
    } else if (op == OP_SPI_PROGRAM_PAGE && plen >= 5u) {
        const uint32_t address = get32(&f[8]);
        const uint8_t amount = f[12];
        if (amount == 0u || amount > 128u || plen != (uint16_t)(5u + amount) ||
            !spi_nor_program_page(address, &f[13], amount)) p[0] = 2u;
    } else if (op == OP_SPI_ERASE_SECTOR && plen == 4u) {
        if (!spi_nor_erase_sector(get32(&f[8]))) p[0] = 2u;
    } else {
        p[0] = 1u;
    }
    respond(id, op, p, out);
}

void protocol_v1_transport_init(void)
{
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
    head = tail = encoded_len = 0u;
    discard_frame = false;
    USART_CR1 = (1u << 5u) | (1u << 3u) | (1u << 2u) | 1u;
    NVIC_ISER0 = 1u << 27u;
}

void protocol_v1_service(void)
{
    while (tail != head) {
        const uint8_t b = rx[tail++];
        if (b == 0u) {
            if (!discard_frame && encoded_len != 0u) {
                uint8_t n = 0u;
                if (decode(encoded, encoded_len, encoded, &n)) handle(encoded, n);
            }
            encoded_len = 0u;
            discard_frame = false;
        } else if (!discard_frame) {
            if (encoded_len < sizeof(encoded)) encoded[encoded_len++] = b;
            else { encoded_len = 0u; discard_frame = true; }
        }
    }
}

void USART1_IRQHandler(void)
{
    const uint32_t status = USART_ISR;
    if ((status & (1u << 5u)) != 0u) {
        const uint8_t b = (uint8_t)USART_RDR;
        const uint8_t next = (uint8_t)(head + 1u);
        if (next != tail) { rx[head] = b; head = next; }
    }
    if ((status & 0x0Fu) != 0u) USART_ICR = status & 0x0Fu;
}
