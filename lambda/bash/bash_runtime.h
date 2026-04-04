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
int64_t bash_to_int_val(Item value);        // coerce to plain int64_t
Item bash_arith_eval_value(Item value);     // evaluate as arithmetic expression (for declare -i)
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
Item bash_logical_not(Item operand);        // !a → 1 if a==0, else 0

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
Item bash_str_eq(Item left, Item right);     // literal strcmp (no glob)
Item bash_test_str_eq(Item left, Item right);
Item bash_test_str_eq_noescape(Item left, Item right);  // FNM_NOESCAPE for word patterns
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
Item bash_var_append(Item var_name, Item old_val, Item append_val); // var+=val (integer-aware)
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
Item bash_expand_default_nocolon(Item value, Item default_val);     // ${var-default}
Item bash_expand_assign_default_nocolon(Item var_name, Item value, Item default_val); // ${var=default}
Item bash_expand_alt_nocolon(Item value, Item alt_val);             // ${var+alt}
Item bash_expand_error_nocolon(Item value, Item msg);               // ${var?msg}
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
Item bash_ensure_array(Item name);                  // ensure var is array, create if needed
Item bash_array_set(Item arr, Item index, Item value);
Item bash_array_get(Item arr, Item index);
Item bash_array_append(Item arr, Item value);       // arr+=(value)
Item bash_array_concat(Item dest, Item src);        // append all elements of src to dest
void bash_array_elem_append(Item arr, Item index, Item append_val, Item var_name); // arr[idx]+=val
Item bash_words_split_into(Item arr, Item words_str); // append IFS-split words from str into arr
Item bash_ifs_split_into(Item arr, Item val);          // split val by current IFS and append into arr
void bash_set_positional_from_array(Item arr);         // set positional params from array (IFS-aware set)
Item bash_array_length(Item arr);                   // ${#arr[@]}
int64_t bash_array_count(Item arr);                 // raw count for iteration
Item bash_array_all(Item arr);                      // ${arr[@]} as list
Item bash_array_unset(Item arr, Item index);        // unset arr[i]
Item bash_array_slice(Item arr, Item offset, Item length); // ${arr[@]:off:len}

// ========================================================================
// Associative array operations (maps)
// ========================================================================
Item bash_assoc_new(void);                          // create empty associative array
Item bash_ensure_assoc(Item name);                  // ensure var is assoc array, create if needed
Item bash_assoc_set(Item map, Item key, Item value);// map[key]=value
Item bash_assoc_get(Item map, Item key);            // ${map[key]}
Item bash_assoc_keys(Item map);                     // ${!map[@]} — return keys as array
Item bash_assoc_values(Item map);                   // ${map[@]} — return values as array
Item bash_assoc_unset(Item map, Item key);          // unset map[key]
Item bash_assoc_length(Item map);                   // ${#map[@]}
int64_t bash_assoc_count(Item map);                 // raw count for iteration

// ========================================================================
// Variable scope management
// ========================================================================
void bash_set_var(Item name, Item value);           // assign variable
Item bash_get_var(Item name);                       // read variable
void bash_set_local_var(Item name, Item value);     // local var=value
void bash_export_var(Item name);                    // export var
void bash_unset_var(Item name);                     // unset var

// Variable attributes (declare/typeset)
void bash_declare_var(Item name, int flags);        // set variable attributes
void bash_declare_local_var(Item name, int flags);  // set variable attributes in local scope
int  bash_get_var_attrs(Item name);                 // get variable attribute flags
bool bash_is_assoc(Item name);                      // check if variable is assoc array
void bash_declare_print_var(Item name);              // declare -p: print var with attrs

// Variable attribute operations (Phase A — Module 3)
Item bash_apply_attrs(Item value, int attrs);        // apply INTEGER/LOWERCASE/UPPERCASE transforms
Item bash_resolve_nameref(Item var_name);            // follow -n nameref chain to target name
int  bash_check_readonly(Item var_name);             // check readonly; returns 1 + prints error if so
void bash_add_attrs(Item var_name, int add_flags);   // add attribute flags to a variable
void bash_remove_attrs(Item var_name, int rm_flags); // remove attribute flags (declare +x)
void bash_declare_nameref(Item name, Item target);        // declare -n name=target
void bash_declare_local_nameref(Item name, Item target);  // local -n name=target

