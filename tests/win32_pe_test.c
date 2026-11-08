#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "win32_pe.h"

static uint32_t u32(const unsigned char *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}
static void p32(unsigned char *p, uint32_t v) {
    for (int i = 0; i < 4; ++i) p[i] = (unsigned char)(v >> (8 * i));
}
int main(void) {
    FILE *f = fopen("build/apps/winhello.exe", "rb");
    assert(f);
    unsigned char image[4096] = {0};
    size_t n = fread(image, 1, sizeof(image), f);
    fclose(f);
    assert(n == 0x600);
    unsigned char *memory = calloc(1, 256 * 1024);
    assert(memory);
    win32_pe_info_t result;
    assert(win32_pe_stage(image, n, memory, 256 * 1024, &result));
    assert(result.entry == WIN32_PE_USER_BASE + 0x1000);
    assert(result.image_size == 0x2000 && result.imported_functions == 3);
    assert(u32(memory + 0x1280) == WIN32_PE_USER_BASE +
           WIN32_PE_RUNTIME_OFFSET + 2 * 64);
    assert(u32(memory + 0x1284) == WIN32_PE_USER_BASE +
           WIN32_PE_RUNTIME_OFFSET + 64);
    assert(u32(memory + 0x1288) == WIN32_PE_USER_BASE +
           WIN32_PE_RUNTIME_OFFSET);
    assert(win32_pe_install_bridges(memory, 256 * 1024));
    assert(memory[WIN32_PE_RUNTIME_OFFSET] == 0x8b);
    assert(memory[WIN32_PE_RUNTIME_OFFSET + 2 * 64] == 0x53);

    assert(!win32_pe_stage(image, 128, memory, 256 * 1024, &result));
    image[0x84] = 0x64; image[0x85] = 0x86; /* PE64 machine */
    assert(!win32_pe_stage(image, n, memory, 256 * 1024, &result));
    image[0x84] = 0x4c; image[0x85] = 0x01;
    p32(image + 0x80 + 24 + 28, 0x00400000u); /* unsupported relocation */
    assert(!win32_pe_stage(image, n, memory, 256 * 1024, &result));
    p32(image + 0x80 + 24 + 28, WIN32_PE_USER_BASE);
    /* Reject unknown Win32 imports rather than jumping to kernel addresses. */
    image[0x200 + (0x12a0 - 0x1000) + 2] = 'X';
    assert(!win32_pe_stage(image, n, memory, 256 * 1024, &result));
    free(memory);
    puts("PE32 loader and kernel32 bridge tests: PASS");
    return 0;
}
