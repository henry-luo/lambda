// py_stdlib.cpp — Python standard library module stubs (Phase C)
// Maximally reuses existing Lambda/JS runtime functions.

#include "py_stdlib.h"
#include "py_runtime.h"
#include "py_class.h"
#include "py_async.h"
#include "../lambda-data.hpp"
#include "../lambda.h"

#include <cmath>
#include <cstring>
#include "../../lib/mem.h"
#include <ctime>
#ifndef _WIN32
#include <unistd.h>    // for usleep
#endif

#include "../../lib/log.h"
#include "../../lib/strbuf.h"
#include "../../lib/stringbuf.h"
#include "../../lib/file.h"
#include "../../lib/file_utils.h"
#include "../../lib/shell.h"
#include "../sysinfo.h"

// Lambda runtime externs (defined in py_runtime.cpp)
extern Input* py_input;

// Lambda format functions (C++ linkage)
#include "../format/format.h"

// Lambda JS runtime functions
extern "C" Item js_property_get(Item, Item);
extern "C" Item js_property_set(Item, Item, Item);
extern "C" Item js_object_keys(Item);
extern "C" int64_t js_array_length(Item);
extern "C" Item js_array_get_int(Item, int64_t);

// Lambda clock function
extern "C" double pn_clock(void);

// Lambda parse/format wrappers
extern "C" Item fn_parse2_mir(Item, Item);

// =========================================================================
// Helper: create a name item (symbol/key) for dict keys
// =========================================================================
static Item mk_name(const char* s) {
    return (Item){.item = s2it(heap_create_name(s))};
}

// Helper: create a float item from a double
static Item mk_float(double d) {
    if (!py_input) return ItemNull;
    double* pd = (double*)pool_calloc(py_input->pool, sizeof(double));
    *pd = d;
    return (Item){.item = d2it(pd)};
}

// Helper: create an int item
static Item mk_int(int64_t v) {
    return (Item){.item = i2it(v)};
}

// Helper: create a string item
static Item mk_str(const char* s) {
    return (Item){.item = s2it(heap_create_name(s))};
}

// Helper: register a C function in a module namespace dict
static void mod_set_func(Item mod, const char* name, void* fn, int arity) {
    py_dict_set(mod, mk_name(name), py_new_function(fn, arity));
}

// Helper: register a constant value
static void mod_set(Item mod, const char* name, Item value) {
    py_dict_set(mod, mk_name(name), value);
}

// =========================================================================
// MATH MODULE — reuses Lambda fn_math_* functions directly
// =========================================================================

// math.factorial(n)
static Item py_math_factorial(Item n) {
    TypeId t = get_type_id(n);
    int64_t v = 0;
    if (t == LMD_TYPE_INT) v = it2i(n);
    else if (t == LMD_TYPE_INT64) v = it2l(n);
    else {
        log_error("py-stdlib: math.factorial requires an integer argument");
        return ItemNull;
    }
    if (v < 0) {
        log_error("py-stdlib: math.factorial not defined for negative values");
        return ItemNull;
    }
    int64_t result = 1;
    for (int64_t i = 2; i <= v; i++) result *= i;
    return mk_int(result);
}

// math.gcd(a, b)
static Item py_math_gcd(Item a, Item b) {
    int64_t x = (get_type_id(a) == LMD_TYPE_INT) ? it2i(a) : (get_type_id(a) == LMD_TYPE_INT64 ? it2l(a) : 0);
    int64_t y = (get_type_id(b) == LMD_TYPE_INT) ? it2i(b) : (get_type_id(b) == LMD_TYPE_INT64 ? it2l(b) : 0);
    if (x < 0) x = -x;
    if (y < 0) y = -y;
    while (y != 0) { int64_t t = y; y = x % y; x = t; }
    return mk_int(x);
}

// math.lcm(a, b)
static Item py_math_lcm(Item a, Item b) {
    Item g = py_math_gcd(a, b);
    int64_t gv = (get_type_id(g) == LMD_TYPE_INT) ? it2i(g) : 0;
    if (gv == 0) return mk_int(0);
    int64_t x = (get_type_id(a) == LMD_TYPE_INT) ? it2i(a) : (get_type_id(a) == LMD_TYPE_INT64 ? it2l(a) : 0);
    int64_t y = (get_type_id(b) == LMD_TYPE_INT) ? it2i(b) : (get_type_id(b) == LMD_TYPE_INT64 ? it2l(b) : 0);
    if (x < 0) x = -x;
    if (y < 0) y = -y;
    return mk_int((x / gv) * y);
}

// math.isnan(x)
static Item py_math_isnan(Item x) {
    if (get_type_id(x) != LMD_TYPE_FLOAT) return (Item){.item = b2it(false)};
    return (Item){.item = b2it(isnan(it2d(x)))};
}

// math.isinf(x)
static Item py_math_isinf(Item x) {
    if (get_type_id(x) != LMD_TYPE_FLOAT) return (Item){.item = b2it(false)};
    return (Item){.item = b2it(isinf(it2d(x)))};
}

// math.isfinite(x)
static Item py_math_isfinite(Item x) {
    if (get_type_id(x) != LMD_TYPE_FLOAT) return (Item){.item = b2it(true)};
    return (Item){.item = b2it(isfinite(it2d(x)))};
}

// math.fabs(x)
static Item py_math_fabs(Item x) {
    double v = 0;
    TypeId t = get_type_id(x);
    if (t == LMD_TYPE_FLOAT) v = it2d(x);
    else if (t == LMD_TYPE_INT) v = (double)it2i(x);
    else if (t == LMD_TYPE_INT64) v = (double)it2l(x);
    return mk_float(fabs(v));
}

// math.copysign(x, y)
static Item py_math_copysign(Item x, Item y) {
    double xv = (get_type_id(x) == LMD_TYPE_FLOAT) ? it2d(x) : (double)it2i(x);
    double yv = (get_type_id(y) == LMD_TYPE_FLOAT) ? it2d(y) : (double)it2i(y);
    return mk_float(copysign(xv, yv));
}

// math.fmod(x, y)
static Item py_math_fmod(Item x, Item y) {
    double xv = (get_type_id(x) == LMD_TYPE_FLOAT) ? it2d(x) : (double)it2i(x);
    double yv = (get_type_id(y) == LMD_TYPE_FLOAT) ? it2d(y) : (double)it2i(y);
    return mk_float(fmod(xv, yv));
}

