#include "udif_blkx.h"
#include "zlib_inflate.h"

/*
 * UDIF block-map format: 204-byte big-endian 'mish' header, then
 * 40-byte runs. This module intentionally does not execute installer code.
 */
static uint32_t be32(const uint8_t *p) {
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8) | (uint32_t)p[3];
}
static uint64_t be64(const uint8_t *p) {
    return ((uint64_t)be32(p) << 32) | be32(p + 4);
}
static bool inside(uint64_t offset, uint64_t bytes, uint64_t limit) {
    return offset <= limit && bytes <= limit - offset;
}
static void zero(void *out, size_t size) {
    uint8_t *p = (uint8_t *)out;
    for (size_t i = 0; i < size; ++i) p[i] = 0;
}
static bool match_at(const char *p, size_t left, const char *s, size_t n) {
    if (left < n) return false;
    for (size_t i = 0; i < n; ++i) if (p[i] != s[i]) return false;
    return true;
}
static int base64_char(char ch) {
    if (ch >= 'A' && ch <= 'Z') return ch - 'A';
    if (ch >= 'a' && ch <= 'z') return ch - 'a' + 26;
    if (ch >= '0' && ch <= '9') return ch - '0' + 52;
    if (ch == '+') return 62;
    if (ch == '/') return 63;
    return -1;
}
static bool space(char c) {
    return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}

/* Decode without silently truncating an attacker-controlled block table. */
static bool decode_base64(const char *src, size_t n, uint8_t *dst,
                          size_t capacity, size_t *written) {
    uint32_t carry = 0;
    unsigned bits = 0;
    bool padding = false;
    size_t count = 0;
    unsigned useful = 0;
    for (size_t i = 0; i < n; ++i) {
        char ch = src[i];
        if (space(ch)) continue;
        if (ch == '=') {
            padding = true;
            continue;
        }
        int v = base64_char(ch);
        if (v < 0 || padding) return false;
        ++useful;
        carry = (carry << 6) | (uint32_t)v;
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            if (count == capacity) return false;
            dst[count++] = (uint8_t)(carry >> bits);
            carry &= (1u << bits) - 1u;
        }
    }
    if (useful < 4 || bits >= 6 || carry) return false;
    *written = count;
    return true;
}

bool udif_blkx_parse(const uint8_t *binary, size_t size,
                     uint64_t data_fork_offset, uint64_t file_size,
                     udif_block_map_t *map) {
    if (!binary || !map || size < 244 || size > UDIF_BLKX_MAX_BINARY ||
        !match_at((const char *)binary, size, "mish", 4) ||
        be32(binary + 4) != 1) return false;
    uint64_t first = be64(binary + 8);
    uint64_t count = be64(binary + 16);
    uint64_t data_start = be64(binary + 24);
    uint32_t total = be32(binary + 200);
    if (!count || total < 2 || total > UDIF_BLKX_MAX_RUNS ||
        (size_t)total > (size - 204) / 40 ||
        !inside(first, count, UINT64_MAX) ||
        !inside(data_fork_offset, data_start, file_size)) return false;

    /* Parse into a scratch map, publishing only after all checks pass. */
    udif_block_map_t temporary;
    zero(&temporary, sizeof(temporary));
    temporary.sector_start = first;
    temporary.sector_count = count;
    temporary.data_offset = data_fork_offset + data_start;
    bool terminated = false;
    uint64_t next_sector = 0;
    for (uint32_t i = 0; i < total; ++i) {
        const uint8_t *p = binary + 204 + i * 40u;
        uint32_t type = be32(p);
        uint64_t sector = be64(p + 8);
        uint64_t sectors = be64(p + 16);
        uint64_t offset = be64(p + 24);
        uint64_t length = be64(p + 32);
        if (type == 0xffffffffu) {
            if (i + 1 != total || sectors || length ||
                next_sector != count) return false;
            terminated = true;
            break;
        }
        if (type == 0x7ffffffeu) {
            if (sectors || length) return false;
            continue;
        }
        if (type != 0 && type != 1 && type != 2 &&
            type != 0x80000004u && type != 0x80000005u &&
            type != 0x80000006u && type != 0x80000007u &&
            type != 0x80000008u) return false;
        if (!sectors || sector != next_sector ||
            !inside(sector, sectors, count)) return false;
        next_sector = sector + sectors;
        if (type == 0 || type == 2) {
            if (length != 0) return false;
        } else {
            if (!inside(temporary.data_offset, offset, file_size) ||
                !inside(temporary.data_offset + offset, length, file_size)) return false;
            if (type == 1) {
                if (sectors > UINT64_MAX / UDIF_SECTOR_SIZE ||
                    length != sectors * UDIF_SECTOR_SIZE) return false;
            } else {
                temporary.has_compressed = true;
                if (length == 0) return false;
            }
        }
        udif_block_run_t *r = &temporary.runs[temporary.runs_count++];
        r->type = type;
        r->sector = sector;
        r->sectors = sectors;
        r->offset = offset;
        r->length = length;
    }
    if (!terminated) return false;
    *map = temporary;
    return true;
}

