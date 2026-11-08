#include <assert.h>
#include <stdio.h>
#include "exec_inspect.h"

static void clear(uint8_t *bytes, size_t length) {
    for (size_t i = 0; i < length; ++i) bytes[i] = 0;
}

static void le16(uint8_t *p, uint16_t value) {
    p[0] = (uint8_t)value; p[1] = (uint8_t)(value >> 8);
}

static void le32(uint8_t *p, uint32_t value) {
    for (int i = 0; i < 4; ++i) p[i] = (uint8_t)(value >> (i * 8));
}

static void test_pe(void) {
    uint8_t b[512]; exec_info_t info;
    clear(b, sizeof(b));
    b[0] = 'M'; b[1] = 'Z'; le32(b + 0x3c, 0x80);
    b[0x80] = 'P'; b[0x81] = 'E';
    le16(b + 0x84, 0x014c); le16(b + 0x86, 2);
    le16(b + 0x94, 0xe0); le16(b + 0x98, 0x010b);
    assert(exec_inspect(b, sizeof(b), &info));
    assert(info.valid && info.format == EXEC_FORMAT_PE32 && info.arch == EXEC_ARCH_X86);
    le16(b + 0x84, 0x8664);
    le16(b + 0x94, 0xf0); le16(b + 0x98, 0x020b);
    assert(exec_inspect(b, sizeof(b), &info));
    assert(info.valid && info.format == EXEC_FORMAT_PE64 && info.arch == EXEC_ARCH_X86_64);
    assert(exec_inspect(b, 0x90, &info) && !info.valid);
    le32(b + 0x3c, 0xffffffffu);
    assert(exec_inspect(b, sizeof(b), &info) && !info.valid);
    clear(b, sizeof(b)); b[0] = 'M'; b[1] = 'Z';
    assert(exec_inspect(b, sizeof(b), &info) && info.format == EXEC_FORMAT_DOS);
    assert(exec_inspect(b, 2, &info) && !info.valid);
}

static void test_macho(void) {
    uint8_t b[128]; exec_info_t info;
    clear(b, sizeof(b));
    /* Thin x86_64, little-endian, MH_EXECUTE with one load command. */
    b[0] = 0xcf; b[1] = 0xfa; b[2] = 0xed; b[3] = 0xfe;
    le32(b + 4, 0x01000007); le32(b + 12, 2);
    le32(b + 16, 1); le32(b + 20, 72);
    assert(exec_inspect(b, sizeof(b), &info));
    assert(info.valid && info.format == EXEC_FORMAT_MACHO64 && info.arch == EXEC_ARCH_X86_64);
    assert(exec_inspect(b, 16, &info) && !info.valid);
    clear(b, sizeof(b));
    /* Apple universal Mach-O, two architecture entries. */
    b[0] = 0xca; b[1] = 0xfe; b[2] = 0xba; b[3] = 0xbe;
    b[7] = 2;
    assert(exec_inspect(b, sizeof(b), &info));
    assert(info.valid && info.format == EXEC_FORMAT_MACHO_FAT && info.arch == EXEC_ARCH_UNIVERSAL);
    assert(exec_inspect(b, 28, &info) && !info.valid);
}

static void test_elf_and_unknown(void) {
    uint8_t b[64]; exec_info_t info;
    clear(b, sizeof(b));
    assert(!exec_inspect(b, sizeof(b), &info));
    assert(!exec_inspect(NULL, 0, &info));
    assert(!exec_inspect(b, sizeof(b), NULL));
    b[0] = 0x7f; b[1] = 'E'; b[2] = 'L'; b[3] = 'F';
    b[4] = 2; b[5] = 1; le16(b + 18, 183);
    assert(exec_inspect(b, sizeof(b), &info));
    assert(info.valid && info.format == EXEC_FORMAT_ELF64 && info.arch == EXEC_ARCH_ARM64);
    assert(exec_inspect(b, 4, &info) && !info.valid);
}

int main(void) {
    test_pe();
    test_macho();
    test_elf_and_unknown();
    puts("Bordify executable inspection tests: PASS");
    return 0;
}
