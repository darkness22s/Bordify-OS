#include "zlib_inflate.h"

/* Compact bounded DEFLATE decoder, no libc, heap or floating point.
 * RFC 1950 (zlib wrapper), RFC 1951 (DEFLATE bit stream).
 * All Huffman lengths, output copies and input reads are validated.
 */

typedef struct {
    const uint8_t *src;
    size_t size;
    size_t at;
    uint32_t bits;
    unsigned count;
} bits_t;

typedef struct {
    uint16_t count[16];       /* number of symbols for each code length */
    uint16_t symbols[288];    /* canonical code order, by length */
    uint16_t symbol_count;  /* count of initialized nonzero symbols */
} huff_t;

static bool read_bits(bits_t *b, unsigned n, uint32_t *value) {
    if (n > 16) return false;
    while (b->count < n) {
        if (b->at == b->size) return false;
        b->bits |= (uint32_t)b->src[b->at++] << b->count;
        b->count += 8;
    }
    *value = b->bits & ((1u << n) - 1u);
    b->bits >>= n;
    b->count -= n;
    return true;
}

static bool make_tree(const uint8_t *lengths, unsigned n, huff_t *tree) {
    uint16_t next[16] = {0};
    for (unsigned i = 0; i < 16; ++i) tree->count[i] = 0;
    tree->symbol_count = 0;
    for (unsigned i=0;i<288u;++i) tree->symbols[i]=0;
    if (n > 288) return false;
    for (unsigned symbol = 0; symbol < n; ++symbol) {
        if (lengths[symbol] > 15) return false;
        ++tree->count[lengths[symbol]];
    }

    /* A full tree is not required for a single-symbol distance alphabet. */
    int32_t remaining = 1;
    for (unsigned len = 1; len <= 15; ++len) {
        remaining = (remaining << 1) - tree->count[len];
        if (remaining < 0) return false;
    }
    /* Zero-length symbols are absent, NOT part of the canonical offsets. */
    tree->symbol_count = (uint16_t)(n - tree->count[0]);
    next[1] = 0;
    for (unsigned len = 2; len <= 15; ++len)
        next[len] = (uint16_t)(next[len - 1] + tree->count[len - 1]);

    for (unsigned symbol = 0; symbol < n; ++symbol) {
        unsigned len = lengths[symbol];
        if (len != 0) {
            uint16_t pos = next[len]++;
            if (pos >= tree->symbol_count || pos >= 288u) return false;
            tree->symbols[pos] = (uint16_t)symbol;
        }
    }
    return true;
}

/* Decode a canonical Huffman code MSB-first from LSB-packed deflate bits. */
static bool decode(bits_t *b, const huff_t *tree, uint16_t *symbol) {
    unsigned code = 0, first = 0, index = 0;
    for (unsigned len = 1; len <= 15; ++len) {
        uint32_t bit = 0;
        if (!read_bits(b, 1, &bit)) return false;
        code |= bit;
        unsigned count = tree->count[len];
        if (code >= first && code - first < count) {
            if (index + code - first >= tree->symbol_count) return false;
            *symbol = tree->symbols[index + code - first];
            return true;
        }
        index += count;
        first = (first + count) << 1;
        code <<= 1;
    }
    return false;
}

static bool fixed_trees(huff_t *literal, huff_t *distance) {
    uint8_t ll[288], dd[32];
    for (unsigned i = 0; i <= 143; ++i) ll[i] = 8;
    for (unsigned i = 144; i <= 255; ++i) ll[i] = 9;
    for (unsigned i = 256; i <= 279; ++i) ll[i] = 7;
    for (unsigned i = 280; i <= 287; ++i) ll[i] = 8;
    for (unsigned i = 0; i < 32; ++i) dd[i] = 5;
    return make_tree(ll, 288, literal) && make_tree(dd, 32, distance);
}

static bool dynamic_trees(bits_t *b, huff_t *literal, huff_t *distance) {
    static const uint8_t order[19] = {
        16,17,18,0,8,7,9,6,10,5,11,4,12,3,13,2,14,1,15
    };
    uint32_t nlit, ndist, ncode;
    if (!read_bits(b, 5, &nlit) ||
        !read_bits(b, 5, &ndist) ||
        !read_bits(b, 4, &ncode)) return false;
    nlit += 257;
    ndist += 1;
    ncode += 4;
    if (nlit > 286 || ndist > 32) return false;
    uint8_t lens[320] = {0};
    uint8_t clens[19] = {0};
    for (unsigned i = 0; i < ncode; ++i) {
        uint32_t val;
        if (!read_bits(b, 3, &val)) return false;
        clens[order[i]] = (uint8_t)val;
    }
    huff_t ctree;
    if (!make_tree(clens, 19, &ctree)) return false;

    unsigned count = nlit + ndist, i = 0;
    while (i < count) {
        uint16_t sym;
        if (!decode(b, &ctree, &sym)) return false;
        if (sym < 16) {
            lens[i++] = (uint8_t)sym;
            continue;
        }
        uint32_t extra, repeats;
        uint8_t val;
        if (sym == 16) {
            if (i == 0 || !read_bits(b, 2, &extra)) return false;
            repeats = extra + 3;
            val = lens[i - 1];
        } else if (sym == 17) {
            if (!read_bits(b, 3, &extra)) return false;
            repeats = extra + 3;
            val = 0;
        } else if (sym == 18) {
            if (!read_bits(b, 7, &extra)) return false;
            repeats = extra + 11;
            val = 0;
        } else return false;
        if (repeats > count - i) return false;
        while (repeats--) lens[i++] = val;
    }
    /* End-of-block marker is mandatory. */
    if (lens[256] == 0) return false;
    return make_tree(lens, nlit, literal) &&
           make_tree(lens + nlit, ndist, distance);
}

