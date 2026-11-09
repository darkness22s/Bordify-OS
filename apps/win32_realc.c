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

    /* Imported kernel32 is present before an explicit LoadLibrary call. */
    HMODULE imported_kernel=GetModuleHandleA("KERNEL32.DLL");
    if(!imported_kernel ||
       GetModuleHandleW(L"kernel32.dll")!=imported_kernel ||
       !GetProcAddress(imported_kernel,"GetTickCount")) ExitProcess(71u);
    if(GetModuleHandleA("msvcrt.dll") ||
       GetLastError()!=ERROR_MOD_NOT_FOUND) ExitProcess(72u);
    HMODULE wide_crt=LoadLibraryW(L"MsVcRt.DlL");
    if(!wide_crt || GetModuleHandleW(L"msvcrt.dll")!=wide_crt ||
       !GetProcAddress(wide_crt,"strlen")) ExitProcess(73u);
    if(!FreeLibrary(wide_crt) ||
       GetModuleHandleA("msvcrt.dll")) ExitProcess(74u);
    if(GetLastError()!=ERROR_MOD_NOT_FOUND) ExitProcess(75u);

    /* Dynamically resolve exports from virtual built-in DLL modules. This
     * tests the real MinGW stdcall ABI without pretending to map external
     * DLL images or run Windows DllMain/TLS initializers. */
    HMODULE kernel32=LoadLibraryA("kernel32.dll");
    if (!kernel32) ExitProcess(56u);
    FARPROC tick_symbol=GetProcAddress(kernel32,"GetTickCount");
    if (!tick_symbol) ExitProcess(57u);
    DWORD (WINAPI *tick_dynamic)(void)=(DWORD (WINAPI *)(void))tick_symbol;
    if ((DWORD)(tick_dynamic()-GetTickCount())>1000u) ExitProcess(58u);
    if (GetProcAddress(kernel32,"BordifyMissingExport")!=0)
        ExitProcess(59u);
    if (GetLastError()!=ERROR_PROC_NOT_FOUND) ExitProcess(60u);
    HMODULE crt=LoadLibraryA("msvcrt.dll");
    if (!crt) ExitProcess(61u);
    FARPROC strlen_symbol=GetProcAddress(crt,"strlen");
    if (!strlen_symbol) ExitProcess(62u);
    unsigned (__cdecl *strlen_dynamic)(const char *)=
        (unsigned (__cdecl *)(const char *))strlen_symbol;
    if (strlen_dynamic("Bordify")!=7u) ExitProcess(63u);
    if (!FreeLibrary(crt) || !FreeLibrary(kernel32)) ExitProcess(64u);
    if (FreeLibrary(kernel32)) ExitProcess(65u);
    if (GetLastError()!=ERROR_INVALID_HANDLE) ExitProcess(66u);

    /* Real MinGW-built PE DLL: its exported native x86 machine code and
     * HIGHLOW-relocated data execute at CPL3 inside Bordify's VM arena. */
    HMODULE real_dll=LoadLibraryA("BORDLL.DLL");
    if(!real_dll)ExitProcess(76u);
    if(GetModuleHandleW(L"bordll.dll")!=real_dll)ExitProcess(77u);
    FARPROC export_fn=GetProcAddress(real_dll,"BordifyAdd");
    if(!export_fn)ExitProcess(78u);
    DWORD (WINAPI *add_dynamic)(DWORD)=(DWORD (WINAPI *)(DWORD))export_fn;
    if(add_dynamic(7u)!=130u)ExitProcess(79u);
    /* A module-owned allocation must never be destroyed by VirtualFree. */
    if(VirtualFree(real_dll,0,MEM_RELEASE) ||
       GetLastError()!=ERROR_INVALID_PARAMETER)ExitProcess(80u);
    HMODULE same=LoadLibraryW(L"bordll.dll");
    if(same!=real_dll || !FreeLibrary(same) ||
       !GetProcAddress(real_dll,"BordifyAdd"))ExitProcess(81u);
    if(!FreeLibrary(real_dll) ||
       GetModuleHandleA("bordll.dll"))ExitProcess(82u);
    if(GetLastError()!=ERROR_MOD_NOT_FOUND)ExitProcess(83u);
    if(FreeLibrary(real_dll) ||
       GetLastError()!=ERROR_INVALID_HANDLE)ExitProcess(84u);
    static const char dll_message[]=
        "BORDIFY MAPPED PE32 DLL: native MinGW export executed in ring 3\\r\\n";
    DWORD dll_written=0;
    HANDLE dll_stdout=GetStdHandle(STD_OUTPUT_HANDLE);
    if(!WriteFile(dll_stdout,dll_message,
                  (DWORD)sizeof(dll_message)-1u,&dll_written,0) ||
       dll_written!=(DWORD)sizeof(dll_message)-1u)ExitProcess(85u);

    HANDLE output=GetStdHandle(STD_OUTPUT_HANDLE);
    DWORD written=0;
    DWORD length=(DWORD)(sizeof(message)-1u);
    if (!WriteFile(output,message,length,&written,0) ||
        written!=length) ExitProcess(49u);
    ExitProcess(0u);
}
