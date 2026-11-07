#include "gui.h"
#include "gfx.h"
#include "settings.h"
#include "input.h"
#include "string.h"
#include "printf.h"

static gui_window_t windows[GUI_MAX_WINDOWS];
static int z_counter;
static gui_window_t *focused;
static gui_window_t *dragging;

static int close_button_x(gui_window_t *window) {
    return window->x + window->width - 30;
}

static int close_button_y(gui_window_t *window) {
    return window->y + 9;
}

void gui_init(void) {
    kmemset(windows, 0, sizeof(windows));
    z_counter = 1;
    focused = NULLPTR;
    dragging = NULLPTR;
}

bool gui_has_open_windows(void) {
    for (int i = 0; i < GUI_MAX_WINDOWS; i++) {
        if (windows[i].open) {
            return true;
        }
    }
    return false;
}

int gui_content_x(gui_window_t *window) {
    return window->x + 1;
}

int gui_content_y(gui_window_t *window) {
    return window->y + GUI_TITLEBAR_HEIGHT;
}

int gui_content_width(gui_window_t *window) {
    return window->width - 2;
}

int gui_content_height(gui_window_t *window) {
    return window->height - GUI_TITLEBAR_HEIGHT - 1;
}

static void focus_window(gui_window_t *window) {
    if (!window || !window->open) {
        return;
    }
    focused = window;
    window->z = ++z_counter;
}

gui_window_t *gui_open_app(const gui_app_t *app, int x, int y) {
    if (!app) {
        return NULLPTR;
    }
    for (int i = 0; i < GUI_MAX_WINDOWS; i++) {
        if (windows[i].open && windows[i].app == app) {
            focus_window(&windows[i]);
            return &windows[i];
        }
    }
    for (int i = 0; i < GUI_MAX_WINDOWS; i++) {
        if (!windows[i].open) {
            gui_window_t *window = &windows[i];
            kmemset(window, 0, sizeof(*window));
            window->app = app;
            window->open = true;
            window->width = app->width;
            window->height = app->height;
            window->x = x;
            window->y = y;
            if (window->x + window->width > gfx_width() - 16) {
                window->x = gfx_width() - window->width - 16;
            }
            if (window->y + window->height > gfx_height() - 48) {
                window->y = gfx_height() - window->height - 48;
            }
            if (window->x < 16) {
                window->x = 16;
            }
            if (window->y < 16) {
                window->y = 16;
            }
            window->z = ++z_counter;
            focused = window;
            if (app->init) {
                app->init(window);
            }
            kprintf("[gui] opened %s\n", app->title);
            return window;
        }
    }
    return NULLPTR;
}

void gui_close_window(gui_window_t *window) {
    if (!window) {
        return;
    }
    window->open = false;
    if (focused == window) {
        focused = NULLPTR;
        int best_z = -1;
        for (int i = 0; i < GUI_MAX_WINDOWS; i++) {
            if (windows[i].open && windows[i].z > best_z) {
                best_z = windows[i].z;
                focused = &windows[i];
            }
        }
    }
    if (dragging == window) {
        dragging = NULLPTR;
    }
}

static gui_window_t *top_window_at(int x, int y) {
    gui_window_t *best = NULLPTR;
    int best_z = -1;
    for (int i = 0; i < GUI_MAX_WINDOWS; i++) {
        gui_window_t *window = &windows[i];
        if (!window->open) {
            continue;
        }
        if (x >= window->x && x < window->x + window->width &&
            y >= window->y && y < window->y + window->height &&
            window->z > best_z) {
            best = window;
            best_z = window->z;
        }
    }
    return best;
}

