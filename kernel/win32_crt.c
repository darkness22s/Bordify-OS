#include "win32_crt.h"

/* The Win32 PE importer links these entry points as msvcrt.dll symbols.
 * Callers run in user mode; the ring-0 syscall validates every parameter.
 * Invalid pointers fail closed with UINT32_MAX, NULL for allocation.
 */
static bool inside(const win32_crt_t *rt,uint32_t ptr,uint32_t length) {
    if (!rt || !rt->memory || ptr < rt->base) return false;
    uint32_t off=ptr-rt->base;
    return (size_t)off<=rt->extent &&
           (size_t)length<=rt->extent-(size_t)off;
}
static uint8_t *bytes(win32_crt_t *rt,uint32_t ptr) {
    return rt->memory+(ptr-rt->base);
}
static uint32_t fail(win32_crt_t *rt) {
    if(rt) rt->last_error=87u;
    return UINT32_MAX;
}
static bool cstring_length(win32_crt_t *rt,uint32_t ptr,uint32_t *out) {
    if (!inside(rt,ptr,1)) return false;
    size_t available=rt->extent-(size_t)(ptr-rt->base);
    if(available>WIN32_CRT_MAX_STRING) available=WIN32_CRT_MAX_STRING;
    const uint8_t *p=bytes(rt,ptr);
    for(size_t i=0;i<available;++i) {
        if(p[i]==0) { *out=(uint32_t)i;return true; }
    }
    return false;
}
static win32_crt_allocation_t *find_allocation(win32_crt_t *rt,
                                                uint32_t ptr) {
    if (!rt) return NULLPTR;
    for(size_t i=0;i<WIN32_CRT_MAX_ALLOCS;++i)
        if(rt->allocations[i].live &&
           rt->base+rt->allocations[i].offset==ptr)
            return &rt->allocations[i];
    return NULLPTR;
}
static uint32_t allocate(win32_crt_t *rt,uint32_t count,bool zero) {
    if (!rt || !rt->memory || count>WIN32_CRT_HEAP_END-WIN32_CRT_HEAP_START)
        return 0;
    uint32_t desired=count?count:1u;
    if(desired>UINT32_MAX-15u) return 0;
    uint32_t aligned=(desired+15u)&~15u;
    win32_crt_allocation_t *free_slot=NULLPTR;
    for(size_t i=0;i<WIN32_CRT_MAX_ALLOCS;++i) {
        win32_crt_allocation_t *record=&rt->allocations[i];
        if(record->live) continue;
        if(record->bytes>=aligned && record->bytes>0) {
            record->live=true;
            if(zero) {
                for(uint32_t j=0;j<aligned;++j)
                    rt->memory[record->offset+j]=0;
            }
            return rt->base+record->offset;
        }
        if(!free_slot) free_slot=record;
    }
    if(!free_slot || rt->next>WIN32_CRT_HEAP_END ||
       aligned>WIN32_CRT_HEAP_END-rt->next ||
       !inside(rt,rt->base+rt->next,aligned))
        return 0;
    free_slot->offset=rt->next;
    free_slot->bytes=aligned;
    free_slot->live=true;
    rt->next+=aligned;
    if(zero) {
        for(uint32_t j=0;j<aligned;++j)
            rt->memory[free_slot->offset+j]=0;
    }
    return rt->base+free_slot->offset;
}
static bool release(win32_crt_t *rt,uint32_t ptr) {
    if(ptr==0) return true; /* ISO C free(NULL) */
    win32_crt_allocation_t *record=find_allocation(rt,ptr);
    if(!record) return false;
    record->live=false;
    /* The topmost allocation can be returned to the bump arena. Clear
     * its reusable record so a later allocation cannot claim overlapping
     * memory after the high-water mark moves backwards. */
    if(record->offset+record->bytes==rt->next) {
        rt->next=record->offset;
        record->bytes=0;
        /* Adjacent previously freed blocks are also part of the free tail.
         * Reclaim them in reverse order without moving live allocations. */
        bool changed=true;
        while(changed) {
            changed=false;
            for(size_t i=0;i<WIN32_CRT_MAX_ALLOCS;++i) {
                win32_crt_allocation_t *candidate=&rt->allocations[i];
                if(!candidate->live && candidate->bytes &&
                   candidate->offset+candidate->bytes==rt->next) {
                    rt->next=candidate->offset;
                    candidate->bytes=0;
                    changed=true;
                    break;
                }
            }
        }
    }
    return true;
}

void win32_crt_init(win32_crt_t *rt,uint8_t *memory,uint32_t base,
                    size_t extent) {
    if(!rt) return;
    rt->memory=memory;
    rt->base=base;
    rt->extent=extent;
    rt->next=WIN32_CRT_HEAP_START;
    rt->last_error=0;
    for(size_t i=0;i<WIN32_CRT_MAX_ALLOCS;++i) {
        rt->allocations[i].offset=0;
        rt->allocations[i].bytes=0;
        rt->allocations[i].live=false;
    }
}

