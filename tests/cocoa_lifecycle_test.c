#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "a64_macho.h"
#include "darwin_objc.h"

static uint8_t guest[A64_GUEST_BYTES];
static uint8_t message[256];
static size_t message_len;
static void emit(const uint8_t *p,size_t n,void *ctx) {
    (void)ctx;
    assert(n<=sizeof(message)-message_len);
    memcpy(message+message_len,p,n);
    message_len+=n;
}
static size_t substring(const uint8_t *buf,size_t total,const char *text) {
    size_t n=strlen(text);
    for(size_t i=0;i+n<=total;++i)
        if(!memcmp(buf+i,text,n))return i;
    return total;
}
static void w32(uint8_t *ptr,uint32_t v) {
    for(unsigned j=0;j<4;++j)ptr[j]=(uint8_t)(v>>(8u*j));
}
int main(void) {
    uint8_t mach[A64_MACHO_MAX_FILE]={0};
    FILE *f=fopen("build/apps/cocoa.mch","rb");
    assert(f);
    size_t size=fread(mach,1,sizeof(mach),f);
    fclose(f);
    assert(size==0x800);
    a64_report_t r;
    assert(a64_macho_run_darwin(mach,size,guest,sizeof(guest),emit,NULL,&r));
    assert(r.finished && r.exit_code==0 && r.steps>90);
    assert(r.objc_linked_calls==25);
    assert(r.cocoa_window_created && r.cocoa_window_visible &&
           r.cocoa_window_title_set);
    assert(r.cocoa_application_started && r.cocoa_application_terminated);
    static const char expected[]=
        "BORDIFY COCOA: NSString NSWindow NSApplication lifecycle verified\n";
    assert(message_len==sizeof(expected)-1);
    assert(!memcmp(message,expected,sizeof(expected)-1));

    /* A Bordify-specific LC_NOTE is never treated as ordinary Mach-O. */
    assert(!a64_macho_run(mach,size,guest,sizeof(guest),emit,NULL,&r));

    /* Must reject altered metadata owner, stub instruction or selector. */
    mach[136]='X';
    assert(!a64_macho_run_darwin(mach,size,guest,sizeof(guest),emit,NULL,&r));
    mach[136]='B';
    uint8_t old_stub=mach[0x604];
    mach[0x604]=0;
    assert(!a64_macho_run_darwin(mach,size,guest,sizeof(guest),emit,NULL,&r));
    mach[0x604]=old_stub;

    size_t where=substring(mach,size,"NSWindow");
    assert(where<size);
    mach[where]='X';
    assert(!a64_macho_run_darwin(mach,size,guest,sizeof(guest),emit,NULL,&r));
    mach[where]='N';

    /* Infinite ARM64 branches obey the instruction budget. */
    uint8_t saved[4];memcpy(saved,mach+0x100,4);
    w32(mach+0x100,0x14000000u);
    assert(!a64_macho_run_darwin(mach,size,guest,sizeof(guest),emit,NULL,&r));
    assert(r.steps==A64_MAX_STEPS);
    memcpy(mach+0x100,saved,4);

    /* Direct ObjC API negative tests ensure disallowed classes and
     * invalid window lifetimes fail closed independently of Mach-O. */
    darwin_objc_state_t st={0};
    uint64_t regs[31]={0};
    memset(guest,0,sizeof(guest));
    memcpy(guest+160,"NSWindow",9);
    memcpy(guest+192,"makeKeyAndOrderFront:",22);
    memcpy(guest+224,"setTitle:",10);
    regs[0]=A64_GUEST_BASE+160;
    assert(darwin_objc_call(11,regs,guest,sizeof(guest),A64_GUEST_BASE,&st));
    assert(regs[0]==DARWIN_CLASS_NSWINDOW);
    regs[1]=A64_GUEST_BASE+192;
    assert(!darwin_objc_call(13,regs,guest,sizeof(guest),A64_GUEST_BASE,&st));
    regs[1]=A64_GUEST_BASE+224;
    assert(!darwin_objc_call(13,regs,guest,sizeof(guest),A64_GUEST_BASE,&st));
    regs[0]=0x0123456789ULL;
    assert(!darwin_objc_call(13,regs,guest,sizeof(guest),A64_GUEST_BASE,&st));

    puts("Clang ARM64 ObjC ABI imports, NSString and AppKit lifecycle: PASS");
    return 0;
}
