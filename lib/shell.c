// shell.c
// Cross-platform shell and process execution utilities

#ifdef __APPLE__
  #define _DARWIN_C_SOURCE
#else
  #define _GNU_SOURCE
#endif

#include "shell.h"
#include "byte_builder.h"
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
  #include <pthread.h>
  #include <limits.h>
  #include <time.h>
  extern char** environ;

  // A child spawned while another thread is between pipe() and posix_spawn()
  // inherits that thread's capture descriptors.  Serialize setup/spawn so
  // parallel test batches cannot retain each other's stdin or output pipes.
  static pthread_mutex_t shell_spawn_mutex = PTHREAD_MUTEX_INITIALIZER;
#endif

// for gethostname
#ifdef _WIN32
  // already included via windows.h
#else
  // gethostname from unistd.h
#endif

extern char* strdup(const char* s);

typedef struct ShellCapture {
    ByteBuilder bytes;
    size_t max_bytes;
    bool limit_hit;
} ShellCapture;

static size_t shell_output_limit(const ShellOptions* opts) {
    return opts && opts->max_output_bytes
        ? opts->max_output_bytes : SHELL_DEFAULT_MAX_OUTPUT_BYTES;
}

static bool shell_capture_init(ShellCapture* capture, size_t max_bytes) {
    memset(capture, 0, sizeof(*capture));
    capture->max_bytes = max_bytes;
    size_t initial_capacity = max_bytes < 4096 ? max_bytes : 4096;
    return byte_builder_init(&capture->bytes, initial_capacity, MEM_CAT_TEMP, true);
}

static bool shell_capture_append(ShellCapture* capture, const char* data, size_t len) {
    if (capture->bytes.length >= capture->max_bytes) {
        capture->limit_hit = capture->limit_hit || len > 0;
        return true;
    }
    size_t available = capture->max_bytes - capture->bytes.length;
    size_t retained = len < available ? len : available;
    if (retained > 0 && !byte_builder_append(&capture->bytes, data, retained)) return false;
    if (retained < len) capture->limit_hit = true;
    return true;
}

static char* shell_capture_take(ShellCapture* capture, size_t* out_len) {
    return (char*)byte_builder_take(&capture->bytes, out_len);
}