// math.degrees(x)
static Item py_math_degrees(Item x) {
    double v = (get_type_id(x) == LMD_TYPE_FLOAT) ? it2d(x) : (double)it2i(x);
    return mk_float(v * (180.0 / M_PI));
}

// math.radians(x)
static Item py_math_radians(Item x) {
    double v = (get_type_id(x) == LMD_TYPE_FLOAT) ? it2d(x) : (double)it2i(x);
    return mk_float(v * (M_PI / 180.0));
}

extern "C" Item py_stdlib_math_init(void) {
    Item mod = py_dict_new();

    // constants
    mod_set(mod, "pi",  mk_float(M_PI));
    mod_set(mod, "e",   mk_float(M_E));
    mod_set(mod, "tau", mk_float(2.0 * M_PI));
    mod_set(mod, "inf", mk_float(INFINITY));
    mod_set(mod, "nan", mk_float(NAN));

    // direct reuse of Lambda fn_math_* functions (Item→Item, arity matches)
    mod_set_func(mod, "sqrt",  (void*)fn_math_sqrt, 1);
    mod_set_func(mod, "log",   (void*)fn_math_log, 1);
    mod_set_func(mod, "log2",  (void*)fn_math_log2, 1);
    mod_set_func(mod, "log10", (void*)fn_math_log10, 1);
    mod_set_func(mod, "log1p", (void*)fn_math_log1p, 1);
    mod_set_func(mod, "exp",   (void*)fn_math_exp, 1);
    mod_set_func(mod, "exp2",  (void*)fn_math_exp2, 1);
    mod_set_func(mod, "expm1", (void*)fn_math_expm1, 1);
    mod_set_func(mod, "sin",   (void*)fn_math_sin, 1);
    mod_set_func(mod, "cos",   (void*)fn_math_cos, 1);
    mod_set_func(mod, "tan",   (void*)fn_math_tan, 1);
    mod_set_func(mod, "asin",  (void*)fn_math_asin, 1);
    mod_set_func(mod, "acos",  (void*)fn_math_acos, 1);
    mod_set_func(mod, "atan",  (void*)fn_math_atan, 1);
    mod_set_func(mod, "atan2", (void*)fn_math_atan2, 2);
    mod_set_func(mod, "sinh",  (void*)fn_math_sinh, 1);
    mod_set_func(mod, "cosh",  (void*)fn_math_cosh, 1);
    mod_set_func(mod, "tanh",  (void*)fn_math_tanh, 1);
    mod_set_func(mod, "asinh", (void*)fn_math_asinh, 1);
    mod_set_func(mod, "acosh", (void*)fn_math_acosh, 1);
    mod_set_func(mod, "atanh", (void*)fn_math_atanh, 1);
    mod_set_func(mod, "pow",   (void*)fn_math_pow, 2);
    mod_set_func(mod, "cbrt",  (void*)fn_math_cbrt, 1);
    mod_set_func(mod, "hypot", (void*)fn_math_hypot, 2);

    // reuse Lambda builtins directly
    mod_set_func(mod, "floor", (void*)fn_floor, 1);
    mod_set_func(mod, "ceil",  (void*)fn_ceil, 1);
    mod_set_func(mod, "trunc", (void*)fn_trunc, 1);
    mod_set_func(mod, "fabs",  (void*)py_math_fabs, 1);

    // new math functions
    mod_set_func(mod, "factorial", (void*)py_math_factorial, 1);
    mod_set_func(mod, "gcd",      (void*)py_math_gcd, 2);
    mod_set_func(mod, "lcm",      (void*)py_math_lcm, 2);
    mod_set_func(mod, "isnan",    (void*)py_math_isnan, 1);
    mod_set_func(mod, "isinf",    (void*)py_math_isinf, 1);
    mod_set_func(mod, "isfinite", (void*)py_math_isfinite, 1);
    mod_set_func(mod, "copysign", (void*)py_math_copysign, 2);
    mod_set_func(mod, "fmod",     (void*)py_math_fmod, 2);
    mod_set_func(mod, "degrees",  (void*)py_math_degrees, 1);
    mod_set_func(mod, "radians",  (void*)py_math_radians, 1);

    // reuse Lambda sum/prod for math.fsum, math.prod
    mod_set_func(mod, "fsum", (void*)fn_sum, 1);
    mod_set_func(mod, "prod", (void*)fn_math_prod, 1);

    return mod;
}

// =========================================================================
// OS MODULE — thin wrappers over POSIX + Lambda path utilities
// =========================================================================

// os.getcwd()
static Item py_os_getcwd(void) {
    char* cwd = file_getcwd();
    if (cwd) {
        Item result = mk_str(cwd);
        mem_free(cwd); // cwd from file_getcwd (lib)
        return result;
    }
    return mk_str(".");
}

// os.listdir(path)
static Item py_os_listdir(Item path_item) {
    const char* path = ".";
    if (get_type_id(path_item) == LMD_TYPE_STRING || get_type_id(path_item) == LMD_TYPE_SYMBOL) {
        String* s = it2s(path_item);
        if (s && s->chars) path = s->chars;
    }

    Item result = py_list_new(0);
    ArrayList* entries = dir_list(path);
    if (!entries) return result;

    for (int i = 0; i < entries->length; i++) {
        DirEntry* entry = (DirEntry*)entries->data[i];
        py_list_append(result, mk_str(entry->name));
        dir_entry_free(entry);
    }
    arraylist_free(entries);
    return result;
}

// os.path.join(a, b, ...)  — up to 6 args
static Item py_os_path_join(Item a, Item b, Item c, Item d, Item e, Item f) {
    StrBuf* sb = strbuf_new();
    Item args[] = {a, b, c, d, e, f};
    for (int i = 0; i < 6; i++) {
        if (args[i].item == ItemNull.item) break;
        String* s = it2s(args[i]);
        if (!s || !s->chars) continue;
        // if absolute path, reset
        if (s->chars[0] == '/') {
            strbuf_reset(sb);
            strbuf_append_str(sb, s->chars);
        } else {
            if (sb->length > 0 && sb->str[sb->length - 1] != '/')
                strbuf_append_char(sb, '/');
            strbuf_append_str(sb, s->chars);
        }
    }
    Item result = mk_str(sb->str ? sb->str : "");
    strbuf_free(sb);
    return result;
}

