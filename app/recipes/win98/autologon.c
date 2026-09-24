#include <windows.h>

#define USER "ANYCONSOLE"

typedef DWORD(WINAPI *create_cache)(HANDLE *cache, LPCSTR user, LPCSTR password);
typedef DWORD(WINAPI *close_cache)(HANDLE cache, DWORD commit);

void start(void) {
    HMODULE library = LoadLibrary("MSPWL32.DLL");
    create_cache create = (create_cache)(void (*)(void))GetProcAddress(library, "CreatePasswordCache");
    close_cache close = (close_cache)(void (*)(void))GetProcAddress(library, "ClosePasswordCache");
    HANDLE cache;
    ExitProcess(!(create && close && !create(&cache, USER, "") && !close(cache, TRUE)));
}
