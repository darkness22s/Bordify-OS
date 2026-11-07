#include "gui.h"
#include "gfx.h"
#include "settings.h"
#include "icons.h"
#include "ui.h"
#include "input.h"
#include "fat16.h"
#include "heap.h"
#include "mb2.h"
#include "pit.h"
#include "rtc.h"
#include "string.h"
#include "printf.h"
#include "power.h"
#include "config.h"

typedef enum {
    SETTINGS_APPEARANCE = 0,
    SETTINGS_SYSTEM = 1,
    SETTINGS_ABOUT = 2,
} settings_page_t;

typedef struct {
    int page;
    char status[96];
    uint32_t status_tick;
} app_settings_state_t;

static app_settings_state_t st;

static void settings_page_layout(gui_window_t *window, int *x0, int *y0, int *w, int *h) {
    *x0 = gui_content_x(window);
    *y0 = gui_content_y(window);
    *w = gui_content_width(window);
    *h = gui_content_height(window);
}

static void set_status(const char *text) {
    kstrncpy(st.status, text, sizeof(st.status) - 1);
    st.status[sizeof(st.status) - 1] = 0;
    st.status_tick = pit_ticks();
}

static void save_or_report(void) {
    if (settings_save()) {
        set_status("Saved to /SYSTEM/SETTINGS.CFG");
    } else {
        set_status("Running from memory - settings not persisted");
    }
}

static uint32_t preview_bg(int theme) {
    if (theme == THEME_DARK) return 0x1C1C1E;
    if (theme == THEME_PAPER) return 0xF4EFE5;
    return 0xFFFFFF;
}

static uint32_t preview_panel(int theme) {
    if (theme == THEME_DARK) return 0x2A2A2D;
    if (theme == THEME_PAPER) return 0xFFFCF4;
    return 0xFFFFFF;
}

static uint32_t preview_ink(int theme) {
    if (theme == THEME_DARK) return 0xF2F0EA;
    return 0x1A1A18;
}

static void draw_sidebar(gui_window_t *window) {
    int x0, y0, w, h;
    settings_page_layout(window, &x0, &y0, &w, &h);
    UNUSED(w);
    int side_w = 190;
    gfx_fill_rect(x0, y0, side_w, h, settings_panel_alt());
    gfx_vline(x0 + side_w, y0, y0 + h - 1, settings_line());
    icon_tile_background(x0 + 22, y0 + 24, 38);
    gfx_text(&font_ui_bold, x0 + 70, y0 + 31, "Bordify", settings_ink());
    gfx_text(&font_ui_small, x0 + 70, y0 + 50, BORDIFY_VERSION, settings_muted());

    const char *items[3] = { "Appearance", "System", "About" };
    mouse_state_t mouse;
    input_mouse_state(&mouse);
    for (int i = 0; i < 3; i++) {
        int iy = y0 + 100 + i * 42;
        bool active = st.page == i;
        bool hover = ui_point_in(x0 + 14, iy, side_w - 28, 34, mouse.x, mouse.y);
        if (active || hover) {
            gfx_rounded_fill(x0 + 14, iy, side_w - 28, 34, 8,
                             active ? settings_accent_soft() : settings_panel());
        }
        gfx_text(&font_ui, x0 + 28, iy + 7, items[i],
                 active ? settings_accent() : settings_ink());
    }

    gfx_text(&font_ui_small, x0 + 22, y0 + h - 40, "Bordify Settings", settings_muted());
}

