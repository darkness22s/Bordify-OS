#include "process.h"
#include "gdt.h"
#include "heap.h"
#include "fat16.h"
#include "serial.h"
#include "printf.h"
#include "string.h"
#include "win32_pe.h"
#include "win32_files.h"

#define PAGE_PRESENT 0x001u
#define PAGE_RW      0x002u
#define PAGE_USER    0x004u
#define PAGE_LARGE   0x080u
#define PROCESS_USER_PAGES (PROCESS_USER_BYTES / 4096u)
#define PROCESS_MAX_ELF_BYTES (64u * 1024u)
#define PROCESS_STACK_RESERVE (16u * 1024u)

typedef struct {
    uint8_t ident[16];
    uint16_t type;
    uint16_t machine;
    uint32_t version;
    uint32_t entry;
    uint32_t phoff;
    uint32_t shoff;
    uint32_t flags;
    uint16_t ehsize;
    uint16_t phentsize;
    uint16_t phnum;
    uint16_t shentsize;
    uint16_t shnum;
    uint16_t shstrndx;
} PACKED elf_header_t;

typedef struct {
    uint32_t type;
    uint32_t offset;
    uint32_t vaddr;
    uint32_t paddr;
    uint32_t filesz;
    uint32_t memsz;
    uint32_t flags;
    uint32_t align;
} PACKED elf_segment_t;

static bool ready;
static bool running;
static int exit_status;
static bool active_win32;
static win32_files_t win32_files;
/* The Windows guest never supplies an arbitrary FAT16 destination. */
static int win32_load_file(uint8_t *out,size_t capacity,void *context) {
    (void)context;
    fat_entry_t apps,entry;
    if (!fat16_mounted() || !fat16_find(0,"APPS",&apps) ||
        !fat16_find(apps.cluster,"WIN32.TXT",&entry) ||
        (entry.attr & FAT_ATTR_DIRECTORY) ||
        entry.size>capacity) return -1;
    return fat16_read(apps.cluster,"WIN32.TXT",out,(uint32_t)capacity);
}
static bool win32_save_file(const uint8_t *data,size_t count,void *context) {
    (void)context;
    fat_entry_t apps,entry;
    if (!fat16_mounted() || !fat16_find(0,"APPS",&apps) ||
        !(apps.attr & FAT_ATTR_DIRECTORY) ||
        fat16_write(apps.cluster,"WIN32.TXT",data,(uint32_t)count)!=(int)count ||
        !fat16_find(apps.cluster,"WIN32.TXT",&entry) ||
        entry.size!=(uint32_t)count) return false;
    return true;
}
static uint32_t *page_directory;
static uint32_t *user_page_table;
static uint8_t *user_memory;

/* Assembly switches to ring 3 via iret and returns when process_leave runs. */
extern void process_enter(uint32_t entry, uint32_t user_stack);
extern NORETURN void process_leave(void);

static bool user_range(uint32_t address, uint32_t length) {
    return address >= PROCESS_USER_BASE &&
           address - PROCESS_USER_BASE <= PROCESS_USER_BYTES &&
           length <= PROCESS_USER_BYTES - (address - PROCESS_USER_BASE);
}

static bool cpu_supports_pse(void) {
    uint32_t a, b, c, d;
    __asm__ volatile("cpuid"
                     : "=a"(a), "=b"(b), "=c"(c), "=d"(d)
                     : "a"(0));
    if (a < 1) return false;
    __asm__ volatile("cpuid"
                     : "=a"(a), "=b"(b), "=c"(c), "=d"(d)
                     : "a"(1));
    return (d & (1u << 3)) != 0;
}

