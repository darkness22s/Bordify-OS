#include "pe64_vm.h"

/* Freestanding Windows PE32+ (AMD64) loader + deliberately small interpreter.
 * The only supported guest workload is a console subset with imports
 * GetStdHandle, WriteFile, ExitProcess, GetLastError. No arbitrary installers.
 * Virtual addresses below are guest-only, never host pointers.
 */
#define MAX_SECTIONS 8u
#define MAX_FUNCTIONS 16u
#define STUB_BASE 0x7000000000000000ULL
#define IMAGE_LIMIT 0x8000u
#define STACK_RVA 0xf000u
#define STDOUT_HANDLE 1u
#define INSTALL_HANDLE 0x100u
#define MAX_INSTALL_BYTES 8192u
/* Single-process audited installer staging. Never a guest-mapped pointer. */
static struct {
    uint8_t bytes[MAX_INSTALL_BYTES];
    size_t length;
    bool open;
    bool committed;
} install_state;

static uint16_t u16(const uint8_t *p) {
    return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}
static uint32_t u32(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}
static uint64_t u64(const uint8_t *p) {
    return (uint64_t)u32(p) | ((uint64_t)u32(p + 4) << 32);
}
static void w64(uint8_t *p, uint64_t v) {
    for (unsigned i = 0; i < 8; ++i) p[i] = (uint8_t)(v >> (8 * i));
}
static bool fits(size_t n, size_t off, size_t bytes) {
    return off <= n && bytes <= n - off;
}
static bool has_name(const uint8_t *mem, size_t n, uint32_t at,
                     const char *name) {
    size_t k = 0;
    if (at >= n) return false;
    while (k < 48 && at + k < n) {
        char a = (char)mem[at + k], b = name[k];
        if (a >= 'A' && a <= 'Z') a = (char)(a + 32);
        if (b >= 'A' && b <= 'Z') b = (char)(b + 32);
        if (a != b) return false;
        if (!a) return true;
        ++k;
    }
    return false;
}
static uint64_t imported_api(const uint8_t *mem, size_t n, uint32_t at) {
    if (has_name(mem, n, at, "GetStdHandle")) return STUB_BASE + 1;
    if (has_name(mem, n, at, "WriteFile")) return STUB_BASE + 2;
    if (has_name(mem, n, at, "ExitProcess")) return STUB_BASE + 3;
    if (has_name(mem, n, at, "GetLastError")) return STUB_BASE + 4;
    if (has_name(mem, n, at, "CreateFileA")) return STUB_BASE + 5;
    if (has_name(mem, n, at, "CloseHandle")) return STUB_BASE + 6;
    return 0;
}
static bool off64(uint64_t guest, size_t size, size_t n, size_t *out) {
    if (guest < PE64_GUEST_BASE || guest - PE64_GUEST_BASE > size ||
        n > size - (size_t)(guest - PE64_GUEST_BASE)) return false;
    *out = (size_t)(guest - PE64_GUEST_BASE);
    return true;
}