static void draw_appearance(gui_window_t *window) {
    int x0, y0, w, h;
    settings_page_layout(window, &x0, &y0, &w, &h);
    int left = x0 + 190 + 34;
    int top = y0 + 28;
    gfx_text(&font_h2, left, top, "Appearance", settings_ink());
    gfx_text(&font_ui, left, top + 38,
             "Make this little machine feel like yours.", settings_muted());

    gfx_text(&font_ui_small, left, top + 86, "THEME", settings_muted());
    mouse_state_t mouse;
    input_mouse_state(&mouse);
    const char *theme_names[3] = { "Light", "Paper", "Night" };
    for (int i = 0; i < 3; i++) {
        int cx = left + i * 154;
        int cy = top + 108;
        bool selected = g_settings.theme == i;
        bool hover = ui_point_in(cx, cy, 140, 108, mouse.x, mouse.y);
        gfx_rounded_fill(cx + 2, cy + 3, 140, 108, 10, settings_shadow());
        gfx_rounded_outline(cx, cy, 140, 108, 10,
                            selected ? 2 : 1,
                            selected ? settings_accent() : (hover ? settings_accent() : settings_line()),
                            preview_bg(i));
        if (i == THEME_DARK) {
            gfx_rounded_fill(cx + 16, cy + 18, 108, 50, 6, preview_panel(i));
            gfx_circle_fill(cx + 25, cy + 27, 2, 0xE25A4A);
            gfx_circle_fill(cx + 33, cy + 27, 2, 0xE0A932);
            gfx_circle_fill(cx + 41, cy + 27, 2, 0x3FA36B);
            gfx_text(&font_ui_small, cx + 16, cy + 76, theme_names[i], preview_ink(i));
        } else {
            gfx_rounded_fill(cx + 16, cy + 18, 108, 50, 6, 0xF5F5F2);
            gfx_hline(cx + 16, cx + 123, cy + 32, 0xD9D9D3);
            gfx_circle_fill(cx + 25, cy + 25, 2, 0xE25A4A);
            gfx_circle_fill(cx + 33, cy + 25, 2, 0xE0A932);
            gfx_circle_fill(cx + 41, cy + 25, 2, 0x3FA36B);
            gfx_text(&font_ui_small, cx + 16, cy + 76, theme_names[i], preview_ink(i));
        }
    }

    int wy = top + 244;
    gfx_text(&font_ui_small, left, wy, "WALLPAPER", settings_muted());
    const char *wall_names[3] = { "Plain", "Grid", "Dots" };
    for (int i = 0; i < 3; i++) {
        int cx = left + i * 154;
        int cy = wy + 22;
        bool selected = g_settings.wallpaper == i;
        bool hover = ui_point_in(cx, cy, 140, 76, mouse.x, mouse.y);
        gfx_rounded_outline(cx, cy, 140, 76, 9,
                            selected ? 2 : 1,
                            selected ? settings_accent() : (hover ? settings_accent() : settings_line()),
                            settings_bg());
        if (i == WALLPAPER_GRID) {
            for (int gx = 18; gx < 130; gx += 16) {
                gfx_vline(cx + gx, cy + 12, cy + 42, settings_line());
            }
            for (int gy = 12; gy < 44; gy += 13) {
                gfx_hline(cx + 16, cx + 126, cy + gy, settings_line());
            }
        } else if (i == WALLPAPER_DOTS) {
            for (int gy = 16; gy < 44; gy += 12) {
                for (int gx = 20; gx < 130; gx += 14) {
                    gfx_circle_fill(cx + gx, cy + gy, 2, settings_line());
                }
            }
        }
        gfx_text(&font_ui_small, cx + 16, cy + 52, wall_names[i], settings_ink());
    }

    int ay = top + 376;
    gfx_text(&font_ui_small, left, ay, "ACCENT", settings_muted());
    const uint32_t accents[4] = { 0x4D86E8, 0xE25A4A, 0x3FA36B, 0xE0A932 };
    for (int i = 0; i < 4; i++) {
        int cx = left + 15 + i * 56;
        int cy = ay + 40;
        bool selected = g_settings.accent == i;
        bool hover = ui_point_in(cx - 20, cy - 20, 40, 40, mouse.x, mouse.y);
        if (selected || hover) {
            gfx_circle_outline(cx, cy, 18, 2, i == 0 ? settings_accent() : accents[i]);
        }
        gfx_circle_fill(cx, cy, 13, accents[i]);
        if (selected) {
            gfx_circle_fill(cx, cy, 5, settings_panel());
        }
    }

    if (st.status[0] && pit_ticks() - st.status_tick < 400) {
        gfx_text(&font_ui_small, left, y0 + h - 34, st.status, settings_accent());
    } else {
        gfx_text(&font_ui_small, left, y0 + h - 34,
                 "All changes are written to the FAT16 disk.", settings_muted());
    }
}

