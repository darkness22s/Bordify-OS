#ifndef BORDIFY_WIN32_VM_H
#define BORDIFY_WIN32_VM_H
#include "types.h"

/* Experimental contiguous Win32 allocations from a dedicated 2-MiB
 * pool inside a protected 4-MiB user mapping. This does not provide
 * Windows NT reserve/commit, VirtualProtect, demand paging or W^X.
 * Guest allocations remain bounded, 4-KiB aligned and zero-filled.
 */
#define WIN32_VM_START (1536u * 1024u)
#define WIN32_VM_END (3584u * 1024u)
#define WIN32_VM_PAGE_SIZE 4096u
#define WIN32_VM_PAGE_COUNT ((WIN32_VM_END-WIN32_VM_START)/WIN32_VM_PAGE_SIZE)
#define WIN32_VM_BITMAP_WORDS ((WIN32_VM_PAGE_COUNT+31u)/32u)
#define WIN32_VM_MEM_COMMIT 0x1000u
#define WIN32_VM_MEM_RESERVE 0x2000u
#define WIN32_VM_MEM_RELEASE 0x8000u
#define WIN32_VM_PAGE_READWRITE 0x04u
#define WIN32_VM_ERROR_INVALID_PARAMETER 87u
#define WIN32_VM_ERROR_NOT_ENOUGH_MEMORY 8u
typedef struct {
    uint8_t *guest;
    uint32_t base;
    size_t bytes;
    uint32_t used_pages[WIN32_VM_BITMAP_WORDS];
    uint32_t module_pages[WIN32_VM_BITMAP_WORDS];
    uint16_t runs[WIN32_VM_PAGE_COUNT]; /* first page stores run length */
    uint32_t last_error;
} win32_vm_t;
void win32_vm_init(win32_vm_t *vm,uint8_t *guest,uint32_t base,size_t bytes);
uint32_t win32_vm_alloc(win32_vm_t *vm,uint32_t addr,uint32_t size,
                        uint32_t flags,uint32_t protect);
uint32_t win32_vm_free(win32_vm_t *vm,uint32_t addr,uint32_t size,
                       uint32_t flags);
/* Dedicated DLL pages cannot be released by guest VirtualFree. */
uint32_t win32_vm_alloc_module(win32_vm_t *vm,uint32_t size);
uint32_t win32_vm_free_module(win32_vm_t *vm,uint32_t addr);
#endif
