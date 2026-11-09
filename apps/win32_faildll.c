/* MinGW PE32 negative fixture. PROCESS_ATTACH failure must make
 * LoadLibraryA return NULL with ERROR_DLL_INIT_FAILED and undo mapping. */
typedef unsigned long DWORD;
typedef int BOOL;
#define WINAPI __attribute__((stdcall))
#define EXPORT __declspec(dllexport)
BOOL WINAPI DllMain(void *module,DWORD reason,void *reserved) {
    (void)module;(void)reserved;
    return reason!=1u; /* reject DLL_PROCESS_ATTACH */
}
EXPORT DWORD WINAPI BordifyNever(DWORD x) {return x+1u;}
