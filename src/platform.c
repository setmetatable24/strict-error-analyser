#if defined(_WIN32)
#define _CRT_SECURE_NO_WARNINGS 1
#include <windows.h>
#include <direct.h>
#include <io.h>
#include <urlmon.h>
#pragma comment(lib, "urlmon.lib")
#else
#define _POSIX_C_SOURCE 200809L
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#endif

#include "platform.h"
#include "common.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

bool file_exists(const char *path) {
    if (!path || !*path) return false;
#if defined(_WIN32)
    DWORD attrs = GetFileAttributesA(path);
    return (attrs != INVALID_FILE_ATTRIBUTES && !(attrs & FILE_ATTRIBUTE_DIRECTORY));
#else
    struct stat st;
    return (stat(path, &st) == 0 && S_ISREG(st.st_mode));
#endif
}

bool dir_exists(const char *path) {
    if (!path || !*path) return false;
#if defined(_WIN32)
    DWORD attrs = GetFileAttributesA(path);
    return (attrs != INVALID_FILE_ATTRIBUTES && (attrs & FILE_ATTRIBUTE_DIRECTORY));
#else
    struct stat st;
    return (stat(path, &st) == 0 && S_ISDIR(st.st_mode));
#endif
}

static bool make_dir_single(const char *path) {
#if defined(_WIN32)
    if (_mkdir(path) == 0) return true;
    return (GetLastError() == ERROR_ALREADY_EXISTS);
#else
    if (mkdir(path, 0755) == 0) return true;
    return true;
#endif
}

bool make_dir_recursive(const char *path) {
    if (!path || !*path) return false;
    char temp[1024];
    size_t len = strlen(path);
    if (len >= sizeof(temp)) return false;
    memcpy(temp, path, len + 1);

    for (size_t i = 1; i < len; ++i) {
        if (temp[i] == '/' || temp[i] == '\\') {
            char prev = temp[i];
            temp[i] = '\0';
            if (strlen(temp) > 2 || (temp[1] != ':' && strlen(temp) > 0)) {
                make_dir_single(temp);
            }
            temp[i] = prev;
        }
    }
    return make_dir_single(temp);
}

void remove_dir_recursive(const char *path) {
    if (!path || !*path) return;
#if defined(_WIN32)
    char search[1024];
    snprintf(search, sizeof(search), "%s\\*.*", path);
    WIN32_FIND_DATAA fd;
    HANDLE h = FindFirstFileA(search, &fd);
    if (h != INVALID_HANDLE_VALUE) {
        do {
            if (strcmp(fd.cFileName, ".") == 0 || strcmp(fd.cFileName, "..") == 0) continue;
            char sub[1024];
            snprintf(sub, sizeof(sub), "%s\\%s", path, fd.cFileName);
            if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
                remove_dir_recursive(sub);
            } else {
                DeleteFileA(sub);
            }
        } while (FindNextFileA(h, &fd));
        FindClose(h);
    }
    RemoveDirectoryA(path);
#else
    char cmd[1024];
    snprintf(cmd, sizeof(cmd), "rm -rf \"%s\"", path);
    system(cmd);
#endif
}

char *read_entire_file(const char *path, size_t *out_size) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long len = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (len < 0) {
        fclose(f);
        return NULL;
    }
    char *buf = (char *)xmalloc((size_t)len + 1);
    size_t read_bytes = fread(buf, 1, (size_t)len, f);
    buf[read_bytes] = '\0';
    fclose(f);
    if (out_size) *out_size = read_bytes;
    return buf;
}

char *read_entire_stdin(size_t *out_size) {
    Buffer b;
    buffer_init(&b);
    char chunk[4096];
    size_t n;
    while ((n = fread(chunk, 1, sizeof(chunk), stdin)) > 0) {
        buffer_append_bytes(&b, chunk, n);
    }
    if (out_size) *out_size = b.size;
    return b.data;
}

bool write_entire_file(const char *path, const char *data, size_t len) {
    FILE *f = fopen(path, "wb");
    if (!f) return false;
    if (len > 0 && data) {
        fwrite(data, 1, len, f);
    }
    fclose(f);
    return true;
}

char *get_user_home(void) {
    const char *h = getenv("USERPROFILE");
    if (!h || !*h) {
        h = getenv("HOME");
    }
    return h ? xstrdup(h) : NULL;
}

char *find_executable_path(const char *base_name) {
    const char *env_var = getenv("LUAU_LSP_PATH");
    if (env_var && *env_var && strcmp(base_name, "luau-lsp") == 0) {
        if (file_exists(env_var)) {
            return xstrdup(env_var);
        }
    }

    char exe_buf[512];
#if defined(_WIN32)
    snprintf(exe_buf, sizeof(exe_buf), "%s.exe", base_name);
#else
    snprintf(exe_buf, sizeof(exe_buf), "%s", base_name);
#endif

#if defined(_WIN32)
    char search_out[MAX_PATH];
    DWORD hit = SearchPathA(NULL, exe_buf, NULL, MAX_PATH, search_out, NULL);
    if (hit > 0 && hit < MAX_PATH) {
        return xstrdup(search_out);
    }
#endif

    char *home = get_user_home();
    if (home) {
        const char *subdirs[] = { ".rokit/bin", ".aftman/bin", ".foreman/bin", ".cargo/bin" };
        for (size_t i = 0; i < sizeof(subdirs) / sizeof(subdirs[0]); ++i) {
            char cand[1024];
            snprintf(cand, sizeof(cand), "%s/%s/%s", home, subdirs[i], exe_buf);
            if (file_exists(cand)) {
                free(home);
                return xstrdup(cand);
            }
        }
        free(home);
    }

    return NULL;
}

