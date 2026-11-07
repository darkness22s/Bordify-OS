#include "settings.h"
#include "fat16.h"
#include "printf.h"
#include "string.h"

#define SETTINGS_PATH "SYSTEM/SETTINGS.CFG"

system_settings_t g_settings;

void settings_defaults(void) {
    g_settings.theme = THEME_LIGHT;
    g_settings.wallpaper = WALLPAPER_PLAIN;
    g_settings.accent = ACCENT_BLUE;
}

static int parse_theme(const char *value) {
    if (kstricmp(value, "PAPER") == 0) {
        return THEME_PAPER;
    }
    if (kstricmp(value, "DARK") == 0) {
        return THEME_DARK;
    }
    return THEME_LIGHT;
}

static int parse_wallpaper(const char *value) {
    if (kstricmp(value, "GRID") == 0) {
        return WALLPAPER_GRID;
    }
    if (kstricmp(value, "DOTS") == 0) {
        return WALLPAPER_DOTS;
    }
    return WALLPAPER_PLAIN;
}

static int parse_accent(const char *value) {
    if (kstricmp(value, "RED") == 0) {
        return ACCENT_RED;
    }
    if (kstricmp(value, "GREEN") == 0) {
        return ACCENT_GREEN;
    }
    if (kstricmp(value, "YELLOW") == 0) {
        return ACCENT_YELLOW;
    }
    return ACCENT_BLUE;
}

bool settings_load(void) {
    settings_defaults();
    char buffer[512];
    int bytes = fat16_read_path(SETTINGS_PATH, buffer, sizeof(buffer) - 1);
    if (bytes < 0) {
        return false;
    }
    buffer[bytes] = 0;
    char *line = buffer;
    while (line && *line) {
        char *next = kstrchr(line, '\n');
        if (next) {
            *next++ = 0;
        }
        char *equals = kstrchr(line, '=');
        if (equals) {
            *equals++ = 0;
            ktrim(line);
            ktrim(equals);
            if (kstricmp(line, "THEME") == 0) {
                g_settings.theme = parse_theme(equals);
            } else if (kstricmp(line, "WALLPAPER") == 0) {
                g_settings.wallpaper = parse_wallpaper(equals);
            } else if (kstricmp(line, "ACCENT") == 0) {
                g_settings.accent = parse_accent(equals);
            }
        }
        line = next;
    }
    return true;
}

static const char *theme_value(void) {
    switch (g_settings.theme) {
        case THEME_PAPER: return "PAPER";
        case THEME_DARK: return "DARK";
        default: return "LIGHT";
    }
}

static const char *wallpaper_value(void) {
    switch (g_settings.wallpaper) {
        case WALLPAPER_GRID: return "GRID";
        case WALLPAPER_DOTS: return "DOTS";
        default: return "PLAIN";
    }
}

static const char *accent_value(void) {
    switch (g_settings.accent) {
        case ACCENT_RED: return "RED";
        case ACCENT_GREEN: return "GREEN";
        case ACCENT_YELLOW: return "YELLOW";
        default: return "BLUE";
    }
}

bool settings_save(void) {
    char buffer[160];
    snprintf(buffer, sizeof(buffer),
             "THEME=%s\nWALLPAPER=%s\nACCENT=%s\n",
             theme_value(), wallpaper_value(), accent_value());
    if (!fat16_mounted()) {
        return false;
    }
    int result = fat16_write_path(SETTINGS_PATH, buffer, (uint32_t)kstrlen(buffer));
    if (result < 0) {
        kprintf("[settings] save failed\n");
        return false;
    }
    kprintf("[settings] saved to /%s\n", SETTINGS_PATH);
    return true;
}

uint32_t settings_bg(void) {
    if (g_settings.theme == THEME_DARK) {
        return 0x1C1C1E;
    }
    if (g_settings.theme == THEME_PAPER) {
        return 0xF4EFE5;
    }
    return 0xFFFFFF;
}

uint32_t settings_panel(void) {
    if (g_settings.theme == THEME_DARK) {
        return 0x27272A;
    }
    if (g_settings.theme == THEME_PAPER) {
        return 0xFFFCF4;
    }
    return 0xFFFFFF;
}

uint32_t settings_panel_alt(void) {
    if (g_settings.theme == THEME_DARK) {
        return 0x202022;
    }
    if (g_settings.theme == THEME_PAPER) {
        return 0xEFE8DB;
    }
    return 0xF5F5F2;
}

uint32_t settings_ink(void) {
    if (g_settings.theme == THEME_DARK) {
        return 0xF2F0EA;
    }
    return 0x1A1A18;
}

uint32_t settings_muted(void) {
    if (g_settings.theme == THEME_DARK) {
        return 0xA8A79F;
    }
    if (g_settings.theme == THEME_PAPER) {
        return 0x746D5F;
    }
    return 0x76756E;
}

uint32_t settings_line(void) {
    if (g_settings.theme == THEME_DARK) {
        return 0x48484B;
    }
    if (g_settings.theme == THEME_PAPER) {
        return 0xD7CEBD;
    }
    return 0xD9D9D3;
}

uint32_t settings_strong(void) {
    if (g_settings.theme == THEME_DARK) {
        return 0xF4F1E9;
    }
    return 0x1B1B18;
}

uint32_t settings_shadow(void) {
    if (g_settings.theme == THEME_DARK) {
        return 0x121214;
    }
    return 0xE8E7E1;
}

uint32_t settings_accent(void) {
    switch (g_settings.accent) {
        case ACCENT_RED: return 0xE25A4A;
        case ACCENT_GREEN: return 0x3FA36B;
        case ACCENT_YELLOW: return 0xE0A932;
        default: return 0x4D86E8;
    }
}

uint32_t settings_accent_soft(void) {
    switch (g_settings.accent) {
        case ACCENT_RED: return 0xF9E1DD;
        case ACCENT_GREEN: return 0xE0F1E7;
        case ACCENT_YELLOW: return 0xF8ECD0;
        default: return 0xE4ECFB;
    }
}

const char *settings_theme_name(void) {
    switch (g_settings.theme) {
        case THEME_PAPER: return "Paper";
        case THEME_DARK: return "Night";
        default: return "Light";
    }
}