// os.path.exists(p)
static Item py_os_path_exists(Item p) {
    String* s = it2s(p);
    if (!s || !s->chars) return (Item){.item = b2it(false)};
    return (Item){.item = b2it(file_exists(s->chars))};
}

// os.path.isfile(p)
static Item py_os_path_isfile(Item p) {
    String* s = it2s(p);
    if (!s || !s->chars) return (Item){.item = b2it(false)};
    return (Item){.item = b2it(file_is_file(s->chars))};
}

// os.path.isdir(p)
static Item py_os_path_isdir(Item p) {
    String* s = it2s(p);
    if (!s || !s->chars) return (Item){.item = b2it(false)};
    return (Item){.item = b2it(file_is_dir(s->chars))};
}

// os.path.basename(p)
static Item py_os_path_basename(Item p) {
    String* s = it2s(p);
    if (!s || !s->chars) return mk_str("");
    const char* last_slash = strrchr(s->chars, '/');
    return mk_str(last_slash ? last_slash + 1 : s->chars);
}

// os.path.dirname(p)
static Item py_os_path_dirname(Item p) {
    String* s = it2s(p);
    if (!s || !s->chars) return mk_str("");
    const char* last_slash = strrchr(s->chars, '/');
    if (!last_slash) return mk_str("");
    // allocate just the dirname portion
    int dlen = (int)(last_slash - s->chars);
    char buf[1024];
    if (dlen >= (int)sizeof(buf)) dlen = (int)sizeof(buf) - 1;
    memcpy(buf, s->chars, dlen);
    buf[dlen] = '\0';
    return mk_str(buf);
}

// os.path.abspath(p)
static Item py_os_path_abspath(Item p) {
    String* s = it2s(p);
    if (!s || !s->chars) return mk_str("");
    char* resolved = file_realpath(s->chars);
    if (resolved) {
        Item result = mk_str(resolved);
        mem_free(resolved); // resolved from file_realpath (lib)
        return result;
    }
    // fallback: prepend cwd
    char* cwd = file_getcwd();
    if (cwd) {
        StrBuf* sb = strbuf_new();
        strbuf_append_str(sb, cwd);
        strbuf_append_char(sb, '/');
        strbuf_append_str(sb, s->chars);
        Item result = mk_str(sb->str);
        strbuf_free(sb);
        mem_free(cwd); // cwd from file_getcwd (lib)
        return result;
    }
    return p;
}

// os.path.splitext(p) -> (root, ext) as tuple
static Item py_os_path_splitext(Item p) {
    String* s = it2s(p);
    if (!s || !s->chars) {
        Item tup = py_tuple_new(2);
        py_tuple_set(tup, 0, mk_str(""));
        py_tuple_set(tup, 1, mk_str(""));
        return tup;
    }
    const char* dot = strrchr(s->chars, '.');
    const char* slash = strrchr(s->chars, '/');
    // dot must be after last slash
    if (dot && (!slash || dot > slash) && dot != s->chars) {
        int root_len = (int)(dot - s->chars);
        char buf[1024];
        if (root_len >= (int)sizeof(buf)) root_len = (int)sizeof(buf) - 1;
        memcpy(buf, s->chars, root_len);
        buf[root_len] = '\0';
        Item tup = py_tuple_new(2);
        py_tuple_set(tup, 0, mk_str(buf));
        py_tuple_set(tup, 1, mk_str(dot));
        return tup;
    }
    Item tup = py_tuple_new(2);
    py_tuple_set(tup, 0, p);
    py_tuple_set(tup, 1, mk_str(""));
    return tup;
}

// os.path sub-module init
extern "C" Item py_stdlib_os_path_init(void) {
    Item mod = py_dict_new();
    mod_set_func(mod, "join",     (void*)py_os_path_join, 6);
    mod_set_func(mod, "exists",   (void*)py_os_path_exists, 1);
    mod_set_func(mod, "isfile",   (void*)py_os_path_isfile, 1);
    mod_set_func(mod, "isdir",    (void*)py_os_path_isdir, 1);
    mod_set_func(mod, "basename", (void*)py_os_path_basename, 1);
    mod_set_func(mod, "dirname",  (void*)py_os_path_dirname, 1);
    mod_set_func(mod, "abspath",  (void*)py_os_path_abspath, 1);
    mod_set_func(mod, "splitext", (void*)py_os_path_splitext, 1);
    return mod;
}

// os.getenv(key, default=None)
static Item py_os_getenv(Item key, Item defval) {
    String* s = it2s(key);
    if (!s || !s->chars) return defval;
    const char* val = shell_getenv(s->chars);
    if (!val) return defval;
    return mk_str(val);
}

// os.sep — path separator
// os.linesep — line separator
extern "C" Item py_stdlib_os_init(void) {
    Item mod = py_dict_new();
    mod_set_func(mod, "getcwd",  (void*)py_os_getcwd, 0);
    mod_set_func(mod, "listdir", (void*)py_os_listdir, 1);
    mod_set_func(mod, "getenv",  (void*)py_os_getenv, 2);

    // os.path sub-module
    mod_set(mod, "path", py_stdlib_os_path_init());

    // constants
#ifdef _WIN32
    mod_set(mod, "sep", mk_str("\\"));
    mod_set(mod, "linesep", mk_str("\r\n"));
    mod_set(mod, "name", mk_str("nt"));
#else
    mod_set(mod, "sep", mk_str("/"));
    mod_set(mod, "linesep", mk_str("\n"));
    mod_set(mod, "name", mk_str("posix"));
#endif

    return mod;
}

// =========================================================================
// SYS MODULE
// =========================================================================

// sys.exit(code)
static Item py_sys_exit(Item code) {
    int exit_code = 0;
    if (get_type_id(code) == LMD_TYPE_INT) exit_code = it2i(code);
    exit(exit_code);
    return ItemNull; // unreachable
}

