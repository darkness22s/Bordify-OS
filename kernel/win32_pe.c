#include "win32_pe.h"

/*
 * A deliberately small, independently implemented Win32 compatibility
 * nucleus. It is NOT Wine and does not implement Win32 installer services.
 *
 * Supported kernel32.dll imports:
 *   ExitProcess(uExitCode), GetStdHandle(STD_OUTPUT_HANDLE),
 *   WriteFile, GetLastError, CreateFileA, ReadFile and CloseHandle.
 *   Only a bounded, fixed Windows path is backed by FAT16.
 *
 * The bridges below reside INSIDE user memory and use Bordify int 0x80;
 * no imported Windows instruction runs at CPL0.
 */
#define MAX_SECTIONS 16
#define MAX_IMPORTS 32
#define BRIDGE_SLOT 64u

static uint16_t rd16(const uint8_t *p) {
    return (uint16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8));
}
static uint32_t rd32(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}
static void wr32(uint8_t *p, uint32_t v) {
    p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24);
}
static bool fits(size_t size, size_t at, size_t n) {
    return at <= size && n <= size - at;
}
static void clear_bytes(uint8_t *p, size_t n) {
    for (size_t i = 0; i < n; ++i) p[i] = 0;
}
static void copy_bytes(uint8_t *p, const uint8_t *src, size_t n) {
    for (size_t i = 0; i < n; ++i) p[i] = src[i];
}
static bool ascii_eq(const char *a, const char *b, size_t limit) {
    for (size_t i = 0; i < limit; ++i) {
        char x = a[i], y = b[i];
        if (x >= 'A' && x <= 'Z') x = (char)(x + 32);
        if (y >= 'A' && y <= 'Z') y = (char)(y + 32);
        if (x != y) return false;
        if (!x) return true;
    }
    return false;
}
static bool name_in_image(const uint8_t *image, uint32_t image_size,
                          uint32_t rva, const char *expected) {
    if (rva >= image_size) return false;
    /* Every expected name fits in 48 bytes; require a terminated string. */
    size_t allowed = image_size - rva;
    return ascii_eq((const char *)(image + rva), expected,
                    allowed < 48 ? allowed : 48);
}
static uint32_t bridge_address(const uint8_t *image, uint32_t size,
                               uint32_t name_rva) {
    if (name_in_image(image, size, name_rva, "ExitProcess"))
        return WIN32_PE_USER_BASE + WIN32_PE_RUNTIME_OFFSET;
    if (name_in_image(image, size, name_rva, "GetStdHandle"))
        return WIN32_PE_USER_BASE + WIN32_PE_RUNTIME_OFFSET + BRIDGE_SLOT;
    if (name_in_image(image, size, name_rva, "WriteFile"))
        return WIN32_PE_USER_BASE + WIN32_PE_RUNTIME_OFFSET + BRIDGE_SLOT * 2u;
    if (name_in_image(image, size, name_rva, "GetLastError"))
        return WIN32_PE_USER_BASE + WIN32_PE_RUNTIME_OFFSET + BRIDGE_SLOT * 3u;
    if (name_in_image(image,size,name_rva,"CreateFileA"))
        return WIN32_PE_USER_BASE + WIN32_PE_RUNTIME_OFFSET + BRIDGE_SLOT * 4u;
    if (name_in_image(image,size,name_rva,"ReadFile"))
        return WIN32_PE_USER_BASE + WIN32_PE_RUNTIME_OFFSET + BRIDGE_SLOT * 5u;
    if (name_in_image(image,size,name_rva,"CloseHandle"))
        return WIN32_PE_USER_BASE + WIN32_PE_RUNTIME_OFFSET + BRIDGE_SLOT * 6u;
    return 0;
}

