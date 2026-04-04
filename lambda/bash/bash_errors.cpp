// bash_errors.cpp — Error Formatting Engine (Phase B — Module 4)
//
// Centralized error message formatting matching GNU Bash output exactly.
// Format: "shell_name: [line N: ][context: ]message\n"

#include "bash_errors.h"
#include "bash_runtime.h"
#include "../../lib/log.h"

#include <stdio.h>
#include <stdarg.h>
#include <string.h>

// ========================================================================
// Shell identity state
// ========================================================================

// These are declared extern so they share state with bash_runtime.cpp.
// bash_runtime.cpp defines them:
//   static char bash_error_script_name[4096]
//   static int bash_current_lineno
// We access the same values through the getter functions already
// declared in bash_runtime.h (bash_set_lineno, bash_set_script_name).
// For the error module, we use a local shell name that defaults to
// bash_error_script_name via the extern access.

static const char* bash_err_shell_name = NULL;  // NULL = use runtime script name

extern "C" void bash_error_set_shell_name(const char* name) {
    bash_err_shell_name = name;
}

extern "C" const char* bash_error_get_shell_name(void) {
    if (bash_err_shell_name) return bash_err_shell_name;
    // fall back to the runtime's error script name
    // declared as extern to access the static in bash_runtime.cpp
    extern char bash_error_script_name[4096];
    return bash_error_script_name;
}

extern "C" void bash_error_set_lineno(int line) {
    bash_set_lineno(line);
}

extern "C" int bash_error_get_lineno(void) {
    extern int bash_current_lineno;
    return bash_current_lineno;
}

// ========================================================================
// Generic error formatting
// ========================================================================

extern "C" void bash_errmsg(const char* fmt, ...) {
    const char* shell = bash_error_get_shell_name();
    fprintf(stderr, "%s: ", shell);
    va_list args;
    va_start(args, fmt);
    vfprintf(stderr, fmt, args);
    va_end(args);
    fprintf(stderr, "\n");
    fflush(stderr);
}

extern "C" void bash_errmsg_at(const char* fmt, ...) {
    const char* shell = bash_error_get_shell_name();
    int lineno = bash_error_get_lineno();
    fprintf(stderr, "%s: line %d: ", shell, lineno);
    va_list args;
    va_start(args, fmt);
    vfprintf(stderr, fmt, args);
    va_end(args);
    fprintf(stderr, "\n");
    fflush(stderr);
}

// ========================================================================
// Specific error producers
// ========================================================================

// "shell: line N: varname: readonly variable"
extern "C" void bash_err_readonly(const char* var_name) {
    const char* shell = bash_error_get_shell_name();
    int lineno = bash_error_get_lineno();
    fprintf(stderr, "%s: line %d: %s: readonly variable\n", shell, lineno, var_name);
    fflush(stderr);
}

// "shell: ${expr: bad substitution"
extern "C" void bash_err_bad_substitution(const char* expr) {
    const char* shell = bash_error_get_shell_name();
    fprintf(stderr, "%s: %s: bad substitution\n", shell, expr);
    fflush(stderr);
}

// "shell: varname: unbound variable"
extern "C" void bash_err_unbound_variable(const char* var_name) {
    const char* shell = bash_error_get_shell_name();
    int lineno = bash_error_get_lineno();
    fprintf(stderr, "%s: line %d: %s: unbound variable\n", shell, lineno, var_name);
    fflush(stderr);
}

// "shell: line N: cmd_name: command not found"
extern "C" void bash_err_not_found(const char* cmd_name) {
    const char* shell = bash_error_get_shell_name();
    int lineno = bash_error_get_lineno();
    fprintf(stderr, "%s: line %d: %s: command not found\n", shell, lineno, cmd_name);
    fflush(stderr);
}

// "shell: line N: `token': syntax error: ..."
extern "C" void bash_err_syntax(const char* token) {
    const char* shell = bash_error_get_shell_name();
    int lineno = bash_error_get_lineno();
    fprintf(stderr, "%s: line %d: syntax error near unexpected token `%s'\n", shell, lineno, token);
    fflush(stderr);
}

