#ifndef LOG_EEPROM_H
#define LOG_EEPROM_H

#include "log_record.h"
#include "log_query.h"

#include <stdbool.h>
#include <stdint.h>

bool log_eeprom_init(void);
bool log_eeprom_append(log_record_kind_t kind, uint16_t epoch, const log_record_t *record,
                       uint8_t year_descriptor);
uint8_t log_eeprom_count(log_record_kind_t kind);
uint8_t log_eeprom_capacity(log_record_kind_t kind);
bool log_eeprom_read(log_record_kind_t kind, uint8_t age,
                     log_record_t *record);
bool log_eeprom_query_read(void *context, log_record_kind_t kind,
                          uint16_t epoch, uint32_t period_start,
                          log_record_t *record, bool *live);
bool log_eeprom_segment_year(uint8_t segment_id, uint8_t *year_descriptor);
bool log_eeprom_active_epoch(uint16_t *epoch);
uint16_t log_eeprom_record_epoch(uint8_t segment);
bool log_eeprom_reset(bool keep_days);
bool log_eeprom_expire_alerts(uint8_t year);
bool log_eeprom_find(log_record_kind_t kind, uint16_t before_epoch,
                      uint32_t before_time, uint16_t skip, bool inclusive,
                      uint32_t date_limit, uint32_t reference, log_stored_t *item);

#endif
