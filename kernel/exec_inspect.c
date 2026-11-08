#include "exec_inspect.h"

/* Never cast untrusted file bytes to aligned/packed host structs. */
static bool available(size_t size, size_t offset, size_t length) {
    return offset <= size && length <= size - offset;
}

static uint16_t u16le(const uint8_t *p) {
    return (uint16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8));
}

static uint32_t u32le(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static uint32_t u32be(const uint8_t *p) {
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8) | (uint32_t)p[3];
}

static uint32_t u32(const uint8_t *p, bool little) {
    return little ? u32le(p) : u32be(p);
}

static exec_arch_t pe_arch(uint16_t machine) {
    switch (machine) {
        case 0x014c: return EXEC_ARCH_X86;
        case 0x8664: return EXEC_ARCH_X86_64;
        case 0x01c0:
        case 0x01c4: return EXEC_ARCH_ARM;
        case 0xaa64: return EXEC_ARCH_ARM64;
        default: return EXEC_ARCH_UNKNOWN;
    }
}

static exec_arch_t macho_arch(uint32_t cpu) {
    switch (cpu) {
        case 7:          return EXEC_ARCH_X86;
        case 0x01000007: return EXEC_ARCH_X86_64;
        case 12:         return EXEC_ARCH_ARM;
        case 0x0100000c: return EXEC_ARCH_ARM64;
        default:         return EXEC_ARCH_UNKNOWN;
    }
}

static exec_arch_t elf_arch(uint16_t machine) {
    switch (machine) {
        case 3:   return EXEC_ARCH_X86;
        case 62:  return EXEC_ARCH_X86_64;
        case 40:  return EXEC_ARCH_ARM;
        case 183: return EXEC_ARCH_ARM64;
        default:  return EXEC_ARCH_UNKNOWN;
    }
}

bool exec_inspect(const void *data, size_t size, exec_info_t *out) {
    if (!out) return false;
    out->format = EXEC_FORMAT_UNKNOWN;
    out->arch = EXEC_ARCH_UNKNOWN;
    out->valid = false;
    if (!data || size < 2) return false;

    const uint8_t *p = (const uint8_t *)data;

    /* Windows PE executables have an MZ DOS header and a PE signature. */
    if (p[0] == 'M' && p[1] == 'Z') {
        out->format = EXEC_FORMAT_DOS;
        if (!available(size, 0, 64)) return true;
        out->valid = true; /* Could be a genuine legacy DOS executable. */
        uint32_t nt = u32le(p + 0x3c);
        if (nt < 64) return true;
        if (!available(size, nt, 4)) {
            out->format = EXEC_FORMAT_PE32;
            out->valid = false;
            return true;
        }
        if (p[nt] != 'P' || p[nt + 1] != 'E' ||
            p[nt + 2] != 0 || p[nt + 3] != 0) return true;

        out->format = EXEC_FORMAT_PE32;
        out->valid = false;
        if (!available(size, nt, 26)) return true;
        uint16_t machine = u16le(p + nt + 4);
        uint16_t sections = u16le(p + nt + 6);
        uint16_t opt_size = u16le(p + nt + 20);
        out->arch = pe_arch(machine);
        if (sections == 0 || sections > 96 || opt_size < 2 ||
            !available(size, (size_t)nt + 24, opt_size)) return true;

        uint16_t magic = u16le(p + nt + 24);
        if (magic == 0x020b) out->format = EXEC_FORMAT_PE64;
        else if (magic != 0x010b) return true;
        out->valid = true;
        return true;
    }

    if (size >= 4) {
        uint32_t magic = u32be(p);
        bool little = false;
        bool is64 = false;
        bool fat = false;
        bool mach = true;

        switch (magic) {
            case 0xfeedface: break;
            case 0xcefaedfe: little = true; break;
            case 0xfeedfacf: is64 = true; break;
            case 0xcffaedfe: little = true; is64 = true; break;
            case 0xcafebabe: fat = true; break;
            case 0xbebafeca: fat = true; little = true; break;
            case 0xcafebabf: fat = true; is64 = true; break;
            case 0xbfbafeca: fat = true; is64 = true; little = true; break;
            default: mach = false; break;
        }
        if (mach) {
            if (fat) {
                out->format = EXEC_FORMAT_MACHO_FAT;
                out->arch = EXEC_ARCH_UNIVERSAL;
                if (!available(size, 0, 8)) return true;
                uint32_t count = u32(p + 4, little);
                size_t entry_size = is64 ? 32 : 20;
                /* Bounds both table multiplication and untrusted arch count. */
                out->valid = count > 0 && count <= 128 &&
                             count <= (size - 8) / entry_size;
                return true;
            }
            out->format = is64 ? EXEC_FORMAT_MACHO64 : EXEC_FORMAT_MACHO32;
            if (!available(size, 0, is64 ? 32 : 28)) return true;
            out->arch = macho_arch(u32(p + 4, little));
            uint32_t filetype = u32(p + 12, little);
            uint32_t command_count = u32(p + 16, little);
            uint32_t command_bytes = u32(p + 20, little);
            out->valid = filetype > 0 && filetype <= 12 &&
                         command_count <= 4096 &&
                         command_count <= command_bytes / 8;
            return true;
        }

        /* ELF is useful to recognize, but is not yet a Bordify ABI. */
        if (p[0] == 0x7f && p[1] == 'E' && p[2] == 'L' && p[3] == 'F') {
            if (!available(size, 0, 5)) return true;
            out->format = p[4] == 2 ? EXEC_FORMAT_ELF64 : EXEC_FORMAT_ELF32;
            if (!available(size, 0, 20)) return true;
            uint8_t cls = p[4], endian = p[5];
            if ((cls != 1 && cls != 2) || (endian != 1 && endian != 2)) return true;
            uint16_t machine = endian == 1 ? u16le(p + 18) :
                (uint16_t)(((uint16_t)p[18] << 8) | p[19]);
            out->arch = elf_arch(machine);
            out->valid = true;
            return true;
        }
    }
    return false;
}

