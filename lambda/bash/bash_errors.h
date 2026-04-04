// bash_errors.h — Error Formatting Engine (Phase B — Module 4)
//
// Centralized error message formatting that exactly matches GNU Bash output.
// All runtime error messages should go through these functions to ensure
// consistent format: "shell_name: [line N: ]context: message\n"

#ifndef BASH_ERRORS_H
#define BASH_ERRORS_H

#ifdef __cplusplus
extern "C" {
#endif

#include "../lambda.h"

// ========================================================================
// Shell identity (for error prefix)
// ========================================================================

// Set/get the shell name used in error message prefix (default: "bash")
void bash_error_set_shell_name(const char* name);
const char* bash_error_get_shell_name(void);

// Set/get current line number for error messages
void bash_error_set_lineno(int line);
int  bash_error_get_lineno(void);

// ========================================================================
// Generic error formatting
// ========================================================================

// Generic error: "shell: msg\n" → stderr
void bash_errmsg(const char* fmt, ...);

// Error with line number: "shell: line N: msg\n" → stderr
void bash_errmsg_at(const char* fmt, ...);

// ========================================================================
// Specific error producers (exact GNU Bash format)
// ========================================================================

// "shell: line N: varname: readonly variable\n"
void bash_err_readonly(const char* var_name);

// "shell: ${expr: bad substitution\n"
void bash_err_bad_substitution(const char* expr);

// "shell: varname: unbound variable\n"
void bash_err_unbound_variable(const char* var_name);

// "shell: line N: cmd_name: command not found\n"
void bash_err_not_found(const char* cmd_name);

// "shell: line N: token: syntax error\n"
void bash_err_syntax(const char* token);

// "shell: func: arg: numeric argument required\n"
void bash_err_numeric_arg(const char* func, const char* arg);

// "shell: line N: cmd: -opt: invalid option\n"
void bash_err_invalid_option(const char* cmd, const char* opt);

// "shell: cmd: too many arguments\n"
void bash_err_too_many_args(const char* cmd);

// "shell: cmd: `name': not a valid identifier\n"
void bash_err_not_valid_identifier(const char* cmd, const char* name);

// "shell: target: ambiguous redirect\n"
void bash_err_ambiguous_redirect(const char* target);

// "shell: line N: ((: expr : division by 0 (error token is \"val \")\n"
void bash_err_division_by_zero(const char* expr, long long token_val);

// "shell: line N: varname: cannot unset: readonly variable\n"
void bash_err_unset_readonly(const char* builtin, const char* var_name);

// "shell: warning: varname: circular name reference\n"
void bash_err_circular_nameref(const char* var_name);

// "shell: declare: varname: not found\n"
void bash_err_declare_not_found(const char* var_name);

// "shell: cmd: path: No such file or directory\n"
void bash_err_no_such_file(const char* cmd, const char* path);

// "shell: varname: parameter null or not set\n"   (${var:?})
void bash_err_param_not_set(const char* var_name, const char* msg);

#ifdef __cplusplus
}
#endif

#endif // BASH_ERRORS_H
