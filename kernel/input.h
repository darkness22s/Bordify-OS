#ifndef BORDIFY_INPUT_H
#define BORDIFY_INPUT_H

#include "types.h"

enum {
    KEY_ESC = 27,
    KEY_BACKSPACE = 8,
    KEY_TAB = 9,
    KEY_ENTER = 13,
    KEY_UP = 0x100,
    KEY_DOWN,
    KEY_LEFT,
    KEY_RIGHT,
    KEY_DELETE,
    KEY_HOME,
    KEY_END,
    KEY_PAGE_UP,
    KEY_PAGE_DOWN,
    KEY_F1,
    KEY_F2,
    KEY_F3,
};

typedef struct {
    uint16_t code;
    char ch;
} key_event_t;

typedef struct {
    int x;
    int y;
    bool left;
    bool middle;
    bool right;
} mouse_state_t;

void input_init(void);
bool input_pop_key(key_event_t *event);
void input_mouse_state(mouse_state_t *state);
void input_set_mouse_position(int x, int y);

#endif
