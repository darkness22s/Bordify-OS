#ifndef BORDIFY_WIN32_PE_H
#define BORDIFY_WIN32_PE_H
#include "types.h"

/*
 * Experimental native x86 PE32 (ring-3) console loader with seven
 * kernel32 imports: ExitProcess, GetStdHandle, WriteFile, GetLastError,
 * CreateFileA, ReadFile and CloseHandle. The file-I/O bridge is bounded
 * to a single, fixed FAT16-backed path (see win32_files.h).
 * This is NOT Wine, general Win32 GUI support or a Win64 loader: the
 * unrelated PE32+ AMD64 interpreter lives in pe64_vm.c.
 *
 * Memory supplied here must be a private user-accessible mapping;
 * this loader stages and validates only and never executes guest code.
 */
#define WIN32_PE_USER_BASE 0x40000000u
#define WIN32_PE_MAX_IMAGE (1536u * 1024u)
#define WIN32_PE_RUNTIME_OFFSET (1664u * 1024u)

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