static void format_uptime(uint32_t seconds, char *out, size_t size) {
    uint32_t h = seconds / 3600;
    uint32_t m = (seconds / 60) % 60;
    uint32_t s = seconds % 60;
    snprintf(out, size, "%u:%02u:%02u", h, m, s);
}

static void draw_info_row(int x, int y, int width, const char *label, const char *value) {
    gfx_text(&font_ui_small, x, y, label, settings_muted());
    int tw = gfx_text_width(&font_ui, value);
    gfx_text(&font_ui, x + width - tw, y - 2, value, settings_ink());
    gfx_hline(x, x + width, y + 26, settings_line());
}

static void draw_system(gui_window_t *window) {
    int x0, y0, w, h;
    settings_page_layout(window, &x0, &y0, &w, &h);
    int left = x0 + 190 + 34;
    int top = y0 + 28;
    int content_w = w - 190 - 68;
    gfx_text(&font_h2, left, top, "System", settings_ink());
    gfx_text(&font_ui, left, top + 38, "The tiny machine under the paper.",
             settings_muted());

    char uptime[32];
    char memory[48];
    char heap[48];
    char disk[64];
    format_uptime(pit_uptime_seconds(), uptime, sizeof(uptime));
    const mb2_info_t *mb = mb2_info();
    snprintf(memory, sizeof(memory), "%u MB", mb->memory_size_kb / 1024u);
    snprintf(heap, sizeof(heap), "%u KB / %u KB",
             (uint32_t)(heap_used() / 1024), (uint32_t)(heap_capacity() / 1024));
    if (fat16_mounted()) {
        const fat_volume_t *vol = fat16_volume();
        snprintf(disk, sizeof(disk), "FAT16 | %s | %u MB",
                 vol->label, vol->volume_sectors / 2048u);
    } else {
        kstrcpy(disk, "No mounted disk");
    }

    int row_y = top + 92;
    int row_gap = 38;
    draw_info_row(left, row_y, content_w, "Kernel", BORDIFY_KERNEL " " BORDIFY_VERSION);
    draw_info_row(left, row_y + row_gap, content_w, "Architecture", "32-bit x86");
    draw_info_row(left, row_y + row_gap * 2, content_w, "Memory", memory);
    draw_info_row(left, row_y + row_gap * 3, content_w, "Kernel heap", heap);
    draw_info_row(left, row_y + row_gap * 4, content_w, "Uptime", uptime);
    draw_info_row(left, row_y + row_gap * 5, content_w, "Display", "1024 x 768 | 32-bit");
    draw_info_row(left, row_y + row_gap * 6, content_w, "Storage", disk);

    mouse_state_t mouse;
    input_mouse_state(&mouse);
    int by = y0 + h - 54;
    bool restart_hover = ui_point_in(left, by, 92, 32, mouse.x, mouse.y);
    bool off_hover = ui_point_in(left + 100, by, 92, 32, mouse.x, mouse.y);
    ui_small_button(left, by, 92, 32, "Restart", restart_hover);
    ui_small_button(left + 100, by, 92, 32, "Power off", off_hover);
}

