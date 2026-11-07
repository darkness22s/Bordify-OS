#include "input.h"
#include "io.h"
#include "idt.h"
#include "gfx.h"
#include "string.h"
#include "printf.h"

#define KBD_QUEUE_SIZE 64

static volatile key_event_t key_queue[KBD_QUEUE_SIZE];
static volatile uint32_t key_head;
static volatile uint32_t key_tail;
static volatile bool shift_down;
static volatile bool caps_lock;
static volatile bool extended_key;
static volatile uint8_t mouse_cycle;
static volatile uint8_t mouse_packet[3];
static volatile mouse_state_t mouse;

static const char keymap[128] = {
    0, 27, '1', '2', '3', '4', '5', '6', '7', '8', '9', '0', '-', '=', 8, 9,
    'q', 'w', 'e', 'r', 't', 'y', 'u', 'i', 'o', 'p', '[', ']', 13, 0, 'a', 's',
    'd', 'f', 'g', 'h', 'j', 'k', 'l', ';', '\'', '`', 0, '\\', 'z', 'x', 'c', 'v',
    'b', 'n', 'm', ',', '.', '/', 0, '*', 0, ' ', 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
};

static const char keymap_shift[128] = {
    0, 27, '!', '@', '#', '$', '%', '^', '&', '*', '(', ')', '_', '+', 8, 9,
    'Q', 'W', 'E', 'R', 'T', 'Y', 'U', 'I', 'O', 'P', '{', '}', 13, 0, 'A', 'S',
    'D', 'F', 'G', 'H', 'J', 'K', 'L', ':', '"', '~', 0, '|', 'Z', 'X', 'C', 'V',
    'B', 'N', 'M', '<', '>', '?', 0, '*', 0, ' ', 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
};

static void queue_key(uint16_t code, char ch) {
    uint32_t next = (key_head + 1) % KBD_QUEUE_SIZE;
    if (next == key_tail) {
        key_tail = (key_tail + 1) % KBD_QUEUE_SIZE;
    }
    key_queue[key_head].code = code;
    key_queue[key_head].ch = ch;
    key_head = next;
}

static void handle_scancode(uint8_t scancode) {
    if (scancode == 0xE0) {
        extended_key = true;
        return;
    }
    bool released = (scancode & 0x80) != 0;
    uint8_t code = scancode & 0x7F;

    if (extended_key) {
        extended_key = false;
        if (!released) {
            switch (code) {
                case 0x48: queue_key(KEY_UP, 0); break;
                case 0x50: queue_key(KEY_DOWN, 0); break;
                case 0x4B: queue_key(KEY_LEFT, 0); break;
                case 0x4D: queue_key(KEY_RIGHT, 0); break;
                case 0x53: queue_key(KEY_DELETE, 0); break;
                case 0x47: queue_key(KEY_HOME, 0); break;
                case 0x4F: queue_key(KEY_END, 0); break;
                case 0x49: queue_key(KEY_PAGE_UP, 0); break;
                case 0x51: queue_key(KEY_PAGE_DOWN, 0); break;
                default: break;
            }
        }
        return;
    }

    if (code == 0x2A || code == 0x36) {
        shift_down = !released;
        return;
    }
    if (code == 0x3A && !released) {
        caps_lock = !caps_lock;
        return;
    }
    if (released) {
        return;
    }
    if (code == 0x3B) {
        queue_key(KEY_F1, 0);
        return;
    }
    if (code == 0x3C) {
        queue_key(KEY_F2, 0);
        return;
    }
    if (code == 0x3D) {
        queue_key(KEY_F3, 0);
        return;
    }
    if (code == 0x1D || code == 0x38) {
        return;
    }

    char ch = shift_down ? keymap_shift[code] : keymap[code];
    if (ch >= 'a' && ch <= 'z' && caps_lock) {
        ch = (char)(ch - 32);
    } else if (ch >= 'A' && ch <= 'Z' && caps_lock && !shift_down) {
        ch = (char)(ch + 32);
    }
    if (ch == KEY_BACKSPACE || ch == KEY_ENTER || ch == KEY_TAB || ch == KEY_ESC) {
        queue_key((uint16_t)(unsigned char)ch, 0);
    } else if (ch) {
        queue_key((uint16_t)(unsigned char)ch, ch);
    }
}

static void keyboard_irq(void) {
    uint8_t status = inb(0x64);
    if (status & 0x01) {
        uint8_t scancode = inb(0x60);
        handle_scancode(scancode);
    }
}

