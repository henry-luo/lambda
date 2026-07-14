// shell.c
// Cross-platform shell and process execution utilities

#ifdef __APPLE__
  #define _DARWIN_C_SOURCE
#else
  #define _GNU_SOURCE
#endif

#include "shell.h"
#include "memtrack.h"
#include "log.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#ifdef _WIN32
  #define WIN32_LEAN_AND_MEAN
  #include <windows.h>
  #include <io.h>
  #include <process.h>
  #include <direct.h>
  #include <sys/stat.h>
  #include <sys/types.h>
  #ifndef S_IXUSR
  #define S_IXUSR 0100
  #endif
#else
  #include <unistd.h>
  #include <sys/types.h>
  #include <sys/wait.h>
  #include <sys/stat.h>
  #include <signal.h>
  #include <fcntl.h>
  #include <spawn.h>
  #include <poll.h>
  #include <limits.h>
  #include <time.h>
  extern char** environ;
#endif

// for gethostname
#ifdef _WIN32
  // already included via windows.h
#else
  // gethostname from unistd.h
#endif

extern char* strdup(const char* s);

typedef struct {
    char* data;
    size_t len;
    size_t cap;
} ShellCapture;

static bool shell_capture_init(ShellCapture* capture) {
    capture->cap = 4096;
    capture->len = 0;
    capture->data = (char*)mem_alloc(capture->cap, MEM_CAT_TEMP);
    if (!capture->data) return false;
    capture->data[0] = '\0';
    return true;
}

static bool shell_capture_append(ShellCapture* capture, const char* data, size_t len) {
    if (len == 0) return true;
    if (capture->len + len + 1 > capture->cap) {
        size_t cap = capture->cap;
        while (capture->len + len + 1 > cap) cap *= 2;
        char* resized = (char*)mem_realloc(capture->data, cap, MEM_CAT_TEMP);
        if (!resized) return false;
        capture->data = resized;
        capture->cap = cap;
    }
    memcpy(capture->data + capture->len, data, len);
    capture->len += len;
    capture->data[capture->len] = '\0';
    return true;
}

static char* shell_capture_take(ShellCapture* capture, size_t* out_len) {
    char* data = capture->data;
    if (out_len) *out_len = capture->len;
    capture->data = NULL;
    capture->len = 0;
    capture->cap = 0;
    return data;
}

static void shell_capture_discard(ShellCapture* capture) {
    mem_free(capture->data);
    capture->data = NULL;
    capture->len = 0;
    capture->cap = 0;
}

// ---------------------------------------------------------------------------
// Internal: platform-specific pipe and process helpers
// ---------------------------------------------------------------------------

#ifdef _WIN32

// Windows pipe pair
typedef struct {
    HANDLE read;
    HANDLE write;
} WinPipe;

static bool win_create_pipe(WinPipe* p) {
    SECURITY_ATTRIBUTES sa;
    sa.nLength = sizeof(sa);
    sa.bInheritHandle = TRUE;
    sa.lpSecurityDescriptor = NULL;
    if (!CreatePipe(&p->read, &p->write, &sa, 0)) {
        log_error("shell: CreatePipe failed: %lu", GetLastError());
        return false;
    }
    return true;
}

static char* win_read_pipe(HANDLE pipe, size_t* out_len) {
    size_t cap = 4096;
    size_t len = 0;
    char* buf = (char*)mem_alloc(cap, MEM_CAT_TEMP);
    if (!buf) return NULL;

    for (;;) {
        DWORD n = 0;
        if (len + 1024 > cap) {
            cap *= 2;
            char* nb = (char*)mem_realloc(buf, cap, MEM_CAT_TEMP);
            if (!nb) { mem_free(buf); return NULL; }
            buf = nb;
        }
        BOOL ok = ReadFile(pipe, buf + len, (DWORD)(cap - len - 1), &n, NULL);
        if (!ok || n == 0) break;
        len += n;
    }
    buf[len] = '\0';
    if (out_len) *out_len = len;
    return buf;
}

static bool win_env_key_matches(const char* assignment, const char* key) {
    const char* separator = strchr(assignment[0] == '=' ? assignment + 1 : assignment, '=');
    if (!separator) return false;
    size_t assignment_key_len = (size_t)(separator - assignment);
    size_t key_len = strlen(key);
    return assignment_key_len == key_len && _strnicmp(assignment, key, key_len) == 0;
}

static bool win_env_is_overridden(const char* assignment, const ShellEnvEntry* extras) {
    if (!extras) return false;
    for (const ShellEnvEntry* entry = extras; entry->key; entry++) {
        if (win_env_key_matches(assignment, entry->key)) return true;
    }
    return false;
}

static char* win_build_env_block(const ShellEnvEntry* extras) {
    if (!extras) return NULL;
    LPCH inherited = GetEnvironmentStringsA();
    if (!inherited) return NULL;

    size_t cap = 2;
    for (const char* item = inherited; *item; item += strlen(item) + 1) {
        cap += strlen(item) + 1;
    }
    for (const ShellEnvEntry* entry = extras; entry->key; entry++) {
        if (entry->value) cap += strlen(entry->key) + strlen(entry->value) + 2;
    }

    char* block = (char*)mem_alloc(cap, MEM_CAT_TEMP);
    if (!block) {
        FreeEnvironmentStringsA(inherited);
        return NULL;
    }
    char* write = block;
    for (const char* item = inherited; *item; item += strlen(item) + 1) {
        if (win_env_is_overridden(item, extras)) continue;
        size_t len = strlen(item) + 1;
        memcpy(write, item, len);
        write += len;
    }
    for (const ShellEnvEntry* entry = extras; entry->key; entry++) {
        if (!entry->value) continue;
        size_t key_len = strlen(entry->key);
        size_t value_len = strlen(entry->value);
        memcpy(write, entry->key, key_len);
        write += key_len;
        *write++ = '=';
        memcpy(write, entry->value, value_len);
        write += value_len;
        *write++ = '\0';
    }
    *write++ = '\0';
    FreeEnvironmentStringsA(inherited);
    return block;
}

