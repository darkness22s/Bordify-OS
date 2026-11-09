#include "win32_pe_dll.h"

/* Treat both file input and its PE metadata as untrusted. Nothing in this
 * parser dereferences a guest-provided pointer as a host/kernel pointer.
 * No executable bytes are invoked from the kernel. */
#define DLL_MAX_SECTIONS 16u
#define DLL_MAX_EXPORTS 256u
static uint16_t rd16(const uint8_t *p) {
    return (uint16_t)(p[0]|((uint16_t)p[1]<<8));
}
static uint32_t rd32(const uint8_t *p) {
    return (uint32_t)p[0]|((uint32_t)p[1]<<8)|
           ((uint32_t)p[2]<<16)|((uint32_t)p[3]<<24);
}
static void wr32(uint8_t *p,uint32_t v) {
    p[0]=(uint8_t)v;p[1]=(uint8_t)(v>>8);
    p[2]=(uint8_t)(v>>16);p[3]=(uint8_t)(v>>24);
}
static bool fits(size_t size,size_t offset,size_t count) {
    return offset<=size && count<=size-offset;
}
static bool cstring(const uint8_t *m,size_t size,uint32_t rva,
                    const char *name) {
    if(!name || rva>=size)return false;
    for(size_t i=0;i<128u;++i) {
        if(!fits(size,rva+i,1u))return false;
        char a=(char)m[rva+i],b=name[i];
        if(a!=b)return false;
        if(!a)return true;
    }
    return false;
}
static bool header(const uint8_t *f,size_t len,size_t *nt_out,
                   size_t *opt_out,uint16_t *section_count) {
    if(!f || len<64u || f[0]!='M' || f[1]!='Z')return false;
    size_t nt=rd32(f+0x3cu);
    if(nt<64u || !fits(len,nt,24u))return false;
    if(f[nt]!='P' || f[nt+1]!='E' || f[nt+2] || f[nt+3])return false;
    uint16_t sections=rd16(f+nt+6u),optional=rd16(f+nt+20u);
    uint16_t flags=rd16(f+nt+22u);
    if(rd16(f+nt+4u)!=0x014cu || !sections ||
       sections>DLL_MAX_SECTIONS || optional<224u ||
       !(flags&0x0002u) || !(flags&0x2000u) ||
       !fits(len,nt+24u,optional))return false;
    size_t opt=nt+24u;
    uint16_t subsystem=rd16(f+opt+68u);
    if(rd16(f+opt)!=0x010bu || rd32(f+opt+92u)<16u ||
       (subsystem!=2u && subsystem!=3u))return false;
    if(!fits(len,opt+optional,(size_t)sections*40u))return false;
    *nt_out=nt;*opt_out=opt;*section_count=sections;
    return true;
}
uint32_t win32_pe_dll_image_size(const uint8_t *file,size_t file_size) {
    size_t nt,opt;uint16_t sections;
    if(!file || file_size>WIN32_PE_DLL_MAX_BYTES ||
       !header(file,file_size,&nt,&opt,&sections))return 0u;
    uint32_t size=rd32(file+opt+56u);
    if(size<4096u || size>WIN32_PE_DLL_MAX_BYTES)return 0u;
    return size;
}
static bool rebase(uint8_t *m,uint32_t image_size,uint32_t preferred,
                   uint32_t base,uint32_t rva,uint32_t length) {
    if(preferred==base)return true;
    if(!rva || length<8u || !fits(image_size,rva,length))return false;
    size_t cursor=rva,end=(size_t)rva+length;
    uint32_t delta=base-preferred;
    while(cursor<end) {
        if(!fits(end,cursor,8u))return false;
        uint32_t page=rd32(m+cursor);
        uint32_t bytes=rd32(m+cursor+4u);
        if(bytes<8u || (bytes&1u) || !fits(end,cursor,bytes))return false;
        for(uint32_t i=8u;i<bytes;i+=2u) {
            uint16_t entry=rd16(m+cursor+i);
            uint32_t type=entry>>12u;
            if(type==0u)continue;
            if(type!=3u)return false; /* HIGHLOW only */
            uint64_t target=(uint64_t)page+(entry&0x0fffu);
            if(target>image_size || !fits(image_size,(size_t)target,4u))
                return false;
            wr32(m+(size_t)target,rd32(m+(size_t)target)+delta);
        }
        cursor+=bytes;
    }
    return cursor==end;
}
static bool executable_rva(const uint8_t *m,uint32_t size,uint32_t rva) {
    size_t nt,opt;uint16_t count;
    if(!header(m,size,&nt,&opt,&count))return false;
    size_t sections=opt+rd16(m+nt+20u);
    for(uint32_t i=0;i<count;++i) {
        const uint8_t *s=m+sections+i*40u;
        uint32_t start=rd32(s+12u),n=rd32(s+8u),raw=rd32(s+16u);
        if(raw>n)n=raw;
        if((rd32(s+36u)&0x20000000u) &&
           rva>=start && rva-start<n && rva<size)return true;
    }
    return false;
}
bool win32_pe_dll_stage(const uint8_t *file,size_t file_size,
                        uint8_t *dst,size_t capacity,
                        uint32_t base,win32_pe_dll_info_t *out) {
    if(out){out->image_size=0;out->guest_base=0;
            out->export_rva=0;out->export_size=0;}
    size_t nt,opt;uint16_t count;
    if(!out || !dst || (base&4095u) ||
       !header(file,file_size,&nt,&opt,&count))return false;
    uint32_t size=rd32(file+opt+56u),headers=rd32(file+opt+60u);
    size_t section_table=opt+rd16(file+nt+20u);
    if(size<4096u || size>WIN32_PE_DLL_MAX_BYTES || size>capacity ||
       headers<section_table+count*40u ||
       !fits(file_size,0u,headers) || headers>size)return false;
    uint32_t exp=rd32(file+opt+96u),exp_size=rd32(file+opt+100u);
    uint32_t reloc=rd32(file+opt+96u+5u*8u);
    uint32_t reloc_size=rd32(file+opt+100u+5u*8u);
    /* MinGW may supply an import directory containing ONLY its zeroed
     * 20-byte terminator. That is still import-free. Require exactly this
     * layout; any actual import descriptors remain unsupported. */
    uint32_t imports=rd32(file+opt+104u);
    uint32_t imports_size=rd32(file+opt+108u);
    if((imports || imports_size) &&
       (!imports || imports_size!=20u ||
        !fits(size,imports,20u)))return false;
    if(rd32(file+opt+96u+9u*8u) ||
       rd32(file+opt+96u+13u*8u) ||
       !exp || exp_size<40u || !fits(size,exp,exp_size))
        return false;
    for(uint32_t i=0;i<count;++i) {
        const uint8_t *s=file+section_table+i*40u;
        uint32_t v=rd32(s+12u),n=rd32(s+8u);
        uint32_t raw=rd32(s+16u),offset=rd32(s+20u);
        if(raw>n)n=raw;
        if(!n || v<headers || !fits(size,v,n) ||
           !fits(file_size,offset,raw))return false;
        for(uint32_t j=0;j<i;++j) {
            const uint8_t *other=file+section_table+j*40u;
            uint32_t b=rd32(other+12u),bn=rd32(other+8u);
            uint32_t br=rd32(other+16u);
            if(br>bn)bn=br;
            if(v<b+bn && b<v+n)return false;
        }
    }
    for(size_t i=0;i<capacity;++i)dst[i]=0;
    for(size_t i=0;i<headers;++i)dst[i]=file[i];
    for(uint32_t i=0;i<count;++i) {
        const uint8_t *s=file+section_table+i*40u;
        uint32_t v=rd32(s+12u),raw=rd32(s+16u),offset=rd32(s+20u);
        for(uint32_t j=0;j<raw;++j)dst[v+j]=file[offset+j];
    }
    if(imports) {
        for(unsigned i=0;i<20u;++i)
            if(dst[imports+i]!=0u)return false; /* real dependencies */
    }
    if(!rebase(dst,size,rd32(file+opt+28u),base,reloc,reloc_size))
        return false;
    const uint8_t *exports=dst+exp;
    uint32_t num_functions=rd32(exports+20u);
    uint32_t num_names=rd32(exports+24u);
    uint32_t funcs=rd32(exports+28u);
    uint32_t names=rd32(exports+32u);
    uint32_t ordinals=rd32(exports+36u);
    if(!num_functions || num_functions>DLL_MAX_EXPORTS ||
       num_names>DLL_MAX_EXPORTS ||
       !fits(size,funcs,(size_t)num_functions*4u) ||
       !fits(size,names,(size_t)num_names*4u) ||
       !fits(size,ordinals,(size_t)num_names*2u))return false;
    for(uint32_t i=0;i<num_names;++i) {
        uint32_t n=rd32(dst+names+i*4u);
        if(n>=size || rd16(dst+ordinals+i*2u)>=num_functions)
            return false;
        bool terminated=false;
        for(uint32_t j=0;j<128u && (size_t)n+j<size;++j)
            if(dst[n+j]==0u){terminated=j!=0u;break;}
        if(!terminated)return false;
    }
    out->image_size=size;
    out->guest_base=base;
    out->export_rva=exp;
    out->export_size=exp_size;
    return true;
}
uint32_t win32_pe_dll_resolve(const uint8_t *image,
                             const win32_pe_dll_info_t *info,
                             const char *name,uint32_t ordinal) {
    if(!image || !info || !info->image_size ||
       !fits(info->image_size,info->export_rva,40u))return 0u;
    const uint8_t *e=image+info->export_rva;
    uint32_t base_ord=rd32(e+16u),nfunc=rd32(e+20u);
    uint32_t nnames=rd32(e+24u),funcs=rd32(e+28u);
    uint32_t names=rd32(e+32u),ords=rd32(e+36u);
    if(nfunc==0 || nfunc>DLL_MAX_EXPORTS || nnames>DLL_MAX_EXPORTS ||
       !fits(info->image_size,funcs,(size_t)nfunc*4u) ||
       !fits(info->image_size,names,(size_t)nnames*4u) ||
       !fits(info->image_size,ords,(size_t)nnames*2u))return 0u;
    uint32_t index=UINT32_MAX;
    if(name) {
        for(uint32_t i=0;i<nnames;++i) {
            uint32_t rva=rd32(image+names+i*4u);
            if(cstring(image,info->image_size,rva,name)) {
                index=rd16(image+ords+i*2u);
                break;
            }
        }
    } else if(ordinal>=base_ord && ordinal-base_ord<nfunc) {
        index=ordinal-base_ord;
    }
    if(index>=nfunc)return 0u;
    uint32_t target=rd32(image+funcs+index*4u);
    if(!target || target>=info->image_size ||
       (target>=info->export_rva &&
        (uint64_t)target<(uint64_t)info->export_rva+info->export_size) ||
       !executable_rva(image,info->image_size,target))
        return 0u;
    return info->guest_base+target;
}
