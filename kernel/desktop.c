#include "desktop.h"
#include "io.h"
#include "gui.h"
#include "gfx.h"
#include "settings.h"
#include "icons.h"
#include "ui.h"
#include "input.h"
#include "pit.h"
#include "string.h"
#include "apps.h"
#include "config.h"

typedef struct {
    const gui_app_t *app;
    const char *label;
    int x;
    int y;
    int size;
} shortcut_t;

static const shortcut_t shortcuts[] = {
    { &files_app, "Files", 50, 64, 56 },
    { &web_app, "web", 50, 146, 56 },
    { &settings_app, "Settings", 50, 228, 56 },
};

static void draw_wallpaper(void) {
    gfx_clear(settings_bg());
    if (g_settings.wallpaper == WALLPAPER_GRID) {
        for (int x = 0; x < gfx_width(); x += 32) {
            gfx_vline(x, 0, gfx_height() - 1, gfx_blend(settings_bg(), settings_line(), 100));
        }
        for (int y = 0; y < gfx_height(); y += 32) {
            gfx_hline(0, gfx_width() - 1, y, gfx_blend(settings_bg(), settings_line(), 100));
        }
    } else if (g_settings.wallpaper == WALLPAPER_DOTS) {
        uint32_t dot = gfx_blend(settings_bg(), settings_line(), 150);
        for (int y = 16; y < gfx_height(); y += 26) {
            for (int x = 16; x < gfx_width(); x += 26) {
                gfx_circle_fill(x, y, 1, dot);
            }
        }
    }
}

static bool shortcut_contains(const shortcut_t *shortcut, int x, int y) {
    return x >= shortcut->x - 10 && x < shortcut->x + shortcut->size + 10 &&
           y >= shortcut->y - 8 && y < shortcut->y + shortcut->size + 34;
}

static void draw_shortcuts(void) {
    mouse_state_t mouse;
    input_mouse_state(&mouse);
    for (size_t i = 0; i < ARRAY_LEN(shortcuts); i++) {
        const shortcut_t *shortcut = &shortcuts[i];
        bool hover = shortcut_contains(shortcut, mouse.x, mouse.y);
        if (hover) {
            gfx_rounded_fill(shortcut->x - 8, shortcut->y - 6,
                             shortcut->size + 16, shortcut->size + 46, 12,
                             gfx_blend(settings_bg(), settings_panel_alt(), 170));
        }
        icon_app((int)i, shortcut->x, shortcut->y, shortcut->size);
        int label_y = shortcut->y + shortcut->size + 9;
        int tw = gfx_text_width(&font_ui_small, shortcut->label);
        gfx_text(&font_ui_small, shortcut->x + shortcut->size / 2 - tw / 2, label_y,
                 shortcut->label, settings_ink());
    }
}

static void draw_dock(void) {
    int width = gfx_width();
    int height = gfx_height();
    gfx_rounded_fill(-28, height - 30, width + 56, 64, 32, settings_panel());
    gfx_rounded_border(-28, height - 30, width + 56, 64, 32, 1, settings_strong());
}

static void draw_desktop(void) {
    draw_wallpaper();
    draw_shortcuts();
    gui_draw_windows();
    draw_dock();
    mouse_state_t mouse;
    input_mouse_state(&mouse);
    gfx_draw_cursor(mouse.x, mouse.y);
    gfx_present();
}

static void handle_shortcut_click(int x, int y) {
    for (size_t i = 0; i < ARRAY_LEN(shortcuts); i++) {
        if (shortcut_contains(&shortcuts[i], x, y)) {
            if (i == 0) {
                gui_open_app(&files_app, 140, 112);
            } else if (i == 1) {
                gui_open_app(&web_app, 82, 84);
            } else {
                gui_open_app(&settings_app, 118, 96);
            }
            return;
        }
    }
}

void desktop_run(void) {
    mouse_state_t previous;
    kmemset(&previous, 0, sizeof(previous));
    input_mouse_state(&previous);
    uint32_t last_draw = 0;
    draw_desktop();

    for (;;) {
        cpu_halt();
        mouse_state_t mouse;
        input_mouse_state(&mouse);
        bool dirty = mouse.x != previous.x || mouse.y != previous.y ||
                     mouse.left != previous.left || mouse.right != previous.right ||
                     mouse.middle != previous.middle;

        key_event_t event;
        while (input_pop_key(&event)) {
            dirty = true;
            if (!gui_has_open_windows()) {
                if (event.code == KEY_F1) {
                    gui_open_app(&files_app, 140, 112);
                } else if (event.code == KEY_F2) {
                    gui_open_app(&web_app, 82, 84);
                } else if (event.code == KEY_F3) {
                    gui_open_app(&settings_app, 118, 96);
                }
                continue;
            }
            gui_handle_key(&event);
        }

        if (mouse.left && !previous.left) {
            if (!gui_handle_mouse_down(mouse.x, mouse.y)) {
                handle_shortcut_click(mouse.x, mouse.y);
            }
        } else if (mouse.left && previous.left) {
            gui_handle_mouse_move(mouse.x, mouse.y, true);
        } else if (!mouse.left && previous.left) {
            gui_handle_mouse_up(mouse.x, mouse.y);
        }

        previous = mouse;
        gui_handle_tick();

        uint32_t now = pit_ticks();
        if (dirty || now - last_draw >= 3) {
            draw_desktop();
            last_draw = now;
        }
    }
}
