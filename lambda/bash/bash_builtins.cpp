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
#include "bash_errors.h"
#include "../lambda-data.hpp"
#include "../transpiler.hpp"
#include "../../lib/log.h"
#include "../../lib/strbuf.h"
#include <cstring>
#include <cstdio>
#include "../../lib/mem.h"
#include <cctype>
#include <ctime>
#include <unistd.h>
#include <sys/stat.h>
#include <regex.h>

// ============================================================================
// getopts state — saved/restored on function scope push/pop
// ============================================================================
#define GETOPTS_STATE_STACK_MAX 64
static int getopts_charind = 0;          // 0-based offset within current option arg
static int getopts_last_optind = 1;      // track OPTIND changes to detect resets
static int getopts_charind_stack[GETOPTS_STATE_STACK_MAX];
static int getopts_optind_stack[GETOPTS_STATE_STACK_MAX];
static int getopts_state_depth = 0;

extern "C" void bash_getopts_push_state(void) {
    if (getopts_state_depth < GETOPTS_STATE_STACK_MAX) {
        getopts_charind_stack[getopts_state_depth] = getopts_charind;
        getopts_optind_stack[getopts_state_depth] = getopts_last_optind;
        getopts_state_depth++;
        getopts_charind = 0;
        getopts_last_optind = 1;
    }
}

extern "C" void bash_getopts_pop_state(void) {
    if (getopts_state_depth > 0) {
        getopts_state_depth--;
        getopts_charind = getopts_charind_stack[getopts_state_depth];
        getopts_last_optind = getopts_optind_stack[getopts_state_depth];
    }
}

// ============================================================================
// Shared escape sequence processor (used by echo -e, printf %b, $'...')
// ============================================================================

// encode a Unicode codepoint as UTF-8 into buf, return number of bytes written
static int utf8_encode(uint32_t cp, char* buf) {
    if (cp < 0x80) {
        buf[0] = (char)cp;
        return 1;
    } else if (cp < 0x800) {
        buf[0] = (char)(0xC0 | (cp >> 6));
        buf[1] = (char)(0x80 | (cp & 0x3F));
        return 2;
    } else if (cp < 0x10000) {
        buf[0] = (char)(0xE0 | (cp >> 12));
        buf[1] = (char)(0x80 | ((cp >> 6) & 0x3F));
        buf[2] = (char)(0x80 | (cp & 0x3F));
        return 3;
    } else if (cp < 0x110000) {
        buf[0] = (char)(0xF0 | (cp >> 18));
        buf[1] = (char)(0x80 | ((cp >> 12) & 0x3F));
        buf[2] = (char)(0x80 | ((cp >> 6) & 0x3F));
        buf[3] = (char)(0x80 | (cp & 0x3F));
        return 4;
    }
    return 0;
}

// process escape sequences in a string, appending to out
// supports: \a \b \e \f \n \r \t \v \\ \' \" \0NNN \xHH \uHHHH \UHHHHHHHH \c (stop)
// returns false if \c was encountered (stop processing)
static bool process_escape_sequences(const char* src, int len, StrBuf* out) {
    for (int i = 0; i < len; i++) {
        if (src[i] != '\\' || i + 1 >= len) {
            strbuf_append_char(out, src[i]);
            continue;
        }
        i++;
        switch (src[i]) {
        case 'a': strbuf_append_char(out, '\a'); break;
        case 'b': strbuf_append_char(out, '\b'); break;
        case 'e': case 'E': strbuf_append_char(out, '\x1B'); break;
        case 'f': strbuf_append_char(out, '\f'); break;
        case 'n': strbuf_append_char(out, '\n'); break;
        case 'r': strbuf_append_char(out, '\r'); break;
        case 't': strbuf_append_char(out, '\t'); break;
        case 'v': strbuf_append_char(out, '\v'); break;
        case '\\': strbuf_append_char(out, '\\'); break;
        case '\'': strbuf_append_char(out, '\''); break;
        case '"': strbuf_append_char(out, '"'); break;
        case 'c': return false; // stop processing
        case '0': {
            // octal: \0NNN (up to 3 octal digits after the 0)
            int val = 0;
            for (int j = 0; j < 3 && i + 1 < len && src[i + 1] >= '0' && src[i + 1] <= '7'; j++) {
                i++;
                val = val * 8 + (src[i] - '0');
            }
            strbuf_append_char(out, (char)(val & 0xFF));
            break;
        }
        case 'x': {
            // hex: \xHH (1-2 hex digits)
            int val = 0;
            int count = 0;
            while (count < 2 && i + 1 < len && isxdigit((unsigned char)src[i + 1])) {
                i++;
                int d = src[i];
                if (d >= '0' && d <= '9') val = val * 16 + (d - '0');
                else if (d >= 'a' && d <= 'f') val = val * 16 + (d - 'a' + 10);
                else if (d >= 'A' && d <= 'F') val = val * 16 + (d - 'A' + 10);
                count++;
            }
            if (count > 0) {
                strbuf_append_char(out, (char)(val & 0xFF));
            } else {
                strbuf_append_str(out, "\\x");
            }
            break;
        }
        case 'u': {
            // unicode: \uHHHH (exactly 4 hex digits)
            uint32_t val = 0;
            int count = 0;
            while (count < 4 && i + 1 < len && isxdigit((unsigned char)src[i + 1])) {
                i++;
                int d = src[i];
                if (d >= '0' && d <= '9') val = val * 16 + (d - '0');
                else if (d >= 'a' && d <= 'f') val = val * 16 + (d - 'a' + 10);
                else if (d >= 'A' && d <= 'F') val = val * 16 + (d - 'A' + 10);
                count++;
            }
            if (count > 0) {
                char utf8[4];
                int n = utf8_encode(val, utf8);
                strbuf_append_str_n(out, utf8, n);
            } else {
                strbuf_append_str(out, "\\u");
            }
            break;
        }
        case 'U': {
            // unicode: \UHHHHHHHH (up to 8 hex digits)
            uint32_t val = 0;
            int count = 0;
            while (count < 8 && i + 1 < len && isxdigit((unsigned char)src[i + 1])) {
                i++;
                int d = src[i];
                if (d >= '0' && d <= '9') val = val * 16 + (d - '0');
                else if (d >= 'a' && d <= 'f') val = val * 16 + (d - 'a' + 10);
                else if (d >= 'A' && d <= 'F') val = val * 16 + (d - 'A' + 10);
                count++;
            }
            if (count > 0) {
                char utf8[4];
                int n = utf8_encode(val, utf8);
                strbuf_append_str_n(out, utf8, n);
            } else {
                strbuf_append_str(out, "\\U");
            }
            break;
        }
        default:
            // unknown escape: output backslash + char literally
            strbuf_append_char(out, '\\');
            strbuf_append_char(out, src[i]);
            break;
        }
    }
    return true;
}

