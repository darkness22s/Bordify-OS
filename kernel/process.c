#include "process.h"
#include "gdt.h"
#include "heap.h"
#include "fat16.h"
#include "serial.h"
#include "printf.h"
#include "string.h"
#include "win32_pe.h"
#include "win32_files.h"
#include "win32_crt.h"
#include "win32_vm.h"
#include "win32_dll.h"
#include "pit.h"

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
static win32_crt_t win32_crt;
static win32_vm_t win32_vm;
static win32_dll_t win32_dll;
/* Fixed guest-only DllMain call stub, outside Win32 bridges and strings. */
#define WIN32_DLL_INIT_STUB (PROCESS_USER_BASE+WIN32_PE_RUNTIME_OFFSET+8192u)
#define WIN32_DLL_INIT_RETURN_EIP (WIN32_DLL_INIT_STUB+25u)
static bool dll_entry_active;
static bool dll_entry_result;
/* Only one guest thread exists. A dynamic DllMain callback is injected
 * into that thread's pending syscall return frame, not executed in ring 0.
 * Preserve its original bridge EIP and stack until the callback finishes. */
typedef struct {
    bool active;
    uint32_t handle;
    uint32_t saved_eip;
    uint32_t saved_esp;
    uint8_t reason; /* 1=PROCESS_ATTACH, 0=PROCESS_DETACH */
} win32_dynamic_entry_t;
static win32_dynamic_entry_t dynamic_entry;
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
        !(apps.attr & FAT_ATTR_DIRECTORY)) {
#ifdef BORDIFY_CHROME_PROBE
        kprintf("[win32-trace] save preconditions rejected\n");
#endif
        return false;
    }
#ifdef BORDIFY_CHROME_PROBE
    uint32_t free_before=fat16_free_clusters();
#endif
    int stored=fat16_write(apps.cluster,"WIN32.TXT",data,(uint32_t)count);
    bool found=fat16_find(apps.cluster,"WIN32.TXT",&entry);
#ifdef BORDIFY_CHROME_PROBE
    kprintf("[win32-trace] save requested=%u stored=%d found=%u size=%u free_before=%u free_after=%u\n",
            (unsigned)count,stored,found ? 1u : 0u,
            found ? entry.size : 0u,free_before,fat16_free_clusters());
#endif
    return stored==(int)count && found && entry.size==(uint32_t)count;
}
/* External DLL loads are restricted to APPS/<8.3>.DLL with bounded
 * file size, no absolute paths, no DOS device names and no search path. */
