/* Real MinGW PE32 DLL fixture, without any CRT or DLL imports.
 * Uses an address relocation to reach the module-local volatile data. */
typedef unsigned long DWORD;
#define WINAPI __attribute__((stdcall))
#define EXPORT __declspec(dllexport)
static volatile DWORD addend=123u;
EXPORT DWORD WINAPI BordifyAdd(DWORD value) {
    return value+addend;
}
