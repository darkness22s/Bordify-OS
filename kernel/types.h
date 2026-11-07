#ifndef BORDIFY_TYPES_H
#define BORDIFY_TYPES_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <stdarg.h>

#define PACKED __attribute__((packed))
#define NORETURN __attribute__((noreturn))
#define UNUSED(x) (void)(x)

#define NULLPTR ((void *)0)
#define MIN(a, b) ((a) < (b) ? (a) : (b))
#define MAX(a, b) ((a) > (b) ? (a) : (b))
#define ABS(a) ((a) < 0 ? -(a) : (a))
#define ARRAY_LEN(x) (sizeof(x) / sizeof((x)[0]))
#define ALIGN_UP(v, a) (((v) + ((a) - 1)) & ~((a) - 1))

typedef uint32_t phys_addr_t;
typedef uint32_t virt_addr_t;

#endif
