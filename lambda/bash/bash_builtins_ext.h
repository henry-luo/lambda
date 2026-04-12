// bash_builtins_ext.h — Extended Builtins (Phase F — Module 9)
//
// Provides builtins needed by GNU tests that are not in bash_builtins.cpp:
// - mapfile / readarray
// - wait
// - hash
// - enable
// - builtin (run builtin directly, bypassing functions)
// - umask
// - trap print (trap -p)

#ifndef BASH_BUILTINS_EXT_H
#define BASH_BUILTINS_EXT_H

#ifdef __cplusplus
extern "C" {
#endif

#include "../lambda.h"

// ============================================================================
// mapfile / readarray
// ============================================================================

// mapfile [-d delim] [-n count] [-O origin] [-s count] [-t] [-u fd] [array]
// Read lines from stdin (or fd) into indexed array variable.
// Returns 0 on success, 1 on error.
Item bash_builtin_mapfile(Item* args, int argc);

// ============================================================================
// wait
// ============================================================================

// wait [-n] [-p varname] [id ...]
// Wait for background processes to complete.
// Returns exit status of last waited-for process.
Item bash_builtin_wait(Item* args, int argc);

// ============================================================================
// hash
// ============================================================================

// hash [-r] [-p path] [-dt] [name ...]
// Manage command path hash table.
// Returns 0 on success, 1 on error.
Item bash_builtin_hash(Item* args, int argc);

// ============================================================================
// enable
// ============================================================================

// enable [-a] [-dnps] [-f filename] [name ...]
// Enable/disable builtins.
// Returns 0 on success, 1 on error.
Item bash_builtin_enable(Item* args, int argc);
Item bash_builtin_compgen(Item* args, int argc);

// ============================================================================
// builtin
// ============================================================================

// builtin name [args]
// Run a builtin command directly, bypassing function lookup.
// Returns the builtin's exit status.
Item bash_builtin_builtin(Item* args, int argc);

// ============================================================================
// umask
// ============================================================================

// umask [-p] [-S] [mode]
// Display or set file creation mask.
// Returns 0 on success, 1 on error.
Item bash_builtin_umask(Item* args, int argc);

// ============================================================================
// trap -p (print)
// ============================================================================

// Print all traps in GNU Bash format: trap -- 'action' SIGNAL
void bash_trap_print_all(void);

// Print trap for a specific signal in GNU Bash format
void bash_trap_print_one(Item signal_name);

#ifdef __cplusplus
}
#endif

#endif // BASH_BUILTINS_EXT_H
