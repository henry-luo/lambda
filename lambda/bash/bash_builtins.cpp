/**
 * Bash Built-in Commands for Lambda
 *
 * Implements Bash built-in commands: echo, printf, test, true, false, exit,
 * return, read, shift, local, export, unset, cd, pwd.
 *
 * All builtins return an exit code Item:
 * - 0 (success) = truthy in Bash
 * - non-zero (failure) = falsy in Bash
 */
#include "bash_runtime.h"
#include "../lambda-data.hpp"
#include "../transpiler.hpp"
#include "../../lib/log.h"
#include "../../lib/strbuf.h"
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <unistd.h>

// ============================================================================
// echo
// ============================================================================

extern "C" Item bash_builtin_echo(Item* args, int argc) {
    // echo: print arguments separated by spaces, followed by newline
    // supports -n (no trailing newline) and -e (enable escape sequences)
    bool no_newline = false;
    bool enable_escapes = false;
    int start = 0;

    // parse flags
    while (start < argc) {
        TypeId type = get_type_id(args[start]);
        if (type != LMD_TYPE_STRING) break;
        String* arg = it2s(args[start]);
        if (!arg) break;

        if (arg->len == 2 && arg->chars[0] == '-' && arg->chars[1] == 'n') {
            no_newline = true;
            start++;
        } else if (arg->len == 2 && arg->chars[0] == '-' && arg->chars[1] == 'e') {
            enable_escapes = true;
            start++;
        } else if (arg->len == 3 && arg->chars[0] == '-' && arg->chars[1] == 'n' && arg->chars[2] == 'e') {
            no_newline = true;
            enable_escapes = true;
            start++;
        } else if (arg->len == 3 && arg->chars[0] == '-' && arg->chars[1] == 'e' && arg->chars[2] == 'n') {
            no_newline = true;
            enable_escapes = true;
            start++;
        } else {
            break;
        }
    }

    for (int i = start; i < argc; i++) {
        if (i > start) bash_raw_putc(' ');
        Item str_val = bash_to_string(args[i]);
        String* s = it2s(str_val);
        if (!s) continue;

        if (enable_escapes) {
            // process escape sequences
            for (int j = 0; j < (int)s->len; j++) {
                if (s->chars[j] == '\\' && j + 1 < (int)s->len) {
                    switch (s->chars[j + 1]) {
                    case 'n': bash_raw_putc('\n'); j++; break;
                    case 't': bash_raw_putc('\t'); j++; break;
                    case '\\': bash_raw_putc('\\'); j++; break;
                    case 'a': bash_raw_putc('\a'); j++; break;
                    case 'b': bash_raw_putc('\b'); j++; break;
                    case 'r': bash_raw_putc('\r'); j++; break;
                    default: bash_raw_putc(s->chars[j]); break;
                    }
                } else {
                    bash_raw_putc(s->chars[j]);
                }
            }
        } else {
            bash_raw_write(s->chars, s->len);
        }
    }

    if (!no_newline) {
        bash_raw_putc('\n');
    }
    fflush(stdout);
    bash_set_exit_code(0);
    return (Item){.item = i2it(0)};
}

// ============================================================================
// printf
// ============================================================================

extern "C" Item bash_builtin_printf(Item format, Item* args, int argc) {
    // simplified printf: supports %s, %d, %f
    String* fmt = it2s(bash_to_string(format));
    if (!fmt) {
        bash_set_exit_code(1);
        return (Item){.item = i2it(1)};
    }

    int arg_idx = 0;
    for (int i = 0; i < (int)fmt->len; i++) {
        if (fmt->chars[i] == '%' && i + 1 < (int)fmt->len) {
            switch (fmt->chars[i + 1]) {
            case 's': {
                if (arg_idx < argc) {
                    Item s = bash_to_string(args[arg_idx++]);
                    String* str = it2s(s);
                    if (str) bash_raw_write(str->chars, str->len);
                }
                i++;
                break;
            }
            case 'd': {
                if (arg_idx < argc) {
                    int64_t val = 0;
                    Item arg = args[arg_idx++];
                    TypeId type = get_type_id(arg);
                    if (type == LMD_TYPE_INT) val = it2i(arg);
                    else if (type == LMD_TYPE_STRING) {
                        String* s = it2s(arg);
                        if (s) val = strtoll(s->chars, NULL, 10);
                    }
                    char buf[32];
                    int n = snprintf(buf, sizeof(buf), "%lld", (long long)val);
                    bash_raw_write(buf, n);
                }
                i++;
                break;
            }
            case '%':
                bash_raw_putc('%');
                i++;
                break;
            default:
                bash_raw_putc(fmt->chars[i]);
                break;
            }
        } else if (fmt->chars[i] == '\\' && i + 1 < (int)fmt->len) {
            // process escape sequences in format string
            switch (fmt->chars[i + 1]) {
            case 'n': bash_raw_putc('\n'); i++; break;
            case 't': bash_raw_putc('\t'); i++; break;
            case '\\': bash_raw_putc('\\'); i++; break;
            default: bash_raw_putc(fmt->chars[i]); break;
            }
        } else {
            bash_raw_putc(fmt->chars[i]);
        }
    }
    fflush(stdout);
    bash_set_exit_code(0);
    return (Item){.item = i2it(0)};
}