bool udif_blkx_from_xml(const char *xml, size_t xml_size,
                        uint64_t data_fork_offset, uint64_t file_size,
                        udif_block_map_t *map) {
    if (!xml || !map || xml_size < 32 || xml_size > (64u * 1024u))
        return false;

    /* Limit search to a blkx array to avoid unrelated <data> resources. */
    size_t start = xml_size;
    static const char key[] = "<key>blkx</key>";
    for (size_t i = 0; i + sizeof(key) - 1 <= xml_size; ++i) {
        if (match_at(xml + i, xml_size - i, key, sizeof(key) - 1)) {
            start = i + sizeof(key) - 1;
            break;
        }
    }
    if (start == xml_size) return false;
    static const char tag[] = "<data>";
    static const char end[] = "</data>";
    size_t data_start = xml_size, data_end = xml_size;
    for (size_t i = start; i + sizeof(tag) - 1 <= xml_size; ++i) {
        if (match_at(xml + i, xml_size - i, "</array>", 8)) break;
        if (match_at(xml + i, xml_size - i, tag, sizeof(tag) - 1)) {
            data_start = i + sizeof(tag) - 1;
            break;
        }
    }
    if (data_start == xml_size) return false;
    for (size_t i = data_start; i + sizeof(end) - 1 <= xml_size; ++i) {
        if (match_at(xml + i, xml_size - i, end, sizeof(end) - 1)) {
            data_end = i;
            break;
        }
    }
    if (data_end == xml_size) return false;

    static uint8_t decoded[UDIF_BLKX_MAX_BINARY];
    size_t count = 0;
    if (!decode_base64(xml + data_start, data_end - data_start,
                       decoded, sizeof(decoded), &count)) return false;
    return udif_blkx_parse(decoded, count, data_fork_offset,
                           file_size, map);
}

udif_io_result_t udif_blkx_read_sector(const udif_block_map_t *map,
                                      uint64_t sector,
                                      udif_reader_t reader, void *context,
                                      uint8_t output[UDIF_SECTOR_SIZE]) {
    if (!map || !reader || !output || sector < map->sector_start ||
        sector - map->sector_start >= map->sector_count)
        return UDIF_IO_BAD_RANGE;
    uint64_t relative = sector - map->sector_start;
    for (uint32_t i = 0; i < map->runs_count; ++i) {
        const udif_block_run_t *run = &map->runs[i];
        if (relative < run->sector ||
            relative - run->sector >= run->sectors) continue;
        if (run->type == 0 || run->type == 2) {
            zero(output, UDIF_SECTOR_SIZE);
            return UDIF_IO_OK;
        }
        if (run->type != 1 && run->type != 0x80000005u)
            return UDIF_IO_UNSUPPORTED;
        uint64_t relative_sector = relative - run->sector;
        if (relative_sector > UINT64_MAX / UDIF_SECTOR_SIZE)
            return UDIF_IO_BAD_RANGE;
        uint64_t within = relative_sector * UDIF_SECTOR_SIZE;
        if (run->type == 1) {
            if (!inside(run->offset, within, UINT64_MAX) ||
                !inside(map->data_offset, run->offset + within,
                        UINT64_MAX))
                return UDIF_IO_BAD_RANGE;
            uint64_t physical = map->data_offset + run->offset + within;
            return reader(context, physical, output, UDIF_SECTOR_SIZE) ==
                   (int)UDIF_SECTOR_SIZE ? UDIF_IO_OK : UDIF_IO_ERROR;
        }

        /*
         * UDIF run type 0x80000005 stores an RFC1950 zlib stream
         * holding one or more entire logical sectors. Keep the maximum
         * decompressed and compressed sizes bounded to 1 MiB each.
         * This path is read-only; no arbitrary bytes are executed.
         */
        #define UDIF_ZLIB_MAX_CHUNK (1024u * 1024u)
        static uint8_t compressed[UDIF_ZLIB_MAX_CHUNK];
        static uint8_t inflated[UDIF_ZLIB_MAX_CHUNK];
        if (run->sectors == 0 ||
            run->sectors > UDIF_ZLIB_MAX_CHUNK / UDIF_SECTOR_SIZE ||
            run->length < 6 || run->length > UDIF_ZLIB_MAX_CHUNK)
            return UDIF_IO_UNSUPPORTED;
        uint64_t expected = run->sectors * UDIF_SECTOR_SIZE;
        if (within > expected || expected - within < UDIF_SECTOR_SIZE)
            return UDIF_IO_BAD_RANGE;
        if (!inside(map->data_offset, run->offset, UINT64_MAX))
            return UDIF_IO_BAD_RANGE;
        uint64_t physical = map->data_offset + run->offset;
        if (reader(context, physical, compressed, (size_t)run->length) !=
            (int)run->length) return UDIF_IO_ERROR;
        size_t written = 0;
        if (!zlib_inflate_bounded(compressed, (size_t)run->length, inflated,
                                   (size_t)expected, &written) ||
            written != expected) return UDIF_IO_ERROR;
        for (unsigned i = 0; i < UDIF_SECTOR_SIZE; ++i)
            output[i] = inflated[(size_t)within + i];
        return UDIF_IO_OK;
    }
    return UDIF_IO_BAD_RANGE;
}
