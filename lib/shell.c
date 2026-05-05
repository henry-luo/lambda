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
    for (int i = 0; args && args[i]; i++) {
        if (i > 0) strcat(cmdline, " ");
        // simple quoting — wrap each arg in double quotes
        strcat(cmdline, "\"");
        strcat(cmdline, args[i]);
        strcat(cmdline, "\"");
    }

    WinPipe stdout_pipe = {0}, stderr_pipe = {0};
    bool merge = opts && opts->merge_stderr;

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

    STARTUPINFOA si;
    PROCESS_INFORMATION pi;
    memset(&si, 0, sizeof(si));
    memset(&pi, 0, sizeof(pi));
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdOutput = stdout_pipe.write;
    si.hStdError = merge ? stdout_pipe.write : stderr_pipe.write;
    si.hStdInput = GetStdHandle(STD_INPUT_HANDLE);

    // set working directory
    const char* cwd = (opts && opts->cwd) ? opts->cwd : NULL;

    BOOL created = CreateProcessA(NULL, cmdline, NULL, NULL, TRUE,
                                  0, NULL, cwd, &si, &pi);
    // close write ends in parent
    CloseHandle(stdout_pipe.write);
    if (!merge) CloseHandle(stderr_pipe.write);

    if (!created) {
        log_error("shell: CreateProcess failed: %lu", GetLastError());
        CloseHandle(stdout_pipe.read);
        if (!merge) CloseHandle(stderr_pipe.read);
        mem_free(cmdline);
        return result;
    }

    // wait with timeout
    DWORD wait_ms = INFINITE;
    if (opts && opts->timeout_ms > 0) wait_ms = (DWORD)opts->timeout_ms;
    DWORD wait_result = WaitForSingleObject(pi.hProcess, wait_ms);

    if (wait_result == WAIT_TIMEOUT) {
        TerminateProcess(pi.hProcess, 1);
        WaitForSingleObject(pi.hProcess, 1000);
        result.timed_out = true;
    }

    DWORD exit_code = 0;
    GetExitCodeProcess(pi.hProcess, &exit_code);
    result.exit_code = (int)exit_code;

    // read captured output
    result.stdout_buf = win_read_pipe(stdout_pipe.read, &result.stdout_len);
    if (!merge) {
        result.stderr_buf = win_read_pipe(stderr_pipe.read, &result.stderr_len);
    }

    CloseHandle(stdout_pipe.read);
    if (!merge) CloseHandle(stderr_pipe.read);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
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
    bool merge = opts && opts->merge_stderr;

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

    // handle cwd via chdir action
    if (opts && opts->cwd) {
        posix_spawn_file_actions_addchdir_np(&actions, opts->cwd);
    }

    posix_spawnattr_t attr;
    posix_spawnattr_init(&attr);

    pid_t pid;
    // args already has program as argv[0]; cast away const for posix_spawn
    int spawn_err = posix_spawnp(&pid, program, &actions, &attr,
                                 (char* const*)args, environ);

    posix_spawn_file_actions_destroy(&actions);
    posix_spawnattr_destroy(&attr);

    // close write ends in parent
    close(stdout_pipe[1]);
    if (!merge) close(stderr_pipe[1]);

    if (spawn_err != 0) {
        log_error("shell: posix_spawnp failed for '%s': %s", program, strerror(spawn_err));
        close(stdout_pipe[0]);
        if (!merge) close(stderr_pipe[0]);
        return result;
    }

    // wait for child (handle timeout before reading pipes so we don't block)
    int status = 0;
    if (opts && opts->timeout_ms > 0) {
        int elapsed = 0;
        int interval = 10; // ms
        while (elapsed < opts->timeout_ms) {
            int wr = waitpid(pid, &status, WNOHANG);
            if (wr > 0) goto got_status;
            if (wr < 0) {
                log_error("shell: waitpid failed: %s", strerror(errno));
                close(stdout_pipe[0]);
                if (!merge) close(stderr_pipe[0]);
                result.exit_code = -1;
                return result;
            }
            struct timespec ts = {0, interval * 1000000L};
            nanosleep(&ts, NULL);
            elapsed += interval;
        }
        // timeout — kill the child
        kill(pid, SIGTERM);
        struct timespec grace = {0, 100000000L}; // 100ms
        nanosleep(&grace, NULL);
        if (waitpid(pid, &status, WNOHANG) == 0) {
            kill(pid, SIGKILL);
            waitpid(pid, &status, 0);
        }
        result.timed_out = true;
        result.exit_code = WIFEXITED(status) ? WEXITSTATUS(status) : -1;
        close(stdout_pipe[0]);
        if (!merge) close(stderr_pipe[0]);
        return result;
    }

    // no timeout — read all output then wait
    result.stdout_buf = posix_read_fd(stdout_pipe[0], &result.stdout_len);
    close(stdout_pipe[0]);

    if (!merge) {
        result.stderr_buf = posix_read_fd(stderr_pipe[0], &result.stderr_len);
        close(stderr_pipe[0]);
    }

    waitpid(pid, &status, 0);

got_status:
    if (WIFEXITED(status)) {
        result.exit_code = WEXITSTATUS(status);
    } else if (WIFSIGNALED(status)) {
        result.exit_code = 128 + WTERMSIG(status);
    } else {
        result.exit_code = -1;
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
    for (int i = 0; args[i]; i++) {
        if (i > 0) strcat(cmdline, " ");
        strcat(cmdline, "\"");
        strcat(cmdline, args[i]);
        strcat(cmdline, "\"");
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
    if (!tmp) tmp = "/tmp";
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
