#include "dmg_install.h"
#include "fat16.h"
#include "package_inspect.h"
#include "udif_blkx.h"
#include "hfsplus_read.h"
#include "a64_macho.h"
#include "serial.h"
#include "printf.h"
#include "string.h"

/* Only the packaged, deterministic regression fixture MOUNT.DMG is
 * installed, into one fixed FAT16 filename. No general installer scripts,
 * filesystem writes inside HFS+ or arbitrary application execution.
 */
static char xml[4096];
static udif_block_map_t map;
static uint8_t extracted[HFSPLUS_MAX_EXTRACT];
static uint8_t verify[HFSPLUS_MAX_EXTRACT];
static uint8_t a64_guest[A64_GUEST_BYTES];

typedef struct {
    uint16_t parent;
    char name[13];
    uint32_t size;
} dmg_source_t;
static int reader(void *ctx,uint64_t at,void *dst,size_t count) {
    dmg_source_t *src=(dmg_source_t*)ctx;
    if (!src || at>src->size || count>src->size-(uint32_t)at ||
        at>UINT32_MAX || count>UINT32_MAX) return -1;
    return fat16_read_range(src->parent,src->name,(uint32_t)at,
                            dst,(uint32_t)count);
}
static void output(const uint8_t *bytes,size_t n,void *ctx) {
    (void)ctx;
    for(size_t i=0;i<n;++i) serial_putc((char)bytes[i]);
}
int dmg_installed_demo_run(void) {
    fat_entry_t apps,app;
    if (!fat16_mounted() || !fat16_find(0,"APPS",&apps) ||
        !fat16_find(apps.cluster,"DMGAPP.MCH",&app) ||
        (app.attr & FAT_ATTR_DIRECTORY) ||
        !app.size || app.size>sizeof(verify) ||
        fat16_read(apps.cluster,app.name,verify,app.size)!=(int)app.size)
        return -1;
    a64_report_t result;
    if (!a64_macho_run_darwin(verify,app.size,a64_guest,
                               sizeof(a64_guest),output,NULLPTR,&result) ||
        !result.finished || result.exit_code!=0) return -1;
    kprintf("[dmg-install] extracted Mach-O ARM64 app launched; exit %d\n",
            result.exit_code);
    return 0;
}
int dmg_install_demo(void) {
    fat_entry_t apps,dmg;
    if (!fat16_mounted() || !fat16_find(0,"APPS",&apps) ||
        !(apps.attr & FAT_ATTR_DIRECTORY) ||
        !fat16_find(apps.cluster,"MOUNT.DMG",&dmg) ||
        dmg.size<512) goto failed;
    dmg_source_t src={apps.cluster,{0},dmg.size};
    kstrncpy(src.name,dmg.name,sizeof(src.name)-1);
    uint8_t trailer[512];
    if (reader(&src,dmg.size-512,trailer,sizeof(trailer))!=512)
        goto failed;
    package_info_t package;
    if (!package_inspect_dmg_trailer(trailer,sizeof(trailer),
                                     dmg.size,&package) ||
        !package.valid || !package.metadata_length ||
        package.metadata_length>sizeof(xml) ||
        package.metadata_offset>UINT32_MAX ||
        reader(&src,package.metadata_offset,xml,
               (size_t)package.metadata_length)!=(int)package.metadata_length ||
        !udif_blkx_from_xml(xml,(size_t)package.metadata_length,
                            package.data_offset,dmg.size,&map))
        goto failed;

    hfsplus_mount_t mount;
    hfsplus_file_t file;
    if (!hfsplus_mount_readonly(&mount,&map,reader,&src) ||
        !hfsplus_find_root_file(&mount,"DARWIN.MCH",&file) ||
        !hfsplus_read_file(&mount,&file,extracted,sizeof(extracted)))
        goto failed;
    kprintf("[dmg] zlib UDIF HFS+ read-only mount; found %s (%u bytes)\n",
            file.filename,file.size);

    /* A bounded, fixed-destination copy, not execution of a PKG script. */
    if (fat16_write(apps.cluster,"DMGAPP.MCH",
                    extracted,file.size)!=(int)file.size ||
        fat16_read(apps.cluster,"DMGAPP.MCH",
                   verify,(uint32_t)sizeof(verify))!=(int)file.size ||
        kmemcmp(extracted,verify,file.size)!=0)
        goto failed;
    kprintf("[dmg-install] extracted HFS+ file to APPS/DMGAPP.MCH (%u bytes)\n",
            file.size);
    return dmg_installed_demo_run();
failed:
    kprintf("[dmg-install] HFS+ DMG extraction rejected or failed\n");
    return -1;
}
