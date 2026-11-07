#ifndef BORDIFY_FONT_H
#define BORDIFY_FONT_H

#include "types.h"

typedef struct {
    uint32_t offset;
    uint8_t width;
    uint8_t height;
    int8_t x_offset;
    int8_t y_offset;
    uint8_t advance;
} glyph_t;

typedef struct {
    const uint8_t *pixels;
    const glyph_t *glyphs;
    uint16_t line_height;
    uint16_t ascent;
} font_t;

extern const font_t font_ui_small;
extern const font_t font_ui;
extern const font_t font_ui_bold;
extern const font_t font_title;
extern const font_t font_h2;
extern const font_t font_display;

#define FONT_FIRST_CHAR 32
#define FONT_LAST_CHAR 126

#endif
