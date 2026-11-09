#include <assert.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "win32_pe_dll.h"

static void p16(uint8_t *p,uint16_t x){p[0]=(uint8_t)x;p[1]=(uint8_t)(x>>8);}
static void p32(uint8_t *p,uint32_t x){
    for(unsigned i=0;i<4u;++i)p[i]=(uint8_t)(x>>(i*8u));
}
static void make_fixture(uint8_t *f) {
    memset(f,0,0x800u);
    f[0]='M';f[1]='Z';p32(f+0x3cu,0x80u);
    f[0x80]='P';f[0x81]='E';
    p16(f+0x84,0x014cu);p16(f+0x86,1u);
    p16(f+0x94,224u);p16(f+0x96,0x2002u); /* image + DLL */
    uint8_t *opt=f+0x98u;
    p16(opt,0x10bu);p32(opt+28u,0x10000000u);
    p32(opt+56u,0x2000u);p32(opt+60u,0x200u);
    p16(opt+68u,3u);p32(opt+92u,16u);
    p32(opt+96u,0x1100u);p32(opt+100u,0x100u);
    p32(opt+96u+5u*8u,0x1300u);
    p32(opt+100u+5u*8u,12u);
    uint8_t *s=f+0x178u;
    memcpy(s,".text",5u);
    p32(s+8u,0x600u);p32(s+12u,0x1000u);
    p32(s+16u,0x600u);p32(s+20u,0x200u);
    p32(s+36u,0x60000020u);
    uint8_t *e=f+0x300u; /* export RVA 0x1100 */
    p32(e+16u,1u);p32(e+20u,1u);p32(e+24u,1u);
    p32(e+28u,0x1140u);p32(e+32u,0x1144u);
    p32(e+36u,0x1148u);
    p32(f+0x340u,0x1200u);
    p32(f+0x344u,0x1180u);
    p16(f+0x348u,0u);
    memcpy(f+0x380u,"BordifyAdd",11u);
    f[0x400u]=0xc3u; /* ret: executable export target */
    p32(f+0x404u,0x10001234u); /* address needing HIGHLOW rebasing */
    p32(f+0x500u,0x1000u);p32(f+0x504u,12u);
    p16(f+0x508u,0x3204u);p16(f+0x50au,0u);
}
/* Build a well-formed single-DLL import descriptor with both IAT and INT.
 * The synthetic export fixture stays unchanged; only import parsing varies. */
