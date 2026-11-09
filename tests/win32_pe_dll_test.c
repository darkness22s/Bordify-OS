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
int main(int argc,char **argv) {
    uint8_t file[0x800u],memory[0x2000u];
    win32_pe_dll_info_t info;
    make_fixture(file);
    assert(win32_pe_dll_image_size(file,sizeof(file))==0x2000u);
    assert(win32_pe_dll_stage(file,sizeof(file),memory,sizeof(memory),
                              0x40180000u,&info));
    assert(info.guest_base==0x40180000u);
    assert(win32_pe_dll_resolve(memory,&info,"BordifyAdd",0u)==0x40181200u);
    assert(win32_pe_dll_resolve(memory,&info,NULL,1u)==0x40181200u);
    assert(!win32_pe_dll_resolve(memory,&info,"NoSuchExport",0u));
    assert(!win32_pe_dll_resolve(memory,&info,NULL,2u));
    assert(memory[0x1204u]==0x34u && memory[0x1207u]==0x40u);
    p32(file+0x340u,0x1110u); /* forwarded export: fail closed */
    assert(win32_pe_dll_stage(file,sizeof(file),memory,sizeof(memory),
                              0x40180000u,&info));
    assert(!win32_pe_dll_resolve(memory,&info,"BordifyAdd",0u));
    make_fixture(file);
    p32(file+0x98u+104u,0x1150u); /* requires unimplemented imports */
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
    file[0x350u]=1u; /* real DLL import descriptor must fail closed */
    assert(!win32_pe_dll_stage(file,sizeof(file),memory,sizeof(memory),
                               0x40180000u,&info));
    make_fixture(file);
    p16(file+0x96u,2u); /* not DLL */
    assert(!win32_pe_dll_image_size(file,sizeof(file)));
    make_fixture(file);
    p16(file+0x508u,0x2204u); /* unsupported HIGHLOW replacement type */
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
