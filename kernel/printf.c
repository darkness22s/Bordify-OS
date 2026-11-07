#include "printf.h"
#include "serial.h"
#include "io.h"
#include "string.h"

static void out_char(char *out, size_t out_size, size_t *pos, char c) {
    if (out && *pos + 1 < out_size) {
        out[*pos] = c;
    }
    (*pos)++;
}

static void out_str(char *out, size_t out_size, size_t *pos, const char *s) {
    if (!s) {
        s = "(null)";
    }
    while (*s) {
        out_char(out, out_size, pos, *s++);
    }
}

static void out_unsigned(char *out, size_t out_size, size_t *pos,
                         uint32_t value, int base, bool upper, int width, char pad) {
    char buf[36];
    int n = 0;
    const char *digits = upper ? "0123456789ABCDEF" : "0123456789abcdef";
    if (value == 0) {
        buf[n++] = '0';
    }
    while (value) {
        buf[n++] = digits[value % (uint32_t)base];
        value /= (uint32_t)base;
    }
    while (n < width) {
        out_char(out, out_size, pos, pad);
        width--;
    }
    while (n--) {
        out_char(out, out_size, pos, buf[n]);
    }
}

int vsnprintf(char *out, size_t out_size, const char *fmt, va_list args) {
    size_t pos = 0;
    while (fmt && *fmt) {
        if (*fmt != '%') {
            out_char(out, out_size, &pos, *fmt++);
            continue;
        }
        fmt++;
        char pad = ' ';
        int width = 0;
        if (*fmt == '0') {
            pad = '0';
            fmt++;
        }
        while (*fmt >= '0' && *fmt <= '9') {
            width = width * 10 + (*fmt - '0');
            fmt++;
        }
        switch (*fmt) {
            case 0:
                fmt--;
                break;
            case 'c': {
                char c = (char)va_arg(args, int);
                out_char(out, out_size, &pos, c);
                break;
            }
            case 's':
                out_str(out, out_size, &pos, va_arg(args, const char *));
                break;
            case 'd':
            case 'i': {
                int32_t v = va_arg(args, int32_t);
                if (v < 0) {
                    out_char(out, out_size, &pos, '-');
                    out_unsigned(out, out_size, &pos, (uint32_t)(-v), 10, false, width, pad);
                } else {
                    out_unsigned(out, out_size, &pos, (uint32_t)v, 10, false, width, pad);
                }
                break;
            }
            case 'u':
                out_unsigned(out, out_size, &pos, va_arg(args, uint32_t), 10, false, width, pad);
                break;
            case 'x':
                out_unsigned(out, out_size, &pos, va_arg(args, uint32_t), 16, false, width, pad);
                break;
            case 'X':
                out_unsigned(out, out_size, &pos, va_arg(args, uint32_t), 16, true, width, pad);
                break;
            case 'p': {
                out_str(out, out_size, &pos, "0x");
                out_unsigned(out, out_size, &pos, (uint32_t)(uintptr_t)va_arg(args, void *),
                             16, false, 8, '0');
                break;
            }
            case '%':
                out_char(out, out_size, &pos, '%');
                break;
            default:
                out_char(out, out_size, &pos, '%');
                out_char(out, out_size, &pos, *fmt);
                break;
        }
        fmt++;
    }
    if (out && out_size) {
        out[pos < out_size ? pos : out_size - 1] = 0;
    }
    return (int)pos;
}

int snprintf(char *out, size_t out_size, const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    int result = vsnprintf(out, out_size, fmt, args);
    va_end(args);
    return result;
}

int kprintf(const char *fmt, ...) {
    char buffer[1024];
    va_list args;
    va_start(args, fmt);
    int result = vsnprintf(buffer, sizeof(buffer), fmt, args);
    va_end(args);
    serial_write(buffer);
    return result;
}

void panic(const char *fmt, ...) {
    char buffer[1024];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buffer, sizeof(buffer), fmt, args);
    va_end(args);
    cpu_cli();
    kprintf("\n\n*** BORDIFY PANIC ***\n%s\n", buffer);
    for (;;) {
        cpu_halt();
    }
}