static bool fix_imports(uint8_t *memory, uint32_t image_size,
                        uint32_t import_rva, uint32_t import_size,
                        uint32_t *count) {
    *count = 0;
    /* An import-free EXE can be admitted, but the Win32 demo uses kernel32. */
    if (import_rva == 0 && import_size == 0) return true;
    if (!import_rva || !import_size ||
        !fits(image_size, import_rva, import_size) ||
        import_size < 40) return false;

    uint32_t descriptors = import_size / 20u;
    if (descriptors > 16) return false;
    bool terminated = false;
    for (uint32_t i = 0; i < descriptors; ++i) {
        uint32_t desc = import_rva + i * 20u;
        uint32_t lookup = rd32(memory + desc);
        uint32_t name = rd32(memory + desc + 12);
        uint32_t iat = rd32(memory + desc + 16);
        if (!lookup && !name && !iat) {
            terminated = true;
            break;
        }
        if (!name_in_image(memory, image_size, name, "kernel32.dll") ||
            !lookup || !iat) return false;

        bool found_end = false;
        for (uint32_t j = 0; j < MAX_IMPORTS; ++j) {
            uint32_t delta = j * 4u;
            if (!fits(image_size, lookup, delta + 4) ||
                !fits(image_size, iat, delta + 4)) return false;
            uint32_t value = rd32(memory + lookup + delta);
            if (value == 0) { found_end = true; break; }
            if (value & 0x80000000u) return false; /* ordinal imports omitted */
            if (!fits(image_size, value, 3)) return false;
            uint32_t address = bridge_address(memory, image_size, value + 2u);
            if (!address) return false; /* missing Win32 API */
            wr32(memory + iat + delta, address);
            ++*count;
        }
        if (!found_end) return false;
    }
    return terminated;
}

bool win32_pe_stage(const uint8_t *file, size_t file_size,
                    uint8_t *memory, size_t memory_size,
                    win32_pe_info_t *info) {
    if (!file || !memory || !info || memory_size < WIN32_PE_MAX_IMAGE) return false;
    info->entry = 0;
    info->image_size = 0;
    info->imported_functions = 0;

    if (!fits(file_size, 0, 64) || file[0] != 'M' || file[1] != 'Z')
        return false;
    size_t nt = rd32(file + 0x3c);
    if (nt < 64 || !fits(file_size, nt, 24)) return false;
    if (file[nt] != 'P' || file[nt + 1] != 'E' ||
        file[nt + 2] || file[nt + 3]) return false;
    uint16_t machine = rd16(file + nt + 4);
    uint16_t section_count = rd16(file + nt + 6);
    uint16_t optional_size = rd16(file + nt + 20);
    uint16_t flags = rd16(file + nt + 22);
    if (machine != 0x014c || section_count == 0 ||
        section_count > MAX_SECTIONS || optional_size < 224 ||
        !(flags & 0x0002) || !fits(file_size, nt + 24, optional_size)) return false;

    size_t opt = nt + 24;
    if (rd16(file + opt) != 0x010b || rd32(file + opt + 28) != WIN32_PE_USER_BASE ||
        rd16(file + opt + 68) != 3 || /* IMAGE_SUBSYSTEM_WINDOWS_CUI */
        rd32(file + opt + 92) < 16) return false;
    uint32_t entry_rva = rd32(file + opt + 16);
    uint32_t image_size = rd32(file + opt + 56);
    uint32_t headers_size = rd32(file + opt + 60);
    uint32_t import_rva = rd32(file + opt + 104);
    uint32_t import_size = rd32(file + opt + 108);
    /* Reject TLS and delayed imports until those loaders exist. */
    if (rd32(file + opt + 96 + 9u * 8u) ||
        rd32(file + opt + 96 + 13u * 8u)) return false;
    if (image_size < 0x2000 || image_size > WIN32_PE_MAX_IMAGE ||
        !entry_rva || entry_rva >= image_size ||
        headers_size < opt + optional_size || headers_size > image_size ||
        !fits(file_size, 0, headers_size)) return false;

    size_t sections = opt + optional_size;
    if (!fits(file_size, sections, (size_t)section_count * 40u)) return false;
    bool executable_entry = false;

    /* Validate complete mapping before copying untrusted sections. */
    for (uint32_t i = 0; i < section_count; ++i) {
        const uint8_t *s = file + sections + i * 40u;
        uint32_t virtual_size = rd32(s + 8);
        uint32_t virtual_rva = rd32(s + 12);
        uint32_t raw_size = rd32(s + 16);
        uint32_t raw_offset = rd32(s + 20);
        uint32_t characteristics = rd32(s + 36);
        uint32_t mapped_size = virtual_size > raw_size ? virtual_size : raw_size;
        if (!mapped_size || !fits(image_size, virtual_rva, mapped_size) ||
            virtual_rva < headers_size ||
            !fits(file_size, raw_offset, raw_size)) return false;
        if ((characteristics & 0x20000000u) &&
            entry_rva >= virtual_rva &&
            entry_rva - virtual_rva < mapped_size) executable_entry = true;
        for (uint32_t j = 0; j < i; ++j) {
            const uint8_t *prev = file + sections + j * 40u;
            uint32_t v = rd32(prev + 12);
            uint32_t n = rd32(prev + 8);
            uint32_t r = rd32(prev + 16);
            if (r > n) n = r;
            if (virtual_rva < v + n && v < virtual_rva + mapped_size)
                return false;
        }
    }
    if (!executable_entry) return false;

    clear_bytes(memory, memory_size);
    copy_bytes(memory, file, headers_size);
    for (uint32_t i = 0; i < section_count; ++i) {
        const uint8_t *s = file + sections + i * 40u;
        uint32_t virtual_rva = rd32(s + 12);
        uint32_t raw_size = rd32(s + 16);
        uint32_t raw_offset = rd32(s + 20);
        copy_bytes(memory + virtual_rva, file + raw_offset, raw_size);
    }

    uint32_t imported = 0;
    if (!fix_imports(memory, image_size, import_rva, import_size, &imported))
        return false;
    info->entry = WIN32_PE_USER_BASE + entry_rva;
    info->image_size = image_size;
    info->imported_functions = imported;
    return true;
}

