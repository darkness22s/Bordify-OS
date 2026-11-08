#ifndef BORDIFY_HFSPLUS_PROBE_H
#define BORDIFY_HFSPLUS_PROBE_H

#include "types.h"

/*
 * HFS+ / HFSX read-only volume-header probe (TN1150).
 * The caller supplies the 512 bytes at volume offset 1024 (sector 2).
 * This does not read a catalog B-tree, mount a filesystem, or write data.
 */
typedef struct {
    bool case_sensitive; /* HFSX */
    bool journaled;
    uint32_t block_size;
    uint32_t total_blocks;
    uint32_t free_blocks;
    uint32_t files;
    uint32_t folders;
    uint32_t catalog_first_block;
    uint32_t catalog_extent_blocks;
} hfsplus_volume_info_t;

/* volume_sectors is the logical 512-byte sector count for this partition.
 * Rejects structurally inconsistent headers before publishing any info. */
bool hfsplus_probe_header(const uint8_t *header, size_t length,
                          uint64_t volume_sectors,
                          hfsplus_volume_info_t *out);
#endif
