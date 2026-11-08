#include "package_inspect.h"

static uint16_t be16(const uint8_t *p) {
    return (uint16_t)(((uint16_t)p[0] << 8) | (uint16_t)p[1]);
}

static uint32_t be32(const uint8_t *p) {
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8) | (uint32_t)p[3];
}

static uint64_t be64(const uint8_t *p) {
    return ((uint64_t)be32(p) << 32) | be32(p + 4);
}

/* Prevent integer overflow and out-of-bounds regions. */
static bool inside(uint64_t start, uint64_t length, uint64_t bound) {
    return start <= bound && length <= bound - start;
}

static void reset(package_info_t *out) {
    out->format = PACKAGE_UNKNOWN;
    out->valid = false;
    out->data_offset = 0;
    out->data_length = 0;
    out->metadata_offset = 0;
    out->metadata_length = 0;
    out->virtual_disk_bytes = 0;
}

bool package_inspect_dmg_trailer(const void *trailer, size_t length,
                                 uint64_t file_size, package_info_t *out) {
    if (!out) return false;
    reset(out);
    if (!trailer || length < 4) return false;
    const uint8_t *p = (const uint8_t *)trailer;
    if (p[0] != 'k' || p[1] != 'o' || p[2] != 'l' || p[3] != 'y')
        return false;
    out->format = PACKAGE_DMG_UDIF;
    /* Incomplete 'koly' signatures are NOT recognized disk images. */
    if (length != 512 || file_size < 512) {
        reset(out);
        return false;
    }

    uint32_t version = be32(p + 4);
    uint32_t header_size = be32(p + 8);
    uint64_t data_offset = be64(p + 24);
    uint64_t data_length = be64(p + 32);
    uint64_t xml_offset = be64(p + 216);
    uint64_t xml_length = be64(p + 224);
    uint64_t sectors = be64(p + 492);
    uint64_t payload_end = file_size - 512;

    out->data_offset = data_offset;
    out->data_length = data_length;
    out->metadata_offset = xml_offset;
    out->metadata_length = xml_length;
    /* SectorCount * 512 must not overflow. */
    if (sectors > UINT64_MAX / 512) return true;
    out->virtual_disk_bytes = sectors * 512;

    out->valid = version == 4 && header_size == 512 &&
                 inside(data_offset, data_length, payload_end) &&
                 xml_length > 0 && inside(xml_offset, xml_length, payload_end) &&
                 sectors > 0;
    return true;
}

bool package_inspect_prefix(const void *bytes, size_t length,
                            uint64_t file_size, package_info_t *out) {
    if (!out) return false;
    reset(out);
    if (!bytes || length < 4) return false;
    const uint8_t *p = (const uint8_t *)bytes;

    if (p[0] == 'x' && p[1] == 'a' && p[2] == 'r' && p[3] == '!') {
        out->format = PACKAGE_PKG_XAR;
        if (length < 28) return true;
        uint16_t header_size = be16(p + 4);
        uint16_t version = be16(p + 6);
        uint64_t compressed_toc_length = be64(p + 8);
        out->metadata_offset = header_size;
        out->metadata_length = compressed_toc_length;
        out->valid = version == 1 && header_size >= 28 &&
                     compressed_toc_length != 0 &&
                     inside(header_size, compressed_toc_length, file_size);
        return true;
    }

    const uint8_t ole_magic[8] = {0xd0, 0xcf, 0x11, 0xe0, 0xa1, 0xb1, 0x1a, 0xe1};
    if (length >= 8) {
        bool ole = true;
        for (int i = 0; i < 8; ++i) {
            if (p[i] != ole_magic[i]) { ole = false; break; }
        }
        if (ole) {
            out->format = PACKAGE_OLE_CONTAINER;
            /* OLE magic alone does not prove this is an MSI installer. */
            if (length < 512 || file_size < 512) return true;
            uint16_t sector_shift = (uint16_t)p[30] | (uint16_t)(p[31] << 8);
            bool byte_order_ok = p[28] == 0xfe && p[29] == 0xff;
            out->valid = byte_order_ok &&
                         (sector_shift == 9 || sector_shift == 12);
            return true;
        }
    }
    return false;
}

const char *package_format_name(package_format_t format) {
    switch (format) {
        case PACKAGE_DMG_UDIF: return "Apple UDIF disk image (.DMG)";
        case PACKAGE_PKG_XAR: return "Apple XAR package (.PKG)";
        case PACKAGE_OLE_CONTAINER: return "OLE compound storage (possible MSI)";
        default: return "Unknown package";
    }
}

const char *package_compatibility_note(package_format_t format) {
    switch (format) {
        case PACKAGE_DMG_UDIF:
            return "Limited raw/zlib sector inspection exists; mounting requires full UDIF maps and APFS/HFS+.";
        case PACKAGE_PKG_XAR:
            return "PKG installation requires XAR extraction and macOS installer semantics.";
        case PACKAGE_OLE_CONTAINER:
            return "MSI installation requires package validation and Windows Installer APIs.";
        default:
            return "This package format is not supported.";
    }
}
