#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "a64_macho.h"
#include "darwin_objc.h"

static uint8_t guest[A64_GUEST_BYTES];
static uint8_t transcript[512];
static size_t length;
static void output(const uint8_t *bytes, size_t n, void *ctx) {
    (void)ctx;
    assert(n<=sizeof(transcript)-length);
    memcpy(transcript+length,bytes,n);
    length+=n;
}
int main(void) {
    uint8_t file[A64_MACHO_MAX_FILE]={0};
    FILE *fp=fopen("build/apps/darwin.mch","rb");
    assert(fp);
    size_t size=fread(file,1,sizeof(file),fp);
    fclose(fp);
    assert(size==1024);
    a64_report_t r;
    assert(a64_macho_run_darwin(file,size,guest,sizeof(guest),
                                output,NULL,&r));
    assert(r.finished && r.exit_code==0 && r.steps>=20);
    const char *expected=
        "BORDIFY DARWIN: NSString objc_msgSend and BSD write on ARM64\n";
    assert(length==strlen(expected));
    assert(!memcmp(transcript,expected,length));

    /* Darwin syscalls and ObjC shims must be opted in explicitly. */
    assert(!a64_macho_run(file,size,guest,sizeof(guest),output,NULL,&r));
    file[0x200]='X'; /* unknown runtime class */
    assert(!a64_macho_run_darwin(file,size,guest,sizeof(guest),
                                 output,NULL,&r));
    file[0x200]='N';
    file[0x220]='X'; /* unsupported selector */
    assert(!a64_macho_run_darwin(file,size,guest,sizeof(guest),
                                 output,NULL,&r));
    file[0x220]='s';
    file[0x100]=0xff; /* unsupported ARM64 instruction */
    assert(!a64_macho_run_darwin(file,size,guest,sizeof(guest),
                                 output,NULL,&r));
    assert(r.unsupported_opcode!=0);
    assert(!a64_macho_run_darwin(file,10,guest,sizeof(guest),
                                 output,NULL,&r));

    /* Class/method lookup is bounded and unavailable methods fail closed. */
    darwin_objc_state_t state={0};
    uint64_t reg[31]={0};
    memset(guest,0,sizeof(guest));
    memcpy(guest+200,"NSApplication",14);
    memcpy(guest+240,"sharedApplication",18);
    memcpy(guest+280,"isRunning",10);
    reg[0]=A64_GUEST_BASE+200;
    assert(darwin_objc_call(11,reg,guest,sizeof(guest),
                            A64_GUEST_BASE,&state));
    assert(reg[0]==DARWIN_CLASS_NSAPPLICATION);
    reg[1]=A64_GUEST_BASE+240;
    assert(darwin_objc_call(13,reg,guest,sizeof(guest),
                            A64_GUEST_BASE,&state));
    assert(reg[0]==DARWIN_OBJECT_APPLICATION);
    reg[1]=A64_GUEST_BASE+280;
    assert(darwin_objc_call(13,reg,guest,sizeof(guest),
                            A64_GUEST_BASE,&state));
    assert(reg[0]==0);
    reg[0]=0xdeadbeef;
    assert(!darwin_objc_call(13,reg,guest,sizeof(guest),
                              A64_GUEST_BASE,&state));
    puts("ARM64 Mach-O Darwin/ObjC experimental runtime tests: PASS");
    return 0;
}
