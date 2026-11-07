#include "power.h"
#include "io.h"
#include "gfx.h"
#include "settings.h"
#include "font.h"

static void settle(void) {
    for (volatile uint32_t i = 0; i < 60000000u; i++) {
        __asm__ volatile("pause");
    }
}

void system_reboot(void) {
    cpu_cli();
    gfx_clear(settings_bg());
    gfx_text_center(&font_title, gfx_width() / 2, gfx_height() / 2 - 20,
                    "Restarting Bordify...", settings_ink());
    gfx_present();
    for (volatile uint32_t i = 0; i < 50000000u; i++) {
        __asm__ volatile("pause");
    }
    for (int i = 0; i < 100000; i++) {
        if (!(inb(0x64) & 0x02)) {
            break;
        }
    }
    outb(0x64, 0xFE);
    for (;;) {
        cpu_halt();
    }
}

void system_poweroff(void) {
    gfx_clear(settings_bg());
    gfx_text_center(&font_title, gfx_width() / 2, gfx_height() / 2,
                    "Powering off...", settings_ink());
    gfx_present();
    settle();
    cpu_cli();
    outw(0x604, 0x2000);
    outw(0xB004, 0x2000);
    for (;;) {
        cpu_halt();
    }
}
