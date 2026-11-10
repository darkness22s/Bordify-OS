#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "win32_dll.h"
#include "win32_pe.h"
#define BASE 0x40000000u
#define BYTES (4u*1024u*1024u)
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
    win32_dll_pin_imports(&dll,3u);
    assert(win32_dll_get_module(&dll,0u,false)==BASE);
    assert(win32_dll_get_module(&dll,BASE+0x2000,false)==k);
    assert(win32_dll_get_module(&dll,BASE+0x2200,false)==WIN32_DLL_MSVCRT_HANDLE);
    assert(dll.refs[0]==0u && dll.refs[1]==0u);
    assert(win32_dll_symbol(&dll,k,BASE+0x2100)==tick);
    assert(!win32_dll_free(&dll,k));
    assert(dll.last_error==WIN32_DLL_ERROR_INVALID_HANDLE);
    assert(win32_dll_symbol(&dll,k,BASE+0x2100)==tick);
    const char *wide="KERNEL32.DLL";
    for(size_t i=0;i<=strlen(wide);++i) {
        guest[0x2600u+i*2u]=(uint8_t)wide[i];
        guest[0x2601u+i*2u]=0u;
    }
    assert(win32_dll_get_module(&dll,BASE+0x2600u,true)==k);
    assert(win32_dll_load_w(&dll,BASE+0x2600u)==k);
    assert(dll.refs[0]==1u && win32_dll_free(&dll,k));
    assert(dll.pinned[0] && dll.refs[0]==0u);
    assert(win32_dll_get_module(&dll,BASE+0x2400,false)==0u);
    assert(dll.last_error==WIN32_DLL_ERROR_MOD_NOT_FOUND);
    guest[0x2601u]=1u;
    assert(!win32_dll_load_w(&dll,BASE+0x2600u));
    assert(dll.last_error==WIN32_DLL_ERROR_BAD_ARGUMENT);
    guest[0x2601u]=0u;
    guest[BYTES-2u]='K';guest[BYTES-1u]=0u;
    assert(!win32_dll_load_w(&dll,BASE+BYTES-2u));
    win32_dll_pin_imports(&dll,0u);
    assert(!win32_dll_get_module(&dll,BASE+0x2000,false));
    assert(dll.last_error==WIN32_DLL_ERROR_MOD_NOT_FOUND);
    /* Dependency-safe process attach order and cyclic graph rejection. */
    win32_dll_init(&dll,guest,BASE,BYTES);
    dll.images[0].state=2u;dll.images[0].handle=BASE+0x190000u;
    dll.images[0].info.guest_base=dll.images[0].handle;
    dll.images[0].info.image_size=0x2000u;
    dll.images[0].info.entry_rva=0x1000u;
    dll.images[0].image_deps=2u;
    dll.images[1].state=2u;dll.images[1].handle=BASE+0x192000u;
    dll.images[1].info.guest_base=dll.images[1].handle;
    dll.images[1].info.image_size=0x2000u;
    dll.images[1].info.entry_rva=0x1200u;
    uint32_t modules[WIN32_DLL_MAX_IMAGES],entries[WIN32_DLL_MAX_IMAGES];
    uint32_t number=0u;
    assert(win32_dll_attach_plan(&dll,modules,entries,&number));
    assert(number==2u && modules[0]==dll.images[1].handle &&
           modules[1]==dll.images[0].handle);
    assert(entries[0]==dll.images[1].handle+0x1200u);
    /* An already attached dependency must not receive PROCESS_ATTACH
     * twice when a later module is attached. */
    dll.images[1].initialized=true;
    assert(win32_dll_attach_plan(&dll,modules,entries,&number));
    assert(number==1u && modules[0]==dll.images[0].handle &&
           entries[0]==dll.images[0].handle+0x1000u);
    dll.images[0].initialized=true;
    /* An attached parent with an unattached dependency is invalid. */
    dll.images[1].initialized=false;
    modules[0]=0x12345678u;entries[0]=0x87654321u;
    assert(!win32_dll_attach_plan(&dll,modules,entries,&number));
    assert(number==0u && modules[0]==0x12345678u &&
           entries[0]==0x87654321u);
    dll.images[1].initialized=true;
    assert(win32_dll_attach_plan(&dll,modules,entries,&number));
    assert(number==0u);
    /* A previously attached image must still have a valid entry RVA.
     * Rejection must not publish any partial attach callbacks. */
    dll.images[0].info.entry_rva=dll.images[0].info.image_size;
    modules[0]=0x12345678u;entries[0]=0x87654321u;
    assert(!win32_dll_attach_plan(&dll,modules,entries,&number));
    assert(number==0u && modules[0]==0x12345678u &&
           entries[0]==0x87654321u);
    dll.images[0].info.entry_rva=0x1000u;
    dll.images[0].initialized=false;
    dll.images[1].initialized=false;
    dll.images[1].image_deps=1u;
    modules[0]=0x12345678u;entries[0]=0x87654321u;
    assert(!win32_dll_attach_plan(&dll,modules,entries,&number));
    assert(number==0u && modules[0]==0x12345678u && entries[0]==0x87654321u);
    dll.images[1].image_deps=0u;
    /* A callback inside an image that extends beyond guest RAM must
     * never be handed to the ring-3 executor. */
    dll.images[0].handle=BASE+BYTES-0x1000u;
    dll.images[0].info.guest_base=dll.images[0].handle;
    assert(!win32_dll_attach_plan(&dll,modules,entries,&number));
    assert(number==0u && modules[0]==0x12345678u &&
           entries[0]==0x87654321u);
    /* Two live DLLs cannot own overlapping mapped image ranges,
     * including a dependency that has no DllMain entrypoint. */
    dll.images[0].handle=BASE+0x190000u;
    dll.images[0].info.guest_base=dll.images[0].handle;
    dll.images[1].handle=BASE+0x191000u;
    dll.images[1].info.guest_base=dll.images[1].handle;
    dll.images[1].info.entry_rva=0u;
    assert(!win32_dll_attach_plan(&dll,modules,entries,&number));
    assert(number==0u && modules[0]==0x12345678u &&
           entries[0]==0x87654321u);
    dll.images[1].handle=BASE+0x192000u;
    dll.images[1].info.guest_base=dll.images[1].handle;
    dll.images[1].info.entry_rva=0x1200u;
    assert(win32_dll_attach_plan(&dll,modules,entries,&number));
    assert(number==2u);
    /* The successful adjacent-image plan overwrote the output slots;
     * re-arm sentinels before checking the next rejection path. */
    modules[0]=0x12345678u;entries[0]=0x87654321u;
    dll.images[0].handle=0xfffff000u;
    dll.images[0].info.guest_base=dll.images[0].handle;
    assert(!win32_dll_attach_plan(&dll,modules,entries,&number));
    assert(number==0u && modules[0]==0x12345678u && entries[0]==0x87654321u);
    /* A forged live handle must not publish an entrypoint from a
     * differently staged image, even if both addresses are in bounds. */
    dll.images[0].handle=BASE+0x190000u;
    dll.images[0].info.guest_base=BASE+0x191000u;
    modules[0]=0x12345678u;entries[0]=0x87654321u;
    assert(!win32_dll_attach_plan(&dll,modules,entries,&number));
    assert(number==0u && modules[0]==0x12345678u &&
           entries[0]==0x87654321u);
    dll.images[0].info.guest_base=dll.images[0].handle;
    assert(win32_dll_attach_plan(&dll,modules,entries,&number));
    assert(number==2u);
    /* GetProcAddress on a synthetic live image with no VM must fail
     * cleanly instead of dereferencing a null VM pointer. */
    win32_dll_init(&dll,guest,BASE,BYTES);
    dll.images[0].state=2u;
    dll.images[0].handle=BASE+0x190000u;
    dll.images[0].info.image_size=0x2000u;
    assert(!win32_dll_symbol(&dll,dll.images[0].handle,BASE+0x2100u));
    assert(dll.last_error==WIN32_DLL_ERROR_INVALID_HANDLE);
    /* A live handle must match the PE image base recorded at staging.
     * Otherwise export resolution could return addresses for a different
     * mapping, even when both ranges lie inside guest RAM. */
    win32_vm_t vm;
    win32_vm_init(&vm,guest,BASE,BYTES);
    win32_dll_set_reader(&dll,&vm,NULL,NULL);
    dll.images[0].info.guest_base=BASE+0x191000u;
    assert(!win32_dll_symbol(&dll,dll.images[0].handle,BASE+0x2100u));
    assert(dll.last_error==WIN32_DLL_ERROR_INVALID_HANDLE);
    win32_dll_init(&dll,guest,BASE,BYTES);
    assert(!dll.allow_dynamic_init && !dll.images[0].initialized);
    dll.allow_dynamic_init=true;
    win32_dll_init(&dll,guest,BASE,BYTES);
    assert(!dll.allow_dynamic_init); /* per-process reset */
    puts("Win32 imported-module handles, attach dependency ordering: PASS");
    return 0;
}