static bool stage(const uint8_t *file, size_t file_size, uint8_t *mem,
                  size_t memory_size, uint32_t *entry,
                  uint32_t *imported) {
    if (file_size < 0x200 || file_size > PE64_MAX_FILE ||
        file[0] != 'M' || file[1] != 'Z') return false;
    uint32_t nt = u32(file + 0x3c);
    if (nt < 0x40 || !fits(file_size, nt, 24) ||
        u32(file + nt) != 0x4550u ||
        u16(file + nt + 4) != 0x8664u) return false;
    uint16_t count = u16(file + nt + 6);
    uint16_t opt_size = u16(file + nt + 20);
    if (!count || count > MAX_SECTIONS || opt_size < 240 ||
        !(u16(file + nt + 22) & 2u) ||
        !fits(file_size, nt + 24, opt_size)) return false;
    size_t opt = nt + 24u;
    if (u16(file + opt) != 0x20bu ||
        u64(file + opt + 24) != PE64_GUEST_BASE ||
        u16(file + opt + 68) != 3 ||
        u32(file + opt + 108) < 16) return false;
    uint32_t entry_rva = u32(file + opt + 16);
    uint32_t image_size = u32(file + opt + 56);
    uint32_t header_size = u32(file + opt + 60);
    uint32_t imports = u32(file + opt + 120);
    uint32_t import_bytes = u32(file + opt + 124);
    if (image_size < 0x2000 || image_size > IMAGE_LIMIT ||
        !entry_rva || entry_rva >= image_size ||
        header_size < opt + opt_size || header_size > image_size ||
        !fits(file_size, 0, header_size)) return false;
    /* No relocations, TLS, exception tables or delay imports in this VM. */
    const unsigned forbidden[] = {3, 5, 9, 13};
    for (size_t i = 0; i < sizeof(forbidden)/sizeof(forbidden[0]); ++i)
        if (u32(file + opt + 112 + forbidden[i] * 8u)) return false;

    size_t sect = opt + opt_size;
    if (!fits(file_size, sect, count * 40u)) return false;
    bool executable = false;
    for (unsigned i = 0; i < count; ++i) {
        const uint8_t *s = file + sect + 40u*i;
        uint32_t va = u32(s + 12), vs = u32(s + 8);
        uint32_t raw = u32(s + 20), rawlen = u32(s + 16);
        uint32_t len = vs > rawlen ? vs : rawlen;
        if (!len || va < header_size || !fits(image_size,va,len) ||
            !fits(file_size,raw,rawlen)) return false;
        if ((u32(s + 36) & 0x20000000u) &&
            entry_rva >= va && entry_rva-va < len) executable=true;
        for (unsigned j = 0; j < i; ++j) {
            const uint8_t *p = file + sect + 40u*j;
            uint32_t pv = u32(p+12), pl = u32(p+8), pr = u32(p+16);
            if (pr > pl) pl=pr;
            if (va < pv+pl && pv < va+len) return false;
        }
    }
    if (!executable || memory_size != PE64_GUEST_BYTES) return false;
    for (size_t i=0;i<memory_size;++i) mem[i]=0;
    for (size_t i=0;i<header_size;++i) mem[i]=file[i];
    for (unsigned i=0;i<count;++i) {
        const uint8_t *s=file+sect+40u*i;
        uint32_t va=u32(s+12), raw=u32(s+20), n=u32(s+16);
        for (uint32_t j=0;j<n;++j) mem[va+j]=file[raw+j];
    }
    *imported=0;
    if (imports || import_bytes) {
        if (!imports || import_bytes < 40 ||
            !fits(image_size,imports,import_bytes)) return false;
        bool end=false;
        for (uint32_t i=0;i<import_bytes/20u && i<16;++i) {
            uint32_t d=imports+20u*i, lookup=u32(mem+d);
            uint32_t name=u32(mem+d+12), iat=u32(mem+d+16);
            if (!lookup && !name && !iat) {end=true;break;}
            if (!has_name(mem,image_size,name,"kernel32.dll") ||
                !lookup || !iat) return false;
            bool done=false;
            for (uint32_t j=0;j<MAX_FUNCTIONS;++j) {
                if (!fits(image_size,lookup,8u*(j+1)) ||
                    !fits(image_size,iat,8u*(j+1))) return false;
                uint64_t thunk=u64(mem+lookup+8u*j);
                if (!thunk) {done=true;break;}
                if (thunk>UINT32_MAX || thunk+2>=image_size) return false;
                uint64_t api=imported_api(mem,image_size,(uint32_t)thunk+2);
                if (!api) return false;
                w64(mem+iat+8u*j,api);
                ++*imported;
            }
            if (!done) return false;
        }
        if (!end) return false;
    }
    *entry=entry_rva;
    return true;
}

