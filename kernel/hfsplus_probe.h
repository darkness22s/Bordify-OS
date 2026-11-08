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
    uint64_t catalog_logical_size;
} hfsplus_volume_info_t;

typedef struct {
    uint16_t tree_depth;
    uint32_t root_node;
    uint32_t leaf_records;
    uint16_t node_size;
    uint32_t total_nodes;
    uint32_t free_nodes;
} hfsplus_catalog_info_t;

/* Read-only B-tree header-node validation, NOT catalog traversal.
 * The supplied bytes are the first 512 bytes of the catalog fork.
 * Validates header record and the first node's record-offset table. */
bool hfsplus_catalog_probe(const uint8_t *sector, size_t length,
                            uint64_t catalog_bytes,
                            hfsplus_catalog_info_t *out);

/* Compute the first catalog-fork sector within the same UDIF partition.
 * Refuses empty/out-of-range extents; this only addresses extent zero. */
bool hfsplus_catalog_first_sector(const hfsplus_volume_info_t *volume,
                                   uint64_t volume_sectors,
                                   uint64_t *sector);

/* volume_sectors is the logical 512-byte sector count for this partition.
 * Rejects structurally inconsistent headers before publishing any info. */
bool hfsplus_probe_header(const uint8_t *header, size_t length,
                          uint64_t volume_sectors,
                          hfsplus_volume_info_t *out);
#endif
