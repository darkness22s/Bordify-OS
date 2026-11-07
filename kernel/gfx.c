#include "gfx.h"
#include "heap.h"
#include "string.h"
#include "printf.h"

static uint8_t *framebuffer;
static uint32_t framebuffer_pitch;
static uint32_t *backbuffer;
static int screen_width;
static int screen_height;
static gfx_clip_t clip;

static inline bool in_clip(int x, int y) {
    return x >= clip.x0 && x < clip.x1 && y >= clip.y0 && y < clip.y1;
}

void gfx_init(uint32_t framebuffer_address, uint32_t pitch, int width, int height) {
    framebuffer = (uint8_t *)(uintptr_t)framebuffer_address;
    framebuffer_pitch = pitch;
    screen_width = width;
    screen_height = height;
    backbuffer = (uint32_t *)kmalloc((size_t)width * (size_t)height * 4u);
    gfx_reset_clip();
    kprintf("[gfx] backbuffer at %p, %dx%d\n", backbuffer, width, height);
}

uint32_t *gfx_buffer(void) {
    return backbuffer;
}

int gfx_width(void) {
    return screen_width;
}

int gfx_height(void) {
    return screen_height;
}

void gfx_present(void) {
    if (!framebuffer || !backbuffer) {
        return;
    }
    uint32_t row_bytes = (uint32_t)screen_width * 4u;
    for (int y = 0; y < screen_height; y++) {
        kmemcpy(framebuffer + (uint32_t)y * framebuffer_pitch,
                backbuffer + (uint32_t)y * (uint32_t)screen_width,
                row_bytes);
    }
}

void gfx_reset_clip(void) {
    clip.x0 = 0;
    clip.y0 = 0;
    clip.x1 = screen_width;
    clip.y1 = screen_height;
}

void gfx_set_clip(int x, int y, int w, int h) {
    clip.x0 = MAX(0, x);
    clip.y0 = MAX(0, y);
    clip.x1 = MIN(screen_width, x + w);
    clip.y1 = MIN(screen_height, y + h);
    if (clip.x1 < clip.x0) {
        clip.x1 = clip.x0;
    }
    if (clip.y1 < clip.y0) {
        clip.y1 = clip.y0;
    }
}

void gfx_set_clip_rect(gfx_clip_t new_clip) {
    clip.x0 = MAX(0, new_clip.x0);
    clip.y0 = MAX(0, new_clip.y0);
    clip.x1 = MIN(screen_width, new_clip.x1);
    clip.y1 = MIN(screen_height, new_clip.y1);
}

gfx_clip_t gfx_get_clip(void) {
    return clip;
}

uint32_t gfx_isqrt(uint32_t value) {
    uint32_t result = 0;
    uint32_t bit = 1u << 30;
    while (bit > value) {
        bit >>= 2;
    }
    while (bit) {
        if (value >= result + bit) {
            value -= result + bit;
            result = (result >> 1) + bit;
        } else {
            result >>= 1;
        }
        bit >>= 2;
    }
    return result;
}

uint32_t gfx_blend(uint32_t dst, uint32_t src, uint8_t alpha) {
    uint32_t inv = 255u - alpha;
    uint32_t rb = (((src & 0x00FF00FFu) * alpha) + ((dst & 0x00FF00FFu) * inv)) >> 8;
    uint32_t g = (((src & 0x0000FF00u) * alpha) + ((dst & 0x0000FF00u) * inv)) >> 8;
    return (rb & 0x00FF00FFu) | (g & 0x0000FF00u);
}

void gfx_pixel(int x, int y, uint32_t color) {
    if (in_clip(x, y)) {
        backbuffer[(uint32_t)y * (uint32_t)screen_width + (uint32_t)x] = color;
    }
}

void gfx_pixel_alpha(int x, int y, uint32_t color, uint8_t alpha) {
    if (alpha == 0 || !in_clip(x, y)) {
        return;
    }
    uint32_t *pixel = &backbuffer[(uint32_t)y * (uint32_t)screen_width + (uint32_t)x];
    *pixel = gfx_blend(*pixel, color, alpha);
}

void gfx_clear(uint32_t color) {
    int count = screen_width * screen_height;
    for (int i = 0; i < count; i++) {
        backbuffer[i] = color;
    }
}

void gfx_fill_rect(int x, int y, int w, int h, uint32_t color) {
    int x0 = MAX(x, clip.x0);
    int y0 = MAX(y, clip.y0);
    int x1 = MIN(x + w, clip.x1);
    int y1 = MIN(y + h, clip.y1);
    if (x1 <= x0 || y1 <= y0) {
        return;
    }
    for (int yy = y0; yy < y1; yy++) {
        uint32_t *row = backbuffer + (uint32_t)yy * (uint32_t)screen_width;
        for (int xx = x0; xx < x1; xx++) {
            row[xx] = color;
        }
    }
}