static bool pe64_vm_run_internal(const uint8_t *file, size_t length,
                                 uint8_t *memory, size_t memory_bytes,
                                 pe64_output_t output, void *ctx,
                                 pe64_install_commit_t commit, void *commit_ctx,
                                 pe64_report_t *report) {
    if (!report) return false;
    report->finished=false; report->exit_code=-1;
    report->steps=0; report->imports=0; report->rejected_opcode=0;
    report->installed_bytes=0;
    install_state.length=0;
    install_state.open=false;
    install_state.committed=false;
    if (!file || !memory || !output) return false;
    uint32_t ip=0;
    if (!stage(file,length,memory,memory_bytes,&ip,&report->imports))
        return false;
    uint64_t rcx=0,rdx=0,r8=0,r9=0,rax=0,rbx=0;
    uint64_t rsp=PE64_GUEST_BASE+STACK_RVA;
    for (uint32_t step=0;step<PE64_MAX_STEPS;++step) {
        if (!fits(memory_bytes,ip,2) || ip >= IMAGE_LIMIT) return false;
        uint8_t *p=memory+ip;
        report->steps=step+1;
        /* 48 C7 C1 imm32: mov rcx, sign-extended imm32 */
        if (fits(memory_bytes,ip,7) &&
            p[0]==0x48 && p[1]==0xc7 && p[2]==0xc1) {
            rcx=(uint64_t)(int64_t)(int32_t)u32(p+3);ip+=7;continue;
        }
        /* 48 8D 0D disp32: lea rcx,[rip+disp32] */
        if (fits(memory_bytes,ip,7) &&
            p[0]==0x48 && p[1]==0x8d && p[2]==0x0d) {
            int64_t addr=(int64_t)(PE64_GUEST_BASE+ip+7)+(int32_t)u32(p+3);
            if (addr<(int64_t)PE64_GUEST_BASE) return false;
            rcx=(uint64_t)addr;ip+=7;continue;
        }
        /* BA imm32: mov edx,imm32 (zero extends to RDX). */
        if (fits(memory_bytes,ip,5) && p[0]==0xba) {
            rdx=u32(p+1);ip+=5;continue;
        }
        /* 48 8D 15 disp32: lea rdx,[rip+disp32] */
        if (fits(memory_bytes,ip,7) &&
            p[0]==0x48 && p[1]==0x8d && p[2]==0x15) {
            int64_t addr=(int64_t)(PE64_GUEST_BASE+ip+7)+(int32_t)u32(p+3);
            if (addr<(int64_t)PE64_GUEST_BASE) return false;
            rdx=(uint64_t)addr;ip+=7;continue;
        }
        if (fits(memory_bytes,ip,6) && p[0]==0x41 && p[1]==0xb8) {
            r8=u32(p+2);ip+=6;continue; /* mov r8d,imm32 */
        }
        if (fits(memory_bytes,ip,3) &&
            p[0]==0x45 && p[1]==0x31 && p[2]==0xc9) {
            r9=0;ip+=3;continue; /* xor r9d,r9d */
        }
        if (fits(memory_bytes,ip,3) &&
            p[0]==0x48 && p[1]==0x89 && p[2]==0xc1) {
            rcx=rax;ip+=3;continue; /* mov rcx,rax */
        }
        /* Handle kept in RBX across an imported call (callee-saved). */
        if (fits(memory_bytes,ip,3) &&
            p[0]==0x48 && p[1]==0x89 && p[2]==0xc3) {
            rbx=rax;ip+=3;continue;
        }
        if (fits(memory_bytes,ip,3) &&
            p[0]==0x48 && p[1]==0x89 && p[2]==0xd9) {
            rcx=rbx;ip+=3;continue;
        }
        if (fits(memory_bytes,ip,4) && p[0]==0x48 &&
            p[1]==0x83 && p[2]==0xec) {
            uint64_t amount=p[3];
            if (rsp<PE64_GUEST_BASE+IMAGE_LIMIT+amount) return false;
            rsp-=amount;ip+=4;continue;
        }
        if (fits(memory_bytes,ip,4) && p[0]==0x48 &&
            p[1]==0x83 && p[2]==0xc4) {
            rsp+=p[3];
            if (rsp>PE64_GUEST_BASE+PE64_GUEST_BYTES) return false;
            ip+=4;continue;
        }
        /* C7 44 24 XX imm32: Win64 stack args (5th,6th,7th). */
        if (fits(memory_bytes,ip,8) && p[0]==0xc7 &&
            p[1]==0x44 && p[2]==0x24 &&
            (p[3]==0x20 || p[3]==0x28 || p[3]==0x30)) {
            size_t offset;
            if (!off64(rsp+p[3],memory_bytes,4,&offset)) return false;
            uint32_t v=u32(p+4);
            for (unsigned j=0;j<4;++j) memory[offset+j]=(uint8_t)(v>>(8*j));
            ip+=8;continue;
        }
        /* FF 15 disp32: indirect call through a 64-bit PE import thunk */
        if (fits(memory_bytes,ip,6) && p[0]==0xff && p[1]==0x15) {
            int64_t target=(int64_t)(PE64_GUEST_BASE+ip+6)+(int32_t)u32(p+2);
            size_t rva;
            if (target<(int64_t)PE64_GUEST_BASE ||
                !off64((uint64_t)target,memory_bytes,8,&rva)) return false;
            uint64_t api=u64(memory+rva);
            if (api==STUB_BASE+1) {
                rax=(rcx==(uint64_t)(int64_t)-11) ? STDOUT_HANDLE : 0;
            } else if (api==STUB_BASE+2) { /* WriteFile */
                size_t data_offset, stack_offset;
                if (!off64(rdx,memory_bytes,(size_t)r8,&data_offset) ||
                    !off64(rsp+32,memory_bytes,4,&stack_offset) ||
                    r9!=0 || u32(memory+stack_offset)!=0) return false;
                if (rcx==STDOUT_HANDLE) {
                    if (r8>256) return false;
                    output(memory+data_offset,(size_t)r8,ctx);
                } else if (rcx==INSTALL_HANDLE && commit &&
                           install_state.open) {
                    if (r8>MAX_INSTALL_BYTES-install_state.length) return false;
                    for (size_t k=0;k<(size_t)r8;++k)
                        install_state.bytes[install_state.length+k]=
                            memory[data_offset+k];
                    install_state.length+=(size_t)r8;
                } else return false;
                rax=1;
            } else if (api==STUB_BASE+3) {
                if (commit && (!install_state.committed ||
                    install_state.open)) return false;
                report->finished=true;
                report->exit_code=(int)(int32_t)rcx;
                return true;
            } else if (api==STUB_BASE+4) {
                rax=0;
            } else if (api==STUB_BASE+5) { /* CreateFileA */
                size_t name_offset, stack_offset;
                if (!commit || install_state.open ||
                    install_state.committed || rdx!=0x40000000ULL ||
                    r8!=0 || r9!=0 ||
                    !off64(rcx,memory_bytes,16,&name_offset) ||
                    !off64(rsp+32,memory_bytes,20,&stack_offset) ||
                    u32(memory+stack_offset)!=2 ||
                    u32(memory+stack_offset+8)!=0x80 ||
                    u32(memory+stack_offset+16)!=0 ||
                    !has_name(memory,memory_bytes,(uint32_t)name_offset,
                              "C:\\APP64.EXE")) return false;
                install_state.open=true;
                install_state.length=0;
                rax=INSTALL_HANDLE;
            } else if (api==STUB_BASE+6) { /* CloseHandle */
                if (!commit || rcx!=INSTALL_HANDLE ||
                    !install_state.open || !install_state.length ||
                    !commit(install_state.bytes,install_state.length,commit_ctx))
                    return false;
                install_state.open=false;
                install_state.committed=true;
                report->installed_bytes=(uint32_t)install_state.length;
                rax=1;
            } else return false;
            ip+=6;continue;
        }
        report->rejected_opcode=u32(memory+ip);
        return false;
    }
    return false;
}

bool pe64_vm_run(const uint8_t *file, size_t length,
                 uint8_t *memory, size_t memory_bytes,
                 pe64_output_t output, void *ctx,
                 pe64_report_t *report) {
    return pe64_vm_run_internal(file,length,memory,memory_bytes,
                                output,ctx,NULL,NULL,report);
}
bool pe64_vm_run_install(const uint8_t *file, size_t length,
                         uint8_t *memory, size_t memory_bytes,
                         pe64_output_t output, void *context,
                         pe64_install_commit_t commit, void *commit_context,
                         pe64_report_t *report) {
    if (!commit) return false;
    return pe64_vm_run_internal(file,length,memory,memory_bytes,
                                output,context,commit,commit_context,report);
}
