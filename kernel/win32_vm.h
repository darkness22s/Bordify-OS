#ifndef BORDIFY_WIN32_VM_H
#define BORDIFY_WIN32_VM_H
#include "types.h"

/* Experimental Win32 virtual allocations from a dedicated 128-KiB guest
 * pool. No user-visible mappings outside the existing ring-3 page table.
 * All allocations are 4-KiB aligned and contiguous; at most 32 runs.
 * MEM_COMMIT|MEM_RESERVE and PAGE_READWRITE only. No page protection API,
 * shared memory, executable memory, address hints, large pages or MEM_RESET.
 */
#define WIN32_VM_START (1536u * 1024u)
#define WIN32_VM_END (1664u * 1024u)
#define WIN32_VM_PAGE_SIZE 4096u
#define WIN32_VM_PAGE_COUNT ((WIN32_VM_END-WIN32_VM_START)/WIN32_VM_PAGE_SIZE)
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
    uint32_t used_pages; /* bit i represents allocated 4-KiB page */
    uint32_t module_pages; /* pages reserved for loaded DLL code */
    uint8_t runs[WIN32_VM_PAGE_COUNT]; /* first page stores length in pages */
    uint32_t last_error;
} win32_vm_t;
void win32_vm_init(win32_vm_t *vm,uint8_t *guest,uint32_t base,size_t bytes);
/* VirtualAlloc(NULL, size, MEM_COMMIT|MEM_RESERVE, PAGE_READWRITE). */
uint32_t win32_vm_alloc(win32_vm_t *vm,uint32_t addr,uint32_t size,
                        uint32_t flags,uint32_t protect);
/* VirtualFree(base, 0, MEM_RELEASE). Returns BOOL. */
uint32_t win32_vm_free(win32_vm_t *vm,uint32_t addr,uint32_t size,
                       uint32_t flags);
/* A DLL-owned run cannot be released by guest VirtualFree; only the DLL
 * manager can release it when its final LoadLibrary ref disappears. */
uint32_t win32_vm_alloc_module(win32_vm_t *vm,uint32_t size);
uint32_t win32_vm_free_module(win32_vm_t *vm,uint32_t addr);
#endif
