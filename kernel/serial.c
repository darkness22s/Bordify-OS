#include "serial.h"
#include "io.h"

static bool serial_ready;

void serial_init(void) {
    outb(0x3F8 + 1, 0x00);
    outb(0x3F8 + 3, 0x80);
    outb(0x3F8 + 0, 0x03);
    outb(0x3F8 + 1, 0x00);
    outb(0x3F8 + 3, 0x03);
    outb(0x3F8 + 2, 0xC7);
    outb(0x3F8 + 4, 0x0B);
    serial_ready = true;
}

void serial_putc(char c) {
    if (!serial_ready) {
        return;
    }
    int tries = 100000;
    while (tries-- && !(inb(0x3F8 + 5) & 0x20)) {
    }
    outb(0x3F8, (uint8_t)c);
}

void serial_write(const char *s) {
    while (s && *s) {
        serial_putc(*s++);
    }
}
