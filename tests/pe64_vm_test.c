#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "pe64_vm.h"

static uint8_t memory[PE64_GUEST_BYTES];
static char transcript[512];
static size_t written;
static void emit(const uint8_t *p, size_t len, void *ctx) {
    (void)ctx;
    assert(len <= sizeof(transcript)-written);
    memcpy(transcript+written,p,len);
    written+=len;
}
static void put16(uint8_t *p, unsigned x) {
    p[0]=(uint8_t)x; p[1]=(uint8_t)(x>>8);
}
int main(void) {
    FILE *f=fopen("build/apps/win64demo.exe","rb");
    assert(f);
    static uint8_t binary[PE64_MAX_FILE];
    size_t size=fread(binary,1,sizeof(binary),f);
    fclose(f);
    assert(size==0x800);
    pe64_report_t info;
    assert(pe64_vm_run(binary,size,memory,sizeof(memory),emit,NULL,&info));
    assert(info.finished && info.exit_code==0 && info.imports==3);
    const char *expected=
        "BORDIFY WIN64: PE32+ x64 WriteFile interpreted on x86 kernel\r\n";
    assert(written==strlen(expected));
    assert(!memcmp(transcript,expected,written));

    /* Verify the CPU architecture and OptionalHeader64 are essential. */
    put16(binary+0x84,0x014c);
    assert(!pe64_vm_run(binary,size,memory,sizeof(memory),emit,NULL,&info));
    put16(binary+0x84,0x8664);
    put16(binary+0x98,0x010b);
    assert(!pe64_vm_run(binary,size,memory,sizeof(memory),emit,NULL,&info));
    put16(binary+0x98,0x020b);
    assert(!pe64_vm_run(binary,32,memory,sizeof(memory),emit,NULL,&info));
    assert(!pe64_vm_run(NULL,size,memory,sizeof(memory),emit,NULL,&info));

    /* DLL and API names are checked, not guessed at runtime. */
    binary[0x440]='n'; /* kernel32.dll at raw 0x440 */
    assert(!pe64_vm_run(binary,size,memory,sizeof(memory),emit,NULL,&info));
    binary[0x440]='k';
    binary[0x4a2]='X'; /* WriteFile is at RVA 12a0 + hint 2 */
    assert(!pe64_vm_run(binary,size,memory,sizeof(memory),emit,NULL,&info));
    binary[0x4a2]='W';
    binary[0x200]=0x0f;binary[0x201]=0x0b;
    assert(!pe64_vm_run(binary,size,memory,sizeof(memory),emit,NULL,&info));
    assert(info.rejected_opcode!=0);
    assert(!pe64_vm_run(binary,size,memory,sizeof(memory)-1,emit,NULL,&info));
    puts("PE32+ AMD64 interpreter & API import tests: PASS");
    return 0;
}