void process_init(void) {
    if (ready) return;
    if (!cpu_supports_pse()) {
        kprintf("[process] CPU does not support 4 MiB pages; user mode unavailable\n");
        return;
    }

    page_directory = (uint32_t *)kmalloc_aligned(4096, 4096);
    user_page_table = (uint32_t *)kmalloc_aligned(4096, 4096);
    user_memory = (uint8_t *)kmalloc_aligned(PROCESS_USER_BYTES, 4096);
    /* The bump allocator does not free partial allocations; fail closed
     * without touching any of the tables or enabling ring-3 paging. */
    if (!page_directory || !user_page_table || !user_memory) {
        kprintf("[process] user-mode allocation failed; paging disabled\n");
        page_directory = NULLPTR;
        user_page_table = NULLPTR;
        user_memory = NULLPTR;
        ready = false;
        return;
    }

    /*
     * Identity-map the 32-bit address space for the existing kernel, using
     * supervisor-only 4 MiB pages. MMIO and the GRUB framebuffer therefore
     * retain their addresses. The single user PDE replaces the 1-GiB
     * identity mapping with only 256 KiB of explicitly user-accessible RAM.
     * This is a transitional design, NOT mature VM management.
     */
    for (uint32_t i = 0; i < 1024; ++i) {
        page_directory[i] = (i << 22) | PAGE_PRESENT | PAGE_RW | PAGE_LARGE;
    }
    kmemset(user_page_table, 0, 4096);
    kmemset(user_memory, 0, PROCESS_USER_BYTES);
    for (uint32_t i = 0; i < PROCESS_USER_PAGES; ++i) {
        user_page_table[i] =
            (uint32_t)(uintptr_t)(user_memory + i * 4096) |
            PAGE_PRESENT | PAGE_RW | PAGE_USER;
    }
    page_directory[PROCESS_USER_BASE >> 22] =
        (uint32_t)(uintptr_t)user_page_table |
        PAGE_PRESENT | PAGE_RW | PAGE_USER;

    uintptr_t directory = (uintptr_t)page_directory;
    __asm__ volatile(
        "mov %%cr4, %%eax\n"
        "or $0x10, %%eax\n"
        "mov %%eax, %%cr4\n"
        "mov %0, %%cr3\n"
        "mov %%cr0, %%eax\n"
        "or $0x80010000, %%eax\n" /* PG and WP */
        "mov %%eax, %%cr0\n"
        : : "r"(directory) : "eax", "memory");

    ready = true;
    kprintf("[process] paging on; ring-3 address range 0x%08x - 0x%08x\n",
            PROCESS_USER_BASE, PROCESS_USER_BASE + PROCESS_USER_BYTES);
}

static bool load_elf32(const uint8_t *file, uint32_t size, uint32_t *entry) {
    if (size < sizeof(elf_header_t)) return false;
    const elf_header_t *elf = (const elf_header_t *)file;
    if (elf->ident[0] != 0x7f || elf->ident[1] != 'E' ||
        elf->ident[2] != 'L' || elf->ident[3] != 'F' ||
        elf->ident[4] != 1 || elf->ident[5] != 1 ||
        elf->ident[6] != 1 || elf->type != 2 ||
        elf->machine != 3 || elf->version != 1 ||
        elf->ehsize != sizeof(elf_header_t) ||
        elf->phentsize != sizeof(elf_segment_t) ||
        elf->phnum == 0 || elf->phnum > 16 ||
        elf->phoff > size ||
        elf->phnum > (size - elf->phoff) / sizeof(elf_segment_t)) {
        return false;
    }
    bool entry_is_executable = false;
    unsigned loaded = 0;
    const uint8_t *ph = file + elf->phoff;

    /* First pass: validate everything before modifying the process image. */
    for (unsigned i = 0; i < elf->phnum; ++i) {
        const elf_segment_t *s =
            (const elf_segment_t *)(ph + i * sizeof(elf_segment_t));
        if (s->type == 2 || s->type == 3) return false; /* DYNAMIC/INTERP */
        if (s->type != 1) continue; /* PT_LOAD */
        if (s->memsz == 0 || s->filesz > s->memsz ||
            s->offset > size || s->filesz > size - s->offset ||
            !user_range(s->vaddr, s->memsz) ||
            s->vaddr + s->memsz > PROCESS_USER_BASE +
                                           PROCESS_USER_BYTES - PROCESS_STACK_RESERVE) {
            return false;
        }
        if ((s->flags & 1u) &&
            elf->entry >= s->vaddr && elf->entry - s->vaddr < s->memsz) {
            entry_is_executable = true;
        }
        ++loaded;
    }
    if (!loaded || !entry_is_executable) return false;

    kmemset((void *)(uintptr_t)PROCESS_USER_BASE, 0, PROCESS_USER_BYTES);
    for (unsigned i = 0; i < elf->phnum; ++i) {
        const elf_segment_t *s =
            (const elf_segment_t *)(ph + i * sizeof(elf_segment_t));
        if (s->type != 1) continue;
        uint8_t *dst = (uint8_t *)(uintptr_t)s->vaddr;
        kmemset(dst, 0, s->memsz);
        kmemcpy(dst, file + s->offset, s->filesz);
    }
    *entry = elf->entry;
    return true;
}

