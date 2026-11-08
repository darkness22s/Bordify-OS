#include "gdt.h"
#include "types.h"
#include "string.h"

typedef struct {
    uint16_t limit_low;
    uint16_t base_low;
    uint8_t base_middle;
    uint8_t access;
    uint8_t granularity;
    uint8_t base_high;
} PACKED gdt_entry_t;

typedef struct {
    uint16_t limit;
    uint32_t base;
} PACKED gdt_pointer_t;

/* x86 selectors: 0x08 kernel CS, 0x10 kernel DS, 0x1B user CS,
 * 0x23 user DS, 0x28 TSS. The I/O bitmap is absent, denying ring-3 I/O. */
static gdt_entry_t gdt[6];
static gdt_pointer_t gdt_ptr;

typedef struct {
    uint32_t prev, esp0, ss0, esp1, ss1, esp2, ss2;
    uint32_t cr3, eip, eflags, eax, ecx, edx, ebx, esp, ebp, esi, edi;
    uint32_t es, cs, ss, ds, fs, gs, ldt;
    uint16_t trap, iomap;
} PACKED tss_t;

static tss_t tss;
static uint8_t syscall_kernel_stack[16384] __attribute__((aligned(16)));

static void gdt_set_gate(int index, uint32_t base, uint32_t limit, uint8_t access, uint8_t granularity) {
    gdt[index].base_low = (uint16_t)(base & 0xFFFF);
    gdt[index].base_middle = (uint8_t)((base >> 16) & 0xFF);
    gdt[index].base_high = (uint8_t)((base >> 24) & 0xFF);
    gdt[index].limit_low = (uint16_t)(limit & 0xFFFF);
    gdt[index].granularity = (uint8_t)((limit >> 16) & 0x0F);
    gdt[index].granularity |= (uint8_t)(granularity & 0xF0);
    gdt[index].access = access;
}

void gdt_init(void) {
    gdt_ptr.limit = (uint16_t)(sizeof(gdt) - 1);
    gdt_ptr.base = (uint32_t)(uintptr_t)&gdt;
    gdt_set_gate(0, 0, 0, 0, 0);
    gdt_set_gate(1, 0, 0xFFFFF, 0x9A, 0xCF);
    gdt_set_gate(2, 0, 0xFFFFF, 0x92, 0xCF);
    gdt_set_gate(3, 0, 0xFFFFF, 0xFA, 0xCF); /* user 32-bit code */
    gdt_set_gate(4, 0, 0xFFFFF, 0xF2, 0xCF); /* user 32-bit data */
    kmemset(&tss, 0, sizeof(tss));
    tss.ss0 = 0x10;
    tss.esp0 = (uint32_t)(uintptr_t)(syscall_kernel_stack +
                                      sizeof(syscall_kernel_stack));
    tss.iomap = sizeof(tss); /* no ring-3 port access */
    gdt_set_gate(5, (uint32_t)(uintptr_t)&tss, sizeof(tss) - 1, 0x89, 0x00);
    __asm__ volatile("lgdt (%0)" : : "r"(&gdt_ptr));
    __asm__ volatile(
        "ljmp $0x08, $1f\n"
        "1:\n"
        "mov $0x10, %%ax\n"
        "mov %%ax, %%ds\n"
        "mov %%ax, %%es\n"
        "mov %%ax, %%fs\n"
        "mov %%ax, %%gs\n"
        "mov %%ax, %%ss\n"
        : : : "eax", "memory");
    uint16_t tss_selector = 0x28;
    __asm__ volatile("ltr %0" : : "r"(tss_selector));
}
