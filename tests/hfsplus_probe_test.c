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
    puts("HFS+ / HFSX read-only volume header tests: PASS");
    return 0;
}
