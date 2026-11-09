#ifndef BORDIFY_WIN32_DLL_H
#define BORDIFY_WIN32_DLL_H
#include "types.h"
#include "win32_pe_dll.h"
#include "win32_vm.h"

/* Virtual built-in DLL module lookup. These handles name Bordify-provided
 * kernel32/msvcrt syscall stubs; they are NOT mapped Windows DLL binaries.
 * No DLL search path, relocation, TLS, DllMain or dynamic library loading.
 */
#define WIN32_DLL_KERNEL32_HANDLE 0x70000001u
#define WIN32_DLL_MSVCRT_HANDLE 0x70000002u
#define WIN32_DLL_ERROR_MOD_NOT_FOUND 126u
#define WIN32_DLL_ERROR_PROC_NOT_FOUND 127u
#define WIN32_DLL_ERROR_INVALID_HANDLE 6u
#define WIN32_DLL_ERROR_BAD_ARGUMENT 87u
#define WIN32_DLL_ERROR_BAD_EXE_FORMAT 193u
#define WIN32_DLL_ERROR_NOT_ENOUGH_MEMORY 8u
/* Reader must reject files exceeding capacity and return their exact length. */
typedef int (*win32_dll_reader_t)(const char *name,uint8_t *out,
                                   size_t capacity,void *context);
typedef struct {
    const uint8_t *guest;
    uint32_t base;
    size_t bytes;
    uint16_t refs[2];
    bool pinned[2]; /* DLLs loaded by the executable import table */
    uint32_t last_error;
    win32_vm_t *vm;
    win32_dll_reader_t reader;
    void *reader_context;
    uint32_t image_handle;
    uint16_t image_refs;
    char image_name[13];
    win32_pe_dll_info_t image_info;
} win32_dll_t;
void win32_dll_init(win32_dll_t *dll,const uint8_t *guest,
                    uint32_t base,size_t bytes);
/* Initialize pin state from the validated PE import descriptors. */
void win32_dll_pin_imports(win32_dll_t *dll,uint32_t mask);
/* Map one bounded, import-free real PE32 DLL from a whitelisted FAT16
 * basename. The mapped DLL's exported code executes only in user mode. */
void win32_dll_set_reader(win32_dll_t *dll,win32_vm_t *vm,
                          win32_dll_reader_t reader,void *context);
uint32_t win32_dll_load(win32_dll_t *dll,uint32_t guest_name);
uint32_t win32_dll_load_w(win32_dll_t *dll,uint32_t guest_name);
/* A NULL name returns the executable; named queries do not increment refs. */
uint32_t win32_dll_get_module(win32_dll_t *dll,uint32_t guest_name,bool wide);
uint32_t win32_dll_symbol(win32_dll_t *dll,uint32_t module,
                          uint32_t guest_symbol);
uint32_t win32_dll_free(win32_dll_t *dll,uint32_t module);
#endif
