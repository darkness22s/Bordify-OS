#include "win32_vm.h"

/* A 512-page bitmap replaces the former 32-bit word. Every run is
 * validated before its ownership bits or guest bytes are changed. */
static bool page_bit(const uint32_t *bitmap,uint32_t page) {
    return (bitmap[page>>5u] & (1u<<(page&31u)))!=0u;
}
static void set_page(uint32_t *bitmap,uint32_t page,bool value) {
    uint32_t bit=1u<<(page&31u);
    if(value)bitmap[page>>5u]|=bit;
    else bitmap[page>>5u]&=~bit;
}
static uint32_t failure(win32_vm_t *vm,uint32_t code) {
    if(vm)vm->last_error=code;
    return 0u;
}
void win32_vm_init(win32_vm_t *vm,uint8_t *guest,uint32_t base,size_t bytes) {
    if(!vm)return;
    vm->guest=guest;vm->base=base;vm->bytes=bytes;vm->last_error=0u;
    for(uint32_t i=0;i<WIN32_VM_PAGE_COUNT;++i)vm->runs[i]=0u;
    for(uint32_t i=0;i<WIN32_VM_BITMAP_WORDS;++i) {
        vm->used_pages[i]=0u;
        vm->module_pages[i]=0u;
    }
}
static bool run_used(const win32_vm_t *vm,uint32_t first,uint32_t pages) {
    for(uint32_t i=first;i<first+pages;++i)
        if(page_bit(vm->used_pages,i))return true;
    return false;
}
static void mark_run(uint32_t *bitmap,uint32_t first,uint32_t pages,
                     bool value) {
    for(uint32_t i=first;i<first+pages;++i)set_page(bitmap,i,value);
}
static void zero_run(win32_vm_t *vm,uint32_t first,uint32_t pages) {
    uint32_t offset=WIN32_VM_START+first*WIN32_VM_PAGE_SIZE;
    for(uint32_t i=0;i<pages*WIN32_VM_PAGE_SIZE;++i)
        vm->guest[offset+i]=0u;
}
uint32_t win32_vm_alloc(win32_vm_t *vm,uint32_t addr,uint32_t size,
                        uint32_t flags,uint32_t protect) {
    if(!vm || !vm->guest || vm->bytes<WIN32_VM_END ||
       addr!=0u || !size ||
       flags!=(WIN32_VM_MEM_COMMIT|WIN32_VM_MEM_RESERVE) ||
       protect!=WIN32_VM_PAGE_READWRITE)
        return failure(vm,WIN32_VM_ERROR_INVALID_PARAMETER);
    if(size>WIN32_VM_END-WIN32_VM_START)
        return failure(vm,WIN32_VM_ERROR_NOT_ENOUGH_MEMORY);
    uint32_t pages=(size+WIN32_VM_PAGE_SIZE-1u)/WIN32_VM_PAGE_SIZE;
    for(uint32_t first=0;first+pages<=WIN32_VM_PAGE_COUNT;++first) {
        if(run_used(vm,first,pages))continue;
        mark_run(vm->used_pages,first,pages,true);
        vm->runs[first]=(uint16_t)pages;
        zero_run(vm,first,pages);
        vm->last_error=0u;
        return vm->base+WIN32_VM_START+first*WIN32_VM_PAGE_SIZE;
    }
    return failure(vm,WIN32_VM_ERROR_NOT_ENOUGH_MEMORY);
}
static bool valid_run(win32_vm_t *vm,uint32_t addr,uint32_t size,
                      uint32_t flags,uint32_t *first,uint32_t *pages) {
    if(!vm || !vm->guest || vm->bytes<WIN32_VM_END ||
       size!=0u || flags!=WIN32_VM_MEM_RELEASE ||
       addr<vm->base+WIN32_VM_START ||
       addr>=vm->base+WIN32_VM_END ||
       (addr-vm->base-WIN32_VM_START)%WIN32_VM_PAGE_SIZE)
        return false;
    *first=(addr-vm->base-WIN32_VM_START)/WIN32_VM_PAGE_SIZE;
    *pages=vm->runs[*first];
    if(!*pages || *first+*pages>WIN32_VM_PAGE_COUNT)return false;
    for(uint32_t i=*first;i<*first+*pages;++i)
        if(!page_bit(vm->used_pages,i))return false;
    return true;
}
uint32_t win32_vm_free(win32_vm_t *vm,uint32_t addr,uint32_t size,
                       uint32_t flags) {
    uint32_t first=0u,pages=0u;
    if(!valid_run(vm,addr,size,flags,&first,&pages))
        return failure(vm,WIN32_VM_ERROR_INVALID_PARAMETER);
    for(uint32_t i=first;i<first+pages;++i)
        if(page_bit(vm->module_pages,i))
            return failure(vm,WIN32_VM_ERROR_INVALID_PARAMETER);
    vm->runs[first]=0u;
    mark_run(vm->used_pages,first,pages,false);
    zero_run(vm,first,pages);
    vm->last_error=0u;
    return 1u;
}
uint32_t win32_vm_alloc_module(win32_vm_t *vm,uint32_t size) {
    uint32_t addr=win32_vm_alloc(vm,0u,size,
                      WIN32_VM_MEM_COMMIT|WIN32_VM_MEM_RESERVE,
                      WIN32_VM_PAGE_READWRITE);
    if(!addr)return 0u;
    uint32_t first=(addr-vm->base-WIN32_VM_START)/WIN32_VM_PAGE_SIZE;
    mark_run(vm->module_pages,first,vm->runs[first],true);
    return addr;
}
uint32_t win32_vm_free_module(win32_vm_t *vm,uint32_t addr) {
    uint32_t first=0u,pages=0u;
    if(!valid_run(vm,addr,0u,WIN32_VM_MEM_RELEASE,&first,&pages))
        return failure(vm,WIN32_VM_ERROR_INVALID_PARAMETER);
    for(uint32_t i=first;i<first+pages;++i)
        if(!page_bit(vm->module_pages,i))
            return failure(vm,WIN32_VM_ERROR_INVALID_PARAMETER);
    mark_run(vm->module_pages,first,pages,false);
    uint32_t result=win32_vm_free(vm,addr,0u,WIN32_VM_MEM_RELEASE);
    if(!result)mark_run(vm->module_pages,first,pages,true);
    return result;
}
