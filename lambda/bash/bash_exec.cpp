// bash_exec.cpp — Exec Engine (Phase E — Module 8)
//
// Implements the exec builtin, persistent fd redirections,
// variable-target fd allocation, and sub-script execution.

#include "bash_exec.h"
#include "bash_runtime.h"
#include "bash_errors.h"
#include "../lambda-data.hpp"
#include "../transpiler.hpp"
#include "../../lib/log.h"
#include "../../lib/strbuf.h"

#include <cstring>
#include <cerrno>
#include <fcntl.h>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/stat.h>

// ============================================================================
// Persistent fd state
// ============================================================================

#define EXEC_MAX_FDS 64
#define EXEC_VARFD_BASE 10  // bash convention: variable fds start at 10

typedef struct ExecFdEntry {
    int fd;             // the fd number
    int original_fd;    // original fd (if it was open before) or -1
    int active;         // 1 if this entry is in use
} ExecFdEntry;

static ExecFdEntry exec_fd_table[EXEC_MAX_FDS];
static int exec_fd_count = 0;
static int exec_next_varfd = EXEC_VARFD_BASE;

// find an entry for the given fd, or NULL
static ExecFdEntry* exec_find_fd(int fd) {
    for (int i = 0; i < exec_fd_count; i++) {
        if (exec_fd_table[i].active && exec_fd_table[i].fd == fd) {
            return &exec_fd_table[i];
        }
    }
    return NULL;
}

// record a persistent fd redirect
static ExecFdEntry* exec_track_fd(int fd) {
    ExecFdEntry* entry = exec_find_fd(fd);
    if (entry) return entry;

    if (exec_fd_count >= EXEC_MAX_FDS) {
        log_error("bash_exec: persistent fd table full (max %d)", EXEC_MAX_FDS);
        return NULL;
    }

    entry = &exec_fd_table[exec_fd_count++];
    entry->fd = fd;
    // save original if it was open
    entry->original_fd = fcntl(fd, F_DUPFD, 100);  // save to high fd
    entry->active = 1;
    return entry;
}

// ============================================================================
// Persistent fd redirections
// ============================================================================

extern "C" void bash_exec_redir_open(int fd, const char* path, int open_flags, int open_mode) {
    if (!path) {
        log_error("bash_exec_redir_open: null path for fd %d", fd);
        return;
    }

    int real_fd = open(path, open_flags, open_mode);
    if (real_fd < 0) {
        bash_errmsg("exec: %d: %s: %s", fd, path, strerror(errno));
        return;
    }

    exec_track_fd(fd);

    if (real_fd != fd) {
        if (dup2(real_fd, fd) < 0) {
            bash_errmsg("exec: dup2(%d, %d): %s", real_fd, fd, strerror(errno));
            close(real_fd);
            return;
        }
        close(real_fd);
    }

    log_debug("bash_exec_redir_open: fd %d -> %s (flags=0x%x)", fd, path, open_flags);
}

extern "C" void bash_exec_redir_dup(int new_fd, int old_fd) {
    exec_track_fd(new_fd);

    if (dup2(old_fd, new_fd) < 0) {
        bash_errmsg("exec: %d: %s", new_fd, strerror(errno));
        return;
    }

    log_debug("bash_exec_redir_dup: fd %d -> fd %d", new_fd, old_fd);
}

extern "C" void bash_exec_redir_close(int fd) {
    ExecFdEntry* entry = exec_find_fd(fd);
    if (entry) {
        entry->active = 0;
    }

    close(fd);
    log_debug("bash_exec_redir_close: fd %d", fd);
}

extern "C" int bash_exec_redir_varfd(Item var_name, const char* path, int open_flags, int open_mode) {
    if (!path) {
        log_error("bash_exec_redir_varfd: null path");
        return -1;
    }

    int real_fd = open(path, open_flags, open_mode);
    if (real_fd < 0) {
        bash_errmsg("exec: %s: %s", path, strerror(errno));
        return -1;
    }

    // find an available fd >= 10
    int target_fd = exec_next_varfd;
    while (target_fd < 256) {
        if (fcntl(target_fd, F_GETFD) < 0) {
            break;  // fd is not in use
        }
        target_fd++;
    }

    if (target_fd >= 256) {
        bash_errmsg("exec: no available file descriptors");
        close(real_fd);
        return -1;
    }

    if (real_fd != target_fd) {
        if (dup2(real_fd, target_fd) < 0) {
            bash_errmsg("exec: dup2(%d, %d): %s", real_fd, target_fd, strerror(errno));
            close(real_fd);
            return -1;
        }
        close(real_fd);
    }

    exec_track_fd(target_fd);
    exec_next_varfd = target_fd + 1;

    // store the fd number in the named variable
    char fd_str[16];
    int len = snprintf(fd_str, sizeof(fd_str), "%d", target_fd);
    Item name_str = bash_to_string(var_name);
    String* ns = it2s(name_str);
    if (ns) {
        Item val = (Item){.item = s2it(heap_create_name(fd_str, len))};
        bash_set_var(name_str, val);
    }

    log_debug("bash_exec_redir_varfd: {%s} = fd %d -> %s", ns ? ns->chars : "?", target_fd, path);
    return target_fd;
}

