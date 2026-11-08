#include "hfsplus_probe.h"

static uint16_t be16(const uint8_t *p) {
    return (uint16_t)(((uint16_t)p[0] << 8) | p[1]);
}
static uint32_t be32(const uint8_t *p) {
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8) | p[3];
}

bool hfsplus_probe_header(const uint8_t *header, size_t length,
                          uint64_t volume_sectors,
                          hfsplus_volume_info_t *out) {
    if (!out) return false;
    out->case_sensitive = false;
    out->journaled = false;
    out->block_size = 0;
    out->total_blocks = 0;
    out->free_blocks = 0;
    out->files = 0;
    out->folders = 0;
    out->catalog_first_block = 0;
    out->catalog_extent_blocks = 0;

    if (!header || length < 512 || volume_sectors < 3) return false;
    const uint16_t sig = be16(header);
    const uint16_t version = be16(header + 2);
    if (!((sig == 0x482Bu && version == 4) ||
          (sig == 0x4858u && version == 5))) return false;

    uint32_t attributes = be32(header + 4);
    uint32_t block_size = be32(header + 40);
    uint32_t total_blocks = be32(header + 44);
    uint32_t free_blocks = be32(header + 48);
    /* TN1150 requires power-of-two allocation blocks >= one sector. */
    if (block_size < 512 || (block_size & (block_size - 1u)) != 0 ||
        block_size % 512 != 0 || total_blocks == 0 ||
        free_blocks > total_blocks ||
        (uint64_t)total_blocks * (block_size / 512u) > volume_sectors)
        return false;

    /* A basic sanity check of the catalog fork's first extent.
     * HFSPlusForkData catalogFile is at offset 272, first extent at 288. */
    uint32_t catalog_total_blocks = be32(header + 284);
    uint32_t first = be32(header + 288);
    uint32_t extent_blocks = be32(header + 292);
    if (catalog_total_blocks > total_blocks ||
        first > total_blocks ||
        extent_blocks > total_blocks - first ||
        extent_blocks > catalog_total_blocks)
        return false;

    out->case_sensitive = sig == 0x4858u;
    out->journaled = (attributes & (1u << 13)) != 0;
    out->block_size = block_size;
    out->total_blocks = total_blocks;
    out->free_blocks = free_blocks;
    out->files = be32(header + 32);
    out->folders = be32(header + 36);
    out->catalog_first_block = first;
    out->catalog_extent_blocks = extent_blocks;
    return true;
}