static void shell_capture_discard(ShellCapture* capture) {
    byte_builder_destroy(&capture->bytes);
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

static bool win_cmdline_append_repeat(ByteBuilder* command, char value, size_t count) {
    if (!byte_builder_reserve(command, count)) return false;
    uint8_t* tail = byte_builder_writable_tail(command, NULL);
    if (count > 0) memset(tail, value, count);
    return byte_builder_commit(command, count);
}

static bool win_cmdline_append_quoted(ByteBuilder* command, const char* argument) {
    const char quote = '"';
    if (!byte_builder_append(command, &quote, 1)) return false;
    size_t backslashes = 0;
    for (const char* p = argument ? argument : ""; ; p++) {
        if (*p == '\\') {
            backslashes++;
            continue;
        }
        if (*p == '"') {
            if (backslashes > (SIZE_MAX - 1) / 2) return false;
            if (!win_cmdline_append_repeat(command, '\\', backslashes * 2 + 1) ||
                !byte_builder_append(command, p, 1)) return false;
            backslashes = 0;
            continue;
        }
        if (*p == '\0') {
            if (backslashes > SIZE_MAX / 2) return false;
            if (!win_cmdline_append_repeat(command, '\\', backslashes * 2) ||
                !byte_builder_append(command, &quote, 1)) return false;
            return true;
        }
        if (!win_cmdline_append_repeat(command, '\\', backslashes) ||
            !byte_builder_append(command, p, 1)) return false;
        backslashes = 0;
    }
}

static char* win_build_command_line(const char* program, const char** args) {
    ByteBuilder command = {0};
    if (!byte_builder_init(&command, 256, MEM_CAT_TEMP, true) ||
        !win_cmdline_append_quoted(&command, program)) {
        byte_builder_destroy(&command);
        return NULL;
    }
    bool raw_shell_command = (_stricmp(program, "cmd") == 0 ||
                              _stricmp(program, "cmd.exe") == 0) &&
                             args && args[1] && _stricmp(args[1], "/c") == 0;
    for (int i = 1; args && args[i]; i++) {
        const char space = ' ';
        if (!byte_builder_append(&command, &space, 1) ||
            (raw_shell_command && i >= 2
                ? !byte_builder_append(&command, args[i], strlen(args[i]))
                : !win_cmdline_append_quoted(&command, args[i]))) {
            byte_builder_destroy(&command);
            return NULL;
        }
    }
    return (char*)byte_builder_take(&command, NULL);
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

typedef struct {
    STARTUPINFOEXA info;
    LPPROC_THREAD_ATTRIBUTE_LIST attributes;
    HANDLE stdin_copy;
} WinRestrictedStartup;

static bool win_restricted_startup_init(WinRestrictedStartup* startup,
                                        HANDLE stdout_handle, HANDLE stderr_handle,
                                        HANDLE stdin_source) {
    memset(startup, 0, sizeof(*startup));
    startup->stdin_copy = INVALID_HANDLE_VALUE;
    HANDLE process = GetCurrentProcess();
    if (stdin_source == NULL || stdin_source == INVALID_HANDLE_VALUE ||
        !DuplicateHandle(process, stdin_source, process, &startup->stdin_copy,
                         0, TRUE, DUPLICATE_SAME_ACCESS)) {
        startup->stdin_copy = CreateFileA("NUL", GENERIC_READ, FILE_SHARE_READ,
                                          NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
        if (startup->stdin_copy == INVALID_HANDLE_VALUE) return false;
        SetHandleInformation(startup->stdin_copy, HANDLE_FLAG_INHERIT, HANDLE_FLAG_INHERIT);
    }

    SIZE_T attribute_bytes = 0;
    InitializeProcThreadAttributeList(NULL, 1, 0, &attribute_bytes);
    startup->attributes = (LPPROC_THREAD_ATTRIBUTE_LIST)mem_alloc(
        (size_t)attribute_bytes, MEM_CAT_TEMP);
    if (!startup->attributes ||
        !InitializeProcThreadAttributeList(startup->attributes, 1, 0, &attribute_bytes)) {
        if (startup->attributes) mem_free(startup->attributes);
        CloseHandle(startup->stdin_copy);
        return false;
    }

    HANDLE inherited[3];
    SIZE_T inherited_count = 0;
    inherited[inherited_count++] = stdout_handle;
    if (stderr_handle != stdout_handle) inherited[inherited_count++] = stderr_handle;
    inherited[inherited_count++] = startup->stdin_copy;
    if (!UpdateProcThreadAttribute(startup->attributes, 0,
            PROC_THREAD_ATTRIBUTE_HANDLE_LIST, inherited,
            inherited_count * sizeof(HANDLE), NULL, NULL)) {
        DeleteProcThreadAttributeList(startup->attributes);
        mem_free(startup->attributes);
        CloseHandle(startup->stdin_copy);
        return false;
    }

    startup->info.StartupInfo.cb = sizeof(startup->info);
    startup->info.StartupInfo.dwFlags = STARTF_USESTDHANDLES;
    startup->info.StartupInfo.hStdOutput = stdout_handle;
    startup->info.StartupInfo.hStdError = stderr_handle;
    startup->info.StartupInfo.hStdInput = startup->stdin_copy;
    startup->info.lpAttributeList = startup->attributes;
    return true;
}

static void win_restricted_startup_destroy(WinRestrictedStartup* startup) {
    if (startup->attributes) {
        DeleteProcThreadAttributeList(startup->attributes);
        mem_free(startup->attributes);
    }
    if (startup->stdin_copy != INVALID_HANDLE_VALUE) CloseHandle(startup->stdin_copy);
    memset(startup, 0, sizeof(*startup));
}

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

#define SHELL_READER_SHUTDOWN_MS 1000

static void win_finish_capture_thread(HANDLE thread, HANDLE pipe) {
    if (!thread) return;
    if (WaitForSingleObject(thread, SHELL_READER_SHUTDOWN_MS) == WAIT_TIMEOUT) {
        // A descendant outside our job may still own the write end. Cancel the
        // blocking read so a failed job assignment cannot hang the caller.
        CancelSynchronousIo(thread);
        CancelIoEx(pipe, NULL);
    }
    WaitForSingleObject(thread, INFINITE);
}

#else // POSIX

static bool posix_pipe_cloexec(int descriptors[2]) {
    if (pipe(descriptors) != 0) return false;
    if (fcntl(descriptors[0], F_SETFD, FD_CLOEXEC) != 0 ||
        fcntl(descriptors[1], F_SETFD, FD_CLOEXEC) != 0) {
        close(descriptors[0]);
        close(descriptors[1]);
        descriptors[0] = descriptors[1] = -1;
        return false;
    }
    return true;
}

static bool posix_set_nonblocking(int descriptor) {
    int flags = fcntl(descriptor, F_GETFL, 0);
    return flags >= 0 && fcntl(descriptor, F_SETFL, flags | O_NONBLOCK) == 0;
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

static void shell_signal_process(pid_t pid, bool private_group, int signal_number) {
    if (private_group && kill(-pid, signal_number) == 0) return;
    kill(pid, signal_number);
}

#define SHELL_PROCESS_GROUP_GRACE_MS 100

static int64_t shell_elapsed_ms(const struct timespec* started) {
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    return (int64_t)(now.tv_sec - started->tv_sec) * 1000 +
           (now.tv_nsec - started->tv_nsec) / 1000000;
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

    char* cmdline = win_build_command_line(program, args);
    if (!cmdline) {
        log_error("shell: malloc failed for cmdline");
        return result;
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

    size_t output_limit = shell_output_limit(opts);
    if (!shell_capture_init(&stdout_context.capture, output_limit) ||
        (!merge && !shell_capture_init(&stderr_context.capture, output_limit))) {
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

    WinRestrictedStartup startup;
    PROCESS_INFORMATION pi;
    memset(&pi, 0, sizeof(pi));
    HANDLE child_stdin = stdin_handle != INVALID_HANDLE_VALUE
        ? stdin_handle : GetStdHandle(STD_INPUT_HANDLE);
    if (!win_restricted_startup_init(&startup, stdout_pipe.write,
            merge ? stdout_pipe.write : stderr_pipe.write, child_stdin)) {
        log_error("shell: restricted startup initialization failed: %lu", GetLastError());
        shell_capture_discard(&stdout_context.capture);
        shell_capture_discard(&stderr_context.capture);
        CloseHandle(stdout_pipe.read);
        CloseHandle(stdout_pipe.write);
        if (!merge) { CloseHandle(stderr_pipe.read); CloseHandle(stderr_pipe.write); }
        if (stdin_handle != INVALID_HANDLE_VALUE) CloseHandle(stdin_handle);
        mem_free(cmdline);
        return result;
    }

    // set working directory
    const char* cwd = (opts && opts->cwd) ? opts->cwd : NULL;
    char* child_env = win_build_env_block(opts ? opts->env : NULL);
    if (opts && opts->env && !child_env) {
        log_error("shell: child environment allocation failed");
        win_restricted_startup_destroy(&startup);
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
    DWORD creation_flags = CREATE_SUSPENDED | CREATE_NEW_PROCESS_GROUP |
                           EXTENDED_STARTUPINFO_PRESENT;
    BOOL created = CreateProcessA(program, cmdline, NULL, NULL, TRUE,
                                  creation_flags, child_env, cwd,
                                  &startup.info.StartupInfo, &pi);
    win_restricted_startup_destroy(&startup);
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
    if (ResumeThread(pi.hThread) == (DWORD)-1) {
        log_error("SHELL-RESUME-WIN32: failed to resume child: %lu", GetLastError());
        if (job) TerminateJobObject(job, 1);
        else TerminateProcess(pi.hProcess, 1);
    }

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
    } else if (wait_result == WAIT_FAILED) {
        log_error("SHELL-WAIT-WIN32: process wait failed: %lu", GetLastError());
        if (job) TerminateJobObject(job, 1);
        else TerminateProcess(pi.hProcess, 1);
        WaitForSingleObject(pi.hProcess, 1000);
    }

    DWORD exit_code = 0;
    GetExitCodeProcess(pi.hProcess, &exit_code);
    result.exit_code = (int)exit_code;

    // Closing a kill-on-close job after the root exits prevents descendants
    // from retaining capture handles and blocking the reader threads forever.
    if (job) {
        CloseHandle(job);
        job = NULL;
    }
    win_finish_capture_thread(stdout_thread, stdout_pipe.read);
    win_finish_capture_thread(stderr_thread, stderr_pipe.read);
    result.stdout_buf = shell_capture_take(&stdout_context.capture, &result.stdout_len);
    if (!merge) {
        result.stderr_buf = shell_capture_take(&stderr_context.capture, &result.stderr_len);
    }
    result.output_limit_exceeded = stdout_context.capture.limit_hit ||
                                   stderr_context.capture.limit_hit;

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

    pthread_mutex_lock(&shell_spawn_mutex);
    if (!posix_pipe_cloexec(stdout_pipe)) {
        log_error("shell: pipe() failed: %s", strerror(errno));
        pthread_mutex_unlock(&shell_spawn_mutex);
        return result;
    }
    if (!merge && !posix_pipe_cloexec(stderr_pipe)) {
        log_error("shell: pipe() failed: %s", strerror(errno));
        close(stdout_pipe[0]);
        close(stdout_pipe[1]);
        pthread_mutex_unlock(&shell_spawn_mutex);
        return result;
    }
    if (!posix_set_nonblocking(stdout_pipe[0]) ||
        (!merge && !posix_set_nonblocking(stderr_pipe[0]))) {
        log_error("SHELL-NONBLOCKING-PIPE: fcntl failed: %s", strerror(errno));
        close(stdout_pipe[0]);
        close(stdout_pipe[1]);
        if (!merge) {
            close(stderr_pipe[0]);
            close(stderr_pipe[1]);
        }
        pthread_mutex_unlock(&shell_spawn_mutex);
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
            pthread_mutex_unlock(&shell_spawn_mutex);
            return result;
        }
    }
    size_t output_limit = shell_output_limit(opts);
    if (!shell_capture_init(&stdout_capture, output_limit) ||
        (!merge && !shell_capture_init(&stderr_capture, output_limit))) {
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
        pthread_mutex_unlock(&shell_spawn_mutex);
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
    bool private_group = false;
#ifdef POSIX_SPAWN_SETPGROUP
    // Give each launch a private group so cleanup cannot leave descendants running.
    if (posix_spawnattr_setpgroup(&attr, 0) == 0) {
        spawn_flags |= POSIX_SPAWN_SETPGROUP;
        private_group = posix_spawnattr_setflags(&attr, spawn_flags) == 0;
    }
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
        pthread_mutex_unlock(&shell_spawn_mutex);
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
    pthread_mutex_unlock(&shell_spawn_mutex);

    if (spawn_err != 0) {
        log_error("shell: posix_spawnp failed for '%s': %s", program, strerror(spawn_err));
        close(stdout_pipe[0]);
        if (!merge) close(stderr_pipe[0]);
        shell_capture_discard(&stdout_capture);
        shell_capture_discard(&stderr_capture);
        return result;
    }

    // Drain both pipes while the child runs; waiting first can deadlock once a pipe fills.
    bool stdout_open = true;
    bool stderr_open = !merge;
    bool child_done = false;
    bool wait_failed = false;
    bool terminate_sent = false;
    int64_t termination_started_ms = -1;
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
                wait_failed = true;
            }
        }

        int64_t elapsed_ms = shell_elapsed_ms(&started);
        bool pipes_open = stdout_open || stderr_open;
        bool timed_out = opts && opts->timeout_ms > 0 &&
                         elapsed_ms >= opts->timeout_ms;
        if ((timed_out || (child_done && pipes_open)) && !terminate_sent) {
            // The private group owns descendants that could otherwise keep a
            // capture pipe open after the launched process has completed.
            shell_signal_process(pid, private_group, SIGTERM);
            terminate_sent = true;
            termination_started_ms = elapsed_ms;
        }
        if (terminate_sent && pipes_open &&
            elapsed_ms >= termination_started_ms + SHELL_PROCESS_GROUP_GRACE_MS) {
            shell_signal_process(pid, private_group, SIGKILL);
        }
        if (timed_out) result.timed_out = true;
    }

    if (!child_done) {
        while (waitpid(pid, &status, 0) < 0 && errno == EINTR) {}
    }
    if (wait_failed) {
        result.exit_code = -1;
    } else if (WIFEXITED(status)) {
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
    result.output_limit_exceeded = stdout_capture.limit_hit || stderr_capture.limit_hit;

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
    HANDLE job;
    HANDLE stdout_read;
    HANDLE stderr_read;
#else
    pid_t pid;
    int stdout_fd;
    int stderr_fd;
    bool private_group;
#endif
    bool finished;
    int exit_code;
    bool merge_stderr;
    size_t output_limit;
};

#ifdef _WIN32

ShellProcess* shell_spawn(const char* program, const char** args,
                           const ShellOptions* opts) {
    if (!program || !args) return NULL;

    ShellProcess* proc = (ShellProcess*)mem_calloc(1, sizeof(ShellProcess), MEM_CAT_TEMP);
    if (!proc) return NULL;

    bool merge = opts && opts->merge_stderr;
    proc->merge_stderr = merge;
    proc->output_limit = shell_output_limit(opts);

    char* cmdline = win_build_command_line(program, args);
    if (!cmdline) { mem_free(proc); return NULL; }

    WinPipe stdout_pipe = {0}, stderr_pipe = {0};
    if (!win_create_pipe(&stdout_pipe)) { mem_free(cmdline); mem_free(proc); return NULL; }
    if (!merge && !win_create_pipe(&stderr_pipe)) {
        CloseHandle(stdout_pipe.read); CloseHandle(stdout_pipe.write);
        mem_free(cmdline); mem_free(proc); return NULL;
    }
    SetHandleInformation(stdout_pipe.read, HANDLE_FLAG_INHERIT, 0);
    if (!merge) SetHandleInformation(stderr_pipe.read, HANDLE_FLAG_INHERIT, 0);

    WinRestrictedStartup startup;
    PROCESS_INFORMATION pi;
    memset(&pi, 0, sizeof(pi));
    if (!win_restricted_startup_init(&startup, stdout_pipe.write,
            merge ? stdout_pipe.write : stderr_pipe.write,
            GetStdHandle(STD_INPUT_HANDLE))) {
        CloseHandle(stdout_pipe.read); CloseHandle(stdout_pipe.write);
        if (!merge) { CloseHandle(stderr_pipe.read); CloseHandle(stderr_pipe.write); }
        mem_free(cmdline); mem_free(proc);
        return NULL;
    }

    HANDLE job = CreateJobObjectA(NULL, NULL);
    if (job) {
        JOBOBJECT_EXTENDED_LIMIT_INFORMATION limit = {0};
        limit.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
        if (!SetInformationJobObject(job, JobObjectExtendedLimitInformation,
                                     &limit, sizeof(limit))) {
            CloseHandle(job);
            job = NULL;
        }
    }

    const char* cwd = (opts && opts->cwd) ? opts->cwd : NULL;
    BOOL ok = CreateProcessA(program, cmdline, NULL, NULL, TRUE,
                             CREATE_SUSPENDED | CREATE_NEW_PROCESS_GROUP |
                             EXTENDED_STARTUPINFO_PRESENT, NULL, cwd,
                             &startup.info.StartupInfo, &pi);
    win_restricted_startup_destroy(&startup);
    CloseHandle(stdout_pipe.write);
    if (!merge) CloseHandle(stderr_pipe.write);
    mem_free(cmdline);

    if (!ok) {
        log_error("shell_spawn: CreateProcess failed: %lu", GetLastError());
        CloseHandle(stdout_pipe.read);
        if (!merge) CloseHandle(stderr_pipe.read);
        if (job) CloseHandle(job);
        mem_free(proc);
        return NULL;
    }
    if (job && !AssignProcessToJobObject(job, pi.hProcess)) {
        CloseHandle(job);
        job = NULL;
    }
    if (ResumeThread(pi.hThread) == (DWORD)-1) {
        if (job) TerminateJobObject(job, 1);
        else TerminateProcess(pi.hProcess, 1);
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
        CloseHandle(stdout_pipe.read);
        if (!merge) CloseHandle(stderr_pipe.read);
        if (job) CloseHandle(job);
        mem_free(proc);
        return NULL;
    }
    proc->hProcess = pi.hProcess;
    proc->hThread = pi.hThread;
    proc->job = job;
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

    WinCaptureThread stdout_context = {0};
    WinCaptureThread stderr_context = {0};
    if (!shell_capture_init(&stdout_context.capture, proc->output_limit) ||
        (!proc->merge_stderr &&
         !shell_capture_init(&stderr_context.capture, proc->output_limit))) {
        shell_capture_discard(&stdout_context.capture);
        shell_capture_discard(&stderr_context.capture);
        return result;
    }
    stdout_context.pipe = proc->stdout_read;
    HANDLE stdout_thread = CreateThread(NULL, 0, win_capture_thread_main,
                                        &stdout_context, 0, NULL);
    HANDLE stderr_thread = NULL;
    if (!proc->merge_stderr) {
        stderr_context.pipe = proc->stderr_read;
        stderr_thread = CreateThread(NULL, 0, win_capture_thread_main,
                                     &stderr_context, 0, NULL);
    }
    if (!stdout_thread || (!proc->merge_stderr && !stderr_thread)) {
        if (proc->job) TerminateJobObject(proc->job, 1);
        else TerminateProcess(proc->hProcess, 1);
    }

    DWORD wait_ms = (timeout_ms > 0) ? (DWORD)timeout_ms : INFINITE;
    DWORD wr = WaitForSingleObject(proc->hProcess, wait_ms);
    if (wr == WAIT_TIMEOUT) {
        if (proc->job) TerminateJobObject(proc->job, 1);
        else TerminateProcess(proc->hProcess, 1);
        WaitForSingleObject(proc->hProcess, 1000);
        result.timed_out = true;
    } else if (wr == WAIT_FAILED) {
        log_error("SHELL-BACKGROUND-WAIT-WIN32: process wait failed: %lu", GetLastError());
        if (proc->job) TerminateJobObject(proc->job, 1);
        else TerminateProcess(proc->hProcess, 1);
        WaitForSingleObject(proc->hProcess, 1000);
    }
    DWORD code;
    GetExitCodeProcess(proc->hProcess, &code);
    result.exit_code = (int)code;
    proc->finished = true;
    proc->exit_code = result.exit_code;

    if (proc->job) {
        CloseHandle(proc->job);
        proc->job = NULL;
    }
    win_finish_capture_thread(stdout_thread, proc->stdout_read);
    win_finish_capture_thread(stderr_thread, proc->stderr_read);
    result.stdout_buf = shell_capture_take(&stdout_context.capture, &result.stdout_len);
    if (!proc->merge_stderr) {
        result.stderr_buf = shell_capture_take(&stderr_context.capture, &result.stderr_len);
    }
    result.output_limit_exceeded = stdout_context.capture.limit_hit ||
                                   stderr_context.capture.limit_hit;
    if (stdout_thread) CloseHandle(stdout_thread);
    if (stderr_thread) CloseHandle(stderr_thread);
    return result;
}

bool shell_process_kill(ShellProcess* proc) {
    if (!proc || proc->finished) return false;
    if (proc->job) return TerminateJobObject(proc->job, 1) != 0;
    return TerminateProcess(proc->hProcess, 1) != 0;
}

void shell_process_free(ShellProcess* proc) {
    if (!proc) return;
    CloseHandle(proc->hProcess);
    CloseHandle(proc->hThread);
    if (proc->job) CloseHandle(proc->job);
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
    proc->output_limit = shell_output_limit(opts);

    int stdout_pipe[2] = {-1, -1};
    int stderr_pipe[2] = {-1, -1};

    pthread_mutex_lock(&shell_spawn_mutex);
    if (!posix_pipe_cloexec(stdout_pipe)) {
        log_error("shell_spawn: pipe() failed: %s", strerror(errno));
        pthread_mutex_unlock(&shell_spawn_mutex);
        mem_free(proc);
        return NULL;
    }
    if (!merge && !posix_pipe_cloexec(stderr_pipe)) {
        log_error("shell_spawn: pipe() failed: %s", strerror(errno));
        close(stdout_pipe[0]); close(stdout_pipe[1]);
        pthread_mutex_unlock(&shell_spawn_mutex);
        mem_free(proc);
        return NULL;
    }
    if (!posix_set_nonblocking(stdout_pipe[0]) ||
        (!merge && !posix_set_nonblocking(stderr_pipe[0]))) {
        log_error("SHELL-BACKGROUND-NONBLOCKING-PIPE: fcntl failed: %s", strerror(errno));
        close(stdout_pipe[0]); close(stdout_pipe[1]);
        if (!merge) { close(stderr_pipe[0]); close(stderr_pipe[1]); }
        pthread_mutex_unlock(&shell_spawn_mutex);
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
    short spawn_flags = 0;
    bool private_group = false;
#ifdef POSIX_SPAWN_SETPGROUP
    if (posix_spawnattr_setpgroup(&attr, 0) == 0) {
        spawn_flags |= POSIX_SPAWN_SETPGROUP;
        private_group = posix_spawnattr_setflags(&attr, spawn_flags) == 0;
    }
#endif
    pid_t pid;
    int err = posix_spawnp(&pid, program, &actions, &attr,
                           (char* const*)args, environ);

    posix_spawn_file_actions_destroy(&actions);
    posix_spawnattr_destroy(&attr);

    close(stdout_pipe[1]);
    if (!merge) close(stderr_pipe[1]);
    pthread_mutex_unlock(&shell_spawn_mutex);

    if (err != 0) {
        log_error("shell_spawn: posix_spawnp failed for '%s': %s", program, strerror(err));
        close(stdout_pipe[0]);
        if (!merge) close(stderr_pipe[0]);
        mem_free(proc);
        return NULL;
    }

    proc->pid = pid;
    proc->private_group = private_group;
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
    if (wr < 0 && errno != EINTR) {
        log_error("SHELL-BACKGROUND-POLL: waitpid failed: %s", strerror(errno));
        proc->finished = true;
        proc->exit_code = -1;
        return true;
    }
    return false;
}

ShellResult shell_process_wait(ShellProcess* proc, int timeout_ms) {
    ShellResult result = {0};
    result.exit_code = -1;
    if (!proc) return result;

    ShellCapture stdout_capture = {0};
    ShellCapture stderr_capture = {0};
    if (!shell_capture_init(&stdout_capture, proc->output_limit) ||
        (!proc->merge_stderr &&
         !shell_capture_init(&stderr_capture, proc->output_limit))) {
        shell_capture_discard(&stdout_capture);
        shell_capture_discard(&stderr_capture);
        return result;
    }

    bool stdout_open = proc->stdout_fd >= 0;
    bool stderr_open = proc->stderr_fd >= 0;
    bool child_done = proc->finished;
    bool terminate_sent = false;
    int64_t termination_started_ms = -1;
    struct timespec started;
    clock_gettime(CLOCK_MONOTONIC, &started);

    while (!child_done || stdout_open || stderr_open) {
        struct pollfd fds[2];
        nfds_t nfds = 0;
        if (stdout_open) fds[nfds++] = (struct pollfd){proc->stdout_fd, POLLIN | POLLHUP, 0};
        if (stderr_open) fds[nfds++] = (struct pollfd){proc->stderr_fd, POLLIN | POLLHUP, 0};
        if (nfds > 0) poll(fds, nfds, 10);

        for (nfds_t i = 0; i < nfds; i++) {
            if (!(fds[i].revents & (POLLIN | POLLHUP | POLLERR | POLLNVAL))) continue;
            bool is_stdout = fds[i].fd == proc->stdout_fd;
            ShellCapture* capture = is_stdout ? &stdout_capture : &stderr_capture;
            bool* open_flag = is_stdout ? &stdout_open : &stderr_open;
            for (;;) {
                char buffer[4096];
                ssize_t count = read(fds[i].fd, buffer, sizeof(buffer));
                if (count > 0) {
                    if (!shell_capture_append(capture, buffer, (size_t)count)) {
                        log_error("SHELL-BACKGROUND-CAPTURE: allocation failed");
                        close(fds[i].fd);
                        *open_flag = false;
                    }
                    if (*open_flag) continue;
                } else if (count == 0 || (count < 0 && errno != EAGAIN && errno != EINTR)) {
                    close(fds[i].fd);
                    *open_flag = false;
                }
                break;
            }
        }

        if (!child_done) {
            int status = 0;
            int waited = waitpid(proc->pid, &status, WNOHANG);
            if (waited == proc->pid) {
                child_done = true;
                proc->finished = true;
                proc->exit_code = WIFEXITED(status) ? WEXITSTATUS(status)
                                : (WIFSIGNALED(status) ? 128 + WTERMSIG(status) : -1);
            } else if (waited < 0 && errno != EINTR) {
                // A failed reap cannot become ready later; keep draining any inherited pipe data.
                log_error("SHELL-BACKGROUND-WAIT: waitpid failed: %s", strerror(errno));
                child_done = true;
                proc->finished = true;
                proc->exit_code = -1;
            }
        }

        int64_t elapsed_ms = shell_elapsed_ms(&started);
        bool pipes_open = stdout_open || stderr_open;
        bool timed_out = timeout_ms > 0 && elapsed_ms >= timeout_ms;
        if ((timed_out || (child_done && pipes_open)) && !terminate_sent) {
            shell_signal_process(proc->pid, proc->private_group, SIGTERM);
            terminate_sent = true;
            termination_started_ms = elapsed_ms;
        }
        if (terminate_sent && pipes_open &&
            elapsed_ms >= termination_started_ms + SHELL_PROCESS_GROUP_GRACE_MS) {
            shell_signal_process(proc->pid, proc->private_group, SIGKILL);
        }
        if (timed_out) result.timed_out = true;
    }

    proc->stdout_fd = -1;
    proc->stderr_fd = -1;
    result.exit_code = proc->exit_code;
    result.stdout_buf = shell_capture_take(&stdout_capture, &result.stdout_len);
    if (!proc->merge_stderr) {
        result.stderr_buf = shell_capture_take(&stderr_capture, &result.stderr_len);
    }
    result.output_limit_exceeded = stdout_capture.limit_hit || stderr_capture.limit_hit;

    return result;
}

bool shell_process_kill(ShellProcess* proc) {
    if (!proc || proc->finished) return false;
    if (proc->private_group && kill(-proc->pid, SIGTERM) == 0) return true;
    return (!proc->private_group || errno == ESRCH) && kill(proc->pid, SIGTERM) == 0;
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
