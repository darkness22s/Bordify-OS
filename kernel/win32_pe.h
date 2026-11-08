#ifndef BORDIFY_WIN32_PE_H
#define BORDIFY_WIN32_PE_H
#include "types.h"

/*
 * First-stage Windows compatibility: standalone PE32 console loader.
 * Deliberately restricted to x86, a fixed image base and a tiny kernel32
 * import subset. It DOES NOT run PE32+ / x64 executables or installers.
 *
 * Memory supplied here must be a private, user-accessible mapping; the
 * loader only copies bytes and never transfers control to foreign code.
 */
#define WIN32_PE_USER_BASE 0x40000000u
#define WIN32_PE_MAX_IMAGE (192u * 1024u)
#define WIN32_PE_RUNTIME_OFFSET (196u * 1024u)

typedef struct {
    uint32_t entry;
    uint32_t image_size;
    uint32_t imported_functions;
} win32_pe_info_t;

bool win32_pe_stage(const uint8_t *file, size_t file_size,
                    uint8_t *memory, size_t memory_size,
                    win32_pe_info_t *info);
/* Installs ring-3 stdcall bridges; call after stage, before transfer. */
bool win32_pe_install_bridges(uint8_t *memory, size_t memory_size);

#endif
