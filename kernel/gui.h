#ifndef BORDIFY_GUI_H
#define BORDIFY_GUI_H

#include "types.h"
#include "input.h"

typedef struct gui_window gui_window_t;

typedef struct {
    const char *title;
    int width;
    int height;
    void (*init)(gui_window_t *window);
    void (*draw)(gui_window_t *window);
    void (*mouse_down)(gui_window_t *window, int x, int y);
    void (*mouse_up)(gui_window_t *window, int x, int y);
    void (*key)(gui_window_t *window, const key_event_t *event);
    void (*tick)(gui_window_t *window);
} gui_app_t;

struct gui_window {
    const gui_app_t *app;
    void *state;
    int x;
    int y;
    int width;
    int height;
    int z;
    bool open;
    int drag_x;
    int drag_y;
};

void gui_init(void);
gui_window_t *gui_open_app(const gui_app_t *app, int x, int y);
void gui_close_window(gui_window_t *window);
bool gui_handle_mouse_down(int x, int y);
void gui_handle_mouse_up(int x, int y);
void gui_handle_mouse_move(int x, int y, bool left_down);
void gui_handle_key(const key_event_t *event);
void gui_handle_tick(void);
void gui_draw_windows(void);
bool gui_has_open_windows(void);
int gui_content_x(gui_window_t *window);
int gui_content_y(gui_window_t *window);
int gui_content_width(gui_window_t *window);
int gui_content_height(gui_window_t *window);

#define GUI_TITLEBAR_HEIGHT 38
#define GUI_MAX_WINDOWS 4

#endif
