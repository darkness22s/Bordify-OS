#ifndef BORDIFY_WIN32_CRT_H
#define BORDIFY_WIN32_CRT_H
#include "types.h"

/* A tested subset of the MSVCRT x86 cdecl ABI, not full CRT startup.
 * Heap is explicitly below the stack reservation and outside the PE image.
 * Every service validates guest addresses and never follows kernel pointers.
 */
#define WIN32_CRT_HEAP_START (1728u * 1024u)
#define WIN32_CRT_HEAP_END (1984u * 1024u)
#define WIN32_CRT_MAX_ALLOCS 64u
#define WIN32_CRT_MAX_STRING (64u * 1024u)

typedef struct {
    uint32_t offset;
    uint32_t bytes;
    bool live;
} win32_crt_allocation_t;
typedef struct {
    uint8_t *memory;
    uint32_t base;
    size_t extent;
    uint32_t next;
    uint32_t last_error;
    win32_crt_allocation_t allocations[WIN32_CRT_MAX_ALLOCS];
} win32_crt_t;
void win32_crt_init(win32_crt_t *rt,uint8_t *memory,uint32_t base,size_t extent);
/* Services 10..20: strlen, memcpy, memset, memcmp, strcmp, strncmp,
 * malloc, free, calloc, memmove and realloc respectively. */
uint32_t win32_crt_call(win32_crt_t *rt,uint32_t service,
                        uint32_t arg1,uint32_t arg2,uint32_t arg3);
#endif
