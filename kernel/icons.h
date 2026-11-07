#ifndef BORDIFY_ICONS_H
#define BORDIFY_ICONS_H

#include "types.h"

enum {
    APP_FILES = 0,
    APP_WEB = 1,
    APP_SETTINGS = 2,
};

void icon_app(int app, int x, int y, int size);
void icon_folder(int x, int y, int size, uint32_t fill, uint32_t ink);
void icon_document(int x, int y, int size, uint32_t fill, uint32_t ink, uint32_t accent);
void icon_chrome(int x, int y, int size);
void icon_tile_background(int x, int y, int size);

#endif
