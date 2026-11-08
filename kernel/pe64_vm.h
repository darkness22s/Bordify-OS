#ifndef BORDIFY_PE64_VM_H
#define BORDIFY_PE64_VM_H
#include "types.h"
/* Bounded PE32+ x86-64 instruction interpreter. No native long mode,
 * installers, arbitrary PE execution or full Win32 runtime. */
#define PE64_GUEST_BASE 0x140000000ULL
#define PE64_GUEST_BYTES (64u * 1024u)
#define PE64_MAX_FILE (64u * 1024u)
#define PE64_MAX_STEPS 2048u
typedef void (*pe64_output_t)(const uint8_t *data, size_t len, void *ctx);
typedef struct {
    bool finished;
    int exit_code;
    uint32_t steps;
    uint32_t imports;
    uint32_t rejected_opcode;
    uint32_t installed_bytes;
} pe64_report_t;
/* Commit is invoked only for the audited destination C:\\APP64.EXE, after
 * CloseHandle; host applies its own filesystem permission policy. */
typedef bool (*pe64_install_commit_t)(const uint8_t *data, size_t size,
                                     void *ctx);
bool pe64_vm_run_install(const uint8_t *file, size_t length,
                         uint8_t *memory, size_t memory_bytes,
                         pe64_output_t output, void *context,
                         pe64_install_commit_t commit, void *commit_context,
                         pe64_report_t *report);
bool pe64_vm_run(const uint8_t *file, size_t length,
                 uint8_t *memory, size_t memory_bytes,
                 pe64_output_t output, void *context,
                 pe64_report_t *report);
#endif
