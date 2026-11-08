#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <zlib.h>
#include "udif_blkx.h"

#define HEADER 204
#define CHUNK 40
static void b32(uint8_t *p, uint32_t n) {
    p[0] = (uint8_t)(n >> 24); p[1] = (uint8_t)(n >> 16);
    p[2] = (uint8_t)(n >> 8); p[3] = (uint8_t)n;
}
static void b64(uint8_t *p, uint64_t n) {
    b32(p, (uint32_t)(n >> 32)); b32(p + 4, (uint32_t)n);
}
static void fixture(uint8_t *b) {
    memset(b, 0, HEADER + CHUNK * 3);
    memcpy(b, "mish", 4);
    b32(b + 4, 1);               /* version */
    b64(b + 8, 5);               /* partition begins at global sector 5 */
    b64(b + 16, 3);              /* 3 logical sectors */
    b64(b + 24, 128);            /* data offset within data fork */
    b32(b + 200, 3);             /* two runs and end marker */
    b32(b + HEADER, 0);          /* first run: one zero-filled sector */
    b64(b + HEADER + 8, 0);
    b64(b + HEADER + 16, 1);
    b32(b + HEADER + CHUNK, 1);  /* second run: 2 raw sectors */
    b64(b + HEADER + CHUNK + 8, 1);
    b64(b + HEADER + CHUNK + 16, 2);
    b64(b + HEADER + CHUNK + 24, 512);
    b64(b + HEADER + CHUNK + 32, 1024);
    b32(b + HEADER + CHUNK * 2, 0xffffffffu);
    b64(b + HEADER + CHUNK * 2 + 8, 3);
}
typedef struct { uint8_t *data; size_t size; } reader_ctx_t;
static int reader(void *context, uint64_t offset, void *out, size_t length) {
    reader_ctx_t *r = (reader_ctx_t *)context;
    if (offset > r->size || length > r->size - (size_t)offset) return -1;
    memcpy(out, r->data + (size_t)offset, length);
    return (int)length;
}
static size_t b64encode(const uint8_t *in, size_t n, char *out, size_t cap) {
    const char *alphabet =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    size_t len = 0;
    for (size_t i = 0; i < n; i += 3) {
        uint32_t v = (uint32_t)in[i] << 16;
        if (i + 1 < n) v |= (uint32_t)in[i + 1] << 8;
        if (i + 2 < n) v |= in[i + 2];
        assert(len + 4 < cap);
        out[len++] = alphabet[(v >> 18) & 63];
        out[len++] = alphabet[(v >> 12) & 63];
        out[len++] = i + 1 < n ? alphabet[(v >> 6) & 63] : '=';
        out[len++] = i + 2 < n ? alphabet[v & 63] : '=';
    }
    out[len] = 0;
    return len;
}

int main(void) {
    uint8_t table[HEADER + CHUNK * 3];
    fixture(table);
    udif_block_map_t map;
    assert(udif_blkx_parse(table, sizeof(table), 64, 4096, &map));
    assert(map.sector_start == 5 && map.sector_count == 3);
    assert(map.runs_count == 2 && !map.has_compressed);

    uint8_t image[4096] = {0};
    uint64_t disk_offset = 64 + 128 + 512;
    for (unsigned i = 0; i < 1024; ++i)
        image[disk_offset + i] = (uint8_t)((i * 13u + 7u) & 255);
    reader_ctx_t reader_context = {image, sizeof(image)};
    uint8_t sector[512];
    memset(sector, 0xaa, sizeof(sector));
    assert(udif_blkx_read_sector(&map, 5, reader,
                                 &reader_context, sector) == UDIF_IO_OK);
    for (size_t i = 0; i < sizeof(sector); ++i) assert(sector[i] == 0);
    assert(udif_blkx_read_sector(&map, 6, reader,
                                 &reader_context, sector) == UDIF_IO_OK);
    assert(!memcmp(sector, image + disk_offset, 512));
    assert(udif_blkx_read_sector(&map, 7, reader,
                                 &reader_context, sector) == UDIF_IO_OK);
    assert(!memcmp(sector, image + disk_offset + 512, 512));
    assert(udif_blkx_read_sector(&map, 8, reader,
                                 &reader_context, sector) == UDIF_IO_BAD_RANGE);

    char encoded[1024];
    size_t n = b64encode(table, sizeof(table), encoded, sizeof(encoded));
    assert(n > 0);
    char xml[2048];
    int wrote = snprintf(xml, sizeof(xml),
        "<plist><dict><key>resource-fork</key><dict>"
        "<key>blkx</key><array><dict><key>Data</key><data>\n%s\n</data>"
        "</dict></array></dict></dict></plist>", encoded);
    assert(wrote > 0 && (size_t)wrote < sizeof(xml));
    assert(udif_blkx_from_xml(xml, (size_t)wrote, 64, 4096, &map));
    assert(map.runs_count == 2);
    assert(!udif_blkx_from_xml("<plist>oops</plist>", 19, 64, 4096, &map));

    /* Verify true zlib-compressed blkx data, not just raw/zero runs. */
    uint8_t original[1024];
    memcpy(original, image + disk_offset, sizeof(original));
    uLongf encoded_length = 2048;
    assert(compress2(image + disk_offset, &encoded_length,
                     original, sizeof(original), Z_BEST_COMPRESSION) == Z_OK);
    fixture(table);
    b32(table + HEADER + CHUNK, 0x80000005u);
    b64(table + HEADER + CHUNK + 32, encoded_length);
    assert(udif_blkx_parse(table, sizeof(table), 64, 4096, &map));
    assert(map.has_compressed);
    assert(udif_blkx_read_sector(&map, 6, reader, &reader_context,
                                 sector) == UDIF_IO_OK);
    assert(!memcmp(sector, original, 512));
    assert(udif_blkx_read_sector(&map, 7, reader, &reader_context,
                                 sector) == UDIF_IO_OK);
    assert(!memcmp(sector, original + 512, 512));
    image[disk_offset + (size_t)encoded_length - 1] ^= 0x40;
    assert(udif_blkx_read_sector(&map, 6, reader, &reader_context,
                                 sector) == UDIF_IO_ERROR);
    image[disk_offset + (size_t)encoded_length - 1] ^= 0x40;

    /* Other compressed UDIF algorithms still require separate decoders. */
    b32(table + HEADER + CHUNK, 0x80000006u);
    assert(udif_blkx_parse(table, sizeof(table), 64, 4096, &map));
    assert(udif_blkx_read_sector(&map, 6, reader, &reader_context,
                                 sector) == UDIF_IO_UNSUPPORTED);

    fixture(table); b64(table + HEADER + CHUNK + 24, UINT64_MAX);
    assert(!udif_blkx_parse(table, sizeof(table), 64, 4096, &map));
    fixture(table); b64(table + HEADER + CHUNK + 8, 0);
    assert(!udif_blkx_parse(table, sizeof(table), 64, 4096, &map));
    fixture(table); b64(table + HEADER + CHUNK + 32, 100);
    assert(!udif_blkx_parse(table, sizeof(table), 64, 4096, &map));
    fixture(table); b32(table + 200, 1000);
    assert(!udif_blkx_parse(table, sizeof(table), 64, 4096, &map));
    fixture(table); table[0] = 'X';
    assert(!udif_blkx_parse(table, sizeof(table), 64, 4096, &map));
    puts("UDIF blkx map/raw/zlib-sector tests: PASS");
    return 0;
}
