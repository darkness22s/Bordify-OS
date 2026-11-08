#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "a64_macho.h"

static uint8_t memory[A64_GUEST_BYTES];
static uint8_t output_buffer[1024];
static size_t output_length;

static void callback(const uint8_t *message, size_t count, void *context) {
    (void)context;
    assert(count <= sizeof(output_buffer) - output_length);
    memcpy(output_buffer + output_length, message, count);
    output_length += count;
}
static void put32(uint8_t *data, uint32_t value) {
    for (int i = 0; i < 4; ++i) data[i] = (uint8_t)(value >> (8 * i));
}

int main(void) {
    FILE *fp = fopen("build/apps/armhello.mch", "rb");
    assert(fp);
    uint8_t file[A64_MACHO_MAX_FILE] = {0};
    size_t size = fread(file, 1, sizeof(file), fp);
    fclose(fp);
    assert(size == 512);
    a64_report_t report;
    assert(a64_macho_run(file, size, memory, sizeof(memory),
                         callback, NULL, &report));
    assert(report.finished && report.exit_code == 0 && report.steps == 7);
    assert(output_length == strlen(
           "BORDIFY ARM64: interpreted Mach-O demo under x86 QEMU\n"));
    assert(!memcmp(output_buffer,
                   "BORDIFY ARM64: interpreted Mach-O demo under x86 QEMU\n",
                   output_length));

    put32(file + 4, 0x01000007); /* x86-64 Mach-O is not Apple Silicon */
    assert(!a64_macho_run(file, size, memory, sizeof(memory),
                          callback, NULL, &report));
    put32(file + 4, 0x0100000c);
    put32(file + 32, 0x0000000c); /* reject dynamic libraries */
    assert(!a64_macho_run(file, size, memory, sizeof(memory),
                          callback, NULL, &report));
    put32(file + 32, 0x19);
    put32(file + 0x100, 0xffffffffu); /* unsupported ARM64 instruction */
    assert(!a64_macho_run(file, size, memory, sizeof(memory),
                          callback, NULL, &report));
    assert(report.unsupported_opcode == 0xffffffffu);
    put32(file + 0x100, 0x14000000u); /* infinite loop is rejected opcode */
    assert(!a64_macho_run(file, size, memory, sizeof(memory),
                          callback, NULL, &report));
    assert(!a64_macho_run(file, 30, memory, sizeof(memory),
                          callback, NULL, &report));
    assert(!a64_macho_run(NULL, size, memory, sizeof(memory),
                          callback, NULL, &report));
    puts("ARM64 Mach-O bounded interpreter tests: PASS");
    return 0;
}
