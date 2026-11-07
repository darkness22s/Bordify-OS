#include "icons.h"
#include "gfx.h"
#include "settings.h"

void icon_tile_background(int x, int y, int size) {
    int radius = size / 5;
    int thickness = MAX(1, size / 42);
    gfx_rounded_fill(x, y, size, size, radius, settings_strong());
    gfx_rounded_fill(x + thickness, y + thickness, size - thickness * 2,
                     size - thickness * 2, MAX(0, radius - thickness), settings_panel());
}

void icon_folder(int x, int y, int size, uint32_t fill, uint32_t ink) {
    int margin = size / 6;
    int tab_w = size / 2 - size / 12;
    int tab_h = MAX(3, size / 7);
    int body_y = y + size / 4;
    int body_h = size - (body_y - y) - size / 5;
    int thickness = MAX(1, size / 32);

    gfx_rounded_outline(x + margin, body_y - tab_h / 2, tab_w, tab_h + 4, 3,
                        thickness, ink, fill);
    gfx_rounded_fill(x + margin, body_y - tab_h / 2 + 2, tab_w, tab_h, 2, fill);
    gfx_rounded_outline(x + margin, body_y, size - margin * 2, body_h, 4,
                        thickness, ink, fill);
    gfx_hline(x + margin + 3, x + size - margin - 3, body_y + body_h / 2,
              gfx_blend(fill, 0xFFFFFF, 70));
}

void icon_document(int x, int y, int size, uint32_t fill, uint32_t ink, uint32_t accent) {
    int margin = size / 6;
    int w = size - margin * 2;
    int h = size - margin;
    int fold = MAX(3, size / 4);
    gfx_rounded_outline(x + margin, y + margin / 2, w, h, 3, 1, ink, fill);
    gfx_triangle_fill(x + margin + w - fold, y + margin / 2,
                      x + margin + w, y + margin / 2,
                      x + margin + w, y + margin / 2 + fold,
                      accent);
    gfx_line(x + margin + w - fold, y + margin / 2,
             x + margin + w - fold, y + margin / 2 + fold, ink);
    gfx_line(x + margin + w - fold, y + margin / 2 + fold,
             x + margin + w, y + margin / 2 + fold, ink);
    for (int i = 0; i < 3; i++) {
        gfx_hline(x + margin + 3, x + margin + w - 4,
                  y + margin + fold + 4 + i * 4, ink);
    }
}

void icon_chrome(int x, int y, int size) {
    int margin = size / 6;
    int radius = (size - margin * 2) / 2;
    int cx = x + size / 2;
    int cy = y + size / 2;
    const uint32_t red = 0xEA4335;
    const uint32_t green = 0x34A853;
    const uint32_t yellow = 0xFBBC05;
    const uint32_t blue = 0x4285F4;
    gfx_circle_fill(cx, cy, radius, red);
    gfx_fan(cx, cy, radius, -30, 90, 10, green);
    gfx_fan(cx, cy, radius, 90, 210, 10, yellow);
    gfx_circle_fill(cx, cy, radius * 52 / 100, 0xFFFFFF);
    gfx_circle_fill(cx, cy, radius * 40 / 100, blue);
    gfx_circle_outline(cx, cy, radius, MAX(1, size / 42), settings_strong());
}

static void icon_monitor(int x, int y, int size) {
    int margin = size / 6;
    int w = size - margin * 2;
    int h = w * 62 / 100;
    int monitor_x = x + margin;
    int monitor_y = y + size / 5;
    int ink = settings_strong();
    int thickness = MAX(1, size / 30);
    gfx_rounded_outline(monitor_x, monitor_y, w, h, MAX(2, size / 14), thickness,
                        ink, 0x9DCBF4);
    int cx = x + size / 2;
    int stand_top = monitor_y + h;
    int stand_bottom = stand_top + size / 8;
    gfx_line_thick(cx, stand_top, cx, stand_bottom, thickness, ink);
    gfx_line_thick(cx - size / 7, stand_bottom, cx + size / 7, stand_bottom,
                   thickness, ink);
    int gear_r = MAX(3, size / 9);
    int gx = cx;
    int gy = monitor_y + h / 2;
    gfx_circle_outline(gx, gy, gear_r, thickness, ink);
    gfx_circle_outline(gx, gy, MAX(1, gear_r / 2), thickness, ink);
    for (int i = 0; i < 8; i++) {
        static const int8_t dx[8] = { 0, 1, 1, 1, 0, -1, -1, -1 };
        static const int8_t dy[8] = { -1, -1, 0, 1, 1, 1, 0, -1 };
        gfx_line_thick(gx + dx[i] * gear_r, gy + dy[i] * gear_r,
                       gx + dx[i] * (gear_r + thickness + 2),
                       gy + dy[i] * (gear_r + thickness + 2), thickness, ink);
    }
}

void icon_app(int app, int x, int y, int size) {
    icon_tile_background(x, y, size);
    switch (app) {
        case APP_FILES:
            icon_folder(x, y, size, 0x9DCBF4, settings_strong());
            break;
        case APP_WEB:
            icon_chrome(x, y, size);
            break;
        case APP_SETTINGS:
            icon_monitor(x, y, size);
            break;
        default:
            break;
    }
}