typedef struct {
    HANDLE pipe;
    ShellCapture capture;
    bool ok;
} WinCaptureThread;

static DWORD WINAPI win_capture_thread_main(LPVOID data) {
    WinCaptureThread* context = (WinCaptureThread*)data;
    context->ok = true;
    for (;;) {
        char buffer[4096];
        DWORD count = 0;
        BOOL read_ok = ReadFile(context->pipe, buffer, sizeof(buffer), &count, NULL);
        if (!read_ok || count == 0) break;
        if (!shell_capture_append(&context->capture, buffer, (size_t)count)) {
            context->ok = false;
            break;
        }
    }
    return 0;
}

#else // POSIX

static char* posix_read_fd(int fd, size_t* out_len) {
    size_t cap = 4096;
    size_t len = 0;
    char* buf = (char*)mem_alloc(cap, MEM_CAT_TEMP);
    if (!buf) return NULL;

    for (;;) {
        if (len + 1024 > cap) {
            cap *= 2;
            char* nb = (char*)mem_realloc(buf, cap, MEM_CAT_TEMP);
            if (!nb) { mem_free(buf); return NULL; }
            buf = nb;
        }
        ssize_t n = read(fd, buf + len, cap - len - 1);
        if (n <= 0) break;
        len += (size_t)n;
    }
    buf[len] = '\0';
    if (out_len) *out_len = len;
    return buf;
}

static bool shell_env_key_matches(const char* assignment, const char* key) {
    size_t key_len = strlen(key);
    return strncmp(assignment, key, key_len) == 0 && assignment[key_len] == '=';
}

static bool shell_env_is_overridden(const char* assignment, const ShellEnvEntry* extras) {
    if (!extras) return false;
    for (const ShellEnvEntry* entry = extras; entry->key; entry++) {
        if (shell_env_key_matches(assignment, entry->key)) return true;
    }
    return false;
}

static char** shell_build_env(const ShellEnvEntry* extras) {
    if (!extras) return NULL;

    size_t inherited_count = 0;
    while (environ[inherited_count]) inherited_count++;
    size_t extra_count = 0;
    while (extras[extra_count].key) extra_count++;

    char** env = (char**)mem_calloc(inherited_count + extra_count + 1,
                                    sizeof(char*), MEM_CAT_TEMP);
    if (!env) return NULL;

    size_t count = 0;
    for (size_t i = 0; i < inherited_count; i++) {
        if (shell_env_is_overridden(environ[i], extras)) continue;
        env[count] = mem_strdup(environ[i], MEM_CAT_TEMP);
        if (!env[count]) goto failed;
        count++;
    }
    for (size_t i = 0; i < extra_count; i++) {
        if (!extras[i].value) continue;
        size_t key_len = strlen(extras[i].key);
        size_t value_len = strlen(extras[i].value);
        env[count] = (char*)mem_alloc(key_len + value_len + 2, MEM_CAT_TEMP);
        if (!env[count]) goto failed;
        memcpy(env[count], extras[i].key, key_len);
        env[count][key_len] = '=';
        memcpy(env[count] + key_len + 1, extras[i].value, value_len + 1);
        count++;
    }
    return env;

failed:
    for (size_t i = 0; i < count; i++) mem_free(env[i]);
    mem_free(env);
    return NULL;
}

static void shell_free_env(char** env) {
    if (!env) return;
    for (size_t i = 0; env[i]; i++) mem_free(env[i]);
    mem_free(env);
}

static void shell_kill_process_group(pid_t pid, int signal_number) {
    if (kill(-pid, signal_number) != 0 && errno == ESRCH) {
        kill(pid, signal_number);
    }
}

#endif

// ---------------------------------------------------------------------------
// Internal: deliver captured output through line callbacks if set
// ---------------------------------------------------------------------------

static void deliver_lines(const char* buf, size_t len,
                          ShellLineCallback cb, void* user_data) {
    if (!cb || !buf || len == 0) return;
    const char* p = buf;
    const char* end = buf + len;
    while (p < end) {
        const char* nl = (const char*)memchr(p, '\n', (size_t)(end - p));
        size_t line_len = nl ? (size_t)(nl - p) : (size_t)(end - p);
        if (!cb(p, line_len, user_data)) break;
        p += line_len + (nl ? 1 : 0);
        if (!nl) break;
    }
}

// ---------------------------------------------------------------------------
// shell_exec — synchronous command execution
// ---------------------------------------------------------------------------

#ifdef _WIN32