// Positional parameters ($1, $2, ...)
void bash_set_positional(Item* args, int count);
void bash_set_pending_args(const char** argv, int argc, bool skip_arg0);  // store raw argv for deferred init
void bash_apply_pending_args(void);                  // apply pending args after heap init
void bash_push_positional(Item* args, int count);   // save + set positional params
void bash_pop_positional(void);                      // restore positional params
Item bash_get_positional(int index);                // $1 = index 1
Item bash_get_arg_count(void);                      // $#
Item bash_get_all_args(void);                       // $@ (as array)
Item bash_get_all_args_string(void);                // $@ / $* (as space-joined string)
Item bash_shift_args(int n);                        // shift [n]

// argument builder for dynamic $@ expansion in function calls
void bash_arg_builder_start(void);
void bash_arg_builder_push(Item arg);
void bash_arg_builder_push_at(void);
Item bash_arg_builder_get_ptr(void);
Item bash_arg_builder_get_count(void);

// Special variables
Item bash_get_exit_code(void);                      // $?
void bash_set_exit_code(int code);
Item bash_save_exit_code(void);                     // save exit code as Item for restoration
void bash_restore_exit_code(Item saved);            // restore exit code from saved Item
void bash_negate_exit_code(void);                   // flip exit code (0↔1)
Item bash_return_with_code(Item val);               // set exit code from value
Item bash_get_script_name(void);                    // $0
void bash_set_script_name(Item name);              // update $0 / BASH_ARGV0
Item bash_get_pid(void);                           // $$
Item bash_get_last_bg_pid(void);                   // $!
Item bash_get_shell_flags(void);                   // $-
Item bash_get_lineno(void);                         // $LINENO
void bash_set_lineno(int line);                     // update current statement line
void bash_set_command(Item cmd_text);               // update $BASH_COMMAND
void bash_set_arith_context(Item expr_text);        // set arithmetic expression text for error messages
Item bash_get_funcname(Item index);                 // ${FUNCNAME[n]}
Item bash_get_funcname_count(void);                 // ${#FUNCNAME[@]}
Item bash_get_bash_source(Item index);              // ${BASH_SOURCE[n]}
Item bash_get_bash_lineno(Item index);              // ${BASH_LINENO[n]}
Item bash_get_bash_source_count(void);              // ${#BASH_SOURCE[@]}
Item bash_get_bash_lineno_count(void);              // ${#BASH_LINENO[@]}
void bash_push_funcname(Item name);                 // enter function/debug frame
void bash_pop_funcname(void);                       // leave function/debug frame
Item bash_get_funcname_all(void);                   // ${FUNCNAME[@]}
void bash_push_argv_frame(Item* args, int count);   // push args onto BASH_ARGV
void bash_pop_bash_argv(void);                      // pop top frame from BASH_ARGV
Item bash_get_bash_argv(Item index);                // ${BASH_ARGV[n]}
Item bash_get_bash_argv_count(void);                // ${#BASH_ARGV[@]}
Item bash_get_bash_argv_all(void);                  // ${BASH_ARGV[@]}
Item bash_get_bash_argc(Item index);                // ${BASH_ARGC[n]}
Item bash_get_bash_argc_count(void);                // ${#BASH_ARGC[@]}
void bash_push_source(Item name);                   // enter source file context
void bash_pop_source(void);                         // leave source file context
void bash_push_call_frame(void);                    // record current call site
void bash_pop_call_frame(void);                     // remove current call site
void bash_restore_call_frame_lineno(void);          // restore lineno from top call frame

// ========================================================================
// Scope lifecycle
// ========================================================================
void bash_scope_push(void);                         // enter new local scope
void bash_scope_pop(void);                          // leave scope
void bash_scope_push_subshell(void);                // snapshot for subshell
void bash_scope_pop_subshell(void);                 // restore after subshell
void bash_getopts_push_state(void);                 // save getopts charind state
void bash_getopts_pop_state(void);                  // restore getopts charind state

