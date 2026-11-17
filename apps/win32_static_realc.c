#include <windows.h>
/* No explicit LoadLibrary: the EXE's real PE import directory names
 * CHAIN.DLL, which itself imports BORDLL.DLL. Runs only in ring 3. */
__declspec(dllimport) DWORD WINAPI BordifyChain(DWORD input);
__declspec(dllimport) DWORD WINAPI BordifyInitState(DWORD input);
__attribute__((noreturn)) void mainCRTStartup(void) {
    static const char message[]=
        "BORDIFY STATIC EXE IMPORT: transitive DLL call passed\r\n";
    if(BordifyChain(7u)!=135u)ExitProcess(101u);
    if(BordifyInitState(7u)!=0x4bu)ExitProcess(105u);
    if(!GetModuleHandleA("init.dll"))ExitProcess(106u);
    HMODULE chain=GetModuleHandleA("chain.dll");
    HMODULE original=GetModuleHandleW(L"bordll.dll");
    if(!chain || !original || chain==original)ExitProcess(102u);
    /* GetModuleHandle does not create an explicit LoadLibrary ref. */
    if(FreeLibrary(original) || GetLastError()!=ERROR_INVALID_HANDLE)
        ExitProcess(103u);
    DWORD written=0;
    if(!WriteFile(GetStdHandle(STD_OUTPUT_HANDLE),message,
        (DWORD)sizeof(message)-1u,&written,0) ||
       written!=(DWORD)sizeof(message)-1u)ExitProcess(104u);
    ExitProcess(0u);
}