static ShellResult shell_exec_win32(const char* program, const char** args,
                                    const ShellOptions* opts) {
    ShellResult result = {0};
    result.exit_code = -1;

    // build command line string from args
    size_t cmdlen = 0;
    for (int i = 0; args && args[i]; i++) {
        cmdlen += strlen(args[i]) + 3; // quotes + space
    }
    char* cmdline = (char*)mem_alloc(cmdlen + 1, MEM_CAT_TEMP);
    if (!cmdline) {
        log_error("shell: malloc failed for cmdline");
        return result;
    }
    cmdline[0] = '\0';
    size_t cmdcap = cmdlen + 1;
    for (int i = 0; args && args[i]; i++) {
        size_t dlen = strlen(cmdline);
        if (i > 0) { snprintf(cmdline + dlen, cmdcap - dlen, " "); dlen = strlen(cmdline); }
        if (i == 2 && args[0] && args[1] &&
            strcmp(args[0], "cmd") == 0 && strcmp(args[1], "/c") == 0) {
            snprintf(cmdline + dlen, cmdcap - dlen, "%s", args[i]);
            continue;
        }
        // simple quoting — wrap each arg in double quotes
        snprintf(cmdline + dlen, cmdcap - dlen, "\""); dlen = strlen(cmdline);
        snprintf(cmdline + dlen, cmdcap - dlen, "%s", args[i]); dlen = strlen(cmdline);
        snprintf(cmdline + dlen, cmdcap - dlen, "\"");
    }

    WinPipe stdout_pipe = {0}, stderr_pipe = {0};
    bool merge = opts && opts->merge_stderr;
    HANDLE stdin_handle = INVALID_HANDLE_VALUE;
    WinCaptureThread stdout_context = {0};
    WinCaptureThread stderr_context = {0};

    if (!win_create_pipe(&stdout_pipe)) { mem_free(cmdline); return result; }
    if (!merge) {
        if (!win_create_pipe(&stderr_pipe)) {
            CloseHandle(stdout_pipe.read);
            CloseHandle(stdout_pipe.write);
            mem_free(cmdline);
            return result;
        }
    }

    // prevent child from inheriting read ends
    SetHandleInformation(stdout_pipe.read, HANDLE_FLAG_INHERIT, 0);
    if (!merge) SetHandleInformation(stderr_pipe.read, HANDLE_FLAG_INHERIT, 0);

    if (opts && opts->stdin_path) {
        SECURITY_ATTRIBUTES stdin_security = {0};
        stdin_security.nLength = sizeof(stdin_security);
        stdin_security.bInheritHandle = TRUE;
        stdin_handle = CreateFileA(opts->stdin_path, GENERIC_READ, FILE_SHARE_READ,
                                   &stdin_security, OPEN_EXISTING,
                                   FILE_ATTRIBUTE_NORMAL, NULL);
        if (stdin_handle == INVALID_HANDLE_VALUE) {
            log_error("shell: cannot open stdin file '%s': %lu",
                      opts->stdin_path, GetLastError());
            CloseHandle(stdout_pipe.read);
            CloseHandle(stdout_pipe.write);
            if (!merge) {
                CloseHandle(stderr_pipe.read);
                CloseHandle(stderr_pipe.write);
            }
            mem_free(cmdline);
            return result;
        }
    }

    if (!shell_capture_init(&stdout_context.capture) ||
        (!merge && !shell_capture_init(&stderr_context.capture))) {
        log_error("shell: capture buffer allocation failed");
        shell_capture_discard(&stdout_context.capture);
        shell_capture_discard(&stderr_context.capture);
        CloseHandle(stdout_pipe.read);
        CloseHandle(stdout_pipe.write);
        if (!merge) {
            CloseHandle(stderr_pipe.read);
            CloseHandle(stderr_pipe.write);
        }
        if (stdin_handle != INVALID_HANDLE_VALUE) CloseHandle(stdin_handle);
        mem_free(cmdline);
        return result;
    }

    STARTUPINFOA si;
    PROCESS_INFORMATION pi;
    memset(&si, 0, sizeof(si));
    memset(&pi, 0, sizeof(pi));
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdOutput = stdout_pipe.write;
    si.hStdError = merge ? stdout_pipe.write : stderr_pipe.write;
    si.hStdInput = stdin_handle != INVALID_HANDLE_VALUE
        ? stdin_handle : GetStdHandle(STD_INPUT_HANDLE);

    // set working directory
    const char* cwd = (opts && opts->cwd) ? opts->cwd : NULL;
    char* child_env = win_build_env_block(opts ? opts->env : NULL);
    if (opts && opts->env && !child_env) {
        log_error("shell: child environment allocation failed");
        shell_capture_discard(&stdout_context.capture);
        shell_capture_discard(&stderr_context.capture);
        CloseHandle(stdout_pipe.read);
        CloseHandle(stdout_pipe.write);
        if (!merge) {
            CloseHandle(stderr_pipe.read);
            CloseHandle(stderr_pipe.write);
        }
        if (stdin_handle != INVALID_HANDLE_VALUE) CloseHandle(stdin_handle);
        mem_free(cmdline);
        return result;
    }

    HANDLE job = CreateJobObjectA(NULL, NULL);
    if (job) {
        JOBOBJECT_EXTENDED_LIMIT_INFORMATION limit = {0};
        limit.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
        SetInformationJobObject(job, JobObjectExtendedLimitInformation,
                                &limit, sizeof(limit));
    }
    DWORD creation_flags = CREATE_SUSPENDED | CREATE_NEW_PROCESS_GROUP;
    BOOL created = CreateProcessA(NULL, cmdline, NULL, NULL, TRUE,
                                  creation_flags, child_env, cwd, &si, &pi);
    mem_free(child_env);
    // close write ends in parent
    CloseHandle(stdout_pipe.write);
    if (!merge) CloseHandle(stderr_pipe.write);
    if (stdin_handle != INVALID_HANDLE_VALUE) CloseHandle(stdin_handle);

    if (!created) {
        log_error("shell: CreateProcess failed: %lu", GetLastError());
        CloseHandle(stdout_pipe.read);
        if (!merge) CloseHandle(stderr_pipe.read);
        if (job) CloseHandle(job);
        shell_capture_discard(&stdout_context.capture);
        shell_capture_discard(&stderr_context.capture);
        mem_free(cmdline);
        return result;
    }

    if (job && !AssignProcessToJobObject(job, pi.hProcess)) {
        CloseHandle(job);
        job = NULL;
    }
    ResumeThread(pi.hThread);

    stdout_context.pipe = stdout_pipe.read;
    HANDLE stdout_thread = CreateThread(NULL, 0, win_capture_thread_main,
                                        &stdout_context, 0, NULL);
    HANDLE stderr_thread = NULL;
    if (!merge) {
        stderr_context.pipe = stderr_pipe.read;
        stderr_thread = CreateThread(NULL, 0, win_capture_thread_main,
                                     &stderr_context, 0, NULL);
    }
    if (!stdout_thread || (!merge && !stderr_thread)) {
        log_error("shell: output reader thread creation failed: %lu", GetLastError());
        if (job) TerminateJobObject(job, 1);
        else TerminateProcess(pi.hProcess, 1);
    }

    // Reader threads drain both streams while waiting so neither child pipe can fill.
    DWORD wait_ms = INFINITE;
    if (opts && opts->timeout_ms > 0) wait_ms = (DWORD)opts->timeout_ms;
    DWORD wait_result = WaitForSingleObject(pi.hProcess, wait_ms);

    if (wait_result == WAIT_TIMEOUT) {
        // The job owns descendants, preventing timed-out grandchildren from leaking.
        if (job) TerminateJobObject(job, 1);
        else TerminateProcess(pi.hProcess, 1);
        WaitForSingleObject(pi.hProcess, 1000);
        result.timed_out = true;
    }

    DWORD exit_code = 0;
    GetExitCodeProcess(pi.hProcess, &exit_code);
    result.exit_code = (int)exit_code;

    if (stdout_thread) WaitForSingleObject(stdout_thread, INFINITE);
    if (stderr_thread) WaitForSingleObject(stderr_thread, INFINITE);
    result.stdout_buf = shell_capture_take(&stdout_context.capture, &result.stdout_len);
    if (!merge) {
        result.stderr_buf = shell_capture_take(&stderr_context.capture, &result.stderr_len);
    }

    if (stdout_thread) CloseHandle(stdout_thread);
    if (stderr_thread) CloseHandle(stderr_thread);
    CloseHandle(stdout_pipe.read);
    if (!merge) CloseHandle(stderr_pipe.read);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    if (job) CloseHandle(job);
    mem_free(cmdline);
    return result;
}

