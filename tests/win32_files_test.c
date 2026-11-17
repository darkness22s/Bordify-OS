#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "win32_pe.h"
#include "win32_files.h"

static uint8_t memory[4u * 1024u * 1024u];
static uint8_t persisted[1024];
static size_t persisted_count;
static bool persisted_exists;
static bool refuse_save;
static bool save(const uint8_t *buf,size_t n,void *ctx) {
    (void)ctx;
    if (refuse_save || n>sizeof(persisted)) return false;
    memcpy(persisted,buf,n);persisted_count=n;persisted_exists=true;return true;
}
static int load(uint8_t *buf,size_t cap,void *ctx) {
    (void)ctx;
    if (!persisted_exists || cap<persisted_count) return -1;
    memcpy(buf,persisted,persisted_count);return (int)persisted_count;
}
static uint32_t rd32(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1]<<8) |
           ((uint32_t)p[2]<<16) | ((uint32_t)p[3]<<24);
}
int main(void) {
    uint8_t pe[4096];
    FILE *f=fopen("build/apps/winio.exe","rb");assert(f);
    size_t file_size=fread(pe,1,sizeof(pe),f);fclose(f);
    assert(file_size==0xA00);
    win32_pe_info_t image;
    assert(win32_pe_stage(pe,file_size,memory,sizeof(memory),&image));
    assert(image.imported_functions==7 && image.entry==0x40001000u);
    uint32_t base=WIN32_PE_USER_BASE+WIN32_PE_RUNTIME_OFFSET;
    for(unsigned i=0;i<7;++i)
        assert(rd32(memory+0x1290+i*4)>=base &&
               rd32(memory+0x1290+i*4)<base+7*64);
    assert(rd32(memory+0x1290)==base+4*64); /* CreateFileA */
    assert(rd32(memory+0x1294)==base+2*64); /* WriteFile */
    assert(rd32(memory+0x1298)==base+5*64); /* ReadFile */
    assert(rd32(memory+0x129c)==base+6*64); /* CloseHandle */
    assert(win32_pe_install_bridges(memory,sizeof(memory)));

    const uint32_t PATH=WIN32_PE_USER_BASE+0x1350;
    const uint32_t TEXT=WIN32_PE_USER_BASE+0x1380;
    const uint32_t DST=WIN32_PE_USER_BASE+0x1400;
    const char *expected=
        "BORDIFY WIN32 FILE: CreateFileA WriteFile ReadFile CloseHandle verified\r\n";
    unsigned n=(unsigned)strlen(expected);
    assert(!memcmp(memory+0x1380,expected,n));

    win32_files_t fs;
    win32_files_reset(&fs);
    assert(win32_files_open(&fs,memory,WIN32_PE_USER_BASE,sizeof(memory),
                            PATH,WIN32_FILE_GENERIC_READ,3,load,NULL)==UINT32_MAX);
    assert(fs.last_error==WIN32_FILE_ERROR_NOT_FOUND);
    assert(win32_files_open(&fs,memory,WIN32_PE_USER_BASE,sizeof(memory),
                            PATH,WIN32_FILE_GENERIC_WRITE,2,load,NULL)==WIN32_FILE_HANDLE);
    assert(win32_files_write(&fs,WIN32_FILE_HANDLE,memory,WIN32_PE_USER_BASE,
                             sizeof(memory),TEXT,n)==(int)n);
    assert(win32_files_write(&fs,WIN32_FILE_HANDLE,memory,WIN32_PE_USER_BASE,
                             sizeof(memory),WIN32_PE_USER_BASE-1,1)==-1);
    assert(fs.last_error==WIN32_FILE_ERROR_BAD_PARAMETER);
    assert(win32_files_write(&fs,WIN32_FILE_HANDLE,memory,WIN32_PE_USER_BASE,
                             sizeof(memory),TEXT,WIN32_FILE_MAX_BYTES)==-1);
    assert(fs.last_error==WIN32_FILE_ERROR_DISK_FULL);
    refuse_save=true;
    assert(!win32_files_close(&fs,WIN32_FILE_HANDLE,save,NULL));
    refuse_save=false;
    assert(win32_files_close(&fs,WIN32_FILE_HANDLE,save,NULL));
    assert(persisted_count==n && !memcmp(persisted,expected,n));

    win32_files_reset(&fs);
    assert(win32_files_open(&fs,memory,WIN32_PE_USER_BASE,sizeof(memory),
                            PATH,WIN32_FILE_GENERIC_READ,3,load,NULL)==WIN32_FILE_HANDLE);
    assert(win32_files_write(&fs,WIN32_FILE_HANDLE,memory,WIN32_PE_USER_BASE,
                             sizeof(memory),TEXT,n)==-1);
    assert(fs.last_error==WIN32_FILE_ERROR_INVALID_HANDLE);
    assert(win32_files_read(&fs,WIN32_FILE_HANDLE,memory,WIN32_PE_USER_BASE,
                            sizeof(memory),DST,n)==(int)n);
    assert(!memcmp(memory+0x1400,expected,n));
    assert(win32_files_read(&fs,WIN32_FILE_HANDLE,memory,WIN32_PE_USER_BASE,
                            sizeof(memory),DST,n)==0);
    assert(win32_files_close(&fs,WIN32_FILE_HANDLE,save,NULL));

    win32_files_reset(&fs);
    memory[0x1350]='X';
    assert(win32_files_open(&fs,memory,WIN32_PE_USER_BASE,sizeof(memory),
                            PATH,WIN32_FILE_GENERIC_WRITE,2,load,NULL)==UINT32_MAX);
    assert(fs.last_error==WIN32_FILE_ERROR_ACCESS);
    memory[0x1350]='C';
    assert(win32_files_open(&fs,memory,WIN32_PE_USER_BASE,sizeof(memory),
                            PATH,WIN32_FILE_GENERIC_WRITE,3,load,NULL)==UINT32_MAX);
    assert(!win32_files_close(&fs,WIN32_FILE_HANDLE,save,NULL));
    assert(fs.last_error==WIN32_FILE_ERROR_INVALID_HANDLE);

    /* Unknown DLL exports and broken image inputs remain rejected. */
    pe[0x200+(0x12b0-0x1000)+2]='X';
    assert(!win32_pe_stage(pe,file_size,memory,sizeof(memory),&image));
    pe[0x200+(0x12b0-0x1000)+2]='C';
    assert(!win32_pe_stage(pe,100,memory,sizeof(memory),&image));
    /* CREATE_ALWAYS followed by immediate CloseHandle must persist a
     * zero-byte file and allow OPEN_EXISTING to read EOF successfully. */
    win32_files_reset(&fs);
    assert(win32_files_open(&fs,memory,WIN32_PE_USER_BASE,sizeof(memory),
                            PATH,WIN32_FILE_GENERIC_WRITE,2,load,NULL)==WIN32_FILE_HANDLE);
    assert(win32_files_close(&fs,WIN32_FILE_HANDLE,save,NULL));
    assert(persisted_exists && persisted_count==0);
    win32_files_reset(&fs);
    assert(win32_files_open(&fs,memory,WIN32_PE_USER_BASE,sizeof(memory),
                            PATH,WIN32_FILE_GENERIC_READ,3,load,NULL)==WIN32_FILE_HANDLE);
    assert(win32_files_read(&fs,WIN32_FILE_HANDLE,memory,WIN32_PE_USER_BASE,
                            sizeof(memory),DST,16)==0);
    assert(win32_files_close(&fs,WIN32_FILE_HANDLE,save,NULL));
    puts("Win32 kernel32 ring-3 file I/O and PE32 loader tests: PASS");
    return 0;
}
