/* Real PE32 DLL importing a second MinGW DLL through its actual IAT. */
typedef unsigned long DWORD;
#define WINAPI __attribute__((stdcall))
#define EXPORT __declspec(dllexport)
#define IMPORT __declspec(dllimport)
IMPORT DWORD WINAPI BordifyAdd(DWORD number);
EXPORT DWORD WINAPI BordifyChain(DWORD number) {
    return BordifyAdd(number)+5u;
}
