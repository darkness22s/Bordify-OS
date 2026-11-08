#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "pe64_vm.h"

static uint8_t memory[PE64_GUEST_BYTES];
static uint8_t setup[PE64_MAX_FILE];
static uint8_t installed[8192];
static size_t installed_size;
static char output_log[512];
static size_t output_len;

static void emit(const uint8_t *p,size_t n,void *ctx) {
    (void)ctx;
    assert(n<=sizeof(output_log)-output_len);
    memcpy(output_log+output_len,p,n);
    output_len+=n;
}
static bool save_app(const uint8_t *data,size_t size,void *ctx) {
    (void)ctx;
    if (size>sizeof(installed) || size<512 || installed_size) return false;
    memcpy(installed,data,size);
    installed_size=size;
    return true;
}
static bool refuse(const uint8_t *data,size_t size,void *ctx) {
    (void)data;(void)size;(void)ctx;
    return false;
}
int main(void) {
    FILE *f=fopen("build/apps/win64setup.exe","rb");
    assert(f);
    size_t size=fread(setup,1,sizeof(setup),f);
    fclose(f);
    assert(size==0xE00);
    pe64_report_t report;
    assert(pe64_vm_run_install(setup,size,memory,sizeof(memory),emit,NULL,
                                save_app,NULL,&report));
    assert(report.finished && report.exit_code==0);
    assert(report.imports==4 && report.installed_bytes==0x800);
    assert(installed_size==0x800 && !memcmp(installed,"MZ",2));

    /* Output of the actual installed x64 program, not the setup loader. */
    assert(pe64_vm_run(installed,installed_size,memory,sizeof(memory),
                       emit,NULL,&report));
    assert(report.finished && report.exit_code==0 && report.imports==3);
    const char *expected=
        "BORDIFY WIN64: PE32+ x64 WriteFile interpreted on x86 kernel\r\n";
    assert(output_len==strlen(expected));
    assert(!memcmp(output_log,expected,output_len));

    installed_size=0;
    /* Different destination paths cannot be installed. */
    setup[0x550]='X'; /* C:\APP64.EXE at RVA 1350/raw 550 */
    assert(!pe64_vm_run_install(setup,size,memory,sizeof(memory),emit,NULL,
                                save_app,NULL,&report));
    assert(installed_size==0);
    setup[0x550]='C';
    /* A read-only / refused commit must abort the VM. */
    assert(!pe64_vm_run_install(setup,size,memory,sizeof(memory),emit,NULL,
                                refuse,NULL,&report));
    assert(installed_size==0);
    assert(!pe64_vm_run(setup,size,memory,sizeof(memory),emit,NULL,&report));
    /* Unknown imports and missing PE header reject before effects. */
    setup[0x4A2]='X';
    assert(!pe64_vm_run_install(setup,size,memory,sizeof(memory),emit,NULL,
                                save_app,NULL,&report));
    setup[0x4A2]='C';
    setup[0]='X';
    assert(!pe64_vm_run_install(setup,size,memory,sizeof(memory),emit,NULL,
                                save_app,NULL,&report));
    puts("Windows x64 installer PE32+ file-commit and installed-app execution: PASS");
    return 0;
}