void gfx_hline(int x0, int x1, int y, uint32_t color) {
    if (x1 < x0) {
        int t = x0;
        x0 = x1;
        x1 = t;
    }
    gfx_fill_rect(x0, y, x1 - x0 + 1, 1, color);
}

void gfx_vline(int x, int y0, int y1, uint32_t color) {
    if (y1 < y0) {
        int t = y0;
        y0 = y1;
        y1 = t;
    }
    gfx_fill_rect(x, y0, 1, y1 - y0 + 1, color);
}

void gfx_rect_outline(int x, int y, int w, int h, int thickness, uint32_t color) {
    if (thickness <= 0) {
        return;
    }
    gfx_fill_rect(x, y, w, thickness, color);
    gfx_fill_rect(x, y + h - thickness, w, thickness, color);
    gfx_fill_rect(x, y, thickness, h, color);
    gfx_fill_rect(x + w - thickness, y, thickness, h, color);
}

void gfx_rounded_fill(int x, int y, int w, int h, int radius, uint32_t color) {
    if (w <= 0 || h <= 0) {
        return;
    }
    if (radius > w / 2) {
        radius = w / 2;
    }
    if (radius > h / 2) {
        radius = h / 2;
    }
    if (radius <= 0) {
        gfx_fill_rect(x, y, w, h, color);
        return;
    }
    for (int yy = 0; yy < h; yy++) {
        int inset = 0;
        if (yy < radius) {
            int dy = radius - yy - 1;
            inset = radius - (int)gfx_isqrt((uint32_t)(radius * radius - dy * dy));
        } else if (yy >= h - radius) {
            int dy = yy - (h - radius);
            inset = radius - (int)gfx_isqrt((uint32_t)(radius * radius - dy * dy));
        }
        gfx_hline(x + inset, x + w - inset - 1, y + yy, color);
    }
}

void gfx_rounded_outline(int x, int y, int w, int h, int radius, int thickness,
                         uint32_t outline, uint32_t fill) {
    gfx_rounded_fill(x, y, w, h, radius, outline);
    if (thickness < 1) {
        thickness = 1;
    }
    if (w > thickness * 2 && h > thickness * 2) {
        gfx_rounded_fill(x + thickness, y + thickness, w - thickness * 2,
                         h - thickness * 2, MAX(0, radius - thickness), fill);
    }
}

static int rounded_inset(int y, int h, int radius) {
    if (radius <= 0) {
        return 0;
    }
    if (y < radius) {
        int dy = radius - y - 1;
        return radius - (int)gfx_isqrt((uint32_t)(radius * radius - dy * dy));
    }
    if (y >= h - radius) {
        int dy = y - (h - radius);
        return radius - (int)gfx_isqrt((uint32_t)(radius * radius - dy * dy));
    }
    return 0;
}

void gfx_rounded_border(int x, int y, int w, int h, int radius, int thickness,
                        uint32_t color) {
    if (thickness <= 0 || w <= 0 || h <= 0) {
        return;
    }
    if (radius > w / 2) {
        radius = w / 2;
    }
    if (radius > h / 2) {
        radius = h / 2;
    }
    int inner_radius = MAX(0, radius - thickness);
    for (int yy = 0; yy < h; yy++) {
        int outer_inset = rounded_inset(yy, h, radius);
        int outer_x0 = x + outer_inset;
        int outer_x1 = x + w - outer_inset - 1;
        int inner_y = yy - thickness;
        int inner_h = h - thickness * 2;
        int inner_inset = -1;
        if (inner_y >= 0 && inner_y < inner_h && inner_h > 0) {
            inner_inset = thickness + rounded_inset(inner_y, inner_h, inner_radius);
        }
        if (inner_inset < 0 || w - inner_inset * 2 <= 0) {
            gfx_hline(outer_x0, outer_x1, y + yy, color);
        } else {
            int inner_x0 = x + inner_inset;
            int inner_x1 = x + w - inner_inset - 1;
            gfx_hline(outer_x0, inner_x0 - 1, y + yy, color);
            gfx_hline(inner_x1 + 1, outer_x1, y + yy, color);
        }
    }
}

void gfx_circle_fill(int cx, int cy, int radius, uint32_t color) {
    if (radius <= 0) {
        return;
    }
    int r2 = radius * radius;
    for (int dy = -radius; dy <= radius; dy++) {
        int span = (int)gfx_isqrt((uint32_t)(r2 - dy * dy));
        gfx_hline(cx - span, cx + span, cy + dy, color);
    }
}