int process_run_elf32(const char *directory, const char *filename) {
    if (!ready || running || !fat16_mounted()) return -1;

    fat_entry_t folder, file_entry;
    if (!fat16_find(0, directory, &folder) ||
        !(folder.attr & FAT_ATTR_DIRECTORY) ||
        !fat16_find(folder.cluster, filename, &file_entry) ||
        (file_entry.attr & FAT_ATTR_DIRECTORY) ||
        file_entry.size < sizeof(elf_header_t) ||
        file_entry.size > PROCESS_MAX_ELF_BYTES) {
        kprintf("[process] ELF32 program not found/unsupported: %s/%s\n",
                directory, filename);
        return -1;
    }
    uint8_t *elf_bytes = (uint8_t *)kmalloc(file_entry.size);
    if (fat16_read(folder.cluster, filename, elf_bytes, file_entry.size) !=
        (int)file_entry.size) {
        kprintf("[process] could not read ELF32 program\n");
        return -1;
    }

    uint32_t entry;
    if (!load_elf32(elf_bytes, file_entry.size, &entry)) {
        kprintf("[process] invalid or unsupported ELF32 image\n");
        return -1;
    }
    running = true;
    exit_status = -1;
    kprintf("[process] launching ELF32 at 0x%08x (ring 3)\n", entry);
    process_enter(entry, PROCESS_USER_BASE + PROCESS_USER_BYTES - 16);
    running = false;
    kprintf("[process] ELF32 user program exited %d\n", exit_status);
    return exit_status;
}

/*
 * First working Win32 milestone: a PE32 console image with a small,
 * explicitly allowed kernel32.dll import surface. No GUI/registry/MSI,
 * no PE64, and no arbitrary application or installer compatibility.
 */
int process_run_win32_pe(const char *directory, const char *filename) {
    if (!ready || running || !fat16_mounted()) return -1;

    fat_entry_t folder, entry;
    if (!fat16_find(0, directory, &folder) ||
        !(folder.attr & FAT_ATTR_DIRECTORY) ||
        !fat16_find(folder.cluster, filename, &entry) ||
        (entry.attr & FAT_ATTR_DIRECTORY) ||
        entry.size < 512 || entry.size > 64u * 1024u) {
        kprintf("[win32] executable not found or outside PE32 sandbox limits\n");
        return -1;
    }
    /* Reuse a fixed staging buffer; the bump heap cannot free allocations. */
    static uint8_t file_bytes[64u * 1024u];
    if (fat16_read(folder.cluster, filename, file_bytes, entry.size) !=
        (int)entry.size) {
        kprintf("[win32] failed to read PE32 image\n");
        return -1;
    }

    win32_pe_info_t image;
    if (!win32_pe_stage(file_bytes, entry.size,
                        (uint8_t *)(uintptr_t)PROCESS_USER_BASE,
                        PROCESS_USER_BYTES, &image) ||
        !win32_pe_install_bridges(
             (uint8_t *)(uintptr_t)PROCESS_USER_BASE,
             PROCESS_USER_BYTES)) {
        kprintf("[win32] unsupported PE32 image, missing imports or unsafe layout\n");
        return -1;
    }

    win32_files_reset(&win32_files);
    active_win32=true;
    running = true;
    exit_status = -1;
    kprintf("[win32] launched PE32 console in ring 3 with %u kernel32 imports\n",
            image.imported_functions);
    process_enter(image.entry, PROCESS_USER_BASE + PROCESS_USER_BYTES - 16);
    running = false;
    active_win32=false;
    kprintf("[win32] PE32 process exited %d\n", exit_status);
    return exit_status;
}

