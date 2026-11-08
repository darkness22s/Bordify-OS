#include <assert.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include "a64_macho.h"

/* Independent clang -target arm64-apple-macos11 -O1 C compiler output.
 * Enforces real loop, stack loads/stores, CMP, conditional branch and CSEL.
 */
static uint8_t guest[A64_GUEST_BYTES];
static unsigned outputs=0;
static void no_output(const uint8_t *p,size_t n,void *ctx) {
    (void)p;(void)n;(void)ctx;
    ++outputs;
}
static void put32(uint8_t *p,uint32_t value) {
    for (unsigned j=0;j<4;++j) p[j]=(uint8_t)(value>>(8u*j));
}
int main(void) {
    FILE *f=fopen("build/apps/armcalc.mch","rb");
    assert(f);
    uint8_t file[A64_MACHO_MAX_FILE];
    size_t n=fread(file,1,sizeof(file),f);
    fclose(f);
    assert(n>=512 && n<=A64_MACHO_MAX_FILE);
    a64_report_t r;
    assert(a64_macho_run_darwin(file,n,guest,sizeof(guest),
                                 no_output,NULL,&r));
    assert(r.finished && r.exit_code==0 && r.steps>45);
    assert(outputs==0);

    /* Change just CMP X8,#55 to CMP X8,#56 inside compiled code.
     * Correct emulation must now select the failure code via CSEL. */
    uint8_t saved[4];
    memcpy(saved,file+0x12c,4);
    put32(file+0x12c,0xf100e11fu);
    assert(a64_macho_run_darwin(file,n,guest,sizeof(guest),
                                 no_output,NULL,&r));
    assert(r.finished && r.exit_code==99);
    memcpy(file+0x12c,saved,4);

    /* A 32-bit x86 Mach-O cannot enter the ARM64 interpreter. */
    put32(file+4,0x01000007u);
    assert(!a64_macho_run_darwin(file,n,guest,sizeof(guest),
                                  no_output,NULL,&r));
    puts("Independently clang-compiled Apple ARM64 C program tests: PASS");
    return 0;
}