extern "C" Item py_stdlib_sys_init(void) {
    Item mod = py_dict_new();

    mod_set_func(mod, "exit", (void*)py_sys_exit, 1);

    // sys.argv — populated from CLI args: ./lambda.exe py script.py arg1 arg2
    // Python sees [script.py, arg1, arg2]
    {
        Item argv_list = py_list_new(0);
        int argc = sysinfo_get_argc();
        char** argv = sysinfo_get_argv();
        // skip "lambda.exe" and "py" (indices 0 and 1), start at 2
        for (int i = 2; i < argc; i++) {
            py_list_append(argv_list, mk_str(argv[i]));
        }
        mod_set(mod, "argv", argv_list);
    }

    // sys.version
    mod_set(mod, "version", mk_str("3.12.0 (Lambda Python)"));

    // sys.platform
#if defined(__APPLE__)
    mod_set(mod, "platform", mk_str("darwin"));
#elif defined(__linux__)
    mod_set(mod, "platform", mk_str("linux"));
#elif defined(_WIN32)
    mod_set(mod, "platform", mk_str("win32"));
#else
    mod_set(mod, "platform", mk_str("unknown"));
#endif

    // sys.maxsize
    mod_set(mod, "maxsize", mk_int(INT56_MAX));

    // sys.path — empty list (module search paths)
    mod_set(mod, "path", py_list_new(0));

    return mod;
}

// =========================================================================
// RE MODULE — wraps Lambda's RE2 wrapper
// =========================================================================

// Internal: compile a Python-style regex string to a RE2 TypePattern*
// Lambda patterns use AST; for Python's string patterns we create TypePattern directly.
#include "../re2_wrapper.hpp"

// We use RE2 directly since Python regex strings are RE2-compatible (mostly)
#include <re2/re2.h>

// Store compiled regex in a Lambda Map for the Python match object
static Item make_match_object(const char* str, int start, int end, const char* group0,
                              re2::RE2* re, const re2::StringPiece& input_sp,
                              const re2::StringPiece* groups, int ngroups) {
    Item m = py_dict_new();
    // store captured groups as a list
    Item group_list = py_list_new(0);
    py_list_append(group_list, mk_str(group0));  // group(0) = full match
    for (int i = 0; i < ngroups; i++) {
        if (groups[i].data()) {
            char buf[4096];
            int glen = (int)groups[i].size();
            if (glen >= (int)sizeof(buf)) glen = (int)sizeof(buf) - 1;
            memcpy(buf, groups[i].data(), glen);
            buf[glen] = '\0';
            py_list_append(group_list, mk_str(buf));
        } else {
            py_list_append(group_list, ItemNull);
        }
    }
    mod_set(m, "__groups__", group_list);
    mod_set(m, "__start__", mk_int(start));
    mod_set(m, "__end__", mk_int(end));

    return m;
}

// re._match_group(match_obj, n) — used by match.group()
static Item py_re_match_group(Item match_obj, Item n_item) {
    Item groups = py_dict_get(match_obj, mk_name("__groups__"));
    if (groups.item == ItemNull.item) return ItemNull;
    int idx = 0;
    if (get_type_id(n_item) == LMD_TYPE_INT) idx = it2i(n_item);
    return py_list_get(groups, mk_int(idx));
}

// re._match_groups(match_obj) — returns tuple of captured groups (not group 0)
static Item py_re_match_groups(Item match_obj) {
    Item groups = py_dict_get(match_obj, mk_name("__groups__"));
    if (groups.item == ItemNull.item) return py_tuple_new(0);
    int64_t len = py_list_length(groups);
    Item result = py_tuple_new((int)(len > 1 ? len - 1 : 0));
    for (int64_t i = 1; i < len; i++) {
        py_tuple_set(result, (int)(i - 1), py_list_get(groups, mk_int(i)));
    }
    return result;
}

// re._match_start(match_obj)
static Item py_re_match_start(Item match_obj) {
    return py_dict_get(match_obj, mk_name("__start__"));
}

// re._match_end(match_obj)
static Item py_re_match_end(Item match_obj) {
    return py_dict_get(match_obj, mk_name("__end__"));
}

// re._match_span(match_obj)
static Item py_re_match_span(Item match_obj) {
    Item tup = py_tuple_new(2);
    py_tuple_set(tup, 0, py_dict_get(match_obj, mk_name("__start__")));
    py_tuple_set(tup, 1, py_dict_get(match_obj, mk_name("__end__")));
    return tup;
}

// Attach callable methods to a match object as bound methods
static void attach_match_methods(Item m) {
    py_dict_set(m, mk_name("group"),  py_bind_method(py_new_function((void*)py_re_match_group, 2), m));
    py_dict_set(m, mk_name("groups"), py_bind_method(py_new_function((void*)py_re_match_groups, 1), m));
    py_dict_set(m, mk_name("start"),  py_bind_method(py_new_function((void*)py_re_match_start, 1), m));
    py_dict_set(m, mk_name("end"),    py_bind_method(py_new_function((void*)py_re_match_end, 1), m));
    py_dict_set(m, mk_name("span"),   py_bind_method(py_new_function((void*)py_re_match_span, 1), m));
}

// re.match(pattern, string)
static Item py_re_match(Item pattern, Item string) {
    String* pat_s = it2s(pattern);
    String* str_s = it2s(string);
    if (!pat_s || !str_s) return ItemNull;

    re2::RE2 re(re2::StringPiece(pat_s->chars, pat_s->len));
    if (!re.ok()) return ItemNull;

    int ngroups = re.NumberOfCapturingGroups();
    re2::StringPiece input(str_s->chars, str_s->len);
    re2::StringPiece* groups = ngroups > 0 ? new re2::StringPiece[ngroups] : nullptr;
    re2::StringPiece full_match;

    // re.match: anchored at start
    if (!re.Match(input, 0, str_s->len, re2::RE2::ANCHOR_START, &full_match, ngroups + 1)) {
        delete[] groups;
        return ItemNull;
    }
    // re-run to capture sub-groups
    re2::StringPiece* all_groups = new re2::StringPiece[ngroups + 1];
    re.Match(input, 0, str_s->len, re2::RE2::ANCHOR_START, all_groups, ngroups + 1);

    char full_buf[4096];
    int flen = (int)all_groups[0].size();
    if (flen >= (int)sizeof(full_buf)) flen = (int)sizeof(full_buf) - 1;
    memcpy(full_buf, all_groups[0].data(), flen);
    full_buf[flen] = '\0';

    int start = (int)(all_groups[0].data() - str_s->chars);
    int end = start + flen;

    Item m = make_match_object(str_s->chars, start, end, full_buf,
                               &re, input, all_groups + 1, ngroups);
    attach_match_methods(m);
    delete[] all_groups;
    delete[] groups;
    return m;
}

