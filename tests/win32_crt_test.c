#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "win32_crt.h"

#define BASE 0x40000000u
#define BYTES (2u*1024u*1024u)
static uint8_t memory[BYTES];
static win32_crt_t rt;
static uint32_t call(uint32_t n,uint32_t a,uint32_t b,uint32_t c) {
    return win32_crt_call(&rt,n,a,b,c);
}

int main(void) {
    win32_crt_init(&rt,memory,BASE,BYTES);
    strcpy((char *)(memory+0x2000),"Hello, Bordify!");
    strcpy((char *)(memory+0x2100),"Hello, Bordify!");
    assert(call(10,BASE+0x2000,0,0)==15u);
    assert(call(14,BASE+0x2000,BASE+0x2100,0)==0);
    memory[0x2100]='J';
    assert((int32_t)call(14,BASE+0x2000,BASE+0x2100,0)<0);
    assert((int32_t)call(15,BASE+0x2000,BASE+0x2100,0)==0);
    assert((int32_t)call(15,BASE+0x2000,BASE+0x2100,1)<0);
    assert(call(12,BASE+0x2200,(uint32_t)'z',32)==BASE+0x2200);
    for(int i=0;i<32;++i) assert(memory[0x2200+i]=='z');
    assert(call(11,BASE+0x2300,BASE+0x2200,32)==BASE+0x2300);
    assert(call(13,BASE+0x2200,BASE+0x2300,32)==0);
    memory[0x2301]='a';
    assert((int32_t)call(13,BASE+0x2200,BASE+0x2300,32)>0);
    strcpy((char *)(memory+0x2400),"abcdef");
    assert(call(19,BASE+0x2402,BASE+0x2400,5)==BASE+0x2402);
    assert(!memcmp(memory+0x2400,"ababcde",7));
    assert(call(19,BASE+0x2400,BASE+0x2402,5)==BASE+0x2400);
    assert(!memcmp(memory+0x2400,"abcde",5));

    uint32_t p=call(16,47,0,0);
    assert(p>=BASE+WIN32_CRT_HEAP_START);
    assert((p&15u)==0);
    assert(call(12,p,'X',47)==p);
    uint32_t enlarged=call(20,p,256,0);
    assert(enlarged && enlarged!=p);
    for(int i=0;i<47;++i)assert(memory[enlarged-BASE+i]=='X');
    assert(call(17,p,0,0)==UINT32_MAX); /* old allocation was freed */
    assert(call(17,enlarged,0,0)==0);
    assert(call(17,enlarged,0,0)==UINT32_MAX); /* double-free */
    assert(call(17,0,0,0)==0);
    uint32_t q=call(18,12,8,0);
    assert(q>=BASE+WIN32_CRT_HEAP_START);
    for(int i=0;i<96;++i)assert(memory[q-BASE+i]==0);
    assert(call(18,UINT32_MAX,16,0)==0); /* overflow */
    assert(call(17,q,0,0)==0);
    uint32_t again=call(16,64,0,0);
    assert(again==q); /* use released heap block */
    assert(call(17,again,0,0)==0);
    assert(call(20,0,24,0)!=0); /* realloc(NULL,n) */
    assert(call(16,WIN32_CRT_HEAP_END-WIN32_CRT_HEAP_START+1u,0,0)==0);

    /* Every memory operation validates the entire requested range. */
    assert(call(10,BASE-1u,0,0)==UINT32_MAX);
    assert(call(10,BASE+BYTES-1u,0,0)==UINT32_MAX);
    assert(call(11,BASE+BYTES-4u,BASE+0x2200,8)==UINT32_MAX);
    assert(call(12,BASE+BYTES-2u,1,8)==UINT32_MAX);
    assert(call(13,BASE+BYTES-2u,BASE+0x2200,8)==UINT32_MAX);
    assert(call(14,BASE+BYTES-1u,BASE+0x2200,0)==UINT32_MAX);
    assert(call(20,BASE+0x1200,512,0)==UINT32_MAX);
    assert(rt.last_error==87u);
    assert(call(99,0,0,0)==UINT32_MAX);
    assert(!memcmp(memory+0x2300,"za",2));

    /* Distinct guest process launches reset all heap bookkeeping. */
    win32_crt_init(&rt,memory,BASE,BYTES);
    assert(call(16,16,0,0)==BASE+WIN32_CRT_HEAP_START);
    puts("Win32 MSVCRT multi-DLL guest memory/heap tests: PASS");
    return 0;
}
