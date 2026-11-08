#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "hfsplus_probe.h"

static void be16set(uint8_t *p, uint16_t v) {
    p[0] = (uint8_t)(v >> 8);
    p[1] = (uint8_t)v;
}
static void be32set(uint8_t *p, uint32_t v) {
    p[0] = (uint8_t)(v >> 24); p[1] = (uint8_t)(v >> 16);
    p[2] = (uint8_t)(v >> 8); p[3] = (uint8_t)v;
}
static void fixture(uint8_t h[512]) {
    memset(h, 0, 512);
    be16set(h, 0x482b); /* HFS+ */
    be16set(h + 2, 4);
    be32set(h + 4, 1u << 13); /* journaled */
    be32set(h + 32, 19); /* files */
    be32set(h + 36, 3); /* folders */
    be32set(h + 40, 4096); /* allocation block */
    be32set(h + 44, 8); /* blocks -> 64 sectors */
    be32set(h + 48, 3); /* free */
    be32set(h + 276, 8192); /* catalog logical size */
    be32set(h + 284, 2); /* catalog total blocks */
    be32set(h + 288, 2); /* first catalog extent starts */
    be32set(h + 292, 2); /* catalog extent occupies two blocks */
}
int main(void) {
    uint8_t h[512];
    hfsplus_volume_info_t info;
    fixture(h);
    assert(hfsplus_probe_header(h, sizeof(h), 64, &info));
    assert(!info.case_sensitive && info.journaled);
    assert(info.files == 19 && info.folders == 3);
    assert(info.block_size == 4096 && info.total_blocks == 8);
    assert(info.catalog_first_block == 2 && info.catalog_extent_blocks == 2);
    assert(!hfsplus_probe_header(h, 511, 64, &info));
    assert(!hfsplus_probe_header(h, sizeof(h), 32, &info));
    assert(!hfsplus_probe_header(NULL, 512, 64, &info));
    assert(!hfsplus_probe_header(h, 512, 64, NULL));
    be16set(h, 0x4858); be16set(h + 2, 5); /* HFSX */
    assert(hfsplus_probe_header(h, 512, 64, &info) && info.case_sensitive);
    be16set(h + 2, 4);
    assert(!hfsplus_probe_header(h, 512, 64, &info));
    fixture(h); be32set(h + 40, 3000); /* not power of 2 */
    assert(!hfsplus_probe_header(h, 512, 64, &info));
    fixture(h); be32set(h + 48, 9); /* free > total */
    assert(!hfsplus_probe_header(h, 512, 64, &info));
    fixture(h); be32set(h + 288, 7); be32set(h + 292, 2);
    assert(!hfsplus_probe_header(h, 512, 64, &info));
    fixture(h); be32set(h + 284, 1);
    assert(!hfsplus_probe_header(h, 512, 64, &info));
    fixture(h); h[0] = 'X';
    assert(!hfsplus_probe_header(h, 512, 64, &info));

    fixture(h);
    assert(hfsplus_probe_header(h, 512, 64, &info));
    uint64_t start = UINT64_MAX;
    assert(hfsplus_catalog_first_sector(&info, 64, &start) && start == 16);
    assert(!hfsplus_catalog_first_sector(&info, 17, &start));
    assert(!hfsplus_catalog_first_sector(NULL, 64, &start));
    uint8_t node[512] = {0};
    node[8] = 1; /* kBTHeaderNode */
    be16set(node + 10, 3);
    be16set(node + 14, 1); /* tree depth */
    be32set(node + 16, 1); /* root node */
    be32set(node + 20, 1); /* leaf records */
    be32set(node + 24, 1); /* first leaf */
    be32set(node + 28, 1); /* last leaf */
    be16set(node + 32, 512);
    be32set(node + 36, 16); /* total nodes */
    be32set(node + 40, 12); /* free nodes */
    be16set(node + 510, 14);
    be16set(node + 508, 120);
    be16set(node + 506, 248);
    be16set(node + 504, 256);
    hfsplus_catalog_info_t catalog;
    assert(hfsplus_catalog_probe(node, 512, 8192, &catalog));
    assert(catalog.root_node == 1 && catalog.total_nodes == 16);
    assert(!hfsplus_catalog_probe(node, 511, 8192, &catalog));
    assert(!hfsplus_catalog_probe(node, 512, 4096, &catalog));
    node[8] = 0; assert(!hfsplus_catalog_probe(node, 512, 8192, &catalog)); node[8] = 1;
    be32set(node + 16, 16);
    assert(!hfsplus_catalog_probe(node, 512, 8192, &catalog));
    be32set(node + 16, 1);
    be16set(node + 510, 0);
    assert(!hfsplus_catalog_probe(node, 512, 8192, &catalog));
    puts("HFS+ / HFSX volume and bounded catalog header tests: PASS");
    return 0;
}
