#ifndef BORDIFY_EXEC_INSPECT_H
#define BORDIFY_EXEC_INSPECT_H

#include "types.h"

/* Format inspection only: this API never maps or executes untrusted code. */
typedef enum {
    EXEC_FORMAT_UNKNOWN = 0,
    EXEC_FORMAT_DOS,
    EXEC_FORMAT_PE32,
    EXEC_FORMAT_PE64,
    EXEC_FORMAT_MACHO32,
    EXEC_FORMAT_MACHO64,
    EXEC_FORMAT_MACHO_FAT,
    EXEC_FORMAT_ELF32,
    EXEC_FORMAT_ELF64
} exec_format_t;

typedef enum {
    EXEC_ARCH_UNKNOWN = 0,
    EXEC_ARCH_X86,
    EXEC_ARCH_X86_64,
    EXEC_ARCH_ARM,
    EXEC_ARCH_ARM64,
    EXEC_ARCH_UNIVERSAL
} exec_arch_t;

typedef struct {
    exec_format_t format;
    exec_arch_t arch;
    bool valid;
} exec_info_t;

/* Returns true for a recognized signature, even for a malformed header.
 * All accesses are bounded by size; the buffer is never modified. */
bool exec_inspect(const void *data, size_t size, exec_info_t *out);
const char *exec_format_name(exec_format_t format);
const char *exec_arch_name(exec_arch_t arch);
const char *exec_compatibility_note(const exec_info_t *info);

#endif
