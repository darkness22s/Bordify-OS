#ifndef BORDIFY_UDIF_BLKX_H
#define BORDIFY_UDIF_BLKX_H

#include "types.h"

/*
 * A narrow, read-only UDIF ("mish") block-map reader.
 * Supports raw, zero/unused and bounded zlib-compressed sectors. Other
 * compression types remain UNSUPPORTED; this is not a general DMG mount.
 * This is not yet an APFS/HFS+ filesystem or a DMG installer.
 */
#define UDIF_BLKX_MAX_RUNS 128u
#define UDIF_BLKX_MAX_BINARY (204u + UDIF_BLKX_MAX_RUNS * 40u)
#define UDIF_SECTOR_SIZE 512u

typedef struct {
    uint32_t type;
    uint64_t sector;
    uint64_t sectors;
    uint64_t offset;
    uint64_t length;
} udif_block_run_t;

typedef struct {
    uint64_t sector_start;
    uint64_t sector_count;
    uint64_t data_offset;
    uint32_t runs_count;
    bool has_compressed;
    udif_block_run_t runs[UDIF_BLKX_MAX_RUNS];
} udif_block_map_t;

typedef enum {
    UDIF_IO_OK = 0,
    UDIF_IO_UNSUPPORTED,
    UDIF_IO_BAD_RANGE,
    UDIF_IO_ERROR
} udif_io_result_t;

/* Reads exactly requested bytes or returns -1. Offset is from DMG start. */
typedef int (*udif_reader_t)(void *context, uint64_t offset, void *data, size_t length);

/* Parse a decoded 'mish' table; all offsets and sizes are bounds checked. */
bool udif_blkx_parse(const uint8_t *binary, size_t size,
                     uint64_t data_fork_offset, uint64_t file_size,
                     udif_block_map_t *map);

/* Find the first blkx/Data in an XML plist, decode, parse and validate it.
   We currently support XML plists only, and do not parse binary plists. */
bool udif_blkx_from_xml(const char *xml, size_t xml_size,
                        uint64_t data_fork_offset, uint64_t file_size,
                        udif_block_map_t *map);

/* Global 512-byte logical sector read from the parsed table. */
udif_io_result_t udif_blkx_read_sector(const udif_block_map_t *map,
                                      uint64_t sector,
                                      udif_reader_t reader, void *context,
                                      uint8_t output[UDIF_SECTOR_SIZE]);
#endif
