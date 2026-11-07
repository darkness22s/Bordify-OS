#include "heap.h"
#include "string.h"
#include "printf.h"

static uintptr_t heap_base;
static uintptr_t heap_next;
static size_t heap_size;

void heap_init(uintptr_t start, size_t size) {
    heap_base = ALIGN_UP(start, 4096);
    heap_next = heap_base;
    heap_size = size;
    kprintf("[heap] %u KiB at 0x%08x\n", (uint32_t)(size / 1024), (uint32_t)heap_base);
}

void *kmalloc_aligned(size_t size, size_t alignment) {
    if (!alignment) {
        alignment = 8;
    }
    uintptr_t address = ALIGN_UP(heap_next, alignment);
    if (address + size < address || address + size > heap_base + heap_size) {
        panic("out of kernel heap (requested %u bytes)", (uint32_t)size);
    }
    heap_next = address + size;
    return (void *)address;
}

void *kmalloc(size_t size) {
    return kmalloc_aligned(size, 16);
}

void *kzalloc(size_t size) {
    void *ptr = kmalloc(size);
    kmemset(ptr, 0, size);
    return ptr;
}

size_t heap_used(void) {
    return (size_t)(heap_next - heap_base);
}

size_t heap_capacity(void) {
    return heap_size;
}
