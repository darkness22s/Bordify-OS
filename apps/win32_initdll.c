/* Real MinGW i386 DLL with a stdcall PE entrypoint, no CRT.
 * The exported function reports a wrong result unless process attach ran. */
typedef unsigned long DWORD;
typedef int BOOL;
#define WINAPI __attribute__((stdcall))
#define EXPORT __declspec(dllexport)
static volatile DWORD initialized=0u;
BOOL WINAPI DllMain(void *module,DWORD reason,void *reserved) {
    (void)module;(void)reserved;
    if(reason==1u)initialized=0x44u; /* DLL_PROCESS_ATTACH */
    if(reason==0u)initialized=0u;     /* DLL_PROCESS_DETACH */
    return 1;
}
EXPORT DWORD WINAPI BordifyInitState(DWORD value) {
    return initialized+value;
}
