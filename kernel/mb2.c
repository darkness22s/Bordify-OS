#include "mb2.h"
#include "printf.h"
#include "string.h"

typedef struct {
    uint32_t total_size;
    uint32_t reserved;
    uint8_t tags[];
} mb2_header_t;

typedef struct {
    uint32_t type;
    uint32_t size;
} mb2_tag_t;

typedef struct {
    uint32_t type;
    uint32_t size;
    uint32_t mem_lower;
    uint32_t mem_upper;
} mb2_tag_basic_mem_t;

typedef struct {
    uint32_t type;
    uint32_t size;
    uint64_t address;
    uint32_t pitch;
    uint32_t width;
    uint32_t height;
    uint8_t bpp;
    uint8_t fb_type;
    uint16_t reserved;
} mb2_tag_framebuffer_t;

typedef struct {
    uint32_t type;
    uint32_t size;
    uint32_t entry_size;
    uint32_t entry_version;
    uint8_t entries[];
} mb2_tag_mmap_t;

typedef struct {
    uint64_t base;
    uint64_t length;
    uint32_t type;
    uint32_t reserved;
} mb2_mmap_entry_t;

typedef struct {
    uint32_t type;
    uint32_t size;
    char string[];
} mb2_tag_string_t;

typedef struct {
    uint32_t type;
    uint32_t size;
    char string[];
} mb2_tag_loader_t;

static mb2_info_t info;

void mb2_parse(uintptr_t info_address) {
    kmemset(&info, 0, sizeof(info));
    if (!info_address) {
        return;
    }
    const mb2_header_t *header = (const mb2_header_t *)info_address;
    uintptr_t cursor = info_address + 8;
    uintptr_t end = info_address + header->total_size;

    while (cursor + sizeof(mb2_tag_t) <= end) {
        const mb2_tag_t *tag = (const mb2_tag_t *)cursor;
        if (tag->type == 0 || tag->size < 8) {
            break;
        }
        switch (tag->type) {
            case 1: {
                const mb2_tag_string_t *s = (const mb2_tag_string_t *)tag;
                info.cmdline = s->string;
                break;
            }
            case 2: {
                const mb2_tag_loader_t *s = (const mb2_tag_loader_t *)tag;
                info.bootloader = s->string;
                break;
            }
            case 4: {
                const mb2_tag_basic_mem_t *mem = (const mb2_tag_basic_mem_t *)tag;
                info.mem_lower_kb = mem->mem_lower;
                info.mem_upper_kb = mem->mem_upper;
                break;
            }
            case 6: {
                const mb2_tag_mmap_t *mmap = (const mb2_tag_mmap_t *)tag;
                uintptr_t entry = (uintptr_t)mmap->entries;
                uintptr_t entries_end = cursor + tag->size;
                uint64_t highest = 0;
                while (entry + mmap->entry_size <= entries_end) {
                    const mb2_mmap_entry_t *e = (const mb2_mmap_entry_t *)entry;
                    if (e->type == 1 && e->base + e->length > highest) {
                        highest = e->base + e->length;
                    }
                    entry += mmap->entry_size;
                }
                if (highest > 0x100000ULL) {
                    info.memory_size_kb = (uint32_t)(highest >> 10);
                }
                break;
            }
            case 8: {
                const mb2_tag_framebuffer_t *fb = (const mb2_tag_framebuffer_t *)tag;
                info.framebuffer_addr = (uint32_t)fb->address;
                info.framebuffer_pitch = fb->pitch;
                info.framebuffer_width = fb->width;
                info.framebuffer_height = fb->height;
                info.framebuffer_bpp = fb->bpp;
                info.framebuffer_type = fb->fb_type;
                info.framebuffer_present = true;
                kprintf("[mb2] framebuffer 0x%08x %ux%u %u bpp pitch %u\n",
                        (uint32_t)fb->address, fb->width, fb->height, fb->bpp, fb->pitch);
                break;
            }
            default:
                break;
        }
        cursor = ALIGN_UP(cursor + tag->size, 8);
    }

    if (!info.memory_size_kb) {
        info.memory_size_kb = info.mem_lower_kb + info.mem_upper_kb;
    }
    kprintf("[mb2] bootloader: %s\n", info.bootloader ? info.bootloader : "unknown");
    kprintf("[mb2] memory: %u KiB\n", info.memory_size_kb);
}

const mb2_info_t *mb2_info(void) {
    return &info;
}
