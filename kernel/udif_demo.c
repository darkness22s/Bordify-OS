#include "udif_demo.h"
#include "fat16.h"
#include "package_inspect.h"
#include "udif_blkx.h"
#include "hfsplus_probe.h"
#include "printf.h"
#include "string.h"

/* Integration smoke tests only. Uses the same FAT16 reader as Files. */
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

static bool read_test_sector(const char *name, const char *marker,
                             size_t marker_len) {
    fat_entry_t apps, dmg;
    if (!fat16_mounted() || !fat16_find(0, "APPS", &apps) ||
        !fat16_find(apps.cluster, name, &dmg) || dmg.size < 512)
        return false;

    uint8_t trailer[512];
    if (fat16_read_range(apps.cluster, dmg.name,
                         dmg.size - sizeof(trailer), trailer,
                         sizeof(trailer)) != (int)sizeof(trailer))
        return false;
    package_info_t package;
    if (!package_inspect_dmg_trailer(trailer, sizeof(trailer), dmg.size,
                                     &package) || !package.valid ||
        package.metadata_length == 0 || package.metadata_length > 4096 ||
        package.metadata_offset > UINT32_MAX) return false;

    static char xml[4096];
    if (fat16_read_range(apps.cluster, dmg.name,
                         (uint32_t)package.metadata_offset,
                         xml, (uint32_t)package.metadata_length) !=
        (int)package.metadata_length) return false;

    static udif_block_map_t map;
    if (!udif_blkx_from_xml(xml, (size_t)package.metadata_length,
                            package.data_offset, dmg.size, &map))
        return false;

    file_reader_t file = {apps.cluster, dmg.name, dmg.size};
    uint8_t first[512];
    if (udif_blkx_read_sector(&map, map.sector_start, file_read,
                              &file, first) != UDIF_IO_OK) return false;
    return kmemcmp(first, marker, marker_len) == 0;
}


/*
 * End-to-end compressed DMG -> UDIF block map -> HFS+ volume header.
 * The fixture is only a filesystem HEADER; no directory catalog is mounted.
 */
static bool probe_hfsplus_volume(void) {
    fat_entry_t apps, dmg;
    if (!fat16_mounted() || !fat16_find(0, "APPS", &apps) ||
        !fat16_find(apps.cluster, "HFSPLUS.DMG", &dmg) ||
        dmg.size < 512) return false;

    uint8_t trailer[512];
    if (fat16_read_range(apps.cluster, dmg.name, dmg.size - 512,
                         trailer, sizeof(trailer)) != 512) return false;
    package_info_t package;
    if (!package_inspect_dmg_trailer(trailer, sizeof(trailer), dmg.size,
                                    &package) || !package.valid ||
        package.metadata_length == 0 || package.metadata_length > 4096 ||
        package.metadata_offset > UINT32_MAX) return false;

    static char xml[4096];
    if (fat16_read_range(apps.cluster, dmg.name,
                         (uint32_t)package.metadata_offset, xml,
                         (uint32_t)package.metadata_length) !=
        (int)package.metadata_length) return false;
    static udif_block_map_t map;
    if (!udif_blkx_from_xml(xml, (size_t)package.metadata_length,
                            package.data_offset, dmg.size, &map) ||
        map.sector_count < 3) return false;

    file_reader_t file = {apps.cluster, dmg.name, dmg.size};
    uint8_t header[512];
    if (udif_blkx_read_sector(&map, map.sector_start + 2,
                              file_read, &file, header) != UDIF_IO_OK)
        return false;
    hfsplus_volume_info_t info;
    if (!hfsplus_probe_header(header, sizeof(header),
                              map.sector_count, &info) ||
        info.case_sensitive || !info.journaled ||
        info.block_size != 4096 || info.total_blocks != 8 ||
        info.catalog_first_block != 2) return false;
    uint64_t catalog_sector;
    if (!hfsplus_catalog_first_sector(&info, map.sector_count,
                                       &catalog_sector) ||
        udif_blkx_read_sector(&map, map.sector_start + catalog_sector,
                              file_read, &file, header) != UDIF_IO_OK)
        return false;
    hfsplus_catalog_info_t catalog;
    return hfsplus_catalog_probe(header, sizeof(header),
                                  info.catalog_logical_size, &catalog) &&
           catalog.root_node == 1 && catalog.node_size == 512 &&
           catalog.total_nodes == 16;
}

void udif_demo_probe(void) {
    static const char raw[] = "BORDIFY_DMG_RAW_SECTOR_READ_OK!!";
    static const char zipped[] = "BORDIFY_DMG_ZLIB_SECTOR_VERIFIED!";
    if (read_test_sector("DMGDEMO.DMG", raw, sizeof(raw) - 1))
        kprintf("[dmg] read-only raw UDIF sector verified in QEMU\n");
    else
        kprintf("[dmg] raw sector probe failed\n");
    if (read_test_sector("ZDMG.DMG", zipped, sizeof(zipped) - 1))
        kprintf("[dmg] zlib compressed UDIF sector verified in QEMU\n");
    else
        kprintf("[dmg] zlib sector probe failed\n");
    if (probe_hfsplus_volume())
        kprintf("[hfsplus] compressed DMG HFS+ volume and catalog header verified in QEMU\n");
    else
        kprintf("[hfsplus] compressed DMG header probe failed\n");
}
