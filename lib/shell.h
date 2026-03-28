// shell.h
// Cross-platform shell and process execution utilities
//
// Provides unified command execution, environment variable access,
// and process management for all Lambda runtime backends.

#ifndef SHELL_H
#define SHELL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// --- Result of a completed shell command ---
typedef struct {
    int exit_code;          // process exit code (0 = success)
    char* stdout_buf;       // captured stdout (caller frees via shell_result_free)
    size_t stdout_len;      // length of stdout
    char* stderr_buf;       // captured stderr (caller frees via shell_result_free)
    size_t stderr_len;      // length of stderr
    bool timed_out;         // true if killed by timeout
} ShellResult;

// --- Opaque handle for a background process ---
typedef struct ShellProcess ShellProcess;

// --- Callback for streaming line-by-line output ---
// Return false to abort the process early.
typedef bool (*ShellLineCallback)(const char* line, size_t len, void* user_data);

// --- Environment variable pair ---
typedef struct {
    const char* key;
    const char* value;
} ShellEnvEntry;

// --- Options for shell_exec / shell_spawn ---
typedef struct {
    const char* cwd;                // working directory (NULL = inherit)
    const ShellEnvEntry* env;       // extra env vars (NULL-terminated array, NULL = inherit all)
    int timeout_ms;                 // timeout in ms (0 = no timeout)
    bool merge_stderr;              // merge stderr into stdout
    ShellLineCallback on_stdout;    // streaming stdout callback (NULL = buffer all)
    ShellLineCallback on_stderr;    // streaming stderr callback (NULL = buffer all)
    void* user_data;                // passed to callbacks
} ShellOptions;

// ---------------------------------------------------------------------------
// Core synchronous execution
// ---------------------------------------------------------------------------

// Run a command synchronously, capturing stdout and stderr.
// `program` is resolved via PATH. `args` is NULL-terminated array starting
// with the program name (argv[0]). Pass NULL opts for defaults.
ShellResult shell_exec(const char* program, const char** args, const ShellOptions* opts);

// Convenience: run with default options (no timeout, inherit cwd/env).
ShellResult shell_exec_simple(const char* program, const char** args);

// Execute a shell command string via /bin/sh -c (or cmd /c on Windows).
// WARNING: only for trusted internal commands — subject to shell expansion.
ShellResult shell_exec_line(const char* cmdline, const ShellOptions* opts);

// ---------------------------------------------------------------------------
// Background (async) processes
// ---------------------------------------------------------------------------

// Launch a process asynchronously. Returns handle (NULL on failure).
ShellProcess* shell_spawn(const char* program, const char** args, const ShellOptions* opts);

// Check if the background process has exited (non-blocking).
bool shell_process_poll(ShellProcess* proc);

// Wait for exit or timeout. Returns result with captured output.
ShellResult shell_process_wait(ShellProcess* proc, int timeout_ms);

// Send termination signal (SIGTERM / TerminateProcess).
bool shell_process_kill(ShellProcess* proc);

// Free the process handle and any buffered output.
void shell_process_free(ShellProcess* proc);

// ---------------------------------------------------------------------------
// Environment variables
// ---------------------------------------------------------------------------

const char* shell_getenv(const char* name);
bool        shell_setenv(const char* name, const char* value);
bool        shell_unsetenv(const char* name);

// ---------------------------------------------------------------------------
// Utilities
// ---------------------------------------------------------------------------

// Resolve program name to absolute path via PATH lookup.
// Returns malloc'd string (caller must free), or NULL if not found.
char* shell_which(const char* program);

// Shell-escape a single argument (POSIX single-quoting).
// Returns malloc'd string (caller must free).
char* shell_quote_arg(const char* arg);

// Free stdout_buf and stderr_buf inside a ShellResult.
void shell_result_free(ShellResult* result);

// Return user home directory (cached after first call, do not free).
const char* shell_get_home_dir(void);

// Return system temp directory (cached after first call, do not free).
const char* shell_get_temp_dir(void);

// Return hostname. Returns malloc'd string (caller must free).
char* shell_get_hostname(void);

#ifdef __cplusplus
}
#endif

#endif // SHELL_H
