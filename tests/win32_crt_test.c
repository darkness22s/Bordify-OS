#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "win32_crt.h"

#define BASE 0x40000000u
#define BYTES (4u*1024u*1024u)
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
    assert(call(30,BASE+0x2000,'B',0)==BASE+0x2007);
    assert(call(30,BASE+0x2000,'z',0)==0u);
    assert(call(30,BASE+0x2000,0,0)==BASE+0x200f);
    /* strrchr must return the last match, not the first. */
    assert(call(31,BASE+0x2000,'o',0)==BASE+0x2008u);
    assert(call(31,BASE+0x2000,'z',0)==0u);
    assert(call(31,BASE+0x2000,0,0)==BASE+0x200fu);
    /* strchr must compare unsigned char values and return the first match. */
    memory[0x2500]=0x80u;
    memory[0x2501]='a';
    memory[0x2502]=0x80u;
    memory[0x2503]=0;
    assert(call(31,BASE+0x2500,0x180u,0)==BASE+0x2502);
    assert(call(30,BASE+0x2500,0x180u,0)==BASE+0x2500);
    assert(call(30,BASE+0x2500,'a',0)==BASE+0x2501);
    assert(call(30,BASE+0x2500,0,0)==BASE+0x2503);
    /* A NUL in the final mapped byte is valid; an absent NUL is not. */
    memory[BYTES-2u]='Q';
    memory[BYTES-1u]=0;
    assert(call(31,BASE+BYTES-2u,'Q',0)==BASE+BYTES-2u);
    assert(call(31,BASE+BYTES-2u,0,0)==BASE+BYTES-1u);
    assert(call(30,BASE+BYTES-2u,'Q',0)==BASE+BYTES-2u);
    assert(call(30,BASE+BYTES-2u,0,0)==BASE+BYTES-1u);
    assert(call(30,BASE+BYTES-2u,'Z',0)==0u);
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
    assert(enlarged==p); /* topmost block grows without moving */
    for(int i=0;i<47;++i)assert(memory[enlarged-BASE+i]=='X');
    /* Shrinking a tail block releases aligned space for the next malloc. */
    uint32_t tail_end=rt.next;
    assert(call(20,p,48,0)==p);
    assert(rt.next==tail_end-208u);
    for(int i=0;i<47;++i)assert(memory[p-BASE+i]=='X');
    uint32_t reclaimed=call(16,208,0,0);
    assert(reclaimed==p+48u);
    assert(call(17,reclaimed,0,0)==0);
    uint32_t blocker=call(16,32,0,0);
    assert(blocker!=0 && blocker!=p);
    uint32_t moved=call(20,p,512,0);
    assert(moved!=0 && moved!=p); /* non-tail block must move */
    for(int i=0;i<47;++i)assert(memory[moved-BASE+i]=='X');
    assert(call(17,p,0,0)==UINT32_MAX); /* old allocation was freed */
    assert(call(17,blocker,0,0)==0);
    assert(call(17,moved,0,0)==0);
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
    /* A failed realloc must preserve both the old allocation and its bytes. */
    uint32_t retained=call(16,32,0,0);
    assert(retained!=0);
    assert(call(12,retained,'R',32)==retained);
    assert(call(20,retained,WIN32_CRT_HEAP_END-WIN32_CRT_HEAP_START+1u,0)==0);
    for(int i=0;i<32;++i) assert(memory[retained-BASE+i]=='R');
    assert(call(17,retained,0,0)==0);
    /* calloc must clear a recycled allocation, not expose prior contents. */
    uint32_t recycled=call(16,32,0,0);
    assert(recycled==retained);
    assert(call(12,recycled,'S',32)==recycled);
    assert(call(17,recycled,0,0)==0);
    uint32_t cleared=call(18,4,8,0);
    assert(cleared==recycled);
    for(int i=0;i<32;++i) assert(memory[cleared-BASE+i]==0);
    assert(call(17,cleared,0,0)==0);
    assert(call(20,0,24,0)!=0); /* realloc(NULL,n) */
    assert(call(16,WIN32_CRT_HEAP_END-WIN32_CRT_HEAP_START+1u,0,0)==0);

    /* Every memory operation validates the entire requested range. */
    assert(call(31,BASE-1u,'x',0)==UINT32_MAX);
    assert(call(30,BASE-1u,'x',0)==UINT32_MAX);
    assert(call(10,BASE-1u,0,0)==UINT32_MAX);
    memory[BYTES-1u]='X'; /* explicitly unterminated at guest boundary */
    assert(call(31,BASE+BYTES-1u,'X',0)==UINT32_MAX);
    assert(call(30,BASE+BYTES-1u,'X',0)==UINT32_MAX);
    assert(call(10,BASE+BYTES-1u,0,0)==UINT32_MAX);
    assert(call(11,BASE+BYTES-4u,BASE+0x2200,8)==UINT32_MAX);
    assert(call(12,BASE+BYTES-2u,1,8)==UINT32_MAX);
    assert(call(13,BASE+BYTES-2u,BASE+0x2200,8)==UINT32_MAX);
    memory[0x2200]='X'; /* force equality before crossing the first buffer bound */
    assert(call(14,BASE+BYTES-1u,BASE+0x2200,0)==UINT32_MAX);
    memory[0x2200]='z';
    assert(call(20,BASE+0x1200,512,0)==UINT32_MAX);
    assert(rt.last_error==87u);
    assert(call(99,0,0,0)==UINT32_MAX);
    assert(!memcmp(memory+0x2300,"za",2));

    /* Freeing the heap tail must reclaim space without leaving a stale
     * free-list record that overlaps subsequent bump allocations. */
    win32_crt_init(&rt,memory,BASE,BYTES);
    uint32_t first=call(16,32,0,0);
    uint32_t second=call(16,64,0,0);
    assert(first==BASE+WIN32_CRT_HEAP_START);
    assert(second==first+32u);
    assert(call(17,second,0,0)==0);
    assert(rt.next==WIN32_CRT_HEAP_START+32u);
    uint32_t replacement=call(16,16,0,0);
    uint32_t neighbor=call(16,48,0,0);
    assert(replacement==second);
    assert(neighbor==replacement+16u);
    assert(call(12,replacement,'A',16)==replacement);
    assert(call(12,neighbor,'B',48)==neighbor);
    assert(call(17,first,0,0)==0);
    uint32_t reused=call(16,32,0,0);
    assert(reused==first);
    assert(call(12,reused,'C',32)==reused);
    for(int i=0;i<16;++i) assert(memory[replacement-BASE+i]=='A');
    for(int i=0;i<48;++i) assert(memory[neighbor-BASE+i]=='B');
    assert(call(17,replacement,0,0)==0);
    assert(call(17,neighbor,0,0)==0);
    assert(call(17,reused,0,0)==0);

    /* Freeing B before C must coalesce both blocks when C is freed.
     * The next bump allocation must not overlap a stale free record. */
    win32_crt_init(&rt,memory,BASE,BYTES);
    uint32_t a=call(16,32,0,0);
    uint32_t b=call(16,48,0,0);
    uint32_t c=call(16,64,0,0);
    assert(a && b==a+32u && c==b+48u);
    assert(call(17,b,0,0)==0);
    assert(call(17,c,0,0)==0);
    assert(rt.next==WIN32_CRT_HEAP_START+32u);
    uint32_t combined=call(16,112,0,0);
    assert(combined==b);
    assert(call(12,combined,'D',112)==combined);
    assert(call(17,a,0,0)==0);
    uint32_t earlier=call(16,32,0,0);
    assert(earlier==a);
    assert(call(12,earlier,'E',32)==earlier);
    for(int i=0;i<112;++i) assert(memory[combined-BASE+i]=='D');
    assert(call(17,combined,0,0)==0);
    assert(call(17,earlier,0,0)==0);

    /* realloc into a recycled earlier block must release its old tail.
     * Otherwise the arena high-water mark grows despite no live tail user. */
    win32_crt_init(&rt,memory,BASE,BYTES);
    uint32_t hole=call(16,128,0,0);
    uint32_t old_tail=call(16,32,0,0);
    /* A live guard prevents the valid in-place tail-growth optimization,
     * forcing realloc to use the earlier freed block instead. */
    uint32_t guard=call(16,16,0,0);
    assert(hole && old_tail==hole+128u && guard==old_tail+32u);
    assert(call(12,old_tail,'T',32)==old_tail);
    assert(call(17,hole,0,0)==0);
    uint32_t relocated=call(20,old_tail,64,0);
    assert(relocated==hole);
    /* Releasing the guard reclaims the old allocation AND the split
     * free remainder immediately below it, down to the live 64-byte
     * relocated allocation. */
    assert(call(17,guard,0,0)==0);
    assert(rt.next==WIN32_CRT_HEAP_START+64u);
    for(int i=0;i<32;++i) assert(memory[relocated-BASE+i]=='T');
    assert(call(17,old_tail,0,0)==UINT32_MAX);
    uint32_t following=call(16,32,0,0);
    assert(following==relocated+64u);
    assert(call(12,following,'F',32)==following);
    for(int i=0;i<32;++i) assert(memory[relocated-BASE+i]=='T');
    assert(call(17,following,0,0)==0);
    assert(call(17,relocated,0,0)==0);

    /* Reusing a large freed block for a small request must preserve the
     * remaining free capacity instead of losing it to internal waste. */
    win32_crt_init(&rt,memory,BASE,BYTES);
    uint32_t large=call(16,128,0,0);
    uint32_t live_guard=call(16,32,0,0);
    assert(large && live_guard==large+128u);
    assert(call(17,large,0,0)==0);
    uint32_t small=call(16,32,0,0);
    uint32_t remainder=call(16,96,0,0);
    assert(small==large && remainder==large+32u);
    assert(call(12,small,'S',32)==small);
    assert(call(12,remainder,'R',96)==remainder);
    for(int i=0;i<32;++i) assert(memory[small-BASE+i]=='S');
    assert(call(17,small,0,0)==0);
    for(int i=0;i<96;++i) assert(memory[remainder-BASE+i]=='R');
    assert(call(17,remainder,0,0)==0);
    assert(call(17,live_guard,0,0)==0);
    assert(rt.next==WIN32_CRT_HEAP_START);

    /* Adjacent non-tail frees coalesce even when a live guard prevents
     * bump-tail reclamation. The merged allocation must not corrupt the
     * guard or leave overlapping free-list records. */
    win32_crt_init(&rt,memory,BASE,BYTES);
    uint32_t frag_a=call(16,32,0,0);
    uint32_t frag_b=call(16,48,0,0);
    uint32_t frag_c=call(16,64,0,0);
    uint32_t frag_guard=call(16,32,0,0);
    assert(frag_b==frag_a+32u && frag_c==frag_b+48u);
    assert(frag_guard==frag_c+64u);
    assert(call(12,frag_guard,'G',32)==frag_guard);
    assert(call(17,frag_a,0,0)==0);
    assert(call(17,frag_c,0,0)==0);
    assert(call(17,frag_b,0,0)==0);
    uint32_t frag_joined=call(16,144,0,0);
    assert(frag_joined==frag_a);
    assert(call(12,frag_joined,'J',144)==frag_joined);
    for(int i=0;i<32;++i) assert(memory[frag_guard-BASE+i]=='G');
    uint32_t frag_next=call(16,16,0,0);
    assert(frag_next>=frag_guard+32u);
    for(int i=0;i<144;++i) assert(memory[frag_joined-BASE+i]=='J');
    assert(call(17,frag_joined,0,0)==0);
    assert(call(17,frag_next,0,0)==0);
    assert(call(17,frag_guard,0,0)==0);
    assert(rt.next==WIN32_CRT_HEAP_START);

    /* Shrinking a live non-tail block returns its aligned suffix to the
     * allocator without overwriting the adjacent live guard. */
    win32_crt_init(&rt,memory,BASE,BYTES);
    uint32_t shrink=call(16,128,0,0);
    uint32_t shrink_guard=call(16,32,0,0);
    assert(shrink && shrink_guard==shrink+128u);
    assert(call(12,shrink,'P',32)==shrink);
    assert(call(12,shrink_guard,'G',32)==shrink_guard);
    assert(call(20,shrink,32,0)==shrink);
    uint32_t shrink_reuse=call(16,96,0,0);
    assert(shrink_reuse==shrink+32u);
    assert(call(12,shrink_reuse,'R',96)==shrink_reuse);
    for(int i=0;i<32;++i) assert(memory[shrink-BASE+i]=='P');
    for(int i=0;i<32;++i) assert(memory[shrink_guard-BASE+i]=='G');
    assert(call(17,shrink_reuse,0,0)==0);
    assert(call(17,shrink,0,0)==0);
    assert(call(17,shrink_guard,0,0)==0);
    assert(rt.next==WIN32_CRT_HEAP_START);

    /* Saturated allocation metadata must not overwrite an existing free
     * record just to allocate from the bump tail. Keep all 32 separated
     * holes available for coalescing after their live guards are freed. */
    win32_crt_init(&rt,memory,BASE,BYTES);
    uint32_t slots[WIN32_CRT_MAX_ALLOCS];
    for(uint32_t i=0;i<WIN32_CRT_MAX_ALLOCS;++i) {
        slots[i]=call(16,16,0,0);
        assert(slots[i]==BASE+WIN32_CRT_HEAP_START+i*16u);
    }
    for(uint32_t i=0;i<WIN32_CRT_MAX_ALLOCS;i+=2u)
        assert(call(17,slots[i],0,0)==0);
    /* No empty metadata slot or free block of 32 bytes remains. */
    assert(call(16,32,0,0)==0);
    assert(call(17,slots[1],0,0)==0);
    uint32_t joined_hole=call(16,32,0,0);
    assert(joined_hole==slots[0]);
    assert(call(12,joined_hole,'H',32)==joined_hole);
    for(uint32_t i=3;i<WIN32_CRT_MAX_ALLOCS;i+=2u)
        assert(call(17,slots[i],0,0)==0);
    for(uint32_t i=0;i<32u;++i)
        assert(memory[joined_hole-BASE+i]=='H');
    assert(call(17,joined_hole,0,0)==0);
    assert(rt.next==WIN32_CRT_HEAP_START);

    /* realloc can expand into an adjacent free block while preserving
     * its pointer, contents and the unused suffix for another malloc. */
    win32_crt_init(&rt,memory,BASE,BYTES);
    uint32_t grow=call(16,32,0,0);
    uint32_t gap=call(16,128,0,0);
    uint32_t grow_guard=call(16,32,0,0);
    assert(grow && gap==grow+32u && grow_guard==gap+128u);
    assert(call(12,grow,'A',32)==grow);
    assert(call(12,grow_guard,'G',32)==grow_guard);
    assert(call(17,gap,0,0)==0);
    assert(call(20,grow,80,0)==grow);
    for(int i=0;i<32;++i) assert(memory[grow-BASE+i]=='A');
    uint32_t suffix=call(16,80,0,0);
    assert(suffix==grow+80u);
    assert(call(12,suffix,'S',80)==suffix);
    for(int i=0;i<32;++i) assert(memory[grow_guard-BASE+i]=='G');
    assert(call(17,suffix,0,0)==0);
    assert(call(17,grow,0,0)==0);
    assert(call(17,grow_guard,0,0)==0);
    assert(rt.next==WIN32_CRT_HEAP_START);

    /* Distinct guest process launches reset all heap bookkeeping. */
    win32_crt_init(&rt,memory,BASE,BYTES);
    assert(call(16,16,0,0)==BASE+WIN32_CRT_HEAP_START);
    puts("Win32 MSVCRT multi-DLL guest memory/heap tests: PASS");
    return 0;
}
