#include "win32_files.h"

/* Deliberately fixed Windows path; protects all unrelated FAT16 files. */
static const char allowed_path[]="C:\\BORDIFY\\WIN32.TXT";
static bool inside(uint32_t p, size_t n, uint32_t base, size_t extent) {
    return p>=base && (uint64_t)(p-base)<=extent &&
           n<=extent-(size_t)(p-base);
}
static bool matches_path(const uint8_t *memory,uint32_t base,
                         size_t extent,uint32_t guest_address) {
    if (!memory || !inside(guest_address,sizeof(allowed_path),base,extent))
        return false;
    size_t offset=guest_address-base;
    for(size_t i=0;i<sizeof(allowed_path);++i)
        if(memory[offset+i]!=(uint8_t)allowed_path[i]) return false;
    return true;
}
void win32_files_reset(win32_files_t *rt) {
    if (!rt) return;
    rt->size=0;rt->offset=0;rt->last_error=0;rt->open=false;
    rt->write_mode=false;
}
uint32_t win32_files_open(win32_files_t *rt,
                          const uint8_t *guest,uint32_t base,size_t extent,
                          uint32_t filename,uint32_t access,
                          uint32_t disposition,
                          win32_file_load_t load,void *context) {
    if (!rt) return UINT32_MAX;
    if (!matches_path(guest,base,extent,filename) || rt->open) {
        rt->last_error=WIN32_FILE_ERROR_ACCESS;return UINT32_MAX;
    }
    rt->size=0;rt->offset=0;
    if (access==WIN32_FILE_GENERIC_WRITE && disposition==2u) {
        rt->write_mode=true;
    } else if (access==WIN32_FILE_GENERIC_READ && disposition==3u && load) {
        int n=load(rt->data,sizeof(rt->data),context);
        if (n<0 || (unsigned)n>sizeof(rt->data)) {
            rt->last_error=WIN32_FILE_ERROR_NOT_FOUND;return UINT32_MAX;
        }
        rt->size=(uint32_t)n;rt->write_mode=false;
    } else {
        rt->last_error=WIN32_FILE_ERROR_BAD_PARAMETER;return UINT32_MAX;
    }
    rt->open=true;
    rt->last_error=0;
    return WIN32_FILE_HANDLE;
}
int win32_files_write(win32_files_t *rt,uint32_t handle,
                      const uint8_t *guest,uint32_t base,size_t extent,
                      uint32_t ptr,uint32_t count) {
    if (!rt) return -1;
    if (!rt->open || !rt->write_mode || handle!=WIN32_FILE_HANDLE) {
        rt->last_error=WIN32_FILE_ERROR_INVALID_HANDLE;return -1;
    }
    if (!guest || !count || !inside(ptr,count,base,extent)) {
        rt->last_error=WIN32_FILE_ERROR_BAD_PARAMETER;return -1;
    }
    if (count>WIN32_FILE_MAX_BYTES-rt->size) {
        rt->last_error=WIN32_FILE_ERROR_DISK_FULL;return -1;
    }
    for(uint32_t i=0;i<count;++i) rt->data[rt->size+i]=guest[ptr-base+i];
    rt->size+=count;
    rt->last_error=0;
    return (int)count;
}
int win32_files_read(win32_files_t *rt,uint32_t handle,
                     uint8_t *guest,uint32_t base,size_t extent,
                     uint32_t ptr,uint32_t count) {
    if (!rt) return -1;
    if (!rt->open || rt->write_mode || handle!=WIN32_FILE_HANDLE) {
        rt->last_error=WIN32_FILE_ERROR_INVALID_HANDLE;return -1;
    }
    if (!guest || !inside(ptr,count,base,extent)) {
        rt->last_error=WIN32_FILE_ERROR_BAD_PARAMETER;return -1;
    }
    if(count>rt->size-rt->offset)count=rt->size-rt->offset;
    for(uint32_t i=0;i<count;++i)guest[ptr-base+i]=rt->data[rt->offset+i];
    rt->offset+=count;rt->last_error=0;return (int)count;
}
bool win32_files_close(win32_files_t *rt,uint32_t handle,
                       win32_file_save_t save,void *context) {
    if (!rt) return false;
    if (!rt->open || handle!=WIN32_FILE_HANDLE) {
        rt->last_error=WIN32_FILE_ERROR_INVALID_HANDLE;return false;
    }
    if (rt->write_mode &&
        (!save || !save(rt->data,rt->size,context))) {
        rt->last_error=WIN32_FILE_ERROR_ACCESS;return false;
    }
    rt->open=false;rt->write_mode=false;rt->last_error=0;return true;
}
