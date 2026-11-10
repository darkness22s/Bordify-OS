#include "win32_dll.h"
#include "win32_pe.h"

/* These are virtual modules containing only the ABI shims provided by the
 * current kernel. Names resolve to addresses inside the *guest's* ring-3
 * trampoline page, never a pointer to a kernel function or host library.
 */
typedef struct { const char *name; uint8_t slot; } export_t;
static const export_t kernel32_exports[]={
    {"ExitProcess",0},{"GetStdHandle",1},{"WriteFile",2},
    {"GetLastError",3},{"CreateFileA",4},{"ReadFile",5},
    {"CloseHandle",6},{"GetCurrentProcessId",7},
    {"GetCurrentThreadId",8},{"GetTickCount",9},
    {"GetModuleHandleA",10},{"GetModuleHandleW",11},
    {"GetCommandLineA",12},{"GetCommandLineW",13},
    {"SetLastError",14},{"IsDebuggerPresent",15},
    {"VirtualAlloc",27},{"VirtualFree",28},
    {"LoadLibraryA",29},{"GetProcAddress",30},{"FreeLibrary",31},
    {"LoadLibraryW",32}
};
static const export_t msvcrt_exports[]={
    {"strlen",16},{"memcpy",17},{"memset",18},{"memcmp",19},
    {"strcmp",20},{"strncmp",21},{"malloc",22},{"free",23},
    {"calloc",24},{"memmove",25},{"realloc",26}
};
static bool in_range(const win32_dll_t *dll,uint32_t address) {
    return dll && dll->guest && address>=dll->base &&
           (size_t)(address-dll->base)<dll->bytes;
}
static bool read_name(const win32_dll_t *dll,uint32_t address,
                      char output[65]) {
    if(!in_range(dll,address))return false;
    size_t offset=(size_t)(address-dll->base);
    for(size_t i=0;i<64u;++i) {
        if(i>=dll->bytes-offset)return false;
        char c=(char)dll->guest[offset+i];
        output[i]=c;
        if(!c)return i!=0u;
    }
    output[64]=0;
    return false; /* unterminated or suspiciously long */
}
/* UTF-16LE names stay in user memory and must fit in 64 ASCII code units.
 * Non-ASCII name resolution needs a real Unicode-aware DLL loader. */
static bool read_wide_name(const win32_dll_t *dll,uint32_t address,
                           char output[65]) {
    if(!in_range(dll,address))return false;
    size_t offset=(size_t)(address-dll->base);
    size_t available=dll->bytes-offset;
    for(size_t i=0;i<64u;++i) {
        size_t at=i*2u;
        if(at>available || available-at<2u)return false;
        uint8_t low=dll->guest[offset+at];
        uint8_t high=dll->guest[offset+at+1u];
        if(high!=0u || low>127u)return false;
        output[i]=(char)low;
        if(low==0u)return i!=0u;
    }
    output[64]=0;
    return false;
}
static char lower_ascii(char c) {
    return (c>='A'&&c<='Z')?(char)(c+('a'-'A')):c;
}
static bool equals_ci(const char *a,const char *b) {
    for(size_t i=0;i<65u;++i) {
        if(lower_ascii(a[i])!=lower_ascii(b[i]))return false;
        if(!a[i])return true;
    }
    return false;
}
static bool equals_exact(const char *a,const char *b) {
    for(size_t i=0;i<65u;++i) {
        if(a[i]!=b[i])return false;
        if(!a[i])return true;
    }
    return false;
}
static uint32_t fail(win32_dll_t *dll,uint32_t error) {
    if(dll)dll->last_error=error;
    return 0;
}
/* Each process has a tiny bounded import graph with strong references.
 * A transient image never becomes visible to GetModuleHandle or
 * GetProcAddress until every IAT entry is validated and resolved. */