uint32_t win32_crt_call(win32_crt_t *rt,uint32_t service,
                        uint32_t arg1,uint32_t arg2,uint32_t arg3) {
    if(!rt || !rt->memory) return UINT32_MAX;
    uint8_t *dst;
    const uint8_t *src;
    uint32_t len1;

    switch(service) {
      case 10: /* strlen(const char *) */
        if(!cstring_length(rt,arg1,&len1)) return fail(rt);
        return len1;
      case 11: /* memcpy(void *, const void *, size_t) */
      case 19: /* memmove */
        if(!inside(rt,arg1,arg3)||!inside(rt,arg2,arg3))
            return fail(rt);
        dst=bytes(rt,arg1);src=bytes(rt,arg2);
        if(service==19 && arg1>arg2 &&
           (uint64_t)arg1<(uint64_t)arg2+arg3) {
            for(uint32_t i=arg3;i>0;--i) dst[i-1]=src[i-1];
        } else {
            for(uint32_t i=0;i<arg3;++i) dst[i]=src[i];
        }
        return arg1;
      case 12: /* memset(void *, int, size_t) */
        if(!inside(rt,arg1,arg3)) return fail(rt);
        dst=bytes(rt,arg1);
        for(uint32_t i=0;i<arg3;++i) dst[i]=(uint8_t)arg2;
        return arg1;
      case 13: /* memcmp(const void *, const void *, size_t) */
        if(!inside(rt,arg1,arg3)||!inside(rt,arg2,arg3))
            return fail(rt);
        dst=bytes(rt,arg1);src=bytes(rt,arg2);
        for(uint32_t i=0;i<arg3;++i)
            if(dst[i]!=src[i])
                return (uint32_t)((int32_t)dst[i]-(int32_t)src[i]);
        return 0;
      case 14: /* strcmp(const char *, const char *) */
      case 15: { /* strncmp(...,size_t n) */
        if(service==15 && arg3==0u) return 0;
        if(!inside(rt,arg1,1)||!inside(rt,arg2,1)) return fail(rt);
        uint32_t size=(service==15 && arg3<WIN32_CRT_MAX_STRING)?
                         arg3:WIN32_CRT_MAX_STRING;
        size_t off1=arg1-rt->base,off2=arg2-rt->base;
        if(size>rt->extent-off1) size=(uint32_t)(rt->extent-off1);
        if(size>rt->extent-off2) size=(uint32_t)(rt->extent-off2);
        dst=bytes(rt,arg1);src=bytes(rt,arg2);
        for(uint32_t i=0;i<size;++i) {
            if(dst[i]!=src[i])
                return (uint32_t)((int32_t)dst[i]-(int32_t)src[i]);
            if(dst[i]==0) return 0;
        }
        if(service==15 && arg3<=size) return 0;
        return fail(rt); /* no NUL in validated memory */
      }
      case 16: /* malloc(size_t) */
        return allocate(rt,arg1,false);
      case 17: /* free(void *) */
        return release(rt,arg1)?0u:fail(rt);
      case 18: /* calloc(size_t count,size_t bytes) */
        if(arg2!=0 && arg1>UINT32_MAX/arg2) return 0;
        return allocate(rt,arg1*arg2,true);
      case 20: { /* realloc(void *pointer,size_t new_size) */
        if(!arg1) return allocate(rt,arg2,false);
        if(!arg2) return release(rt,arg1)?0u:fail(rt);
        win32_crt_allocation_t *previous=find_allocation(rt,arg1);
        if(!previous) return fail(rt);
        if(arg2<=previous->bytes) {
            /* A shrinking realloc retains the pointer. Reclaim aligned
             * trailing space only for the current topmost allocation:
             * reducing a non-tail record must not overlap later blocks. */
            uint32_t desired=(arg2+15u)&~15u;
            if(previous->offset+previous->bytes==rt->next &&
               desired<previous->bytes) {
                rt->next=previous->offset+desired;
                previous->bytes=desired;
            }
            return arg1;
        }
        /* Grow the topmost heap block in place when its tail is free. */
        if(arg2<=UINT32_MAX-15u) {
            uint32_t aligned=(arg2+15u)&~15u;
            uint32_t end=previous->offset+previous->bytes;
            if(end==rt->next && end<=WIN32_CRT_HEAP_END &&
               aligned<=WIN32_CRT_HEAP_END-previous->offset &&
               inside(rt,arg1,aligned)) {
                previous->bytes=aligned;
                rt->next=previous->offset+aligned;
                return arg1;
            }
        }
        uint32_t length=previous->bytes;
        uint32_t ptr=allocate(rt,arg2,false);
        if(!ptr) return 0;
        dst=bytes(rt,ptr);src=bytes(rt,arg1);
        for(uint32_t i=0;i<length;++i)dst[i]=src[i];
        previous->live=false;
        return ptr;
      }
      case 30: { /* strchr(const char *s, int c): bounded guest pointer */
        if(!inside(rt,arg1,1u)) return fail(rt);
        uint32_t length=0u;
        if(!cstring_length(rt,arg1,&length)) return fail(rt);
        const uint8_t *s=bytes(rt,arg1);
        uint8_t needle=(uint8_t)arg2;
        for(uint32_t i=0;i<=length;++i)
            if(s[i]==needle) return arg1+i;
        return 0u;
      }
      case 31: { /* strrchr: last occurrence, including terminating NUL */
        if(!inside(rt,arg1,1u)) return fail(rt);
        uint32_t length=0u;
        if(!cstring_length(rt,arg1,&length)) return fail(rt);
        const uint8_t *s=bytes(rt,arg1);
        uint8_t needle=(uint8_t)arg2;
        for(uint32_t i=length+1u;i>0u;--i)
            if(s[i-1u]==needle) return arg1+i-1u;
        return 0u;
      }
      default: return fail(rt);
    }
}
