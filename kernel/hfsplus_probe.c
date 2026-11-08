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
    out->catalog_logical_size = 0;

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
    uint64_t catalog_logical_size =
        ((uint64_t)be32(header + 272) << 32) | be32(header + 276);
    uint32_t catalog_total_blocks = be32(header + 284);
    uint32_t first = be32(header + 288);
    uint32_t extent_blocks = be32(header + 292);
    if (catalog_total_blocks > total_blocks ||
        first > total_blocks ||
        extent_blocks > total_blocks - first ||
        extent_blocks > catalog_total_blocks ||
        catalog_logical_size > (uint64_t)catalog_total_blocks * block_size)
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
    out->catalog_logical_size = catalog_logical_size;
    return true;
}

bool hfsplus_catalog_first_sector(const hfsplus_volume_info_t *volume,
                                   uint64_t volume_sectors,
                                   uint64_t *sector) {
    if (!volume || !sector || volume->block_size < 512 ||
        volume->block_size % 512 != 0 ||
        volume->catalog_extent_blocks == 0 ||
        volume->catalog_logical_size < 512) return false;
    uint64_t sectors_per_block = volume->block_size / 512u;
    uint64_t start = (uint64_t)volume->catalog_first_block * sectors_per_block;
    uint64_t extent = (uint64_t)volume->catalog_extent_blocks * sectors_per_block;
    if (start >= volume_sectors || extent > volume_sectors - start ||
        volume->catalog_logical_size > extent * 512u) return false;
    *sector = start;
    return true;
}

bool hfsplus_catalog_probe(const uint8_t *sector, size_t length,
                            uint64_t catalog_bytes,
                            hfsplus_catalog_info_t *out) {
    if (!out) return false;
    out->tree_depth = 0;
    out->root_node = 0;
    out->leaf_records = 0;
    out->node_size = 0;
    out->total_nodes = 0;
    out->free_nodes = 0;
    if (!sector || length < 512 || catalog_bytes < 512) return false;
    /* BTNodeDescriptor: fLink, bLink, kind=header, height=0,
     * numRecords>=1, reserved=0. Header record begins at offset 14. */
    if (be32(sector) != 0 || be32(sector + 4) != 0 ||
        sector[8] != 1 || sector[9] != 0 ||
        be16(sector + 10) != 3 || be16(sector + 12) != 0)
        return false;
    uint16_t depth = be16(sector + 14);
    uint32_t root = be32(sector + 16);
    uint32_t leaves = be32(sector + 20);
    uint32_t first_leaf = be32(sector + 24);
    uint32_t last_leaf = be32(sector + 28);
    uint16_t node_size = be16(sector + 32);
    uint32_t total = be32(sector + 36);
    uint32_t free_nodes = be32(sector + 40);
    if (node_size < 512 || (node_size & (node_size - 1u)) ||
        (uint64_t)node_size > catalog_bytes ||
        total == 0 || free_nodes >= total ||
        (uint64_t)total * node_size > catalog_bytes ||
        (depth == 0 && (root != 0 || leaves != 0 ||
                        first_leaf != 0 || last_leaf != 0)) ||
        (depth != 0 && (root == 0 || root >= total ||
                        first_leaf == 0 || first_leaf >= total ||
                        last_leaf == 0 || last_leaf >= total ||
                        leaves == 0)))
        return false;
    /* The header node is typically 512 bytes. Larger nodes require
     * a complete node read to inspect the trailing offset table. */
    if (node_size != 512) return false;
    /* Three record offsets plus free-space offset, stored backwards
     * at the end of the header node. */
    uint16_t record0 = be16(sector + 510);
    uint16_t record1 = be16(sector + 508);
    uint16_t record2 = be16(sector + 506);
    uint16_t free_start = be16(sector + 504);
    if (record0 != 14 || record1 < 120 || record1 <= record0 ||
        record2 <= record1 || free_start <= record2 ||
        free_start > 504) return false;
    out->tree_depth = depth;
    out->root_node = root;
    out->leaf_records = leaves;
    out->node_size = node_size;
    out->total_nodes = total;
    out->free_nodes = free_nodes;
    return true;
}