// ========================================================================
// Built-in commands
// ========================================================================
Item bash_builtin_echo(Item* args, int argc);
Item bash_builtin_printf(Item* all_args, int total_argc);
Item bash_process_escapes(Item input);
Item bash_builtin_let(Item* args, int argc);
Item bash_builtin_type(Item* args, int argc);
Item bash_builtin_command(Item* args, int argc);
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
Item bash_builtin_pushd(Item* args, int argc);
Item bash_builtin_popd(Item* args, int argc);
Item bash_builtin_dirs(Item* args, int argc);
Item bash_builtin_getopts(Item* args, int argc);
Item bash_dirstack_get(Item index);
Item bash_dirstack_total(void);
Item bash_builtin_caller(Item* args, int argc);
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
Item bash_expand_tilde_assign(Item word);                // ~ after : in assignments
Item bash_expand_tilde_assign_arg(Item word);            // ~ after = or : in cmd args
Item bash_glob_expand(Item pattern);                     // *.txt → matching paths
Item bash_expand_brace(Item word);                       // {a,b,c} → "a b c"

// ========================================================================
// External command execution
// ========================================================================
Item bash_exec_external(Item* argv, int argc);           // run system binary via posix_spawn
Item bash_exec_cmd_with_array(Item cmd_name, Item arr);  // dispatch command with IFS-split array args

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
Item bash_cmd_sub_word_split(Item s);               // apply IFS word-splitting to unquoted command substitution result
void bash_cmd_sub_enter(void);                      // enter command substitution context (suppresses debug trap without functrace)
void bash_cmd_sub_exit(void);                       // exit command substitution context

// ========================================================================
// Environment variable integration
// ========================================================================
void bash_env_import(void);                         // import env vars into bash var table
void bash_env_sync_export(Item name);               // export var → setenv for child processes

// ========================================================================
// Script sourcing
// ========================================================================
Item bash_source_file(Item filename);               // source/. — parse & execute file

// ========================================================================
// Runtime function registry (for functions defined in sourced files)
// ========================================================================
typedef Item (*BashRtFuncPtr)(Item* args, int argc);
void bash_register_rt_func(const char* name, BashRtFuncPtr ptr);  // register at JIT-link time
Item bash_call_rt_func(Item name, Item* args, int argc);           // runtime dispatch by name
BashRtFuncPtr bash_lookup_rt_func(const char* name);               // lookup (returns NULL if not found)

// ========================================================================
// Shell options (set -e, set -u, set -x, set -o pipefail)
// ========================================================================
void bash_set_option(Item option, bool enable);      // set/unset shell option
void bash_set_option_flag(char flag, bool enable);   // set/unset shell option by flag char (e, u, x, T)
bool bash_get_option_errexit(void);                  // -e: exit on error
bool bash_get_option_nounset(void);                  // -u: error on undefined var
bool bash_get_option_xtrace(void);                   // -x: trace commands
bool bash_get_option_pipefail(void);                 // -o pipefail
bool bash_get_option_extdebug(void);                 // shopt -s extdebug
bool bash_get_option_nocasematch(void);              // shopt -s nocasematch
bool bash_get_option_extglob(void);                  // shopt -s extglob

// errexit suppression
void bash_errexit_push(void);                        // enter errexit-suppressed context
void bash_errexit_pop(void);                         // leave errexit-suppressed context
int bash_check_errexit(void);                        // check if set -e should trigger (returns 1 if should exit)

// ========================================================================
// Signal handling / trap (Phase 8)
// ========================================================================
void bash_trap_set(Item handler, Item signal_name);  // register trap handler string
void bash_trap_run_exit(void);                       // run EXIT trap (idempotent)
void bash_trap_check(void);                          // check and run pending signal traps
Item bash_run_debug_trap(void);                      // run DEBUG trap before a command
void bash_run_return_trap(void);                     // run RETURN trap on function exit
bool bash_is_functrace(void);                        // check if functrace (set -T) is enabled
Item bash_eval_string(Item code);                    // evaluate bash code string in current scope

// ========================================================================
// POSIX compatibility mode
// ========================================================================
void bash_set_posix_mode(bool mode);   // enable/disable POSIX-compatible mode
bool bash_get_posix_mode(void);        // query current POSIX mode

// ========================================================================
// Runtime initialization
// ========================================================================
void bash_runtime_init(void);
void bash_runtime_cleanup(void);

#ifdef __cplusplus
}
#endif