bool download_file(const char *url, const char *dest_path) {
    make_dir_recursive(dest_path);
#if defined(_WIN32)
    HRESULT hr = URLDownloadToFileA(NULL, url, dest_path, 0, NULL);
    return (hr == S_OK && file_exists(dest_path));
#else
    char cmd[2048];
    snprintf(cmd, sizeof(cmd), "curl -sSfL \"%s\" -o \"%s\"", url, dest_path);
    int rc = system(cmd);
    return (rc == 0 && file_exists(dest_path));
#endif
}

char *get_default_global_types_path(void) {
    char *home = get_user_home();
    char buf[1024];
    if (home) {
        snprintf(buf, sizeof(buf), "%s/.gemini/config/skills/strict-error-analyser/resources/globalTypes.d.luau", home);
        free(home);
        return xstrdup(buf);
    }
    return xstrdup("resources/globalTypes.d.luau");
}

bool run_rokit_bootstrap(void) {
    char *rokit = find_executable_path("rokit");
    if (!rokit) return false;
    char cmd[1024];
    snprintf(cmd, sizeof(cmd), "\"%s\" trust JohnnyMorganz/luau-lsp", rokit);
    system(cmd);
    snprintf(cmd, sizeof(cmd), "\"%s\" add --global JohnnyMorganz/luau-lsp", rokit);
    int rc = system(cmd);
    free(rokit);
    return (rc == 0);
}

char *run_subprocess_capture(const char *cmd, const char *cwd, int *out_exit_code) {
    Buffer output;
    buffer_init(&output);

#if defined(_WIN32)
    HANDLE hReadPipe = NULL, hWritePipe = NULL;
    SECURITY_ATTRIBUTES sa;
    sa.nLength = sizeof(SECURITY_ATTRIBUTES);
    sa.bInheritHandle = TRUE;
    sa.lpSecurityDescriptor = NULL;

    if (!CreatePipe(&hReadPipe, &hWritePipe, &sa, 0)) {
        if (out_exit_code) *out_exit_code = -1;
        return NULL;
    }
    SetHandleInformation(hReadPipe, HANDLE_FLAG_INHERIT, 0);

    STARTUPINFOA si;
    ZeroMemory(&si, sizeof(si));
    si.cb = sizeof(si);
    si.hStdOutput = hWritePipe;
    si.hStdError = hWritePipe;
    si.dwFlags |= STARTF_USESTDHANDLES;

    PROCESS_INFORMATION pi;
    ZeroMemory(&pi, sizeof(pi));

    char cmd_copy[4096];
    strncpy(cmd_copy, cmd, sizeof(cmd_copy) - 1);
    cmd_copy[sizeof(cmd_copy) - 1] = '\0';

    BOOL created = CreateProcessA(NULL, cmd_copy, NULL, NULL, TRUE, 0, NULL, cwd, &si, &pi);
    CloseHandle(hWritePipe);

    if (!created) {
        CloseHandle(hReadPipe);
        if (out_exit_code) *out_exit_code = -1;
        return NULL;
    }

    char chunk[4096];
    DWORD bytesRead = 0;
    while (ReadFile(hReadPipe, chunk, sizeof(chunk), &bytesRead, NULL) && bytesRead > 0) {
        buffer_append_bytes(&output, chunk, bytesRead);
    }
    CloseHandle(hReadPipe);

    WaitForSingleObject(pi.hProcess, INFINITE);
    DWORD exit_code = 0;
    GetExitCodeProcess(pi.hProcess, &exit_code);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);

    if (out_exit_code) *out_exit_code = (int)exit_code;
#else
    char full_cmd[4096];
    if (cwd && *cwd) {
        snprintf(full_cmd, sizeof(full_cmd), "cd \"%s\" && %s 2>&1", cwd, cmd);
    } else {
        snprintf(full_cmd, sizeof(full_cmd), "%s 2>&1", cmd);
    }
    FILE *p = popen(full_cmd, "r");
    if (!p) {
        if (out_exit_code) *out_exit_code = -1;
        return NULL;
    }
    char chunk[4096];
    size_t n;
    while ((n = fread(chunk, 1, sizeof(chunk), p)) > 0) {
        buffer_append_bytes(&output, chunk, n);
    }
    int rc = pclose(p);
    if (out_exit_code) *out_exit_code = WEXITSTATUS(rc);
#endif

    return output.data ? output.data : xstrdup("");
}
