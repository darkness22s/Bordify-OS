#ifndef BORDIFY_SETTINGS_H
#define BORDIFY_SETTINGS_H

#include "types.h"

typedef enum {
    THEME_LIGHT = 0,
    THEME_PAPER = 1,
    THEME_DARK = 2,
} theme_id_t;

typedef enum {
    WALLPAPER_PLAIN = 0,
    WALLPAPER_GRID = 1,
    WALLPAPER_DOTS = 2,
} wallpaper_id_t;

typedef enum {
    ACCENT_BLUE = 0,
    ACCENT_RED = 1,
    ACCENT_GREEN = 2,
    ACCENT_YELLOW = 3,
} accent_id_t;

typedef struct {
    int theme;
    int wallpaper;
    int accent;
} system_settings_t;

extern system_settings_t g_settings;

void settings_defaults(void);
bool settings_load(void);
bool settings_save(void);
uint32_t settings_bg(void);
uint32_t settings_panel(void);
uint32_t settings_panel_alt(void);
uint32_t settings_ink(void);
uint32_t settings_muted(void);
uint32_t settings_line(void);
uint32_t settings_strong(void);
uint32_t settings_accent(void);
uint32_t settings_accent_soft(void);
uint32_t settings_shadow(void);
const char *settings_theme_name(void);

#endif
