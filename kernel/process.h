#ifndef BORDIFY_PROCESS_H
#define BORDIFY_PROCESS_H

#include "types.h"

#define PROCESS_USER_BASE 0x40000000u
#define PROCESS_USER_BYTES (256u * 1024u)

/*
 * Stage-1 x86 protected-process execution. This is a native Bordify ELF32
 * ABI, NOT Windows PE64 or Apple Mach-O execution.
 */
void process_init(void);
int process_run_elf32(const char *directory, const char *filename);
int process_run_win32_pe(const char *directory, const char *filename);

/* Called from interrupt_dispatch for user exceptions and int 0x80. */
void process_handle_interrupt(uint32_t *frame);

#endif