// ============================================================================
// test / [ ]
// ============================================================================

extern "C" Item bash_builtin_test(Item* args, int argc) {
    // simplified test implementation
    // handles: -z str, -n str, str1 = str2, str1 != str2,
    //          int1 -eq int2, int1 -ne int2, etc.
    if (argc == 0) {
        bash_set_exit_code(1);
        return (Item){.item = i2it(1)};  // false
    }

    if (argc == 1) {
        // single arg: true if non-empty string
        bool result = bash_is_truthy(args[0]);
        int code = result ? 0 : 1;
        bash_set_exit_code(code);
        return (Item){.item = i2it(code)};
    }

    if (argc == 2) {
        // unary operators: -z, -n
        String* op = it2s(args[0]);
        if (op && op->len == 2 && op->chars[0] == '-') {
            if (op->chars[1] == 'z') {
                Item r = bash_test_z(args[1]);
                int code = it2b(r) ? 0 : 1;
                bash_set_exit_code(code);
                return (Item){.item = i2it(code)};
            } else if (op->chars[1] == 'n') {
                Item r = bash_test_n(args[1]);
                int code = it2b(r) ? 0 : 1;
                bash_set_exit_code(code);
                return (Item){.item = i2it(code)};
            }
        }
        // ! expr
        if (op && op->len == 1 && op->chars[0] == '!') {
            bool result = !bash_is_truthy(args[1]);
            int code = result ? 0 : 1;
            bash_set_exit_code(code);
            return (Item){.item = i2it(code)};
        }
    }

    if (argc == 3) {
        // binary operators
        String* op = it2s(args[1]);
        if (!op) {
            bash_set_exit_code(2);
            return (Item){.item = i2it(2)};
        }

        Item left = args[0];
        Item right = args[2];
        bool result = false;

        if (op->len == 1 && op->chars[0] == '=') {
            result = it2b(bash_test_str_eq(left, right));
        } else if (op->len == 2 && memcmp(op->chars, "==", 2) == 0) {
            result = it2b(bash_test_str_eq(left, right));
        } else if (op->len == 2 && memcmp(op->chars, "!=", 2) == 0) {
            result = it2b(bash_test_str_ne(left, right));
        } else if (op->len == 3 && memcmp(op->chars, "-eq", 3) == 0) {
            result = it2b(bash_test_eq(left, right));
        } else if (op->len == 3 && memcmp(op->chars, "-ne", 3) == 0) {
            result = it2b(bash_test_ne(left, right));
        } else if (op->len == 3 && memcmp(op->chars, "-gt", 3) == 0) {
            result = it2b(bash_test_gt(left, right));
        } else if (op->len == 3 && memcmp(op->chars, "-ge", 3) == 0) {
            result = it2b(bash_test_ge(left, right));
        } else if (op->len == 3 && memcmp(op->chars, "-lt", 3) == 0) {
            result = it2b(bash_test_lt(left, right));
        } else if (op->len == 3 && memcmp(op->chars, "-le", 3) == 0) {
            result = it2b(bash_test_le(left, right));
        } else {
            log_error("bash: test: unknown operator '%.*s'", (int)op->len, op->chars);
            bash_set_exit_code(2);
            return (Item){.item = i2it(2)};
        }

        int code = result ? 0 : 1;
        bash_set_exit_code(code);
        return (Item){.item = i2it(code)};
    }

    bash_set_exit_code(2);
    return (Item){.item = i2it(2)};
}

