# Module IO: Shell (`lib/shell.c`)

## Overview

A common C-level shell and process module (`lib/shell.c` + `lib/shell.h`) providing unified CLI and process-execution utilities for all Lambda runtime targets: **Lambda Script**, **JavaScript**, **Python**, and **Bash** transpilation.

Currently, shell-related operations are scattered:
- Bash transpiler uses `posix_spawn` directly in `lambda/bash/`
- Test harness has ad-hoc `popen()` wrappers (`test/test_html_roundtrip_gtest.cpp`)
- Environment variable access uses raw `getenv()`/`setenv()` calls with no abstraction
- No unified way to capture command output, check exit codes, or manage subprocesses

This module consolidates all process and shell interaction into a single, cross-platform C library that every runtime backend can call.

## Design Principles

1. **Cross-platform** — abstracts POSIX (`posix_spawn`, `fork/exec`, `pipe`) and Win32 (`CreateProcess`) behind a single API
2. **Zero std:: dependency** — pure C with Lambda `lib/` types (`StrBuf`, `ArrayList`)
3. **Capture-first** — every execution function returns structured results (exit code + stdout + stderr)
4. **Safe by default** — no shell injection; command + args passed as arrays, not interpolated strings
5. **Streaming support** — large output can be consumed line-by-line via callbacks
6. **Non-blocking option** — background process launch with handle-based status polling
7. **Consistent error reporting** — all functions return a result struct; errors logged via `log_error()`

## Data Structures

```c
// lib/shell.h

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
    char* stdout_buf;       // captured stdout (caller must free)
    size_t stdout_len;      // length of stdout
    char* stderr_buf;       // captured stderr (caller must free)
    size_t stderr_len;      // length of stderr
    bool timed_out;         // true if killed by timeout
} ShellResult;

// --- Handle for a background process ---
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
    const ShellEnvEntry* env;       // extra env vars (NULL-terminated array, NULL = inherit)
    int timeout_ms;                 // timeout in ms (0 = no timeout)
    bool merge_stderr;              // merge stderr into stdout
    ShellLineCallback on_stdout;    // streaming stdout callback (NULL = buffer all)
    ShellLineCallback on_stderr;    // streaming stderr callback (NULL = buffer all)
    void* user_data;                // passed to callbacks
} ShellOptions;

#ifdef __cplusplus
}
#endif

#endif // SHELL_H
```

## API Surface

### Core Execution

| Function | Signature | Description |
|----------|-----------|-------------|
| `shell_exec` | `ShellResult shell_exec(const char* program, const char** args, const ShellOptions* opts)` | Run command synchronously, capture output |
| `shell_exec_simple` | `ShellResult shell_exec_simple(const char* program, const char** args)` | Convenience wrapper with default options |
| `shell_exec_line` | `ShellResult shell_exec_line(const char* cmdline, const ShellOptions* opts)` | Execute a shell command string via `/bin/sh -c` (use with caution — intended for trusted internal commands only) |

### Background Processes

| Function | Signature | Description |
|----------|-----------|-------------|
| `shell_spawn` | `ShellProcess* shell_spawn(const char* program, const char** args, const ShellOptions* opts)` | Launch async process, return handle |
| `shell_process_poll` | `bool shell_process_poll(ShellProcess* proc)` | Check if process has exited |
| `shell_process_wait` | `ShellResult shell_process_wait(ShellProcess* proc, int timeout_ms)` | Block until exit or timeout |
| `shell_process_kill` | `bool shell_process_kill(ShellProcess* proc)` | Send SIGTERM (POSIX) / TerminateProcess (Win32) |
| `shell_process_free` | `void shell_process_free(ShellProcess* proc)` | Release handle and buffers |

### Environment Variables

| Function | Signature | Description |
|----------|-----------|-------------|
| `shell_getenv` | `const char* shell_getenv(const char* name)` | Get environment variable (NULL if unset) |
| `shell_setenv` | `bool shell_setenv(const char* name, const char* value)` | Set environment variable |
| `shell_unsetenv` | `bool shell_unsetenv(const char* name)` | Unset environment variable |

### Utilities

| Function | Signature | Description |
|----------|-----------|-------------|
| `shell_which` | `char* shell_which(const char* program)` | Resolve program to absolute path via PATH lookup (caller must free) |
| `shell_quote_arg` | `char* shell_quote_arg(const char* arg)` | Shell-escape a single argument (caller must free) |
| `shell_result_free` | `void shell_result_free(ShellResult* result)` | Free stdout/stderr buffers in a ShellResult |
| `shell_get_home_dir` | `const char* shell_get_home_dir(void)` | Return user home directory (cached) |
| `shell_get_temp_dir` | `const char* shell_get_temp_dir(void)` | Return system temp directory (cached) |
| `shell_get_hostname` | `char* shell_get_hostname(void)` | Return hostname (caller must free) |

## Platform Abstraction

```
┌──────────────────────────────────────────────────────────┐
│                     lib/shell.h                          │
│          Unified API (ShellResult, ShellProcess)         │
├──────────────┬───────────────────────┬───────────────────┤
│   POSIX      │       macOS           │     Windows       │
│  fork/exec   │   posix_spawn         │  CreateProcess    │
│  pipe/dup2   │   pipe/dup2           │  CreatePipe       │
│  waitpid     │   waitpid             │  WaitForSingle    │
│  kill        │   kill                │  TerminateProcess  │
│  getenv      │   getenv              │  GetEnvironment   │
└──────────────┴───────────────────────┴───────────────────┘
```

