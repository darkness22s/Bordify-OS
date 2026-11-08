#include "a64_demo.h"
#include "a64_macho.h"
#include "fat16.h"
#include "serial.h"
#include "printf.h"

/* A single auditable ARM64 Mach-O demo for exercising the interpreter. */
static uint8_t file_bytes[A64_MACHO_MAX_FILE];
static uint8_t guest_memory[A64_GUEST_BYTES];

static void serial_output(const uint8_t *bytes, size_t count, void *unused) {
    (void)unused;
    for (size_t i = 0; i < count; ++i) serial_putc((char)bytes[i]);
}

int a64_demo_run(void) {
    fat_entry_t apps, file;
    if (!fat16_mounted() || !fat16_find(0, "APPS", &apps) ||
        !(apps.attr & FAT_ATTR_DIRECTORY) ||
        !fat16_find(apps.cluster, "ARMHELLO.MCH", &file) ||
        file.size < 32 || file.size > A64_MACHO_MAX_FILE) {
        kprintf("[arm64] Mach-O demo missing or oversized\n");
        return -1;
    }
    if (fat16_read(apps.cluster, file.name, file_bytes, file.size) !=
        (int)file.size) {
        kprintf("[arm64] failed reading Mach-O demo\n");
        return -1;
    }
    a64_report_t report;
    if (!a64_macho_run(file_bytes, file.size,
                       guest_memory, sizeof(guest_memory),
                       serial_output, NULLPTR, &report) ||
        !report.finished) {
        kprintf("[arm64] rejected demo instruction or malformed Mach-O\n");
        return -1;
    }
    kprintf("[arm64] Mach-O interpreted safely; guest exited %d after %u steps\n",
            report.exit_code, report.steps);
    return report.exit_code;
}
