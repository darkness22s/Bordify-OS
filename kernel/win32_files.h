#ifndef BORDIFY_WIN32_FILES_H
#define BORDIFY_WIN32_FILES_H
#include "types.h"

/* Research-stage Win32 file handle, isolated from kernel pointers.
 * One file, one handle, one fixed destination. No general filesystem API.
 * All address arguments are validated against the ring-3 memory window.
 */
#define WIN32_FILE_HANDLE 0x200u
#define WIN32_FILE_MAX_BYTES 1024u
#define WIN32_FILE_ERROR_NOT_FOUND 2u
#define WIN32_FILE_ERROR_ACCESS 5u
#define WIN32_FILE_ERROR_INVALID_HANDLE 6u
#define WIN32_FILE_ERROR_BAD_PARAMETER 87u
#define WIN32_FILE_ERROR_DISK_FULL 112u
#define WIN32_FILE_GENERIC_READ 0x80000000u
#define WIN32_FILE_GENERIC_WRITE 0x40000000u
typedef int (*win32_file_load_t)(uint8_t *out, size_t capacity, void *context);
typedef bool (*win32_file_save_t)(const uint8_t *bytes, size_t count, void *context);
typedef struct {
    uint8_t data[WIN32_FILE_MAX_BYTES];
    uint32_t size;
    uint32_t offset;
    uint32_t last_error;
    bool open;
    bool write_mode;
} win32_files_t;

void win32_files_reset(win32_files_t *rt);
uint32_t win32_files_open(win32_files_t *rt,
                          const uint8_t *guest, uint32_t base, size_t extent,
                          uint32_t path_pointer, uint32_t access,
                          uint32_t disposition,
                          win32_file_load_t load, void *context);
int win32_files_write(win32_files_t *rt, uint32_t handle,
                      const uint8_t *guest, uint32_t base, size_t extent,
                      uint32_t data_pointer, uint32_t count);
int win32_files_read(win32_files_t *rt, uint32_t handle,
                     uint8_t *guest, uint32_t base, size_t extent,
                     uint32_t data_pointer, uint32_t count);
bool win32_files_close(win32_files_t *rt, uint32_t handle,
                       win32_file_save_t save, void *context);
#endif
