#include "log_writer.h"

void test_feed_seconds(uint32_t seconds, uint32_t pulses, uint32_t cpm,
                       uint32_t start)
{
    for (uint32_t i = 1u; i <= seconds; ++i)
        log_writer_second(pulses, cpm, i, start + i, true);
}
