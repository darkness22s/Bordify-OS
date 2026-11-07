#ifndef BORDIFY_IDT_H
#define BORDIFY_IDT_H

#include "types.h"

typedef void (*irq_handler_t)(void);

void idt_init(void);
void irq_register(int irq, irq_handler_t handler);
void irq_unmask(int irq);
void irq_mask(int irq);

#endif
