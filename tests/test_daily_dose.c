#include "board.h"
#include "daily_dose.h"
#include "dose_reconcile.h"
#include "log_history.h"
#include "log_writer.h"

#define CHECK(condition) do { if (!(condition)) return __LINE__; } while (0)

static uint8_t eeprom[2048];
static uint32_t writes;
static uint32_t day_time = 100u * 86400u;
static bool fault;

bool board_eeprom_read(uint16_t offset, uint8_t *value)
{
    if (offset >= sizeof(eeprom)) return false;
    *value = eeprom[offset];
    return true;
}

bool board_eeprom_write(uint16_t offset, uint8_t value)
{
    if (offset >= sizeof(eeprom)) return false;
    eeprom[offset] = value;
    ++writes;
    return true;
}

bool log_writer_fault(void) { return fault; }

void log_history_begin(log_cursor_t *cursor)
{
    cursor->skip = 0u;
}

bool log_history_next_from(uint8_t memory, uint8_t type, uint32_t period,
                           log_cursor_t *cursor, uint32_t limit,
                           bool include_live, log_point_t *point)
{
    (void)memory;
    (void)period;
    (void)limit;
    (void)include_live;
    if (cursor->skip++ != 0u) return false;
    *point = (log_point_t){
        .period_start = day_time,
        .count = type == LOG_RECORD_DAY ? 90000u : 18495u,
        .calendar_valid = true,
        .available = true
    };
    return true;
}

int test_entry(void)
{
    for (unsigned i = 0u; i < sizeof(eeprom); ++i) eeprom[i] = 0xFFu;
    daily_dose_init(100u, 18378u); /* 2.042 uSv, older than the stored logs. */
    daily_dose_set_total(108378u);
    CHECK(daily_dose_checkpoint());

    dose_reconcile_begin(BOARD_LOG_MEMORY_SPI, 100u, day_time);
    daily_dose_add(17u); /* The graph snapshot remains authoritative. */
    uint32_t day = 0u, total = 0u;
    dose_reconcile_result_t result;
    unsigned steps = 0u;
    do {
        result = dose_reconcile_step(BOARD_LOG_MEMORY_SPI, 100u, &day, &total);
        CHECK(++steps <= 8u);
    } while (result == DOSE_RECONCILE_PENDING);
    CHECK(result == DOSE_RECONCILE_COMPLETE);
    CHECK(day == 90000u);
    CHECK(total == 90000u);

    daily_dose_set_count(day);
    daily_dose_set_total(total);
    CHECK(daily_dose_count() == day); /* The old EEPROM value must not win. */
    CHECK(daily_dose_total() == total);
    CHECK(daily_dose_checkpoint());
    daily_dose_clear();
    daily_dose_set_day(100u, 0u); /* A reboot restores the corrected checkpoint. */
    CHECK(daily_dose_count() == day);
    CHECK(daily_dose_total() == total);

    const uint32_t saved_writes = writes;
    daily_dose_set_count(day);
    CHECK(daily_dose_checkpoint());
    CHECK(writes == saved_writes); /* Applying the same result needs no write. */

    daily_dose_set_count(0u); /* A cleared/new log set can legitimately be lower. */
    daily_dose_set_total(0u);
    CHECK(daily_dose_checkpoint());
    daily_dose_add(123u);
    daily_dose_set_day(100u, 999u);
    CHECK(daily_dose_count() == 0u);
    CHECK(daily_dose_total() == 0u);

    dose_reconcile_begin(BOARD_LOG_MEMORY_SPI, 100u, day_time);
    fault = true;
    CHECK(dose_reconcile_step(BOARD_LOG_MEMORY_SPI, 100u, &day, &total)
        == DOSE_RECONCILE_FAILED);
    CHECK(daily_dose_count() == 0u);
    return 0;
}
