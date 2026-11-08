#include "hfsplus_read.h"
#include "hfsplus_probe.h"

/* HFS Plus Volume Format (TN1150): all on-disk integers big-endian.
 * No dynamic allocations. All disk I/O goes through read-only UDIF.
 */
static uint16_t be16(const uint8_t *p) {
    return ((uint16_t)p[0] << 8) | p[1];
}
static uint32_t be32(const uint8_t *p) {
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8) | p[3];
}
static uint64_t be64(const uint8_t *p) {
    return ((uint64_t)be32(p) << 32) | be32(p+4);
}
static bool range(size_t bound, size_t off, size_t size) {
    return off <= bound && size <= bound-off;
}
static bool read_sector(const hfsplus_mount_t *m, uint64_t relative,
                        uint8_t out[512]) {
    if (!m || !m->map || relative >= m->map->sector_count ||
        m->map->sector_start > UINT64_MAX - relative) return false;
    return udif_blkx_read_sector(m->map,m->map->sector_start+relative,
                                  m->reader,m->context,out)==UDIF_IO_OK;
}
/* HFS+ allocation bitmap: MSB-first bit per allocation block.
 * Deliberately support only bitmap data reachable via the first extent,
 * and reject volumes requiring bitmap overflow extents. */
static bool allocated_blocks(const hfsplus_mount_t *m,
                             uint32_t first, uint32_t count) {
    if (!m || !m->map || !m->reader || !m->bitmap_extent_blocks ||
        !m->allocation_block_bytes ||
        !count || first >= m->total_allocation_blocks ||
        count > m->total_allocation_blocks-first) return false;
    uint64_t bitmap_sectors=(uint64_t)m->bitmap_extent_blocks *
                             (m->allocation_block_bytes/512u);
    uint64_t base=(uint64_t)m->bitmap_first_block *
                   (m->allocation_block_bytes/512u);
    uint64_t first_byte=first/8u;
    uint64_t last_byte=((uint64_t)first+count-1u)/8u;
    if (last_byte >= bitmap_sectors*512u ||
        base>=m->map->sector_count ||
        bitmap_sectors>m->map->sector_count-base) return false;
    uint8_t sector[512];
    uint64_t previous=UINT64_MAX;
    for (uint32_t block=first; block<first+count; ++block) {
        uint64_t byte_index=block/8u;
        uint64_t at=byte_index/512u;
        if (at!=previous) {
            if (!read_sector(m,base+at,sector)) return false;
            previous=at;
        }
        if (!(sector[byte_index%512u] & (0x80u >> (block%8u))))
            return false;
    }
    (void)first_byte;
    return true;
}

