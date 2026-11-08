#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <zlib.h>
#include "zlib_inflate.h"

/* The host zlib encoder is a fixture oracle only; none is linked into kernel. */
static uint8_t source[32768];
static uint8_t encoded[65536];
static uint8_t decoded[32768];
static uint32_t rng = 0x12345678u;

static uint32_t random32(void) {
    rng ^= rng << 13;
    rng ^= rng >> 17;
    rng ^= rng << 5;
    return rng;
}

static size_t encode(size_t input_len, int level, int strategy) {
    z_stream s;
    memset(&s, 0, sizeof(s));
    assert(deflateInit2(&s, level, Z_DEFLATED, MAX_WBITS, 8,
                        strategy) == Z_OK);
    s.next_in = source;
    s.avail_in = (uInt)input_len;
    s.next_out = encoded;
    s.avail_out = sizeof(encoded);
    assert(deflate(&s, Z_FINISH) == Z_STREAM_END);
    size_t used = s.total_out;
    assert(deflateEnd(&s) == Z_OK);
    return used;
}

static void roundtrip(size_t n, int level, int strategy) {
    size_t coded = encode(n, level, strategy);
    size_t written = 0;
    memset(decoded, 0x9c, sizeof(decoded));
    assert(zlib_inflate_bounded(encoded, coded, decoded, n, &written));
    assert(written == n && memcmp(decoded, source, n) == 0);

    /* Reject truncated, checksum-corrupt, and too-large outputs. */
    assert(!zlib_inflate_bounded(encoded, coded - 1, decoded, n, &written));
    assert(!zlib_inflate_bounded(encoded, coded, decoded, n - 1, &written));
    encoded[coded - 1] ^= 0x80u;
    assert(!zlib_inflate_bounded(encoded, coded, decoded, n, &written));
    encoded[coded - 1] ^= 0x80u;

    uint8_t bad_header = encoded[0];
    encoded[0] = 0;
    assert(!zlib_inflate_bounded(encoded, coded, decoded, n, &written));
    encoded[0] = bad_header;
}

int main(void) {
    for (size_t i = 0; i < sizeof(source); ++i) source[i] = 0;
    memcpy(source, "BORDIFY_DEFLATE_RAW_AND_DYNAMIC_TEST", 36);

    /* Stored DEFLATE, fixed Huffman and dynamic Huffman, including
       repeated matches and 32-KiB-window matches. */
    roundtrip(512, Z_NO_COMPRESSION, Z_DEFAULT_STRATEGY);
    roundtrip(512, Z_BEST_COMPRESSION, Z_FIXED);
    roundtrip(512, Z_BEST_COMPRESSION, Z_DEFAULT_STRATEGY);

    for (size_t i = 0; i < sizeof(source); ++i)
        source[i] = (uint8_t)("01234ABCD EFGHIJKLMNOPQRSTUVWXYZ"[
                              random32() % 33u]);
    roundtrip(512, Z_BEST_COMPRESSION, Z_DEFAULT_STRATEGY);
    roundtrip(4096, Z_BEST_COMPRESSION, Z_DEFAULT_STRATEGY);
    roundtrip(16384, Z_BEST_COMPRESSION, Z_DEFAULT_STRATEGY);
    roundtrip(32768, Z_BEST_COMPRESSION, Z_DEFAULT_STRATEGY);
    roundtrip(32768, Z_BEST_COMPRESSION, Z_FIXED);

    /* Deliberately sparse dynamic Huffman alphabets: most code lengths
     * are zero. Decoder must never address an unpopulated symbol. */
    for (size_t i = 0; i < sizeof(source); ++i)
        source[i]=(uint8_t)((i%101u)==0 ? 'Z' : ((i%13u)==0 ? 'B' : 'A'));
    roundtrip(32768,Z_BEST_COMPRESSION,Z_DEFAULT_STRATEGY);

    for (size_t i = 0; i < sizeof(source); ++i)
        source[i] = (uint8_t)random32();
    roundtrip(512, Z_BEST_COMPRESSION, Z_DEFAULT_STRATEGY);
    roundtrip(8192, Z_NO_COMPRESSION, Z_DEFAULT_STRATEGY);
    roundtrip(32768, Z_BEST_SPEED, Z_DEFAULT_STRATEGY);

    size_t used = 0;
    assert(!zlib_inflate_bounded(NULL, 0, decoded, 512, &used));
    assert(!zlib_inflate_bounded(encoded, 0, NULL, 512, &used));
    assert(!zlib_inflate_bounded(encoded, 0, decoded, 512, NULL));
    puts("zlib stored/fixed/dynamic Huffman and corruption tests: PASS");
    return 0;
}
