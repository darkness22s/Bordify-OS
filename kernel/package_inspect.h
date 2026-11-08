#ifndef BORDIFY_PACKAGE_INSPECT_H
#define BORDIFY_PACKAGE_INSPECT_H

#include "types.h"

/*
 * Package metadata inspection ONLY. No installer execution, filesystem
 * mounting, extraction or decoding of untrusted compressed payloads.
 */
typedef enum {
    PACKAGE_UNKNOWN = 0,
    PACKAGE_DMG_UDIF,
    PACKAGE_PKG_XAR,
    PACKAGE_OLE_CONTAINER
} package_format_t;

typedef struct {
    package_format_t format;
    bool valid;
    uint64_t data_offset;
    uint64_t data_length;
    uint64_t metadata_offset;
    uint64_t metadata_length;
    uint64_t virtual_disk_bytes;
} package_info_t;

/* Recognize leading-header containers (XAR/.pkg, OLE/.msi). */
bool package_inspect_prefix(const void *bytes, size_t length,
                            uint64_t file_size, package_info_t *out);

/*
 * UDIF DMG has a "koly" footer, not a leading magic.
 * Pass EXACTLY the final 512 bytes of the physical file.
 */
bool package_inspect_dmg_trailer(const void *trailer, size_t length,
                                 uint64_t file_size, package_info_t *out);

const char *package_format_name(package_format_t format);
const char *package_compatibility_note(package_format_t format);

#endif