void gfx_circle_outline(int cx, int cy, int radius, int thickness, uint32_t color) {
    if (radius <= 0) {
        return;
    }
    int r2_out = radius * radius;
    int r2_in = (radius - thickness) * (radius - thickness);
    for (int dy = -radius; dy <= radius; dy++) {
        int outer = (int)gfx_isqrt((uint32_t)(r2_out - dy * dy));
        int inner = 0;
        if (ABS(dy) <= radius - thickness) {
            inner = (int)gfx_isqrt((uint32_t)(r2_in - dy * dy));
        }
        if (inner > 0) {
            gfx_hline(cx - outer, cx - inner - 1, cy + dy, color);
            gfx_hline(cx + inner + 1, cx + outer, cy + dy, color);
        } else {
            gfx_hline(cx - outer, cx + outer, cy + dy, color);
        }
    }
}

void gfx_line(int x0, int y0, int x1, int y1, uint32_t color) {
    int dx = ABS(x1 - x0);
    int sx = x0 < x1 ? 1 : -1;
    int dy = -ABS(y1 - y0);
    int sy = y0 < y1 ? 1 : -1;
    int error = dx + dy;
    for (;;) {
        gfx_pixel(x0, y0, color);
        if (x0 == x1 && y0 == y1) {
            break;
        }
        int e2 = 2 * error;
        if (e2 >= dy) {
            error += dy;
            x0 += sx;
        }
        if (e2 <= dx) {
            error += dx;
            y0 += sy;
        }
    }
}

void gfx_line_thick(int x0, int y0, int x1, int y1, int thickness, uint32_t color) {
    if (thickness <= 1) {
        gfx_line(x0, y0, x1, y1, color);
        return;
    }
    int half = thickness / 2;
    int steps = MAX(ABS(x1 - x0), ABS(y1 - y0));
    if (steps == 0) {
        gfx_circle_fill(x0, y0, half, color);
        return;
    }
    for (int i = 0; i <= steps; i++) {
        int x = x0 + (x1 - x0) * i / steps;
        int y = y0 + (y1 - y0) * i / steps;
        gfx_circle_fill(x, y, half, color);
    }
}

static int edge(int ax, int ay, int bx, int by, int cx, int cy) {
    return (bx - ax) * (cy - ay) - (by - ay) * (cx - ax);
}

void gfx_triangle_fill(int x0, int y0, int x1, int y1, int x2, int y2, uint32_t color) {
    int min_x = MIN(x0, MIN(x1, x2));
    int max_x = MAX(x0, MAX(x1, x2));
    int min_y = MIN(y0, MIN(y1, y2));
    int max_y = MAX(y0, MAX(y1, y2));
    int area = edge(x0, y0, x1, y1, x2, y2);
    if (area == 0) {
        return;
    }
    for (int y = min_y; y <= max_y; y++) {
        for (int x = min_x; x <= max_x; x++) {
            int w0 = edge(x1, y1, x2, y2, x, y);
            int w1 = edge(x2, y2, x0, y0, x, y);
            int w2 = edge(x0, y0, x1, y1, x, y);
            if ((w0 >= 0 && w1 >= 0 && w2 >= 0) ||
                (w0 <= 0 && w1 <= 0 && w2 <= 0)) {
                gfx_pixel(x, y, color);
            }
        }
    }
}

static int cos_table[37];
static int sin_table[37];
static bool trig_ready;

static void init_trig(void) {
    if (trig_ready) {
        return;
    }
    static const int cos10[37] = {
        1000, 985, 940, 866, 766, 643, 500, 342, 174, 0,
        -174, -342, -500, -643, -766, -866, -940, -985, -1000,
        -985, -940, -866, -766, -643, -500, -342, -174, 0,
        174, 342, 500, 643, 766, 866, 940, 985, 1000
    };
    for (int i = 0; i < 37; i++) {
        cos_table[i] = cos10[i];
        sin_table[i] = cos10[(i + 27) % 36];
    }
    trig_ready = true;
}

