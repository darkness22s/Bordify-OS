#include "idt.h"
#include "io.h"
#include "printf.h"
#include "string.h"
#include "process.h"

typedef struct {
    uint16_t offset_low;
    uint16_t selector;
    uint8_t zero;
    uint8_t type_attr;
    uint16_t offset_high;
} PACKED idt_entry_t;

typedef struct {
    uint16_t limit;
    uint32_t base;
} PACKED idt_pointer_t;

static idt_entry_t idt[256];
static idt_pointer_t idt_ptr;
static irq_handler_t irq_handlers[16];

extern void (*isr_stub_table[48])(void);
extern void isr128(void);

static void idt_set_gate(int vector, void (*handler)(void), uint8_t type_attr) {
    uint32_t address = (uint32_t)(uintptr_t)handler;
    idt[vector].offset_low = (uint16_t)(address & 0xFFFF);
    idt[vector].selector = 0x08;
    idt[vector].zero = 0;
    idt[vector].type_attr = type_attr;
    idt[vector].offset_high = (uint16_t)((address >> 16) & 0xFFFF);
}

void irq_register(int irq, irq_handler_t handler) {
    if (irq >= 0 && irq < 16) {
        irq_handlers[irq] = handler;
    }
}

static void pic_remap(void) {
    uint8_t master_mask = inb(0x21);
    uint8_t slave_mask = inb(0xA1);

    outb(0x20, 0x11);
    io_wait();
    outb(0xA0, 0x11);
    io_wait();
    outb(0x21, 0x20);
    io_wait();
    outb(0xA1, 0x28);
    io_wait();
    outb(0x21, 0x04);
    io_wait();
    outb(0xA1, 0x02);
    io_wait();
    outb(0x21, 0x01);
    io_wait();
    outb(0xA1, 0x01);
    io_wait();

    outb(0x21, master_mask);
    outb(0xA1, slave_mask);
}

void irq_unmask(int irq) {
    uint16_t port;
    uint8_t value;
    if (irq < 8) {
        port = 0x21;
        value = inb(port) & (uint8_t)~(1u << irq);
    } else {
        port = 0xA1;
        value = inb(port) & (uint8_t)~(1u << (irq - 8));
        outb(0x21, inb(0x21) & (uint8_t)~(1u << 2));
    }
    outb(port, value);
}

void irq_mask(int irq) {
    uint16_t port;
    uint8_t value;
    if (irq < 8) {
        port = 0x21;
        value = inb(port) | (uint8_t)(1u << irq);
    } else {
        port = 0xA1;
        value = inb(port) | (uint8_t)(1u << (irq - 8));
    }
    outb(port, value);
}

void interrupt_dispatch(uint32_t *frame) {
    uint32_t vector = frame[8];
    if (vector >= 32 && vector < 48) {
        int irq = (int)(vector - 32);
        if (irq_handlers[irq]) {
            irq_handlers[irq]();
        }
        if (irq >= 8) {
            outb(0xA0, 0x20);
        }
        outb(0x20, 0x20);
        return;
    }
    /* The int 0x80 gate is available to CPL3 only; all other handlers
       remain ring-0. User faults are contained rather than panicking OS. */
    if (vector == 128 || ((frame[11] & 3u) == 3u)) {
        process_handle_interrupt(frame);
        return;
    }
    uint32_t error = frame[9];
    uint32_t eip = frame[10];
    panic("CPU exception %u (error 0x%08x) at eip 0x%08x",
          vector, error, eip);
}

void idt_init(void) {
    kmemset(idt, 0, sizeof(idt));
    kmemset(irq_handlers, 0, sizeof(irq_handlers));
    idt_ptr.limit = (uint16_t)(sizeof(idt) - 1);
    idt_ptr.base = (uint32_t)(uintptr_t)&idt;

    for (int i = 0; i < 48; i++) {
        idt_set_gate(i, isr_stub_table[i], 0x8E);
    }

    idt_set_gate(128, isr128, 0xEE); /* DPL=3 software syscall */

    __asm__ volatile("lidt (%0)" : : "r"(&idt_ptr));
    pic_remap();
    outb(0x21, 0xFF);
    outb(0xA1, 0xFF);
}