Implementation uses `posix_spawn` where available (macOS, modern Linux) for efficiency, falling back to `fork/exec` on older platforms. Windows builds use `CreateProcess` with anonymous pipes.

## Integration with Language Runtimes

### Lambda Script

Exposed via `io.exec()` and `io.spawn()` system procedures registered in `sys_func_registry.c`:

```lambda
// synchronous execution — returns map with exit_code, stdout, stderr
let result = io.exec("git", ["status", "--short"])
let lines = result.stdout | split_lines

// with options
let result = io.exec("make", ["build"], { cwd: "./project", timeout: 30000 })

// background process
let proc = io.spawn("node", ["server.js"])
// ... later ...
let done = io.poll(proc)
io.kill(proc)
```

### Bash (Transpiler)

Replaces current ad-hoc `posix_spawn` calls in `lambda/bash/` with `shell_exec`:

```c
// before: direct posix_spawn + manual pipe setup in bash runtime
// after:
const char* args[] = {"ls", "-la", NULL};
ShellResult r = shell_exec("ls", args, NULL);
```

### JavaScript / Python

Transpiled `subprocess` / `child_process` calls map to `shell_exec`/`shell_spawn`:

```python
# Python: subprocess.run(["ls", "-la"], capture_output=True)
# → transpiles to shell_exec("ls", (const char*[]){"ls", "-la", NULL}, &opts)
```

```javascript
// JS: const { execSync } = require('child_process');
//     execSync('ls -la')
// → transpiles to shell_exec_line("ls -la", NULL)
```

## Security Considerations

1. **No shell interpolation by default** — `shell_exec` passes args directly to `execvp`/`posix_spawn`, bypassing shell expansion. This prevents command injection.
2. **`shell_exec_line` is restricted** — only for trusted internal use (e.g., Bash transpiler executing user-authored shell scripts). Should never be exposed directly to untrusted input.
3. **`shell_quote_arg`** — available for cases where shell string construction is unavoidable; uses POSIX single-quoting with proper escaping.
4. **Timeout enforcement** — prevents infinite hangs from runaway child processes.
5. **PATH-only resolution** — `shell_which` searches `$PATH` entries sequentially; does not execute anything.

## Implementation Phases

### Phase 1: Core Synchronous Execution
- Implement `shell_exec`, `shell_exec_simple`, `shell_result_free`
- POSIX (posix_spawn + pipe) and Win32 (CreateProcess) backends
- Timeout via `alarm()`/`SIGALRM` (POSIX) or `WaitForSingleObject` timeout (Win32)
- Unit tests: exit codes, stdout/stderr capture, timeout, missing program

### Phase 2: Environment & Utilities
- `shell_getenv`, `shell_setenv`, `shell_unsetenv`
- `shell_which` — PATH scanning with stat + permission check
- `shell_quote_arg` — safe argument escaping
- `shell_get_home_dir`, `shell_get_temp_dir`, `shell_get_hostname`
- Unit tests: env round-trip, which resolution, quoting edge cases

### Phase 3: Background Processes
- Implement `ShellProcess` handle with `shell_spawn`, `shell_process_poll/wait/kill/free`
- Non-blocking pipe reads via `fcntl(O_NONBLOCK)` / `PeekNamedPipe`
- Unit tests: spawn + poll loop, kill + cleanup, wait timeout

### Phase 4: Streaming & Line Callbacks
- `ShellLineCallback` integration for `on_stdout` / `on_stderr`
- Buffered line splitting over raw pipe reads
- Early abort when callback returns false
- Unit tests: large output streaming, partial line handling

### Phase 5: Runtime Integration
- Register `io.exec`, `io.spawn`, `io.poll`, `io.kill` in `sys_func_registry.c`
- Wire Bash transpiler to use `shell_exec` instead of direct `posix_spawn`
- Add JS/Python transpiler mappings
- Integration tests with Lambda scripts

### Phase 6: `shell_exec_line` & Advanced
- `/bin/sh -c` execution for Bash transpiler compatibility
- `merge_stderr` option
- Custom `env` array passing (child-only environment variables)
- Cross-platform CI validation (macOS, Linux, Windows)

## Relationship to Module IO: File

The Shell module (`lib/shell.c`) and the File module (`lib/file.c` + `lib/file_utils.c`) together provide comprehensive I/O support:

| Concern | Module |
|---------|--------|
| Read/write files | `lib/file.c` |
| Directory operations (mkdir, stat, list, walk) | `lib/file.c` + `lib/file_utils.c` |
| File metadata (size, permissions, timestamps) | `lib/file.c` |
| Path manipulation | `lambda/path.c` |
| Process execution & capture | `lib/shell.c` |
| Environment variables | `lib/shell.c` |
| Program lookup (which) | `lib/shell.c` |
| Temporary files / directories | `lib/file.c` (creation) + `lib/shell.c` (temp dir location) |

The two modules share no internal state and can be used independently, but together they give every Lambda runtime backend (Lambda, JS, Python, Bash) a complete foundation for system interaction.
