#include "udif_demo.h"
#include "fat16.h"
#include "package_inspect.h"
#include "udif_blkx.h"
#include "printf.h"
#include "string.h"

/* Integration smoke test only. Uses the same disk read API as Files. */
typedef struct {
    uint16_t cluster;
    const char *name;
    uint32_t size;
} file_reader_t;

static int file_read(void *context, uint64_t offset,
                     void *output, size_t bytes) {
    file_reader_t *r = (file_reader_t *)context;
    if (offset > r->size || bytes > r->size - (uint32_t)offset ||
        bytes > UINT32_MAX) return -1;
    return fat16_read_range(r->cluster, r->name,
                            (uint32_t)offset, output, (uint32_t)bytes);
}

void udif_demo_probe(void) {
    fat_entry_t apps, dmg;
    if (!fat16_mounted() ||
        !fat16_find(0, "APPS", &apps) ||
        !fat16_find(apps.cluster, "DMGDEMO.DMG", &dmg) ||
        dmg.size < 1024) {
        kprintf("[dmg] demo image missing\n");
        return;
    }
    uint8_t trailer[512];
    if (fat16_read_range(apps.cluster, dmg.name,
                         dmg.size - sizeof(trailer), trailer,
                         sizeof(trailer)) != sizeof(trailer)) {
        kprintf("[dmg] demo trailer I/O failed\n");
        return;
    }
    package_info_t package;
    if (!package_inspect_dmg_trailer(trailer, sizeof(trailer), dmg.size,
                                     &package) || !package.valid ||
        package.metadata_length == 0 || package.metadata_length > 4096 ||
        package.metadata_offset > UINT32_MAX) {
        kprintf("[dmg] invalid demo footer\n");
        return;
    }
    static char xml[4096];
    if (fat16_read_range(apps.cluster, dmg.name,
                         (uint32_t)package.metadata_offset,
                         xml, (uint32_t)package.metadata_length) !=
        (int)package.metadata_length) {
        kprintf("[dmg] demo metadata I/O failed\n");
        return;
    }
    static udif_block_map_t map;
    if (!udif_blkx_from_xml(xml, (size_t)package.metadata_length,
                            package.data_offset, dmg.size, &map)) {
        kprintf("[dmg] invalid demo blkx map\n");
        return;
    }
    file_reader_t file = { apps.cluster, dmg.name, dmg.size };
    uint8_t first[512];
    udif_io_result_t status =
        udif_blkx_read_sector(&map, map.sector_start, file_read,
                              &file, first);
    static const char expected[] = "BORDIFY_DMG_RAW_SECTOR_READ_OK!!";
    if (status == UDIF_IO_OK &&
        kmemcmp(first, expected, sizeof(expected) - 1) == 0) {
        kprintf("[dmg] read-only raw UDIF sector verified in QEMU\n");
    } else {
        kprintf("[dmg] raw sector probe failed (%d)\n", (int)status);
    }
}
