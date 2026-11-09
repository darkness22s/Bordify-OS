#include "win32_dll.h"
#include "win32_pe.h"

/* These are virtual modules containing only the ABI shims provided by the
 * current kernel. Names resolve to addresses inside the *guest's* ring-3
 * trampoline page, never a pointer to a kernel function or host library.
 */
typedef struct { const char *name; uint8_t slot; } export_t;
static const export_t kernel32_exports[]={
    {"ExitProcess",0},{"GetStdHandle",1},{"WriteFile",2},
    {"GetLastError",3},{"CreateFileA",4},{"ReadFile",5},
    {"CloseHandle",6},{"GetCurrentProcessId",7},
    {"GetCurrentThreadId",8},{"GetTickCount",9},
    {"GetModuleHandleA",10},{"GetModuleHandleW",11},
    {"GetCommandLineA",12},{"GetCommandLineW",13},
    {"SetLastError",14},{"IsDebuggerPresent",15},
    {"VirtualAlloc",27},{"VirtualFree",28},
    {"LoadLibraryA",29},{"GetProcAddress",30},{"FreeLibrary",31},
    {"LoadLibraryW",32}
};
static const export_t msvcrt_exports[]={
    {"strlen",16},{"memcpy",17},{"memset",18},{"memcmp",19},
    {"strcmp",20},{"strncmp",21},{"malloc",22},{"free",23},
    {"calloc",24},{"memmove",25},{"realloc",26}
};
static bool in_range(const win32_dll_t *dll,uint32_t address) {
    return dll && dll->guest && address>=dll->base &&
           (size_t)(address-dll->base)<dll->bytes;
}
static bool read_name(const win32_dll_t *dll,uint32_t address,
                      char output[65]) {
    if(!in_range(dll,address))return false;
    size_t offset=(size_t)(address-dll->base);
    for(size_t i=0;i<64u;++i) {
        if(i>=dll->bytes-offset)return false;
        char c=(char)dll->guest[offset+i];
        output[i]=c;
        if(!c)return i!=0u;
    }
    output[64]=0;
    return false; /* unterminated or suspiciously long */
}
/* UTF-16LE names stay in user memory and must fit in 64 ASCII code units.
 * Non-ASCII name resolution needs a real Unicode-aware DLL loader. */
