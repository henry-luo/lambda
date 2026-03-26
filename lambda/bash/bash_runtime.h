#pragma once

// bash_runtime.h — C API for Bash runtime functions callable from JIT code
// All functions take and return Lambda Item values (64-bit tagged).
// Bash semantics: variables are strings by default, coerced to integers in
// arithmetic contexts.  Exit codes (0 = success) map to Lambda booleans.

#ifdef __cplusplus
extern "C" {
#endif

#include "../lambda.h"

// ========================================================================
// Type conversion (Bash string-first semantics)
// ========================================================================
Item bash_to_int(Item value);               // coerce to integer (atoi semantics)
Item bash_to_string(Item value);            // coerce to string (all values)
bool bash_is_truthy(Item value);            // Bash truthiness: non-empty string
int  bash_exit_code(Item value);            // Item → exit code (0/1)
Item bash_from_exit_code(int code);         // exit code → Item bool

// ========================================================================
// Arithmetic operators (integer arithmetic, like $(( )))
// ========================================================================
Item bash_add(Item left, Item right);
Item bash_subtract(Item left, Item right);
Item bash_multiply(Item left, Item right);
Item bash_divide(Item left, Item right);    // integer division
Item bash_modulo(Item left, Item right);
Item bash_power(Item left, Item right);     // **
Item bash_negate(Item operand);             // unary -

// ========================================================================
// Bitwise operators
// ========================================================================
Item bash_bit_and(Item left, Item right);
Item bash_bit_or(Item left, Item right);
Item bash_bit_xor(Item left, Item right);
Item bash_bit_not(Item operand);
Item bash_lshift(Item left, Item right);
Item bash_rshift(Item left, Item right);

// ========================================================================
// Arithmetic comparison (used in $(( )) and [[ ]])
// ========================================================================
Item bash_arith_eq(Item left, Item right);  // ==
Item bash_arith_ne(Item left, Item right);  // !=
Item bash_arith_lt(Item left, Item right);  // <
Item bash_arith_le(Item left, Item right);  // <=
Item bash_arith_gt(Item left, Item right);  // >
Item bash_arith_ge(Item left, Item right);  // >=

// ========================================================================
// Test / conditional operators ([ ] and [[ ]])
// ========================================================================

// numeric comparisons: -eq, -ne, -gt, -ge, -lt, -le
Item bash_test_eq(Item left, Item right);
Item bash_test_ne(Item left, Item right);
Item bash_test_gt(Item left, Item right);
Item bash_test_ge(Item left, Item right);
Item bash_test_lt(Item left, Item right);
Item bash_test_le(Item left, Item right);

// string comparisons:  == / = , != , < , >
Item bash_test_str_eq(Item left, Item right);
Item bash_test_str_ne(Item left, Item right);
Item bash_test_str_lt(Item left, Item right);
Item bash_test_str_gt(Item left, Item right);

// unary string tests: -z, -n
Item bash_test_z(Item value);               // true if string is empty
Item bash_test_n(Item value);               // true if string is non-empty

// file test operators
Item bash_test_f(Item value);               // -f (regular file)
Item bash_test_d(Item value);               // -d (directory)
Item bash_test_e(Item value);               // -e (exists)
Item bash_test_r(Item value);               // -r (readable)
Item bash_test_w(Item value);               // -w (writable)
Item bash_test_x(Item value);               // -x (executable)
Item bash_test_s(Item value);               // -s (non-zero size)
Item bash_test_l(Item value);               // -L (symlink)

// regex match: =~
Item bash_test_regex(Item string, Item pattern);
Item bash_test_glob(Item string, Item pattern);

// ========================================================================
// String operations
// ========================================================================
Item bash_string_length(Item str);          // ${#var}
Item bash_string_concat(Item left, Item right);
Item bash_string_substring(Item str, Item offset, Item length); // ${var:off:len}
Item bash_string_trim_prefix(Item str, Item pattern, bool greedy);  // ${var#pat} / ${var##pat}
Item bash_string_trim_suffix(Item str, Item pattern, bool greedy);  // ${var%pat} / ${var%%pat}
Item bash_string_replace(Item str, Item pattern, Item replacement, bool all); // ${var/pat/str} ${var//pat/str}
Item bash_string_upper(Item str, bool all);  // ${var^} / ${var^^}
Item bash_string_lower(Item str, bool all);  // ${var,} / ${var,,}

// ========================================================================
// Parameter expansion  (${var:-default} etc.)
// ========================================================================
Item bash_expand_default(Item value, Item default_val);     // ${var:-default}
Item bash_expand_assign_default(Item var_name, Item value, Item default_val); // ${var:=default}
Item bash_expand_alt(Item value, Item alt_val);             // ${var:+alt}
Item bash_expand_error(Item value, Item msg);               // ${var:?msg}
Item bash_expand_trim_prefix(Item val, Item pat);           // ${var#pat}
Item bash_expand_trim_prefix_long(Item val, Item pat);      // ${var##pat}
Item bash_expand_trim_suffix(Item val, Item pat);           // ${var%pat}
Item bash_expand_trim_suffix_long(Item val, Item pat);      // ${var%%pat}
Item bash_expand_replace(Item val, Item pat, Item repl);    // ${var/pat/str}
Item bash_expand_replace_all(Item val, Item pat, Item repl);// ${var//pat/str}
Item bash_expand_substring(Item val, Item offset, Item len);// ${var:off:len}
Item bash_expand_upper_first(Item val);                     // ${var^}
Item bash_expand_upper_all(Item val);                       // ${var^^}
Item bash_expand_lower_first(Item val);                     // ${var,}
Item bash_expand_lower_all(Item val);                       // ${var,,}

// ========================================================================
// Array operations
// ========================================================================
Item bash_int_to_item(int64_t n);                   // convert int64 to Item
Item bash_array_new(void);                          // create empty array
Item bash_array_set(Item arr, Item index, Item value);
Item bash_array_get(Item arr, Item index);
Item bash_array_append(Item arr, Item value);       // arr+=(value)
Item bash_array_length(Item arr);                   // ${#arr[@]}
int64_t bash_array_count(Item arr);                 // raw count for iteration
Item bash_array_all(Item arr);                      // ${arr[@]} as list
Item bash_array_unset(Item arr, Item index);        // unset arr[i]
Item bash_array_slice(Item arr, Item offset, Item length); // ${arr[@]:off:len}

// ========================================================================
// Variable scope management
// ========================================================================
void bash_set_var(Item name, Item value);           // assign variable
Item bash_get_var(Item name);                       // read variable
void bash_set_local_var(Item name, Item value);     // local var=value
void bash_export_var(Item name);                    // export var
void bash_unset_var(Item name);                     // unset var

// Positional parameters ($1, $2, ...)
void bash_set_positional(Item* args, int count);
void bash_push_positional(Item* args, int count);   // save + set positional params
void bash_pop_positional(void);                      // restore positional params
Item bash_get_positional(int index);                // $1 = index 1
Item bash_get_arg_count(void);                      // $#
Item bash_get_all_args(void);                       // $@ (as array)
Item bash_get_all_args_string(void);                // $@ / $* (as space-joined string)
Item bash_shift_args(int n);                        // shift [n]

// Special variables
Item bash_get_exit_code(void);                      // $?
void bash_set_exit_code(int code);
void bash_negate_exit_code(void);                   // flip exit code (0↔1)
Item bash_return_with_code(Item val);               // set exit code from value
Item bash_get_script_name(void);                    // $0

// ========================================================================
// Scope lifecycle
// ========================================================================
void bash_scope_push(void);                         // enter new local scope
void bash_scope_pop(void);                          // leave scope
void bash_scope_push_subshell(void);                // snapshot for subshell
void bash_scope_pop_subshell(void);                 // restore after subshell

// ========================================================================
// Built-in commands
// ========================================================================
Item bash_builtin_echo(Item* args, int argc);
Item bash_builtin_printf(Item format, Item* args, int argc);
Item bash_builtin_test(Item* args, int argc);       // test / [ ]
Item bash_builtin_true(void);
Item bash_builtin_false(void);
Item bash_builtin_exit(Item code);
Item bash_builtin_return(Item code);
Item bash_builtin_read(Item* args, int argc);
Item bash_builtin_shift(Item n);
Item bash_builtin_local(Item name, Item value);
Item bash_builtin_export(Item name, Item value);
Item bash_builtin_unset(Item name);
Item bash_builtin_cd(Item dir);
Item bash_builtin_pwd(void);
Item bash_builtin_cat(Item* args, int argc);
Item bash_builtin_wc(Item* args, int argc);
Item bash_builtin_head(Item* args, int argc);
Item bash_builtin_tail(Item* args, int argc);
Item bash_builtin_grep(Item* args, int argc);
Item bash_builtin_sort(Item* args, int argc);
Item bash_builtin_tr(Item* args, int argc);
Item bash_builtin_cut(Item* args, int argc);

// ========================================================================
// Control flow support
// ========================================================================
int  bash_get_loop_control(void);                   // 0=none, 1=break, 2=continue
void bash_set_loop_control(int control, int depth);
void bash_clear_loop_control(void);

// ========================================================================
// Command execution and pipeline
// ========================================================================
Item bash_command_substitution(Item* args, int argc);  // $(command)
Item bash_pipe(Item left_output, Item* right_cmd, int right_argc);

// ========================================================================
// Pipeline stdin item passing (builtin-to-builtin)
// ========================================================================
void bash_set_stdin_item(Item input);   // set pending stdin content for next pipe stage
Item bash_get_stdin_item(void);         // read pending stdin (for cat, wc, grep, etc.)
void bash_clear_stdin_item(void);       // clear after consumption

// ========================================================================
// File redirections
// ========================================================================
Item bash_redirect_write(Item filename, Item content);   // > file
Item bash_redirect_append(Item filename, Item content);  // >> file
Item bash_redirect_read(Item filename);                  // < file

// ========================================================================
// Expansions (tilde, glob, brace)
// ========================================================================
Item bash_expand_tilde(Item word);                       // ~ → $HOME
Item bash_glob_expand(Item pattern);                     // *.txt → matching paths
Item bash_expand_brace(Item word);                       // {a,b,c} → "a b c"

// ========================================================================
// External command execution
// ========================================================================
Item bash_exec_external(Item* argv, int argc);           // run system binary via posix_spawn

// ========================================================================
// Output
// ========================================================================
void bash_write_heredoc(Item content, int is_herestring); // write heredoc/herestring
void bash_write_stdout(Item value);                 // write string to stdout
void bash_write_stderr(Item value);                 // write string to stderr
void bash_raw_write(const char* data, int len);     // write raw bytes (capture-aware)
void bash_raw_putc(char c);                         // write single char (capture-aware)
void bash_begin_capture(void);                      // start capturing stdout
Item bash_end_capture(void);                        // stop capturing, return string (strips trailing newlines)
Item bash_end_capture_raw(void);                    // stop capturing, return string (preserves trailing newlines)

// ========================================================================
// Runtime initialization
// ========================================================================
void bash_runtime_init(void);
void bash_runtime_cleanup(void);

#ifdef __cplusplus
}
#endif