static void set_imports(uint8_t *f) {
    p32(f+0x98u+104u,0x1500u);p32(f+0x98u+108u,40u);
    p32(f+0x700u,0x1540u); /* OriginalFirstThunk */
    p32(f+0x70cu,0x1580u); /* DLL basename */
    p32(f+0x710u,0x1550u);/* FirstThunk */
    p32(f+0x740u,0x15a0u);
    p32(f+0x750u,0x15a0u);
    memcpy(f+0x780u,"kernel32.dll",13u);
    memcpy(f+0x7a2u,"GetTickCount",13u);
}
static uint32_t import_lookup(void *context,const char *dll,
                              const char *symbol,uint32_t ordinal) {
    unsigned *calls=context; ++*calls;
    if(strcmp(dll,"kernel32.dll")!=0)return 0u;
    if(symbol && strcmp(symbol,"GetTickCount")==0)return 0x40070000u;
    if(!symbol && ordinal==7u)return 0x40070004u;
    return 0u;
}
int main(int argc,char **argv) {
    uint8_t file[0x800u],memory[0x2000u];
    win32_pe_dll_info_t info;
    make_fixture(file);
    assert(win32_pe_dll_image_size(file,sizeof(file))==0x2000u);
    /* The staging API must enforce the same file cap as the size probe,
     * even when an oversized buffer starts with an otherwise valid DLL. */
    uint8_t *oversized=calloc(WIN32_PE_DLL_MAX_BYTES+1u,1u);
    assert(oversized);
    memcpy(oversized,file,sizeof(file));
    assert(!win32_pe_dll_image_size(oversized,WIN32_PE_DLL_MAX_BYTES+1u));
    assert(!win32_pe_dll_stage(oversized,WIN32_PE_DLL_MAX_BYTES+1u,
                               memory,sizeof(memory),0x40180000u,&info));
    free(oversized);
    assert(win32_pe_dll_stage(file,sizeof(file),memory,sizeof(memory),
                              0x40180000u,&info));
    assert(info.guest_base==0x40180000u);
    /* A high guest base must not wrap exports into low/kernel addresses. */
    assert(!win32_pe_dll_stage(file,sizeof(file),memory,sizeof(memory),
                               0xfffff000u,&info));
    assert(win32_pe_dll_stage(file,sizeof(file),memory,sizeof(memory),
                              0x40180000u,&info));
    assert(win32_pe_dll_resolve(memory,&info,"BordifyAdd",0u)==0x40181200u);
    assert(win32_pe_dll_resolve(memory,&info,NULL,1u)==0x40181200u);
    assert(info.entry_rva==0u);
    make_fixture(file);
    p32(file+0x98u+16u,0x1200u); /* valid executable DllMain RVA */
    assert(win32_pe_dll_stage(file,sizeof(file),memory,sizeof(memory),
                              0x40180000u,&info));
    assert(info.entry_rva==0x1200u);
    p32(file+0x98u+16u,0x1800u); /* RVA outside executable section */
    assert(!win32_pe_dll_stage(file,sizeof(file),memory,sizeof(memory),
                               0x40180000u,&info));
    /* A virtual executable tail is zero-filled, not file-backed code. */
    make_fixture(file);
    p32(file+0x178u+8u,0x800u); /* virtual size exceeds raw 0x600 */
    p32(file+0x98u+16u,0x1700u); /* entry in unbacked tail */
    assert(!win32_pe_dll_stage(file,sizeof(file),memory,sizeof(memory),
                               0x40180000u,&info));
    p32(file+0x98u+16u,0u);
    p32(file+0x340u,0x1700u); /* exported code in unbacked tail */
    assert(win32_pe_dll_stage(file,sizeof(file),memory,sizeof(memory),
                              0x40180000u,&info));
    assert(!win32_pe_dll_resolve(memory,&info,"BordifyAdd",0u));
    make_fixture(file);
    assert(!win32_pe_dll_resolve(memory,&info,"NoSuchExport",0u));
    assert(!win32_pe_dll_resolve(memory,&info,NULL,2u));
    assert(memory[0x1204u]==0x34u && memory[0x1207u]==0x40u);
    p32(file+0x340u,0x1110u); /* forwarded export: fail closed */
    assert(win32_pe_dll_stage(file,sizeof(file),memory,sizeof(memory),
                              0x40180000u,&info));
    assert(!win32_pe_dll_resolve(memory,&info,"BordifyAdd",0u));
    make_fixture(file);
    p32(file+0x98u+104u,0x1150u); /* malformed nonzero RVA, zero size */
    assert(!win32_pe_dll_stage(file,sizeof(file),memory,sizeof(memory),
                               0x40180000u,&info));
    make_fixture(file);
    /* Empty compiler-style import descriptor is allowed, real import isn't. */
    p32(file+0x98u+104u,0x1150u);
    p32(file+0x98u+108u,20u);
    assert(win32_pe_dll_stage(file,sizeof(file),memory,sizeof(memory),
                              0x40180000u,&info));
    make_fixture(file);
    p32(file+0x98u+104u,0x1150u);
    p32(file+0x98u+108u,20u);
    file[0x350u]=1u; /* unrecognized descriptor still needs binding */
    assert(win32_pe_dll_stage(file,sizeof(file),memory,sizeof(memory),
                              0x40180000u,&info));
    assert(!win32_pe_dll_bind_imports(memory,&info,NULL,NULL));
    make_fixture(file);
    set_imports(file);
    assert(win32_pe_dll_stage(file,sizeof(file),memory,sizeof(memory),
                              0x40180000u,&info));
    unsigned calls=0;
    assert(win32_pe_dll_bind_imports(memory,&info,import_lookup,&calls));
    assert(calls==1u);
    assert(memory[0x1550u]==0u &&
           memory[0x1551u]==0u &&
           memory[0x1552u]==7u &&
           memory[0x1553u]==0x40u);
    /* A valid first import followed by an unresolved second import must
     * leave the original IAT completely untouched. */
    make_fixture(file);
    set_imports(file);
    p32(file+0x744u,0x15c0u); /* second INT entry */
    p32(file+0x754u,0x15c0u); /* second IAT entry */
    memcpy(file+0x7c2u,"MissingApi",11u);
    assert(win32_pe_dll_stage(file,sizeof(file),memory,sizeof(memory),
                              0x40180000u,&info));
    uint8_t old_iat[8];
    memcpy(old_iat,memory+0x1550u,sizeof(old_iat));
    assert(!win32_pe_dll_bind_imports(memory,&info,import_lookup,&calls));
    assert(memcmp(old_iat,memory+0x1550u,sizeof(old_iat))==0);
    /* Repeated import descriptors targeting the same IAT slot must fail
     * without modifying the original thunk, even if both resolve. */
    make_fixture(file);
    set_imports(file);
    p32(file+0x98u+108u,60u); /* two descriptors and terminator */
    memcpy(file+0x714u,file+0x700u,20u);
    assert(win32_pe_dll_stage(file,sizeof(file),memory,sizeof(memory),
                              0x40180000u,&info));
    uint8_t duplicate_iat[4];
    memcpy(duplicate_iat,memory+0x1550u,sizeof(duplicate_iat));
    assert(!win32_pe_dll_bind_imports(memory,&info,import_lookup,&calls));
    assert(memcmp(duplicate_iat,memory+0x1550u,
                  sizeof(duplicate_iat))==0);
    make_fixture(file);
    set_imports(file);
    p32(file+0x740u,0x80000007u);
    assert(win32_pe_dll_stage(file,sizeof(file),memory,sizeof(memory),
                              0x40180000u,&info));
    assert(win32_pe_dll_bind_imports(memory,&info,import_lookup,&calls));
    assert(memory[0x1550u]==4u); /* ordinal resolves to 0x40070004 */
    make_fixture(file);
    set_imports(file);
    file[0x780u]='Z'; /* unknown module name */
    assert(win32_pe_dll_stage(file,sizeof(file),memory,sizeof(memory),
                              0x40180000u,&info));
    assert(!win32_pe_dll_bind_imports(memory,&info,import_lookup,&calls));
    make_fixture(file);
    set_imports(file);
    p32(file+0x740u,0x7fffffffu); /* wildly out-of-bounds hint/name */
    assert(win32_pe_dll_stage(file,sizeof(file),memory,sizeof(memory),
                              0x40180000u,&info));
    assert(!win32_pe_dll_bind_imports(memory,&info,import_lookup,&calls));
    make_fixture(file);
    set_imports(file);
    memset(file+0x714u,0xff,20u); /* missing terminator */
    assert(win32_pe_dll_stage(file,sizeof(file),memory,sizeof(memory),
                              0x40180000u,&info));
    assert(!win32_pe_dll_bind_imports(memory,&info,import_lookup,&calls));
    make_fixture(file);
    p16(file+0x96u,2u); /* not DLL */
    assert(!win32_pe_dll_image_size(file,sizeof(file)));
    make_fixture(file);
    p16(file+0x508u,0x2204u); /* unsupported HIGHLOW replacement type */
    assert(!win32_pe_dll_stage(file,sizeof(file),memory,sizeof(memory),
                               0x40180000u,&info));
    make_fixture(file);
    p32(file+0x178u+20u,0x100u); /* raw bytes alias PE headers */
    assert(!win32_pe_dll_stage(file,sizeof(file),memory,sizeof(memory),
                               0x40180000u,&info));
    make_fixture(file);
    /* Distinct virtual sections must not alias the same raw file bytes. */
    p16(file+0x86u,2u);
    uint8_t *second=file+0x1a0u;
    memcpy(second,".data",5u);
    p32(second+8u,0x100u);p32(second+12u,0x1800u);
    p32(second+16u,0x100u);p32(second+20u,0x400u);
    assert(!win32_pe_dll_stage(file,sizeof(file),memory,sizeof(memory),
                               0x40180000u,&info));
    make_fixture(file);
    p32(file+0x178u+16u,0x1000u); /* section overflows file */
    assert(!win32_pe_dll_stage(file,sizeof(file),memory,sizeof(memory),
                               0x40180000u,&info));
    make_fixture(file);
    p32(file+0x98u+96u+9u*8u,0x1200u); /* TLS not supported */
    assert(!win32_pe_dll_stage(file,sizeof(file),memory,sizeof(memory),
                               0x40180000u,&info));
    if(argc>1) {
        FILE *in=fopen(argv[1],"rb");
        assert(in);
        uint8_t *bin=malloc(WIN32_PE_DLL_MAX_BYTES);
        uint8_t *mapped=malloc(WIN32_PE_DLL_MAX_BYTES);
        assert(bin && mapped);
        size_t n=fread(bin,1,WIN32_PE_DLL_MAX_BYTES,in);
        assert(!ferror(in) && feof(in));fclose(in);
        uint32_t sz=win32_pe_dll_image_size(bin,n);
        assert(sz && sz<=WIN32_PE_DLL_MAX_BYTES);
        assert(win32_pe_dll_stage(bin,n,mapped,WIN32_PE_DLL_MAX_BYTES,
                                  0x40180000u,&info));
        assert(win32_pe_dll_resolve(mapped,&info,"BordifyAdd",0u));
        free(bin);free(mapped);
    }
    puts("Bounded PE32 DLL relocations, export lookup and rejection: PASS");
    return 0;
}