#else // POSIX

static ShellResult shell_exec_posix(const char* program, const char** args,
                                    const ShellOptions* opts) {
    ShellResult result = {0};
    result.exit_code = -1;

    int stdout_pipe[2] = {-1, -1};
    int stderr_pipe[2] = {-1, -1};
    int stdin_fd = -1;
    bool merge = opts && opts->merge_stderr;
    ShellCapture stdout_capture = {0};
    ShellCapture stderr_capture = {0};

    if (pipe(stdout_pipe) < 0) {
        log_error("shell: pipe() failed: %s", strerror(errno));
        return result;
    }
    if (!merge && pipe(stderr_pipe) < 0) {
        log_error("shell: pipe() failed: %s", strerror(errno));
        close(stdout_pipe[0]);
        close(stdout_pipe[1]);
        return result;
    }
    if (opts && opts->stdin_path) {
        stdin_fd = open(opts->stdin_path, O_RDONLY);
        if (stdin_fd < 0) {
            log_error("shell: cannot open stdin file '%s': %s", opts->stdin_path, strerror(errno));
            close(stdout_pipe[0]);
            close(stdout_pipe[1]);
            if (!merge) {
                close(stderr_pipe[0]);
                close(stderr_pipe[1]);
            }
            return result;
        }
    }
    if (!shell_capture_init(&stdout_capture) ||
        (!merge && !shell_capture_init(&stderr_capture))) {
        log_error("shell: capture buffer allocation failed");
        shell_capture_discard(&stdout_capture);
        shell_capture_discard(&stderr_capture);
        close(stdout_pipe[0]);
        close(stdout_pipe[1]);
        if (!merge) {
            close(stderr_pipe[0]);
            close(stderr_pipe[1]);
        }
        if (stdin_fd >= 0) close(stdin_fd);
        return result;
    }

    // use posix_spawn for efficiency
    posix_spawn_file_actions_t actions;
    posix_spawn_file_actions_init(&actions);
    posix_spawn_file_actions_addclose(&actions, stdout_pipe[0]);
    posix_spawn_file_actions_adddup2(&actions, stdout_pipe[1], STDOUT_FILENO);

    if (merge) {
        // dup stderr before closing stdout_pipe[1]
        posix_spawn_file_actions_adddup2(&actions, stdout_pipe[1], STDERR_FILENO);
        posix_spawn_file_actions_addclose(&actions, stdout_pipe[1]);
    } else {
        posix_spawn_file_actions_addclose(&actions, stdout_pipe[1]);
        posix_spawn_file_actions_addclose(&actions, stderr_pipe[0]);
        posix_spawn_file_actions_adddup2(&actions, stderr_pipe[1], STDERR_FILENO);
        posix_spawn_file_actions_addclose(&actions, stderr_pipe[1]);
    }
    if (stdin_fd >= 0) {
        posix_spawn_file_actions_adddup2(&actions, stdin_fd, STDIN_FILENO);
        posix_spawn_file_actions_addclose(&actions, stdin_fd);
    }

    // handle cwd via chdir action
    if (opts && opts->cwd) {
        posix_spawn_file_actions_addchdir_np(&actions, opts->cwd);
    }

    posix_spawnattr_t attr;
    posix_spawnattr_init(&attr);
    short spawn_flags = 0;
#ifdef POSIX_SPAWN_SETPGROUP
    // Give each launch a private group so timeout cleanup cannot leave descendants running.
    posix_spawnattr_setpgroup(&attr, 0);
    spawn_flags |= POSIX_SPAWN_SETPGROUP;
    posix_spawnattr_setflags(&attr, spawn_flags);
#endif

    char** child_env = shell_build_env(opts ? opts->env : NULL);
    if (opts && opts->env && !child_env) {
        log_error("shell: child environment allocation failed");
        posix_spawn_file_actions_destroy(&actions);
        posix_spawnattr_destroy(&attr);
        close(stdout_pipe[0]);
        close(stdout_pipe[1]);
        if (!merge) {
            close(stderr_pipe[0]);
            close(stderr_pipe[1]);
        }
        if (stdin_fd >= 0) close(stdin_fd);
        shell_capture_discard(&stdout_capture);
        shell_capture_discard(&stderr_capture);
        return result;
    }

    pid_t pid;
    // args already has program as argv[0]; cast away const for posix_spawn
    int spawn_err = posix_spawnp(&pid, program, &actions, &attr,
                                 (char* const*)args, child_env ? child_env : environ);

    posix_spawn_file_actions_destroy(&actions);
    posix_spawnattr_destroy(&attr);
    shell_free_env(child_env);

    // close write ends in parent
    close(stdout_pipe[1]);
    if (!merge) close(stderr_pipe[1]);
    if (stdin_fd >= 0) close(stdin_fd);

    if (spawn_err != 0) {
        log_error("shell: posix_spawnp failed for '%s': %s", program, strerror(spawn_err));
        close(stdout_pipe[0]);
        if (!merge) close(stderr_pipe[0]);
        shell_capture_discard(&stdout_capture);
        shell_capture_discard(&stderr_capture);
        return result;
    }

    // Drain both pipes while the child runs; waiting first can deadlock once a pipe fills.
    fcntl(stdout_pipe[0], F_SETFL, fcntl(stdout_pipe[0], F_GETFL, 0) | O_NONBLOCK);
    if (!merge) {
        fcntl(stderr_pipe[0], F_SETFL, fcntl(stderr_pipe[0], F_GETFL, 0) | O_NONBLOCK);
    }

    bool stdout_open = true;
    bool stderr_open = !merge;
    bool child_done = false;
    bool terminate_sent = false;
    int status = 0;
    struct timespec started;
    clock_gettime(CLOCK_MONOTONIC, &started);

    while (!child_done || stdout_open || stderr_open) {
        struct pollfd fds[2];
        nfds_t nfds = 0;
        if (stdout_open) {
            fds[nfds].fd = stdout_pipe[0];
            fds[nfds].events = POLLIN | POLLHUP;
            fds[nfds].revents = 0;
            nfds++;
        }
        if (stderr_open) {
            fds[nfds].fd = stderr_pipe[0];
            fds[nfds].events = POLLIN | POLLHUP;
            fds[nfds].revents = 0;
            nfds++;
        }
        if (nfds > 0) poll(fds, nfds, 10);
        else {
            struct timespec pause = {0, 10000000L};
            nanosleep(&pause, NULL);
        }

        for (nfds_t i = 0; i < nfds; i++) {
            if (!(fds[i].revents & (POLLIN | POLLHUP | POLLERR))) continue;
            ShellCapture* capture = fds[i].fd == stdout_pipe[0]
                ? &stdout_capture : &stderr_capture;
            bool* open_flag = fds[i].fd == stdout_pipe[0]
                ? &stdout_open : &stderr_open;
            for (;;) {
                char buffer[4096];
                ssize_t count = read(fds[i].fd, buffer, sizeof(buffer));
                if (count > 0) {
                    if (!shell_capture_append(capture, buffer, (size_t)count)) {
                        log_error("shell: capture buffer allocation failed while reading output");
                        *open_flag = false;
                        close(fds[i].fd);
                        break;
                    }
                    continue;
                }
                if (count == 0) {
                    *open_flag = false;
                    close(fds[i].fd);
                }
                break;
            }
        }

        if (!child_done) {
            int waited = waitpid(pid, &status, WNOHANG);
            if (waited == pid) child_done = true;
            else if (waited < 0 && errno != EINTR) {
                log_error("shell: waitpid failed: %s", strerror(errno));
                child_done = true;
                status = 0;
            }
        }

        if (!child_done && opts && opts->timeout_ms > 0) {
            struct timespec now;
            clock_gettime(CLOCK_MONOTONIC, &now);
            int64_t elapsed_ms = (int64_t)(now.tv_sec - started.tv_sec) * 1000 +
                (now.tv_nsec - started.tv_nsec) / 1000000;
            if (elapsed_ms >= opts->timeout_ms) {
                // The private process group is the invariant that makes timeout cleanup complete.
                if (!terminate_sent) {
                    shell_kill_process_group(pid, SIGTERM);
                    terminate_sent = true;
                } else if (elapsed_ms >= opts->timeout_ms + 100) {
                    shell_kill_process_group(pid, SIGKILL);
                }
                result.timed_out = true;
            }
        }
    }

    if (!child_done) {
        while (waitpid(pid, &status, 0) < 0 && errno == EINTR) {}
    }
    if (WIFEXITED(status)) {
        result.exit_code = WEXITSTATUS(status);
    } else if (WIFSIGNALED(status)) {
        result.exit_code = 128 + WTERMSIG(status);
    } else {
        result.exit_code = -1;
    }

    result.stdout_buf = shell_capture_take(&stdout_capture, &result.stdout_len);
    if (!merge) {
        result.stderr_buf = shell_capture_take(&stderr_capture, &result.stderr_len);
    }

    return result;
}

