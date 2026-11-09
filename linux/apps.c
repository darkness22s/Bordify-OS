/* Linux-native implementations behind Bordify's unchanged window UI. */
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include "apps.h"
#include "gfx.h"
#include "gui.h"
#include "settings.h"
#include "ui.h"

static char status[160] = "Drag a file onto the desktop to launch it.";
static void spawn(const char *command, const char *argument) {
    pid_t pid = fork();
    if (pid == 0) {
        execlp(command, command, argument, (char *)NULL);
        perror(command);
        _exit(127);
    }
    if (pid < 0) snprintf(status, sizeof(status), "Could not start %s", command);
}
static void heading(gui_window_t *w, const char *title, const char *subtitle) {
    int x = gui_content_x(w) + 30;
    int y = gui_content_y(w) + 24;
    gfx_text(&font_h2, x, y, title, settings_ink());
    gfx_text(&font_ui, x, y + 42, subtitle, settings_muted());
}
static void files_draw(gui_window_t *w) {
    int x = gui_content_x(w) + 30, y = gui_content_y(w) + 118;
    heading(w, "Files", "Your Linux home folder and cross-platform launcher.");
    ui_button(x, y, 200, 42, "Open Home", true, false);
    ui_button(x + 218, y, 220, 42, "Choose an app...", false, false);
    ui_button(x + 218, y + 54, 220, 42, "Choose macOS .app", false, false);
    gfx_text(&font_ui, x, y + 110, "Windows .exe / .msi: Wine", settings_ink());
    gfx_text(&font_ui, x, y + 140, "macOS .app / .pkg / Mach-O: Darling (optional)", settings_ink());
    gfx_text(&font_ui_small, x, y + 198, status, settings_muted());
    gfx_text(&font_ui_small, x, y + 223, "Unknown files open with your Linux default app.", settings_muted());
}
static void files_click(gui_window_t *w, int mx, int my) {
    int x = gui_content_x(w) + 30, y = gui_content_y(w) + 118;
    if (ui_point_in(x, y, 200, 42, mx, my)) {
        const char *home = getenv("HOME");
        spawn("xdg-open", home ? home : "/");
    }
    if (ui_point_in(x + 218, y, 220, 42, mx, my))
        spawn("bordify-choose-app", "--choose");
    if (ui_point_in(x + 218, y + 54, 220, 42, mx, my))
        spawn("bordify-choose-app", "--bundle");
}
static void web_draw(gui_window_t *w) {
    int x = gui_content_x(w) + 30, y = gui_content_y(w) + 130;
    heading(w, "web", "Browse the internet using the Linux browser.");
    ui_button(x, y, 200, 42, "Open browser", true, false);
    gfx_text(&font_ui_small, x, y + 76, "Network and web rendering are provided by Linux.", settings_muted());
}
static void web_click(gui_window_t *w, int mx, int my) {
    int x = gui_content_x(w) + 30, y = gui_content_y(w) + 130;
    if (ui_point_in(x, y, 200, 42, mx, my))
        spawn("xdg-open", "https://www.google.com/");
}
static void settings_draw(gui_window_t *w) {
    int x = gui_content_x(w) + 30, y = gui_content_y(w) + 110;
    heading(w, "Appearance", "Bordify's original theme, wallpaper and accent controls.");
    gfx_text(&font_ui_small, x, y, "THEME", settings_muted());
    const char *themes[] = {"Light", "Paper", "Night"};
    for (int i = 0; i < 3; ++i)
        ui_button(x + 122*i, y + 24, 110, 40, themes[i], g_settings.theme==i, false);
    gfx_text(&font_ui_small, x, y + 92, "WALLPAPER", settings_muted());
    const char *papers[] = {"Plain", "Grid", "Dots"};
    for (int i = 0; i < 3; ++i)
        ui_button(x + 122*i, y + 116, 110, 40, papers[i], g_settings.wallpaper==i, false);
    gfx_text(&font_ui_small, x, y + 186, "ACCENT", settings_muted());
    const char *accents[] = {"Blue", "Red", "Green", "Yellow"};
    for (int i = 0; i < 4; ++i)
        ui_button(x + 110*i, y + 210, 100, 40, accents[i], g_settings.accent==i, false);
    gfx_text(&font_ui_small, x, y + 272, "Preferences are stored in ~/.config/bordify/settings.cfg", settings_muted());
}
static void settings_click(gui_window_t *w, int mx, int my) {
    int x = gui_content_x(w) + 30, y = gui_content_y(w) + 110;
    for (int i = 0; i < 3; ++i) {
        if (ui_point_in(x + 122*i, y + 24, 110, 40, mx, my)) g_settings.theme = i;
        if (ui_point_in(x + 122*i, y + 116, 110, 40, mx, my)) g_settings.wallpaper = i;
    }
    for (int i = 0; i < 4; ++i)
        if (ui_point_in(x + 110*i, y + 210, 100, 40, mx, my)) g_settings.accent = i;
    settings_save();
}
static void generic_key(gui_window_t *w, const key_event_t *e) {
    if (e->code == KEY_ESC) gui_close_window(w);
}
const gui_app_t files_app = {
    .title="Files", .width=660, .height=450,
    .draw=files_draw, .mouse_down=files_click, .key=generic_key
};
const gui_app_t web_app = {
    .title="web", .width=600, .height=350,
    .draw=web_draw, .mouse_down=web_click, .key=generic_key
};
const gui_app_t settings_app = {
    .title="Settings", .width=670, .height=540,
    .draw=settings_draw, .mouse_down=settings_click, .key=generic_key
};
