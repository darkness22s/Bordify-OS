#include <assert.h>
#include <stdio.h>
#include "package_inspect.h"

static void clear(uint8_t *p, size_t n) {
    for (size_t i = 0; i < n; ++i) p[i] = 0;
}
static void be16set(uint8_t *p, uint16_t x) {
    p[0] = (uint8_t)(x >> 8); p[1] = (uint8_t)x;
}
static void be32set(uint8_t *p, uint32_t x) {
    p[0] = (uint8_t)(x >> 24); p[1] = (uint8_t)(x >> 16);
    p[2] = (uint8_t)(x >> 8); p[3] = (uint8_t)x;
}
static void be64set(uint8_t *p, uint64_t x) {
    be32set(p, (uint32_t)(x >> 32)); be32set(p + 4, (uint32_t)x);
}
static void udif(uint8_t *p) {
    clear(p, 512);
    p[0] = 'k'; p[1] = 'o'; p[2] = 'l'; p[3] = 'y';
    be32set(p + 4, 4); be32set(p + 8, 512);
    be64set(p + 24, 0); be64set(p + 32, 4096);
    be64set(p + 216, 4096); be64set(p + 224, 512);
    be64set(p + 492, 8);
}
static void test_udif(void) {
    uint8_t p[512]; package_info_t info;
    udif(p);
    assert(package_inspect_dmg_trailer(p, sizeof(p), 5120, &info));
    assert(info.valid && info.format == PACKAGE_DMG_UDIF);
    assert(info.virtual_disk_bytes == 4096 && info.metadata_offset == 4096);
    assert(!package_inspect_dmg_trailer(p, 200, 5120, &info));
    assert(info.format==PACKAGE_UNKNOWN && !info.valid);
    assert(!package_inspect_dmg_trailer(p, 4, 5120, &info));
    assert(info.format==PACKAGE_UNKNOWN && !info.valid);
    assert(!package_inspect_dmg_trailer(p, sizeof(p), 500, &info));
    assert(info.format==PACKAGE_UNKNOWN && !info.valid);
    be64set(p + 224, UINT64_MAX);
    assert(package_inspect_dmg_trailer(p, sizeof(p), 5120, &info) && !info.valid);
    udif(p); be64set(p + 492, UINT64_MAX);
    assert(package_inspect_dmg_trailer(p, sizeof(p), 5120, &info) && !info.valid);
    udif(p); be32set(p + 8, 256);
    assert(package_inspect_dmg_trailer(p, sizeof(p), 5120, &info) && !info.valid);
    udif(p); p[0] = 'b';
    assert(!package_inspect_dmg_trailer(p, sizeof(p), 5120, &info));
}
static void test_xar(void) {
    uint8_t p[512]; package_info_t info;
    clear(p, sizeof(p));
    p[0] = 'x'; p[1] = 'a'; p[2] = 'r'; p[3] = '!';
    be16set(p + 4, 28); be16set(p + 6, 1);
    be64set(p + 8, 192);
    assert(package_inspect_prefix(p, sizeof(p), 512, &info));
    assert(info.valid && info.format == PACKAGE_PKG_XAR);
    assert(package_inspect_prefix(p, 20, 512, &info) && !info.valid);
    assert(package_inspect_prefix(p, sizeof(p), 200, &info) && !info.valid);
    be64set(p + 8, UINT64_MAX);
    assert(package_inspect_prefix(p, sizeof(p), 512, &info) && !info.valid);
}
static void test_ole(void) {
    uint8_t p[512]; package_info_t info;
    clear(p, sizeof(p));
    const uint8_t magic[8] = {0xd0,0xcf,0x11,0xe0,0xa1,0xb1,0x1a,0xe1};
    for (int i = 0; i < 8; ++i) p[i] = magic[i];
    p[28] = 0xfe; p[29] = 0xff; p[30] = 9; p[31] = 0;
    assert(package_inspect_prefix(p, sizeof(p), 512, &info));
    assert(info.valid && info.format == PACKAGE_OLE_CONTAINER);
    assert(package_inspect_prefix(p, 8, 512, &info) && !info.valid);
    p[28] = 0; assert(package_inspect_prefix(p, sizeof(p), 512, &info) && !info.valid);
    clear(p, sizeof(p));
    assert(!package_inspect_prefix(p, sizeof(p), 512, &info));
    assert(!package_inspect_prefix(NULL, 0, 0, &info));
    assert(!package_inspect_prefix(p, sizeof(p), 512, NULL));
}
int main(void) {
    test_udif();
    test_xar();
    test_ole();
    puts("Bordify package inspection tests: PASS");
    return 0;
}
