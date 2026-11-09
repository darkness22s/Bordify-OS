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
    unsigned char *memory = calloc(1, 2 * 1024 * 1024);
    assert(memory);
    win32_pe_info_t result;
    assert(win32_pe_stage(image, n, memory, 2 * 1024 * 1024, &result));
    assert(result.entry == WIN32_PE_USER_BASE + 0x1000);
    assert(result.image_size == 0x2000 && result.imported_functions == 3);
    assert(result.imported_module_mask == 1u);
    assert(u32(memory + 0x1280) == WIN32_PE_USER_BASE +
           WIN32_PE_RUNTIME_OFFSET + 2 * 64);
    assert(u32(memory + 0x1284) == WIN32_PE_USER_BASE +
           WIN32_PE_RUNTIME_OFFSET + 64);
    assert(u32(memory + 0x1288) == WIN32_PE_USER_BASE +
           WIN32_PE_RUNTIME_OFFSET);
    assert(win32_pe_install_bridges(memory, 2 * 1024 * 1024));
    assert(memory[WIN32_PE_RUNTIME_OFFSET] == 0x8b);
    assert(memory[WIN32_PE_RUNTIME_OFFSET + 2 * 64] == 0x53);
    /* Generic PE/COFF loader: zero OriginalFirstThunk legally means read
     * the import-name thunks from FirstThunk before filling the IAT. */
    p32(image+0x400u,0u);
    assert(win32_pe_stage(image,n,memory,2u*1024u*1024u,&result));
    assert(result.imported_functions==3u && result.reject_reason==WIN32_PE_OK);
    p32(image+0x400u,0x1260u);
    /* MinGW writes a directory Size including ILT/IAT/name data, not
     * merely the 2*20-byte import descriptor region. */
    p32(image+0x80u+24u+108u,0x300u);
    assert(win32_pe_stage(image,n,memory,2u*1024u*1024u,&result));
    assert(result.imported_functions==3u);
    p32(image+0x80u+24u+108u,40u);
    assert(win32_pe_stage(image,n,memory,2u*1024u*1024u,&result));

    /* Per-process strings live above the image in ring-3 mapped RAM. */
    assert(win32_pe_install_bridges(memory,2u*1024u*1024u));
    assert(win32_pe_set_command_line(memory,2u*1024u*1024u,"WINREAL.EXE"));
    assert(strcmp((const char *)(memory+WIN32_PE_CMDLINE_A_OFFSET),
                  "\"C:\\BORDIFY\\APPS\\WINREAL.EXE\"")==0);
    assert(memory[WIN32_PE_CMDLINE_W_OFFSET]==(uint8_t)'"' &&
           memory[WIN32_PE_CMDLINE_W_OFFSET+1u]==0);
    assert(!win32_pe_set_command_line(memory,2u*1024u*1024u,"Bad Name.exe"));
    assert(memory[WIN32_PE_RUNTIME_OFFSET+10u*64u+6u]==0xb8);
    assert(u32(memory+WIN32_PE_RUNTIME_OFFSET+10u*64u+7u)==26u);
    assert(u32(memory+WIN32_PE_RUNTIME_OFFSET+11u*64u+7u)==27u);
    assert(u32(memory+WIN32_PE_RUNTIME_OFFSET+32u*64u+7u)==28u);
    assert(u32(memory+WIN32_PE_RUNTIME_OFFSET+12u*64u+1u)==
           WIN32_PE_USER_BASE+WIN32_PE_CMDLINE_A_OFFSET);
    assert(strcmp(win32_pe_reason_name(WIN32_PE_UNSUPPORTED_API),
                  "unsupported imported API")==0);


    /* Section table at 0x178..0x1a0 must fit in SizeOfHeaders.
     * Input bytes existing beyond that declared boundary are not enough. */
    p32(image + 0x80 + 24 + 60, 0x190);
    assert(!win32_pe_stage(image,n,memory,2*1024*1024,&result));
    p32(image + 0x80 + 24 + 60, 0x200);
    assert(!win32_pe_stage(image, 128, memory, 2 * 1024 * 1024, &result));
    image[0x84] = 0x64; image[0x85] = 0x86; /* PE64 machine */
    assert(!win32_pe_stage(image, n, memory, 2 * 1024 * 1024, &result));
    image[0x84] = 0x4c; image[0x85] = 0x01;
    /* Genuine IMAGE_BASE_RELOCATION block: map 0x00400000-preferred
     * PE32 into Bordify's isolated 0x40000000 user page-table window. */
    const size_t opt=0x80u+24u;
    const size_t reloc=0x200u+(0x1350u-0x1000u);
    const size_t target=0x200u+(0x1380u-0x1000u);
    p32(image+opt+28,0x00400000u);
    assert(!win32_pe_stage(image,n,memory,2u*1024u*1024u,&result));
    p32(image+opt+96+5u*8u,0x1350u);
    p32(image+opt+96+5u*8u+4u,12u);
    p32(image+reloc,0x1000u);
    p32(image+reloc+4,12u);
    image[reloc+8]=0x80; image[reloc+9]=0x33; /* HIGHLOW RVA 0x1380 */
    image[reloc+10]=0; image[reloc+11]=0;       /* ABSOLUTE padding */
    p32(image+target,0x00401234u);
    assert(win32_pe_stage(image,n,memory,2u*1024u*1024u,&result));
    assert(u32(memory+0x1380u)==WIN32_PE_USER_BASE+0x1234u);
    p32(image+reloc+4,14u); /* block cannot overrun directory */
    assert(!win32_pe_stage(image,n,memory,2u*1024u*1024u,&result));
    p32(image+reloc+4,12u);
    image[reloc+9]=0x23; /* unsupported HIGH relocation */
    assert(!win32_pe_stage(image,n,memory,2u*1024u*1024u,&result));
    image[reloc+9]=0x33;
    p32(image+opt+28,WIN32_PE_USER_BASE);
    p32(image+opt+96+5u*8u,0);
    p32(image+opt+96+5u*8u+4u,0);
    /* Reject unknown Win32 imports rather than jumping to kernel addresses. */
    image[0x200 + (0x12a0 - 0x1000) + 2] = 'X';
    assert(!win32_pe_stage(image, n, memory, 2 * 1024 * 1024, &result));
    free(memory);
    puts("PE32 loader and kernel32 bridge tests: PASS");
    return 0;
}
