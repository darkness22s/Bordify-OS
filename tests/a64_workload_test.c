#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "a64_macho.h"

/* Actual clang -target arm64-apple-macos11 machine code: no handcrafted
 * opcodes in this fixture. Verifies real arithmetic, stack, calls, branches.
 */
static uint8_t guest[A64_GUEST_BYTES];
static char transcript[512];
static size_t written;
static void on_output(const uint8_t *p,size_t n,void *unused) {
    (void)unused;
    assert(n<=sizeof(transcript)-written);
    memcpy(transcript+written,p,n);
    written+=n;
}
static void set32(uint8_t *buf,unsigned value) {
    for(int i=0;i<4;++i)buf[i]=(uint8_t)(value>>(i*8));
}
int main(void) {
    FILE *f=fopen("build/apps/armwork.mch","rb");
    assert(f);
    uint8_t file[A64_MACHO_MAX_FILE];
    size_t n=fread(file,1,sizeof(file),f);
    fclose(f);
    assert(n>=512 && n<=4096);
    a64_report_t r;
    assert(a64_macho_run_darwin(file,n,guest,sizeof(guest),on_output,NULL,&r));
    assert(r.finished && r.exit_code==0 && r.steps>45);
    static const char expected[]=
        "BORDIFY ARM64 EXTENDED: verified arithmetic branches and stack\n";
    assert(written==sizeof(expected)-1);
    assert(!memcmp(transcript,expected,sizeof(expected)-1));

    /* Darwin BSD calls require opt-in; other mode never runs those calls. */
    assert(!a64_macho_run(file,n,guest,sizeof(guest),on_output,NULL,&r));
    /* A branch to itself must stop at the instruction budget. */
    uint8_t original[4];
    memcpy(original,file+0x110,4);
    set32(file+0x110,0x14000000u); /* B #0 */
    assert(!a64_macho_run_darwin(file,n,guest,sizeof(guest),on_output,NULL,&r));
    assert(!r.finished && r.steps==A64_MAX_STEPS);
    memcpy(file+0x110,original,4);

    /* Deliberately dereference a null base register via LDR X11,[X0]. */
    memcpy(original,file+0x12c,4);
    set32(file+0x12c,0xf940000bu);
    assert(!a64_macho_run_darwin(file,n,guest,sizeof(guest),on_output,NULL,&r));
    assert(!r.finished);
    memcpy(file+0x12c,original,4);

    /* Malformed guest pair-stack operation with an extreme unsigned base
     * must fail safely without signed conversion overflow. */
    memcpy(original,file+0x100,4);
    set32(file+0x100,0x92800000u); /* MOVN X0,#0 -> UINT64_MAX */
    memcpy(file+0x104,4);
    set32(file+0x104,0xa9807c01u); /* STP X1,XZR,[X0,#-16]! */
    assert(!a64_macho_run_darwin(file,n,guest,sizeof(guest),on_output,NULL,&r));
    memcpy(file+0x100,original,4);
    /* The next instruction is restored from the compiler fixture below. */
    /* Reject unsupported instruction in the function prologue. */
    memcpy(original,file+0x100,4);
    set32(file+0x100,0xffffffffu);
    assert(!a64_macho_run_darwin(file,n,guest,sizeof(guest),on_output,NULL,&r));
    assert(r.unsupported_opcode==0xffffffffu);
    memcpy(file+0x100,original,4);

    set32(file+4,0x01000007u); /* Mach-O x86_64 => reject */
    assert(!a64_macho_run_darwin(file,n,guest,sizeof(guest),on_output,NULL,&r));
    puts("Independently assembled ARM64 Mach-O arithmetic/branch/stack tests: PASS");
    return 0;
}
