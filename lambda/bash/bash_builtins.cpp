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
#include <cctype>
#include <unistd.h>
#include <regex.h>

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

    bool first = true;
    for (int i = start; i < argc; i++) {
        // skip null args (from unquoted unset variable expansions)
        if (get_type_id(args[i]) == LMD_TYPE_NULL) continue;
        
        if (!first) bash_raw_putc(' ');
        first = false;
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

// helper: extract an integer argument value for printf format
static int64_t printf_get_int(Item arg) {
    TypeId type = get_type_id(arg);
    if (type == LMD_TYPE_INT) return it2i(arg);
    if (type == LMD_TYPE_STRING) {
        String* s = it2s(arg);
        if (s && s->len > 0) {
            // bash supports 0x hex and 0 octal prefixes
            if (s->len > 1 && s->chars[0] == '0' && (s->chars[1] == 'x' || s->chars[1] == 'X'))
                return strtoll(s->chars, NULL, 16);
            if (s->len > 1 && s->chars[0] == '0')
                return strtoll(s->chars, NULL, 8);
            // bash: leading quote/double-quote means character value
            if (s->len >= 2 && (s->chars[0] == '\'' || s->chars[0] == '"'))
                return (unsigned char)s->chars[1];
            return strtoll(s->chars, NULL, 10);
        }
    }
    return 0;
}

static double printf_get_float(Item arg) {
    TypeId type = get_type_id(arg);
    if (type == LMD_TYPE_INT) return (double)it2i(arg);
    if (type == LMD_TYPE_STRING) {
        String* s = it2s(arg);
        if (s && s->len > 0) return strtod(s->chars, NULL);
    }
    // try converting to string first
    Item str = bash_to_string(arg);
    String* s = it2s(str);
    if (s && s->len > 0) return strtod(s->chars, NULL);
    return 0.0;
}

// process a single pass of the format string, consuming args starting at arg_idx
// returns updated arg_idx
static int printf_format_pass(String* fmt, Item* args, int argc, int arg_idx, StrBuf* out) {
    for (int i = 0; i < (int)fmt->len; i++) {
        if (fmt->chars[i] == '%' && i + 1 < (int)fmt->len) {
            if (fmt->chars[i + 1] == '%') {
                strbuf_append_char(out, '%');
                i++;
                continue;
            }
            // collect full format specifier: flags, width, precision, conversion
            char spec[64];
            int si = 0;
            spec[si++] = '%';
            i++;
            // flags: -, +, space, 0, #
            while (i < (int)fmt->len && si < 60 &&
                   (fmt->chars[i] == '-' || fmt->chars[i] == '+' || fmt->chars[i] == ' ' ||
                    fmt->chars[i] == '0' || fmt->chars[i] == '#')) {
                spec[si++] = fmt->chars[i++];
            }
            // width (may be * for arg-supplied width)
            if (i < (int)fmt->len && fmt->chars[i] == '*') {
                int w = (arg_idx < argc) ? (int)printf_get_int(args[arg_idx++]) : 0;
                si += snprintf(spec + si, sizeof(spec) - si, "%d", w);
                i++;
            } else {
                while (i < (int)fmt->len && si < 60 && fmt->chars[i] >= '0' && fmt->chars[i] <= '9') {
                    spec[si++] = fmt->chars[i++];
                }
            }
            // precision
            if (i < (int)fmt->len && fmt->chars[i] == '.') {
                spec[si++] = '.';
                i++;
                if (i < (int)fmt->len && fmt->chars[i] == '*') {
                    int p = (arg_idx < argc) ? (int)printf_get_int(args[arg_idx++]) : 0;
                    si += snprintf(spec + si, sizeof(spec) - si, "%d", p);
                    i++;
                } else {
                    while (i < (int)fmt->len && si < 60 && fmt->chars[i] >= '0' && fmt->chars[i] <= '9') {
                        spec[si++] = fmt->chars[i++];
                    }
                }
            }
            // conversion character
            if (i >= (int)fmt->len) break;
            char conv = fmt->chars[i];
            switch (conv) {
            case 'd': case 'i': {
                // replace with lld for 64-bit
                spec[si++] = 'l';
                spec[si++] = 'l';
                spec[si++] = 'd';
                spec[si] = '\0';
                int64_t val = (arg_idx < argc) ? printf_get_int(args[arg_idx++]) : 0;
                char buf[128];
                int n = snprintf(buf, sizeof(buf), spec, (long long)val);
                strbuf_append_str_n(out, buf, n);
                break;
            }
            case 'o': {
                spec[si++] = 'l';
                spec[si++] = 'l';
                spec[si++] = 'o';
                spec[si] = '\0';
                int64_t val = (arg_idx < argc) ? printf_get_int(args[arg_idx++]) : 0;
                char buf[128];
                int n = snprintf(buf, sizeof(buf), spec, (long long)val);
                strbuf_append_str_n(out, buf, n);
                break;
            }
            case 'u': {
                spec[si++] = 'l';
                spec[si++] = 'l';
                spec[si++] = 'u';
                spec[si] = '\0';
                int64_t val = (arg_idx < argc) ? printf_get_int(args[arg_idx++]) : 0;
                char buf[128];
                int n = snprintf(buf, sizeof(buf), spec, (unsigned long long)val);
                strbuf_append_str_n(out, buf, n);
                break;
            }
            case 'x': case 'X': {
                spec[si++] = 'l';
                spec[si++] = 'l';
                spec[si++] = conv;
                spec[si] = '\0';
                int64_t val = (arg_idx < argc) ? printf_get_int(args[arg_idx++]) : 0;
                char buf[128];
                int n = snprintf(buf, sizeof(buf), spec, (unsigned long long)val);
                strbuf_append_str_n(out, buf, n);
                break;
            }
            case 'f': case 'F': case 'e': case 'E': case 'g': case 'G': {
                spec[si++] = conv;
                spec[si] = '\0';
                double val = (arg_idx < argc) ? printf_get_float(args[arg_idx++]) : 0.0;
                char buf[256];
                int n = snprintf(buf, sizeof(buf), spec, val);
                strbuf_append_str_n(out, buf, n);
                break;
            }
            case 's': {
                spec[si++] = 's';
                spec[si] = '\0';
                const char* str = "";
                if (arg_idx < argc) {
                    Item s = bash_to_string(args[arg_idx++]);
                    String* ss = it2s(s);
                    if (ss) str = ss->chars;
                }
                char buf[4096];
                int n = snprintf(buf, sizeof(buf), spec, str);
                strbuf_append_str_n(out, buf, n > (int)sizeof(buf) - 1 ? (int)sizeof(buf) - 1 : n);
                break;
            }
            case 'c': {
                char ch = ' ';
                if (arg_idx < argc) {
                    Item s = bash_to_string(args[arg_idx++]);
                    String* ss = it2s(s);
                    if (ss && ss->len > 0) ch = ss->chars[0];
                }
                strbuf_append_char(out, ch);
                break;
            }
            case 'b': {
                // %b: like %s but interpret backslash escapes
                const char* str = "";
                if (arg_idx < argc) {
                    Item s = bash_to_string(args[arg_idx++]);
                    String* ss = it2s(s);
                    if (ss) str = ss->chars;
                }
                for (const char* p = str; *p; p++) {
                    if (*p == '\\' && *(p + 1)) {
                        p++;
                        switch (*p) {
                        case 'n': strbuf_append_char(out, '\n'); break;
                        case 't': strbuf_append_char(out, '\t'); break;
                        case 'r': strbuf_append_char(out, '\r'); break;
                        case 'a': strbuf_append_char(out, '\a'); break;
                        case 'b': strbuf_append_char(out, '\b'); break;
                        case 'f': strbuf_append_char(out, '\f'); break;
                        case 'v': strbuf_append_char(out, '\v'); break;
                        case '\\': strbuf_append_char(out, '\\'); break;
                        case '0': {
                            // octal
                            int val = 0;
                            for (int j = 0; j < 3 && p[1] >= '0' && p[1] <= '7'; j++) {
                                p++;
                                val = val * 8 + (*p - '0');
                            }
                            strbuf_append_char(out, (char)val);
                            break;
                        }
                        default: strbuf_append_char(out, '\\'); strbuf_append_char(out, *p); break;
                        }
                    } else {
                        strbuf_append_char(out, *p);
                    }
                }
                break;
            }
            case 'q': {
                // %q: shell-quote the argument (like bash's printf %q)
                const char* str = "";
                if (arg_idx < argc) {
                    Item s = bash_to_string(args[arg_idx++]);
                    String* ss = it2s(s);
                    if (ss) str = ss->chars;
                }
                // check if needs quoting: chars that are special in shell
                bool needs_quoting = (*str == '\0');
                for (const char* p = str; *p && !needs_quoting; p++) {
                    if (!isalnum((unsigned char)*p) && *p != '_' && *p != '-' && *p != '.' && *p != '/' && *p != ':' && *p != '@' && *p != ',') {
                        needs_quoting = true;
                    }
                }
                if (!needs_quoting) {
                    strbuf_append_str(out, str);
                } else {
                    // escape with backslashes or use $'...' form
                    int has_nonprint = 0;
                    for (const char* p2 = str; *p2; p2++) {
                        if ((unsigned char)*p2 < 0x20 || (unsigned char)*p2 == 0x7f) {
                            has_nonprint = 1; break;
                        }
                    }
                    if (!has_nonprint) {
                        static const char special_chars[] = " \t!\"#$&'()*;<=>?[\\]^`{|}~";
                        for (const char* p2 = str; *p2; p2++) {
                            if (strchr(special_chars, *p2)) strbuf_append_char(out, '\\');
                            strbuf_append_char(out, *p2);
                        }
                    } else {
                        // use $'...' form for non-printable chars
                        strbuf_append_str(out, "$'");
                        for (const char* p2 = str; *p2; p2++) {
                            if (*p2 == '\\' || *p2 == '\'') {
                                strbuf_append_char(out, '\\');
                                strbuf_append_char(out, *p2);
                            } else if ((unsigned char)*p2 < 0x20) {
                                char esc[8];
                                snprintf(esc, sizeof(esc), "\\x%02x", (unsigned char)*p2);
                                strbuf_append_str(out, esc);
                            } else {
                                strbuf_append_char(out, *p2);
                            }
                        }
                        strbuf_append_char(out, '\'');
                    }
                }
                break;
            }
            default:
                // unknown conversion: output literally
                strbuf_append_str_n(out, spec, si);
                strbuf_append_char(out, conv);
                break;
            }
        } else if (fmt->chars[i] == '\\' && i + 1 < (int)fmt->len) {
            switch (fmt->chars[i + 1]) {
            case 'n': strbuf_append_char(out, '\n'); i++; break;
            case 't': strbuf_append_char(out, '\t'); i++; break;
            case 'r': strbuf_append_char(out, '\r'); i++; break;
            case 'a': strbuf_append_char(out, '\a'); i++; break;
            case 'b': strbuf_append_char(out, '\b'); i++; break;
            case 'f': strbuf_append_char(out, '\f'); i++; break;
            case 'v': strbuf_append_char(out, '\v'); i++; break;
            case '\\': strbuf_append_char(out, '\\'); i++; break;
            case '0': {
                // octal escape \0NNN
                int val = 0;
                i++; // skip '0'
                for (int j = 0; j < 3 && i + 1 < (int)fmt->len && fmt->chars[i + 1] >= '0' && fmt->chars[i + 1] <= '7'; j++) {
                    i++;
                    val = val * 8 + (fmt->chars[i] - '0');
                }
                strbuf_append_char(out, (char)val);
                break;
            }
            default: strbuf_append_char(out, fmt->chars[i]); break;
            }
        } else {
            strbuf_append_char(out, fmt->chars[i]);
        }
    }
    return arg_idx;
}

extern "C" Item bash_builtin_printf(Item* all_args, int total_argc) {
    if (total_argc == 0) {
        bash_set_exit_code(0);
        return (Item){.item = i2it(0)};
    }

    int idx = 0;
    // check for -v varname
    bool assign_to_var = false;
    String* var_name = NULL;
    if (total_argc >= 3) {
        String* first = it2s(bash_to_string(all_args[0]));
        if (first && first->len == 2 && first->chars[0] == '-' && first->chars[1] == 'v') {
            assign_to_var = true;
            var_name = it2s(bash_to_string(all_args[1]));
            idx = 2;
        }
    }

    if (idx >= total_argc) {
        bash_set_exit_code(0);
        return (Item){.item = i2it(0)};
    }

    // next arg is the format string
    String* fmt = it2s(bash_to_string(all_args[idx]));
    if (!fmt) {
        bash_set_exit_code(1);
        return (Item){.item = i2it(1)};
    }
    idx++;

    Item* args = all_args + idx;
    int argc = total_argc - idx;

    StrBuf* out = strbuf_new();

    // bash printf reuses format string if there are remaining args
    int arg_idx = 0;
    do {
        arg_idx = printf_format_pass(fmt, args, argc, arg_idx, out);
    } while (arg_idx < argc && arg_idx > 0);

    if (assign_to_var && var_name) {
        Item name_item = (Item){.item = s2it(heap_create_name(var_name->chars, var_name->len))};
        Item val_item = (Item){.item = s2it(heap_create_name(out->str, out->length))};
        bash_set_var(name_item, val_item);
    } else {
        if (out->length > 0) {
            bash_raw_write(out->str, (int)out->length);
        }
        fflush(stdout);
    }

    strbuf_free(out);
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
    // read [-r] [-p prompt] [-d delim] [-n nchars] [-t timeout] var1 [var2 ...]
    // Reads a line from stdin_item (herestring/pipe) or real stdin, then
    // splits by IFS and assigns to named variables.
    bool raw_mode = false;  // -r: no backslash escaping
    int var_start = 0;      // index of first variable name in args

    // parse flags
    for (int i = 0; i < argc; i++) {
        String* arg = it2s(bash_to_string(args[i]));
        if (!arg || arg->len == 0 || arg->chars[0] != '-') break;
        if (arg->len == 2 && arg->chars[1] == 'r') {
            raw_mode = true;
            var_start = i + 1;
        } else if (arg->len == 2 && (arg->chars[1] == 'p' || arg->chars[1] == 'd' ||
                                      arg->chars[1] == 'n' || arg->chars[1] == 't' ||
                                      arg->chars[1] == 'u' || arg->chars[1] == 'a')) {
            // flags that take an argument: skip the next arg too
            var_start = i + 2;
            i++; // skip the argument to this flag
        } else {
            var_start = i + 1;
        }
    }

    // collect variable names
    int num_vars = argc - var_start;
    if (num_vars <= 0) {
        // default variable is REPLY
        num_vars = 0; // we'll handle it below
    }

    // read input line: prefer stdin_item (pipe/herestring), then real stdin
    char buffer[4096];
    const char* line = NULL;
    int line_len = 0;

    Item stdin_item = bash_get_stdin_item();
    String* stdin_str = it2s(bash_to_string(stdin_item));
    if (stdin_str && stdin_str->len > 0) {
        // read first line from stdin_item
        line = stdin_str->chars;
        line_len = stdin_str->len;
        // find end of first line
        for (int i = 0; i < line_len; i++) {
            if (line[i] == '\n') {
                line_len = i;
                break;
            }
        }
    } else {
        // read from real stdin
        if (fgets(buffer, sizeof(buffer), stdin)) {
            line_len = (int)strlen(buffer);
            if (line_len > 0 && buffer[line_len - 1] == '\n') line_len--;
            line = buffer;
        } else {
            // EOF: assign empty to all vars and return 1
            Item empty = (Item){.item = s2it(heap_create_name("", 0))};
            if (num_vars == 0) {
                Item reply_name = (Item){.item = s2it(heap_create_name("REPLY", 5))};
                bash_set_var(reply_name, empty);
            } else {
                for (int i = var_start; i < argc; i++) {
                    bash_set_var(args[i], empty);
                }
            }
            bash_set_exit_code(1);
            return (Item){.item = i2it(1)};
        }
    }

    // strip trailing whitespace (bash strips trailing IFS whitespace)
    while (line_len > 0 && (line[line_len - 1] == ' ' || line[line_len - 1] == '\t')) {
        line_len--;
    }

    (void)raw_mode; // TODO: handle backslash escaping in non-raw mode

    // if no variable names specified, assign whole line to REPLY
    if (num_vars <= 0) {
        Item reply_name = (Item){.item = s2it(heap_create_name("REPLY", 5))};
        String* val = heap_create_name(line, line_len);
        bash_set_var(reply_name, (Item){.item = s2it(val)});
        bash_set_exit_code(0);
        return (Item){.item = i2it(0)};
    }

    // get IFS for splitting (default: space, tab, newline)
    Item ifs_name = (Item){.item = s2it(heap_create_name("IFS", 3))};
    Item ifs_item = bash_get_var(ifs_name);
    String* ifs_str = it2s(bash_to_string(ifs_item));
    const char* ifs = " \t\n";
    int ifs_len = 3;
    if (ifs_str && ifs_str->len > 0) {
        ifs = ifs_str->chars;
        ifs_len = ifs_str->len;
    }

    // split line by IFS and assign to variables
    int pos = 0;
    for (int vi = 0; vi < num_vars; vi++) {
        int arg_idx = var_start + vi;
        bool is_last = (vi == num_vars - 1);

        // skip leading IFS whitespace
        while (pos < line_len) {
            bool is_ifs = false;
            for (int k = 0; k < ifs_len; k++) {
                if (line[pos] == ifs[k]) { is_ifs = true; break; }
            }
            if (!is_ifs) break;
            pos++;
        }

        if (is_last) {
            // last variable gets the rest of the line
            String* val = heap_create_name(line + pos, line_len - pos);
            bash_set_var(args[arg_idx], (Item){.item = s2it(val)});
        } else {
            // find next IFS delimiter
            int word_start = pos;
            while (pos < line_len) {
                bool is_ifs = false;
                for (int k = 0; k < ifs_len; k++) {
                    if (line[pos] == ifs[k]) { is_ifs = true; break; }
                }
                if (is_ifs) break;
                pos++;
            }
            String* val = heap_create_name(line + word_start, pos - word_start);
            bash_set_var(args[arg_idx], (Item){.item = s2it(val)});
            // skip trailing IFS delimiter(s)
            if (pos < line_len) pos++;
        }
    }

    bash_set_exit_code(0);
    return (Item){.item = i2it(0)};
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
        Item home_name = (Item){.item = s2it(heap_create_name("HOME", 4))};
        Item home_val = bash_get_var(home_name);
        String* home_str = it2s(bash_to_string(home_val));
        if (home_str && home_str->len > 0) {
            path = home_str->chars;
        } else {
            path = getenv("HOME");
        }
        if (!path) {
            log_error("bash: cd: HOME not set");
            bash_set_exit_code(1);
            return (Item){.item = i2it(1)};
        }
    } else {
        path = dir_str->chars;
    }

    // get current logical PWD (to save as OLDPWD)
    Item pwd_name = (Item){.item = s2it(heap_create_name("PWD", 3))};
    Item cur_pwd = bash_get_var(pwd_name);
    String* cur_pwd_str = it2s(bash_to_string(cur_pwd));
    char old_pwd_buf[4096];
    if (cur_pwd_str && cur_pwd_str->len > 0) {
        snprintf(old_pwd_buf, sizeof(old_pwd_buf), "%s", cur_pwd_str->chars);
    } else {
        if (getcwd(old_pwd_buf, sizeof(old_pwd_buf)) == NULL) old_pwd_buf[0] = '\0';
    }

    if (chdir(path) != 0) {
        log_error("bash: cd: %s: No such file or directory", path);
        bash_set_exit_code(1);
        return (Item){.item = i2it(1)};
    }

    // update OLDPWD from logical PWD
    Item oldpwd_name = (Item){.item = s2it(heap_create_name("OLDPWD", 6))};
    Item oldpwd_val = (Item){.item = s2it(heap_create_name(old_pwd_buf, strlen(old_pwd_buf)))};
    bash_set_var(oldpwd_name, oldpwd_val);

    // update PWD: use path as-is if absolute, otherwise compute logical path
    const char* new_pwd;
    char new_pwd_buf[4096];
    if (path[0] == '/') {
        new_pwd = path;
    } else {
        // relative path: old_pwd + "/" + path
        snprintf(new_pwd_buf, sizeof(new_pwd_buf), "%s/%s", old_pwd_buf, path);
        new_pwd = new_pwd_buf;
    }
    Item pwd_val = (Item){.item = s2it(heap_create_name(new_pwd, strlen(new_pwd)))};
    bash_set_var(pwd_name, pwd_val);

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

extern "C" Item bash_builtin_caller(Item* args, int argc) {
    int frame = 0;
    if (argc > 0) {
        String* arg = it2s(bash_to_string(args[0]));
        if (!arg || arg->len == 0) {
            bash_set_exit_code(1);
            return (Item){.item = i2it(1)};
        }
        if (arg->chars[0] == '-' && !(arg->len > 1 && isdigit((unsigned char)arg->chars[1]))) {
            String* src = it2s(bash_get_bash_source((Item){.item = i2it(0)}));
            fflush(stdout);
            fprintf(stderr, "%.*s: line %d: caller: %.*s: invalid option\n",
                    src ? src->len : 0, src ? src->chars : "",
                    (int)it2i(bash_get_lineno()), arg->len, arg->chars);
            fprintf(stderr, "caller: usage: caller [expr]\n");
            bash_set_exit_code(2);
            return (Item){.item = i2it(2)};
        }
        char buf[64];
        int copy_len = arg->len < (int)sizeof(buf) - 1 ? arg->len : (int)sizeof(buf) - 1;
        memcpy(buf, arg->chars, copy_len);
        buf[copy_len] = '\0';
        char* end = NULL;
        long parsed = strtol(buf, &end, 10);
        if (!end || *end != '\0' || parsed < 0) {
            String* src = it2s(bash_get_bash_source((Item){.item = i2it(0)}));
            fflush(stdout);
            fprintf(stderr, "%.*s: line %d: caller: %.*s: invalid number\n",
                    src ? src->len : 0, src ? src->chars : "",
                    (int)it2i(bash_get_lineno()), arg->len, arg->chars);
            fprintf(stderr, "caller: usage: caller [expr]\n");
            bash_set_exit_code(2);
            return (Item){.item = i2it(2)};
        }
        frame = (int)parsed;
    }

    Item line_item = bash_get_bash_lineno((Item){.item = i2it(frame)});
    int line = (int)it2i(line_item);
    String* src = it2s(bash_get_bash_source((Item){.item = i2it(frame + 1)}));
    String* func = it2s(bash_get_funcname((Item){.item = i2it(frame + 1)}));

    if (argc == 0 && line == 0) {
        bash_raw_write("0 NULL\n", 7);
        bash_set_exit_code(0);
        return (Item){.item = i2it(0)};
    }

    if (line == 0 || !src || src->len == 0) {
        bash_set_exit_code(1);
        return (Item){.item = i2it(1)};
    }

    char buf[1024];
    if (argc == 0) {
        int n = snprintf(buf, sizeof(buf), "%d %.*s\n", line, src->len, src->chars);
        bash_raw_write(buf, n);
    } else if (func && func->len > 0) {
        int n = snprintf(buf, sizeof(buf), "%d %.*s %.*s\n", line, func->len, func->chars, src->len, src->chars);
        bash_raw_write(buf, n);
    } else {
        int n = snprintf(buf, sizeof(buf), "%d %.*s\n", line, src->len, src->chars);
        bash_raw_write(buf, n);
    }

    bash_set_exit_code(0);
    return (Item){.item = i2it(0)};
}

// ============================================================================
// Pipeline builtins: cat, wc, head, tail, grep, sort, tr, cut
// These read from the stdin item set by the pipeline capture chain.
// ============================================================================

// cat: read stdin item and write to stdout (identity pipe stage)
extern "C" Item bash_builtin_cat(Item* args, int argc) {
    (void)args; (void)argc;
    Item input = bash_get_stdin_item();
    String* s = it2s(bash_to_string(input));
    if (s && s->len > 0) {
        bash_raw_write(s->chars, s->len);
    }
    bash_set_exit_code(0);
    return (Item){.item = i2it(0)};
}

// wc: word/line/char count
// supports -l (lines), -w (words), -c (bytes/chars)
// default (no flags) prints lines, words, chars
extern "C" Item bash_builtin_wc(Item* args, int argc) {
    bool flag_l = false, flag_w = false, flag_c = false;
    int start = 0;

    for (int i = 0; i < argc; i++) {
        String* arg = it2s(bash_to_string(args[i]));
        if (!arg || arg->len == 0 || arg->chars[0] != '-') break;
        for (int j = 1; j < (int)arg->len; j++) {
            if (arg->chars[j] == 'l') flag_l = true;
            else if (arg->chars[j] == 'w') flag_w = true;
            else if (arg->chars[j] == 'c') flag_c = true;
        }
        start++;
    }

    if (!flag_l && !flag_w && !flag_c) {
        flag_l = flag_w = flag_c = true;
    }

    Item input = bash_get_stdin_item();
    String* s = it2s(bash_to_string(input));
    int lines = 0, words = 0, chars = 0;
    if (s && s->len > 0) {
        chars = s->len;
        bool in_word = false;
        for (int i = 0; i < (int)s->len; i++) {
            if (s->chars[i] == '\n') lines++;
            if (s->chars[i] == ' ' || s->chars[i] == '\t' || s->chars[i] == '\n') {
                in_word = false;
            } else {
                if (!in_word) words++;
                in_word = true;
            }
        }
    }

    char buf[128];
    int pos = 0;
    if (flag_l) {
        pos += snprintf(buf + pos, sizeof(buf) - pos, "%d", lines);
    }
    if (flag_w) {
        if (pos > 0) buf[pos++] = ' ';
        pos += snprintf(buf + pos, sizeof(buf) - pos, "%d", words);
    }
    if (flag_c) {
        if (pos > 0) buf[pos++] = ' ';
        pos += snprintf(buf + pos, sizeof(buf) - pos, "%d", chars);
    }
    bash_raw_write(buf, pos);
    bash_raw_putc('\n');
    bash_set_exit_code(0);
    return (Item){.item = i2it(0)};
}

// head: first N lines (default 10)
extern "C" Item bash_builtin_head(Item* args, int argc) {
    int n = 10;
    for (int i = 0; i < argc - 1; i++) {
        String* arg = it2s(bash_to_string(args[i]));
        if (arg && arg->len == 2 && arg->chars[0] == '-' && arg->chars[1] == 'n') {
            String* val = it2s(bash_to_string(args[i + 1]));
            if (val) n = (int)strtol(val->chars, NULL, 10);
            break;
        }
        if (arg && arg->len >= 2 && arg->chars[0] == '-' && arg->chars[1] >= '0' && arg->chars[1] <= '9') {
            n = (int)strtol(arg->chars + 1, NULL, 10);
            break;
        }
    }

    Item input = bash_get_stdin_item();
    String* s = it2s(bash_to_string(input));
    if (!s || s->len == 0) {
        bash_set_exit_code(0);
        return (Item){.item = i2it(0)};
    }

    int line_count = 0;
    int end = 0;
    for (int i = 0; i < (int)s->len && line_count < n; i++) {
        if (s->chars[i] == '\n') line_count++;
        end = i + 1;
    }
    if (end > 0) {
        bash_raw_write(s->chars, end);
    }
    bash_set_exit_code(0);
    return (Item){.item = i2it(0)};
}

// tail: last N lines (default 10)
extern "C" Item bash_builtin_tail(Item* args, int argc) {
    int n = 10;
    for (int i = 0; i < argc - 1; i++) {
        String* arg = it2s(bash_to_string(args[i]));
        if (arg && arg->len == 2 && arg->chars[0] == '-' && arg->chars[1] == 'n') {
            String* val = it2s(bash_to_string(args[i + 1]));
            if (val) n = (int)strtol(val->chars, NULL, 10);
            break;
        }
        if (arg && arg->len >= 2 && arg->chars[0] == '-' && arg->chars[1] >= '0' && arg->chars[1] <= '9') {
            n = (int)strtol(arg->chars + 1, NULL, 10);
            break;
        }
    }

    Item input = bash_get_stdin_item();
    String* s = it2s(bash_to_string(input));
    if (!s || s->len == 0) {
        bash_set_exit_code(0);
        return (Item){.item = i2it(0)};
    }

    int total_lines = 0;
    for (int i = 0; i < (int)s->len; i++) {
        if (s->chars[i] == '\n') total_lines++;
    }
    if (s->len > 0 && s->chars[s->len - 1] != '\n') total_lines++;

    int skip = total_lines - n;
    if (skip < 0) skip = 0;

    int line_count = 0;
    int start = 0;
    for (int i = 0; i < (int)s->len && line_count < skip; i++) {
        if (s->chars[i] == '\n') {
            line_count++;
            start = i + 1;
        }
    }
    if (start < (int)s->len) {
        bash_raw_write(s->chars + start, s->len - start);
    }
    bash_set_exit_code(0);
    return (Item){.item = i2it(0)};
}

// grep: pattern matching (POSIX regex)
// supports -v (invert), -c (count), -i (case insensitive), -q (quiet)
extern "C" Item bash_builtin_grep(Item* args, int argc) {
    bool flag_v = false, flag_c = false, flag_i = false, flag_q = false;
    int start = 0;

    for (int i = 0; i < argc; i++) {
        String* arg = it2s(bash_to_string(args[i]));
        if (!arg || arg->len == 0 || arg->chars[0] != '-') break;
        for (int j = 1; j < (int)arg->len; j++) {
            if (arg->chars[j] == 'v') flag_v = true;
            else if (arg->chars[j] == 'c') flag_c = true;
            else if (arg->chars[j] == 'i') flag_i = true;
            else if (arg->chars[j] == 'q') flag_q = true;
        }
        start++;
    }

    if (start >= argc) {
        bash_set_exit_code(2);
        return (Item){.item = i2it(2)};
    }
    String* pattern = it2s(bash_to_string(args[start]));
    if (!pattern) {
        bash_set_exit_code(2);
        return (Item){.item = i2it(2)};
    }

    regex_t regex;
    int cflags = REG_EXTENDED | REG_NOSUB;
    if (flag_i) cflags |= REG_ICASE;
    if (regcomp(&regex, pattern->chars, cflags) != 0) {
        bash_set_exit_code(2);
        return (Item){.item = i2it(2)};
    }

    Item input = bash_get_stdin_item();
    String* s = it2s(bash_to_string(input));
    int match_count = 0;
    bool any_match = false;

    if (s && s->len > 0) {
        int line_start = 0;
        for (int i = 0; i <= (int)s->len; i++) {
            if (i == (int)s->len || s->chars[i] == '\n') {
                // skip trailing empty line (input ends with \n)
                if (i == (int)s->len && i == line_start) break;
                int line_len = i - line_start;
                char* line_buf = (char*)malloc(line_len + 1);
                memcpy(line_buf, s->chars + line_start, line_len);
                line_buf[line_len] = '\0';

                bool matched = (regexec(&regex, line_buf, 0, NULL, 0) == 0);
                if (flag_v) matched = !matched;

                if (matched) {
                    any_match = true;
                    match_count++;
                    if (!flag_c && !flag_q) {
                        bash_raw_write(s->chars + line_start, line_len);
                        bash_raw_putc('\n');
                    }
                }
                free(line_buf);
                line_start = i + 1;
            }
        }
    }

    regfree(&regex);

    if (flag_c) {
        char buf[32];
        int n = snprintf(buf, sizeof(buf), "%d", match_count);
        bash_raw_write(buf, n);
        bash_raw_putc('\n');
    }

    bash_set_exit_code(any_match ? 0 : 1);
    return (Item){.item = i2it(any_match ? 0 : 1)};
}

// sort: line sorting
// supports -r (reverse), -n (numeric)
extern "C" Item bash_builtin_sort(Item* args, int argc) {
    bool flag_r = false, flag_n = false;
    for (int i = 0; i < argc; i++) {
        String* arg = it2s(bash_to_string(args[i]));
        if (!arg || arg->len == 0 || arg->chars[0] != '-') break;
        for (int j = 1; j < (int)arg->len; j++) {
            if (arg->chars[j] == 'r') flag_r = true;
            else if (arg->chars[j] == 'n') flag_n = true;
        }
    }

    Item input = bash_get_stdin_item();
    String* s = it2s(bash_to_string(input));
    if (!s || s->len == 0) {
        bash_set_exit_code(0);
        return (Item){.item = i2it(0)};
    }

    int max_lines = 1024;
    const char** lines = (const char**)malloc(max_lines * sizeof(const char*));
    int* lens = (int*)malloc(max_lines * sizeof(int));
    int line_count = 0;
    int line_start = 0;

    for (int i = 0; i <= (int)s->len; i++) {
        if (i == (int)s->len || s->chars[i] == '\n') {
            if (line_count >= max_lines) {
                max_lines *= 2;
                lines = (const char**)realloc(lines, max_lines * sizeof(const char*));
                lens = (int*)realloc(lens, max_lines * sizeof(int));
            }
            lines[line_count] = s->chars + line_start;
            lens[line_count] = i - line_start;
            line_count++;
            line_start = i + 1;
        }
    }

    if (line_count > 0 && lens[line_count - 1] == 0 && s->len > 0 && s->chars[s->len - 1] == '\n') {
        line_count--;
    }

    // insertion sort
    for (int i = 1; i < line_count; i++) {
        const char* key_line = lines[i];
        int key_len = lens[i];
        int j = i - 1;
        while (j >= 0) {
            int cmp;
            if (flag_n) {
                long a = strtol(lines[j], NULL, 10);
                long b = strtol(key_line, NULL, 10);
                cmp = (a > b) ? 1 : (a < b) ? -1 : 0;
            } else {
                int min_len = lens[j] < key_len ? lens[j] : key_len;
                cmp = memcmp(lines[j], key_line, min_len);
                if (cmp == 0) cmp = lens[j] - key_len;
            }
            if (flag_r) cmp = -cmp;
            if (cmp <= 0) break;
            lines[j + 1] = lines[j];
            lens[j + 1] = lens[j];
            j--;
        }
        lines[j + 1] = key_line;
        lens[j + 1] = key_len;
    }

    for (int i = 0; i < line_count; i++) {
        bash_raw_write(lines[i], lens[i]);
        bash_raw_putc('\n');
    }

    free(lines);
    free(lens);
    bash_set_exit_code(0);
    return (Item){.item = i2it(0)};
}

// tr: character transliteration
// supports tr SET1 SET2 and tr -d SET1 (delete)
extern "C" Item bash_builtin_tr(Item* args, int argc) {
    bool flag_d = false;
    int start = 0;

    for (int i = 0; i < argc; i++) {
        String* arg = it2s(bash_to_string(args[i]));
        if (!arg || arg->len == 0 || arg->chars[0] != '-') break;
        if (arg->len == 2 && arg->chars[1] == 'd') flag_d = true;
        start++;
    }

    if (start >= argc) {
        bash_set_exit_code(1);
        return (Item){.item = i2it(1)};
    }

    String* set1 = it2s(bash_to_string(args[start]));
    String* set2 = NULL;
    if (!flag_d && start + 1 < argc) {
        set2 = it2s(bash_to_string(args[start + 1]));
    }

    Item input = bash_get_stdin_item();
    String* s = it2s(bash_to_string(input));
    if (!s || s->len == 0 || !set1) {
        bash_set_exit_code(0);
        return (Item){.item = i2it(0)};
    }

    for (int i = 0; i < (int)s->len; i++) {
        char c = s->chars[i];
        int idx = -1;
        for (int j = 0; j < (int)set1->len; j++) {
            if (set1->chars[j] == c) { idx = j; break; }
        }
        if (idx >= 0) {
            if (flag_d) continue;
            if (set2 && set2->len > 0) {
                int map_idx = idx < (int)set2->len ? idx : (int)set2->len - 1;
                bash_raw_putc(set2->chars[map_idx]);
            }
        } else {
            bash_raw_putc(c);
        }
    }
    bash_set_exit_code(0);
    return (Item){.item = i2it(0)};
}

// cut: field extraction
// supports -d DELIM -f FIELD
extern "C" Item bash_builtin_cut(Item* args, int argc) {
    char delim = '\t';
    int field = 0; // 1-based

    for (int i = 0; i < argc; i++) {
        String* arg = it2s(bash_to_string(args[i]));
        if (!arg) continue;
        if (arg->len == 2 && arg->chars[0] == '-' && arg->chars[1] == 'd') {
            if (i + 1 < argc) {
                String* d = it2s(bash_to_string(args[++i]));
                if (d && d->len > 0) delim = d->chars[0];
            }
        } else if (arg->len == 2 && arg->chars[0] == '-' && arg->chars[1] == 'f') {
            if (i + 1 < argc) {
                String* f = it2s(bash_to_string(args[++i]));
                if (f) field = (int)strtol(f->chars, NULL, 10);
            }
        }
    }

    Item input = bash_get_stdin_item();
    String* s = it2s(bash_to_string(input));
    if (!s || s->len == 0 || field <= 0) {
        if (s && s->len > 0) bash_raw_write(s->chars, s->len);
        bash_set_exit_code(0);
        return (Item){.item = i2it(0)};
    }

    int line_start = 0;
    for (int i = 0; i <= (int)s->len; i++) {
        if (i == (int)s->len || s->chars[i] == '\n') {
            // skip trailing empty line
            if (i == (int)s->len && i == line_start) break;
            int line_len = i - line_start;
            // extract the requested field from this line
            int cur_field = 1;
            int field_start = line_start;
            int field_end = line_start + line_len; // default: rest of line
            bool found_field = false;
            for (int j = line_start; j < line_start + line_len; j++) {
                if (s->chars[j] == delim) {
                    if (cur_field == field) {
                        field_end = j;
                        found_field = true;
                        break;
                    }
                    cur_field++;
                    field_start = j + 1;
                }
            }
            if (cur_field >= field) {
                if (!found_field) field_end = line_start + line_len;
                bash_raw_write(s->chars + field_start, field_end - field_start);
            }
            if (i < (int)s->len) bash_raw_putc('\n');
            line_start = i + 1;
        }
    }
    bash_set_exit_code(0);
    return (Item){.item = i2it(0)};
}

// ============================================================================
// let — evaluate arithmetic expressions
// ============================================================================

// mini recursive-descent parser for bash arithmetic expressions
// supports: =, +=, -=, *=, /=, %=, +, -, *, /, %, ( ), variable refs, integers

struct LetParser {
    const char* src;
    int pos;
    int len;
};

static void let_skip_ws(LetParser* p) {
    while (p->pos < p->len && (p->src[p->pos] == ' ' || p->src[p->pos] == '\t'))
        p->pos++;
}

static int64_t let_parse_expr(LetParser* p);
static int64_t let_parse_assign(LetParser* p);

static int64_t let_get_var_value(const char* name, int name_len) {
    String* s = heap_create_name(name, name_len);
    Item name_item = (Item){.item = s2it(s)};
    Item val = bash_get_var(name_item);
    TypeId t = get_type_id(val);
    if (t == LMD_TYPE_INT) return it2i(val);
    if (t == LMD_TYPE_STRING) {
        String* vs = it2s(val);
        if (!vs || vs->len == 0) return 0;
        // try recursive variable reference: if the string is a variable name
        char* endptr;
        long long v = strtoll(vs->chars, &endptr, 10);
        if (endptr != vs->chars) return (int64_t)v;
        // might be a variable name reference (bash allows var indirection in arithmetic)
        return let_get_var_value(vs->chars, vs->len);
    }
    return 0;
}

static void let_set_var_value(const char* name, int name_len, int64_t value) {
    String* s = heap_create_name(name, name_len);
    Item name_item = (Item){.item = s2it(s)};
    Item val_item = (Item){.item = i2it(value)};
    bash_set_var(name_item, val_item);
}

static int64_t let_parse_primary(LetParser* p) {
    let_skip_ws(p);
    if (p->pos >= p->len) return 0;

    char c = p->src[p->pos];

    // parenthesized expression
    if (c == '(') {
        p->pos++;
        int64_t val = let_parse_expr(p);
        let_skip_ws(p);
        if (p->pos < p->len && p->src[p->pos] == ')') p->pos++;
        return val;
    }

    // unary minus
    if (c == '-') {
        p->pos++;
        return -let_parse_primary(p);
    }

    // unary plus
    if (c == '+' && (p->pos + 1 >= p->len || p->src[p->pos+1] != '=')) {
        p->pos++;
        return let_parse_primary(p);
    }

    // logical not
    if (c == '!') {
        p->pos++;
        return !let_parse_primary(p);
    }

    // bitwise not
    if (c == '~') {
        p->pos++;
        return ~let_parse_primary(p);
    }

    // number
    if (c >= '0' && c <= '9') {
        int64_t val = 0;
        // handle base prefixes
        if (c == '0' && p->pos + 1 < p->len) {
            char next = p->src[p->pos + 1];
            if (next == 'x' || next == 'X') {
                p->pos += 2;
                while (p->pos < p->len) {
                    char h = p->src[p->pos];
                    if (h >= '0' && h <= '9') val = val * 16 + (h - '0');
                    else if (h >= 'a' && h <= 'f') val = val * 16 + (h - 'a' + 10);
                    else if (h >= 'A' && h <= 'F') val = val * 16 + (h - 'A' + 10);
                    else break;
                    p->pos++;
                }
                return val;
            }
        }
        while (p->pos < p->len && p->src[p->pos] >= '0' && p->src[p->pos] <= '9') {
            val = val * 10 + (p->src[p->pos] - '0');
            p->pos++;
        }
        return val;
    }

    // variable name (or variable reference with $)
    bool has_dollar = false;
    if (c == '$') { has_dollar = true; p->pos++; let_skip_ws(p); }

    if (p->pos < p->len && (p->src[p->pos] == '_' ||
        (p->src[p->pos] >= 'a' && p->src[p->pos] <= 'z') ||
        (p->src[p->pos] >= 'A' && p->src[p->pos] <= 'Z'))) {
        int start = p->pos;
        while (p->pos < p->len && (p->src[p->pos] == '_' ||
               (p->src[p->pos] >= 'a' && p->src[p->pos] <= 'z') ||
               (p->src[p->pos] >= 'A' && p->src[p->pos] <= 'Z') ||
               (p->src[p->pos] >= '0' && p->src[p->pos] <= '9'))) {
            p->pos++;
        }
        int name_len = p->pos - start;

        // check for assignment operators (only if no $)
        if (!has_dollar) {
            let_skip_ws(p);
            if (p->pos < p->len) {
                char op = p->src[p->pos];
                char op2 = (p->pos + 1 < p->len) ? p->src[p->pos + 1] : 0;

                if (op == '=' && op2 != '=') {
                    p->pos++;
                    int64_t rhs = let_parse_expr(p);
                    let_set_var_value(p->src + start, name_len, rhs);
                    return rhs;
                }
                if (op2 == '=' && (op == '+' || op == '-' || op == '*' || op == '/' || op == '%' ||
                                   op == '&' || op == '|' || op == '^')) {
                    p->pos += 2;
                    int64_t cur = let_get_var_value(p->src + start, name_len);
                    int64_t rhs = let_parse_expr(p);
                    int64_t result;
                    switch (op) {
                    case '+': result = cur + rhs; break;
                    case '-': result = cur - rhs; break;
                    case '*': result = cur * rhs; break;
                    case '/': result = rhs != 0 ? cur / rhs : 0; break;
                    case '%': result = rhs != 0 ? cur % rhs : 0; break;
                    case '&': result = cur & rhs; break;
                    case '|': result = cur | rhs; break;
                    case '^': result = cur ^ rhs; break;
                    default: result = rhs; break;
                    }
                    let_set_var_value(p->src + start, name_len, result);
                    return result;
                }
                // <<= and >>= 
                if (op == '<' && op2 == '<' && p->pos + 2 < p->len && p->src[p->pos + 2] == '=') {
                    p->pos += 3;
                    int64_t cur = let_get_var_value(p->src + start, name_len);
                    int64_t rhs = let_parse_expr(p);
                    int64_t result = cur << rhs;
                    let_set_var_value(p->src + start, name_len, result);
                    return result;
                }
                if (op == '>' && op2 == '>' && p->pos + 2 < p->len && p->src[p->pos + 2] == '=') {
                    p->pos += 3;
                    int64_t cur = let_get_var_value(p->src + start, name_len);
                    int64_t rhs = let_parse_expr(p);
                    int64_t result = cur >> rhs;
                    let_set_var_value(p->src + start, name_len, result);
                    return result;
                }
                // pre-increment/decrement (++ or --)
                if (op == '+' && op2 == '+') {
                    p->pos += 2;
                    int64_t cur = let_get_var_value(p->src + start, name_len);
                    let_set_var_value(p->src + start, name_len, cur + 1);
                    return cur + 1;
                }
                if (op == '-' && op2 == '-') {
                    p->pos += 2;
                    int64_t cur = let_get_var_value(p->src + start, name_len);
                    let_set_var_value(p->src + start, name_len, cur - 1);
                    return cur - 1;
                }
            }
        }

        return let_get_var_value(p->src + start, name_len);
    }

    return 0;
}

static int64_t let_parse_mul(LetParser* p) {
    int64_t left = let_parse_primary(p);
    while (true) {
        let_skip_ws(p);
        if (p->pos >= p->len) break;
        char op = p->src[p->pos];
        if (op == '*' && (p->pos + 1 >= p->len || p->src[p->pos + 1] != '=')) {
            p->pos++; left *= let_parse_primary(p);
        } else if (op == '/' && (p->pos + 1 >= p->len || p->src[p->pos + 1] != '=')) {
            p->pos++; int64_t r = let_parse_primary(p); left = r ? left / r : 0;
        } else if (op == '%' && (p->pos + 1 >= p->len || p->src[p->pos + 1] != '=')) {
            p->pos++; int64_t r = let_parse_primary(p); left = r ? left % r : 0;
        } else break;
    }
    return left;
}

static int64_t let_parse_add(LetParser* p) {
    int64_t left = let_parse_mul(p);
    while (true) {
        let_skip_ws(p);
        if (p->pos >= p->len) break;
        char op = p->src[p->pos];
        char op2 = (p->pos + 1 < p->len) ? p->src[p->pos + 1] : 0;
        if (op == '+' && op2 != '=' && op2 != '+') {
            p->pos++; left += let_parse_mul(p);
        } else if (op == '-' && op2 != '=' && op2 != '-') {
            p->pos++; left -= let_parse_mul(p);
        } else break;
    }
    return left;
}

static int64_t let_parse_shift(LetParser* p) {
    int64_t left = let_parse_add(p);
    while (true) {
        let_skip_ws(p);
        if (p->pos + 1 >= p->len) break;
        char op = p->src[p->pos];
        char op2 = p->src[p->pos + 1];
        char op3 = (p->pos + 2 < p->len) ? p->src[p->pos + 2] : 0;
        if (op == '<' && op2 == '<' && op3 != '=') {
            p->pos += 2; left <<= let_parse_add(p);
        } else if (op == '>' && op2 == '>' && op3 != '=') {
            p->pos += 2; left >>= let_parse_add(p);
        } else break;
    }
    return left;
}

static int64_t let_parse_relational(LetParser* p) {
    int64_t left = let_parse_shift(p);
    while (true) {
        let_skip_ws(p);
        if (p->pos >= p->len) break;
        char op = p->src[p->pos];
        char op2 = (p->pos + 1 < p->len) ? p->src[p->pos + 1] : 0;
        if (op == '<' && op2 == '=') {
            p->pos += 2; left = left <= let_parse_shift(p);
        } else if (op == '>' && op2 == '=') {
            p->pos += 2; left = left >= let_parse_shift(p);
        } else if (op == '<' && op2 != '<') {
            p->pos++; left = left < let_parse_shift(p);
        } else if (op == '>' && op2 != '>') {
            p->pos++; left = left > let_parse_shift(p);
        } else break;
    }
    return left;
}

static int64_t let_parse_equality(LetParser* p) {
    int64_t left = let_parse_relational(p);
    while (true) {
        let_skip_ws(p);
        if (p->pos + 1 >= p->len) break;
        char op = p->src[p->pos];
        char op2 = p->src[p->pos + 1];
        if (op == '=' && op2 == '=') {
            p->pos += 2; left = left == let_parse_relational(p);
        } else if (op == '!' && op2 == '=') {
            p->pos += 2; left = left != let_parse_relational(p);
        } else break;
    }
    return left;
}

static int64_t let_parse_bitand(LetParser* p) {
    int64_t left = let_parse_equality(p);
    while (true) {
        let_skip_ws(p);
        if (p->pos >= p->len) break;
        char op = p->src[p->pos];
        char op2 = (p->pos + 1 < p->len) ? p->src[p->pos + 1] : 0;
        if (op == '&' && op2 != '&' && op2 != '=') {
            p->pos++; left &= let_parse_equality(p);
        } else break;
    }
    return left;
}

static int64_t let_parse_bitxor(LetParser* p) {
    int64_t left = let_parse_bitand(p);
    while (true) {
        let_skip_ws(p);
        if (p->pos >= p->len) break;
        char op = p->src[p->pos];
        char op2 = (p->pos + 1 < p->len) ? p->src[p->pos + 1] : 0;
        if (op == '^' && op2 != '=') {
            p->pos++; left ^= let_parse_bitand(p);
        } else break;
    }
    return left;
}

static int64_t let_parse_bitor(LetParser* p) {
    int64_t left = let_parse_bitxor(p);
    while (true) {
        let_skip_ws(p);
        if (p->pos >= p->len) break;
        char op = p->src[p->pos];
        char op2 = (p->pos + 1 < p->len) ? p->src[p->pos + 1] : 0;
        if (op == '|' && op2 != '|' && op2 != '=') {
            p->pos++; left |= let_parse_bitxor(p);
        } else break;
    }
    return left;
}

static int64_t let_parse_logand(LetParser* p) {
    int64_t left = let_parse_bitor(p);
    while (true) {
        let_skip_ws(p);
        if (p->pos + 1 >= p->len) break;
        if (p->src[p->pos] == '&' && p->src[p->pos + 1] == '&') {
            p->pos += 2;
            int64_t right = let_parse_bitor(p);
            left = (left && right) ? 1 : 0;
        } else break;
    }
    return left;
}

static int64_t let_parse_logor(LetParser* p) {
    int64_t left = let_parse_logand(p);
    while (true) {
        let_skip_ws(p);
        if (p->pos + 1 >= p->len) break;
        if (p->src[p->pos] == '|' && p->src[p->pos + 1] == '|') {
            p->pos += 2;
            int64_t right = let_parse_logand(p);
            left = (left || right) ? 1 : 0;
        } else break;
    }
    return left;
}

static int64_t let_parse_ternary(LetParser* p) {
    int64_t cond = let_parse_logor(p);
    let_skip_ws(p);
    if (p->pos < p->len && p->src[p->pos] == '?') {
        p->pos++;
        int64_t then_val = let_parse_expr(p);
        let_skip_ws(p);
        if (p->pos < p->len && p->src[p->pos] == ':') p->pos++;
        int64_t else_val = let_parse_expr(p);
        return cond ? then_val : else_val;
    }
    return cond;
}

static int64_t let_parse_expr(LetParser* p) {
    int64_t val = let_parse_ternary(p);
    // handle comma operator
    while (true) {
        let_skip_ws(p);
        if (p->pos < p->len && p->src[p->pos] == ',') {
            p->pos++;
            val = let_parse_ternary(p);
        } else break;
    }
    return val;
}

extern "C" Item bash_builtin_let(Item* args, int argc) {
    int64_t last_val = 0;
    for (int i = 0; i < argc; i++) {
        Item arg_str = bash_to_string(args[i]);
        String* s = it2s(arg_str);
        if (!s || s->len == 0) continue;
        LetParser parser = {s->chars, 0, s->len};
        last_val = let_parse_expr(&parser);
    }
    // let returns 1 (failure) if last expression is 0, 0 (success) otherwise
    int exit_code = (last_val == 0) ? 1 : 0;
    bash_set_exit_code(exit_code);
    return (Item){.item = i2it(exit_code)};
}
