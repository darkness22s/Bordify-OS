#include "win32_vm.h"

/* The arena is already mapped as ring-3 pages by process_init.
 * A free always zeroes the entire released run to prevent stale data leaks
 * into the next allocation in the same process. */
void win32_vm_init(win32_vm_t *vm,uint8_t *guest,uint32_t base,size_t bytes) {
    if (!vm) return;
    vm->guest=guest;vm->base=base;vm->bytes=bytes;
    vm->used_pages=0;vm->last_error=0;
    for(unsigned i=0;i<WIN32_VM_PAGE_COUNT;++i)vm->runs[i]=0;
}
static uint32_t failure(win32_vm_t *vm,uint32_t code) {
    if(vm) vm->last_error=code;
    return 0;
}
uint32_t win32_vm_alloc(win32_vm_t *vm,uint32_t addr,uint32_t size,
                        uint32_t flags,uint32_t protect) {
    if(!vm || !vm->guest || vm->bytes<WIN32_VM_END ||
       addr!=0u || size==0u ||
       flags!=(WIN32_VM_MEM_COMMIT|WIN32_VM_MEM_RESERVE) ||
       protect!=WIN32_VM_PAGE_READWRITE)
        return failure(vm,WIN32_VM_ERROR_INVALID_PARAMETER);
    if(size>(WIN32_VM_END-WIN32_VM_START))
        return failure(vm,WIN32_VM_ERROR_NOT_ENOUGH_MEMORY);
    uint32_t pages=(size+WIN32_VM_PAGE_SIZE-1u)/WIN32_VM_PAGE_SIZE;
    for(uint32_t first=0;first+pages<=WIN32_VM_PAGE_COUNT;++first) {
        /* Full-arena requests avoid shifting uint32_t by 32. */
        uint32_t mask=pages==32u ? 0xffffffffu :
                      ((1u<<pages)-1u)<<first;
        if((vm->used_pages&mask)!=0u)continue;
        vm->used_pages|=mask;
        vm->runs[first]=(uint8_t)pages;
        uint32_t off=WIN32_VM_START+first*WIN32_VM_PAGE_SIZE;
        for(uint32_t i=0;i<pages*WIN32_VM_PAGE_SIZE;++i)
            vm->guest[off+i]=0;
        vm->last_error=0;
        return vm->base+off;
    }
    return failure(vm,WIN32_VM_ERROR_NOT_ENOUGH_MEMORY);
}
uint32_t win32_vm_free(win32_vm_t *vm,uint32_t addr,uint32_t size,
                       uint32_t flags) {
    if(!vm || !vm->guest || vm->bytes<WIN32_VM_END ||
       size!=0u || flags!=WIN32_VM_MEM_RELEASE ||
       addr<vm->base+WIN32_VM_START ||
       addr>=vm->base+WIN32_VM_END ||
       (addr-vm->base-WIN32_VM_START)%WIN32_VM_PAGE_SIZE)
        return failure(vm,WIN32_VM_ERROR_INVALID_PARAMETER);
    uint32_t first=(addr-vm->base-WIN32_VM_START)/WIN32_VM_PAGE_SIZE;
    uint32_t pages=vm->runs[first];
    if(!pages || first+pages>WIN32_VM_PAGE_COUNT)
        return failure(vm,WIN32_VM_ERROR_INVALID_PARAMETER);
    uint32_t mask=pages==32u ? 0xffffffffu :
                  ((1u<<pages)-1u)<<first;
    vm->runs[first]=0;
    vm->used_pages&=~mask;
    uint32_t off=WIN32_VM_START+first*WIN32_VM_PAGE_SIZE;
    for(uint32_t i=0;i<pages*WIN32_VM_PAGE_SIZE;++i)
        vm->guest[off+i]=0;
    vm->last_error=0;
    return 1u;
}