const char *exec_format_name(exec_format_t format) {
    switch (format) {
        case EXEC_FORMAT_DOS:       return "DOS MZ";
        case EXEC_FORMAT_PE32:      return "Windows PE32";
        case EXEC_FORMAT_PE64:      return "Windows PE32+ (64-bit)";
        case EXEC_FORMAT_MACHO32:   return "Apple Mach-O (32-bit)";
        case EXEC_FORMAT_MACHO64:   return "Apple Mach-O (64-bit)";
        case EXEC_FORMAT_MACHO_FAT: return "Apple universal/fat Mach-O";
        case EXEC_FORMAT_ELF32:     return "ELF (32-bit)";
        case EXEC_FORMAT_ELF64:     return "ELF (64-bit)";
        default:                    return "Unknown";
    }
}

const char *exec_arch_name(exec_arch_t arch) {
    switch (arch) {
        case EXEC_ARCH_X86:       return "x86 (32-bit)";
        case EXEC_ARCH_X86_64:    return "x86-64";
        case EXEC_ARCH_ARM:       return "ARM (32-bit)";
        case EXEC_ARCH_ARM64:     return "ARM64";
        case EXEC_ARCH_UNIVERSAL: return "Multiple architectures";
        default:                  return "Unknown or unsupported CPU";
    }
}

const char *exec_compatibility_note(const exec_info_t *info) {
    if (!info || !info->valid) return "Malformed or incomplete header; not safe to load.";
    switch (info->format) {
        case EXEC_FORMAT_PE32:
        case EXEC_FORMAT_PE64:
            return "Windows app execution requires process isolation, a PE loader and Win32 APIs.";
        case EXEC_FORMAT_MACHO32:
        case EXEC_FORMAT_MACHO64:
        case EXEC_FORMAT_MACHO_FAT:
            return "macOS app execution requires a Mach-O loader and Darwin/Cocoa frameworks.";
        case EXEC_FORMAT_DOS:
            return "Real-mode DOS programs require a compatible DOS runtime or emulator.";
        case EXEC_FORMAT_ELF32:
        case EXEC_FORMAT_ELF64:
            return "ELF programs require a process loader and matching system-call ABI.";
        default:
            return "Unknown executable format.";
    }
}
