#include "chrome_trial.h"

#ifdef BORDIFY_CHROME_PROBE

#include "fat16.h"
#include "exec_inspect.h"
#include "package_inspect.h"
#include "udif_blkx.h"
#include "hfsplus_read.h"
#include "process.h"
#include "pe64_vm.h"
#include "printf.h"

/* Research experiment only: real downloaded Chrome files are untrusted
 * data, not audited test fixtures. Do not enable on normal release boots.
 * No filesystem mutations or host OS calls are permitted here.
 */
typedef struct {
    uint16_t parent;
    char filename[13];
    uint32_t bytes;
} chrome_reader_t;

static uint8_t prefix[4096];
static uint8_t pe_file[PE64_MAX_FILE];
static uint8_t pe_guest[PE64_GUEST_BYTES];
static char xml[64u * 1024u];
static udif_block_map_t block_map;

static int chrome_read(void *context, uint64_t offset,
                       void *output, size_t count) {
    chrome_reader_t *file = (chrome_reader_t *)context;
    if (!file || !output || offset > file->bytes ||
        count > (uint64_t)file->bytes - offset ||
        count > UINT32_MAX)
        return -1;
    return fat16_read_range(file->parent,file->filename,
                            (uint32_t)offset,output,(uint32_t)count);
}
static void ignore_guest_output(const uint8_t *data, size_t count, void *ctx) {
    (void)data; (void)count; (void)ctx;
}
static void try_windows(uint16_t parent) {
    fat_entry_t item;
    if (!fat16_find(parent,"CHROME.EXE",&item) ||
        (item.attr & FAT_ATTR_DIRECTORY)) {
        kprintf("[chrome-win] installer missing from FAT16\n");
        return;
    }
    kprintf("[chrome-win] real installer size=%u bytes\n",item.size);
    size_t count=item.size<sizeof(prefix) ? item.size : sizeof(prefix);
    if (count<512 || fat16_read_range(parent,item.name,0,prefix,
                                      (uint32_t)count)!=(int)count) {
        kprintf("[chrome-win] executable header read failed\n");
        return;
    }
    exec_info_t info;
    if (!exec_inspect(prefix,count,&info) || !info.valid) {
        kprintf("[chrome-win] executable header rejected\n");
    } else {
        kprintf("[chrome-win] detected %s / %s\n",
                exec_format_name(info.format),exec_arch_name(info.arch));
    }

    /* This is a real launch attempt through the existing ring-3 PE loader.
     * Chrome normally exceeds its 64-KiB whole-image/PE32-only limits.
     * The loader is fail-closed and will not execute unsupported code. */
    int rc=process_run_win32_pe("APPS","CHROME.EXE");
    if (rc==0)
        kprintf("[chrome-win] native PE32 loader reported exit 0 (inspect separately)\n");
    else
        kprintf("[chrome-win] native PE32 launch attempt rejected (status=%d)\n",rc);

    if (item.size > PE64_MAX_FILE) {
        kprintf("[chrome-win] AMD64 interpreter rejected: installer exceeds 65536-byte image cap\n");
        return;
    }
    if (fat16_read(parent,item.name,pe_file,item.size)!=(int)item.size) {
        kprintf("[chrome-win] bounded AMD64 image read failed\n");
        return;
    }
    pe64_report_t report;
    bool ran=pe64_vm_run(pe_file,item.size,pe_guest,sizeof(pe_guest),
                         ignore_guest_output,0,&report);
    kprintf("[chrome-win] bounded AMD64 launch attempt result=%s\n",
            ran && report.finished ? "finished" : "rejected");
}

/* Independent Windows application tests: one generic procedure rather
 * than per-app hard-coded imports. This is a genuine safe launch attempt
 * through the current PE32 loader, not pretend execution of unsupported PE.
 */
static void try_other_windows(uint16_t parent, const char *filename,
                              const char *label) {
    fat_entry_t item;
    if (!fat16_find(parent,filename,&item) ||
        (item.attr & FAT_ATTR_DIRECTORY)) {
        kprintf("[winapps] %s: file not available on FAT16\n",label);
        return;
    }
    kprintf("[winapps] %s: real file size=%u\n",label,item.size);
    uint8_t first[512];
    if (item.size<512 || fat16_read_range(parent,item.name,0,
                                         first,sizeof(first))!=512) {
        kprintf("[winapps] %s: header read rejected\n",label);
        return;
    }
    exec_info_t info;
    if (!exec_inspect(first,sizeof(first),&info) || !info.valid) {
        kprintf("[winapps] %s: PE format rejected\n",label);
        return;
    }
    kprintf("[winapps] %s: detected %s / %s\n",
            label,exec_format_name(info.format),exec_arch_name(info.arch));
    int rc=process_run_win32_pe("APPS",filename);
    kprintf("[winapps] %s: native Win32 attempt returned %d\n",label,rc);
}
static void inspect_existing_demo(uint16_t parent, const char *filename) {
    fat_entry_t file;
    uint8_t header[8];
    if (!fat16_find(parent,filename,&file) ||
        fat16_read_range(parent,file.name,0,header,sizeof(header))!=
            (int)sizeof(header)) {
        kprintf("[winapps] baseline %s: file/header inaccessible\n",filename);
        return;
    }
    kprintf("[winapps] baseline %s: size=%u first4=%x %x %x %x\n",
            filename,file.size,(unsigned)header[0],(unsigned)header[1],
            (unsigned)header[2],(unsigned)header[3]);
}