static bool huffman_block(bits_t *b, const huff_t *lit,
                          const huff_t *dist,
                          uint8_t *dst, size_t capacity, size_t *used) {
    static const uint16_t length_base[29] = {
        3,4,5,6,7,8,9,10,11,13,15,17,19,23,27,31,
        35,43,51,59,67,83,99,115,131,163,195,227,258
    };
    static const uint8_t length_extra[29] = {
        0,0,0,0,0,0,0,0,1,1,1,1,2,2,2,2,
        3,3,3,3,4,4,4,4,5,5,5,5,0
    };
    static const uint16_t distance_base[30] = {
        1,2,3,4,5,7,9,13,17,25,33,49,65,97,129,193,
        257,385,513,769,1025,1537,2049,3073,4097,
        6145,8193,12289,16385,24577
    };
    static const uint8_t distance_extra[30] = {
        0,0,0,0,1,1,2,2,3,3,4,4,5,5,6,6,
        7,7,8,8,9,9,10,10,11,11,12,12,13,13
    };

    for (;;) {
        uint16_t sym;
        if (!decode(b, lit, &sym)) return false;
        if (sym < 256) {
            if (*used == capacity) return false;
            dst[(*used)++] = (uint8_t)sym;
        } else if (sym == 256) {
            return true;
        } else {
            if (sym < 257 || sym > 285) return false;
            unsigned index = sym - 257;
            uint32_t extra = 0;
            if (!read_bits(b, length_extra[index], &extra)) return false;
            uint32_t length = length_base[index] + extra;
            uint16_t d;
            if (!decode(b, dist, &d) || d >= 30) return false;
            if (!read_bits(b, distance_extra[d], &extra)) return false;
            uint32_t backwards = distance_base[d] + extra;
            if (!backwards || backwards > *used ||
                length > capacity - *used) return false;
            for (uint32_t n = 0; n < length; ++n) {
                dst[*used] = dst[*used - backwards];
                ++*used;
            }
        }
    }
}

static bool deflate(const uint8_t *src, size_t src_len,
                    uint8_t *dst, size_t capacity, size_t *out_size) {
    bits_t b = {src, src_len, 0, 0, 0};
    size_t produced = 0;
    bool last = false;
    do {
        uint32_t final_bit, type;
        if (!read_bits(&b, 1, &final_bit) ||
            !read_bits(&b, 2, &type)) return false;
        last = final_bit != 0;
        if (type == 0) {
            unsigned remainder = b.count & 7u;
            b.bits >>= remainder;
            b.count -= remainder;
            uint32_t length, check;
            if (!read_bits(&b, 16, &length) ||
                !read_bits(&b, 16, &check) ||
                (length ^ 65535u) != check ||
                length > capacity - produced) return false;
            for (uint32_t i = 0; i < length; ++i) {
                uint32_t c;
                if (!read_bits(&b, 8, &c)) return false;
                dst[produced++] = (uint8_t)c;
            }
        } else if (type == 1 || type == 2) {
            huff_t lit, dist;
            if (type == 1 ? !fixed_trees(&lit, &dist) :
                            !dynamic_trees(&b, &lit, &dist)) return false;
            if (!huffman_block(&b, &lit, &dist, dst, capacity, &produced))
                return false;
        } else return false;
    } while (!last);
    *out_size = produced;
    return true;
}

static uint32_t adler32(const uint8_t *data, size_t size) {
    uint32_t s1 = 1, s2 = 0;
    for (size_t i = 0; i < size; ++i) {
        s1 += data[i];
        s2 += s1;
        if ((i & 0xfffu) == 0xfffu) {
            s1 %= 65521u;
            s2 %= 65521u;
        }
    }
    s1 %= 65521u;
    s2 %= 65521u;
    return (s2 << 16) | s1;
}

bool zlib_inflate_bounded(const uint8_t *src, size_t src_size,
                          uint8_t *dst, size_t dst_capacity,
                          size_t *dst_written) {
    if (!dst_written) return false;
    *dst_written = 0;
    if (!src || !dst || src_size < 6 || !dst_capacity)
        return false;
    uint8_t cmf = src[0], flg = src[1];
    if ((cmf & 15u) != 8 || (cmf >> 4) > 7 ||
        (((unsigned)cmf << 8) | flg) % 31u != 0 ||
        (flg & 0x20u)) return false; /* no preset dictionary */

    size_t size = 0;
    if (!deflate(src + 2, src_size - 6, dst, dst_capacity, &size))
        return false;
    size_t i = src_size - 4;
    uint32_t check = ((uint32_t)src[i] << 24) |
                     ((uint32_t)src[i + 1] << 16) |
                     ((uint32_t)src[i + 2] << 8) | src[i + 3];
    if (adler32(dst, size) != check) return false;
    *dst_written = size;
    return true;
}
