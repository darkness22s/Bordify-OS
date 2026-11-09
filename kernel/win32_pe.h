#ifndef BORDIFY_WIN32_PE_H
#define BORDIFY_WIN32_PE_H
#include "types.h"

/*
 * Experimental native x86 PE32 (ring-3) console loader with seven
 * kernel32 imports: ExitProcess, GetStdHandle, WriteFile, GetLastError,
 * CreateFileA, ReadFile and CloseHandle. The file-I/O bridge is bounded
 * to a single, fixed FAT16-backed path (see win32_files.h).
 * Additional tested process/timing helpers are provided for independent
 * C-compiled PE32 console workloads. No thread scheduler, dynamic DLLs,
 * GUI, heap, TLS, registry, COM or Win64 runtime is implemented.
 * This is NOT Wine, general Win32 GUI support or a Win64 loader: the
 * unrelated PE32+ AMD64 interpreter lives in pe64_vm.c.
 *
 * Memory supplied here must be a private user-accessible mapping;
 * this loader stages and validates only and never executes guest code.
 */
#define WIN32_PE_USER_BASE 0x40000000u
#define WIN32_PE_MAX_IMAGE (1536u * 1024u)
#define WIN32_PE_RUNTIME_OFFSET (1664u * 1024u)
/* Keep all import stubs and process-owned strings outside mapped PE sections. */
#define WIN32_PE_BRIDGE_SLOT 64u
#define WIN32_PE_BRIDGE_COUNT 16u
#define WIN32_PE_CMDLINE_A_OFFSET (WIN32_PE_RUNTIME_OFFSET + 4096u)
#define WIN32_PE_CMDLINE_W_OFFSET (WIN32_PE_CMDLINE_A_OFFSET + 256u)
#define WIN32_PE_CMDLINE_LIMIT 128u

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
/* Initializes the process-owned ANSI/UTF-16 command-line strings. */
bool win32_pe_set_command_line(uint8_t *memory, size_t memory_size,
                               const char *exe_name);

#endif
