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
    HANDLE output=GetStdHandle(STD_OUTPUT_HANDLE);
    DWORD written=0;
    DWORD length=(DWORD)(sizeof(message)-1u);
    if (!WriteFile(output,message,length,&written,0) ||
        written!=length) ExitProcess(49u);
    ExitProcess(0u);
}