// ============================================================================
// true / false
// ============================================================================

extern "C" Item bash_builtin_true(void) {
    bash_set_exit_code(0);
    return (Item){.item = i2it(0)};
}

extern "C" Item bash_builtin_false(void) {
    bash_set_exit_code(1);
    return (Item){.item = i2it(1)};
}

// ============================================================================
// exit / return
// ============================================================================

extern "C" Item bash_builtin_exit(Item code) {
    int exit_code = 0;
    TypeId type = get_type_id(code);
    if (type == LMD_TYPE_INT) {
        exit_code = (int)(it2i(code) & 0xFF);
    } else if (type == LMD_TYPE_STRING) {
        String* s = it2s(code);
        if (s && s->len > 0) {
            exit_code = (int)(strtol(s->chars, NULL, 10) & 0xFF);
        }
    }
    bash_set_exit_code(exit_code);
    return (Item){.item = i2it(exit_code)};
}

extern "C" Item bash_builtin_return(Item code) {
    int ret_code = 0;
    TypeId type = get_type_id(code);
    if (type == LMD_TYPE_INT) {
        ret_code = (int)(it2i(code) & 0xFF);
    } else if (type == LMD_TYPE_STRING) {
        String* s = it2s(code);
        if (s && s->len > 0) {
            ret_code = (int)(strtol(s->chars, NULL, 10) & 0xFF);
        }
    }
    bash_set_exit_code(ret_code);
    return (Item){.item = i2it(ret_code)};
}

// ============================================================================
// read
// ============================================================================

extern "C" Item bash_builtin_read(Item* args, int argc) {
    // simplified read: reads a line from stdin
    // read [-r] [-p prompt] var1 [var2 ...]
    // For now, just read a line and assign to the first variable name
    char buffer[4096];
    if (fgets(buffer, sizeof(buffer), stdin)) {
        // remove trailing newline
        size_t len = strlen(buffer);
        if (len > 0 && buffer[len - 1] == '\n') buffer[--len] = '\0';
        String* result = heap_create_name(buffer, len);
        bash_set_exit_code(0);
        return (Item){.item = s2it(result)};
    }
    bash_set_exit_code(1);
    return (Item){.item = s2it(heap_create_name("", 0))};
}

// ============================================================================
// shift
// ============================================================================

extern "C" Item bash_builtin_shift(Item n) {
    int count = 1;
    TypeId type = get_type_id(n);
    if (type == LMD_TYPE_INT) {
        count = (int)it2i(n);
    }
    return bash_shift_args(count);
}

// ============================================================================
// local / export / unset
// ============================================================================

extern "C" Item bash_builtin_local(Item name, Item value) {
    bash_set_local_var(name, value);
    bash_set_exit_code(0);
    return (Item){.item = i2it(0)};
}

extern "C" Item bash_builtin_export(Item name, Item value) {
    bash_set_var(name, value);
    bash_export_var(name);
    bash_set_exit_code(0);
    return (Item){.item = i2it(0)};
}

extern "C" Item bash_builtin_unset(Item name) {
    bash_unset_var(name);
    bash_set_exit_code(0);
    return (Item){.item = i2it(0)};
}

// ============================================================================
// cd / pwd
// ============================================================================

extern "C" Item bash_builtin_cd(Item dir) {
    String* dir_str = it2s(bash_to_string(dir));
    const char* path = NULL;

    if (!dir_str || dir_str->len == 0) {
        // cd with no args: go to $HOME
        path = getenv("HOME");
        if (!path) {
            log_error("bash: cd: HOME not set");
            bash_set_exit_code(1);
            return (Item){.item = i2it(1)};
        }
    } else {
        path = dir_str->chars;
    }

    if (chdir(path) != 0) {
        log_error("bash: cd: %s: No such file or directory", path);
        bash_set_exit_code(1);
        return (Item){.item = i2it(1)};
    }

    bash_set_exit_code(0);
    return (Item){.item = i2it(0)};
}

extern "C" Item bash_builtin_pwd(void) {
    char cwd[4096];
    if (getcwd(cwd, sizeof(cwd))) {
        printf("%s\n", cwd);
        bash_set_exit_code(0);
        return (Item){.item = s2it(heap_create_name(cwd))};
    }
    bash_set_exit_code(1);
    return (Item){.item = s2it(heap_create_name("", 0))};
}