// re.search(pattern, string)
static Item py_re_search(Item pattern, Item string) {
    String* pat_s = it2s(pattern);
    String* str_s = it2s(string);
    if (!pat_s || !str_s) return ItemNull;

    re2::RE2 re(re2::StringPiece(pat_s->chars, pat_s->len));
    if (!re.ok()) return ItemNull;

    int ngroups = re.NumberOfCapturingGroups();
    re2::StringPiece input(str_s->chars, str_s->len);
    re2::StringPiece* all_groups = new re2::StringPiece[ngroups + 1];

    // re.search: unanchored
    if (!re.Match(input, 0, str_s->len, re2::RE2::UNANCHORED, all_groups, ngroups + 1)) {
        delete[] all_groups;
        return ItemNull;
    }

    char full_buf[4096];
    int flen = (int)all_groups[0].size();
    if (flen >= (int)sizeof(full_buf)) flen = (int)sizeof(full_buf) - 1;
    memcpy(full_buf, all_groups[0].data(), flen);
    full_buf[flen] = '\0';

    int start = (int)(all_groups[0].data() - str_s->chars);
    int end = start + flen;

    Item m = make_match_object(str_s->chars, start, end, full_buf,
                               &re, input, all_groups + 1, ngroups);
    attach_match_methods(m);
    delete[] all_groups;
    return m;
}

// re.findall(pattern, string)
static Item py_re_findall(Item pattern, Item string) {
    String* pat_s = it2s(pattern);
    String* str_s = it2s(string);
    if (!pat_s || !str_s) return py_list_new(0);

    re2::RE2 re(re2::StringPiece(pat_s->chars, pat_s->len));
    if (!re.ok()) return py_list_new(0);

    int ngroups = re.NumberOfCapturingGroups();
    re2::StringPiece input(str_s->chars, str_s->len);
    Item result = py_list_new(0);

    size_t pos = 0;
    while (pos <= (size_t)str_s->len) {
        re2::StringPiece* groups = new re2::StringPiece[ngroups + 1];
        if (!re.Match(input, pos, str_s->len, re2::RE2::UNANCHORED, groups, ngroups + 1)) {
            delete[] groups;
            break;
        }

        if (ngroups == 0) {
            // no capture groups — return full match strings
            char buf[4096];
            int mlen = (int)groups[0].size();
            if (mlen >= (int)sizeof(buf)) mlen = (int)sizeof(buf) - 1;
            memcpy(buf, groups[0].data(), mlen);
            buf[mlen] = '\0';
            py_list_append(result, mk_str(buf));
        } else if (ngroups == 1) {
            // single capture group — return strings
            char buf[4096];
            int mlen = (int)groups[1].size();
            if (mlen >= (int)sizeof(buf)) mlen = (int)sizeof(buf) - 1;
            memcpy(buf, groups[1].data(), mlen);
            buf[mlen] = '\0';
            py_list_append(result, mk_str(buf));
        } else {
            // multiple capture groups — return tuples
            Item tup = py_tuple_new(ngroups);
            for (int g = 1; g <= ngroups; g++) {
                char buf[4096];
                int mlen = (int)groups[g].size();
                if (mlen >= (int)sizeof(buf)) mlen = (int)sizeof(buf) - 1;
                memcpy(buf, groups[g].data(), mlen);
                buf[mlen] = '\0';
                py_tuple_set(tup, g - 1, mk_str(buf));
            }
            py_list_append(result, tup);
        }

        size_t match_end = (size_t)(groups[0].data() - str_s->chars) + groups[0].size();
        if (match_end == pos) match_end++; // prevent infinite loop on zero-width match
        pos = match_end;
        delete[] groups;
    }

    return result;
}

// re.sub(pattern, repl, string, count=0)
static Item py_re_sub(Item pattern, Item repl, Item string_item, Item count_item) {
    String* pat_s = it2s(pattern);
    String* repl_s = it2s(repl);
    String* str_s = it2s(string_item);
    if (!pat_s || !repl_s || !str_s) return string_item;

    int max_count = 0;
    if (get_type_id(count_item) == LMD_TYPE_INT) max_count = it2i(count_item);

    re2::RE2 re(re2::StringPiece(pat_s->chars, pat_s->len));
    if (!re.ok()) return string_item;

    StrBuf* sb = strbuf_new();
    re2::StringPiece input(str_s->chars, str_s->len);
    size_t pos = 0;
    int replacements = 0;

    while (pos <= (size_t)str_s->len) {
        if (max_count > 0 && replacements >= max_count) break;
        re2::StringPiece match;
        if (!re.Match(input, pos, str_s->len, re2::RE2::UNANCHORED, &match, 1)) {
            break;
        }
        // append text before match
        size_t match_start = (size_t)(match.data() - str_s->chars);
        if (match_start > pos) {
            strbuf_append_str_n(sb, str_s->chars + pos, (size_t)(match_start - pos));
        }
        // append replacement
        strbuf_append_str_n(sb, repl_s->chars, repl_s->len);
        replacements++;

        size_t match_end = match_start + match.size();
        if (match_end == pos) match_end++; // prevent infinite loop
        pos = match_end;
    }
    // append remainder
    if (pos <= (size_t)str_s->len) {
        strbuf_append_str(sb, str_s->chars + pos);
    }
    Item result = mk_str(sb->str ? sb->str : "");
    strbuf_free(sb);
    return result;
}

// re.split(pattern, string)
static Item py_re_split(Item pattern, Item string) {
    String* pat_s = it2s(pattern);
    String* str_s = it2s(string);
    if (!pat_s || !str_s) return py_list_new(0);

    re2::RE2 re(re2::StringPiece(pat_s->chars, pat_s->len));
    if (!re.ok()) return py_list_new(0);

    Item result = py_list_new(0);
    re2::StringPiece input(str_s->chars, str_s->len);
    size_t pos = 0;

    while (pos <= (size_t)str_s->len) {
        re2::StringPiece match;
        if (!re.Match(input, pos, str_s->len, re2::RE2::UNANCHORED, &match, 1)) {
            // no more matches — add remainder
            py_list_append(result, mk_str(str_s->chars + pos));
            break;
        }
        // add text before match
        size_t match_start = (size_t)(match.data() - str_s->chars);
        int seg_len = (int)(match_start - pos);
        char buf[4096];
        if (seg_len >= (int)sizeof(buf)) seg_len = (int)sizeof(buf) - 1;
        memcpy(buf, str_s->chars + pos, seg_len);
        buf[seg_len] = '\0';
        py_list_append(result, mk_str(buf));

        size_t match_end = match_start + match.size();
        if (match_end == pos) match_end++;
        pos = match_end;
    }

    return result;
}