static int win32_read_dll(const char *name,uint8_t *out,
                          size_t capacity,void *context) {
    (void)context;
    fat_entry_t folder,entry;
    if(!name || !out || !fat16_mounted() ||
       !fat16_find(0,"APPS",&folder) ||
       !(folder.attr&FAT_ATTR_DIRECTORY) ||
       !fat16_find(folder.cluster,name,&entry) ||
       (entry.attr&FAT_ATTR_DIRECTORY) ||
       entry.size<512u || entry.size>capacity)return -1;
    int got=fat16_read(folder.cluster,name,out,entry.size);
    return got==(int)entry.size?got:-1;
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
     * identity mapping with a bounded 4 MiB of explicitly user-accessible RAM.
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
/* DllMain(HINSTANCE,DWORD,LPVOID) uses stdcall and returns BOOL.
 * Prepare its arguments in the user stack and enter CPL3. Never directly
 * call a guest entrypoint from the supervisor. Syscall 29 returns here. */
/* DllMain(HINSTANCE,DWORD,LPVOID) is stdcall, returning BOOL.
 * The trampoline executes only with ring-3 CS/SS; no guest code is
 * callable from the kernel. The last syscall returns to its caller. */
static bool win32_prepare_dll_entry(uint32_t module,uint32_t entry,
                                    uint8_t reason) {
    if(!active_win32 || !user_range(entry,1u) ||
       !user_range(WIN32_DLL_INIT_STUB,27u) ||
       reason>1u || !module)return false;
    uint8_t *code=(uint8_t *)(uintptr_t)WIN32_DLL_INIT_STUB;
    static const uint8_t thunk[27]={
        0x6a,0x00,             /* push reserved */
        0x6a,0x01,             /* push attach/detach reason */
        0x68,0,0,0,0,          /* push HMODULE */
        0xb8,0,0,0,0,          /* mov eax,entry */
        0xff,0xd0,            /* call eax */
        0x89,0xc3,            /* mov ebx,eax: DllMain BOOL */
        0xb8,29,0,0,0,        /* syscall #29: complete callback */
        0xcd,0x80,
        0x0f,0x0b
    };
    kmemcpy(code,thunk,sizeof(thunk));
    code[3]=reason;
    for(unsigned i=0;i<4u;++i) {
        code[5u+i]=(uint8_t)(module>>(8u*i));
        code[10u+i]=(uint8_t)(entry>>(8u*i));
    }
    return true;
}
static bool win32_call_dll_entry(uint32_t module,uint32_t entry,
                                uint8_t reason) {
    if(!win32_prepare_dll_entry(module,entry,reason))return false;
    dll_entry_active=true;
    dll_entry_result=false;
    running=true;
    process_enter(WIN32_DLL_INIT_STUB,
                  PROCESS_USER_BASE+PROCESS_USER_BYTES-16u);
    running=false;
    dll_entry_active=false;
    return dll_entry_result;
}
static bool begin_dynamic_entry(uint32_t *frame,uint32_t module,
                                uint32_t entry,uint8_t reason,
                                unsigned bridge_slot) {
    uint32_t expected=WIN32_PE_USER_BASE+WIN32_PE_RUNTIME_OFFSET+
                       bridge_slot*WIN32_PE_BRIDGE_SLOT+12u;
    if(dynamic_entry.active || dll_entry_active || !frame ||
       frame[10]!=expected || !user_range(frame[13],12u) ||
       !win32_prepare_dll_entry(module,entry,reason))
        return false;
    dynamic_entry.active=true;
    dynamic_entry.handle=module;
    dynamic_entry.saved_eip=frame[10];
    dynamic_entry.saved_esp=frame[13];
    dynamic_entry.reason=reason;
    frame[10]=WIN32_DLL_INIT_STUB; /* return from int 80 directly to CPL3 */
    frame[7]=0u;
    return true;
}
static uint32_t win32_guest_load(uint32_t *frame,bool wide) {
    if(!active_win32 || dynamic_entry.active)return 0u;
    /* Guarded dynamic loading can prepare one new initialized module.
     * Recursively importing an uninitialized DLL is deliberately rejected. */
    win32_dll.allow_dynamic_init=!dll_entry_active;
    uint32_t handle=wide?win32_dll_load_w(&win32_dll,frame[4]):
                         win32_dll_load(&win32_dll,frame[4]);
    win32_dll.allow_dynamic_init=false;
    win32_files.last_error=win32_dll.last_error;
    if(!handle)return 0u;
    for(unsigned i=0;i<WIN32_DLL_MAX_IMAGES;++i) {
        const win32_dll_image_t *m=&win32_dll.images[i];
        if(m->state!=2u || m->handle!=handle)continue;
        if(!m->info.entry_rva || m->initialized)return handle;
        if(!begin_dynamic_entry(frame,handle,
                handle+m->info.entry_rva,1u,wide?32u:29u)) {
            (void)win32_dll_free(&win32_dll,handle);
            win32_files.last_error=WIN32_DLL_ERROR_INIT_FAILED;
            return 0u;
        }
        return 0u; /* EIP changed: pending guest callback */
    }
    return handle; /* virtual kernel32/msvcrt module */
}
static uint32_t win32_guest_free(uint32_t *frame) {
    if(!active_win32 || dynamic_entry.active)return 0u;
    uint32_t handle=frame[4];
    for(unsigned i=0;i<WIN32_DLL_MAX_IMAGES;++i) {
        const win32_dll_image_t *m=&win32_dll.images[i];
        if(m->state!=2u || m->handle!=handle)continue;
        if(m->initialized && m->info.entry_rva &&
           m->refs==1u && m->dep_refs==0u) {
            if(!begin_dynamic_entry(frame,handle,
                    handle+m->info.entry_rva,0u,31u)) {
                win32_files.last_error=WIN32_DLL_ERROR_INIT_FAILED;
                return 0u;
            }
            return 0u;
        }
        break;
    }
    uint32_t ok=win32_dll_free(&win32_dll,handle);
    win32_files.last_error=win32_dll.last_error;
    return ok;
}
int process_run_win32_pe(const char *directory, const char *filename) {
    if (!ready || running || !fat16_mounted()) return -1;

    fat_entry_t folder, entry;
    if (!fat16_find(0, directory, &folder) ||
        !(folder.attr & FAT_ATTR_DIRECTORY) ||
        !fat16_find(folder.cluster, filename, &entry) ||
        (entry.attr & FAT_ATTR_DIRECTORY) ||
        entry.size < 512 || entry.size > 2u * 1024u * 1024u) {
        kprintf("[win32] executable not found or outside PE32 sandbox limits\n");
        return -1;
    }
    /* Reuse a fixed staging buffer; the bump heap cannot free allocations. */
    static uint8_t file_bytes[2u * 1024u * 1024u];
    if (fat16_read(folder.cluster, filename, file_bytes, entry.size) !=
        (int)entry.size) {
        kprintf("[win32] failed to read PE32 image\n");
        return -1;
    }

    win32_pe_info_t image;
    if (!win32_pe_stage_deferred(file_bytes,entry.size,
                        (uint8_t *)(uintptr_t)PROCESS_USER_BASE,
                        PROCESS_USER_BYTES,&image)) {
        kprintf("[win32] PE32 staging rejected: %s (code %u)\n",
                win32_pe_reason_name(image.reject_reason),
                image.reject_reason);
        return -1;
    }
    if (!win32_pe_install_bridges(
            (uint8_t *)(uintptr_t)PROCESS_USER_BASE,
            PROCESS_USER_BYTES) ||
        !win32_pe_set_command_line(
            (uint8_t *)(uintptr_t)PROCESS_USER_BASE,
            PROCESS_USER_BYTES,filename)) {
        kprintf("[win32] PE32 process setup rejected: command line or bridges\n");
        return -1;
    }

    win32_files_reset(&win32_files);
    win32_crt_init(&win32_crt,(uint8_t *)(uintptr_t)PROCESS_USER_BASE,
                   PROCESS_USER_BASE,PROCESS_USER_BYTES);
    win32_vm_init(&win32_vm,(uint8_t *)(uintptr_t)PROCESS_USER_BASE,
                  PROCESS_USER_BASE,PROCESS_USER_BYTES);
    win32_dll_init(&win32_dll,(const uint8_t *)(uintptr_t)PROCESS_USER_BASE,
                   PROCESS_USER_BASE,PROCESS_USER_BYTES);
    win32_dll_pin_imports(&win32_dll,image.imported_module_mask);
    win32_dll_set_reader(&win32_dll,&win32_vm,win32_read_dll,NULLPTR);
    if(!win32_dll_bind_exe_imports(&win32_dll,image.image_size,
                image.import_rva,image.import_size,&image.imported_functions)) {
        kprintf("[win32] import-table binding rejected: Win32/DLL error %u\n",
                win32_dll.last_error);
        return -1;
    }
    active_win32=true;
    dynamic_entry.active=false;
    exit_status=-1;
    uint32_t modules[WIN32_DLL_MAX_IMAGES]={0u};
    uint32_t entries[WIN32_DLL_MAX_IMAGES]={0u};
    uint32_t init_count=0u,attached=0u;
    if(!win32_dll_attach_plan(&win32_dll,modules,entries,&init_count)) {
        kprintf("[win32] DLL attach order invalid\n");
        active_win32=false;
        win32_dll_release_exe_imports(&win32_dll);
        return -1;
    }
    for(;attached<init_count;++attached) {
        if(!win32_call_dll_entry(modules[attached],entries[attached],1u)) {
            kprintf("[win32] DLL_PROCESS_ATTACH failed for 0x%08x\n",
                    modules[attached]);
            while(attached) {
                --attached;
                (void)win32_call_dll_entry(modules[attached],entries[attached],0u);
            }
            active_win32=false;
            win32_dll_release_exe_imports(&win32_dll);
            return -1;
        }
        for(unsigned i=0;i<WIN32_DLL_MAX_IMAGES;++i)
            if(win32_dll.images[i].handle==modules[attached])
                win32_dll.images[i].initialized=true;
    }
    running=true;
    exit_status=-1;
    kprintf("[win32] launched PE32 console in ring 3 with %u imports, %u DLL attach calls\n",
            image.imported_functions,attached);
    process_enter(image.entry,PROCESS_USER_BASE+PROCESS_USER_BYTES-16u);
    running=false;
    /* DLL_PROCESS_DETACH in reverse dependency order, before memory release. */
    while(attached) {
        --attached;
        (void)win32_call_dll_entry(modules[attached],entries[attached],0u);
    }
    active_win32=false;
    win32_dll_release_exe_imports(&win32_dll);
    kprintf("[win32] PE32 process exited %d\n",exit_status);
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
            case 3: { /* Win32 CreateFileA */
                uint32_t name=frame[4], access=frame[6], disposition=frame[5];
                frame[7]=active_win32 ?
                    win32_files_open(&win32_files,
                        (const uint8_t *)(uintptr_t)PROCESS_USER_BASE,
                        PROCESS_USER_BASE,PROCESS_USER_BYTES,
                        name,access,disposition,
                        win32_load_file,NULLPTR) : UINT32_MAX;
#ifdef BORDIFY_CHROME_PROBE
                kprintf("[win32-trace] CreateFileA ptr=%x access=%x mode=%u -> handle=%x last_error=%u\n",
                        name,access,disposition,frame[7],win32_files.last_error);
#endif
                return;
            }
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
            case 6: { /* Win32 CloseHandle */
                uint32_t handle=frame[4], len=win32_files.size;
                frame[7]=active_win32 &&
                    win32_files_close(&win32_files,handle,
                                      win32_save_file,NULLPTR) ? 1u : 0u;
#ifdef BORDIFY_CHROME_PROBE
                kprintf("[win32-trace] CloseHandle=%x size=%u -> %u err=%u\n",
                        handle,len,frame[7],win32_files.last_error);
#endif
                return;
            }
            case 7: /* Win32 GetLastError */
                frame[7]=active_win32 ? win32_files.last_error : 0u;
                return;
            case 8: /* Win32 GetTickCount: PIT ticks in milliseconds */
                frame[7]=active_win32 ? pit_ticks()*10u : UINT32_MAX;
                return;
            case 9: /* Win32 SetLastError: EBX=new error code */
                if (active_win32) win32_files.last_error=frame[4];
                frame[7]=active_win32 ? 0u : UINT32_MAX;
                return;
            case 10: case 11: case 12: case 13: case 14: case 15:
            case 16: case 17: case 18: case 19: case 20: case 30: case 31:
                frame[7]=active_win32?
                    win32_crt_call(&win32_crt,frame[7],
                                   frame[4],frame[6],frame[5]) :
                    UINT32_MAX;
                return;
            case 21: { /* Win32 VirtualAlloc: 4 stdcall arguments on user stack */
                uint32_t args=frame[4];
                if(!active_win32 || !user_range(args,16u)) {
                    frame[7]=0u;
                    if(active_win32) win32_files.last_error=87u;
                    return;
                }
                const uint32_t *p=(const uint32_t *)(uintptr_t)args;
                frame[7]=win32_vm_alloc(&win32_vm,p[0],p[1],p[2],p[3]);
                win32_files.last_error=win32_vm.last_error;
                return;
            }
            case 22: { /* Win32 VirtualFree: 3 stdcall arguments */
                uint32_t args=frame[4];
                if(!active_win32 || !user_range(args,12u)) {
                    frame[7]=0u;
                    if(active_win32) win32_files.last_error=87u;
                    return;
                }
                const uint32_t *p=(const uint32_t *)(uintptr_t)args;
                frame[7]=win32_vm_free(&win32_vm,p[0],p[1],p[2]);
                win32_files.last_error=win32_vm.last_error;
                return;
            }
            case 23: /* LoadLibraryA; may arrange ring-3 DllMain */
                frame[7]=win32_guest_load(frame,false);
                return;
            case 24: { /* GetProcAddress: EBX=guest pointer to 2 args */
                uint32_t params=frame[4];
                if(!active_win32 || !user_range(params,8u)) {
                    frame[7]=0u;
                    if(active_win32)win32_files.last_error=87u;
                    return;
                }
                const uint32_t *p=(const uint32_t *)(uintptr_t)params;
                frame[7]=win32_dll_symbol(&win32_dll,p[0],p[1]);
                win32_files.last_error=win32_dll.last_error;
                return;
            }
            case 25: /* FreeLibrary; final ref may require detach */
                frame[7]=win32_guest_free(frame);
                return;
            case 26: /* GetModuleHandleA: EBX=optional module name */
            case 27: { /* GetModuleHandleW: UTF-16LE module name */
                bool wide=frame[7]==27u;
                frame[7]=active_win32?
                    win32_dll_get_module(&win32_dll,frame[4],wide):0u;
                if(active_win32)win32_files.last_error=win32_dll.last_error;
                return;
            }
            case 28: /* LoadLibraryW; may arrange ring-3 DllMain */
                frame[7]=win32_guest_load(frame,true);
                return;
            case 29: /* ring-3 DllMain callback completion */
                if(active_win32 && dll_entry_active &&
                   frame[10]==WIN32_DLL_INIT_RETURN_EIP) {
                    dll_entry_result=frame[4]!=0u;
                    running=false;
                    process_leave();
                }
                if(active_win32 && dynamic_entry.active &&
                   frame[10]==WIN32_DLL_INIT_RETURN_EIP &&
                   frame[13]==dynamic_entry.saved_esp) {
                    uint32_t handle=dynamic_entry.handle;
                    uint8_t reason=dynamic_entry.reason;
                    frame[10]=dynamic_entry.saved_eip;
                    dynamic_entry.active=false;
                    if(reason==1u) {
                        if(frame[4]) {
                            for(unsigned i=0;i<WIN32_DLL_MAX_IMAGES;++i)
                                if(win32_dll.images[i].state==2u &&
                                   win32_dll.images[i].handle==handle)
                                    win32_dll.images[i].initialized=true;
                            frame[7]=handle;
                            win32_files.last_error=0u;
                            kprintf("[win32-dll] dynamic PROCESS_ATTACH completed in ring 3\n");
                        } else {
                            (void)win32_dll_free(&win32_dll,handle);
                            frame[7]=0u;
                            win32_files.last_error=WIN32_DLL_ERROR_INIT_FAILED;
                            kprintf("[win32-dll] dynamic PROCESS_ATTACH rejected in ring 3\n");
                        }
                    } else {
                        /* The guest DllMain(DLL_PROCESS_DETACH) has now run.
                         * Clear the attach state before releasing the final
                         * reference; win32_dll_free deliberately rejects
                         * unloading an attached image without detach. */
                        for(unsigned i=0;i<WIN32_DLL_MAX_IMAGES;++i)
                            if(win32_dll.images[i].state==2u &&
                               win32_dll.images[i].handle==handle)
                                win32_dll.images[i].initialized=false;
                        frame[7]=win32_dll_free(&win32_dll,handle);
                        win32_files.last_error=win32_dll.last_error;
                        kprintf("[win32-dll] dynamic PROCESS_DETACH completed in ring 3\n");
                    }
                    return;
                }
                frame[7]=UINT32_MAX;
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