#endif // _WIN32

ShellResult shell_exec(const char* program, const char** args,
                       const ShellOptions* opts) {
    ShellResult result = {0};
    result.exit_code = -1;

    if (!program) {
        log_error("shell_exec: program is NULL");
        return result;
    }
    if (!args) {
        log_error("shell_exec: args is NULL");
        return result;
    }

#ifdef _WIN32
    result = shell_exec_win32(program, args, opts);
#else
    result = shell_exec_posix(program, args, opts);
#endif

    // deliver line callbacks if provided
    if (opts) {
        deliver_lines(result.stdout_buf, result.stdout_len,
                      opts->on_stdout, opts->user_data);
        deliver_lines(result.stderr_buf, result.stderr_len,
                      opts->on_stderr, opts->user_data);
    }

    return result;
}

ShellResult shell_exec_simple(const char* program, const char** args) {
    return shell_exec(program, args, NULL);
}

ShellResult shell_exec_line(const char* cmdline, const ShellOptions* opts) {
    ShellResult result = {0};
    result.exit_code = -1;

    if (!cmdline) {
        log_error("shell_exec_line: cmdline is NULL");
        return result;
    }

#ifdef _WIN32
    const char* args[] = {"cmd", "/c", cmdline, NULL};
    return shell_exec("cmd", args, opts);
#else
    const char* args[] = {"sh", "-c", cmdline, NULL};
    return shell_exec("sh", args, opts);
#endif
}