void process_handle_interrupt(uint32_t *frame) {
    uint32_t vector = frame[8];
    uint32_t cs = frame[11];
    if (!running || (cs & 3u) != 3u) {
        panic("unexpected user interrupt %u (cs=0x%x)", vector, cs);
    }

    if (vector == 0x80) {
        switch (frame[7]) { /* EAX = syscall */
            case 1: { /* write serial: EBX = buffer, ECX = length */
                uint32_t pointer = frame[4];
                uint32_t length = frame[6];
                if (length > 256 || !user_range(pointer, length)) {
                    frame[7] = (uint32_t)-1;
                    return;
                }
                const uint8_t *data = (const uint8_t *)(uintptr_t)pointer;
                for (uint32_t i = 0; i < length; ++i) {
                    serial_putc((char)data[i]);
                }
                frame[7] = length;
                return;
            }
            case 3: /* Win32 CreateFileA: EBX=name, ECX=access, EDX=disposition */
                frame[7]=active_win32 ?
                    win32_files_open(&win32_files,
                        (const uint8_t *)(uintptr_t)PROCESS_USER_BASE,
                        PROCESS_USER_BASE,PROCESS_USER_BYTES,
                        frame[4],frame[6],frame[5],
                        win32_load_file,NULLPTR) : UINT32_MAX;
                return;
            case 4: /* Win32 WriteFile: EBX=handle, ECX=buffer, EDX=size */
                frame[7]=active_win32 ?
                    (uint32_t)win32_files_write(&win32_files,frame[4],
                        (const uint8_t *)(uintptr_t)PROCESS_USER_BASE,
                        PROCESS_USER_BASE,PROCESS_USER_BYTES,
                        frame[6],frame[5]) : UINT32_MAX;
                return;
            case 5: /* Win32 ReadFile: EBX=handle, ECX=buffer, EDX=size */
                frame[7]=active_win32 ?
                    (uint32_t)win32_files_read(&win32_files,frame[4],
                        (uint8_t *)(uintptr_t)PROCESS_USER_BASE,
                        PROCESS_USER_BASE,PROCESS_USER_BYTES,
                        frame[6],frame[5]) : UINT32_MAX;
                return;
            case 6: /* Win32 CloseHandle: EBX=handle */
                frame[7]=active_win32 &&
                    win32_files_close(&win32_files,frame[4],
                                      win32_save_file,NULLPTR) ? 1u : 0u;
                return;
            case 7: /* Win32 GetLastError */
                frame[7]=active_win32 ? win32_files.last_error : 0u;
                return;
            case 2: /* exit: EBX = signed status */
                exit_status = (int)frame[4];
                running = false;
                process_leave();
            default:
                frame[7] = (uint32_t)-38; /* ENOSYS */
                return;
        }
    }
    kprintf("[process] isolated user fault %u at 0x%08x (err 0x%08x)\n",
            vector, frame[10], frame[9]);
    exit_status = -1;
    running = false;
    process_leave();
}