extern "C" Item py_stdlib_re_init(void) {
    Item mod = py_dict_new();
    mod_set_func(mod, "match",   (void*)py_re_match, 2);
    mod_set_func(mod, "search",  (void*)py_re_search, 2);
    mod_set_func(mod, "findall", (void*)py_re_findall, 2);
    mod_set_func(mod, "sub",     (void*)py_re_sub, 4);
    mod_set_func(mod, "split",   (void*)py_re_split, 2);
    return mod;
}

// =========================================================================
// JSON MODULE — wraps Lambda's parse/format functions
// =========================================================================

// json.loads(s)
static Item py_json_loads(Item s) {
    String* str = it2s(s);
    if (!str || !str->chars) return ItemNull;
    // use Lambda's parse function with "json" format specifier
    Item json_sym = mk_str("json");
    return fn_parse2_mir(s, json_sym);
}

// json.dumps(obj, indent=None)
static Item py_json_dumps(Item obj, Item indent) {
    if (!py_input) return mk_str("null");
    String* result = format_json(py_input->pool, obj);
    if (!result) return mk_str("null");
    return (Item){.item = s2it(result)};
}

extern "C" Item py_stdlib_json_init(void) {
    Item mod = py_dict_new();
    mod_set_func(mod, "loads", (void*)py_json_loads, 1);
    mod_set_func(mod, "dumps", (void*)py_json_dumps, 2);
    return mod;
}

// =========================================================================
// TIME MODULE — wraps Lambda's pn_clock and POSIX time functions
// =========================================================================

// time.time() — Unix timestamp as float
static Item py_time_time(void) {
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    double t = (double)ts.tv_sec + (double)ts.tv_nsec / 1e9;
    return mk_float(t);
}

// time.monotonic() — monotonic clock as float
static Item py_time_monotonic(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    double t = (double)ts.tv_sec + (double)ts.tv_nsec / 1e9;
    return mk_float(t);
}

// time.sleep(secs)
static Item py_time_sleep(Item secs) {
    double s = 0;
    TypeId t = get_type_id(secs);
    if (t == LMD_TYPE_FLOAT) s = it2d(secs);
    else if (t == LMD_TYPE_INT) s = (double)it2i(secs);
    if (s > 0) {
        usleep((useconds_t)(s * 1e6));
    }
    return ItemNull;
}

// time.perf_counter() — reuses Lambda's pn_clock (high-resolution timer)
static Item py_time_perf_counter(void) {
    return mk_float(pn_clock());
}

// time.perf_counter_ns() — monotonic nanoseconds as integer
static Item py_time_perf_counter_ns(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    int64_t ns = (int64_t)ts.tv_sec * 1000000000LL + (int64_t)ts.tv_nsec;
    return mk_int(ns);
}

// time.time_ns() — Unix epoch nanoseconds as integer
static Item py_time_time_ns(void) {
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    int64_t ns = (int64_t)ts.tv_sec * 1000000000LL + (int64_t)ts.tv_nsec;
    return mk_int(ns);
}

// time.monotonic_ns() — monotonic nanoseconds as integer
static Item py_time_monotonic_ns(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    int64_t ns = (int64_t)ts.tv_sec * 1000000000LL + (int64_t)ts.tv_nsec;
    return mk_int(ns);
}

extern "C" Item py_stdlib_time_init(void) {
    Item mod = py_dict_new();
    mod_set_func(mod, "time",            (void*)py_time_time, 0);
    mod_set_func(mod, "time_ns",         (void*)py_time_time_ns, 0);
    mod_set_func(mod, "monotonic",       (void*)py_time_monotonic, 0);
    mod_set_func(mod, "monotonic_ns",    (void*)py_time_monotonic_ns, 0);
    mod_set_func(mod, "sleep",           (void*)py_time_sleep, 1);
    mod_set_func(mod, "perf_counter",    (void*)py_time_perf_counter, 0);
    mod_set_func(mod, "perf_counter_ns", (void*)py_time_perf_counter_ns, 0);
    return mod;
}

// =========================================================================
// RANDOM MODULE — uses Lambda's SplitMix64 PRNG via seeded state
// =========================================================================

static uint64_t _py_rand_state = 0;

static uint64_t splitmix64_next(void) {
    _py_rand_state += 0x9E3779B97F4A7C15ULL;
    uint64_t z = _py_rand_state;
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
    return z ^ (z >> 31);
}

// random.seed(x)
static Item py_random_seed(Item x) {
    if (get_type_id(x) == LMD_TYPE_INT) _py_rand_state = (uint64_t)it2i(x);
    else if (get_type_id(x) == LMD_TYPE_INT64) _py_rand_state = (uint64_t)it2l(x);
    else {
        struct timespec ts;
        clock_gettime(CLOCK_REALTIME, &ts);
        _py_rand_state = (uint64_t)ts.tv_nsec ^ (uint64_t)ts.tv_sec;
    }
    return ItemNull;
}

// random.random() -> float in [0, 1)
static Item py_random_random(void) {
    if (_py_rand_state == 0) {
        struct timespec ts;
        clock_gettime(CLOCK_REALTIME, &ts);
        _py_rand_state = (uint64_t)ts.tv_nsec ^ (uint64_t)ts.tv_sec;
    }
    uint64_t r = splitmix64_next();
    double d = (double)(r >> 11) / (double)(1ULL << 53);
    return mk_float(d);
}

// random.randint(a, b) -> int in [a, b]
static Item py_random_randint(Item a, Item b) {
    int64_t lo = (get_type_id(a) == LMD_TYPE_INT) ? it2i(a) : it2l(a);
    int64_t hi = (get_type_id(b) == LMD_TYPE_INT) ? it2i(b) : it2l(b);
    if (lo > hi) { int64_t tmp = lo; lo = hi; hi = tmp; }
    if (_py_rand_state == 0) {
        struct timespec ts;
        clock_gettime(CLOCK_REALTIME, &ts);
        _py_rand_state = (uint64_t)ts.tv_nsec ^ (uint64_t)ts.tv_sec;
    }
    uint64_t range = (uint64_t)(hi - lo + 1);
    uint64_t r = splitmix64_next();
    return mk_int(lo + (int64_t)(r % range));
}