// ---------------------------------------------------------------------------
// Background processes
// ---------------------------------------------------------------------------

struct ShellProcess {
#ifdef _WIN32
    HANDLE hProcess;
    HANDLE hThread;
    HANDLE stdout_read;
    HANDLE stderr_read;
#else
    pid_t pid;
    int stdout_fd;
    int stderr_fd;
#endif
    bool finished;
    int exit_code;
    bool merge_stderr;
};

#ifdef _WIN32

ShellProcess* shell_spawn(const char* program, const char** args,
                           const ShellOptions* opts) {
    if (!program || !args) return NULL;

    ShellProcess* proc = (ShellProcess*)mem_calloc(1, sizeof(ShellProcess), MEM_CAT_TEMP);
    if (!proc) return NULL;

    bool merge = opts && opts->merge_stderr;
    proc->merge_stderr = merge;

    // build command line
    size_t cmdlen = 0;
    for (int i = 0; args[i]; i++) cmdlen += strlen(args[i]) + 3;
    char* cmdline = (char*)mem_alloc(cmdlen + 1, MEM_CAT_TEMP);
    if (!cmdline) { mem_free(proc); return NULL; }
    cmdline[0] = '\0';
    size_t cmdcap = cmdlen + 1;
    for (int i = 0; args[i]; i++) {
        size_t dlen = strlen(cmdline);
        if (i > 0) { snprintf(cmdline + dlen, cmdcap - dlen, " "); dlen = strlen(cmdline); }
        snprintf(cmdline + dlen, cmdcap - dlen, "\""); dlen = strlen(cmdline);
        snprintf(cmdline + dlen, cmdcap - dlen, "%s", args[i]); dlen = strlen(cmdline);
        snprintf(cmdline + dlen, cmdcap - dlen, "\"");
    }

    WinPipe stdout_pipe = {0}, stderr_pipe = {0};
    if (!win_create_pipe(&stdout_pipe)) { mem_free(cmdline); mem_free(proc); return NULL; }
    if (!merge && !win_create_pipe(&stderr_pipe)) {
        CloseHandle(stdout_pipe.read); CloseHandle(stdout_pipe.write);
        mem_free(cmdline); mem_free(proc); return NULL;
    }
    SetHandleInformation(stdout_pipe.read, HANDLE_FLAG_INHERIT, 0);
    if (!merge) SetHandleInformation(stderr_pipe.read, HANDLE_FLAG_INHERIT, 0);

    STARTUPINFOA si;
    PROCESS_INFORMATION pi;
    memset(&si, 0, sizeof(si));
    memset(&pi, 0, sizeof(pi));
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdOutput = stdout_pipe.write;
    si.hStdError = merge ? stdout_pipe.write : stderr_pipe.write;
    si.hStdInput = GetStdHandle(STD_INPUT_HANDLE);

    const char* cwd = (opts && opts->cwd) ? opts->cwd : NULL;
    BOOL ok = CreateProcessA(NULL, cmdline, NULL, NULL, TRUE, 0, NULL, cwd, &si, &pi);
    CloseHandle(stdout_pipe.write);
    if (!merge) CloseHandle(stderr_pipe.write);
    mem_free(cmdline);

    if (!ok) {
        log_error("shell_spawn: CreateProcess failed: %lu", GetLastError());
        CloseHandle(stdout_pipe.read);
        if (!merge) CloseHandle(stderr_pipe.read);
        mem_free(proc);
        return NULL;
    }
    proc->hProcess = pi.hProcess;
    proc->hThread = pi.hThread;
    proc->stdout_read = stdout_pipe.read;
    proc->stderr_read = merge ? INVALID_HANDLE_VALUE : stderr_pipe.read;
    return proc;
}

bool shell_process_poll(ShellProcess* proc) {
    if (!proc || proc->finished) return true;
    DWORD r = WaitForSingleObject(proc->hProcess, 0);
    if (r == WAIT_OBJECT_0) {
        proc->finished = true;
        DWORD code;
        GetExitCodeProcess(proc->hProcess, &code);
        proc->exit_code = (int)code;
        return true;
    }
    return false;
}

ShellResult shell_process_wait(ShellProcess* proc, int timeout_ms) {
    ShellResult result = {0};
    result.exit_code = -1;
    if (!proc) return result;

    DWORD wait_ms = (timeout_ms > 0) ? (DWORD)timeout_ms : INFINITE;
    DWORD wr = WaitForSingleObject(proc->hProcess, wait_ms);
    if (wr == WAIT_TIMEOUT) {
        TerminateProcess(proc->hProcess, 1);
        WaitForSingleObject(proc->hProcess, 1000);
        result.timed_out = true;
    }
    DWORD code;
    GetExitCodeProcess(proc->hProcess, &code);
    result.exit_code = (int)code;
    proc->finished = true;
    proc->exit_code = result.exit_code;

    result.stdout_buf = win_read_pipe(proc->stdout_read, &result.stdout_len);
    if (!proc->merge_stderr && proc->stderr_read != INVALID_HANDLE_VALUE) {
        result.stderr_buf = win_read_pipe(proc->stderr_read, &result.stderr_len);
    }
    return result;
}

bool shell_process_kill(ShellProcess* proc) {
    if (!proc || proc->finished) return false;
    return TerminateProcess(proc->hProcess, 1) != 0;
}

void shell_process_free(ShellProcess* proc) {
    if (!proc) return;
    CloseHandle(proc->hProcess);
    CloseHandle(proc->hThread);
    CloseHandle(proc->stdout_read);
    if (!proc->merge_stderr && proc->stderr_read != INVALID_HANDLE_VALUE)
        CloseHandle(proc->stderr_read);
    mem_free(proc);
}

#else // POSIX