static void mouse_irq(void) {
    uint8_t status = inb(0x64);
    if (!(status & 0x01)) {
        return;
    }
    uint8_t byte = inb(0x60);
    switch (mouse_cycle) {
        case 0:
            if (!(byte & 0x08)) {
                return;
            }
            mouse_packet[0] = byte;
            mouse_cycle = 1;
            break;
        case 1:
            mouse_packet[1] = byte;
            mouse_cycle = 2;
            break;
        case 2: {
            mouse_packet[2] = byte;
            mouse_cycle = 0;
            uint8_t flags = mouse_packet[0];
            int dx = mouse_packet[1];
            int dy = mouse_packet[2];
            if (flags & 0x40) {
                dx |= ~0xFF;
            }
            if (flags & 0x80) {
                dy |= ~0xFF;
            }
            if (flags & 0x10) {
                dx = (int8_t)mouse_packet[1];
            }
            if (flags & 0x20) {
                dy = (int8_t)mouse_packet[2];
            }
            int new_x = mouse.x + dx;
            int new_y = mouse.y - dy;
            if (new_x < 0) {
                new_x = 0;
            }
            if (new_y < 0) {
                new_y = 0;
            }
            if (new_x >= gfx_width()) {
                new_x = gfx_width() - 1;
            }
            if (new_y >= gfx_height()) {
                new_y = gfx_height() - 1;
            }
            mouse.x = new_x;
            mouse.y = new_y;
            mouse.left = (flags & 0x01) != 0;
            mouse.right = (flags & 0x02) != 0;
            mouse.middle = (flags & 0x04) != 0;
            break;
        }
        default:
            mouse_cycle = 0;
            break;
    }
}

static bool wait_input_clear(void) {
    for (int i = 0; i < 100000; i++) {
        if (!(inb(0x64) & 0x02)) {
            return true;
        }
    }
    return false;
}

static bool wait_output_full(void) {
    for (int i = 0; i < 100000; i++) {
        if (inb(0x64) & 0x01) {
            return true;
        }
    }
    return false;
}

static void ps2_write(uint8_t value) {
    wait_input_clear();
    outb(0x60, value);
}

static void ps2_write_mouse(uint8_t value) {
    wait_input_clear();
    outb(0x64, 0xD4);
    wait_input_clear();
    outb(0x60, value);
}

static bool ps2_read_ack(void) {
    for (int i = 0; i < 10; i++) {
        if (!wait_output_full()) {
            return false;
        }
        uint8_t value = inb(0x60);
        if (value == 0xFA) {
            return true;
        }
    }
    return false;
}

void input_init(void) {
    key_head = 0;
    key_tail = 0;
    shift_down = false;
    caps_lock = false;
    extended_key = false;
    mouse_cycle = 0;
    mouse.x = gfx_width() / 2;
    mouse.y = gfx_height() / 2;
    mouse.left = mouse.middle = mouse.right = false;

    outb(0x64, 0xAD);
    outb(0x64, 0xA7);
    while (inb(0x64) & 0x01) {
        (void)inb(0x60);
    }

    wait_input_clear();
    outb(0x64, 0x20);
    wait_output_full();
    uint8_t config = inb(0x60);
    config |= 0x03;
    config &= (uint8_t)~0x30;
    wait_input_clear();
    outb(0x64, 0x60);
    wait_input_clear();
    outb(0x60, config);

    outb(0x64, 0xAE);
    wait_input_clear();
    ps2_write(0xF4);
    ps2_read_ack();

    outb(0x64, 0xA7);
    wait_input_clear();
    ps2_write_mouse(0xFF);
    ps2_read_ack();
    wait_output_full();
    uint8_t mouse_id = inb(0x60);
    (void)mouse_id;
    ps2_write_mouse(0xF6);
    ps2_read_ack();
    ps2_write_mouse(0xF4);
    ps2_read_ack();

    irq_register(1, keyboard_irq);
    irq_register(12, mouse_irq);
    irq_unmask(1);
    irq_unmask(2);
    irq_unmask(12);
    kprintf("[input] PS/2 keyboard and mouse ready\n");
}

bool input_pop_key(key_event_t *event) {
    if (!event) {
        return false;
    }
    uint32_t irq = irq_save();
    if (key_tail == key_head) {
        irq_restore(irq);
        return false;
    }
    *event = key_queue[key_tail];
    key_tail = (key_tail + 1) % KBD_QUEUE_SIZE;
    irq_restore(irq);
    return true;
}

void input_mouse_state(mouse_state_t *state) {
    if (!state) {
        return;
    }
    uint32_t irq = irq_save();
    state->x = mouse.x;
    state->y = mouse.y;
    state->left = mouse.left;
    state->middle = mouse.middle;
    state->right = mouse.right;
    irq_restore(irq);
}

void input_set_mouse_position(int x, int y) {
    uint32_t irq = irq_save();
    mouse.x = x;
    mouse.y = y;
    irq_restore(irq);
}
