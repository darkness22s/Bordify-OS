#ifndef BORDIFY_PRINTF_H
#define BORDIFY_PRINTF_H

#include "types.h"

int vsnprintf(char *out, size_t out_size, const char *fmt, va_list args);
int snprintf(char *out, size_t out_size, const char *fmt, ...);
int kprintf(const char *fmt, ...);
void panic(const char *fmt, ...) NORETURN;

#endif
