#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "win32_vm.h"
#define BASE 0x40000000u
#define BYTES (2u*1024u*1024u)
static uint8_t guest[BYTES];
int main(void) {
    win32_vm_t vm;
    win32_vm_init(&vm,guest,BASE,BYTES);
    uint32_t a=win32_vm_alloc(&vm,0,1u,0x3000u,0x04u);
    assert(a==BASE+WIN32_VM_START && vm.used_pages==1u);
    guest[a-BASE]=0xa5;
    uint32_t b=win32_vm_alloc(&vm,0,8193u,0x3000u,0x04u);
    assert(b==a+4096u && vm.used_pages==0x0fu);
    assert(!win32_vm_free(&vm,b+4096u,0,0x8000u));
    assert(vm.last_error==87u);
    assert(!win32_vm_alloc(&vm,0,4096,0x3000u,0x40u));
    assert(vm.last_error==87u); /* reject executable allocation */
    assert(!win32_vm_alloc(&vm,0,4096,0x1000u,0x04u));
    assert(!win32_vm_alloc(&vm,0,4096,0x3000u,0x02u));
    assert(!win32_vm_alloc(&vm,BASE+0x2000,4096,0x3000u,0x04u));
    assert(!win32_vm_alloc(&vm,0,UINT32_MAX,0x3000u,0x04u));
    assert(vm.last_error==WIN32_VM_ERROR_NOT_ENOUGH_MEMORY);
    assert(!win32_vm_free(&vm,a,1u,0x8000u));
    assert(vm.last_error==87u);
    assert(win32_vm_free(&vm,a,0,0x8000u));
    assert(guest[a-BASE]==0); /* freed memory is scrubbed */
    assert(!win32_vm_free(&vm,a,0,0x8000u)); /* double-free */
    assert(win32_vm_alloc(&vm,0,4096,0x3000u,0x04u)==a);
    assert(win32_vm_free(&vm,a,0,0x8000u));
    assert(win32_vm_free(&vm,b,0,0x8000u));
    uint32_t full=win32_vm_alloc(&vm,0,WIN32_VM_END-WIN32_VM_START,
                                 0x3000u,0x04u);
    assert(full==a && vm.used_pages==0xffffffffu);
    assert(!win32_vm_alloc(&vm,0,4096,0x3000u,0x04u));
    assert(vm.last_error==WIN32_VM_ERROR_NOT_ENOUGH_MEMORY);
    assert(win32_vm_free(&vm,full,0,0x8000u));
    win32_vm_init(&vm,guest,BASE,BYTES);
    assert(vm.used_pages==0u); /* resets across processes */
    assert(win32_vm_alloc(&vm,0,4096,0x3000u,0x04u)==a);
    puts("Win32 VirtualAlloc/VirtualFree isolated guest page arena: PASS");
    return 0;
}
