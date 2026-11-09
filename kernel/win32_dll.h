#ifndef BORDIFY_WIN32_DLL_H
#define BORDIFY_WIN32_DLL_H
#include "types.h"
#include "win32_pe_dll.h"
#include "win32_vm.h"

/* Virtual kernel32/msvcrt handles and bounded PE32 file-backed DLLs.
 * No full NT loader, TLS, GUI, general DLL search or OS thread ABI.
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
    uint8_t exe_image_deps;
    uint8_t exe_builtin_deps;
    bool binding_exe; /* allow entrypoints only in static-import startup */
} win32_dll_t;
void win32_dll_init(win32_dll_t *dll,const uint8_t *guest,
                    uint32_t base,size_t bytes);
/* Initialize pin state from the validated PE import descriptors. */
void win32_dll_pin_imports(win32_dll_t *dll,uint32_t mask);
/* Resolve the static import table of a staged PE32 executable.
 * The image MUST NOT enter ring 3 until this returns true. */
bool win32_dll_bind_exe_imports(win32_dll_t *dll,uint32_t image_size,
                                uint32_t import_rva,uint32_t import_size,
                                uint32_t *bound_count);
void win32_dll_release_exe_imports(win32_dll_t *dll);
/* Order real DLL entrypoints after their dependencies. The returned
 * addresses are untrusted GUEST EIPs, never kernel call targets. */
bool win32_dll_attach_plan(const win32_dll_t *dll,
                           uint32_t handles[WIN32_DLL_MAX_IMAGES],
                           uint32_t entries[WIN32_DLL_MAX_IMAGES],
                           uint32_t *count);
/* Map up to four bounded PE32 DLLs from whitelisted FAT16 8.3 basenames.
 * Imports can target kernel32, msvcrt, and other supported mapped DLLs.
 * Unresolved imports, missing exports and dependency cycles fail closed.
 * Startup-import DLL entrypoints may execute in ring 3; dynamic-load
 * entrypoints are rejected until the interrupted guest call gate exists. */
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