// public API: process escape sequences in a Lambda string, return new string
extern "C" Item bash_process_escapes(Item input) {
    String* s = it2s(bash_to_string(input));
    if (!s || s->len == 0) return input;
    StrBuf* out = strbuf_new();
    process_escape_sequences(s->chars, s->len, out);
    Item result = (Item){.item = s2it(heap_create_name(out->str, (int)out->length))};
    strbuf_free(out);
    return result;
}

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
            // process escape sequences using shared processor
            StrBuf* esc_buf = strbuf_new();
            bool cont = process_escape_sequences(s->chars, s->len, esc_buf);
            bash_raw_write(esc_buf->str, (int)esc_buf->length);
            strbuf_free(esc_buf);
            if (!cont) {
                // \c encountered — stop all output (no trailing newline either)
                fflush(stdout);
                bash_set_exit_code(0);
                return (Item){.item = i2it(0)};
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

            // check for %(fmt)T — strftime date/time format
            if (fmt->chars[i] == '(') {
                i++;
                // collect strftime format until )T
                char timefmt[256];
                int tfi = 0;
                while (i < (int)fmt->len && tfi < (int)sizeof(timefmt) - 1) {
                    if (fmt->chars[i] == ')' && i + 1 < (int)fmt->len && fmt->chars[i + 1] == 'T') {
                        break;
                    }
                    timefmt[tfi++] = fmt->chars[i++];
                }
                timefmt[tfi] = '\0';
                if (i < (int)fmt->len && fmt->chars[i] == ')') i++; // skip ')'
                if (i < (int)fmt->len && fmt->chars[i] == 'T') ; // will be skipped by 'i' at end of loop
                else i--; // malformed: backtrack

                // get time argument: -1 = current time, -2 = shell start time (treat as current)
                time_t t;
                if (arg_idx < argc) {
                    int64_t val = printf_get_int(args[arg_idx++]);
                    if (val == -1 || val == -2) {
                        t = time(NULL);
                    } else {
                        t = (time_t)val;
                    }
                } else {
                    t = time(NULL); // no arg = current time (bash default for %(fmt)T)
                }
                struct tm* tm = localtime(&t);
                char timebuf[512];
                if (tm) {
                    size_t n = strftime(timebuf, sizeof(timebuf), timefmt, tm);
                    strbuf_append_str_n(out, timebuf, (int)n);
                }
                continue; // %(fmt)T handled, skip the switch below
            }

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
                // %b: like %s but interpret backslash escapes (using shared processor)
                const char* str = "";
                int str_len = 0;
                if (arg_idx < argc) {
                    Item s = bash_to_string(args[arg_idx++]);
                    String* ss = it2s(s);
                    if (ss) { str = ss->chars; str_len = ss->len; }
                }
                StrBuf* esc_buf = strbuf_new();
                bool cont = process_escape_sequences(str, str_len, esc_buf);
                strbuf_append_str_n(out, esc_buf->str, (int)esc_buf->length);
                strbuf_free(esc_buf);
                if (!cont) {
                    // \c in %b stops all output
                    return arg_idx;
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
            case 'e': case 'E': strbuf_append_char(out, '\x1B'); i++; break;
            case 'f': strbuf_append_char(out, '\f'); i++; break;
            case 'v': strbuf_append_char(out, '\v'); i++; break;
            case '\\': strbuf_append_char(out, '\\'); i++; break;
            case 'c': return arg_idx; // \c in format string: stop all output
            case '0': {
                // octal escape \0NNN
                int val = 0;
                i++; // skip '0'
                for (int j = 0; j < 3 && i + 1 < (int)fmt->len && fmt->chars[i + 1] >= '0' && fmt->chars[i + 1] <= '7'; j++) {
                    i++;
                    val = val * 8 + (fmt->chars[i] - '0');
                }
                strbuf_append_char(out, (char)(val & 0xFF));
                break;
            }
            case 'x': {
                // hex escape \xHH
                i++; // skip 'x'
                int val = 0;
                int count = 0;
                while (count < 2 && i + 1 < (int)fmt->len && isxdigit((unsigned char)fmt->chars[i + 1])) {
                    i++;
                    int d = fmt->chars[i];
                    if (d >= '0' && d <= '9') val = val * 16 + (d - '0');
                    else if (d >= 'a' && d <= 'f') val = val * 16 + (d - 'a' + 10);
                    else if (d >= 'A' && d <= 'F') val = val * 16 + (d - 'A' + 10);
                    count++;
                }
                if (count > 0) strbuf_append_char(out, (char)(val & 0xFF));
                else { strbuf_append_str(out, "\\x"); }
                break;
            }
            case 'u': {
                // unicode escape \uHHHH
                i++; // skip 'u'
                uint32_t val = 0;
                int count = 0;
                while (count < 4 && i + 1 < (int)fmt->len && isxdigit((unsigned char)fmt->chars[i + 1])) {
                    i++;
                    int d = fmt->chars[i];
                    if (d >= '0' && d <= '9') val = val * 16 + (d - '0');
                    else if (d >= 'a' && d <= 'f') val = val * 16 + (d - 'a' + 10);
                    else if (d >= 'A' && d <= 'F') val = val * 16 + (d - 'A' + 10);
                    count++;
                }
                if (count > 0) {
                    char utf8[4];
                    int n = utf8_encode(val, utf8);
                    strbuf_append_str_n(out, utf8, n);
                } else { strbuf_append_str(out, "\\u"); }
                break;
            }
            case 'U': {
                // unicode escape \UHHHHHHHH
                i++; // skip 'U'
                uint32_t val = 0;
                int count = 0;
                while (count < 8 && i + 1 < (int)fmt->len && isxdigit((unsigned char)fmt->chars[i + 1])) {
                    i++;
                    int d = fmt->chars[i];
                    if (d >= '0' && d <= '9') val = val * 16 + (d - '0');
                    else if (d >= 'a' && d <= 'f') val = val * 16 + (d - 'a' + 10);
                    else if (d >= 'A' && d <= 'F') val = val * 16 + (d - 'A' + 10);
                    count++;
                }
                if (count > 0) {
                    char utf8[4];
                    int n = utf8_encode(val, utf8);
                    strbuf_append_str_n(out, utf8, n);
                } else { strbuf_append_str(out, "\\U"); }
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
        // unary operators: -z, -n, -f, -d, -e, -r, -w, -x, -s, -L, -p, -t
        String* op = it2s(args[0]);
        if (op && op->len == 2 && op->chars[0] == '-') {
            char c = op->chars[1];
            Item r;
            bool handled = true;
            switch (c) {
                case 'z': r = bash_test_z(args[1]); break;
                case 'n': r = bash_test_n(args[1]); break;
                case 'f': r = bash_test_f(args[1]); break;
                case 'd': r = bash_test_d(args[1]); break;
                case 'e': r = bash_test_e(args[1]); break;
                case 'r': r = bash_test_r(args[1]); break;
                case 'w': r = bash_test_w(args[1]); break;
                case 'x': r = bash_test_x(args[1]); break;
                case 's': r = bash_test_s(args[1]); break;
                case 'L': case 'h': r = bash_test_l(args[1]); break;
                default: handled = false; break;
            }
            if (handled) {
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
    // read [-r] [-p prompt] [-d delim] [-n nchars] [-t timeout] [-a array] var1 [var2 ...]
    // Reads a line from stdin_item (herestring/pipe) or real stdin, then
    // splits by IFS and assigns to named variables.
    bool raw_mode = false;  // -r: no backslash escaping
    int var_start = 0;      // index of first variable name in args
    Item array_name = {.item = ITEM_NULL};  // -a: array variable name
    char delim_char = '\n';  // -d: delimiter (default newline)
    bool has_delim = false;

    // parse flags
    for (int i = 0; i < argc; i++) {
        String* arg = it2s(bash_to_string(args[i]));
        if (!arg || arg->len == 0 || arg->chars[0] != '-') break;
        if (arg->len == 2 && arg->chars[1] == 'r') {
            raw_mode = true;
            var_start = i + 1;
        } else if (arg->len == 2 && arg->chars[1] == 'a' && i + 1 < argc) {
            // -a array_name
            array_name = args[i + 1];
            var_start = i + 2;
            i++; // skip array name
        } else if (arg->len == 2 && arg->chars[1] == 'd' && i + 1 < argc) {
            // -d delim
            String* d = it2s(bash_to_string(args[i + 1]));
            if (d && d->len > 0) {
                delim_char = d->chars[0];
            } else {
                delim_char = '\0'; // NUL delimiter
            }
            has_delim = true;
            var_start = i + 2;
            i++;
        } else if (arg->len == 2 && (arg->chars[1] == 'p' ||
                                      arg->chars[1] == 'n' || arg->chars[1] == 't' ||
                                      arg->chars[1] == 'u')) {
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
    bool using_stdin_item = (stdin_str && stdin_str->len > 0);
    if (using_stdin_item) {
        // read from stdin_item up to delimiter
        line = stdin_str->chars;
        line_len = stdin_str->len;
        int consumed = line_len; // default: consume entire string
        if (delim_char == '\0') {
            // NUL delimiter: read entire input (NUL won't appear in string)
            // do NOT strip trailing newline - bash preserves it with -d
        } else {
            // find delimiter character
            for (int i = 0; i < line_len; i++) {
                if (line[i] == delim_char) {
                    line_len = i;
                    consumed = i + 1; // consume through the delimiter
                    break;
                }
            }
        }
        // advance stdin_item past consumed portion
        if (consumed >= stdin_str->len) {
            // set to empty string but keep the "set" flag so EOF is detected
            Item empty_item = (Item){.item = s2it(heap_create_name("", 0))};
            bash_set_stdin_item(empty_item);
        } else {
            const char* rest = stdin_str->chars + consumed;
            int rest_len = stdin_str->len - consumed;
            Item rest_item = (Item){.item = s2it(heap_create_name(rest, rest_len))};
            bash_set_stdin_item(rest_item);
        }
    } else if (bash_stdin_item_is_set()) {
        // stdin_item was set but is now empty — EOF on pipe
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

    // handle backslash escaping in non-raw mode (-r disables this)
    // in non-raw mode: backslash before any character removes the backslash
    // (backslash at end of line is line continuation, but we read a single line)
    char unesc_buf[4096];
    if (!raw_mode && line && line_len > 0) {
        int out = 0;
        for (int i = 0; i < line_len && out < (int)sizeof(unesc_buf) - 1; i++) {
            if (line[i] == '\\' && i + 1 < line_len) {
                i++; // skip backslash, take next char
                unesc_buf[out++] = line[i];
            } else if (line[i] == '\\' && i + 1 >= line_len) {
                // trailing backslash: line continuation (discard it)
            } else {
                unesc_buf[out++] = line[i];
            }
        }
        line = unesc_buf;
        line_len = out;
    }

    // if no variable names specified and no -a flag, assign whole line to REPLY
    if (num_vars <= 0 && array_name.item == ITEM_NULL) {
        // REPLY preserves the full line without any whitespace stripping
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
    if (ifs_str && ifs_str->len > 0) {
        ifs = ifs_str->chars;
    }

    // build IFS character maps
    bool ifs_map[256] = {};
    bool ifs_ws_map[256] = {};
    for (int i = 0; ifs[i]; i++) {
        unsigned char c = (unsigned char)ifs[i];
        ifs_map[c] = true;
        if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
            ifs_ws_map[c] = true;
        }
    }

    // strip trailing IFS whitespace from line for field splitting
    // (the last variable gets the remainder, which should not have trailing IFS ws)
    int trimmed_line_len = line_len;
    while (trimmed_line_len > 0 && ifs_ws_map[(unsigned char)line[trimmed_line_len - 1]]) {
        trimmed_line_len--;
    }
    line_len = trimmed_line_len;

    // POSIX IFS splitting for read:
    // - Perform full POSIX split tracking field start/end positions in original line.
    // - If total fields <= num_vars: assign each field to corresponding var, empty for rest.
    // - If total fields > num_vars: assign fields 1..n-1 to vars 1..n-1;
    //   last var gets raw line from start of field n to end of (trimmed) line.
    //
    // We track up to num_vars+1 fields (if there's a n+1-th field, we know to use raw remainder).
    const int MAX_FIELDS = 64;
    struct FieldPos { int start; int end; int raw_start; }; // raw_start: pos in line where this field starts (for last-var remainder)
    FieldPos fields[MAX_FIELDS];
    int num_fields = 0;

    int pos = 0;

    // skip leading IFS whitespace
    while (pos < line_len && ifs_ws_map[(unsigned char)line[pos]]) pos++;

    auto consume_separator = [&]() {
        if (pos >= line_len || !ifs_map[(unsigned char)line[pos]]) return;
        if (ifs_ws_map[(unsigned char)line[pos]]) {
            // IFS whitespace: skip all, then if non-ws IFS follows consume it too
            while (pos < line_len && ifs_ws_map[(unsigned char)line[pos]]) pos++;
            if (pos < line_len && ifs_map[(unsigned char)line[pos]] && !ifs_ws_map[(unsigned char)line[pos]]) {
                pos++;
                while (pos < line_len && ifs_ws_map[(unsigned char)line[pos]]) pos++;
            }
        } else {
            // non-ws IFS: consume it and any following IFS-ws
            pos++;
            while (pos < line_len && ifs_ws_map[(unsigned char)line[pos]]) pos++;
        }
    };

    while (pos < line_len && num_fields < MAX_FIELDS) {
        int raw_start = pos;
        // check for leading non-ws IFS (empty field before it)
        if (ifs_map[(unsigned char)line[pos]] && !ifs_ws_map[(unsigned char)line[pos]]) {
            fields[num_fields++] = {pos, pos, raw_start};
            pos++; // consume non-ws IFS
            while (pos < line_len && ifs_ws_map[(unsigned char)line[pos]]) pos++;
            continue;
        }
        // accumulate non-IFS chars
        int word_start = pos;
        while (pos < line_len && !ifs_map[(unsigned char)line[pos]]) pos++;
        int word_end = pos;
        fields[num_fields++] = {word_start, word_end, raw_start};
        consume_separator();
    }

    // assign variables
    if (array_name.item != ITEM_NULL) {
        // -a mode: assign all fields to an indexed array
        // create array with all fields
        Item arr = bash_array_new();
        for (int fi = 0; fi < num_fields; fi++) {
            String* val = heap_create_name(line + fields[fi].start, fields[fi].end - fields[fi].start);
            bash_array_append(arr, (Item){.item = s2it(val)});
        }
        bash_set_var(array_name, arr);
    } else if (num_fields <= num_vars) {
        // fewer or equal fields: assign each field value, empty for vars beyond fields
        for (int vi = 0; vi < num_vars; vi++) {
            int arg_idx = var_start + vi;
            if (arg_idx >= argc) break;
            String* val;
            if (vi < num_fields) {
                val = heap_create_name(line + fields[vi].start, fields[vi].end - fields[vi].start);
            } else {
                val = heap_create_name("", 0);
            }
            bash_set_var(args[arg_idx], (Item){.item = s2it(val)});
        }
    } else {
        // more fields than vars: assign fields 1..n-1, raw remainder to var n
        for (int vi = 0; vi < num_vars - 1; vi++) {
            int arg_idx = var_start + vi;
            if (arg_idx >= argc) break;
            String* val = heap_create_name(line + fields[vi].start, fields[vi].end - fields[vi].start);
            bash_set_var(args[arg_idx], (Item){.item = s2it(val)});
        }
        // last var: raw line from raw_start of field num_vars to end
        int last_arg_idx = var_start + num_vars - 1;
        if (last_arg_idx < argc) {
            int raw_from = fields[num_vars - 1].raw_start;
            int rlen = (raw_from < line_len) ? (line_len - raw_from) : 0;
            String* val = heap_create_name(line + raw_from, rlen);
            bash_set_var(args[last_arg_idx], (Item){.item = s2it(val)});
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
    // parse flags
    bool flag_v = false;  // -v: display non-printing characters
    int file_start = 0;
    for (int i = 0; i < argc; i++) {
        String* arg = it2s(bash_to_string(args[i]));
        if (!arg || arg->len == 0 || arg->chars[0] != '-' || arg->len == 1) break;
        // check if it's a flag (starts with - and has letters)
        bool is_flag = true;
        for (int j = 1; j < arg->len; j++) {
            char c = arg->chars[j];
            if (c == 'v') flag_v = true;
            else if (c == 'e') flag_v = true; // -e implies -v
            else if (c == 't') flag_v = true; // -t implies -v
            else if (c == 'A') flag_v = true; // -A implies -v
            else { is_flag = false; break; }
        }
        if (!is_flag) break;
        file_start = i + 1;
    }

    // helper lambda-style: output with -v transform
    auto cat_output = [&](const char* data, int len) {
        if (!flag_v) {
            bash_raw_write(data, len);
            return;
        }
        // -v mode: replace non-printing chars with ^X notation
        for (int i = 0; i < len; i++) {
            unsigned char c = (unsigned char)data[i];
            if (c == '\n' || c == '\t') {
                // pass through tabs and newlines
                bash_raw_write((const char*)&c, 1);
            } else if (c < 0x20) {
                // control chars 0x00-0x1F: ^@ through ^_
                char buf[2] = {'^', (char)('@' + c)};
                bash_raw_write(buf, 2);
            } else if (c == 0x7F) {
                bash_raw_write("^?", 2);
            } else if (c >= 0x80) {
                // high bytes: M-^X or M-X
                bash_raw_write("M-", 2);
                unsigned char lo = c & 0x7F;
                if (lo < 0x20) {
                    char buf[2] = {'^', (char)('@' + lo)};
                    bash_raw_write(buf, 2);
                } else if (lo == 0x7F) {
                    bash_raw_write("^?", 2);
                } else {
                    bash_raw_write((const char*)&lo, 1);
                }
            } else {
                bash_raw_write((const char*)&c, 1);
            }
        }
    };

    // if file arguments provided, read and output each file
    bool had_files = false;
    for (int i = file_start; i < argc; i++) {
        String* arg = it2s(bash_to_string(args[i]));
        if (!arg || arg->len == 0) continue;
        if (arg->chars[0] == '-' && arg->len == 1) {
            // cat - : read stdin
            Item input = bash_get_stdin_item();
            String* s = it2s(bash_to_string(input));
            if (s && s->len > 0) cat_output(s->chars, s->len);
            had_files = true;
            continue;
        }
        // read file
        FILE* f = fopen(arg->chars, "r");
        if (!f) {
            fprintf(stderr, "cat: %s: No such file or directory\n", arg->chars);
            bash_set_exit_code(1);
            return (Item){.item = i2it(1)};
        }
        char buf[4096];
        size_t n;
        while ((n = fread(buf, 1, sizeof(buf), f)) > 0) {
            cat_output(buf, (int)n);
        }
        fclose(f);
        had_files = true;
    }
    // no file args: read stdin
    if (!had_files) {
        Item input = bash_get_stdin_item();
        String* s = it2s(bash_to_string(input));
        if (s && s->len > 0) {
            cat_output(s->chars, s->len);
        }
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
                char* line_buf = (char*)mem_alloc(line_len + 1, MEM_CAT_BASH_RUNTIME);
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
                mem_free(line_buf);
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
    const char** lines = (const char**)mem_alloc(max_lines * sizeof(const char*), MEM_CAT_BASH_RUNTIME);
    int* lens = (int*)mem_alloc(max_lines * sizeof(int), MEM_CAT_BASH_RUNTIME);
    int line_count = 0;
    int line_start = 0;

    for (int i = 0; i <= (int)s->len; i++) {
        if (i == (int)s->len || s->chars[i] == '\n') {
            if (line_count >= max_lines) {
                max_lines *= 2;
                lines = (const char**)mem_realloc(lines, max_lines * sizeof(const char*), MEM_CAT_BASH_RUNTIME);
                lens = (int*)mem_realloc(lens, max_lines * sizeof(int), MEM_CAT_BASH_RUNTIME);
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

    mem_free(lines);
    mem_free(lens);
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

// ============================================================================
// type builtin
// ============================================================================

static bool bash_name_is_builtin(const char* name, int len) {
    static const char* builtins[] = {
        "echo", "printf", "test", "[", "true", "false", "exit", "return",
        "read", "shift", "local", "export", "unset", "cd", "pwd", "set",
        "shopt", "let", "declare", "typeset", "readonly", "source", ".",
        "trap", "eval", "exec", "builtin", "command", "type", "hash",
        "enable", "mapfile", "readarray", "caller", "getopts", "wait",
        "kill", "jobs", "fg", "bg", "disown", "suspend", "logout",
        "pushd", "popd", "dirs", "alias", "unalias", "bind", "help",
        "times", "ulimit", "umask", "complete", "compgen", "compopt",
        NULL
    };
    for (int i = 0; builtins[i]; i++) {
        if ((int)strlen(builtins[i]) == len && memcmp(builtins[i], name, len) == 0)
            return true;
    }
    return false;
}

static bool bash_name_is_keyword(const char* name, int len) {
    static const char* keywords[] = {
        "if", "then", "else", "elif", "fi", "case", "esac", "for", "while",
        "until", "do", "done", "in", "function", "select", "time", "coproc",
        "{", "}", "!", "[[", "]]",
        NULL
    };
    for (int i = 0; keywords[i]; i++) {
        if ((int)strlen(keywords[i]) == len && memcmp(keywords[i], name, len) == 0)
            return true;
    }
    return false;
}

static bool bash_find_in_path(const char* name, int len, char* out_path, int out_size) {
    // if name contains '/', check if file exists directly
    for (int i = 0; i < len; i++) {
        if (name[i] == '/') {
            if (len < out_size) {
                memcpy(out_path, name, len);
                out_path[len] = '\0';
                return access(out_path, X_OK) == 0;
            }
            return false;
        }
    }
    const char* path_env = getenv("PATH");
    if (!path_env) return false;
    const char* p = path_env;
    while (*p) {
        const char* colon = p;
        while (*colon && *colon != ':') colon++;
        int dir_len = (int)(colon - p);
        if (dir_len + 1 + len + 1 < out_size) {
            memcpy(out_path, p, dir_len);
            out_path[dir_len] = '/';
            memcpy(out_path + dir_len + 1, name, len);
            out_path[dir_len + 1 + len] = '\0';
            if (access(out_path, X_OK) == 0) return true;
        }
        p = *colon ? colon + 1 : colon;
    }
    return false;
}

extern "C" Item bash_builtin_type(Item* args, int argc) {
    bool flag_t = false;   // -t: type word only
    bool flag_a = false;   // -a: all matches
    bool flag_p = false;   // -p: path only
    bool flag_P = false;   // -P: force path search
    bool flag_f = false;   // -f: suppress function lookup
    int name_start = 0;
    int ret_code = 0;

    // parse flags
    for (int i = 0; i < argc; i++) {
        String* s = it2s(bash_to_string(args[i]));
        if (!s || s->len == 0 || s->chars[0] != '-') { name_start = i; break; }
        // check for recognized flags
        bool valid = true;
        for (int j = 1; j < (int)s->len; j++) {
            switch (s->chars[j]) {
                case 't': flag_t = true; break;
                case 'a': flag_a = true; break;
                case 'p': flag_p = true; break;
                case 'P': flag_P = true; break;
                case 'f': flag_f = true; break;
                default:
                    // invalid flag
                    valid = false;
                    break;
            }
            if (!valid) break;
        }
        if (!valid) {
            String* src = it2s(bash_get_bash_source((Item){.item = i2it(0)}));
            fflush(stdout);
            fprintf(stderr, "%.*s: line %d: type: -%c: invalid option\n",
                    src ? src->len : 0, src ? src->chars : "",
                    (int)it2i(bash_get_lineno()), s->chars[1]);
            fprintf(stderr, "type: usage: type [-afptP] name [name ...]\n");
            bash_set_exit_code(2);
            return (Item){.item = i2it(2)};
        }
        name_start = i + 1;
    }

    if (name_start >= argc) {
        // no names given: silently succeed (bash behavior)
        bash_set_exit_code(0);
        return (Item){.item = i2it(0)};
    }

    for (int i = name_start; i < argc; i++) {
        String* s = it2s(bash_to_string(args[i]));
        if (!s) continue;
        const char* name = s->chars;
        int len = (int)s->len;
        char buf[1024];
        int n;
        bool found = false;

        // 1. function
        if (!flag_f && !flag_P) {
            BashRtFuncPtr func = bash_lookup_rt_func(name);
            if (func) {
                found = true;
                if (flag_t) {
                    bash_raw_write("function\n", 9);
                } else if (!flag_p) {
                    n = snprintf(buf, sizeof(buf), "%.*s is a function\n", len, name);
                    bash_raw_write(buf, n);
                    int src_len = 0;
                    const char* src = bash_get_func_source(name, &src_len);
                    if (src && src_len > 0) {
                        bash_raw_write(src, src_len);
                        bash_raw_write("\n", 1);
                    }
                }
                if (!flag_a) continue;
            }
        }

        // 2. builtin
        if (!flag_P) {
            if (bash_name_is_builtin(name, len)) {
                found = true;
                if (flag_t) {
                    if (!found || flag_a) bash_raw_write("builtin\n", 8);
                    else bash_raw_write("builtin\n", 8);
                } else if (!flag_p) {
                    n = snprintf(buf, sizeof(buf), "%.*s is a shell builtin\n", len, name);
                    bash_raw_write(buf, n);
                }
                if (!flag_a) continue;
            }
        }

        // 3. keyword
        if (!flag_P) {
            if (bash_name_is_keyword(name, len)) {
                found = true;
                if (flag_t) {
                    bash_raw_write("keyword\n", 8);
                } else if (!flag_p) {
                    n = snprintf(buf, sizeof(buf), "%.*s is a shell keyword\n", len, name);
                    bash_raw_write(buf, n);
                }
                if (!flag_a) continue;
            }
        }

        // 4. file in PATH
        {
            char path_buf[1024];
            if (bash_find_in_path(name, len, path_buf, sizeof(path_buf))) {
                found = true;
                if (flag_t) {
                    bash_raw_write("file\n", 5);
                } else if (flag_p || flag_P) {
                    n = snprintf(buf, sizeof(buf), "%s\n", path_buf);
                    bash_raw_write(buf, n);
                } else {
                    n = snprintf(buf, sizeof(buf), "%.*s is %s\n", len, name, path_buf);
                    bash_raw_write(buf, n);
                }
                if (!flag_a) continue;
            }
        }

        if (!found) {
            String* src2 = it2s(bash_get_bash_source((Item){.item = i2it(0)}));
            fflush(stdout);
            fprintf(stderr, "%.*s: line %d: type: %.*s: not found\n",
                    src2 ? src2->len : 0, src2 ? src2->chars : "",
                    (int)it2i(bash_get_lineno()), len, name);
            ret_code = 1;
        }
    }

    bash_set_exit_code(ret_code);
    return (Item){.item = i2it(ret_code)};
}

// ============================================================================
// command builtin (-v / -V)
// ============================================================================

extern "C" Item bash_builtin_command(Item* args, int argc) {
    bool flag_v = false;
    bool flag_V = false;
    bool flag_p = false;
    int name_start = 0;

    for (int i = 0; i < argc; i++) {
        String* s = it2s(bash_to_string(args[i]));
        if (!s || s->len == 0 || s->chars[0] != '-') { name_start = i; break; }
        for (int j = 1; j < (int)s->len; j++) {
            switch (s->chars[j]) {
                case 'v': flag_v = true; break;
                case 'V': flag_V = true; break;
                case 'p': flag_p = true; break;
                default: break;
            }
        }
        name_start = i + 1;
    }

    (void)flag_p;

    if (!flag_v && !flag_V) {
        // command without -v/-V: skip functions, run builtin/external
        // for now, just pass through to external
        bash_set_exit_code(127);
        return (Item){.item = i2it(127)};
    }

    int ret_code = 0;
    for (int i = name_start; i < argc; i++) {
        String* s = it2s(bash_to_string(args[i]));
        if (!s) continue;
        const char* name = s->chars;
        int len = (int)s->len;
        char buf[1024];
        int n;
        bool found = false;

        // function
        BashRtFuncPtr func = bash_lookup_rt_func(name);
        if (func) {
            found = true;
            if (flag_v) {
                n = snprintf(buf, sizeof(buf), "%.*s\n", len, name);
                bash_raw_write(buf, n);
            } else {
                n = snprintf(buf, sizeof(buf), "%.*s is a function\n", len, name);
                bash_raw_write(buf, n);
            }
            continue;
        }

        // keyword
        if (bash_name_is_keyword(name, len)) {
            found = true;
            if (flag_v) {
                n = snprintf(buf, sizeof(buf), "%.*s\n", len, name);
                bash_raw_write(buf, n);
            } else {
                n = snprintf(buf, sizeof(buf), "%.*s is a shell keyword\n", len, name);
                bash_raw_write(buf, n);
            }
            continue;
        }

        // builtin
        if (bash_name_is_builtin(name, len)) {
            found = true;
            if (flag_v) {
                n = snprintf(buf, sizeof(buf), "%.*s\n", len, name);
                bash_raw_write(buf, n);
            } else {
                n = snprintf(buf, sizeof(buf), "%.*s is a shell builtin\n", len, name);
                bash_raw_write(buf, n);
            }
            continue;
        }

        // file
        char path_buf[1024];
        if (bash_find_in_path(name, len, path_buf, sizeof(path_buf))) {
            found = true;
            if (flag_v) {
                n = snprintf(buf, sizeof(buf), "%s\n", path_buf);
                bash_raw_write(buf, n);
            } else {
                n = snprintf(buf, sizeof(buf), "%.*s is %s\n", len, name, path_buf);
                bash_raw_write(buf, n);
            }
            continue;
        }

        if (!found) {
            String* src3 = it2s(bash_get_bash_source((Item){.item = i2it(0)}));
            fflush(stdout);
            fprintf(stderr, "%.*s: line %d: command: %.*s: not found\n",
                    src3 ? src3->len : 0, src3 ? src3->chars : "",
                    (int)it2i(bash_get_lineno()), len, name);
            ret_code = 1;
        }
    }

    bash_set_exit_code(ret_code);
    return (Item){.item = i2it(ret_code)};
}

// ============================================================================
// Directory stack: pushd, popd, dirs
// ============================================================================
// The directory stack stores directories below the current one.
// dirs prints: $PWD  stack[0]  stack[1] ...
// DIRSTACK[0] = $PWD, DIRSTACK[1] = stack[0], etc.

#define DIRSTACK_MAX 256
static char* dir_stack[DIRSTACK_MAX];
static int dir_stack_size = 0;

// helper: get current PWD string
static const char* dirstack_get_pwd(void) {
    Item pwd_name = (Item){.item = s2it(heap_create_name("PWD", 3))};
    Item pwd_val = bash_get_var(pwd_name);
    String* pwd_str = it2s(bash_to_string(pwd_val));
    if (pwd_str && pwd_str->len > 0) return pwd_str->chars;
    static char cwd[4096];
    if (getcwd(cwd, sizeof(cwd))) return cwd;
    return "/";
}

// helper: sync the DIRSTACK bash variable from the internal stack
static void dirstack_sync_var(void) {
    Item ds_name = (Item){.item = s2it(heap_create_name("DIRSTACK", 8))};
    Item arr = bash_array_new();
    // DIRSTACK[0] = PWD (current directory)
    const char* pwd = dirstack_get_pwd();
    arr = bash_array_set(arr, (Item){.item = i2it(0)},
          (Item){.item = s2it(heap_create_name(pwd, strlen(pwd)))});
    // DIRSTACK[1..N] = stack entries
    for (int i = 0; i < dir_stack_size; i++) {
        arr = bash_array_set(arr, (Item){.item = i2it(i + 1)},
              (Item){.item = s2it(heap_create_name(dir_stack[i], strlen(dir_stack[i])))});
    }
    bash_set_var(ds_name, arr);
}

// helper: print the directory stack (space-separated)
static void dirstack_print(void) {
    const char* pwd = dirstack_get_pwd();
    int n = printf("%s", pwd);
    (void)n;
    for (int i = 0; i < dir_stack_size; i++) {
        printf(" %s", dir_stack[i]);
    }
    printf("\n");
}

// helper: print full (long) directory stack
static void dirstack_print_long(void) {
    dirstack_print(); // for now same as non-long (no ~ substitution)
}

// helper: print verbose (-v) directory stack
static void dirstack_print_verbose(void) {
    const char* pwd = dirstack_get_pwd();
    printf(" 0  %s\n", pwd);
    for (int i = 0; i < dir_stack_size; i++) {
        printf(" %d  %s\n", i + 1, dir_stack[i]);
    }
}

// helper: get the Nth entry from stack (0 = PWD, 1 = stack[0], ...)
static const char* dirstack_get(int n) {
    if (n == 0) return dirstack_get_pwd();
    if (n >= 1 && n <= dir_stack_size) return dir_stack[n - 1];
    return NULL;
}

// helper: total number of entries (including PWD)
static int dirstack_total(void) {
    return dir_stack_size + 1;
}

// helper: do chdir and update PWD/OLDPWD
static int dirstack_chdir(const char* path) {
    if (chdir(path) != 0) return -1;
    // update PWD and OLDPWD
    Item pwd_name = (Item){.item = s2it(heap_create_name("PWD", 3))};
    Item cur_pwd = bash_get_var(pwd_name);
    String* cur_str = it2s(bash_to_string(cur_pwd));
    if (cur_str && cur_str->len > 0) {
        Item oldpwd_name = (Item){.item = s2it(heap_create_name("OLDPWD", 6))};
        bash_set_var(oldpwd_name, cur_pwd);
    }
    // use logical path (the argument) for absolute paths
    const char* new_pwd = path;
    if (path[0] != '/') {
        // relative: resolve via old PWD + "/" + path
        char buf[4096];
        const char* old_pwd = (cur_str && cur_str->len > 0) ? cur_str->chars : "/";
        snprintf(buf, sizeof(buf), "%s/%s", old_pwd, path);
        Item pwd_val = (Item){.item = s2it(heap_create_name(buf, strlen(buf)))};
        bash_set_var(pwd_name, pwd_val);
    } else {
        Item pwd_val = (Item){.item = s2it(heap_create_name(new_pwd, strlen(new_pwd)))};
        bash_set_var(pwd_name, pwd_val);
    }
    return 0;
}

// helper: insert at front of stack
static void dirstack_push_front(const char* dir) {
    if (dir_stack_size >= DIRSTACK_MAX) return;
    for (int i = dir_stack_size; i > 0; i--) {
        dir_stack[i] = dir_stack[i - 1];
    }
    dir_stack[0] = mem_strdup(dir, MEM_CAT_BASH_RUNTIME);
    dir_stack_size++;
}

// helper: remove from front of stack
static void dirstack_pop_front(void) {
    if (dir_stack_size == 0) return;
    mem_free(dir_stack[0]);
    for (int i = 0; i < dir_stack_size - 1; i++) {
        dir_stack[i] = dir_stack[i + 1];
    }
    dir_stack_size--;
}

// helper: remove at index (0-based in the stack, not including PWD offset)
static void dirstack_remove_at(int idx) {
    if (idx < 0 || idx >= dir_stack_size) return;
    mem_free(dir_stack[idx]);
    for (int i = idx; i < dir_stack_size - 1; i++) {
        dir_stack[i] = dir_stack[i + 1];
    }
    dir_stack_size--;
}

// helper: get the script source prefix for error messages
static void dirstack_err_prefix(char* buf, int bufsize) {
    String* src = it2s(bash_get_bash_source((Item){.item = i2it(0)}));
    int line = (int)it2i(bash_get_lineno());
    snprintf(buf, bufsize, "%.*s: line %d",
             src ? src->len : 0, src ? src->chars : "", line);
}

// pushd [-n] [+N | -N | dir]
extern "C" Item bash_builtin_pushd(Item* args, int argc) {
    bool no_cd = false; // -n flag
    int argi = 0;
    char err[256];

    // parse flags
    while (argi < argc) {
        String* s = it2s(bash_to_string(args[argi]));
        if (!s || s->len == 0 || s->chars[0] != '-') break;
        if (s->len == 2 && s->chars[1] == 'n') {
            no_cd = true;
            argi++;
        } else if (s->chars[1] == '-' && s->len == 2) {
            argi++; break; // --
        } else {
            // could be -N (negative offset)
            if (isdigit((unsigned char)s->chars[1])) break;
            dirstack_err_prefix(err, sizeof(err));
            fflush(stdout);
            fprintf(stderr, "%s: pushd: %.*s: invalid number\n", err, s->len, s->chars);
            fprintf(stderr, "pushd: usage: pushd [-n] [+N | -N | dir]\n");
            bash_set_exit_code(1);
            return (Item){.item = i2it(1)};
        }
    }

    if (argi >= argc) {
        // pushd with no args: swap top two
        if (dir_stack_size == 0) {
            dirstack_err_prefix(err, sizeof(err));
            fflush(stdout);
            fprintf(stderr, "%s: pushd: no other directory\n", err);
            bash_set_exit_code(1);
            return (Item){.item = i2it(1)};
        }
        // swap PWD and stack[0]
        const char* old_pwd = dirstack_get_pwd();
        char* saved = mem_strdup(old_pwd, MEM_CAT_BASH_RUNTIME);
        const char* target = dir_stack[0];
        if (!no_cd) {
            if (dirstack_chdir(target) != 0) {
                dirstack_err_prefix(err, sizeof(err));
                fflush(stdout);
                fprintf(stderr, "%s: pushd: %s: No such file or directory\n", err, target);
                mem_free(saved);
                bash_set_exit_code(1);
                return (Item){.item = i2it(1)};
            }
            mem_free(dir_stack[0]);
            dir_stack[0] = saved;
        } else {
            // -n: just swap the entries without changing directory
            mem_free(dir_stack[0]);
            dir_stack[0] = saved;
        }
        dirstack_sync_var();
        dirstack_print();
        bash_set_exit_code(0);
        return (Item){.item = i2it(0)};
    }

    String* arg = it2s(bash_to_string(args[argi]));
    if (!arg || arg->len == 0) {
        bash_set_exit_code(1);
        return (Item){.item = i2it(1)};
    }

    // +N or -N: rotate
    if (arg->chars[0] == '+' || (arg->chars[0] == '-' && isdigit((unsigned char)arg->chars[1]))) {
        int total = dirstack_total();
        char nbuf[32]; int nlen = arg->len < 31 ? arg->len : 31;
        memcpy(nbuf, arg->chars, nlen); nbuf[nlen] = '\0';
        int offset = atoi(nbuf);
        int idx;
        if (arg->chars[0] == '+') {
            idx = offset;
        } else {
            idx = total - 1 + offset; // -N means from bottom: -0=bottom, -1=one above
        }
        if (idx < 0 || idx >= total) {
            dirstack_err_prefix(err, sizeof(err));
            fflush(stdout);
            fprintf(stderr, "%s: pushd: %.*s: directory stack index out of range\n",
                    err, arg->len, arg->chars);
            bash_set_exit_code(1);
            return (Item){.item = i2it(1)};
        }
        // rotate: move entry at idx to top
        // build entire stack list starting with PWD at position 0
        // then rotate so idx becomes position 0
        char* full[DIRSTACK_MAX + 1];
        const char* pwd = dirstack_get_pwd();
        full[0] = mem_strdup(pwd, MEM_CAT_BASH_RUNTIME);
        for (int i = 0; i < dir_stack_size; i++) full[i + 1] = mem_strdup(dir_stack[i], MEM_CAT_BASH_RUNTIME);
        // rotate left by idx
        char* rotated[DIRSTACK_MAX + 1];
        for (int i = 0; i < total; i++) {
            rotated[i] = full[(i + idx) % total];
        }
        // set PWD to rotated[0], stack to rotated[1..]
        if (!no_cd) {
            if (dirstack_chdir(rotated[0]) != 0) {
                dirstack_err_prefix(err, sizeof(err));
                fflush(stdout);
                fprintf(stderr, "%s: pushd: %s: No such file or directory\n", err, rotated[0]);
                for (int i = 0; i < total; i++) mem_free(full[i]);
                bash_set_exit_code(1);
                return (Item){.item = i2it(1)};
            }
        }
        // update stack
        for (int i = 0; i < dir_stack_size; i++) mem_free(dir_stack[i]);
        dir_stack_size = total - 1;
        for (int i = 0; i < dir_stack_size; i++) dir_stack[i] = rotated[i + 1];
        mem_free(rotated[0]); // was used for chdir
        dirstack_sync_var();
        dirstack_print();
        bash_set_exit_code(0);
        return (Item){.item = i2it(0)};
    }

    // pushd dir: push current dir, cd to new dir
    char path[4096];
    int copy_len = arg->len < (int)sizeof(path) - 1 ? arg->len : (int)sizeof(path) - 1;
    memcpy(path, arg->chars, copy_len);
    path[copy_len] = '\0';

    // check if directory exists
    struct stat st;
    if (stat(path, &st) != 0 || !S_ISDIR(st.st_mode)) {
        dirstack_err_prefix(err, sizeof(err));
        fflush(stdout);
        fprintf(stderr, "%s: pushd: %s: No such file or directory\n", err, path);
        bash_set_exit_code(1);
        return (Item){.item = i2it(1)};
    }

    if (no_cd) {
        // -n: add dir to stack without cd-ing
        dirstack_push_front(path);
    } else {
        const char* old_pwd = dirstack_get_pwd();
        dirstack_push_front(old_pwd);
        if (dirstack_chdir(path) != 0) {
            dirstack_pop_front();
            dirstack_err_prefix(err, sizeof(err));
            fflush(stdout);
            fprintf(stderr, "%s: pushd: %s: No such file or directory\n", err, path);
            bash_set_exit_code(1);
            return (Item){.item = i2it(1)};
        }
    }
    dirstack_sync_var();
    dirstack_print();
    bash_set_exit_code(0);
    return (Item){.item = i2it(0)};
}

// popd [-n] [+N | -N]
extern "C" Item bash_builtin_popd(Item* args, int argc) {
    bool no_cd = false;
    int argi = 0;
    char err[256];

    while (argi < argc) {
        String* s = it2s(bash_to_string(args[argi]));
        if (!s || s->len == 0 || s->chars[0] != '-') break;
        if (s->len == 2 && s->chars[1] == 'n') {
            no_cd = true;
            argi++;
        } else if (s->chars[1] == '-' && s->len == 2) {
            argi++; break;
        } else {
            if (isdigit((unsigned char)s->chars[1])) break;
            dirstack_err_prefix(err, sizeof(err));
            fflush(stdout);
            fprintf(stderr, "%s: popd: %.*s: invalid number\n", err, s->len, s->chars);
            fprintf(stderr, "popd: usage: popd [-n] [+N | -N]\n");
            bash_set_exit_code(1);
            return (Item){.item = i2it(1)};
        }
    }

    if (argi >= argc) {
        // popd with no args: pop top, cd to new top
        if (dir_stack_size == 0) {
            dirstack_err_prefix(err, sizeof(err));
            fflush(stdout);
            fprintf(stderr, "%s: popd: directory stack empty\n", err);
            bash_set_exit_code(1);
            return (Item){.item = i2it(1)};
        }
        const char* target = dir_stack[0];
        if (!no_cd) {
            if (dirstack_chdir(target) != 0) {
                dirstack_err_prefix(err, sizeof(err));
                fflush(stdout);
                fprintf(stderr, "%s: popd: %s: No such file or directory\n", err, target);
                bash_set_exit_code(1);
                return (Item){.item = i2it(1)};
            }
        }
        dirstack_pop_front();
        dirstack_sync_var();
        dirstack_print();
        bash_set_exit_code(0);
        return (Item){.item = i2it(0)};
    }

    String* arg = it2s(bash_to_string(args[argi]));
    if (!arg || arg->len == 0) {
        bash_set_exit_code(1);
        return (Item){.item = i2it(1)};
    }

    int total = dirstack_total();
    char nbuf[32]; int nlen = arg->len < 31 ? arg->len : 31;
    memcpy(nbuf, arg->chars, nlen); nbuf[nlen] = '\0';
    int offset = atoi(nbuf);
    int idx;
    if (arg->chars[0] == '+') {
        idx = offset;
    } else if (arg->chars[0] == '-') {
        idx = total - 1 + offset;
    } else {
        bash_set_exit_code(1);
        return (Item){.item = i2it(1)};
    }

    if (idx < 0 || idx >= total) {
        dirstack_err_prefix(err, sizeof(err));
        fflush(stdout);
        fprintf(stderr, "%s: popd: %.*s: directory stack index out of range\n",
                err, arg->len, arg->chars);
        bash_set_exit_code(1);
        return (Item){.item = i2it(1)};
    }

    if (idx == 0) {
        // popd +0: remove current directory, cd to next
        if (dir_stack_size == 0) {
            dirstack_err_prefix(err, sizeof(err));
            fflush(stdout);
            fprintf(stderr, "%s: popd: directory stack empty\n", err);
            bash_set_exit_code(1);
            return (Item){.item = i2it(1)};
        }
        if (!no_cd) dirstack_chdir(dir_stack[0]);
        dirstack_pop_front();
    } else {
        // popd +N/-N: remove entry at index (1-based maps to stack index-1)
        if (no_cd || idx != 0) {
            dirstack_remove_at(idx - 1);
        }
    }
    dirstack_sync_var();
    dirstack_print();
    bash_set_exit_code(0);
    return (Item){.item = i2it(0)};
}

// dirs [-clpv] [+N] [-N]
extern "C" Item bash_builtin_dirs(Item* args, int argc) {
    bool flag_c = false, flag_l = false, flag_p = false, flag_v = false;
    int argi = 0;
    char err[256];

    while (argi < argc) {
        String* s = it2s(bash_to_string(args[argi]));
        if (!s || s->len == 0 || s->chars[0] != '-' && s->chars[0] != '+') break;
        if (s->chars[0] == '+') break; // +N arg, not a flag
        if (s->len >= 2 && s->chars[0] == '-' && isdigit((unsigned char)s->chars[1])) break; // -N arg
        if (s->chars[0] == '-' && s->len >= 2) {
            for (int j = 1; j < s->len; j++) {
                switch (s->chars[j]) {
                case 'c': flag_c = true; break;
                case 'l': flag_l = true; break;
                case 'p': flag_p = true; break;
                case 'v': flag_v = true; break;
                default:
                    dirstack_err_prefix(err, sizeof(err));
                    fflush(stdout);
                    fprintf(stderr, "%s: dirs: %c: invalid option\n", err, s->chars[j]);
                    fprintf(stderr, "dirs: usage: dirs [-clpv] [+N] [-N]\n");
                    bash_set_exit_code(2);
                    return (Item){.item = i2it(2)};
                }
            }
            argi++;
        } else {
            break;
        }
    }

    if (flag_c) {
        // clear the stack
        for (int i = 0; i < dir_stack_size; i++) mem_free(dir_stack[i]);
        dir_stack_size = 0;
        dirstack_sync_var();
        bash_set_exit_code(0);
        return (Item){.item = i2it(0)};
    }

    // check for +N / -N argument
    if (argi < argc) {
        String* s = it2s(bash_to_string(args[argi]));
        if (s && s->len > 0 && (s->chars[0] == '+' || (s->chars[0] == '-' && isdigit((unsigned char)s->chars[1])))) {
            char nbuf[32]; int nlen = s->len < 31 ? s->len : 31;
            memcpy(nbuf, s->chars, nlen); nbuf[nlen] = '\0';
            int offset = atoi(nbuf);
            int total = dirstack_total();
            int idx;
            if (s->chars[0] == '+') {
                idx = offset;
            } else {
                idx = total - 1 + offset;
            }
            if (idx < 0 || idx >= total) {
                dirstack_err_prefix(err, sizeof(err));
                fflush(stdout);
                fprintf(stderr, "%s: dirs: %d: directory stack index out of range\n",
                        err, offset >= 0 ? offset : -offset);
                bash_set_exit_code(1);
                return (Item){.item = i2it(1)};
            }
            const char* entry = dirstack_get(idx);
            if (entry) {
                if (flag_v) {
                    printf(" %d  %s\n", idx, entry);
                } else {
                    printf("%s\n", entry);
                }
            }
            bash_set_exit_code(0);
            return (Item){.item = i2it(0)};
        }
        // not a valid argument — check if it's a bare number (dirs 7 → invalid option)
        if (s && s->len > 0 && isdigit((unsigned char)s->chars[0])) {
            dirstack_err_prefix(err, sizeof(err));
            fflush(stdout);
            fprintf(stderr, "%s: dirs: %.*s: invalid option\n", err, s->len, s->chars);
            fprintf(stderr, "dirs: usage: dirs [-clpv] [+N] [-N]\n");
            bash_set_exit_code(2);
            return (Item){.item = i2it(2)};
        }
    }

    // print full stack
    if (flag_v) {
        dirstack_print_verbose();
    } else if (flag_l) {
        dirstack_print_long();
    } else {
        dirstack_print();
    }
    bash_set_exit_code(0);
    return (Item){.item = i2it(0)};
}

// get Nth directory from stack by absolute index (0=top/PWD)
// for tilde expansion: caller converts ~-N to proper index
extern "C" Item bash_dirstack_get(Item index) {
    int64_t idx = (int64_t)it2i(index);
    int total = dirstack_total();
    if (idx < 0 || idx >= total) {
        return (Item){.item = 0};
    }
    const char* entry = dirstack_get((int)idx);
    if (!entry) {
        return (Item){.item = 0};
    }
    return (Item){.item = s2it(heap_create_name(entry, strlen(entry)))};
}

// get total number of directory stack entries (for tilde ~-N computation)
extern "C" Item bash_dirstack_total(void) {
    return (Item){.item = i2it(dirstack_total())};
}

// ============================================================================
// getopts builtin
// ============================================================================

extern "C" Item bash_builtin_getopts(Item* args, int argc) {
    // getopts optstring name [args...]
    // args[0] = optstring, args[1] = name, args[2..] = override args (optional)
    if (argc < 2) {
        fprintf(stderr, "getopts: usage: getopts optstring name [arg ...]\n");
        bash_set_exit_code(2);
        return (Item){.item = i2it(2)};
    }

    // check if first arg looks like an invalid option to getopts itself
    String* first_str = it2s(bash_to_string(args[0]));
    if (first_str && first_str->len > 0 && first_str->chars[0] == '-') {
        fprintf(stderr, "%s: line %d: getopts: %.*s: invalid option\n",
                it2s(bash_get_script_name())->chars, (int)it2i(bash_get_lineno()),
                first_str->len, first_str->chars);
        fprintf(stderr, "getopts: usage: getopts optstring name [arg ...]\n");
        bash_set_exit_code(2);
        return (Item){.item = i2it(2)};
    }

    String* optstring_str = it2s(bash_to_string(args[0]));
    String* varname_str = it2s(bash_to_string(args[1]));
    if (!optstring_str || !varname_str) {
        bash_set_exit_code(2);
        return (Item){.item = i2it(2)};
    }

    const char* optstring = optstring_str->chars;
    int optstring_len = optstring_str->len;
    const char* varname = varname_str->chars;
    int varname_len = varname_str->len;

    // validate variable name (print error but still process options for OPTIND tracking)
    bool bad_varname = false;
    if (varname_len == 0 || !(isalpha((unsigned char)varname[0]) || varname[0] == '_')) {
        fprintf(stderr, "%s: line %d: getopts: `%.*s': not a valid identifier\n",
                it2s(bash_get_script_name())->chars, (int)it2i(bash_get_lineno()), varname_len, varname);
        bad_varname = true;
    }
    if (!bad_varname) {
        for (int i = 1; i < varname_len; i++) {
            if (!(isalnum((unsigned char)varname[i]) || varname[i] == '_')) {
                fprintf(stderr, "%s: line %d: getopts: `%.*s': not a valid identifier\n",
                        it2s(bash_get_script_name())->chars, (int)it2i(bash_get_lineno()), varname_len, varname);
                bad_varname = true;
                break;
            }
        }
    }

    // check silent mode (leading ':' in optstring)
    bool silent = false;
    const char* opts = optstring;
    int opts_len = optstring_len;
    if (opts_len > 0 && opts[0] == ':') {
        silent = true;
        opts++;
        opts_len--;
    }

    // check OPTERR
    bool opterr = true;
    {
        Item opterr_name = {.item = s2it(heap_create_name("OPTERR", 6))};
        Item opterr_val = bash_get_var(opterr_name);
        String* opterr_s = it2s(opterr_val);
        if (opterr_s && opterr_s->len == 1 && opterr_s->chars[0] == '0') {
            opterr = false;
        }
    }

    // get OPTIND (1-based index into args)
    int optind = 1;
    {
        Item optind_name = {.item = s2it(heap_create_name("OPTIND", 6))};
        Item optind_val = bash_get_var(optind_name);
        String* optind_s = it2s(optind_val);
        if (optind_s && optind_s->len > 0) {
            optind = atoi(optind_s->chars);
            if (optind < 1) optind = 1;
        }
    }

    // determine the argument list to parse
    // if extra args provided (argc > 2), use those; otherwise use positional params
    bool use_extra_args = (argc > 2);
    int arg_count;
    if (use_extra_args) {
        arg_count = argc - 2;
    } else {
        Item count_item = bash_get_arg_count();
        arg_count = (int)it2i(count_item);
    }

    // helper to get arg at 1-based index
    auto get_arg = [&](int idx) -> String* {
        if (use_extra_args) {
            if (idx < 1 || idx > arg_count) return NULL;
            return it2s(bash_to_string(args[1 + idx]));
        } else {
            if (idx < 1 || idx > arg_count) return NULL;
            return it2s(bash_to_string(bash_get_positional(idx)));
        }
    };

    // if OPTIND was reset externally, reset charind
    if (optind != getopts_last_optind) {
        getopts_charind = 0;
        getopts_last_optind = optind;
    }

    // if optind is out of range, we're done
    if (optind > arg_count) {
        getopts_charind = 0;
        Item name_item = {.item = s2it(heap_create_name(varname, varname_len))};
        Item q_item = {.item = s2it(heap_create_name("?", 1))};
        bash_set_var(name_item, q_item);
        bash_set_exit_code(1);
        return (Item){.item = i2it(1)};
    }

    String* current_arg = get_arg(optind);
    if (!current_arg) {
        getopts_charind = 0;
        Item name_item = {.item = s2it(heap_create_name(varname, varname_len))};
        Item q_item = {.item = s2it(heap_create_name("?", 1))};
        bash_set_var(name_item, q_item);
        bash_set_exit_code(1);
        return (Item){.item = i2it(1)};
    }

    // skip non-option arguments (doesn't start with '-', or is "-" alone, or is "--")
    if (current_arg->len == 0 || current_arg->chars[0] != '-' || current_arg->len == 1) {
        // not an option — done
        getopts_charind = 0;
        Item name_item = {.item = s2it(heap_create_name(varname, varname_len))};
        Item q_item = {.item = s2it(heap_create_name("?", 1))};
        bash_set_var(name_item, q_item);
        bash_set_exit_code(1);
        return (Item){.item = i2it(1)};
    }

    // check for "--" (end of options)
    if (current_arg->len == 2 && current_arg->chars[0] == '-' && current_arg->chars[1] == '-') {
        getopts_charind = 0;
        optind++;
        // update OPTIND
        char buf[16];
        int blen = snprintf(buf, sizeof(buf), "%d", optind);
        Item optind_name = {.item = s2it(heap_create_name("OPTIND", 6))};
        Item optind_val = {.item = s2it(heap_create_name(buf, blen))};
        bash_set_var(optind_name, optind_val);
        getopts_last_optind = optind;

        Item name_item = {.item = s2it(heap_create_name(varname, varname_len))};
        Item q_item = {.item = s2it(heap_create_name("?", 1))};
        bash_set_var(name_item, q_item);
        bash_set_exit_code(1);
        return (Item){.item = i2it(1)};
    }

    // current_arg starts with '-' and has at least 2 chars
    // determine which character in the option string we're looking at
    int charpos = getopts_charind + 1;  // skip the leading '-'
    if (charpos >= current_arg->len) {
        // this shouldn't happen, but handle gracefully by moving to next arg
        getopts_charind = 0;
        optind++;
        charpos = 1;
        current_arg = get_arg(optind);
        if (!current_arg || current_arg->len < 2 || current_arg->chars[0] != '-') {
            char buf[16];
            int blen = snprintf(buf, sizeof(buf), "%d", optind);
            Item optind_name = {.item = s2it(heap_create_name("OPTIND", 6))};
            Item optind_val = {.item = s2it(heap_create_name(buf, blen))};
            bash_set_var(optind_name, optind_val);
            getopts_last_optind = optind;
            Item name_item = {.item = s2it(heap_create_name(varname, varname_len))};
            Item q_item = {.item = s2it(heap_create_name("?", 1))};
            bash_set_var(name_item, q_item);
            bash_set_exit_code(1);
            return (Item){.item = i2it(1)};
        }
    }

    char opt_char = current_arg->chars[charpos];

    // find opt_char in optstring
    int found_pos = -1;
    bool has_arg = false;
    for (int i = 0; i < opts_len; i++) {
        if (opts[i] == opt_char) {
            found_pos = i;
            has_arg = (i + 1 < opts_len && opts[i + 1] == ':');
            break;
        }
    }

    Item name_item = {.item = s2it(heap_create_name(varname, varname_len))};
    Item optarg_name = {.item = s2it(heap_create_name("OPTARG", 6))};

    if (found_pos < 0) {
        // unknown option
        if (silent) {
            // silent mode: set name to '?', OPTARG to the bad char
            char bad[2] = {opt_char, '\0'};
            Item q_item = {.item = s2it(heap_create_name("?", 1))};
            Item bad_item = {.item = s2it(heap_create_name(bad, 1))};
            bash_set_var(name_item, q_item);
            bash_set_var(optarg_name, bad_item);
        } else {
            // print error, set name to '?', unset OPTARG
            if (opterr) {
                fprintf(stderr, "%s: illegal option -- %c\n",
                        it2s(bash_get_script_name())->chars, opt_char);
            }
            Item q_item = {.item = s2it(heap_create_name("?", 1))};
            bash_set_var(name_item, q_item);
            // unset OPTARG
            Item empty = {.item = s2it(heap_create_name("", 0))};
            bash_set_var(optarg_name, empty);
        }

        // advance within combined options or to next arg
        if (charpos + 1 < current_arg->len) {
            getopts_charind = charpos;
        } else {
            getopts_charind = 0;
            optind++;
        }

        // update OPTIND
        char buf[16];
        int blen = snprintf(buf, sizeof(buf), "%d", optind);
        Item optind_name_item = {.item = s2it(heap_create_name("OPTIND", 6))};
        Item optind_val_item = {.item = s2it(heap_create_name(buf, blen))};
        bash_set_var(optind_name_item, optind_val_item);
        getopts_last_optind = optind;

        bash_set_exit_code(0);
        return (Item){.item = i2it(0)};
    }

    // valid option found
    char opt_str[2] = {opt_char, '\0'};
    Item opt_item = {.item = s2it(heap_create_name(opt_str, 1))};
    bash_set_var(name_item, opt_item);

    if (has_arg) {
        // option requires an argument
        if (charpos + 1 < current_arg->len) {
            // rest of current arg is the option argument (e.g., -bval)
            const char* optarg_val = current_arg->chars + charpos + 1;
            int optarg_len = current_arg->len - charpos - 1;
            Item optarg_item = {.item = s2it(heap_create_name(optarg_val, optarg_len))};
            bash_set_var(optarg_name, optarg_item);
            getopts_charind = 0;
            optind++;
        } else {
            // next arg is the option argument
            optind++;
            String* next_arg = get_arg(optind);
            if (!next_arg) {
                // missing argument
                if (silent) {
                    Item colon_item = {.item = s2it(heap_create_name(":", 1))};
                    bash_set_var(name_item, colon_item);
                    Item bad_item = {.item = s2it(heap_create_name(opt_str, 1))};
                    bash_set_var(optarg_name, bad_item);
                } else {
                    if (opterr) {
                        fprintf(stderr, "%s: option requires an argument -- %c\n",
                                it2s(bash_get_script_name())->chars, opt_char);
                    }
                    Item q_item = {.item = s2it(heap_create_name("?", 1))};
                    bash_set_var(name_item, q_item);
                    Item empty = {.item = s2it(heap_create_name("", 0))};
                    bash_set_var(optarg_name, empty);
                }
                getopts_charind = 0;
                // update OPTIND and return error (but still exit 0 for the loop to continue in bash)
                // actually bash returns 1 here to stop the while loop
                optind++;
                char buf[16];
                int blen = snprintf(buf, sizeof(buf), "%d", optind);
                Item optind_name_item = {.item = s2it(heap_create_name("OPTIND", 6))};
                Item optind_val_item = {.item = s2it(heap_create_name(buf, blen))};
                bash_set_var(optind_name_item, optind_val_item);
                getopts_last_optind = optind;
                bash_set_exit_code(0);
                return (Item){.item = i2it(0)};
            } else {
                Item optarg_item = {.item = s2it(heap_create_name(next_arg->chars, next_arg->len))};
                bash_set_var(optarg_name, optarg_item);
                getopts_charind = 0;
                optind++;
            }
        }
    } else {
        // no argument needed — unset OPTARG
        Item empty = {.item = s2it(heap_create_name("", 0))};
        bash_set_var(optarg_name, empty);

        // advance within combined options or to next arg
        if (charpos + 1 < current_arg->len) {
            getopts_charind = charpos;
        } else {
            getopts_charind = 0;
            optind++;
        }
    }

    // update OPTIND
    char buf[16];
    int blen = snprintf(buf, sizeof(buf), "%d", optind);
    Item optind_name_item = {.item = s2it(heap_create_name("OPTIND", 6))};
    Item optind_val_item = {.item = s2it(heap_create_name(buf, blen))};
    bash_set_var(optind_name_item, optind_val_item);
    getopts_last_optind = optind;

    if (bad_varname) {
        bash_set_exit_code(1);
        return (Item){.item = i2it(1)};
    }
    bash_set_exit_code(0);
    return (Item){.item = i2it(0)};
}