bool hfsplus_mount_readonly(hfsplus_mount_t *out,
                             const udif_block_map_t *map,
                             udif_reader_t reader, void *context) {
    if (!out) return false;
    out->map=0; out->reader=0; out->context=0;
    if (!map || !reader || map->sector_count < 8) return false;
    hfsplus_mount_t temp={0};
    temp.map=map; temp.reader=reader; temp.context=context;
    uint8_t sector[512];
    if (!read_sector(&temp,2,sector)) return false;
    hfsplus_volume_info_t volume;
    if (!hfsplus_probe_header(sector,sizeof(sector),map->sector_count,&volume) ||
        !volume.catalog_logical_size || volume.catalog_logical_size>65536u)
        return false;
    uint64_t catalog_sector;
    if (!hfsplus_catalog_first_sector(&volume,map->sector_count,
                                       &catalog_sector) ||
        !read_sector(&temp,catalog_sector,sector)) return false;
    hfsplus_catalog_info_t cat;
    if (!hfsplus_catalog_probe(sector,sizeof(sector),
                                volume.catalog_logical_size,&cat) ||
        cat.tree_depth!=1 || cat.node_size!=512 || cat.root_node==0 ||
        cat.leaf_records==0 || cat.leaf_records>16) return false;
    uint64_t node_offset=(uint64_t)cat.root_node*cat.node_size;
    if (node_offset>volume.catalog_logical_size ||
        cat.node_size>volume.catalog_logical_size-node_offset)
        return false;
    /* Only a root leaf in the catalog's first extent is supported. */
    uint64_t extent_bytes=(uint64_t)volume.catalog_extent_blocks *
                           volume.block_size;
    if (node_offset+512>extent_bytes) return false;
    temp.catalog_start_sector=catalog_sector;
    temp.catalog_bytes=volume.catalog_logical_size;
    temp.allocation_block_bytes=volume.block_size;
    /* Allocation-file fork starts at volume header offset 112.
     * Reject malformed/fragmented bitmaps rather than trusting file extents. */
    uint32_t bitmap_total, bitmap_first, bitmap_extent;
    /* sector currently contains the catalog B-tree header; reload volume. */
    if (!read_sector(&temp,2,sector)) return false;
    bitmap_total=be32(sector+124);
    bitmap_first=be32(sector+128);
    bitmap_extent=be32(sector+132);
    if (!bitmap_first || !bitmap_extent ||
        bitmap_total!=bitmap_extent ||
        bitmap_first>=volume.total_blocks ||
        bitmap_extent>volume.total_blocks-bitmap_first ||
        (uint64_t)bitmap_extent*volume.block_size*8u <
            volume.total_blocks) return false;
    temp.bitmap_first_block=bitmap_first;
    temp.bitmap_extent_blocks=bitmap_extent;
    temp.total_allocation_blocks=volume.total_blocks;
    temp.root_node=cat.root_node;
    temp.node_bytes=cat.node_size;
    temp.file_count=cat.leaf_records;
    if (!allocated_blocks(&temp,bitmap_first,bitmap_extent) ||
        !allocated_blocks(&temp,volume.catalog_first_block,
                           volume.catalog_extent_blocks)) return false;
    *out=temp;
    return true;
}
bool hfsplus_find_root_file(const hfsplus_mount_t *m,
                            const char *name, hfsplus_file_t *file) {
    if (!m || !m->map || !m->reader || !name || !file) return false;
    file->size=0; file->first_block=0;file->extent_blocks=0;
    file->filename[0]=0;file->file_id=0;
    size_t target_len=0;
    while (target_len<31 && name[target_len]) ++target_len;
    if (!target_len || target_len==31 || name[target_len]) return false;
    uint8_t node[512];
    uint64_t sector=m->catalog_start_sector +
                     (uint64_t)m->root_node * (m->node_bytes/512u);
    if (!read_sector(m,sector,node)) return false;
    /* Leaf node descriptor: fLink,bLink,kind(-1),height(1),records,reserved. */
    uint16_t count=be16(node+10);
    /* A depth-one tree consists of a single root leaf. Links to other
     * leaves would require traversal we do not implement: reject them. */
    if (be32(node)!=0 || be32(node+4)!=0 ||
        node[8]!=0xff || node[9]!=1 || be16(node+12)!=0 ||
        count==0 || count>16 || count!=m->file_count)
        return false;
    size_t table_start=512u-2u*((size_t)count+1u);
    size_t previous=0;
    for (unsigned i=0;i<count;++i) {
        size_t start=be16(node+510u-2u*i);
        size_t end=be16(node+508u-2u*i);
        if (start<14u || start<previous || end<=start || end>table_start)
            return false;
        previous=end;
        if (!range(end,start,8)) return false;
        size_t key_len=be16(node+start);
        uint32_t parent=be32(node+start+2);
        size_t name_len=be16(node+start+6);
        if (name_len>255 || key_len!=6u+2u*name_len ||
            !range(end,start+2,key_len))
            return false;
        bool match=parent==2 && name_len==target_len;
        if (match) {
            for (size_t j=0;j<target_len;++j) {
                uint8_t c=(uint8_t)name[j];
                if (c<32 || c>126 ||
                    be16(node+start+8u+2u*j)!=c) {
                    match=false;break;
                }
            }
        }
        if (!match) continue;
        /* HFSPlusCatalogFile begins immediately after even-length key. */
        size_t record=start+2u+key_len;
        if (!range(end,record,248) || be16(node+record)!=2) return false;
        const uint8_t *fork=node+record+88; /* dataFork */
        uint64_t logical=be64(fork);
        uint32_t total=be32(fork+12);
        uint32_t first=be32(fork+16);
        uint32_t blocks=be32(fork+20);
        if (!logical || logical>HFSPLUS_MAX_EXTRACT ||
            !total || !blocks || blocks!=total || first==0 ||
            first>=m->total_allocation_blocks ||
            blocks>m->total_allocation_blocks-first ||
            logical>(uint64_t)blocks*m->allocation_block_bytes ||
            (uint64_t)(first+blocks)*
                (m->allocation_block_bytes/512u)>m->map->sector_count)
            return false;
        if (!allocated_blocks(m,first,blocks)) return false;
        file->file_id=be32(node+record+8);
        if (file->file_id<16) return false;
        file->first_block=first;
        file->extent_blocks=blocks;
        file->size=(uint32_t)logical;
        for(size_t j=0;j<=target_len;++j)
            file->filename[j]=name[j];
        return true;
    }
    return false;
}
bool hfsplus_read_file(const hfsplus_mount_t *m,
                        const hfsplus_file_t *file,
                        uint8_t *out, size_t cap) {
    if (!m || !m->map || !m->reader || !file || !out ||
        !file->size || file->size>HFSPLUS_MAX_EXTRACT ||
        file->size>cap || !file->extent_blocks ||
        !m->allocation_block_bytes ||
        !m->total_allocation_blocks ||
        file->first_block==0 ||
        file->first_block>=m->total_allocation_blocks ||
        file->extent_blocks>m->total_allocation_blocks-file->first_block ||
        (uint64_t)file->size>
            (uint64_t)file->extent_blocks*m->allocation_block_bytes)
        return false;
    if (!allocated_blocks(m,file->first_block,file->extent_blocks))
        return false;
    uint64_t base=(uint64_t)file->first_block *
                   (m->allocation_block_bytes/512u);
    uint64_t sectors=((uint64_t)file->size+511u)/512u;
    if (base>=m->map->sector_count ||
        sectors>m->map->sector_count-base) return false;
    uint8_t sector[512];
    uint32_t written=0;
    for (uint64_t i=0;i<sectors;++i) {
        if (!read_sector(m,base+i,sector)) {
            /* Never expose a successful-looking partial extraction when
             * a later sector is unreadable or its checksum fails. */
            for (uint32_t j=0;j<written;++j) out[j]=0;
            return false;
        }
        unsigned take=file->size-written<512u ? file->size-written : 512u;
        for (unsigned j=0;j<take;++j) out[written+j]=sector[j];
        written+=take;
    }
    return written==file->size;
}