bool win32_pe_install_bridges(uint8_t *memory, size_t size) {
    if (!memory || !fits(size,WIN32_PE_RUNTIME_OFFSET,BRIDGE_SLOT*7u))
        return false;
    /* Tiny x86 stdcall stubs assembled and inspected for exact calling
     * conventions. Every syscall is invoked from the guest's ring-3
     * mapping; kernel does not expose executable dispatch pointers.
     */
    static const uint8_t exit_process[]={
        0x8b,0x5c,0x24,0x04,0xb8,0x02,0,0,0,0xcd,0x80,0x0f,0x0b
    };
    static const uint8_t get_std_handle[]={
        0xb8,0x01,0,0,0,0xc2,0x04,0
    };
    /* WriteFile dispatches the pseudo stdout handle 1 to the existing
     * console service, and WIN32_FILE_HANDLE to the bounded file writer.
     * The Win32 lpNumberOfBytesWritten output is populated when non-NULL.
     */
    static const uint8_t write_file[]={
        0x53,0x8b,0x5c,0x24,0x08,0x8b,0x4c,0x24,0x0c,
        0x8b,0x54,0x24,0x10,0x83,0xfb,0x01,0x75,0x09,
        0xb8,0x01,0,0,0,0xcd,0x80,0xeb,0x07,
        0xb8,0x04,0,0,0,0xcd,0x80,0x83,0xf8,0xff,
        0x74,0x11,0x8b,0x54,0x24,0x14,0x85,0xd2,
        0x74,0x02,0x89,0x02,0xb8,0x01,0,0,0,
        0xeb,0x02,0x31,0xc0,0x5b,0xc2,0x14,0
    };
    static const uint8_t get_last_error[]={
        0xb8,0x07,0,0,0,0xcd,0x80,0xc3
    };
    static const uint8_t create_file[]={
        0x53,0x8b,0x5c,0x24,0x08,0x8b,0x4c,0x24,0x0c,
        0x8b,0x54,0x24,0x18,0xb8,0x03,0,0,0,
        0xcd,0x80,0x5b,0xc2,0x1c,0
    };
    static const uint8_t read_file[]={
        0x53,0x8b,0x5c,0x24,0x08,0x8b,0x4c,0x24,0x0c,
        0x8b,0x54,0x24,0x10,0xb8,0x05,0,0,0,
        0xcd,0x80,0x83,0xf8,0xff,0x74,0x11,
        0x8b,0x54,0x24,0x14,0x85,0xd2,0x74,0x02,
        0x89,0x02,0xb8,0x01,0,0,0,0xeb,0x02,
        0x31,0xc0,0x5b,0xc2,0x14,0
    };
    static const uint8_t close_handle[]={
        0x53,0x8b,0x5c,0x24,0x08,0xb8,0x06,0,0,0,
        0xcd,0x80,0x5b,0xc2,0x04,0
    };
    static const uint8_t *const code[7]={
        exit_process,get_std_handle,write_file,get_last_error,
        create_file,read_file,close_handle
    };
    static const size_t lengths[7]={
        sizeof(exit_process),sizeof(get_std_handle),
        sizeof(write_file),sizeof(get_last_error),
        sizeof(create_file),sizeof(read_file),sizeof(close_handle)
    };
    for(size_t i=0;i<7;++i){
        if(lengths[i]>BRIDGE_SLOT) return false;
        uint8_t *dest=memory+WIN32_PE_RUNTIME_OFFSET+i*BRIDGE_SLOT;
        clear_bytes(dest,BRIDGE_SLOT);
        copy_bytes(dest,code[i],lengths[i]);
    }
    return true;
}