// ============================================================================
// Persistent fd state management
// ============================================================================

extern "C" void bash_exec_redir_restore_all(void) {
    for (int i = exec_fd_count - 1; i >= 0; i--) {
        ExecFdEntry* entry = &exec_fd_table[i];
        if (!entry->active) continue;

        if (entry->original_fd >= 0) {
            dup2(entry->original_fd, entry->fd);
            close(entry->original_fd);
        } else {
            close(entry->fd);
        }
        entry->active = 0;
    }
    exec_fd_count = 0;
    exec_next_varfd = EXEC_VARFD_BASE;
}

extern "C" int bash_exec_redir_is_active(int fd) {
    ExecFdEntry* entry = exec_find_fd(fd);
    return entry ? 1 : 0;
}

// ============================================================================
// exec builtin
// ============================================================================

extern "C" int bash_exec_builtin(Item* args, int argc, int flags, const char* argv0_override) {
    if (argc == 0) {
        // redirect-only exec — redirections were already applied by caller
        log_debug("bash_exec_builtin: redirect-only (no command)");
        return 0;
    }

    // build argv for execvp
    const char* exec_argv[256];
    int exec_argc = 0;

    for (int i = 0; i < argc && exec_argc < 255; i++) {
        Item s = bash_to_string(args[i]);
        String* str = it2s(s);
        if (str) {
            exec_argv[exec_argc++] = str->chars;
        }
    }
    exec_argv[exec_argc] = NULL;

    if (exec_argc == 0) return 1;

    // handle -a flag (set argv[0])
    if ((flags & BASH_EXEC_ARGV0) && argv0_override) {
        exec_argv[0] = argv0_override;
    }

    // handle -l flag (login shell: dash before argv[0])
    char login_name[512];
    if (flags & BASH_EXEC_LOGIN) {
        snprintf(login_name, sizeof(login_name), "-%s", exec_argv[0]);
        exec_argv[0] = login_name;
    }

    // handle -c flag (clear environment)
    if (flags & BASH_EXEC_CLEAN_ENV) {
        // clear environ except for _ and PATH
        extern char** environ;
        environ = NULL;
    }

    // flush output before replacing process
    fflush(stdout);
    fflush(stderr);

    log_debug("bash_exec_builtin: execvp(%s, ...)", exec_argv[0]);

    // resolve command path
    const char* cmd = exec_argv[0];
    if (strchr(cmd, '/')) {
        execv(cmd, (char* const*)exec_argv);
    } else {
        execvp(cmd, (char* const*)exec_argv);
    }

    // if we get here, exec failed
    int err = errno;
    bash_errmsg("exec: %s: %s", cmd, strerror(err));
    return (err == ENOENT) ? 127 : 126;
}

// ============================================================================
// Sub-script execution
// ============================================================================

extern "C" int bash_exec_subscript(const char* interpreter, const char* script_path,
                                    const char** extra_args, int extra_argc) {
    if (!interpreter || !script_path) {
        log_error("bash_exec_subscript: null interpreter or script_path");
        return 127;
    }

    // build argv: [interpreter, "bash", script_path, extra_args..., NULL]
    const char* argv[258];
    int ai = 0;
    argv[ai++] = interpreter;
    argv[ai++] = "bash";
    argv[ai++] = script_path;
    for (int i = 0; i < extra_argc && ai < 257; i++) {
        argv[ai++] = extra_args[i];
    }
    argv[ai] = NULL;

    // sync exported variables to environ
    // (bash_env_sync_export is called per-variable, but we rely on the
    //  current environ being up-to-date from previous export calls)

    fflush(stdout);
    fflush(stderr);

    pid_t pid = fork();
    if (pid < 0) {
        bash_errmsg("%s: fork: %s", script_path, strerror(errno));
        return 127;
    }

    if (pid == 0) {
        // child process
        execvp(interpreter, (char* const*)argv);
        // exec failed
        fprintf(stderr, "%s: %s: %s\n", script_path, interpreter, strerror(errno));
        _exit(127);
    }

    // parent: wait for child
    int status = 0;
    waitpid(pid, &status, 0);

    if (WIFEXITED(status)) {
        int code = WEXITSTATUS(status);
        log_debug("bash_exec_subscript: %s %s exited with %d", interpreter, script_path, code);
        return code;
    }

    if (WIFSIGNALED(status)) {
        int sig = WTERMSIG(status);
        log_debug("bash_exec_subscript: %s %s killed by signal %d", interpreter, script_path, sig);
        return 128 + sig;
    }

    return 1;
}

// ============================================================================
// Module lifecycle
// ============================================================================

extern "C" void bash_exec_init(void) {
    exec_fd_count = 0;
    exec_next_varfd = EXEC_VARFD_BASE;
    memset(exec_fd_table, 0, sizeof(exec_fd_table));
}

extern "C" void bash_exec_cleanup(void) {
    bash_exec_redir_restore_all();
}
