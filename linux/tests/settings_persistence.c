/* The Linux settings backend must safely save/load paths on a 64-bit host. */
#include "settings.h"
int main(void) {
    settings_defaults();
    g_settings.theme = THEME_DARK;
    g_settings.wallpaper = WALLPAPER_DOTS;
    g_settings.accent = ACCENT_GREEN;
    if (!settings_save()) return 1;
    settings_defaults();
    if (g_settings.theme != THEME_LIGHT) return 2;
    if (!settings_load()) return 3;
    if (g_settings.theme != THEME_DARK) return 4;
    if (g_settings.wallpaper != WALLPAPER_DOTS) return 5;
    if (g_settings.accent != ACCENT_GREEN) return 6;
    return 0;
}
