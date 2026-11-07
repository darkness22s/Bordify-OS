#ifndef BORDIFY_MB2_H
#define BORDIFY_MB2_H

#include "types.h"

typedef struct {
    uint32_t framebuffer_addr;
    uint32_t framebuffer_pitch;
    uint32_t framebuffer_width;
    uint32_t framebuffer_height;
    uint8_t framebuffer_bpp;
    uint8_t framebuffer_type;
    bool framebuffer_present;
    uint32_t mem_lower_kb;
    uint32_t mem_upper_kb;
    uint32_t memory_size_kb;
    const char *bootloader;
    const char *cmdline;
} mb2_info_t;

void mb2_parse(uintptr_t info_address);
const mb2_info_t *mb2_info(void);

#endif