static int builtin_by_name(const char *name) {
    if(equals_ci(name,"kernel32.dll")||equals_ci(name,"kernel32"))return 0;
    if(equals_ci(name,"msvcrt.dll")||equals_ci(name,"msvcrt"))return 1;
    return -1;
}
static int builtin_by_handle(uint32_t handle) {
    if(handle==WIN32_DLL_KERNEL32_HANDLE)return 0;
    if(handle==WIN32_DLL_MSVCRT_HANDLE)return 1;
    return -1;
}
static uint32_t builtin_symbol(int id,const char *name,uint32_t ordinal) {
    if(!name || ordinal)return 0u;
    const export_t *list=id==0?kernel32_exports:msvcrt_exports;
    size_t count=id==0?sizeof(kernel32_exports)/sizeof(export_t):
                       sizeof(msvcrt_exports)/sizeof(export_t);
    for(size_t i=0;i<count;++i)
        if(equals_exact(name,list[i].name))
            return WIN32_PE_USER_BASE+WIN32_PE_RUNTIME_OFFSET+
                   (uint32_t)list[i].slot*WIN32_PE_BRIDGE_SLOT;
    return 0u;
}
/* No slash, colon, path traversal or long filename support. */
static bool valid_dll_basename(const char *name) {
    if(!name)return false;
    unsigned i=0;
    while(name[i] && name[i]!='.') {
        char c=name[i];
        if(i>=8u || !((c>='A'&&c<='Z')||(c>='a'&&c<='z')||
                       (c>='0'&&c<='9')||c=='_'||c=='-'))
            return false;
        ++i;
    }
    return i!=0u && name[i]=='.' && equals_ci(name+i,".dll");
}
static void clear_module(win32_dll_image_t *m) {
    m->state=0;m->initialized=false;
    m->handle=0;m->refs=0;m->dep_refs=0;
    m->image_deps=0;m->builtin_deps=0;m->name[0]=0;
    m->info.image_size=0;m->info.guest_base=0;
    m->info.export_rva=0;m->info.export_size=0;
    m->info.import_rva=0;m->info.import_size=0;
    m->info.entry_rva=0;
}
void win32_dll_init(win32_dll_t *dll,const uint8_t *guest,
                    uint32_t base,size_t bytes) {
    if(!dll)return;
    dll->guest=guest;dll->base=base;dll->bytes=bytes;dll->last_error=0;
    dll->refs[0]=0;dll->refs[1]=0;
    dll->pinned[0]=false;dll->pinned[1]=false;
    dll->builtin_dep_refs[0]=0;dll->builtin_dep_refs[1]=0;
    dll->exe_image_deps=0u;dll->exe_builtin_deps=0u;
    dll->binding_exe=false;dll->allow_dynamic_init=false;
    dll->vm=NULLPTR;dll->reader=NULLPTR;dll->reader_context=NULLPTR;
    for(unsigned i=0;i<WIN32_DLL_MAX_IMAGES;++i)
        clear_module(&dll->images[i]);
}
void win32_dll_pin_imports(win32_dll_t *dll,uint32_t mask) {
    if(!dll)return;
    dll->pinned[0]=(mask&1u)!=0u;
    dll->pinned[1]=(mask&2u)!=0u;
}
void win32_dll_set_reader(win32_dll_t *dll,win32_vm_t *vm,
                          win32_dll_reader_t reader,void *context) {
    if(!dll)return;
    dll->vm=vm;dll->reader=reader;dll->reader_context=context;
}
static int slot_by_name(const win32_dll_t *dll,const char *name) {
    for(unsigned i=0;i<WIN32_DLL_MAX_IMAGES;++i)
        if(dll->images[i].state &&
           equals_ci(name,dll->images[i].name))return (int)i;
    return -1;
}
static int slot_by_handle(const win32_dll_t *dll,uint32_t handle) {
    for(unsigned i=0;i<WIN32_DLL_MAX_IMAGES;++i)
        if(dll->images[i].state==2u &&
           dll->images[i].handle==handle)return (int)i;
    return -1;
}
static void unload_slot(win32_dll_t *dll,unsigned slot) {
    if(slot>=WIN32_DLL_MAX_IMAGES)return;
    win32_dll_image_t *m=&dll->images[slot];
    if(m->state!=2u && m->state!=1u)return;
    uint8_t real_deps=m->image_deps,builtin_deps=m->builtin_deps;
    uint32_t handle=m->handle;
    clear_module(m);
    if(handle && dll->vm)win32_vm_free_module(dll->vm,handle);
    for(unsigned i=0;i<2u;++i)
        if((builtin_deps&(1u<<i)) && dll->builtin_dep_refs[i])
            --dll->builtin_dep_refs[i];
    for(unsigned i=0;i<WIN32_DLL_MAX_IMAGES;++i)
        if((real_deps&(1u<<i)) && dll->images[i].dep_refs) {
            --dll->images[i].dep_refs;
            if(!dll->images[i].refs && !dll->images[i].dep_refs)
                unload_slot(dll,i);
        }
}
/* Reentrant because a DLL may import another DLL. The staging buffer is
 * consumed and copied into guest memory BEFORE resolving dependencies. */
