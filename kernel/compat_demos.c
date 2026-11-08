#include "compat_demos.h"
#include "pe64_vm.h"
#include "a64_macho.h"
#include "dmg_install.h"
#include "fat16.h"
#include "serial.h"
#include "printf.h"
#include "string.h"

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
int compat_win64_demo_run(void) {
    uint32_t length=load_demo("WIN64.EXE");
    pe64_report_t w;
    if (length && pe64_vm_run(file_data,length,x64_memory,sizeof(x64_memory),
                              serial_bytes,NULLPTR,&w) &&
        w.finished && w.exit_code==0) {
        kprintf("[win64] PE32+ AMD64 interpreted safely; %u imports; exited %d\n",
                w.imports,w.exit_code);
        return w.exit_code;
    }
    kprintf("[win64] PE32+ interpreter rejected test executable\n");
    return -1;
}
int compat_darwin_demo_run(void) {
    uint32_t length=load_demo("DARWIN.MCH");
    a64_report_t d;
    if (length && a64_macho_run_darwin(file_data,length,arm_memory,
                                       sizeof(arm_memory),serial_bytes,
                                       NULLPTR,&d) &&
        d.finished && d.exit_code==0) {
        kprintf("[darwin] ARM64 BSD syscall and NSString/NSApplication bridge verified; exited %d\n",
                d.exit_code);
        return d.exit_code;
    }
    kprintf("[darwin] ARM64 compatibility bridge rejected test executable\n");
    return -1;
}
/* Independently clang-assembled (not generated hand-encoded) ARM64 Mach-O.
 * Uses real AArch64 arithmetic, flags, conditional branches, BL/RET,
 * pair-stack and LDR/STR while Darwin output remains test-only.
 */
int compat_arm64_workload_run(void) {
    uint32_t length=load_demo("ARMWORK.MCH");
    a64_report_t result;
    if (length && a64_macho_run_darwin(file_data,length,
                                       arm_memory,sizeof(arm_memory),
                                       serial_bytes,NULLPTR,&result) &&
        result.finished && result.exit_code==0 && result.steps>45) {
        kprintf("[arm64] clang-assembled Mach-O arithmetic, branches and stack passed; %u steps, exit %d\n",
                result.steps,result.exit_code);
        return result.exit_code;
    }
    kprintf("[arm64] extended clang-assembled Mach-O rejected or failed\n");
    return -1;
}
/* Genuine C-compiled ARM64 Mach-O, independent of handcrafted opcodes.
 * A volatile checksum loop uses guest stack memory and condition flags.
 */
int compat_arm64_c_run(void) {
    uint32_t length=load_demo("ARMCALC.MCH");
    a64_report_t result;
    if (length && a64_macho_run_darwin(file_data,length,
                                       arm_memory,sizeof(arm_memory),
                                       serial_bytes,NULLPTR,&result) &&
        result.finished && result.exit_code==0 && result.steps>45) {
        kprintf("[arm64] clang-compiled freestanding C Mach-O checksum passed; %u steps, exit %d\n",
                result.steps,result.exit_code);
        return 0;
    }
    kprintf("[arm64] compiled C checksum guest rejected or failed\n");
    return -1;
}
/* Only this audited setup may commit C:\\APP64.EXE to /APPS/APP64.EXE.
 * Never expose a general guest-controlled FAT16 path or kernel file handle. */
static uint8_t installed_verify[8192];
static bool installed_commit(const uint8_t *data, size_t bytes, void *ctx) {
    (void)ctx;
    fat_entry_t apps;
    if (!fat16_mounted() || !data || bytes<512 ||
        bytes>sizeof(installed_verify) ||
        !fat16_find(0,"APPS",&apps) ||
        !(apps.attr & FAT_ATTR_DIRECTORY) ||
        fat16_write(apps.cluster,"APP64.EXE",data,(uint32_t)bytes) !=
          (int)bytes) return false;
    int got=fat16_read(apps.cluster,"APP64.EXE",
                       installed_verify,(uint32_t)sizeof(installed_verify));
    return got==(int)bytes && kmemcmp(installed_verify,data,bytes)==0;
}
int compat_win64_installed_run(void) {
    uint32_t bytes=load_demo("APP64.EXE");
    pe64_report_t launched;
    if (!bytes || !pe64_vm_run(file_data,bytes,x64_memory,
                               sizeof(x64_memory),serial_bytes,
                               NULLPTR,&launched) ||
        !launched.finished || launched.exit_code!=0) {
        kprintf("[win64-install] installed AMD64 application failed\n");
        return -1;
    }
    kprintf("[win64-install] installed AMD64 application launched; exit %d\n",
            launched.exit_code);
    return 0;
}
int compat_win64_installer_run(void) {
    uint32_t bytes=load_demo("SETUP64.EXE");
    pe64_report_t installed;
    if (!bytes ||
        !pe64_vm_run_install(file_data,bytes,x64_memory,sizeof(x64_memory),
                            serial_bytes,NULLPTR,installed_commit,NULLPTR,
                            &installed) ||
        !installed.finished || installed.exit_code!=0 ||
        installed.installed_bytes!=2048u) {
        kprintf("[win64-install] x64 setup rejected or installation failed\n");
        return -1;
    }
    kprintf("[win64-install] PE32+ installer committed APP64.EXE (%u bytes)\n",
            installed.installed_bytes);
    return compat_win64_installed_run();
}

void compat_vm_demos(void) {
    compat_win64_demo_run();
    compat_win64_installer_run();
    compat_darwin_demo_run();
    compat_arm64_workload_run();
    compat_arm64_c_run();
    dmg_install_demo();
}