bool gui_handle_mouse_down(int x, int y) {
    gui_window_t *window = top_window_at(x, y);
    if (!window) {
        return false;
    }
    focus_window(window);
    if (x >= close_button_x(window) && x < close_button_x(window) + 22 &&
        y >= close_button_y(window) && y < close_button_y(window) + 22) {
        gui_close_window(window);
        return true;
    }
    int local_y = y - window->y;
    if (local_y < GUI_TITLEBAR_HEIGHT) {
        dragging = window;
        window->drag_x = x - window->x;
        window->drag_y = y - window->y;
        return true;
    }
    if (window->app->mouse_down) {
        window->app->mouse_down(window, x, y);
    }
    return true;
}

void gui_handle_mouse_up(int x, int y) {
    if (dragging) {
        dragging = NULLPTR;
        return;
    }
    gui_window_t *window = top_window_at(x, y);
    if (window && window->app->mouse_up) {
        window->app->mouse_up(window, x, y);
    }
}

void gui_handle_mouse_move(int x, int y, bool left_down) {
    if (dragging && left_down) {
        int nx = x - dragging->drag_x;
        int ny = y - dragging->drag_y;
        if (nx < 8) {
            nx = 8;
        }
        if (ny < 8) {
            ny = 8;
        }
        if (nx + dragging->width > gfx_width() - 8) {
            nx = gfx_width() - dragging->width - 8;
        }
        if (ny + dragging->height > gfx_height() - 52) {
            ny = gfx_height() - dragging->height - 52;
        }
        dragging->x = nx;
        dragging->y = ny;
    }
}

void gui_handle_key(const key_event_t *event) {
    if (!event) {
        return;
    }
    if (focused && focused->open && focused->app->key) {
        focused->app->key(focused, event);
    }
}

void gui_handle_tick(void) {
    for (int i = 0; i < GUI_MAX_WINDOWS; i++) {
        if (windows[i].open && windows[i].app->tick) {
            windows[i].app->tick(&windows[i]);
        }
    }
}

static void draw_window(gui_window_t *window) {
    int x = window->x;
    int y = window->y;
    int w = window->width;
    int h = window->height;
    int radius = 12;

    gfx_rounded_fill(x + 3, y + 5, w, h, radius, settings_shadow());
    gfx_rounded_fill(x, y, w, h, radius, settings_panel());
    gfx_rounded_border(x, y, w, h, radius, 1, settings_strong());

    int title_y = y + (GUI_TITLEBAR_HEIGHT - font_ui_bold.line_height) / 2;
    gfx_text_clip(&font_ui_bold, x + 18, title_y, window->app->title, w - 70,
                  focused == window ? settings_ink() : settings_muted());
    gfx_hline(x + 1, x + w - 2, y + GUI_TITLEBAR_HEIGHT - 1, settings_line());

    mouse_state_t mouse;
    input_mouse_state(&mouse);
    bool close_hover = mouse.x >= close_button_x(window) &&
                       mouse.x < close_button_x(window) + 22 &&
                       mouse.y >= close_button_y(window) &&
                       mouse.y < close_button_y(window) + 22;
    int bx = close_button_x(window);
    int by = close_button_y(window);
    gfx_circle_outline(bx + 11, by + 11, 10, 1, close_hover ? settings_accent() : settings_line());
    if (close_hover) {
        gfx_circle_fill(bx + 11, by + 11, 9, settings_accent_soft());
    }
    gfx_line(bx + 7, by + 7, bx + 15, by + 15, settings_ink());
    gfx_line(bx + 15, by + 7, bx + 7, by + 15, settings_ink());

    gfx_set_clip(gui_content_x(window), gui_content_y(window),
                 gui_content_width(window), gui_content_height(window));
    if (window->app->draw) {
        window->app->draw(window);
    }
    gfx_reset_clip();

    gfx_rounded_border(x, y, w, h, radius, 1, settings_strong());
}

void gui_draw_windows(void) {
    for (int z = 1; z <= z_counter; z++) {
        for (int i = 0; i < GUI_MAX_WINDOWS; i++) {
            if (windows[i].open && windows[i].z == z) {
                draw_window(&windows[i]);
            }
        }
    }
}
