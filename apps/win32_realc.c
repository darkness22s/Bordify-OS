/*
 * Independently compiled Windows PE32 program using a normal MinGW toolchain.
 * This is not generated opcode-by-opcode. It is intentionally CRT-free so the
 * current small kernel32 subset can execute a real compiler's control flow.
 *
 * Expected: Windows executable prefers 0x00400000; Bordify rebases it to
 * 0x40000000, resolves real kernel32.dll imports and enters code in ring 3.
 * No GUI, browser, threads, Win64, CRT or third-party-app claim is made.
 */
#include <windows.h>

static int includes_ascii(const char *text, const char *needle) {
    if (!text || !needle) return 0;
    for (const char *p=text; *p; ++p) {
        const char *a=p,*b=needle;
        while (*a && *b && *a==*b) {++a;++b;}
        if (!*b) return 1;
    }
    return 0;
}
static int includes_wide(const WCHAR *text,const WCHAR *needle) {
    if (!text || !needle) return 0;
    for (const WCHAR *p=text; *p; ++p) {
        const WCHAR *a=p,*b=needle;
        while (*a && *b && *a==*b) {++a;++b;}
        if (!*b) return 1;
    }
    return 0;
}
__attribute__((noreturn)) void mainCRTStartup(void) {
    static const char message[]=
        "BORDIFY REAL MINGW PE32: kernel32 process, timer, commandline and WriteFile OK\r\n";
    static const WCHAR wide_name[]={
        'W','I','N','R','E','A','L','.','E','X','E',0
    };
    DWORD first=GetTickCount();
    DWORD second=GetTickCount();
    HMODULE module_a=GetModuleHandleA(0);
    HMODULE module_w=GetModuleHandleW(0);
    if (GetCurrentProcessId()!=1u || GetCurrentThreadId()!=1u ||
        (DWORD)(second-first)>1000u ||
        (DWORD)(ULONG_PTR)module_a!=0x40000000u ||
        (DWORD)(ULONG_PTR)module_w!=0x40000000u ||
        !includes_ascii(GetCommandLineA(),"WINREAL.EXE") ||
        !includes_wide(GetCommandLineW(),wide_name) ||
        IsDebuggerPresent()) {
        ExitProcess(47u);
    }
    SetLastError(0x1234u);
    if (GetLastError()!=0x1234u) ExitProcess(48u);
    /* Test an ordinary compiler-emitted call to the actual kernel32 ABI.
     * Allocations must be ring-3 private, 4-KiB aligned, zero-initialized,
     * and safely reusable only after VirtualFree(MEM_RELEASE). */
    char *allocation=(char *)VirtualAlloc(0,8192u,
                          MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);
    if (!allocation || ((ULONG_PTR)allocation&4095u)) ExitProcess(50u);
    if (allocation[0]!=0 || allocation[4096]!=0) ExitProcess(51u);
    allocation[0]='B';
    allocation[4096]='X';
    if (allocation[0]!='B' || allocation[4096]!='X' ||
        !VirtualFree(allocation,0,MEM_RELEASE)) ExitProcess(52u);
    if (VirtualFree(allocation,0,MEM_RELEASE)) ExitProcess(53u);
    if (GetLastError()!=ERROR_INVALID_PARAMETER) ExitProcess(54u);
    char *another=(char *)VirtualAlloc(0,4096u,
                          MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);
    if (another!=allocation || another[0]!=0 ||
        !VirtualFree(another,0,MEM_RELEASE)) ExitProcess(55u);

    HANDLE output=GetStdHandle(STD_OUTPUT_HANDLE);
    DWORD written=0;
    DWORD length=(DWORD)(sizeof(message)-1u);
    if (!WriteFile(output,message,length,&written,0) ||
        written!=length) ExitProcess(49u);
    ExitProcess(0u);
}
