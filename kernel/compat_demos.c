#include "compat_demos.h"
#include "pe64_vm.h"
#include "a64_macho.h"
#include "fat16.h"
#include "serial.h"
#include "printf.h"

/* Only included audited sample binaries are run. Both CPUs are interpreted,
 * not executed as arbitrary ring-0 or ring-3 native host code. */
static uint8_t file_data[PE64_MAX_FILE];
static uint8_t x64_memory[PE64_GUEST_BYTES];
static uint8_t arm_memory[A64_GUEST_BYTES];

static void serial_bytes(const uint8_t *data, size_t n, void *ctx) {
    (void)ctx;
    for (size_t i=0;i<n;++i) serial_putc((char)data[i]);
}
static uint32_t load_demo(const char *name) {
    fat_entry_t apps, file;
    if (!fat16_mounted() || !fat16_find(0,"APPS",&apps) ||
        !(apps.attr & FAT_ATTR_DIRECTORY) ||
        !fat16_find(apps.cluster,name,&file) ||
        (file.attr & FAT_ATTR_DIRECTORY) ||
        file.size<512 || file.size>sizeof(file_data))
        return 0;
    if (fat16_read(apps.cluster,file.name,file_data,file.size)!=(int)file.size)
        return 0;
    return file.size;
}
void compat_vm_demos(void) {
    uint32_t length=load_demo("WIN64.EXE");
    pe64_report_t w;
    if (length && pe64_vm_run(file_data,length,x64_memory,sizeof(x64_memory),
                              serial_bytes,NULLPTR,&w) &&
        w.finished && w.exit_code==0) {
        kprintf("[win64] PE32+ AMD64 interpreted safely; %u imports; exited %d\n",
                w.imports,w.exit_code);
    } else {
        kprintf("[win64] PE32+ interpreter rejected test executable\n");
    }
    length=load_demo("DARWIN.MCH");
    a64_report_t d;
    if (length && a64_macho_run_darwin(file_data,length,arm_memory,
                                       sizeof(arm_memory),serial_bytes,
                                       NULLPTR,&d) &&
        d.finished && d.exit_code==0) {
        kprintf("[darwin] ARM64 BSD syscall and NSString/NSApplication bridge verified; exited %d\n",
                d.exit_code);
    } else {
        kprintf("[darwin] ARM64 compatibility bridge rejected test executable\n");
    }
}
