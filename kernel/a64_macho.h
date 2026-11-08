#ifndef BORDIFY_A64_MACHO_H
#define BORDIFY_A64_MACHO_H

#include "types.h"

/* Interpreter for a deliberately tiny ARM64 Mach-O subset.
 * There is NO Darwin runtime, dyld, Objective-C, Cocoa, or x64 execution.
 * Guest bytes never execute directly on the host CPU.
 */
#define A64_GUEST_BASE 0x100000000ULL
#define A64_GUEST_BYTES (64u * 1024u)
#define A64_MACHO_MAX_FILE 4096u
#define A64_MAX_STEPS 1024u

typedef void (*a64_output_t)(const uint8_t *bytes, size_t count, void *context);
typedef struct {
    bool finished;
    int exit_code;
    uint32_t steps;
    uint32_t unsupported_opcode;
} a64_report_t;

bool a64_macho_run(const uint8_t *file, size_t file_size,
                   uint8_t *guest, size_t guest_size,
                   a64_output_t output, void *context,
                   a64_report_t *report);
#endif