void gfx_fan(int cx, int cy, int radius, int start_deg, int end_deg, int segments, uint32_t color) {
    init_trig();
    if (segments < 1) {
        segments = 1;
    }
    int px = cx + radius;
    int py = cy;
    int start = start_deg;
    for (int i = 1; i <= segments; i++) {
        int angle = start_deg + (end_deg - start_deg) * i / segments;
        int wrapped = angle % 360;
        if (wrapped < 0) {
            wrapped += 360;
        }
        int index = (wrapped + 5) / 10;
        if (index > 36) {
            index = 36;
        }
        int nx = cx + (radius * cos_table[index]) / 1000;
        int ny = cy + (radius * sin_table[index]) / 1000;
        int prev = start;
        if (i > 1) {
            prev = start_deg + (end_deg - start_deg) * (i - 1) / segments;
        }
        int prev_wrapped = prev % 360;
        if (prev_wrapped < 0) {
            prev_wrapped += 360;
        }
        int prev_index = (prev_wrapped + 5) / 10;
        if (prev_index > 36) {
            prev_index = 36;
        }
        int ox = cx + (radius * cos_table[prev_index]) / 1000;
        int oy = cy + (radius * sin_table[prev_index]) / 1000;
        gfx_triangle_fill(cx, cy, ox, oy, nx, ny, color);
        px = nx;
        py = ny;
    }
    UNUSED(px);
    UNUSED(py);
}

static const glyph_t *glyph_for(const font_t *font, char c) {
    if ((unsigned char)c < FONT_FIRST_CHAR || (unsigned char)c > FONT_LAST_CHAR) {
        c = '?';
    }
    return &font->glyphs[(unsigned char)c - FONT_FIRST_CHAR];
}

int gfx_text_width_n(const font_t *font, const char *text, size_t length) {
    if (!font || !text) {
        return 0;
    }
    int width = 0;
    for (size_t i = 0; i < length && text[i]; i++) {
        const glyph_t *glyph = glyph_for(font, text[i]);
        width += glyph->advance;
    }
    return width;
}

int gfx_text_width(const font_t *font, const char *text) {
    return gfx_text_width_n(font, text, kstrlen(text));
}

void gfx_text(const font_t *font, int x, int y, const char *text, uint32_t color) {
    if (!font || !text) {
        return;
    }
    int pen = x;
    int baseline = y + font->ascent;
    for (const char *p = text; *p; p++) {
        const glyph_t *glyph = glyph_for(font, *p);
        if (glyph->width && glyph->height) {
            for (int gy = 0; gy < glyph->height; gy++) {
                int py = baseline + glyph->y_offset + gy;
                if (py < clip.y0 || py >= clip.y1) {
                    continue;
                }
                for (int gx = 0; gx < glyph->width; gx++) {
                    int px = pen + glyph->x_offset + gx;
                    uint8_t alpha = font->pixels[glyph->offset + (uint32_t)gy * glyph->width + (uint32_t)gx];
                    if (alpha) {
                        gfx_pixel_alpha(px, py, color, alpha);
                    }
                }
            }
        }
        pen += glyph->advance;
    }
}

void gfx_text_clip(const font_t *font, int x, int y, const char *text, int max_width, uint32_t color) {
    if (!font || !text || max_width <= 0) {
        return;
    }
    char buffer[256];
    size_t length = kstrlen(text);
    if (length >= sizeof(buffer)) {
        length = sizeof(buffer) - 1;
    }
    size_t out = 0;
    int width = 0;
    for (size_t i = 0; i < length; i++) {
        const glyph_t *glyph = glyph_for(font, text[i]);
        if (width + glyph->advance > max_width) {
            if (out >= 3) {
                out -= 3;
                buffer[out++] = '.';
                buffer[out++] = '.';
                buffer[out++] = '.';
            }
            break;
        }
        buffer[out++] = text[i];
        width += glyph->advance;
    }
    buffer[out] = 0;
    gfx_text(font, x, y, buffer, color);
}

void gfx_text_center(const font_t *font, int cx, int y, const char *text, uint32_t color) {
    int width = gfx_text_width(font, text);
    gfx_text(font, cx - width / 2, y, text, color);
}

void gfx_draw_cursor(int x, int y) {
    static const int8_t shape[16][2] = {
        {0, 0}, {0, 15}, {4, 11}, {7, 17}, {10, 16}, {7, 10},
        {13, 10}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0},
        {0, 0}, {0, 0}, {0, 0}, {0, 0}
    };
    for (int i = 0; i < 6; i++) {
        int x0 = x + shape[i][0];
        int y0 = y + shape[i][1];
        int x1 = x + shape[i + 1][0];
        int y1 = y + shape[i + 1][1];
        gfx_line_thick(x0, y0, x1, y1, 3, COLOR_BLACK);
    }
    for (int i = 0; i < 6; i++) {
        int x0 = x + shape[i][0];
        int y0 = y + shape[i][1];
        int x1 = x + shape[i + 1][0];
        int y1 = y + shape[i + 1][1];
        gfx_line(x0, y0, x1, y1, COLOR_WHITE);
    }
}
