/* Independent Win64 PE executable; CI must actually execute it under Wine. */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
int main(void) {
    static const char msg[] = "BORDIFY_LINUX_WINE_PE64_OK\n";
    DWORD written = 0;
    HANDLE out = GetStdHandle(STD_OUTPUT_HANDLE);
    if (!out || out == INVALID_HANDLE_VALUE) return 7;
    if (!WriteFile(out, msg, sizeof(msg) - 1, &written, NULL)) return 8;
    return written == sizeof(msg) - 1 ? 0 : 9;
}
