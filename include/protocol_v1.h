#ifndef PROTOCOL_V1_H
#define PROTOCOL_V1_H

#include <stdbool.h>
#include <stdint.h>

#define PROTOCOL_V1_VERSION 1u
#define PROTOCOL_V1_MAX_PAYLOAD 262u

enum {
    PROTOCOL_V1_CAP_STATUS = 1UL << 0u,
    PROTOCOL_V1_CAP_GET_SETTINGS = 1UL << 1u,
    PROTOCOL_V1_CAP_SET_SETTING = 1UL << 2u,
    PROTOCOL_V1_CAP_LOG_INFO = 1UL << 3u,
    PROTOCOL_V1_CAP_LOG_PAGE = 1UL << 4u,
    PROTOCOL_V1_CAP_SPI_DUMP = 1UL << 5u,
    PROTOCOL_V1_CAP_SET_RTC = 1UL << 6u,
    PROTOCOL_V1_CAP_DEVICE_ACTION = 1UL << 7u,
    PROTOCOL_V1_CAP_SPI_WRITE = 1UL << 8u,
    PROTOCOL_V1_CAP_THEME_CONFIG = 1UL << 9u,
    PROTOCOL_V1_CAP_PRECISE_RTC = 1UL << 10u,
    PROTOCOL_V1_CAP_RATE_CPM_TENTHS = 1UL << 11u,
    PROTOCOL_V1_CAP_DEVICE_GRAPHS = 1UL << 12u,
    PROTOCOL_V1_CAP_DEVICE_DEBUG = 1UL << 13u,
    PROTOCOL_V1_CAP_FLASHLIGHT = 1UL << 14u,
    PROTOCOL_V1_CAP_INDICATION_MODES = 1UL << 15u,
    PROTOCOL_V1_CAP_RATE_ALARMS = 1UL << 16u,
    PROTOCOL_V1_CAP_DOSE_ALARMS = 1UL << 17u,
    PROTOCOL_V1_CAP_SETTING_MUTABILITY = 1UL << 18u
};

typedef struct {
    uint32_t uptime_seconds;
    uint32_t count_today;
    /* Live display rate in tenths of CPM, matching the LCD value. */
    uint32_t rate_cpm_tenths;
    uint32_t dose_nsv;
    uint16_t battery_raw;
    uint16_t solar_raw;
    uint16_t usb_raw;
    /* bit 0 HV, bit 1 USB, bits 2-3 indication mode, bits 4-15 B-START ADC */
    uint16_t flags;
    uint32_t count_total;
    uint16_t dose_fraction_3600;
    uint32_t dose_count_total;
    uint32_t screen_seconds;
    uint32_t sleep_seconds;
    uint32_t usb_seconds;
    uint32_t rate_sample_pulses;
    uint32_t hv_session_seconds;
} protocol_v1_status_t;

enum {
    PROTOCOL_V1_OP_HELLO = 1u,
    PROTOCOL_V1_OP_GET_STATUS = 2u,
    PROTOCOL_V1_OP_GET_SETTINGS = 3u,
    PROTOCOL_V1_OP_SET_SETTING = 4u,
    PROTOCOL_V1_OP_GET_LOG_INFO = 5u,
    PROTOCOL_V1_OP_GET_LOG_PAGE = 6u,
    PROTOCOL_V1_OP_GET_SPI_DUMP_PAGE = 7u,
    PROTOCOL_V1_OP_SET_RTC = 8u,
    PROTOCOL_V1_OP_DEVICE_ACTION = 9u,
    PROTOCOL_V1_OP_GET_THEME = 12u,
    PROTOCOL_V1_OP_SET_THEME = 13u
};

enum {
    PROTOCOL_V1_ACTION_RESET = 0u,
    PROTOCOL_V1_ACTION_POWER_OFF = 1u,
    PROTOCOL_V1_ACTION_REBOOT = 2u,
    PROTOCOL_V1_ACTION_WAKE_SCREEN = 3u,
    PROTOCOL_V1_ACTION_SET_INDICATION_MODE = 4u,
    PROTOCOL_V1_ACTION_SET_FLASHLIGHT = 5u,
    PROTOCOL_V1_ACTION_SET_SCREEN_ALWAYS_ON = 6u
};

void protocol_v1_transport_init(void);
void protocol_v1_set_status(const protocol_v1_status_t *status);
bool protocol_v1_service(void);

/* USART1 RX interrupt, routed from the startup vector table. */
void USART1_IRQHandler(void);

#endif