ShellProcess* shell_spawn(const char* program, const char** args,
                           const ShellOptions* opts) {
    if (!program || !args) return NULL;

    ShellProcess* proc = (ShellProcess*)mem_calloc(1, sizeof(ShellProcess), MEM_CAT_TEMP);
    if (!proc) return NULL;

    bool merge = opts && opts->merge_stderr;
    proc->merge_stderr = merge;

    int stdout_pipe[2] = {-1, -1};
    int stderr_pipe[2] = {-1, -1};

    if (pipe(stdout_pipe) < 0) {
        log_error("shell_spawn: pipe() failed: %s", strerror(errno));
        mem_free(proc);
        return NULL;
    }
    if (!merge && pipe(stderr_pipe) < 0) {
        log_error("shell_spawn: pipe() failed: %s", strerror(errno));
        close(stdout_pipe[0]); close(stdout_pipe[1]);
        mem_free(proc);
        return NULL;
    }
    posix_spawn_file_actions_t actions;
    posix_spawn_file_actions_init(&actions);
    posix_spawn_file_actions_addclose(&actions, stdout_pipe[0]);
    posix_spawn_file_actions_adddup2(&actions, stdout_pipe[1], STDOUT_FILENO);

    if (merge) {
        posix_spawn_file_actions_adddup2(&actions, stdout_pipe[1], STDERR_FILENO);
        posix_spawn_file_actions_addclose(&actions, stdout_pipe[1]);
    } else {
        posix_spawn_file_actions_addclose(&actions, stdout_pipe[1]);
        posix_spawn_file_actions_addclose(&actions, stderr_pipe[0]);
        posix_spawn_file_actions_adddup2(&actions, stderr_pipe[1], STDERR_FILENO);
        posix_spawn_file_actions_addclose(&actions, stderr_pipe[1]);
    }
    if (opts && opts->cwd) {
        posix_spawn_file_actions_addchdir_np(&actions, opts->cwd);
    }

    posix_spawnattr_t attr;
    posix_spawnattr_init(&attr);
    pid_t pid;
    int err = posix_spawnp(&pid, program, &actions, &attr,
                           (char* const*)args, environ);

    posix_spawn_file_actions_destroy(&actions);
    posix_spawnattr_destroy(&attr);

    close(stdout_pipe[1]);
    if (!merge) close(stderr_pipe[1]);

    if (err != 0) {
        log_error("shell_spawn: posix_spawnp failed for '%s': %s", program, strerror(err));
        close(stdout_pipe[0]);
        if (!merge) close(stderr_pipe[0]);
        mem_free(proc);
        return NULL;
    }

    // set read ends as non-blocking
    fcntl(stdout_pipe[0], F_SETFL, O_NONBLOCK);
    if (!merge) fcntl(stderr_pipe[0], F_SETFL, O_NONBLOCK);

    proc->pid = pid;
    proc->stdout_fd = stdout_pipe[0];
    proc->stderr_fd = merge ? -1 : stderr_pipe[0];
    return proc;
}

bool shell_process_poll(ShellProcess* proc) {
    if (!proc || proc->finished) return true;
    int status;
    int wr = waitpid(proc->pid, &status, WNOHANG);
    if (wr > 0) {
        proc->finished = true;
        proc->exit_code = WIFEXITED(status) ? WEXITSTATUS(status)
                        : (WIFSIGNALED(status) ? 128 + WTERMSIG(status) : -1);
        return true;
    }
    return false;
}

ShellResult shell_process_wait(ShellProcess* proc, int timeout_ms) {
    ShellResult result = {0};
    result.exit_code = -1;
    if (!proc) return result;

    if (timeout_ms > 0) {
        int elapsed = 0;
        int interval = 10;
        while (elapsed < timeout_ms) {
            if (shell_process_poll(proc)) break;
            struct timespec ts = {0, interval * 1000000L};
            nanosleep(&ts, NULL);
            elapsed += interval;
        }
        if (!proc->finished) {
            kill(proc->pid, SIGTERM);
            struct timespec grace = {0, 100000000L};
            nanosleep(&grace, NULL);
            if (!shell_process_poll(proc)) {
                kill(proc->pid, SIGKILL);
                int status;
                waitpid(proc->pid, &status, 0);
                proc->finished = true;
            }
            result.timed_out = true;
        }
    } else {
        int status;
        waitpid(proc->pid, &status, 0);
        proc->finished = true;
        proc->exit_code = WIFEXITED(status) ? WEXITSTATUS(status)
                        : (WIFSIGNALED(status) ? 128 + WTERMSIG(status) : -1);
    }

    result.exit_code = proc->exit_code;

    // set fds back to blocking for final read
    if (proc->stdout_fd >= 0) {
        fcntl(proc->stdout_fd, F_SETFL, 0);
        result.stdout_buf = posix_read_fd(proc->stdout_fd, &result.stdout_len);
    }
    if (proc->stderr_fd >= 0) {
        fcntl(proc->stderr_fd, F_SETFL, 0);
        result.stderr_buf = posix_read_fd(proc->stderr_fd, &result.stderr_len);
    }

    return result;
}

bool shell_process_kill(ShellProcess* proc) {
    if (!proc || proc->finished) return false;
    return kill(proc->pid, SIGTERM) == 0;
}

void shell_process_free(ShellProcess* proc) {
    if (!proc) return;
    if (proc->stdout_fd >= 0) close(proc->stdout_fd);
    if (proc->stderr_fd >= 0) close(proc->stderr_fd);
    mem_free(proc);
}

#endif // _WIN32

// ---------------------------------------------------------------------------
// Environment variables
// ---------------------------------------------------------------------------

const char* shell_getenv(const char* name) {
    if (!name) return NULL;
    return getenv(name);
}

bool shell_setenv(const char* name, const char* value) {
    if (!name) return false;
#ifdef _WIN32
    if (!value) {
        return _putenv_s(name, "") == 0;
    }
    return _putenv_s(name, value) == 0;
#else
    if (!value) return unsetenv(name) == 0;
    return setenv(name, value, 1) == 0;
#endif
}

