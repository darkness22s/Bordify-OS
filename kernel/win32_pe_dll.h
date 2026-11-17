#ifndef BORDIFY_WIN32_PE_DLL_H
#define BORDIFY_WIN32_PE_DLL_H
#include "types.h"

/* A bounded real PE32 DLL image mapper. This first slice accepts only
 * bounded i386 DLLs without TLS, delay-load, or forwarded exports.
 * Validated DllMain addresses may only execute in ring 3. All returned function
 * addresses must lie in executable sections of this mapped guest image.
 */
#define WIN32_PE_DLL_MAX_BYTES (128u * 1024u)
typedef struct {
    uint32_t image_size;
    uint32_t guest_base;
    uint32_t export_rva;
    uint32_t export_size;
    uint32_t import_rva;
    uint32_t import_size;
    uint32_t entry_rva; /* optional PE32 DllMain, executable section only */
} win32_pe_dll_info_t;
uint32_t win32_pe_dll_image_size(const uint8_t *file,size_t file_size);
bool win32_pe_dll_stage(const uint8_t *file,size_t file_size,
                        uint8_t *destination,size_t capacity,
                        uint32_t guest_base,win32_pe_dll_info_t *out);
/* name is a NUL-terminated ASCII export, or NULL for an ordinal. */
/* Validate and bind PE32 import descriptors and IAT entries in an already
 * staged ring-3 image. The callback must return guest-code addresses only.
 * This binder never invokes DllMain/TLS itself; the caller must not
 * expose an image after an unsuccessful import binding. */
typedef uint32_t (*win32_pe_dll_import_lookup_t)(void *context,
    const char *dll_name,const char *symbol,uint32_t ordinal);
bool win32_pe_dll_bind_imports(uint8_t *memory,
    const win32_pe_dll_info_t *info,
    win32_pe_dll_import_lookup_t lookup,void *context);
uint32_t win32_pe_dll_resolve(const uint8_t *memory,
                             const win32_pe_dll_info_t *info,
                             const char *name,uint32_t ordinal);
#endif
