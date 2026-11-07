#ifndef BORDIFY_PIT_H
#define BORDIFY_PIT_H

#include "types.h"

void pit_init(void);
uint32_t pit_ticks(void);
uint32_t pit_uptime_seconds(void);
void pit_sleep_ms(uint32_t ms);

#endif
