#ifndef BORDIFY_UI_H
#define BORDIFY_UI_H

#include "types.h"

void ui_button(int x, int y, int w, int h, const char *label, bool primary, bool hovered);
void ui_small_button(int x, int y, int w, int h, const char *label, bool hovered);
void ui_field(int x, int y, int w, int h, const char *text, bool focused, bool placeholder);
void ui_panel(int x, int y, int w, int h, int radius);
void ui_badge(int x, int y, const char *label);
void ui_divider(int x0, int x1, int y);
bool ui_point_in(int x, int y, int w, int h, int px, int py);
const char *ui_format_size(uint32_t bytes, char *buffer, size_t size);

#endif
