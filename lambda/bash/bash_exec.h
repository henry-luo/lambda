// bash_exec.h — Exec Engine (Phase E — Module 8)
//
// Provides:
// - exec builtin: persistent fd redirections and process replacement
// - Variable-target fd allocation: exec {varname}>file
// - Sub-script execution via fork+exec

#ifndef BASH_EXEC_H
#define BASH_EXEC_H

#ifdef __cplusplus
extern "C" {
#endif

#include "../lambda.h"

// ============================================================================
// exec builtin flags
// ============================================================================

#define BASH_EXEC_CLEAN_ENV  0x01   // -c: clear environment
#define BASH_EXEC_LOGIN      0x02   // -l: place dash before argv[0]
#define BASH_EXEC_ARGV0      0x04   // -a name: set argv[0] to name

// ============================================================================
// exec builtin
// ============================================================================

// exec with command: replace current process via execvp()
// exec with no command (argc==0): apply persistent redirections only
// Returns: 0 on redirect-only success, does not return on successful exec
//          1 on failure (sets $? and prints error)
int bash_exec_builtin(Item* args, int argc, int flags, const char* argv0_override);

// ============================================================================
// Persistent fd redirections (survive beyond a single command)
// ============================================================================

// exec N>file or exec N>>file — open file on fd
void bash_exec_redir_open(int fd, const char* path, int open_flags, int open_mode);

// exec N>&M — duplicate fd
void bash_exec_redir_dup(int new_fd, int old_fd);

// exec N>&- — close fd
void bash_exec_redir_close(int fd);

// exec {varname}>file — open file, allocate fd >= 10, store in variable
int bash_exec_redir_varfd(Item var_name, const char* path, int open_flags, int open_mode);

// ============================================================================
// Persistent fd state management
// ============================================================================

// Restore all persistent fd redirections to their original state
void bash_exec_redir_restore_all(void);

// Check if a persistent fd is active
int bash_exec_redir_is_active(int fd);

// ============================================================================
// Sub-script execution
// ============================================================================

// Execute a sub-script via fork+exec
// Inherits exported variables and functions
// Sets $0 to script_path in child
// Returns child exit code
int bash_exec_subscript(const char* interpreter, const char* script_path,
                        const char** extra_args, int extra_argc);

// ============================================================================
// Module lifecycle
// ============================================================================

void bash_exec_init(void);
void bash_exec_cleanup(void);

#ifdef __cplusplus
}
#endif

#endif // BASH_EXEC_H
