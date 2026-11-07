#ifndef BORDIFY_HEAP_H
#define BORDIFY_HEAP_H

#include "types.h"

void heap_init(uintptr_t start, size_t size);
void *kmalloc(size_t size);
void *kzalloc(size_t size);
void *kmalloc_aligned(size_t size, size_t alignment);
size_t heap_used(void);
size_t heap_capacity(void);

#endif
