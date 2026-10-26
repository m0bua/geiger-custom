#ifndef LOG_EEPROM_H
#define LOG_EEPROM_H

#include "log_record.h"
#include "log_query.h"

#include <stdbool.h>
#include <stdint.h>

bool log_eeprom_init(void);
bool log_eeprom_append(log_record_kind_t kind, const log_record_t *record,
                       uint8_t year_descriptor);
uint8_t log_eeprom_count(log_record_kind_t kind);
uint8_t log_eeprom_capacity(log_record_kind_t kind);
bool log_eeprom_read(log_record_kind_t kind, uint8_t age,
                     log_record_t *record);
bool log_eeprom_query_read(void *context, log_record_kind_t kind,
                          uint16_t epoch, uint32_t period_start,
                          log_record_t *record, bool *live);
bool log_eeprom_segment_year(uint8_t segment_id, uint8_t *year_descriptor);
bool log_eeprom_active_segment(uint8_t *segment_id);

#endif
