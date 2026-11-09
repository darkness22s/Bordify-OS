/* Linux adapters: keep original Bordify renderer/settings, store settings in HOME. */
#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include "fat16.h"
#include "heap.h"
#include "printf.h"

void *kmalloc(size_t size) { return malloc(size); }
void *kzalloc(size_t size) { return calloc(1, size); }
void *kmalloc_aligned(size_t size, size_t align) {
    void *p = NULL;
    if (posix_memalign(&p, align, size)) return NULL;
    return p;
}
int kprintf(const char *fmt, ...) {
    va_list a; va_start(a, fmt);
    int n = vfprintf(stderr, fmt, a);
    va_end(a);
    return n;
}
void panic(const char *fmt, ...) {
    va_list a; va_start(a, fmt);
    vfprintf(stderr, fmt, a);
    va_end(a);
    abort();
}
static bool settings_path(char *buf, size_t length) {
    const char *home = getenv("HOME");
    if (!home || !*home) return false;
    int n = snprintf(buf, length, "%s/.config/bordify/settings.cfg", home);
    return n > 0 && (size_t)n < length;
}
bool fat16_mounted(void) { return true; }
int fat16_read_path(const char *path, void *buf, uint32_t max_size) {
    if (strcmp(path, "SYSTEM/SETTINGS.CFG") != 0) return -1;
    char filename[4096];
    if (!settings_path(filename, sizeof(filename))) return -1;
    FILE *f = fopen(filename, "rb");
    if (!f) return -1;
    size_t n = fread(buf, 1, max_size, f);
    int error = ferror(f);
    fclose(f);
    return error ? -1 : (int)n;
}
int fat16_write_path(const char *path, const void *buf, uint32_t size) {
    if (strcmp(path, "SYSTEM/SETTINGS.CFG") != 0) return -1;
    char filename[4096], directory[4096];
    if (!settings_path(filename, sizeof(filename))) return -1;
    snprintf(directory, sizeof(directory), "%s", filename);
    char *slash = kstrrchr(directory, '/');
    if (!slash) return -1;
    *slash = '\0';
    if (mkdir(directory, 0700) != 0 && errno != EEXIST) {
        char parent[4096];
        snprintf(parent, sizeof(parent), "%s", directory);
        char *last = kstrrchr(parent, '/');
        if (!last) return -1;
        *last = '\0';
        if (mkdir(parent, 0700) != 0 && errno != EEXIST) return -1;
        if (mkdir(directory, 0700) != 0 && errno != EEXIST) return -1;
    }
    FILE *f = fopen(filename, "wb");
    if (!f) return -1;
    size_t n = fwrite(buf, 1, size, f);
    int err = fflush(f);
    if (fclose(f) != 0) err = -1;
    return (n == size && err == 0) ? (int)n : -1;
}
