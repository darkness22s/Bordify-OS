#ifndef BORDIFY_ZLIB_INFLATE_H
#define BORDIFY_ZLIB_INFLATE_H
#include "types.h"

/*
 * Bounded, freestanding RFC 1950 / RFC 1951 zlib/DEFLATE decoder.
 * Supports stored, fixed-Huffman, and dynamic-Huffman blocks.
 * Does not support preset dictionaries.
 * Caller owns source/destination, checks exact output length, and must
 * keep them non-overlapping. False means corrupt, truncated or oversize.
 */
bool zlib_inflate_bounded(const uint8_t *src, size_t src_size,
                          uint8_t *dst, size_t dst_capacity,
                          size_t *dst_written);
#endif
