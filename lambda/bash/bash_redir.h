// bash_redir.h — Redirection & I/O Engine (Phase C — Module 7)
//
// Provides:
// - Stderr redirection (2>file, 2>>file, 2>/dev/null)
// - FD duplication (2>&1, >&2)
// - I/O state save/restore stack for compound commands
// - Combined redirect (&>file, &>>file)

#ifndef BASH_REDIR_H
#define BASH_REDIR_H

#ifdef __cplusplus
extern "C" {
#endif

#include "../lambda.h"

// ============================================================================
// Stderr routing modes
// ============================================================================

typedef enum BashStderrRoute {
    BASH_STDERR_NORMAL,     // default: write to real stderr
    BASH_STDERR_TO_NULL,    // 2>/dev/null: discard
    BASH_STDERR_TO_STDOUT,  // 2>&1: merge into stdout (capture-aware)
    BASH_STDERR_TO_FILE,    // 2>file: capture and write to file
} BashStderrRoute;

// ============================================================================
// I/O state push/pop — for compound commands and nested redirections
// ============================================================================

// Save current I/O routing state (stderr mode, etc.) onto the stack.
// Must be paired with bash_io_pop().
void bash_io_push(void);

// Restore previous I/O routing state from the stack.
void bash_io_pop(void);

// ============================================================================
// Stderr redirection control
// ============================================================================

// Set stderr routing mode (call between push/pop for scoped redirections).
void bash_stderr_set_route(BashStderrRoute route);

// Set the target filename for BASH_STDERR_TO_FILE mode.
// Caller must ensure the filename outlives the I/O scope.
void bash_stderr_set_file(const char* filename, int append);

// Route stderr output according to current mode.
// Called by bash_write_stderr() — replaces direct fwrite to stderr.
void bash_stderr_route_write(const char* data, int len);

// ============================================================================
// Stderr capture for file redirect
// ============================================================================

// Begin capturing stderr output (for 2>file redirections).
void bash_stderr_begin_capture(void);

// End stderr capture and write to file. Returns 0 on success, 1 on error.
int bash_stderr_end_capture_to_file(const char* filename, int append);

// ============================================================================
// Combined redirect helpers
// ============================================================================

// Set up 2>&1 redirect (merge stderr into stdout).
void bash_redir_stderr_to_stdout(void);

// Set up 2>/dev/null redirect (discard stderr).
void bash_redir_stderr_to_null(void);

// Set up 2>file or 2>>file redirect.
void bash_redir_stderr_to_file(Item filename, int append);

// Restore stderr to normal routing.
void bash_redir_stderr_restore(void);

#ifdef __cplusplus
}
#endif

#endif // BASH_REDIR_H