static void draw_about(gui_window_t *window) {
    int x0, y0, w, h;
    settings_page_layout(window, &x0, &y0, &w, &h);
    int left = x0 + 190 + 34;
    int top = y0 + 28;
    int content_w = w - 190 - 68;
    gfx_text(&font_h2, left, top, "About", settings_ink());
    gfx_text(&font_ui, left, top + 38, "A small hand-made operating system.",
             settings_muted());

    icon_tile_background(left, top + 86, 64);
    icon_folder(left, top + 86, 64, 0x9DCBF4, settings_strong());
    gfx_text(&font_display, left + 84, top + 92, "Bordify", settings_ink());
    gfx_text(&font_ui, left + 86, top + 138, "OS " BORDIFY_VERSION " | " BORDIFY_CODENAME,
             settings_muted());

    gfx_hline(left, left + content_w, top + 176, settings_line());
    gfx_text(&font_ui_small, left, top + 194, "KERNEL", settings_muted());
    gfx_text(&font_ui_bold, left, top + 214, "Bordify/32", settings_ink());
    gfx_text(&font_ui_small, left + 200, top + 194, "BOOTLOADER", settings_muted());
    const mb2_info_t *mb = mb2_info();
    gfx_text(&font_ui_bold, left + 200, top + 214,
             mb->bootloader ? mb->bootloader : "Multiboot2", settings_ink());
    gfx_text(&font_ui_small, left, top + 254, "BOOTED FROM", settings_muted());
    gfx_text(&font_ui_bold, left, top + 274, "FAT16 disk image", settings_ink());
    gfx_text(&font_ui_small, left + 200, top + 254, "DATE", settings_muted());
    char date[32];
    rtc_format_date(date, sizeof(date));
    gfx_text(&font_ui_bold, left + 200, top + 274, date, settings_ink());

    int quote_y = top + 332;
    gfx_rounded_fill(left, quote_y, content_w, 84, 10, settings_panel_alt());
    gfx_text(&font_ui, left + 18, quote_y + 16,
             "\"Everything you see is drawn by a", settings_ink());
    gfx_text(&font_ui, left + 18, quote_y + 38,
             "kernel written for this screen.\"", settings_ink());
    gfx_text(&font_ui_small, left + 18, quote_y + 62,
             "Framebuffer | PS/2 | PIT | ATA PIO | FAT16 | Window manager",
             settings_muted());

    if (st.status[0] && pit_ticks() - st.status_tick < 400) {
        gfx_text(&font_ui_small, left, y0 + h - 34, st.status, settings_accent());
    }
}

static void settings_draw(gui_window_t *window) {
    draw_sidebar(window);
    if (st.page == SETTINGS_APPEARANCE) {
        draw_appearance(window);
    } else if (st.page == SETTINGS_SYSTEM) {
        draw_system(window);
    } else {
        draw_about(window);
    }
}

static void settings_mouse_down(gui_window_t *window, int x, int y) {
    int x0, y0, w, h;
    settings_page_layout(window, &x0, &y0, &w, &h);
    for (int i = 0; i < 3; i++) {
        int iy = y0 + 100 + i * 42;
        if (ui_point_in(x0 + 14, iy, 190 - 28, 34, x, y)) {
            st.page = i;
            return;
        }
    }

    if (st.page == SETTINGS_APPEARANCE) {
        int left = x0 + 190 + 34;
        int top = y0 + 28;
        for (int i = 0; i < 3; i++) {
            int cx = left + i * 154;
            int cy = top + 108;
            if (ui_point_in(cx, cy, 140, 108, x, y)) {
                g_settings.theme = i;
                save_or_report();
                return;
            }
        }
        int wy = top + 244;
        for (int i = 0; i < 3; i++) {
            int cx = left + i * 154;
            int cy = wy + 22;
            if (ui_point_in(cx, cy, 140, 76, x, y)) {
                g_settings.wallpaper = i;
                save_or_report();
                return;
            }
        }
        int ay = top + 376;
        for (int i = 0; i < 4; i++) {
            int cx = left + 15 + i * 56;
            int cy = ay + 40;
            if (ui_point_in(cx - 20, cy - 20, 40, 40, x, y)) {
                g_settings.accent = i;
                save_or_report();
                return;
            }
        }
    } else if (st.page == SETTINGS_SYSTEM) {
        int left = x0 + 190 + 34;
        int by = y0 + h - 54;
        if (ui_point_in(left, by, 92, 32, x, y)) {
            system_reboot();
        } else if (ui_point_in(left + 100, by, 92, 32, x, y)) {
            system_poweroff();
        }
    }
    UNUSED(w);
}

static void settings_key(gui_window_t *window, const key_event_t *event) {
    UNUSED(window);
    UNUSED(event);
}

static void settings_init(gui_window_t *window) {
    UNUSED(window);
    st.page = SETTINGS_APPEARANCE;
    st.status[0] = 0;
    st.status_tick = 0;
}

const gui_app_t settings_app = {
    .title = "Settings",
    .width = 790,
    .height = 570,
    .init = settings_init,
    .draw = settings_draw,
    .mouse_down = settings_mouse_down,
    .mouse_up = NULLPTR,
    .key = settings_key,
    .tick = NULLPTR,
};