// random.choice(seq) -> random element
static Item py_random_choice(Item seq) {
    int64_t len = py_list_length(seq);
    if (len <= 0) return ItemNull;
    if (_py_rand_state == 0) {
        struct timespec ts;
        clock_gettime(CLOCK_REALTIME, &ts);
        _py_rand_state = (uint64_t)ts.tv_nsec ^ (uint64_t)ts.tv_sec;
    }
    uint64_t r = splitmix64_next();
    int64_t idx = (int64_t)(r % (uint64_t)len);
    return py_list_get(seq, mk_int(idx));
}

// random.shuffle(lst) -> shuffle in-place, returns None
static Item py_random_shuffle(Item lst) {
    int64_t len = py_list_length(lst);
    if (len <= 1) return ItemNull;
    if (_py_rand_state == 0) {
        struct timespec ts;
        clock_gettime(CLOCK_REALTIME, &ts);
        _py_rand_state = (uint64_t)ts.tv_nsec ^ (uint64_t)ts.tv_sec;
    }
    // Fisher-Yates shuffle
    for (int64_t i = len - 1; i > 0; i--) {
        uint64_t r = splitmix64_next();
        int64_t j = (int64_t)(r % (uint64_t)(i + 1));
        Item a = py_list_get(lst, mk_int(i));
        Item b = py_list_get(lst, mk_int(j));
        py_list_set(lst, mk_int(i), b);
        py_list_set(lst, mk_int(j), a);
    }
    return ItemNull;
}

extern "C" Item py_stdlib_random_init(void) {
    Item mod = py_dict_new();
    mod_set_func(mod, "random",  (void*)py_random_random, 0);
    mod_set_func(mod, "randint", (void*)py_random_randint, 2);
    mod_set_func(mod, "choice",  (void*)py_random_choice, 1);
    mod_set_func(mod, "shuffle", (void*)py_random_shuffle, 1);
    mod_set_func(mod, "seed",    (void*)py_random_seed, 1);
    return mod;
}

// =========================================================================
// FUNCTOOLS MODULE — reuses Lambda's reduce, adds partial and lru_cache
// =========================================================================

// functools.reduce(func, iterable, initializer=None)
// Wraps Lambda's fn_reduce which takes (collection, function)
static Item py_functools_reduce(Item func, Item iterable, Item initializer) {
    if (initializer.item != ItemNull.item) {
        // prepend initializer to the iterable
        Item lst = py_list_new(0);
        py_list_append(lst, initializer);
        // iterate over the original iterable and append each element
        int64_t len = py_list_length(iterable);
        for (int64_t i = 0; i < len; i++) {
            py_list_append(lst, py_list_get(iterable, mk_int(i)));
        }
        return fn_reduce(lst, func);
    }
    // Lambda's reduce takes (collection, func) — reversed arg order
    return fn_reduce(iterable, func);
}

// functools.partial(func, *args) — up to 5 pre-filled args
// Returns a dict with __is_partial__, __func__, __args__
static Item py_functools_partial(Item func, Item a0, Item a1, Item a2, Item a3, Item a4) {
    Item partial = py_dict_new();
    mod_set(partial, "__is_partial__", (Item){.item = b2it(true)});
    mod_set(partial, "__func__", func);
    Item args = py_list_new(0);
    Item pargs[] = {a0, a1, a2, a3, a4};
    for (int i = 0; i < 5; i++) {
        if (pargs[i].item == ItemNull.item) break;
        py_list_append(args, pargs[i]);
    }
    mod_set(partial, "__args__", args);
    return partial;
}

extern "C" Item py_stdlib_functools_init(void) {
    Item mod = py_dict_new();
    mod_set_func(mod, "reduce",  (void*)py_functools_reduce, 3);
    mod_set_func(mod, "partial", (void*)py_functools_partial, 6);
    return mod;
}

// =========================================================================
// COLLECTIONS MODULE — defaultdict, Counter, deque, namedtuple, OrderedDict
// =========================================================================

// collections.OrderedDict is just a regular dict (Python 3.7+ guarantees order)
// We return py_dict_new directly.
static Item py_collections_OrderedDict(void) {
    return py_dict_new();
}

// defaultdict(default_factory) — dict with a fallback
// Stored as a dict with __default_factory__ field
static Item py_collections_defaultdict(Item factory) {
    Item dd = py_dict_new();
    mod_set(dd, "__default_factory__", factory);
    mod_set(dd, "__is_defaultdict__", (Item){.item = b2it(true)});
    return dd;
}

// Counter(iterable) — count occurrences
static Item py_collections_Counter(Item iterable) {
    Item counter = py_dict_new();
    int64_t len = py_list_length(iterable);
    if (len <= 0) {
        // also handle string input
        String* s = it2s(iterable);
        if (s && s->chars) {
            for (size_t i = 0; i < s->len; i++) {
                char ch[2] = {s->chars[i], '\0'};
                Item key = mk_str(ch);
                Item cur = py_dict_get(counter, key);
                int count = 0;
                if (cur.item != ItemNull.item && get_type_id(cur) == LMD_TYPE_INT) {
                    count = it2i(cur);
                }
                py_dict_set(counter, key, mk_int(count + 1));
            }
        }
        return counter;
    }
    for (int64_t i = 0; i < len; i++) {
        Item elem = py_list_get(iterable, mk_int(i));
        Item cur = py_dict_get(counter, elem);
        int count = 0;
        if (cur.item != ItemNull.item && get_type_id(cur) == LMD_TYPE_INT) {
            count = it2i(cur);
        }
        py_dict_set(counter, elem, mk_int(count + 1));
    }
    return counter;
}

// deque(iterable) — backed by a list (O(1) append/pop right, O(n) left)
static Item py_collections_deque(Item iterable) {
    // just copy into a list — deque acts as a list with appendleft/popleft
    int64_t len = py_list_length(iterable);
    Item dq = py_list_new(0);
    for (int64_t i = 0; i < len; i++) {
        py_list_append(dq, py_list_get(iterable, mk_int(i)));
    }
    return dq;
}

extern "C" Item py_stdlib_collections_init(void) {
    Item mod = py_dict_new();
    mod_set_func(mod, "OrderedDict", (void*)py_collections_OrderedDict, 0);
    mod_set_func(mod, "defaultdict", (void*)py_collections_defaultdict, 1);
    mod_set_func(mod, "Counter",     (void*)py_collections_Counter, 1);
    mod_set_func(mod, "deque",       (void*)py_collections_deque, 1);
    return mod;
}