// "shell: func: arg: numeric argument required"
extern "C" void bash_err_numeric_arg(const char* func, const char* arg) {
    const char* shell = bash_error_get_shell_name();
    fprintf(stderr, "%s: %s: %s: numeric argument required\n", shell, func, arg);
    fflush(stderr);
}

// "shell: line N: cmd: -opt: invalid option"
extern "C" void bash_err_invalid_option(const char* cmd, const char* opt) {
    const char* shell = bash_error_get_shell_name();
    int lineno = bash_error_get_lineno();
    fprintf(stderr, "%s: line %d: %s: %s: invalid option\n", shell, lineno, cmd, opt);
    fflush(stderr);
}

// "shell: cmd: too many arguments"
extern "C" void bash_err_too_many_args(const char* cmd) {
    const char* shell = bash_error_get_shell_name();
    fprintf(stderr, "%s: %s: too many arguments\n", shell, cmd);
    fflush(stderr);
}

// "shell: cmd: `name': not a valid identifier"
extern "C" void bash_err_not_valid_identifier(const char* cmd, const char* name) {
    const char* shell = bash_error_get_shell_name();
    int lineno = bash_error_get_lineno();
    fprintf(stderr, "%s: line %d: %s: `%s': not a valid identifier\n", shell, lineno, cmd, name);
    fflush(stderr);
}

// "shell: target: ambiguous redirect"
extern "C" void bash_err_ambiguous_redirect(const char* target) {
    const char* shell = bash_error_get_shell_name();
    fprintf(stderr, "%s: %s: ambiguous redirect\n", shell, target);
    fflush(stderr);
}

// "shell: line N: ((: expr : division by 0 (error token is "val ")"
extern "C" void bash_err_division_by_zero(const char* expr, long long token_val) {
    const char* shell = bash_error_get_shell_name();
    int lineno = bash_error_get_lineno();
    fprintf(stderr, "%s: line %d: ((: %s : division by 0 (error token is \"%lld \")\n",
            shell, lineno, expr, token_val);
    fflush(stderr);
}

// "shell: line N: unset: varname: cannot unset: readonly variable"
extern "C" void bash_err_unset_readonly(const char* builtin, const char* var_name) {
    const char* shell = bash_error_get_shell_name();
    int lineno = bash_error_get_lineno();
    fprintf(stderr, "%s: line %d: %s: %s: cannot unset: readonly variable\n",
            shell, lineno, builtin, var_name);
    fflush(stderr);
}

// "shell: warning: varname: circular name reference"
extern "C" void bash_err_circular_nameref(const char* var_name) {
    const char* shell = bash_error_get_shell_name();
    fprintf(stderr, "%s: warning: %s: circular name reference\n", shell, var_name);
    fflush(stderr);
}

// "shell: declare: varname: not found"
extern "C" void bash_err_declare_not_found(const char* var_name) {
    const char* shell = bash_error_get_shell_name();
    fprintf(stderr, "%s: declare: %s: not found\n", shell, var_name);
    fflush(stderr);
}

// "shell: cmd: path: No such file or directory"
extern "C" void bash_err_no_such_file(const char* cmd, const char* path) {
    const char* shell = bash_error_get_shell_name();
    if (cmd && *cmd) {
        fprintf(stderr, "%s: %s: %s: No such file or directory\n", shell, cmd, path);
    } else {
        fprintf(stderr, "%s: %s: No such file or directory\n", shell, path);
    }
    fflush(stderr);
}

// "shell: varname: msg"   or   "shell: varname: parameter null or not set"
extern "C" void bash_err_param_not_set(const char* var_name, const char* msg) {
    const char* shell = bash_error_get_shell_name();
    if (msg && *msg) {
        fprintf(stderr, "%s: %s: %s\n", shell, var_name, msg);
    } else {
        fprintf(stderr, "%s: %s: parameter null or not set\n", shell, var_name);
    }
    fflush(stderr);
}
