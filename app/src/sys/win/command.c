#include "command.h"

#include "log.h"
#include "strutil.h"

HANDLE cmd_execute(const char *path, const char *const argv[]) {
    STARTUPINFO si;
    PROCESS_INFORMATION pi;
    memset(&si, 0, sizeof(si));
    si.cb = sizeof(si);

    // Windows command-line parsing is WTF:
    // <http://daviddeley.com/autohotkey/parameters/parameters.htm#WINPASS>
    // only make it work for this very specific program
    // (don't handle escaping nor quotes)
    char cmd[256];
    size_t ret = xstrjoin(cmd, argv, ' ', sizeof(cmd));
    if (ret >= sizeof(cmd)) {
        LOGE("Command too long (%" PRIsizet " chars)", sizeof(cmd) - 1);
        return NULL;
    }

    if (!CreateProcess(NULL, cmd, NULL, NULL, FALSE, CREATE_NO_WINDOW, NULL, NULL, &si, &pi)) {
        return NULL;
    }

    return pi.hProcess;
}

HANDLE cmd_execute_redirect(const char *path, const char *const argv[],
                            HANDLE *pipe_stdin, HANDLE *pipe_stdout, HANDLE *pipe_stderr) {
    LOGW("cmd_execute_redirect() not implemented yet for Windows");
    return NULL;
}

SDL_bool cmd_terminate(HANDLE handle) {
    return TerminateProcess(handle, 1) && CloseHandle(handle);
}

SDL_bool cmd_simple_wait(HANDLE handle, DWORD *exit_code) {
    DWORD code;
    if (WaitForSingleObject(handle, INFINITE) != WAIT_OBJECT_0 || !GetExitCodeProcess(handle, &code)) {
        // cannot wait or retrieve the exit code
        code = -1; // max value, it's unsigned
    }
    if (exit_code) {
        *exit_code = code;
    }
    return !code;
}