// =========================================================================
// IO MODULE — minimal StringIO support
// =========================================================================

// io.StringIO() — returns a list-backed buffer
static Item py_io_StringIO(void) {
    Item sio = py_dict_new();
    mod_set(sio, "__buffer__", py_list_new(0));
    mod_set(sio, "__pos__", mk_int(0));
    return sio;
}

extern "C" Item py_stdlib_io_init(void) {
    Item mod = py_dict_new();
    mod_set_func(mod, "StringIO", (void*)py_io_StringIO, 0);
    return mod;
}

// =========================================================================
// COPY MODULE — shallow and deep copy
// =========================================================================

// copy.copy(obj) — shallow copy
static Item py_copy_copy(Item obj) {
    TypeId t = get_type_id(obj);
    if (t == LMD_TYPE_ARRAY) {
        int64_t len = py_list_length(obj);
        Item result = py_list_new(0);
        for (int64_t i = 0; i < len; i++) {
            py_list_append(result, py_list_get(obj, mk_int(i)));
        }
        return result;
    }
    if (t == LMD_TYPE_MAP || t == LMD_TYPE_OBJECT) {
        Item keys = js_object_keys(obj);
        int64_t klen = js_array_length(keys);
        Item result = py_dict_new();
        for (int64_t i = 0; i < klen; i++) {
            Item key = js_array_get_int(keys, i);
            Item val = js_property_get(obj, key);
            py_dict_set(result, key, val);
        }
        return result;
    }
    return obj; // immutable types return themselves
}

// copy.deepcopy(obj) — recursive deep copy
static Item py_copy_deepcopy(Item obj) {
    TypeId t = get_type_id(obj);
    if (t == LMD_TYPE_ARRAY) {
        int64_t len = py_list_length(obj);
        Item result = py_list_new(0);
        for (int64_t i = 0; i < len; i++) {
            py_list_append(result, py_copy_deepcopy(py_list_get(obj, mk_int(i))));
        }
        return result;
    }
    if (t == LMD_TYPE_MAP || t == LMD_TYPE_OBJECT) {
        Item keys = js_object_keys(obj);
        int64_t klen = js_array_length(keys);
        Item result = py_dict_new();
        for (int64_t i = 0; i < klen; i++) {
            Item key = js_array_get_int(keys, i);
            Item val = js_property_get(obj, key);
            py_dict_set(result, key, py_copy_deepcopy(val));
        }
        return result;
    }
    return obj;
}

extern "C" Item py_stdlib_copy_init(void) {
    Item mod = py_dict_new();
    mod_set_func(mod, "copy",     (void*)py_copy_copy, 1);
    mod_set_func(mod, "deepcopy", (void*)py_copy_deepcopy, 1);
    return mod;
}

// =========================================================================
// ARRAY MODULE — typed array stub backed by Lambda Array
// =========================================================================

// array.array(typecode, initializer) — returns a Lambda list
// Type codes are accepted but not enforced; storage uses generic Items.
static Item py_array_array_new(Item typecode, Item initializer) {
    if (get_type_id(initializer) == LMD_TYPE_ARRAY) {
        return initializer;
    }
    return py_list_new(0);
}

extern "C" Item py_stdlib_array_init(void) {
    Item mod = py_dict_new();
    mod_set_func(mod, "array", (void*)py_array_array_new, 2);
    return mod;
}

// =========================================================================
// ABC MODULE — abstract base class stubs
// =========================================================================

// abstractmethod decorator — passthrough (marks method as abstract in CPython;
// for benchmark compatibility we just return the function unmodified)
static Item py_abc_abstractmethod(Item func) {
    return func;
}

extern "C" Item py_stdlib_abc_init(void) {
    Item mod = py_dict_new();
    mod_set_func(mod, "abstractmethod", (void*)py_abc_abstractmethod, 1);
    // ABC base class — an empty class that can be used as a base
    Item bases = py_list_new(0);
    Item methods = py_dict_new();
    Item abc_class = py_class_new(mk_str("ABC"), bases, methods);
    mod_set(mod, "ABC", abc_class);
    return mod;
}

// =========================================================================
// ENUM MODULE — minimal Enum class stub
// =========================================================================

extern "C" Item py_stdlib_enum_init(void) {
    Item mod = py_dict_new();
    // Enum — an empty base class; subclass attributes become enum values.
    // In LambdaPy, class body assignments already set class-level attributes,
    // so ClassName.MEMBER naturally resolves without special metaclass logic.
    Item bases = py_list_new(0);
    Item methods = py_dict_new();
    Item enum_class = py_class_new(mk_str("Enum"), bases, methods);
    mod_set(mod, "Enum", enum_class);
    mod_set(mod, "IntEnum", enum_class);
    return mod;
}

// =========================================================================
// BUILTIN MODULE LOOKUP TABLE
// =========================================================================

typedef struct {
    const char* name;
    PyBuiltinModuleInitFn init_fn;
} BuiltinModuleEntry;

static BuiltinModuleEntry builtin_modules[] = {
    {"asyncio",     py_stdlib_asyncio_init},
    {"math",        py_stdlib_math_init},
    {"os",          py_stdlib_os_init},
    {"os.path",     py_stdlib_os_path_init},
    {"sys",         py_stdlib_sys_init},
    {"re",          py_stdlib_re_init},
    {"json",        py_stdlib_json_init},
    {"time",        py_stdlib_time_init},
    {"random",      py_stdlib_random_init},
    {"functools",   py_stdlib_functools_init},
    {"collections", py_stdlib_collections_init},
    {"io",          py_stdlib_io_init},
    {"copy",        py_stdlib_copy_init},
    {"array",       py_stdlib_array_init},
    {"abc",         py_stdlib_abc_init},
    {"enum",        py_stdlib_enum_init},
    {NULL, NULL}
};

extern "C" PyBuiltinModuleInitFn py_stdlib_find_builtin(const char* name, int len) {
    for (int i = 0; builtin_modules[i].name != NULL; i++) {
        if ((int)strlen(builtin_modules[i].name) == len &&
            strncmp(builtin_modules[i].name, name, len) == 0) {
            return builtin_modules[i].init_fn;
        }
    }
    return NULL;
}
