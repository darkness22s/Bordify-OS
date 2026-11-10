/*
 * Compiled by a regular MinGW i686 C compiler. Imports real msvcrt.dll and
 * kernel32.dll names through import descriptors, with no generated opcodes.
 *
 * Deliberately CRT-startup-free: this acceptance test verifies the genuine
 * x86 cdecl MSVCRT ABI in a ring-3 process; it does not simulate full CRT
 * initialization or claim that arbitrary Windows applications work.
 */
#include <windows.h>
#include <stdlib.h>
#include <string.h>

__attribute__((noreturn)) void mainCRTStartup(void) {
    static const char success[]=
        "BORDIFY REAL MSVCRT PE32: two DLL imports, strings and heap verified\r\n";
    HMODULE imported_crt=GetModuleHandleA("msvcrt.dll");
    if(!imported_crt ||
       imported_crt!=GetModuleHandleW(L"MSVCRT.DLL") ||
       !GetProcAddress(imported_crt,"strlen")) ExitProcess(71u);
    char a[80],b[80],c[80];
    const char text[]="windows-runtime";
    if(strlen(text)!=15u) ExitProcess(60u);
    memset(a,0,sizeof(a));
    memcpy(a,text,16u);
    if(strcmp(a,text)!=0 || strncmp(a,"window",6u)!=0 ||
       strncmp(a,"WIN",3u)==0) ExitProcess(61u);

    memset(b,'X',sizeof(b));
    memcpy(b,a,16u);
    if(memcmp(a,b,16u)!=0) ExitProcess(62u);
    memmove(b+3,b,16u);
    if(memcmp(b+3,a,16u)!=0) ExitProcess(63u);
    memset(c,0,sizeof(c));
    memcpy(c,b+3,16u);
    if(strcmp(c,text)!=0) ExitProcess(64u);

    /* Resolve the newly implemented cdecl export dynamically so the guest
     * must execute Bordify's MSVCRT bridge rather than a compiler builtin.
     * A missing character returns NULL; searching for NUL returns the end. */
    FARPROC proc=GetProcAddress(imported_crt,"strchr");
    if(!proc) ExitProcess(72u);
    char *(__cdecl *guest_strchr)(const char *,int)=
        (char *(__cdecl *)(const char *,int))proc;
    if(guest_strchr(text,'r')!=text+8 ||
       guest_strchr(text,'!')!=NULL ||
       guest_strchr(text,0)!=text+15 ||
       guest_strchr(text,'w')!=text ||
       guest_strchr(text,'e')!=text+5) ExitProcess(73u);

    char *allocated=(char *)malloc(36u);
    if(!allocated) ExitProcess(65u);
    memset(allocated,'B',35u);
    allocated[35]=0;
    if(strlen(allocated)!=35u) ExitProcess(66u);

    char *grown=(char *)realloc(allocated,300u);
    if(!grown || memcmp(grown,allocated,0u)!=0 ||
       strlen(grown)!=35u) ExitProcess(67u);
    char *zero=(char *)calloc(4u,32u);
    if(!zero) ExitProcess(68u);
    for(unsigned i=0;i<128u;++i)
        if(zero[i]!=0) ExitProcess(69u);
    free(grown);
    free(zero);

    DWORD written=0;
    HANDLE output=GetStdHandle(STD_OUTPUT_HANDLE);
    if(!WriteFile(output,success,(DWORD)sizeof(success)-1u,&written,0) ||
       written!=(DWORD)sizeof(success)-1u) ExitProcess(70u);
    ExitProcess(0);
}