bool shell_unsetenv(const char* name) {
    if (!name) return false;
#ifdef _WIN32
    return _putenv_s(name, "") == 0;
#else
    return unsetenv(name) == 0;
#endif
}

// ---------------------------------------------------------------------------
// shell_which — resolve program name via PATH
// ---------------------------------------------------------------------------

char* shell_which(const char* program) {
    if (!program || !*program) return NULL;

    // if it contains a path separator, check directly
    if (strchr(program, '/') != NULL
#ifdef _WIN32
        || strchr(program, '\\') != NULL
#endif
    ) {
        struct stat st;
        if (stat(program, &st) == 0 && (st.st_mode & S_IXUSR)) {
            return mem_strdup(program, MEM_CAT_TEMP);
        }
        return NULL;
    }

    const char* path_env = getenv("PATH");
    if (!path_env) return NULL;

    char* path_copy = mem_strdup(path_env, MEM_CAT_TEMP);
    if (!path_copy) return NULL;

#ifdef _WIN32
    const char* sep = ";";
    const char* exts[] = {".exe", ".cmd", ".bat", ".com", "", NULL};
#else
    const char* sep = ":";
    const char* exts[] = {"", NULL};
#endif

    char* saveptr = NULL;
    char* dir = strtok_r(path_copy, sep, &saveptr);

    while (dir) {
        for (int i = 0; exts[i]; i++) {
            size_t need = strlen(dir) + 1 + strlen(program) + strlen(exts[i]) + 1;
            char* full = (char*)mem_alloc(need, MEM_CAT_TEMP);
            if (!full) continue;
            snprintf(full, need, "%s/%s%s", dir, program, exts[i]);

            struct stat st;
            if (stat(full, &st) == 0
#ifndef _WIN32
                && (st.st_mode & S_IXUSR)
#endif
            ) {
                mem_free(path_copy);
                return full;
            }
            mem_free(full);
        }
        dir = strtok_r(NULL, sep, &saveptr);
    }

    mem_free(path_copy);
    return NULL;
}

// ---------------------------------------------------------------------------
// shell_quote_arg — POSIX single-quoting
// ---------------------------------------------------------------------------

char* shell_quote_arg(const char* arg) {
    if (!arg) return mem_strdup("''", MEM_CAT_TEMP);

    // check if quoting is needed
    bool needs_quoting = false;
    for (const char* p = arg; *p; p++) {
        if (!( (*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z') ||
               (*p >= '0' && *p <= '9') || *p == '_' || *p == '-' ||
               *p == '.' || *p == '/' || *p == ':' || *p == '@')) {
            needs_quoting = true;
            break;
        }
    }
    if (!needs_quoting && *arg != '\0') {
        return mem_strdup(arg, MEM_CAT_TEMP);
    }

    // count single quotes in input
    size_t len = strlen(arg);
    size_t sq_count = 0;
    for (size_t i = 0; i < len; i++) {
        if (arg[i] == '\'') sq_count++;
    }

    // 'arg' with \' for each embedded single quote
    // each ' becomes: '\''  (end quote, escaped quote, start quote)
    size_t out_len = 2 + len + sq_count * 3 + 1;
    char* out = (char*)mem_alloc(out_len, MEM_CAT_TEMP);
    if (!out) return NULL;

    char* w = out;
    *w++ = '\'';
    for (size_t i = 0; i < len; i++) {
        if (arg[i] == '\'') {
            *w++ = '\'';  // end single quote
            *w++ = '\\';
            *w++ = '\'';  // escaped literal quote
            *w++ = '\'';  // resume single quote
        } else {
            *w++ = arg[i];
        }
    }
    *w++ = '\'';
    *w = '\0';
    return out;
}

// ---------------------------------------------------------------------------
// shell_result_free
// ---------------------------------------------------------------------------

void shell_result_free(ShellResult* result) {
    if (!result) return;
    mem_free(result->stdout_buf);
    mem_free(result->stderr_buf);
    result->stdout_buf = NULL;
    result->stderr_buf = NULL;
    result->stdout_len = 0;
    result->stderr_len = 0;
}

// ---------------------------------------------------------------------------
// shell_get_home_dir / shell_get_temp_dir / shell_get_hostname
// ---------------------------------------------------------------------------

static const char* s_cached_home = NULL;
static const char* s_cached_temp = NULL;

const char* shell_get_home_dir(void) {
    if (s_cached_home) return s_cached_home;
#ifdef _WIN32
    const char* home = getenv("USERPROFILE");
    if (!home) home = getenv("HOMEDRIVE");
#else
    const char* home = getenv("HOME");
#endif
    if (home) s_cached_home = mem_strdup(home, MEM_CAT_TEMP);
    return s_cached_home;
}

const char* shell_get_temp_dir(void) {
    if (s_cached_temp) return s_cached_temp;
#ifdef _WIN32
    char buf[MAX_PATH + 1];
    DWORD len = GetTempPathA(sizeof(buf), buf);
    if (len > 0 && len < sizeof(buf)) {
        // remove trailing backslash
        if (buf[len - 1] == '\\') buf[len - 1] = '\0';
        s_cached_temp = mem_strdup(buf, MEM_CAT_TEMP);
    }
#else
    const char* tmp = getenv("TMPDIR");
    if (!tmp) tmp = "/tmp";  // TMP_PATH_OK: shell tmpdir fallback when $TMPDIR unset
    s_cached_temp = mem_strdup(tmp, MEM_CAT_TEMP);
#endif
    return s_cached_temp;
}

char* shell_get_hostname(void) {
    char buf[256];
#ifdef _WIN32
    DWORD size = sizeof(buf);
    if (GetComputerNameA(buf, &size)) {
        return mem_strdup(buf, MEM_CAT_TEMP);
    }
#else
    if (gethostname(buf, sizeof(buf)) == 0) {
        buf[sizeof(buf) - 1] = '\0';
        return mem_strdup(buf, MEM_CAT_TEMP);
    }
#endif
    return mem_strdup("localhost", MEM_CAT_TEMP);
}