static bool read_wide_name(const win32_dll_t *dll,uint32_t address,
                           char output[65]) {
    if(!in_range(dll,address))return false;
    size_t offset=(size_t)(address-dll->base);
    size_t available=dll->bytes-offset;
    for(size_t i=0;i<64u;++i) {
        size_t at=i*2u;
        if(at>available || available-at<2u)return false;
        uint8_t low=dll->guest[offset+at];
        uint8_t high=dll->guest[offset+at+1u];
        if(high!=0u || low>127u)return false;
        output[i]=(char)low;
        if(low==0u)return i!=0u;
    }
    output[64]=0;
    return false;
}
static char lower_ascii(char c) {
    return (c>='A'&&c<='Z')?(char)(c+('a'-'A')):c;
}
static bool equals_ci(const char *a,const char *b) {
    for(size_t i=0;i<65u;++i) {
        if(lower_ascii(a[i])!=lower_ascii(b[i]))return false;
        if(!a[i])return true;
    }
    return false;
}
static bool equals_exact(const char *a,const char *b) {
    for(size_t i=0;i<65u;++i) {
        if(a[i]!=b[i])return false;
        if(!a[i])return true;
    }
    return false;
}
static uint32_t fail(win32_dll_t *dll,uint32_t error) {
    if(dll)dll->last_error=error;
    return 0;
}
void win32_dll_init(win32_dll_t *dll,const uint8_t *guest,
                    uint32_t base,size_t bytes) {
    if(!dll)return;
    dll->guest=guest;dll->base=base;dll->bytes=bytes;
    dll->last_error=0;
    dll->refs[0]=0;dll->refs[1]=0;
    dll->pinned[0]=false;dll->pinned[1]=false;
}
static int module_id(uint32_t handle) {
    if(handle==WIN32_DLL_KERNEL32_HANDLE)return 0;
    if(handle==WIN32_DLL_MSVCRT_HANDLE)return 1;
    return -1;
}
static int module_by_name(const char *name) {
    if(equals_ci(name,"kernel32.dll")||equals_ci(name,"kernel32"))return 0;
    if(equals_ci(name,"msvcrt.dll")||equals_ci(name,"msvcrt"))return 1;
    return -1;
}
void win32_dll_pin_imports(win32_dll_t *dll,uint32_t mask) {
    if(!dll)return;
    dll->pinned[0]=(mask&1u)!=0u;
    dll->pinned[1]=(mask&2u)!=0u;
}
static uint32_t load_name(win32_dll_t *dll,const char *name) {
    int id=module_by_name(name);
    if(id<0)return fail(dll,WIN32_DLL_ERROR_MOD_NOT_FOUND);
    if(dll->refs[id]==65535u)return fail(dll,WIN32_DLL_ERROR_BAD_ARGUMENT);
    dll->refs[id]++;
    dll->last_error=0;
    return id==0?WIN32_DLL_KERNEL32_HANDLE:WIN32_DLL_MSVCRT_HANDLE;
}
uint32_t win32_dll_load(win32_dll_t *dll,uint32_t guest_name) {
    char name[65];
    if(!dll||!read_name(dll,guest_name,name))
        return fail(dll,WIN32_DLL_ERROR_BAD_ARGUMENT);
    return load_name(dll,name);
}
uint32_t win32_dll_load_w(win32_dll_t *dll,uint32_t guest_name) {
    char name[65];
    if(!dll||!read_wide_name(dll,guest_name,name))
        return fail(dll,WIN32_DLL_ERROR_BAD_ARGUMENT);
    return load_name(dll,name);
}
uint32_t win32_dll_get_module(win32_dll_t *dll,uint32_t guest_name,bool wide) {
    if(!dll)return 0;
    if(guest_name==0u) {
        dll->last_error=0;
        return WIN32_PE_USER_BASE;
    }
    char name[65];
    if(!(wide?read_wide_name(dll,guest_name,name):
               read_name(dll,guest_name,name)))
        return fail(dll,WIN32_DLL_ERROR_BAD_ARGUMENT);
    int id=module_by_name(name);
    if(id<0 || (!dll->pinned[id] && dll->refs[id]==0u))
        return fail(dll,WIN32_DLL_ERROR_MOD_NOT_FOUND);
    dll->last_error=0;
    return id==0?WIN32_DLL_KERNEL32_HANDLE:WIN32_DLL_MSVCRT_HANDLE;
}
uint32_t win32_dll_symbol(win32_dll_t *dll,uint32_t module,
                          uint32_t guest_symbol) {
    if(!dll)return 0;
    int id=module_id(module);
    if(id<0||(!dll->refs[id]&&!dll->pinned[id]))
        return fail(dll,WIN32_DLL_ERROR_INVALID_HANDLE);
    /* The Win32 low-word ordinal form is not implemented. */
    if(guest_symbol<=65535u)
        return fail(dll,WIN32_DLL_ERROR_PROC_NOT_FOUND);
    char name[65];
    if(!read_name(dll,guest_symbol,name))
        return fail(dll,WIN32_DLL_ERROR_BAD_ARGUMENT);
    const export_t *list=id==0?kernel32_exports:msvcrt_exports;
    size_t count=id==0?sizeof(kernel32_exports)/sizeof(export_t):
                       sizeof(msvcrt_exports)/sizeof(export_t);
    for(size_t i=0;i<count;++i) {
        if(equals_exact(name,list[i].name)) {
            dll->last_error=0;
            return WIN32_PE_USER_BASE+WIN32_PE_RUNTIME_OFFSET+
                   (uint32_t)list[i].slot*WIN32_PE_BRIDGE_SLOT;
        }
    }
    return fail(dll,WIN32_DLL_ERROR_PROC_NOT_FOUND);
}
uint32_t win32_dll_free(win32_dll_t *dll,uint32_t module) {
    if(!dll)return 0;
    int id=module_id(module);
    if(id<0||!dll->refs[id])
        return fail(dll,WIN32_DLL_ERROR_INVALID_HANDLE);
    dll->refs[id]--;
    dll->last_error=0;
    return 1u;
}
