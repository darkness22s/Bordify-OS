#ifndef BORDIFY_HFSPLUS_READ_H
#define BORDIFY_HFSPLUS_READ_H
#include "types.h"
#include "udif_blkx.h"

/* Read-only HFS+ filesystem subset backed by UDIF sector callbacks.
 * Scope: 512-byte catalog B-tree leaf, root directory file key,
 * inline first data-fork extent (no overflow B-tree), UTF-16BE ASCII names.
 * No decompression of HFS+ file compression, hard links, ACL, or writes.
 */
#define HFSPLUS_MAX_EXTRACT (16u * 1024u)
typedef struct {
    uint32_t file_id;
    uint32_t first_block;
    uint32_t extent_blocks;
    uint32_t size;
    char filename[32];
} hfsplus_file_t;
typedef struct {
    const udif_block_map_t *map;
    udif_reader_t reader;
    void *context;
    uint32_t allocation_block_bytes;
    uint32_t total_allocation_blocks;
    uint32_t bitmap_first_block;
    uint32_t bitmap_extent_blocks;
    uint64_t catalog_start_sector;
    uint64_t catalog_bytes;
    uint32_t root_node;
    uint16_t node_bytes;
    uint32_t file_count;
} hfsplus_mount_t;

/* Mount only validated HFS+ first catalog extent and header metadata.
 * Mount is read-only and does not allocate or mutate host storage. */
bool hfsplus_mount_readonly(hfsplus_mount_t *mount,
                             const udif_block_map_t *map,
                             udif_reader_t reader, void *context);
/* Find a root-directory file record, return direct first extent metadata.
 * Unsupported catalog B-trees and compressed/symlink files fail closed. */
bool hfsplus_find_root_file(const hfsplus_mount_t *mount,
                            const char *name, hfsplus_file_t *file);
/* Read file bytes through the read-only UDIF sector reader.
 * out_capacity must be >= file->size, file size <= 16 KiB. */
bool hfsplus_read_file(const hfsplus_mount_t *mount,
                        const hfsplus_file_t *file,
                        uint8_t *out, size_t out_capacity);
#endif
