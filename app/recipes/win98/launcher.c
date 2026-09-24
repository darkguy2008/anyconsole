#include <windows.h>

#define HOOK_DRIVE "E:\\"
#define START_HOOK HOOK_DRIVE "START.BAT"
#define STOP_HOOK HOOK_DRIVE "STOP.BAT"
#define AUTORUN_FILE "AUTORUN.INF"
#define QUIT_PORT "COM1"
#define OFF_ARGUMENT "/off"
#define COMMAND_MAX (2 * MAX_PATH)
#define PORT_RETRY_MILLISECONDS 100
#define PORT_BUSY_MILLISECONDS 60000

static LONG stopped;

static void run(char *command, const char *directory, BOOL wait) {
    STARTUPINFO startup = {.cb = sizeof startup};
    PROCESS_INFORMATION process;
    if (!CreateProcess(NULL, command, NULL, NULL, FALSE, 0, NULL, directory, &startup, &process)) return;
    if (wait) WaitForSingleObject(process.hProcess, INFINITE);
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
}

static BOOL run_hook(const char *hook, BOOL wait) {
    char command[COMMAND_MAX];
    if (GetFileAttributes(hook) == INVALID_FILE_ATTRIBUTES) return FALSE;
    wsprintf(command, "COMMAND.COM /C %s", hook);
    run(command, NULL, wait);
    return TRUE;
}

static void autorun(void) {
    char root[] = "C:\\", inf[MAX_PATH], open[MAX_PATH], command[COMMAND_MAX];
    for (; root[0] <= 'Z'; root[0]++) {
        if (GetDriveType(root) != DRIVE_CDROM) continue;
        wsprintf(inf, "%s" AUTORUN_FILE, root);
        if (!GetPrivateProfileString("autorun", "open", "", open, sizeof open, inf)) continue;
        wsprintf(command, "%s%s", root, open);
        run(command, root, FALSE);
        return;
    }
}

static DWORD WINAPI stop(LPVOID unused) {
    (void)unused;
    if (!InterlockedExchange(&stopped, TRUE)) run_hook(STOP_HOOK, TRUE);
    return 0;
}

static void power_off(void) {
    stop(NULL);
    ExitWindowsEx(EWX_SHUTDOWN | EWX_POWEROFF | EWX_FORCE, 0);
}

static BOOL listen_to(HANDLE port) {
    COMMTIMEOUTS blocking = {0};
    DCB state = {.DCBlength = sizeof state};
    if (port == INVALID_HANDLE_VALUE || !GetCommState(port, &state)) return FALSE;
    state.fOutxCtsFlow = state.fOutxDsrFlow = state.fDsrSensitivity = FALSE;
    return SetCommState(port, &state) && SetCommTimeouts(port, &blocking);
}

static HANDLE open_quit_port(void) {
    for (int waited = 0; waited < PORT_BUSY_MILLISECONDS; waited += PORT_RETRY_MILLISECONDS) {
        HANDLE port = CreateFile(QUIT_PORT, GENERIC_READ, 0, NULL, OPEN_EXISTING, 0, NULL);
        if (port != INVALID_HANDLE_VALUE || GetLastError() != ERROR_ACCESS_DENIED) return port;
        Sleep(PORT_RETRY_MILLISECONDS);
    }
    return INVALID_HANDLE_VALUE;
}

static DWORD WINAPI await_quit(LPVOID unused) {
    HANDLE port = open_quit_port();
    char command;
    DWORD count;
    (void)unused;
    if (listen_to(port) && ReadFile(port, &command, sizeof command, &count, NULL) && count) power_off();
    return 0;
}

static void pump_messages_until(HANDLE handle) {
    MSG message;
    while (MsgWaitForMultipleObjects(1, &handle, FALSE, INFINITE, QS_ALLINPUT) == WAIT_OBJECT_0 + 1)
        while (PeekMessage(&message, NULL, 0, 0, PM_REMOVE)) DispatchMessage(&message);
}

static LRESULT CALLBACK on_message(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
    DWORD id;
    if (message != WM_QUERYENDSESSION) return DefWindowProc(window, message, wparam, lparam);
    HANDLE thread = CreateThread(NULL, 0, stop, NULL, 0, &id);
    if (thread) pump_messages_until(thread);
    CloseHandle(thread);
    return TRUE;
}

static BOOL asked_to_power_off(void) {
    const char *command_line = GetCommandLine();
    int length = lstrlen(command_line), argument = lstrlen(OFF_ARGUMENT);
    return length >= argument && !lstrcmpi(command_line + length - argument, OFF_ARGUMENT);
}

void start(void) {
    WNDCLASS class = {.lpfnWndProc = on_message, .hInstance = GetModuleHandle(NULL), .lpszClassName = "anyconsole"};
    MSG message;
    DWORD id;
    if (asked_to_power_off()) {
        power_off();
        ExitProcess(0);
    }
    if (!run_hook(START_HOOK, FALSE)) autorun();
    RegisterClass(&class);
    CreateWindow(class.lpszClassName, "", 0, 0, 0, 0, 0, NULL, NULL, class.hInstance, NULL);
    CloseHandle(CreateThread(NULL, 0, await_quit, NULL, 0, &id));
    while (GetMessage(&message, NULL, 0, 0) > 0) DispatchMessage(&message);
    ExitProcess(0);
}