static uint8_t file_staging[WIN32_PE_DLL_MAX_BYTES];
typedef struct {
    win32_dll_t *dll;
    unsigned parent; /* WIN32_DLL_MAX_IMAGES identifies executable imports */
    uint32_t count;
} link_context_t;
static uint32_t load_name_mode(win32_dll_t *dll,const char *name,bool dependency);
static uint32_t import_symbol(void *context,const char *module,
                              const char *symbol,uint32_t ordinal) {
    link_context_t *ctx=context;
    win32_dll_t *dll=ctx->dll;
    win32_dll_image_t *parent=ctx->parent<WIN32_DLL_MAX_IMAGES?
                                &dll->images[ctx->parent]:NULLPTR;
    uint8_t *image_deps=parent?&parent->image_deps:&dll->exe_image_deps;
    uint8_t *builtin_deps=parent?&parent->builtin_deps:&dll->exe_builtin_deps;
    int builtin=builtin_by_name(module);
    if(builtin>=0) {
        uint32_t proc=builtin_symbol(builtin,symbol,ordinal);
        if(!proc){fail(dll,WIN32_DLL_ERROR_PROC_NOT_FOUND);return 0u;}
        uint8_t mask=(uint8_t)(1u<<builtin);
        if(!(*builtin_deps&mask)) {
            if(dll->builtin_dep_refs[builtin]==65535u) {
                fail(dll,WIN32_DLL_ERROR_NOT_ENOUGH_MEMORY);
                return 0u;
            }
            ++dll->builtin_dep_refs[builtin];
            *builtin_deps|=mask;
        }
        ++ctx->count;
        return proc;
    }
    if(!valid_dll_basename(module)) {
        fail(dll,WIN32_DLL_ERROR_MOD_NOT_FOUND);
        return 0u;
    }
    int child=slot_by_name(dll,module);
    if(child>=0 && dll->images[child].state!=2u) {
        fail(dll,WIN32_DLL_ERROR_BAD_EXE_FORMAT); /* dependency cycle */
        return 0u;
    }
    uint32_t handle=0u;
    if(child<0 || !(*image_deps&(1u<<child))) {
        handle=load_name_mode(dll,module,true);
        if(!handle)return 0u;
        child=slot_by_handle(dll,handle);
        if(child<0){fail(dll,WIN32_DLL_ERROR_BAD_EXE_FORMAT);return 0u;}
        *image_deps|=(uint8_t)(1u<<child);
    } else handle=dll->images[child].handle;
    const win32_dll_image_t *dep=&dll->images[child];
    uint32_t code=win32_pe_dll_resolve(
        dll->vm->guest+(size_t)(dep->handle-dll->vm->base),
        &dep->info,symbol,ordinal);
    if(!code)fail(dll,WIN32_DLL_ERROR_PROC_NOT_FOUND);
    else ++ctx->count;
    return code;
}
static uint32_t load_name_mode(win32_dll_t *dll,const char *name,bool dependency) {
    int builtin=builtin_by_name(name);
    if(builtin>=0) {
        if(!dependency) {
            if(dll->refs[builtin]==65535u)
                return fail(dll,WIN32_DLL_ERROR_BAD_ARGUMENT);
            ++dll->refs[builtin];
        }
        dll->last_error=0;
        return builtin==0?WIN32_DLL_KERNEL32_HANDLE:WIN32_DLL_MSVCRT_HANDLE;
    }
    if(!valid_dll_basename(name) || !dll->reader || !dll->vm)
        return fail(dll,WIN32_DLL_ERROR_MOD_NOT_FOUND);
    int slot=slot_by_name(dll,name);
    if(slot>=0) {
        win32_dll_image_t *m=&dll->images[slot];
        if(m->state!=2u)return fail(dll,WIN32_DLL_ERROR_BAD_EXE_FORMAT);
        uint16_t *refs=dependency?&m->dep_refs:&m->refs;
        if(*refs==65535u)return fail(dll,WIN32_DLL_ERROR_NOT_ENOUGH_MEMORY);
        ++*refs;
        dll->last_error=0;
        return m->handle;
    }
    for(unsigned i=0;i<WIN32_DLL_MAX_IMAGES;++i)
        if(!dll->images[i].state){slot=(int)i;break;}
    if(slot<0)return fail(dll,WIN32_DLL_ERROR_NOT_ENOUGH_MEMORY);
    win32_dll_image_t *m=&dll->images[slot];
    clear_module(m);
    m->state=1u;
    unsigned len=0u;
    for(;name[len] && len+1u<sizeof(m->name);++len)
        m->name[len]=name[len];
    m->name[len]=0;
    int size=dll->reader(name,file_staging,sizeof(file_staging),
                         dll->reader_context);
    if(size<=0 || (size_t)size>sizeof(file_staging)) {
        clear_module(m);
        return fail(dll,WIN32_DLL_ERROR_MOD_NOT_FOUND);
    }
    uint32_t mapped_size=win32_pe_dll_image_size(file_staging,(size_t)size);
    if(!mapped_size){clear_module(m);return fail(dll,WIN32_DLL_ERROR_BAD_EXE_FORMAT);}
    uint32_t address=win32_vm_alloc_module(dll->vm,mapped_size);
    if(!address){clear_module(m);return fail(dll,WIN32_DLL_ERROR_NOT_ENOUGH_MEMORY);}
    m->handle=address;
    uint8_t *mapped=dll->vm->guest+(size_t)(address-dll->vm->base);
    if(!win32_pe_dll_stage(file_staging,(size_t)size,mapped,mapped_size,
                           address,&m->info)) {
        unload_slot(dll,(unsigned)slot);
        return fail(dll,WIN32_DLL_ERROR_BAD_EXE_FORMAT);
    }
    /* Only the top-level guest LoadLibrary syscall may request a new
     * DllMain callback. Refuse uninitialized dependency entrypoints
     * instead of letting their exports run without PROCESS_ATTACH. */
    if(m->info.entry_rva && !dll->binding_exe &&
       (!dll->allow_dynamic_init || dependency)) {
        unload_slot(dll,(unsigned)slot);
        return fail(dll,WIN32_DLL_ERROR_BAD_EXE_FORMAT);
    }
    link_context_t ctx={dll,(unsigned)slot,0u};
    dll->last_error=0;
    if(!win32_pe_dll_bind_imports(mapped,&m->info,import_symbol,&ctx)) {
        uint32_t code=dll->last_error?dll->last_error:
                       WIN32_DLL_ERROR_BAD_EXE_FORMAT;
        unload_slot(dll,(unsigned)slot); /* rolls back dependency refs */
        return fail(dll,code);
    }
    m->state=2u;
    if(dependency)m->dep_refs=1u;
    else m->refs=1u;
    dll->last_error=0u;
    return address;
}
void win32_dll_release_exe_imports(win32_dll_t *dll) {
    if(!dll)return;
    uint8_t modules=dll->exe_image_deps,builtins=dll->exe_builtin_deps;
    dll->exe_image_deps=0u;dll->exe_builtin_deps=0u;
    for(unsigned i=0;i<2u;++i)
        if((builtins&(1u<<i)) && dll->builtin_dep_refs[i])
            --dll->builtin_dep_refs[i];
    for(unsigned i=0;i<WIN32_DLL_MAX_IMAGES;++i)
        if((modules&(1u<<i)) && dll->images[i].dep_refs) {
            --dll->images[i].dep_refs;
            if(!dll->images[i].refs && !dll->images[i].dep_refs)
                unload_slot(dll,i);
        }
}
bool win32_dll_bind_exe_imports(win32_dll_t *dll,uint32_t image_size,
                                uint32_t import_rva,uint32_t import_size,
                                uint32_t *bound_count) {
    if(bound_count)*bound_count=0u;
    if(!dll || !dll->vm || !dll->vm->guest ||
       image_size>WIN32_VM_START || !bound_count)return false;
    win32_pe_dll_info_t image={0};
    image.image_size=image_size;
    image.guest_base=dll->base;
    image.import_rva=import_rva;
    image.import_size=import_size;
    link_context_t ctx={dll,WIN32_DLL_MAX_IMAGES,0u};
    dll->binding_exe=true;
    bool ok=win32_pe_dll_bind_imports(dll->vm->guest,&image,
                                      import_symbol,&ctx);
    dll->binding_exe=false;
    if(!ok) {
        win32_dll_release_exe_imports(dll);
        return false;
    }
    *bound_count=ctx.count;
    return true;
}
bool win32_dll_attach_plan(const win32_dll_t *dll,
                           uint32_t handles[WIN32_DLL_MAX_IMAGES],
                           uint32_t entries[WIN32_DLL_MAX_IMAGES],
                           uint32_t *count) {
    if(!dll || !handles || !entries || !count)return false;
    *count=0u;
    /* Plan atomically: a cycle or invalid entry must not expose a
     * partially ordered list to the caller. */
    uint32_t planned_handles[WIN32_DLL_MAX_IMAGES];
    uint32_t planned_entries[WIN32_DLL_MAX_IMAGES];
    uint32_t planned_count=0u;
    uint8_t visited=0u;
    /* Reject overlapping live module images before publishing any attach
     * addresses. An overlapping mapping would let one DLL's callback
     * execute bytes belonging to another DLL. Check even entryless DLLs. */
    for(unsigned i=0;i<WIN32_DLL_MAX_IMAGES;++i) {
        const win32_dll_image_t *a=&dll->images[i];
        if(a->state!=2u)continue;
        if(!dll->guest || !a->info.image_size || a->handle<dll->base ||
           (uint64_t)(a->handle-dll->base)+a->info.image_size>dll->bytes)
            return false;
        /* Validate entry RVAs even for already-attached modules: their
         * state must not conceal a corrupt callback address. */
        if(a->info.entry_rva &&
           (a->info.entry_rva>=a->info.image_size ||
            (uint64_t)a->handle+a->info.entry_rva>UINT32_MAX))
            return false;
        uint64_t a_end=(uint64_t)a->handle+a->info.image_size;
        for(unsigned j=0;j<i;++j) {
            const win32_dll_image_t *b=&dll->images[j];
            if(b->state!=2u)continue;
            uint64_t b_end=(uint64_t)b->handle+b->info.image_size;
            if((uint64_t)a->handle<b_end &&
               (uint64_t)b->handle<a_end)return false;
        }
    }
    for(unsigned pass=0;pass<WIN32_DLL_MAX_IMAGES;++pass) {
        bool progress=false;
        for(unsigned i=0;i<WIN32_DLL_MAX_IMAGES;++i) {
            const win32_dll_image_t *m=&dll->images[i];
            uint8_t bit=(uint8_t)(1u<<i);
            if(m->state!=2u || (visited&bit))continue;
            if((m->image_deps&~visited)!=0u)continue;
            /* An attached parent cannot legitimately depend on a DLL
             * whose PROCESS_ATTACH has not completed. Reject this
             * inconsistent graph rather than silently accepting it. */
            if(m->initialized) {
                for(unsigned dep=0;dep<WIN32_DLL_MAX_IMAGES;++dep)
                    if((m->image_deps&(1u<<dep)) &&
                       !dll->images[dep].initialized &&
                       dll->images[dep].info.entry_rva)return false;
            }
            visited|=bit;
            /* PROCESS_ATTACH is once per module lifetime. A later plan
             * must not schedule an already initialized DLL again. */
            if(m->info.entry_rva && !m->initialized) {
                if(m->info.entry_rva>=m->info.image_size ||
                   !m->handle || !dll->guest ||
                   m->handle<dll->base ||
                   (uint64_t)(m->handle-dll->base)+
                       m->info.image_size>dll->bytes ||
                   m->handle>UINT32_MAX-m->info.entry_rva)return false;
                planned_handles[planned_count]=m->handle;
                planned_entries[planned_count]=m->handle+m->info.entry_rva;
                ++planned_count;
            }
            progress=true;
        }
        if(!progress)break;
    }
    for(unsigned i=0;i<WIN32_DLL_MAX_IMAGES;++i)
        if(dll->images[i].state==2u && !(visited&(1u<<i)))
            return false;
    for(uint32_t i=0;i<planned_count;++i) {
        handles[i]=planned_handles[i];
        entries[i]=planned_entries[i];
    }
    *count=planned_count;
    return true;
}
uint32_t win32_dll_load(win32_dll_t *dll,uint32_t guest_name) {
    char name[65];
    if(!dll || !read_name(dll,guest_name,name))
        return fail(dll,WIN32_DLL_ERROR_BAD_ARGUMENT);
    return load_name_mode(dll,name,false);
}
uint32_t win32_dll_load_w(win32_dll_t *dll,uint32_t guest_name) {
    char name[65];
    if(!dll || !read_wide_name(dll,guest_name,name))
        return fail(dll,WIN32_DLL_ERROR_BAD_ARGUMENT);
    return load_name_mode(dll,name,false);
}
uint32_t win32_dll_get_module(win32_dll_t *dll,uint32_t guest_name,bool wide) {
    if(!dll)return 0u;
    if(guest_name==0u){dll->last_error=0u;return WIN32_PE_USER_BASE;}
    char name[65];
    if(!(wide?read_wide_name(dll,guest_name,name):
               read_name(dll,guest_name,name)))
        return fail(dll,WIN32_DLL_ERROR_BAD_ARGUMENT);
    int builtin=builtin_by_name(name);
    if(builtin>=0) {
        if(!dll->pinned[builtin] && !dll->refs[builtin] &&
           !dll->builtin_dep_refs[builtin])
            return fail(dll,WIN32_DLL_ERROR_MOD_NOT_FOUND);
        dll->last_error=0u;
        return builtin==0?WIN32_DLL_KERNEL32_HANDLE:WIN32_DLL_MSVCRT_HANDLE;
    }
    int slot=slot_by_name(dll,name);
    if(slot<0 || dll->images[slot].state!=2u)
        return fail(dll,WIN32_DLL_ERROR_MOD_NOT_FOUND);
    dll->last_error=0u;
    return dll->images[slot].handle;
}
uint32_t win32_dll_symbol(win32_dll_t *dll,uint32_t handle,
                          uint32_t guest_symbol) {
    if(!dll)return 0u;
    int slot=slot_by_handle(dll,handle);
    if(slot>=0) {
        char name[65];
        const char *symbol=NULLPTR;
        if(guest_symbol>65535u) {
            if(!read_name(dll,guest_symbol,name))
                return fail(dll,WIN32_DLL_ERROR_BAD_ARGUMENT);
            symbol=name;
        }
        const win32_dll_image_t *m=&dll->images[slot];
        /* Never dereference an absent VM or an out-of-bounds module image
         * when resolving an export from a previously loaded DLL. */
        if(!dll->vm || !dll->vm->guest || !m->info.image_size ||
           handle<dll->vm->base ||
           (uint64_t)(handle-dll->vm->base)+m->info.image_size>
               dll->vm->bytes)
            return fail(dll,WIN32_DLL_ERROR_INVALID_HANDLE);
        uint32_t code=win32_pe_dll_resolve(
            dll->vm->guest+(size_t)(handle-dll->vm->base),
            &m->info,symbol,guest_symbol);
        if(!code)return fail(dll,WIN32_DLL_ERROR_PROC_NOT_FOUND);
        dll->last_error=0;
        return code;
    }
    int builtin=builtin_by_handle(handle);
    if(builtin<0 || (!dll->refs[builtin] && !dll->pinned[builtin] &&
                      !dll->builtin_dep_refs[builtin]))
        return fail(dll,WIN32_DLL_ERROR_INVALID_HANDLE);
    if(guest_symbol<=65535u)
        return fail(dll,WIN32_DLL_ERROR_PROC_NOT_FOUND);
    char name[65];
    if(!read_name(dll,guest_symbol,name))
        return fail(dll,WIN32_DLL_ERROR_BAD_ARGUMENT);
    uint32_t code=builtin_symbol(builtin,name,0u);
    if(!code)return fail(dll,WIN32_DLL_ERROR_PROC_NOT_FOUND);
    dll->last_error=0u;
    return code;
}
uint32_t win32_dll_free(win32_dll_t *dll,uint32_t handle) {
    if(!dll)return 0u;
    int slot=slot_by_handle(dll,handle);
    if(slot>=0) {
        win32_dll_image_t *m=&dll->images[slot];
        if(!m->refs)return fail(dll,WIN32_DLL_ERROR_INVALID_HANDLE);
        --m->refs;
        if(!m->refs && !m->dep_refs)unload_slot(dll,(unsigned)slot);
        dll->last_error=0u;
        return 1u;
    }
    int builtin=builtin_by_handle(handle);
    if(builtin<0 || !dll->refs[builtin])
        return fail(dll,WIN32_DLL_ERROR_INVALID_HANDLE);
    --dll->refs[builtin];
    dll->last_error=0u;
    return 1u;
}
