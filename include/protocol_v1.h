#ifndef PROTOCOL_V1_H
#define PROTOCOL_V1_H

#include <stdint.h>

#define PROTOCOL_V1_VERSION 1u
#define PROTOCOL_V1_MAX_PAYLOAD 64u

typedef struct {
    uint32_t uptime_seconds;
    uint32_t count_today;
    uint32_t rate_cpm;
    uint32_t dose_nsv;
    uint16_t battery_raw;
    uint16_t solar_raw;
    uint16_t usb_raw;
    uint16_t flags;
    uint32_t count_total;
} protocol_v1_status_t;

enum {
    PROTOCOL_V1_OP_HELLO = 1u,
    PROTOCOL_V1_OP_GET_STATUS = 2u,
    PROTOCOL_V1_OP_GET_SETTINGS = 3u,
    PROTOCOL_V1_OP_SET_SETTING = 4u,
    PROTOCOL_V1_OP_GET_LOG_INFO = 5u,
    PROTOCOL_V1_OP_GET_LOG_PAGE = 6u,
    PROTOCOL_V1_OP_GET_SPI_DUMP_PAGE = 7u
};

void protocol_v1_transport_init(void);
void protocol_v1_set_status(const protocol_v1_status_t *status);
void protocol_v1_service(void);

/* USART1 RX interrupt, routed from the startup vector table. */
void USART1_IRQHandler(void);

#endif
