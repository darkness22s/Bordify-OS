#include "types.h"
#include "config.h"
#include "io.h"
#include "serial.h"
#include "printf.h"
#include "mb2.h"
#include "gdt.h"
#include "idt.h"
#include "heap.h"
#include "gfx.h"
#include "font.h"
#include "pit.h"
#include "input.h"
#include "ata.h"
#include "net.h"
#include "fat16.h"
#include "settings.h"
#include "gui.h"
#include "desktop.h"
#include "icons.h"
#include "process.h"

extern uint8_t kernel_end;

static void boot_splash(void) {
    gfx_clear(0xFFFFFF);
    int cx = gfx_width() / 2;
    icon_app(APP_FILES, cx - 28, gfx_height() / 2 - 104, 56);
    gfx_text_center(&font_display, cx, gfx_height() / 2 - 20, BORDIFY_NAME, 0x1A1A18);
    gfx_text_center(&font_ui, cx, gfx_height() / 2 + 40,
                    "a small operating system", 0x76756E);
    gfx_present();
}

void kernel_main(uint32_t mb_info_address) {
    serial_init();
    kprintf("\n");
    kprintf("Bordify OS %s (%s)\n", BORDIFY_VERSION, BORDIFY_KERNEL);
    kprintf("booting...\n");

    mb2_parse(mb_info_address);
    gdt_init();
    idt_init();

    uintptr_t heap_start = ALIGN_UP((uintptr_t)&kernel_end, 4096);
    size_t heap_size = HEAP_SIZE_BYTES;
    const mb2_info_t *mb = mb2_info();
    if (mb->memory_size_kb > 64u * 1024u) {
        uintptr_t limit = (uintptr_t)(mb->memory_size_kb * 1024u) - 4u * 1024u * 1024u;
        if (heap_start + heap_size > limit) {
            heap_size = limit > heap_start ? limit - heap_start : (4u * 1024u * 1024u);
        }
    }
    heap_init(heap_start, heap_size);

    if (!mb->framebuffer_present || mb->framebuffer_bpp != 32) {
        panic("Bordify requires a 32-bit linear framebuffer");
    }
    gfx_init(mb->framebuffer_addr, mb->framebuffer_pitch,
             (int)mb->framebuffer_width, (int)mb->framebuffer_height);

    pit_init();
    cpu_sti();
    boot_splash();
    pit_sleep_ms(550);

    input_init();
    ata_init();
    net_init();
    if (fat16_mount()) {
        settings_load();
    } else {
        settings_defaults();
        kprintf("[kernel] no FAT16 disk, continuing with defaults\n");
    }

    process_init();
    if (fat16_mounted()) {
        /* Boot-time end-to-end test: runs from FAT16 at CPL3, not in kernel. */
        int result = process_run_elf32("APPS", "HELLO.ELF");
        if (result != 0) {
            kprintf("[process] demo failed (status %d)\n", result);
        }
        /* An isolated page fault must NOT panic the kernel. */
        int fault_result = process_run_elf32("APPS", "FAULT.ELF");
        if (fault_result != -1) {
            kprintf("[process] unexpected fault-test result %d\n", fault_result);
        }
    }

    if (fat16_mounted()) {
        int win32_result = process_run_win32_pe("APPS", "WINHELLO.EXE");
        if (win32_result != 0) {
            kprintf("[win32] demo failed (status %d)\n", win32_result);
        }
    }

    gui_init();
    kprintf("[kernel] starting desktop\n");
    desktop_run();
}
