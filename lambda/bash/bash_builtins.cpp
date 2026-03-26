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
