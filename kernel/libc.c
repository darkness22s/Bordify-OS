#include "types.h"
#include "string.h"

void *memcpy(void *dst, const void *src, size_t n) {
    return kmemcpy(dst, src, n);
}

void *memset(void *dst, int value, size_t n) {
    return kmemset(dst, value, n);
}

void *memmove(void *dst, const void *src, size_t n) {
    return kmemmove(dst, src, n);
}

int memcmp(const void *a, const void *b, size_t n) {
    return kmemcmp(a, b, n);
}
