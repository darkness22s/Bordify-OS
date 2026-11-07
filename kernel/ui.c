#include "ui.h"
#include "gfx.h"
#include "settings.h"
#include "font.h"
#include "printf.h"

bool ui_point_in(int x, int y, int w, int h, int px, int py) {
    return px >= x && py >= y && px < x + w && py < y + h;
}

void ui_panel(int x, int y, int w, int h, int radius) {
    gfx_rounded_outline(x, y, w, h, radius, 1, settings_strong(), settings_panel());
}

void ui_button(int x, int y, int w, int h, const char *label, bool primary, bool hovered) {
    if (primary) {
        uint32_t fill = hovered ? settings_accent() : gfx_blend(settings_accent(), settings_panel(), 40);
        gfx_rounded_outline(x, y, w, h, 8, 1, settings_strong(), fill);
        gfx_text_center(&font_ui_bold, x + w / 2, y + (h - font_ui_bold.line_height) / 2,
                        label, 0xFFFFFF);
    } else {
        uint32_t fill = hovered ? settings_panel_alt() : settings_panel();
        gfx_rounded_outline(x, y, w, h, 8, 1, settings_line(), fill);
        gfx_text_center(&font_ui, x + w / 2, y + (h - font_ui.line_height) / 2,
                        label, settings_ink());
    }
}

void ui_small_button(int x, int y, int w, int h, const char *label, bool hovered) {
    uint32_t fill = hovered ? settings_panel_alt() : settings_panel();
    gfx_rounded_outline(x, y, w, h, 6, 1, settings_line(), fill);
    gfx_text_center(&font_ui_small, x + w / 2, y + (h - font_ui_small.line_height) / 2,
                    label, settings_ink());
}

void ui_field(int x, int y, int w, int h, const char *text, bool focused, bool placeholder) {
    uint32_t fill = focused ? settings_panel() : settings_panel_alt();
    uint32_t line = focused ? settings_accent() : settings_line();
    gfx_rounded_outline(x, y, w, h, 7, 1, line, fill);
    int tx = x + 10;
    int ty = y + (h - font_ui.line_height) / 2;
    uint32_t color = placeholder ? settings_muted() : settings_ink();
    gfx_text_clip(&font_ui, tx, ty, text, w - 20, color);
    if (focused) {
        int caret_x = tx + gfx_text_width(&font_ui, text) + 1;
        if (caret_x > x + w - 8) {
            caret_x = x + w - 8;
        }
        gfx_vline(caret_x, y + 8, y + h - 9, settings_ink());
    }
}

void ui_badge(int x, int y, const char *label) {
    int width = gfx_text_width(&font_ui_small, label) + 18;
    gfx_rounded_outline(x, y, width, 24, 12, 1, settings_line(), settings_accent_soft());
    gfx_text_center(&font_ui_small, x + width / 2, y + 4, label, settings_accent());
}

void ui_divider(int x0, int x1, int y) {
    gfx_hline(x0, x1, y, settings_line());
}

const char *ui_format_size(uint32_t bytes, char *buffer, size_t size) {
    if (bytes < 1024) {
        snprintf(buffer, size, "%u B", bytes);
    } else if (bytes < 1024u * 1024u) {
        snprintf(buffer, size, "%u KB", (bytes + 1023u) / 1024u);
    } else {
        snprintf(buffer, size, "%u MB", (bytes + 1024u * 1024u - 1u) / (1024u * 1024u));
    }
    return buffer;
}
