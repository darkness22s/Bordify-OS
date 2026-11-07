#ifndef BORDIFY_GFX_H
#define BORDIFY_GFX_H

#include "types.h"
#include "font.h"

#define COLOR_BLACK 0x000000u
#define COLOR_WHITE 0xFFFFFFu

typedef struct {
    int x0;
    int y0;
    int x1;
    int y1;
} gfx_clip_t;

void gfx_init(uint32_t framebuffer, uint32_t pitch, int width, int height);
uint32_t *gfx_buffer(void);
int gfx_width(void);
int gfx_height(void);
void gfx_present(void);

void gfx_clear(uint32_t color);
void gfx_pixel(int x, int y, uint32_t color);
void gfx_pixel_alpha(int x, int y, uint32_t color, uint8_t alpha);
void gfx_fill_rect(int x, int y, int w, int h, uint32_t color);
void gfx_hline(int x0, int x1, int y, uint32_t color);
void gfx_vline(int x, int y0, int y1, uint32_t color);
void gfx_rect_outline(int x, int y, int w, int h, int thickness, uint32_t color);
void gfx_rounded_fill(int x, int y, int w, int h, int radius, uint32_t color);
void gfx_rounded_outline(int x, int y, int w, int h, int radius, int thickness,
                         uint32_t outline, uint32_t fill);
void gfx_rounded_border(int x, int y, int w, int h, int radius, int thickness,
                        uint32_t color);
void gfx_circle_fill(int cx, int cy, int radius, uint32_t color);
void gfx_circle_outline(int cx, int cy, int radius, int thickness, uint32_t color);
void gfx_line(int x0, int y0, int x1, int y1, uint32_t color);
void gfx_line_thick(int x0, int y0, int x1, int y1, int thickness, uint32_t color);
void gfx_triangle_fill(int x0, int y0, int x1, int y1, int x2, int y2, uint32_t color);
void gfx_fan(int cx, int cy, int radius, int start_deg, int end_deg, int segments, uint32_t color);
uint32_t gfx_isqrt(uint32_t value);

void gfx_set_clip(int x, int y, int w, int h);
void gfx_set_clip_rect(gfx_clip_t clip);
gfx_clip_t gfx_get_clip(void);
void gfx_reset_clip(void);

int gfx_text_width(const font_t *font, const char *text);
int gfx_text_width_n(const font_t *font, const char *text, size_t length);
void gfx_text(const font_t *font, int x, int y, const char *text, uint32_t color);
void gfx_text_clip(const font_t *font, int x, int y, const char *text, int max_width, uint32_t color);
void gfx_text_center(const font_t *font, int cx, int y, const char *text, uint32_t color);

void gfx_draw_cursor(int x, int y);
uint32_t gfx_blend(uint32_t dst, uint32_t src, uint8_t alpha);

#endif
