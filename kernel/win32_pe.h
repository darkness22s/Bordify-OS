#ifndef BORDIFY_WIN32_PE_H
#define BORDIFY_WIN32_PE_H
#include "types.h"

/*
 * Experimental native x86 PE32 (ring-3) console loader with seven
 * kernel32 imports: ExitProcess, GetStdHandle, WriteFile, GetLastError,
 * CreateFileA, ReadFile and CloseHandle. The file-I/O bridge is bounded
 * to a single, fixed FAT16-backed path (see win32_files.h).
 * Additional tested process/timing helpers are provided for independent
 * C-compiled PE32 console workloads, plus a bounded MSVCRT cdecl subset
 * resolved from a second virtual DLL import table. This is NOT general
 * dynamic DLL loading, full CRT initialization, or complete Windows ABI.
 * No thread scheduler, external DLL mapping, GUI, TLS, registry, COM or
 * native Win64 runtime is implemented.
 * This is NOT Wine, general Win32 GUI support or a Win64 loader: the
 * unrelated PE32+ AMD64 interpreter lives in pe64_vm.c.
 *
 * Memory supplied here must be a private user-accessible mapping;
 * this loader stages and validates only and never executes guest code.
 */
#define WIN32_PE_USER_BASE 0x40000000u
#define WIN32_PE_MAX_IMAGE (1536u * 1024u)
#define WIN32_PE_RUNTIME_OFFSET (3584u * 1024u)
/* Keep all import stubs and process-owned strings outside mapped PE sections. */
#define WIN32_PE_BRIDGE_SLOT 64u
#define WIN32_PE_BRIDGE_COUNT 33u
#define WIN32_PE_CMDLINE_A_OFFSET (WIN32_PE_RUNTIME_OFFSET + 4096u)
#define WIN32_PE_CMDLINE_W_OFFSET (WIN32_PE_CMDLINE_A_OFFSET + 256u)
#define WIN32_PE_CMDLINE_LIMIT 128u

/* Stable failure codes make independent compiled Windows-app diagnostics
 * actionable without ever executing an unsafe or partially mapped image. */
enum {
    WIN32_PE_OK=0,
    WIN32_PE_BAD_ARGUMENT=1,
    WIN32_PE_BAD_DOS=2,
    WIN32_PE_BAD_NT=3,
    WIN32_PE_BAD_ARCH=4,
    WIN32_PE_BAD_SUBSYSTEM=5,
    WIN32_PE_RELOCS_STRIPPED=6,
    WIN32_PE_UNSUPPORTED_TLS=7,
    WIN32_PE_BAD_IMAGE_SIZE=8,
    WIN32_PE_BAD_SECTION_TABLE=9,
    WIN32_PE_BAD_SECTION=10,
    WIN32_PE_OVERLAPPING_SECTIONS=11,
    WIN32_PE_ENTRY_NOT_EXECUTABLE=12,
    WIN32_PE_BAD_RELOCATIONS=13,
    WIN32_PE_BAD_IMPORTS=14,
    WIN32_PE_UNSUPPORTED_DLL=15,
    WIN32_PE_UNSUPPORTED_API=16
};
typedef struct {
    uint32_t entry;
    uint32_t image_size;
    uint32_t imported_functions;
    uint32_t imported_module_mask; /* bit 0=kernel32, bit 1=msvcrt */
    uint32_t import_rva;
    uint32_t import_size;
    uint32_t reject_reason;
} win32_pe_info_t;
const char *win32_pe_reason_name(uint32_t reason);

bool win32_pe_stage(const uint8_t *file, size_t file_size,
                    uint8_t *memory, size_t memory_size,
                    win32_pe_info_t *info);
/* Maps the EXE but deliberately leaves its IAT unresolved until the
 * process-specific DLL manager is initialized. MUST bind before ring 3. */
bool win32_pe_stage_deferred(const uint8_t *file, size_t file_size,
                             uint8_t *memory, size_t memory_size,
                             win32_pe_info_t *info);
/* Installs ring-3 stdcall bridges; call after stage, before transfer. */
bool win32_pe_install_bridges(uint8_t *memory, size_t memory_size);
/* Initializes the process-owned ANSI/UTF-16 command-line strings. */
bool win32_pe_set_command_line(uint8_t *memory, size_t memory_size,
                               const char *exe_name);

#endif