static void try_macos(uint16_t parent) {
    fat_entry_t item;
    if (!fat16_find(parent,"CHROME.DMG",&item) ||
        (item.attr & FAT_ATTR_DIRECTORY) || item.size<512) {
        kprintf("[chrome-mac] installer missing or truncated\n");
        return;
    }
    kprintf("[chrome-mac] real DMG size=%u bytes\n",item.size);
    chrome_reader_t reader={parent,{0},item.size};
    unsigned i=0;
    while (i+1<sizeof(reader.filename) && item.name[i]) {
        reader.filename[i]=item.name[i];
        ++i;
    }
    reader.filename[i]=0;
    uint8_t trailer[512];
    if (chrome_read(&reader,item.size-512,trailer,sizeof(trailer))!=512) {
        kprintf("[chrome-mac] failed reading real DMG footer\n");
        return;
    }
    package_info_t pkg;
    if (!package_inspect_dmg_trailer(trailer,sizeof(trailer),
                                     item.size,&pkg) || !pkg.valid) {
        kprintf("[chrome-mac] UDIF trailer rejected\n");
        kprintf("[chrome-mac] app launch rejected: unsupported image\n");
        return;
    }
    kprintf("[chrome-mac] valid UDIF trailer, XML metadata bytes=%u\n",
            (uint32_t)(pkg.metadata_length>UINT32_MAX ?
                       UINT32_MAX:pkg.metadata_length));

    if (pkg.metadata_length>sizeof(xml) ||
        !pkg.metadata_length || pkg.metadata_offset>UINT32_MAX) {
        kprintf("[chrome-mac] blkx parser rejected: metadata exceeds 65536-byte limit\n");
        kprintf("[chrome-mac] app launch rejected: no general APFS/.app/dyld runtime\n");
        return;
    }
    if (chrome_read(&reader,pkg.metadata_offset,xml,
                    (size_t)pkg.metadata_length)!=(int)pkg.metadata_length ||
        !udif_blkx_from_xml(xml,(size_t)pkg.metadata_length,
                            pkg.data_offset,item.size,&block_map)) {
        kprintf("[chrome-mac] blkx parser rejected: unsupported real DMG layout\n");
        kprintf("[chrome-mac] app launch rejected: no general APFS/.app/dyld runtime\n");
        return;
    }
    hfsplus_mount_t mount;
    if (!hfsplus_mount_readonly(&mount,&block_map,chrome_read,&reader)) {
        kprintf("[chrome-mac] HFS+ mount rejected: unsupported real filesystem\n");
    } else {
        kprintf("[chrome-mac] bounded HFS+ root mounted (read only)\n");
        hfsplus_file_t app;
        if (!hfsplus_find_root_file(&mount,"Google Chrome.app",&app))
            kprintf("[chrome-mac] .app bundle not available through single-file root catalog reader\n");
    }
    kprintf("[chrome-mac] app launch rejected: no general APFS/.app/dyld runtime\n");
}

void chrome_trial_run(void) {
    kprintf("[chrome-test] genuine Chrome Windows+macOS compatibility experiment\n");
    fat_entry_t apps;
    if (!fat16_mounted() || !fat16_find(0,"APPS",&apps) ||
        !(apps.attr & FAT_ATTR_DIRECTORY)) {
        kprintf("[chrome-test] APPS directory unavailable\n");
        return;
    }
    inspect_existing_demo(apps.cluster,"WINIO.EXE");
    inspect_existing_demo(apps.cluster,"WIN64.EXE");
    inspect_existing_demo(apps.cluster,"SETUP64.EXE");
    try_windows(apps.cluster);
    try_other_windows(apps.cluster,"WEBSET.EXE","Chrome web installer");
    try_other_windows(apps.cluster,"SEVEN.EXE","7-Zip standalone utility");
    try_other_windows(apps.cluster,"CHRMAPP.EXE","extracted Chrome application");
    try_macos(apps.cluster);
    kprintf("[chrome-test] experiment complete; no Chrome application launched\n");
}

#else
/* Normal release image has no Chrome experiment execution. */
void chrome_trial_run(void) {}
#endif
