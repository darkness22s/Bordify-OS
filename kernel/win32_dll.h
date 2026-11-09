#ifndef BORDIFY_WIN32_DLL_H
#define BORDIFY_WIN32_DLL_H
#include "types.h"
#include "win32_pe_dll.h"
#include "win32_vm.h"

/* Virtual kernel32/msvcrt handles and bounded PE32 file-backed DLLs.
 * No full NT loader, TLS/DllMain, GUI, general DLL search or OS thread ABI.
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
#define WIN32_DLL_MAX_IMAGES 4u
typedef struct {
    uint32_t handle;
    uint16_t refs;            /* explicit LoadLibrary references */
    uint16_t dep_refs;        /* strong references from importing DLLs */
    uint8_t state;            /* 0=empty, 1=loading, 2=ready */
    uint8_t image_deps;       /* bit set for each real imported DLL */
    uint8_t builtin_deps;     /* virtual DLL import mask */
    char name[13];
    win32_pe_dll_info_t info;
} win32_dll_image_t;
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
    uint16_t builtin_dep_refs[2];
    win32_dll_image_t images[WIN32_DLL_MAX_IMAGES];
} win32_dll_t;
void win32_dll_init(win32_dll_t *dll,const uint8_t *guest,
                    uint32_t base,size_t bytes);
/* Initialize pin state from the validated PE import descriptors. */
void win32_dll_pin_imports(win32_dll_t *dll,uint32_t mask);
/* Map up to four bounded PE32 DLLs from whitelisted FAT16 8.3 basenames.
 * Imports can target kernel32, msvcrt, and other supported mapped DLLs.
 * Unresolved imports, missing exports and dependency cycles fail closed.
 * Exported DLL code executes only in ring 3; no DllMain is called. */
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
