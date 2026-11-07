#include "pit.h"
#include "idt.h"
#include "io.h"

static volatile uint32_t ticks;

static void pit_tick(void) {
    ticks++;
}

void pit_init(void) {
    uint32_t divisor = 1193182u / 100u;
    outb(0x43, 0x36);
    outb(0x40, (uint8_t)(divisor & 0xFF));
    outb(0x40, (uint8_t)((divisor >> 8) & 0xFF));
    irq_register(0, pit_tick);
    irq_unmask(0);
}

uint32_t pit_ticks(void) {
    return ticks;
}

uint32_t pit_uptime_seconds(void) {
    return ticks / 100u;
}

void pit_sleep_ms(uint32_t ms) {
    uint32_t target = ticks + ms / 10u;
    if (target == ticks) {
        target++;
    }
    while ((int32_t)(ticks - target) < 0) {
        cpu_halt();
    }
}
