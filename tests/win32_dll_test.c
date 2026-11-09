#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "win32_dll.h"
#include "win32_pe.h"
#define BASE 0x40000000u
#define BYTES (2u*1024u*1024u)
static uint8_t guest[BYTES];
int main(void) {
    win32_dll_t dll;
    win32_dll_init(&dll,guest,BASE,BYTES);
    strcpy((char *)(guest+0x2000),"KERNEL32.dll");
    strcpy((char *)(guest+0x2100),"GetTickCount");
    strcpy((char *)(guest+0x2200),"msvcrt.dll");
    strcpy((char *)(guest+0x2300),"strlen");
    strcpy((char *)(guest+0x2400),"chromium.dll");
    strcpy((char *)(guest+0x2500),"SomeUnimplementedApi");
    uint32_t k=win32_dll_load(&dll,BASE+0x2000);
    assert(k==WIN32_DLL_KERNEL32_HANDLE && dll.refs[0]==1u);
    uint32_t tick=win32_dll_symbol(&dll,k,BASE+0x2100);
    assert(tick==WIN32_PE_USER_BASE+WIN32_PE_RUNTIME_OFFSET+
                 9u*WIN32_PE_BRIDGE_SLOT);
    assert(!win32_dll_symbol(&dll,k,BASE+0x2500));
    assert(dll.last_error==WIN32_DLL_ERROR_PROC_NOT_FOUND);
    assert(!win32_dll_symbol(&dll,k,1u)); /* ordinal pointers rejected */
    assert(dll.last_error==WIN32_DLL_ERROR_PROC_NOT_FOUND);
    assert(win32_dll_load(&dll,BASE+0x2400)==0u);
    assert(dll.last_error==WIN32_DLL_ERROR_MOD_NOT_FOUND);
    assert(win32_dll_load(&dll,BASE+0x2200)==WIN32_DLL_MSVCRT_HANDLE);
    uint32_t crt=win32_dll_symbol(&dll,WIN32_DLL_MSVCRT_HANDLE,BASE+0x2300);
    assert(crt==WIN32_PE_USER_BASE+WIN32_PE_RUNTIME_OFFSET+
                16u*WIN32_PE_BRIDGE_SLOT);
    assert(win32_dll_free(&dll,k)==1u);
    assert(!win32_dll_symbol(&dll,k,BASE+0x2100));
    assert(dll.last_error==WIN32_DLL_ERROR_INVALID_HANDLE);
    assert(!win32_dll_free(&dll,k));
    assert(win32_dll_free(&dll,WIN32_DLL_MSVCRT_HANDLE)==1u);
    assert(!win32_dll_load(&dll,BASE-1u));
    assert(dll.last_error==WIN32_DLL_ERROR_BAD_ARGUMENT);
    /* A string cut by the end of guest RAM is not a valid module name. */
    guest[BYTES-1u]='A';
    assert(!win32_dll_load(&dll,BASE+BYTES-1u));
    win32_dll_init(&dll,guest,BASE,BYTES);
    assert(!win32_dll_symbol(&dll,k,BASE+0x2100)); /* process reset */
    puts("Win32 built-in DLL loading and GetProcAddress: PASS");
    return 0;
}
