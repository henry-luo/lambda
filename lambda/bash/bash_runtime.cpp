/**
 * Bash Runtime Functions for Lambda
 *
 * Implements Bash semantics on top of Lambda's type system.
 * All functions are callable from MIR JIT compiled code.
 *
 * Core Bash semantics:
 * - All variables are strings by default
 * - Integers are coerced from strings in arithmetic contexts  
 * - Exit codes: 0 = success (truthy), non-zero = failure (falsy)
 * - Unset variables expand to empty string
 */
#include "bash_runtime.h"
#include "bash_ast.hpp"
#include "../lambda-data.hpp"
#include "../transpiler.hpp"
#include "../../lib/log.h"
#include "../../lib/hashmap.h"
#include "../../lib/strbuf.h"
#include <cstring>
#include <cstdlib>
#include <cstdio>
#include <cmath>
#include <cctype>
#include <regex.h>
#include <fnmatch.h>
#include <signal.h>

// ============================================================================
// Runtime state
// ============================================================================

static int bash_last_exit_code = 0;

// Loop control: 0=none, 1=break, 2=continue
static int bash_loop_control = 0;
static int bash_loop_control_depth = 1;

// Shell option flags (set -e, set -u, set -x, set -o pipefail)
static bool bash_opt_errexit = false;   // -e
static bool bash_opt_nounset = false;   // -u
static bool bash_opt_xtrace = false;    // -x
static bool bash_opt_pipefail = false;  // pipefail

// Trap handler table — indices:
//   0=EXIT 1=ERR 2=DEBUG 3=HUP(SIGHUP) 4=INT(SIGINT) 5=QUIT(SIGQUIT) 6=TERM(SIGTERM)
#define BASH_TRAP_IDX_EXIT  0
#define BASH_TRAP_IDX_ERR   1
#define BASH_TRAP_IDX_DEBUG 2
#define BASH_TRAP_IDX_HUP   3
#define BASH_TRAP_IDX_INT   4
#define BASH_TRAP_IDX_QUIT  5
#define BASH_TRAP_IDX_TERM  6
#define BASH_TRAP_NUM       7

static char* bash_trap_handlers[BASH_TRAP_NUM];           // NULL=default, ""=ignore, else=code
static volatile sig_atomic_t bash_trap_fired[BASH_TRAP_NUM]; // set by OS signal handler

static void bash_os_signal_handler(int signum) {
    switch (signum) {
    case SIGHUP:  bash_trap_fired[BASH_TRAP_IDX_HUP]  = 1; break;
    case SIGINT:  bash_trap_fired[BASH_TRAP_IDX_INT]  = 1; break;
    case SIGQUIT: bash_trap_fired[BASH_TRAP_IDX_QUIT] = 1; break;
    case SIGTERM: bash_trap_fired[BASH_TRAP_IDX_TERM] = 1; break;
    default: break;
    }
}

// Runtime function registry (for functions defined in sourced files)
static struct hashmap* bash_rt_func_table = NULL;

// Variable table (runtime)
static struct hashmap* bash_var_table = NULL;

// Subshell scope stack
#define BASH_SUBSHELL_STACK_MAX 32
static struct hashmap* bash_subshell_stack[BASH_SUBSHELL_STACK_MAX];
static int bash_subshell_depth = 0;

// Function dynamic scope stack (Option A: runtime scope stack)
// Each frame is a hashmap of BashRtVar holding variables declared `local`
// in the currently-executing function call. Lookup walks top-to-bottom
// then falls through to the global bash_var_table (dynamic scoping).
#define BASH_FUNC_SCOPE_STACK_MAX 256
static struct hashmap* bash_func_scope_stack[BASH_FUNC_SCOPE_STACK_MAX];
static int bash_func_scope_depth = 0;

// POSIX-compatible mode (set by --posix CLI flag)
static bool bash_posix_mode = false;

// helper to grow a list's capacity
static void bash_grow_list(List* list) {
    int new_cap = list->capacity ? list->capacity * 2 : 8;
    Item* new_items = (Item*)heap_calloc(sizeof(Item) * new_cap, LMD_TYPE_RAW_POINTER);
    if (list->items && list->length > 0) {
        memcpy(new_items, list->items, sizeof(Item) * list->length);
    }
    list->items = new_items;
    list->capacity = new_cap;
}

// ============================================================================
// Type conversion (Bash string-first semantics)
// ============================================================================

extern "C" Item bash_to_int(Item value) {
    TypeId type = get_type_id(value);
    switch (type) {
    case LMD_TYPE_NULL:
        return (Item){.item = i2it(0)};
    case LMD_TYPE_BOOL:
        return (Item){.item = i2it(it2b(value) ? 1 : 0)};
    case LMD_TYPE_INT:
        return value;
    case LMD_TYPE_FLOAT: {
        double d = it2d(value);
        return (Item){.item = i2it((int64_t)d)};
    }
    case LMD_TYPE_STRING: {
        String* str = it2s(value);
        if (!str || str->len == 0) return (Item){.item = i2it(0)};
        char* endptr;
        long long v = strtoll(str->chars, &endptr, 10);
        if (endptr == str->chars) return (Item){.item = i2it(0)};
        return (Item){.item = i2it((int64_t)v)};
    }
    default:
        return (Item){.item = i2it(0)};
    }
}

extern "C" Item bash_to_string(Item value) {
    TypeId type = get_type_id(value);
    switch (type) {
    case LMD_TYPE_NULL:
        return (Item){.item = s2it(heap_create_name("", 0))};
    case LMD_TYPE_BOOL:
        // Bash booleans map to exit codes as strings
        return (Item){.item = s2it(heap_create_name(it2b(value) ? "0" : "1"))};
    case LMD_TYPE_INT: {
        char buffer[32];
        snprintf(buffer, sizeof(buffer), "%lld", (long long)it2i(value));
        return (Item){.item = s2it(heap_create_name(buffer))};
    }
    case LMD_TYPE_FLOAT: {
        double d = it2d(value);
        char buffer[64];
        snprintf(buffer, sizeof(buffer), "%g", d);
        return (Item){.item = s2it(heap_create_name(buffer))};
    }
    case LMD_TYPE_STRING:
        return value;
    case LMD_TYPE_ARRAY: {
        // array → space-separated elements
        List* list = it2list(value);
        if (!list || list->length == 0) return (Item){.item = s2it(heap_create_name("", 0))};
        StrBuf* sb = strbuf_new();
        for (int i = 0; i < list->length; i++) {
            if (i > 0) strbuf_append_char(sb, ' ');
            Item elem_str = bash_to_string(list->items[i]);
            String* s = it2s(elem_str);
            if (s && s->len > 0) strbuf_append_str_n(sb, s->chars, s->len);
        }
        String* result = heap_create_name(sb->str, sb->length);
        strbuf_free(sb);
        return (Item){.item = s2it(result)};
    }
    default: {
        return (Item){.item = s2it(heap_create_name("", 0))};
    }
    }
}

extern "C" bool bash_is_truthy(Item value) {
    // Bash semantics: empty string / unset = falsy, non-empty = truthy
    // Exit code 0 = truthy (success), non-zero = falsy (failure)
    TypeId type = get_type_id(value);
    switch (type) {
    case LMD_TYPE_NULL:
        return false;
    case LMD_TYPE_BOOL:
        return it2b(value);
    case LMD_TYPE_INT:
        return it2i(value) == 0;  // exit code 0 = success = truthy
    case LMD_TYPE_STRING: {
        String* str = it2s(value);
        return str && str->len > 0;
    }
    default:
        return false;
    }
}

extern "C" int bash_exit_code(Item value) {
    TypeId type = get_type_id(value);
    switch (type) {
    case LMD_TYPE_NULL:
        return 0;
    case LMD_TYPE_BOOL:
        return it2b(value) ? 0 : 1;
    case LMD_TYPE_INT:
        return (int)(it2i(value) & 0xFF);  // exit codes are 0-255
    case LMD_TYPE_STRING: {
        String* str = it2s(value);
        return (str && str->len > 0) ? 0 : 1;
    }
    default:
        return 0;
    }
}

extern "C" Item bash_from_exit_code(int code) {
    return (Item){.item = i2it(code)};
}

// ============================================================================
// Internal: coerce Item to int64 for arithmetic
// ============================================================================

static int64_t bash_coerce_int(Item value) {
    TypeId type = get_type_id(value);
    switch (type) {
    case LMD_TYPE_NULL:
        return 0;
    case LMD_TYPE_BOOL:
        return it2b(value) ? 1 : 0;
    case LMD_TYPE_INT:
        return it2i(value);
    case LMD_TYPE_FLOAT:
        return (int64_t)it2d(value);
    case LMD_TYPE_STRING: {
        String* str = it2s(value);
        if (!str || str->len == 0) return 0;
        char* endptr;
        long long v = strtoll(str->chars, &endptr, 10);
        if (endptr == str->chars) return 0;
        return (int64_t)v;
    }
    default:
        return 0;
    }
}

// ============================================================================
// Arithmetic operators (integer arithmetic)
// ============================================================================

extern "C" Item bash_add(Item left, Item right) {
    int64_t a = bash_coerce_int(left);
    int64_t b = bash_coerce_int(right);
    return (Item){.item = i2it(a + b)};
}

extern "C" Item bash_subtract(Item left, Item right) {
    int64_t a = bash_coerce_int(left);
    int64_t b = bash_coerce_int(right);
    return (Item){.item = i2it(a - b)};
}

extern "C" Item bash_multiply(Item left, Item right) {
    int64_t a = bash_coerce_int(left);
    int64_t b = bash_coerce_int(right);
    return (Item){.item = i2it(a * b)};
}

extern "C" Item bash_divide(Item left, Item right) {
    int64_t a = bash_coerce_int(left);
    int64_t b = bash_coerce_int(right);
    if (b == 0) {
        log_error("bash: division by zero");
        return (Item){.item = i2it(0)};
    }
    return (Item){.item = i2it(a / b)};
}

extern "C" Item bash_modulo(Item left, Item right) {
    int64_t a = bash_coerce_int(left);
    int64_t b = bash_coerce_int(right);
    if (b == 0) {
        log_error("bash: division by zero");
        return (Item){.item = i2it(0)};
    }
    return (Item){.item = i2it(a % b)};
}

extern "C" Item bash_power(Item left, Item right) {
    int64_t base = bash_coerce_int(left);
    int64_t exp = bash_coerce_int(right);
    if (exp < 0) {
        // Bash ** with negative exponent yields 0 for integers
        return (Item){.item = i2it(0)};
    }
    int64_t result = 1;
    int64_t b = base;
    int64_t e = exp;
    while (e > 0) {
        if (e & 1) result *= b;
        b *= b;
        e >>= 1;
    }
    return (Item){.item = i2it(result)};
}

extern "C" Item bash_negate(Item operand) {
    int64_t a = bash_coerce_int(operand);
    return (Item){.item = i2it(-a)};
}

// ============================================================================
// Bitwise operators
// ============================================================================

extern "C" Item bash_bit_and(Item left, Item right) {
    return (Item){.item = i2it(bash_coerce_int(left) & bash_coerce_int(right))};
}

extern "C" Item bash_bit_or(Item left, Item right) {
    return (Item){.item = i2it(bash_coerce_int(left) | bash_coerce_int(right))};
}

extern "C" Item bash_bit_xor(Item left, Item right) {
    return (Item){.item = i2it(bash_coerce_int(left) ^ bash_coerce_int(right))};
}

extern "C" Item bash_bit_not(Item operand) {
    return (Item){.item = i2it(~bash_coerce_int(operand))};
}

extern "C" Item bash_lshift(Item left, Item right) {
    return (Item){.item = i2it(bash_coerce_int(left) << bash_coerce_int(right))};
}

extern "C" Item bash_rshift(Item left, Item right) {
    return (Item){.item = i2it(bash_coerce_int(left) >> bash_coerce_int(right))};
}

// ============================================================================
// Arithmetic comparison
// ============================================================================

extern "C" Item bash_arith_eq(Item left, Item right) {
    return (Item){.item = i2it(bash_coerce_int(left) == bash_coerce_int(right) ? 1 : 0)};
}

extern "C" Item bash_arith_ne(Item left, Item right) {
    return (Item){.item = i2it(bash_coerce_int(left) != bash_coerce_int(right) ? 1 : 0)};
}

extern "C" Item bash_arith_lt(Item left, Item right) {
    return (Item){.item = i2it(bash_coerce_int(left) < bash_coerce_int(right) ? 1 : 0)};
}

extern "C" Item bash_arith_le(Item left, Item right) {
    return (Item){.item = i2it(bash_coerce_int(left) <= bash_coerce_int(right) ? 1 : 0)};
}

extern "C" Item bash_arith_gt(Item left, Item right) {
    return (Item){.item = i2it(bash_coerce_int(left) > bash_coerce_int(right) ? 1 : 0)};
}

extern "C" Item bash_arith_ge(Item left, Item right) {
    return (Item){.item = i2it(bash_coerce_int(left) >= bash_coerce_int(right) ? 1 : 0)};
}

// ============================================================================
// Test / conditional operators
// ============================================================================

// Internal: get C string from an Item
static const char* bash_item_cstr(Item value, char* buf, size_t buf_size) {
    TypeId type = get_type_id(value);
    switch (type) {
    case LMD_TYPE_STRING: {
        String* str = it2s(value);
        return str ? str->chars : "";
    }
    case LMD_TYPE_INT:
        snprintf(buf, buf_size, "%lld", (long long)it2i(value));
        return buf;
    case LMD_TYPE_BOOL:
        return it2b(value) ? "1" : "0";
    case LMD_TYPE_NULL:
        return "";
    default:
        return "";
    }
}

// numeric test comparisons: -eq, -ne, -gt, -ge, -lt, -le
extern "C" Item bash_test_eq(Item left, Item right) {
    bool result = (bash_coerce_int(left) == bash_coerce_int(right));
    bash_last_exit_code = result ? 0 : 1;
    return (Item){.item = b2it(result)};
}

extern "C" Item bash_test_ne(Item left, Item right) {
    bool result = (bash_coerce_int(left) != bash_coerce_int(right));
    bash_last_exit_code = result ? 0 : 1;
    return (Item){.item = b2it(result)};
}

extern "C" Item bash_test_gt(Item left, Item right) {
    bool result = (bash_coerce_int(left) > bash_coerce_int(right));
    bash_last_exit_code = result ? 0 : 1;
    return (Item){.item = b2it(result)};
}

extern "C" Item bash_test_ge(Item left, Item right) {
    bool result = (bash_coerce_int(left) >= bash_coerce_int(right));
    bash_last_exit_code = result ? 0 : 1;
    return (Item){.item = b2it(result)};
}

extern "C" Item bash_test_lt(Item left, Item right) {
    bool result = (bash_coerce_int(left) < bash_coerce_int(right));
    bash_last_exit_code = result ? 0 : 1;
    return (Item){.item = b2it(result)};
}

extern "C" Item bash_test_le(Item left, Item right) {
    bool result = (bash_coerce_int(left) <= bash_coerce_int(right));
    bash_last_exit_code = result ? 0 : 1;
    return (Item){.item = b2it(result)};
}

// string test comparisons
extern "C" Item bash_test_str_eq(Item left, Item right) {
    char buf_l[64], buf_r[64];
    const char* l = bash_item_cstr(left, buf_l, sizeof(buf_l));
    const char* r = bash_item_cstr(right, buf_r, sizeof(buf_r));
    // support glob patterns (*, ?, [) for [[ ]] extended tests
    bool has_glob = (strchr(r, '*') || strchr(r, '?') || strchr(r, '['));
    bool result = has_glob ? (fnmatch(r, l, 0) == 0) : (strcmp(l, r) == 0);
    bash_last_exit_code = result ? 0 : 1;
    return (Item){.item = b2it(result)};
}

extern "C" Item bash_test_str_ne(Item left, Item right) {
    char buf_l[64], buf_r[64];
    const char* l = bash_item_cstr(left, buf_l, sizeof(buf_l));
    const char* r = bash_item_cstr(right, buf_r, sizeof(buf_r));
    bool result = (strcmp(l, r) != 0);
    bash_last_exit_code = result ? 0 : 1;
    return (Item){.item = b2it(result)};
}

extern "C" Item bash_test_str_lt(Item left, Item right) {
    char buf_l[64], buf_r[64];
    const char* l = bash_item_cstr(left, buf_l, sizeof(buf_l));
    const char* r = bash_item_cstr(right, buf_r, sizeof(buf_r));
    bool result = (strcmp(l, r) < 0);
    bash_last_exit_code = result ? 0 : 1;
    return (Item){.item = b2it(result)};
}

extern "C" Item bash_test_str_gt(Item left, Item right) {
    char buf_l[64], buf_r[64];
    const char* l = bash_item_cstr(left, buf_l, sizeof(buf_l));
    const char* r = bash_item_cstr(right, buf_r, sizeof(buf_r));
    bool result = (strcmp(l, r) > 0);
    bash_last_exit_code = result ? 0 : 1;
    return (Item){.item = b2it(result)};
}

extern "C" Item bash_test_regex(Item left, Item right) {
    char buf_l[256], buf_r[256];
    const char* text = bash_item_cstr(left, buf_l, sizeof(buf_l));
    const char* pattern = bash_item_cstr(right, buf_r, sizeof(buf_r));
    regex_t regex;
    int ret = regcomp(&regex, pattern, REG_EXTENDED | REG_NOSUB);
    bool result = false;
    if (ret == 0) {
        result = (regexec(&regex, text, 0, NULL, 0) == 0);
        regfree(&regex);
    }
    bash_last_exit_code = result ? 0 : 1;
    return (Item){.item = b2it(result)};
}

extern "C" Item bash_test_glob(Item left, Item right) {
    char buf_l[256], buf_r[256];
    const char* text = bash_item_cstr(left, buf_l, sizeof(buf_l));
    const char* pattern = bash_item_cstr(right, buf_r, sizeof(buf_r));
    bool result = (fnmatch(pattern, text, 0) == 0);
    bash_last_exit_code = result ? 0 : 1;
    return (Item){.item = b2it(result)};
}

// unary string tests
extern "C" Item bash_test_z(Item value) {
    TypeId type = get_type_id(value);
    bool result;
    if (type == LMD_TYPE_NULL) { result = true; }
    else if (type == LMD_TYPE_STRING) {
        String* str = it2s(value);
        result = (!str || str->len == 0);
    } else { result = false; }
    bash_last_exit_code = result ? 0 : 1;
    return (Item){.item = b2it(result)};
}

extern "C" Item bash_test_n(Item value) {
    TypeId type = get_type_id(value);
    bool result;
    if (type == LMD_TYPE_NULL) { result = false; }
    else if (type == LMD_TYPE_STRING) {
        String* str = it2s(value);
        result = (str && str->len > 0);
    } else { result = true; }
    bash_last_exit_code = result ? 0 : 1;
    return (Item){.item = b2it(result)};
}

// file test operators
#include "../../lib/file.h"
#include <unistd.h>  // for pipe, read, write, close, getuid, fork

static const char* bash_item_to_cstr(Item value) {
    Item str = bash_to_string(value);
    String* s = it2s(str);
    if (!s || s->len == 0) return "";
    return s->chars;
}

extern "C" Item bash_test_f(Item value) {
    const char* path = bash_item_to_cstr(value);
    bool result = file_is_file(path);
    bash_last_exit_code = result ? 0 : 1;
    return (Item){.item = b2it(result)};
}

extern "C" Item bash_test_d(Item value) {
    const char* path = bash_item_to_cstr(value);
    bool result = file_is_dir(path);
    bash_last_exit_code = result ? 0 : 1;
    return (Item){.item = b2it(result)};
}

extern "C" Item bash_test_e(Item value) {
    const char* path = bash_item_to_cstr(value);
    bool result = file_exists(path);
    bash_last_exit_code = result ? 0 : 1;
    return (Item){.item = b2it(result)};
}

extern "C" Item bash_test_r(Item value) {
    const char* path = bash_item_to_cstr(value);
    bool result = file_is_readable(path);
    bash_last_exit_code = result ? 0 : 1;
    return (Item){.item = b2it(result)};
}

extern "C" Item bash_test_w(Item value) {
    const char* path = bash_item_to_cstr(value);
    bool result = file_is_writable(path);
    bash_last_exit_code = result ? 0 : 1;
    return (Item){.item = b2it(result)};
}

extern "C" Item bash_test_x(Item value) {
    const char* path = bash_item_to_cstr(value);
    bool result = file_is_executable(path);
    bash_last_exit_code = result ? 0 : 1;
    return (Item){.item = b2it(result)};
}

extern "C" Item bash_test_s(Item value) {
    const char* path = bash_item_to_cstr(value);
    bool result = (file_size(path) > 0);
    bash_last_exit_code = result ? 0 : 1;
    return (Item){.item = b2it(result)};
}

extern "C" Item bash_test_l(Item value) {
    const char* path = bash_item_to_cstr(value);
    bool result = file_is_symlink(path);
    bash_last_exit_code = result ? 0 : 1;
    return (Item){.item = b2it(result)};
}

// ============================================================================
// Parameter expansion functions
// ============================================================================

static bool bash_item_is_empty(Item val) {
    TypeId type = get_type_id(val);
    if (type == LMD_TYPE_NULL) return true;
    if (type == LMD_TYPE_STRING) {
        String* s = it2s(val);
        return (!s || s->len == 0);
    }
    return false;
}

// check if variable is truly unset (null), not just empty
static bool bash_item_is_unset(Item val) {
    TypeId type = get_type_id(val);
    if (type == LMD_TYPE_NULL) return true;
    if (type == LMD_TYPE_STRING) {
        String* s = it2s(val);
        return !s;
    }
    return false;
}

static Item bash_make_string(const char* str, size_t len) {
    return (Item){.item = s2it(heap_create_name(str, len))};
}

// ${var:-default} — return default if var is unset or empty
extern "C" Item bash_expand_default(Item val, Item def) {
    return bash_item_is_empty(val) ? def : val;
}

// ${var:=default} — assign default if var is unset or empty
extern "C" Item bash_expand_assign_default(Item var_name, Item val, Item def) {
    if (bash_item_is_empty(val)) {
        bash_set_var(var_name, def);
        return def;
    }
    return val;
}

// ${var:+alt} — return alt if var is set and non-empty
extern "C" Item bash_expand_alt(Item val, Item alt) {
    return bash_item_is_empty(val) ? (Item){.item = s2it(NULL)} : alt;
}

// ${var:?msg} — error if unset or empty
extern "C" Item bash_expand_error(Item val, Item msg) {
    if (bash_item_is_empty(val)) {
        String* m = it2s(bash_to_string(msg));
        log_error("bash: %s", m ? m->chars : "parameter null or not set");
    }
    return val;
}

// --- nocolon variants: only trigger on truly unset, not empty ---

// ${var-default} — return default only if var is unset
extern "C" Item bash_expand_default_nocolon(Item val, Item def) {
    return bash_item_is_unset(val) ? def : val;
}

// ${var=default} — assign default only if var is unset
extern "C" Item bash_expand_assign_default_nocolon(Item var_name, Item val, Item def) {
    if (bash_item_is_unset(val)) {
        bash_set_var(var_name, def);
        return def;
    }
    return val;
}

// ${var+alt} — return alt if var is set (even if empty)
extern "C" Item bash_expand_alt_nocolon(Item val, Item alt) {
    return bash_item_is_unset(val) ? (Item){.item = s2it(NULL)} : alt;
}

// ${var?msg} — error only if var is unset
extern "C" Item bash_expand_error_nocolon(Item val, Item msg) {
    if (bash_item_is_unset(val)) {
        String* m = it2s(bash_to_string(msg));
        log_error("bash: %s", m ? m->chars : "parameter null or not set");
    }
    return val;
}

// ${var#pattern} — remove shortest prefix match
extern "C" Item bash_expand_trim_prefix(Item val, Item pat) {
    char buf_v[512], buf_p[256];
    const char* str = bash_item_cstr(val, buf_v, sizeof(buf_v));
    const char* pattern = bash_item_cstr(pat, buf_p, sizeof(buf_p));
    size_t slen = strlen(str);
    // try shortest prefix
    for (size_t i = 0; i <= slen; i++) {
        char tmp[512];
        memcpy(tmp, str, i);
        tmp[i] = '\0';
        if (fnmatch(pattern, tmp, 0) == 0) {
            return bash_make_string(str + i, slen - i);
        }
    }
    return val;
}

// ${var##pattern} — remove longest prefix match
extern "C" Item bash_expand_trim_prefix_long(Item val, Item pat) {
    char buf_v[512], buf_p[256];
    const char* str = bash_item_cstr(val, buf_v, sizeof(buf_v));
    const char* pattern = bash_item_cstr(pat, buf_p, sizeof(buf_p));
    size_t slen = strlen(str);
    // try longest prefix first
    for (size_t i = slen; i > 0; i--) {
        char tmp[512];
        memcpy(tmp, str, i);
        tmp[i] = '\0';
        if (fnmatch(pattern, tmp, 0) == 0) {
            return bash_make_string(str + i, slen - i);
        }
    }
    return val;
}

// ${var%pattern} — remove shortest suffix match
extern "C" Item bash_expand_trim_suffix(Item val, Item pat) {
    char buf_v[512], buf_p[256];
    const char* str = bash_item_cstr(val, buf_v, sizeof(buf_v));
    const char* pattern = bash_item_cstr(pat, buf_p, sizeof(buf_p));
    size_t slen = strlen(str);
    // try shortest suffix (from end)
    for (size_t i = slen; i > 0; i--) {
        if (fnmatch(pattern, str + i, 0) == 0) {
            return bash_make_string(str, i);
        }
    }
    return val;
}

// ${var%%pattern} — remove longest suffix match
extern "C" Item bash_expand_trim_suffix_long(Item val, Item pat) {
    char buf_v[512], buf_p[256];
    const char* str = bash_item_cstr(val, buf_v, sizeof(buf_v));
    const char* pattern = bash_item_cstr(pat, buf_p, sizeof(buf_p));
    size_t slen = strlen(str);
    // try longest suffix (from start)
    for (size_t i = 0; i <= slen; i++) {
        if (fnmatch(pattern, str + i, 0) == 0) {
            return bash_make_string(str, i);
        }
    }
    return val;
}

// ${var/pattern/replacement} — replace first occurrence
extern "C" Item bash_expand_replace(Item val, Item pat, Item repl) {
    char buf_v[512], buf_p[256], buf_r[256];
    const char* str = bash_item_cstr(val, buf_v, sizeof(buf_v));
    const char* pattern = bash_item_cstr(pat, buf_p, sizeof(buf_p));
    const char* replacement = bash_item_cstr(repl, buf_r, sizeof(buf_r));
    // simple substring match for non-glob patterns
    const char* found = strstr(str, pattern);
    if (!found) return val;
    size_t plen = strlen(pattern);
    size_t rlen = strlen(replacement);
    size_t slen = strlen(str);
    size_t new_len = slen - plen + rlen;
    StrBuf* sb = strbuf_new_cap(new_len + 1);
    size_t prefix = found - str;
    strbuf_append_str_n(sb, str, prefix);
    strbuf_append_str_n(sb, replacement, rlen);
    strbuf_append_str_n(sb, found + plen, slen - prefix - plen);
    Item result = bash_make_string(sb->str, sb->length);
    strbuf_free(sb);
    return result;
}

// ${var//pattern/replacement} — replace all occurrences
extern "C" Item bash_expand_replace_all(Item val, Item pat, Item repl) {
    char buf_v[512], buf_p[256], buf_r[256];
    const char* str = bash_item_cstr(val, buf_v, sizeof(buf_v));
    const char* pattern = bash_item_cstr(pat, buf_p, sizeof(buf_p));
    const char* replacement = bash_item_cstr(repl, buf_r, sizeof(buf_r));
    size_t plen = strlen(pattern);
    if (plen == 0) return val;
    size_t rlen = strlen(replacement);
    StrBuf* sb = strbuf_new_cap(strlen(str) + 64);
    const char* p = str;
    while (*p) {
        const char* found = strstr(p, pattern);
        if (!found) {
            strbuf_append_str(sb, p);
            break;
        }
        strbuf_append_str_n(sb, p, found - p);
        strbuf_append_str_n(sb, replacement, rlen);
        p = found + plen;
    }
    Item result = bash_make_string(sb->str, sb->length);
    strbuf_free(sb);
    return result;
}

// ${var:offset:length} — substring extraction
extern "C" Item bash_expand_substring(Item val, Item offset_item, Item len_item) {
    char buf[512];
    const char* str = bash_item_cstr(val, buf, sizeof(buf));
    size_t slen = strlen(str);
    int64_t offset = bash_coerce_int(offset_item);
    int64_t length = bash_coerce_int(len_item);
    if (offset < 0) offset = (int64_t)slen + offset;
    if (offset < 0) offset = 0;
    if ((size_t)offset >= slen) return bash_make_string("", 0);
    size_t avail = slen - offset;
    if (length < 0) length = (int64_t)avail;
    if ((size_t)length > avail) length = avail;
    return bash_make_string(str + offset, length);
}

// ${var^} — uppercase first char
extern "C" Item bash_expand_upper_first(Item val) {
    char buf[512];
    const char* str = bash_item_cstr(val, buf, sizeof(buf));
    size_t slen = strlen(str);
    if (slen == 0) return val;
    StrBuf* sb = strbuf_new_cap(slen + 1);
    strbuf_append_char(sb, (char)toupper((unsigned char)str[0]));
    if (slen > 1) strbuf_append_str_n(sb, str + 1, slen - 1);
    Item result = bash_make_string(sb->str, sb->length);
    strbuf_free(sb);
    return result;
}

// ${var^^} — uppercase all
extern "C" Item bash_expand_upper_all(Item val) {
    char buf[512];
    const char* str = bash_item_cstr(val, buf, sizeof(buf));
    size_t slen = strlen(str);
    StrBuf* sb = strbuf_new_cap(slen + 1);
    for (size_t i = 0; i < slen; i++) {
        strbuf_append_char(sb, (char)toupper((unsigned char)str[i]));
    }
    Item result = bash_make_string(sb->str, sb->length);
    strbuf_free(sb);
    return result;
}

// ${var,} — lowercase first char
extern "C" Item bash_expand_lower_first(Item val) {
    char buf[512];
    const char* str = bash_item_cstr(val, buf, sizeof(buf));
    size_t slen = strlen(str);
    if (slen == 0) return val;
    StrBuf* sb = strbuf_new_cap(slen + 1);
    strbuf_append_char(sb, (char)tolower((unsigned char)str[0]));
    if (slen > 1) strbuf_append_str_n(sb, str + 1, slen - 1);
    Item result = bash_make_string(sb->str, sb->length);
    strbuf_free(sb);
    return result;
}

// ${var,,} — lowercase all
extern "C" Item bash_expand_lower_all(Item val) {
    char buf[512];
    const char* str = bash_item_cstr(val, buf, sizeof(buf));
    size_t slen = strlen(str);
    StrBuf* sb = strbuf_new_cap(slen + 1);
    for (size_t i = 0; i < slen; i++) {
        strbuf_append_char(sb, (char)tolower((unsigned char)str[i]));
    }
    Item result = bash_make_string(sb->str, sb->length);
    strbuf_free(sb);
    return result;
}

// ============================================================================
// String operations
// ============================================================================

extern "C" Item bash_string_length(Item str) {
    TypeId type = get_type_id(str);
    if (type == LMD_TYPE_STRING) {
        String* s = it2s(str);
        return (Item){.item = i2it(s ? s->len : 0)};
    }
    // for non-strings, convert to string first, then get length
    Item as_str = bash_to_string(str);
    String* s = it2s(as_str);
    return (Item){.item = i2it(s ? s->len : 0)};
}

extern "C" Item bash_string_concat(Item left, Item right) {
    Item l_str = bash_to_string(left);
    Item r_str = bash_to_string(right);
    String* l = it2s(l_str);
    String* r = it2s(r_str);

    if (!l && !r) return (Item){.item = s2it(heap_create_name("", 0))};
    if (!l) return r_str;
    if (!r) return l_str;

    int new_len = l->len + r->len;
    StrBuf* sb = strbuf_new_cap(new_len + 1);
    strbuf_append_str_n(sb, l->chars, l->len);
    strbuf_append_str_n(sb, r->chars, r->len);
    String* result = heap_create_name(sb->str, sb->length);
    strbuf_free(sb);
    return (Item){.item = s2it(result)};
}

// ============================================================================
// Tilde expansion: ~ → $HOME, ~/path → $HOME/path
// ============================================================================

#include <pwd.h>

extern "C" Item bash_expand_tilde(Item word) {
    const char* w = bash_item_to_cstr(word);
    if (!w || w[0] != '~') return word;

    const char* home = NULL;
    const char* suffix = w + 1; // after ~

    if (*suffix == '\0' || *suffix == '/') {
        // ~ or ~/path → use $HOME from bash variable table first, then getenv
        // look HOME up via bash_get_var (forward declared in bash_runtime.h)
        Item home_name = (Item){.item = s2it(heap_create_name("HOME", 4))};
        extern Item bash_get_var(Item name);
        Item home_val = bash_get_var(home_name);
        const char* bash_home = bash_item_to_cstr(home_val);
        if (bash_home && *bash_home) {
            home = bash_home;
        } else {
            home = getenv("HOME");
        }
        if (!home) {
            struct passwd* pw = getpwuid(getuid());
            if (pw) home = pw->pw_dir;
        }
    } else {
        // ~user → lookup user's home directory
        const char* slash = strchr(suffix, '/');
        char username[256];
        int ulen = slash ? (int)(slash - suffix) : (int)strlen(suffix);
        if (ulen >= (int)sizeof(username)) ulen = (int)sizeof(username) - 1;
        memcpy(username, suffix, ulen);
        username[ulen] = '\0';
        struct passwd* pw = getpwnam(username);
        if (pw) {
            home = pw->pw_dir;
            suffix = slash ? slash : suffix + ulen;
        }
    }

    if (!home) return word; // can't expand, return literal

    StrBuf* sb = strbuf_new();
    strbuf_append_str(sb, home);
    if (*suffix) strbuf_append_str(sb, suffix);
    String* result = heap_create_name(sb->str, sb->length);
    strbuf_free(sb);
    return (Item){.item = s2it(result)};
}

// ============================================================================
// Glob expansion: *.txt → array of matching paths
// ============================================================================

#include <glob.h>

extern "C" Item bash_glob_expand(Item pattern) {
    const char* pat = bash_item_to_cstr(pattern);
    if (!pat || !*pat) return pattern;

    glob_t g;
    int flags = GLOB_NOSORT | GLOB_NOCHECK; // NOCHECK: return pattern if no matches
    int ret = glob(pat, flags, NULL, &g);

    if (ret != 0) {
        globfree(&g);
        return pattern; // return pattern literally on error
    }

    if (g.gl_pathc == 1) {
        // single match (or no match with NOCHECK) — return as string
        String* result = heap_create_name(g.gl_pathv[0]);
        globfree(&g);
        return (Item){.item = s2it(result)};
    }

    // multiple matches — build space-separated string for echo, or array for for-in
    // for command args, we need to return them as a space-separated string
    // that the builtin will print. A proper implementation would return an array,
    // but for now echo/printf treat args as individual items.
    // Return space-separated string for simplicity.
    StrBuf* sb = strbuf_new();
    for (size_t i = 0; i < g.gl_pathc; i++) {
        if (i > 0) strbuf_append_char(sb, ' ');
        strbuf_append_str(sb, g.gl_pathv[i]);
    }
    String* result = heap_create_name(sb->str, sb->length);
    strbuf_free(sb);
    globfree(&g);
    return (Item){.item = s2it(result)};
}

// ============================================================================
// Brace expansion: {a,b,c} → "a b c", {1..5} → "1 2 3 4 5"
// ============================================================================

extern "C" Item bash_expand_brace(Item word) {
    const char* w = bash_item_to_cstr(word);
    if (!w || !*w) return word;

    int len = (int)strlen(w);
    // must start with { and end with }
    if (w[0] != '{' || w[len - 1] != '}') return word;

    // extract inside: between { and }
    const char* inside = w + 1;
    int ilen = len - 2;

    // check for range: {start..end} or {start..end..step}
    const char* dotdot = NULL;
    for (int i = 0; i < ilen - 1; i++) {
        if (inside[i] == '.' && inside[i + 1] == '.') {
            dotdot = inside + i;
            break;
        }
    }

    if (dotdot) {
        // range expansion
        char start_buf[64], end_buf[64];
        int start_len = (int)(dotdot - inside);
        int after_dots = (int)(inside + ilen - (dotdot + 2));
        if (start_len <= 0 || start_len >= (int)sizeof(start_buf) ||
            after_dots <= 0 || after_dots >= (int)sizeof(end_buf))
            return word;
        memcpy(start_buf, inside, start_len);
        start_buf[start_len] = '\0';
        memcpy(end_buf, dotdot + 2, after_dots);
        end_buf[after_dots] = '\0';

        // check for step: {start..end..step}
        int step = 1;
        char* step_dot = strstr(end_buf, "..");
        if (step_dot) {
            *step_dot = '\0';
            step = atoi(step_dot + 2);
            if (step == 0) step = 1;
        }

        // numeric range?
        char* endp1 = NULL;
        char* endp2 = NULL;
        long s = strtol(start_buf, &endp1, 10);
        long e = strtol(end_buf, &endp2, 10);

        if (endp1 && *endp1 == '\0' && endp2 && *endp2 == '\0') {
            // numeric range: {1..5} or {10..1}
            // check for zero-padding
            bool zero_pad = (start_buf[0] == '0' && start_len > 1) ||
                            (end_buf[0] == '0' && (int)strlen(end_buf) > 1);
            int pad_width = 0;
            if (zero_pad) {
                pad_width = start_len > (int)strlen(end_buf) ? start_len : (int)strlen(end_buf);
            }

            StrBuf* sb = strbuf_new();
            if (s <= e) {
                if (step < 0) step = -step;
                for (long i = s; i <= e; i += step) {
                    if (sb->length > 0) strbuf_append_char(sb, ' ');
                    char num[32];
                    if (zero_pad)
                        snprintf(num, sizeof(num), "%0*ld", pad_width, i);
                    else
                        snprintf(num, sizeof(num), "%ld", i);
                    strbuf_append_str(sb, num);
                }
            } else {
                if (step < 0) step = -step;
                for (long i = s; i >= e; i -= step) {
                    if (sb->length > 0) strbuf_append_char(sb, ' ');
                    char num[32];
                    if (zero_pad)
                        snprintf(num, sizeof(num), "%0*ld", pad_width, i);
                    else
                        snprintf(num, sizeof(num), "%ld", i);
                    strbuf_append_str(sb, num);
                }
            }
            String* result = heap_create_name(sb->str, sb->length);
            strbuf_free(sb);
            return (Item){.item = s2it(result)};
        }

        // character range: {a..z}
        if (start_len == 1 && (int)strlen(end_buf) == 1) {
            char sc = start_buf[0];
            char ec = end_buf[0];
            StrBuf* sb = strbuf_new();
            if (sc <= ec) {
                for (char c = sc; c <= ec; c += (char)step) {
                    if (sb->length > 0) strbuf_append_char(sb, ' ');
                    strbuf_append_char(sb, c);
                }
            } else {
                for (char c = sc; c >= ec; c -= (char)step) {
                    if (sb->length > 0) strbuf_append_char(sb, ' ');
                    strbuf_append_char(sb, c);
                }
            }
            String* result = heap_create_name(sb->str, sb->length);
            strbuf_free(sb);
            return (Item){.item = s2it(result)};
        }

        return word; // unrecognized range format
    }

    // comma-separated list: {a,b,c}
    // check for commas (not inside nested braces)
    int brace_depth = 0;
    bool has_comma = false;
    for (int i = 0; i < ilen; i++) {
        if (inside[i] == '{') brace_depth++;
        else if (inside[i] == '}') brace_depth--;
        else if (inside[i] == ',' && brace_depth == 0) { has_comma = true; break; }
    }

    if (!has_comma) return word; // no commas, return literal

    StrBuf* sb = strbuf_new();
    brace_depth = 0;
    const char* start = inside;
    for (int i = 0; i <= ilen; i++) {
        bool at_end = (i == ilen);
        if (!at_end) {
            if (inside[i] == '{') { brace_depth++; continue; }
            if (inside[i] == '}') { brace_depth--; continue; }
            if (inside[i] != ',' || brace_depth != 0) continue;
        }
        // emit this element
        if (sb->length > 0) strbuf_append_char(sb, ' ');
        int elem_len = (int)(inside + i - start);
        if (elem_len > 0) strbuf_append_str_n(sb, start, elem_len);
        start = inside + i + 1;
    }
    String* result = heap_create_name(sb->str, sb->length);
    strbuf_free(sb);
    return (Item){.item = s2it(result)};
}

extern "C" Item bash_string_substring(Item str, Item offset, Item length) {
    Item s_str = bash_to_string(str);
    String* s = it2s(s_str);
    if (!s) return (Item){.item = s2it(heap_create_name("", 0))};

    int64_t off = bash_coerce_int(offset);
    int64_t len = bash_coerce_int(length);

    // handle negative offset (from end)
    if (off < 0) off = s->len + off;
    if (off < 0) off = 0;
    if (off >= s->len) return (Item){.item = s2it(heap_create_name("", 0))};

    // if length is 0 or negative, return empty
    if (len <= 0 && get_type_id(length) != LMD_TYPE_NULL) {
        return (Item){.item = s2it(heap_create_name("", 0))};
    }

    // NULL length means "to end"
    if (get_type_id(length) == LMD_TYPE_NULL) {
        len = s->len - off;
    }

    if (off + len > s->len) len = s->len - off;

    String* result = heap_create_name(s->chars + off, (size_t)len);
    return (Item){.item = s2it(result)};
}

// simple glob-style pattern match for ${var#pat} etc.
static bool bash_glob_match(const char* str, const char* pat) {
    while (*pat) {
        if (*pat == '*') {
            pat++;
            if (!*pat) return true;
            while (*str) {
                if (bash_glob_match(str, pat)) return true;
                str++;
            }
            return false;
        } else if (*pat == '?') {
            if (!*str) return false;
            str++;
            pat++;
        } else {
            if (*str != *pat) return false;
            str++;
            pat++;
        }
    }
    return *str == 0;
}

extern "C" Item bash_string_trim_prefix(Item str, Item pattern, bool greedy) {
    String* s = it2s(bash_to_string(str));
    String* p = it2s(bash_to_string(pattern));
    if (!s || !p) return str;

    if (greedy) {
        // ${var##pat}: longest prefix match
        for (int i = s->len; i >= 0; i--) {
            char saved = s->chars[i];
            s->chars[i] = '\0';
            if (bash_glob_match(s->chars, p->chars)) {
                s->chars[i] = saved;
                return (Item){.item = s2it(heap_create_name(s->chars + i, s->len - i))};
            }
            s->chars[i] = saved;
        }
    } else {
        // ${var#pat}: shortest prefix match
        for (int i = 0; i <= (int)s->len; i++) {
            char saved = s->chars[i];
            s->chars[i] = '\0';
            if (bash_glob_match(s->chars, p->chars)) {
                s->chars[i] = saved;
                return (Item){.item = s2it(heap_create_name(s->chars + i, s->len - i))};
            }
            s->chars[i] = saved;
        }
    }
    return str;
}

extern "C" Item bash_string_trim_suffix(Item str, Item pattern, bool greedy) {
    String* s = it2s(bash_to_string(str));
    String* p = it2s(bash_to_string(pattern));
    if (!s || !p) return str;

    if (greedy) {
        // ${var%%pat}: longest suffix match
        for (int i = 0; i <= (int)s->len; i++) {
            if (bash_glob_match(s->chars + i, p->chars)) {
                return (Item){.item = s2it(heap_create_name(s->chars, i))};
            }
        }
    } else {
        // ${var%pat}: shortest suffix match
        for (int i = s->len; i >= 0; i--) {
            if (bash_glob_match(s->chars + i, p->chars)) {
                return (Item){.item = s2it(heap_create_name(s->chars, i))};
            }
        }
    }
    return str;
}

extern "C" Item bash_string_replace(Item str, Item pattern, Item replacement, bool all) {
    String* s = it2s(bash_to_string(str));
    String* p = it2s(bash_to_string(pattern));
    String* r = it2s(bash_to_string(replacement));
    if (!s || !p || p->len == 0) return str;
    if (!r) r = heap_create_name("", 0);

    StrBuf* sb = strbuf_new();
    const char* src = s->chars;
    int src_len = s->len;
    int pat_len = p->len;

    for (int i = 0; i < src_len; ) {
        if (i + pat_len <= src_len && memcmp(src + i, p->chars, pat_len) == 0) {
            strbuf_append_str_n(sb, r->chars, r->len);
            i += pat_len;
            if (!all) {
                strbuf_append_str_n(sb, src + i, src_len - i);
                break;
            }
        } else {
            strbuf_append_char(sb, src[i]);
            i++;
        }
    }

    String* result = heap_create_name(sb->str, sb->length);
    strbuf_free(sb);
    return (Item){.item = s2it(result)};
}

extern "C" Item bash_string_upper(Item str, bool all) {
    String* s = it2s(bash_to_string(str));
    if (!s || s->len == 0) return str;

    StrBuf* sb = strbuf_new_cap(s->len + 1);
    for (int i = 0; i < (int)s->len; i++) {
        char c = s->chars[i];
        if (all || i == 0) {
            if (c >= 'a' && c <= 'z') c -= 32;
        }
        strbuf_append_char(sb, c);
    }
    String* result = heap_create_name(sb->str, sb->length);
    strbuf_free(sb);
    return (Item){.item = s2it(result)};
}

extern "C" Item bash_string_lower(Item str, bool all) {
    String* s = it2s(bash_to_string(str));
    if (!s || s->len == 0) return str;

    StrBuf* sb = strbuf_new_cap(s->len + 1);
    for (int i = 0; i < (int)s->len; i++) {
        char c = s->chars[i];
        if (all || i == 0) {
            if (c >= 'A' && c <= 'Z') c += 32;
        }
        strbuf_append_char(sb, c);
    }
    String* result = heap_create_name(sb->str, sb->length);
    strbuf_free(sb);
    return (Item){.item = s2it(result)};
}

// ============================================================================
// Array operations
// ============================================================================

extern "C" Item bash_int_to_item(int64_t n) {
    return (Item){.item = i2it((int)n)};
}

extern "C" Item bash_array_new(void) {
    List* list = (List*)heap_calloc(sizeof(List), LMD_TYPE_ARRAY);
    list->type_id = LMD_TYPE_ARRAY;
    list->length = 0;
    list->capacity = 8;
    list->items = (Item*)heap_calloc(sizeof(Item) * 8, LMD_TYPE_RAW_POINTER);
    return (Item){.item = (uint64_t)(uintptr_t)list};
}

extern "C" Item bash_array_set(Item arr, Item index, Item value) {
    List* list = it2list(arr);
    if (!list) return arr;
    int64_t idx = bash_coerce_int(index);
    if (idx < 0) return arr;

    // grow if needed
    while (idx >= list->capacity) {
        bash_grow_list(list);
    }
    list->items[idx] = value;
    if (idx >= list->length) list->length = (int)(idx + 1);
    return arr;
}

extern "C" Item bash_array_get(Item arr, Item index) {
    List* list = it2list(arr);
    if (!list) return (Item){.item = s2it(heap_create_name("", 0))};
    int64_t idx = bash_coerce_int(index);
    if (idx < 0 || idx >= list->length) return (Item){.item = s2it(heap_create_name("", 0))};
    return list->items[idx];
}

extern "C" Item bash_array_append(Item arr, Item value) {
    List* list = it2list(arr);
    if (!list) return arr;
    if (list->length >= list->capacity) {
        bash_grow_list(list);
    }
    list->items[list->length++] = value;
    return arr;
}

extern "C" Item bash_array_length(Item arr) {
    List* list = it2list(arr);
    if (!list) return (Item){.item = i2it(0)};
    return (Item){.item = i2it(list->length)};
}

// return raw int64 count (for internal iteration loops)
extern "C" int64_t bash_array_count(Item arr) {
    List* list = it2list(arr);
    if (!list) return 0;
    return list->length;
}

extern "C" Item bash_array_all(Item arr) {
    // return the array as-is (it's already a list)
    return arr;
}

extern "C" Item bash_array_unset(Item arr, Item index) {
    List* list = it2list(arr);
    if (!list) return arr;
    int64_t idx = bash_coerce_int(index);
    if (idx < 0 || idx >= list->length) return arr;
    // shift remaining elements down
    for (int i = (int)idx; i < list->length - 1; i++) {
        list->items[i] = list->items[i + 1];
    }
    list->length--;
    return arr;
}

extern "C" Item bash_array_slice(Item arr, Item offset, Item length) {
    List* list = it2list(arr);
    if (!list) return bash_array_new();

    int64_t off = bash_coerce_int(offset);
    int64_t len = bash_coerce_int(length);
    if (off < 0) off = 0;
    if (off >= list->length) return bash_array_new();
    if (len <= 0 || off + len > list->length) len = list->length - off;

    Item result = bash_array_new();
    List* res_list = it2list(result);
    for (int64_t i = 0; i < len; i++) {
        bash_array_append(result, list->items[off + i]);
    }
    (void)res_list;
    return result;
}

// ============================================================================
// Control flow support
// ============================================================================

extern "C" int bash_get_loop_control(void) {
    return bash_loop_control;
}

extern "C" void bash_set_loop_control(int control, int depth) {
    bash_loop_control = control;
    bash_loop_control_depth = depth;
}

extern "C" void bash_clear_loop_control(void) {
    bash_loop_control = 0;
    bash_loop_control_depth = 1;
}

// ============================================================================
// Exit code management
// ============================================================================

extern "C" Item bash_get_exit_code(void) {
    return (Item){.item = i2it(bash_last_exit_code)};
}

extern "C" void bash_set_exit_code(int code) {
    bash_last_exit_code = code & 0xFF;
}

// Save exit code as plain Item int for later restoration (avoids Item vs int confusion)
extern "C" Item bash_save_exit_code(void) {
    return (Item){.item = i2it(bash_last_exit_code)};
}

// Restore exit code from a saved Item int (counterpart to bash_save_exit_code)
extern "C" void bash_restore_exit_code(Item saved) {
    bash_last_exit_code = (int)it2i(saved) & 0xFF;
}

extern "C" void bash_negate_exit_code(void) {
    bash_last_exit_code = (bash_last_exit_code == 0) ? 1 : 0;
}

extern "C" Item bash_return_with_code(Item val) {
    int code = (int)bash_coerce_int(val);
    bash_last_exit_code = code & 0xFF;
    return (Item){.item = i2it(code)};
}

// ============================================================================
// File redirect runtime
// ============================================================================

#include <fcntl.h>

extern "C" Item bash_redirect_write(Item filename, Item content) {
    Item fn_str = bash_to_string(filename);
    String* fn = it2s(fn_str);
    if (!fn || fn->len == 0) {
        bash_last_exit_code = 1;
        return (Item){.item = i2it(1)};
    }

    // /dev/null: just discard
    if (fn->len == 9 && memcmp(fn->chars, "/dev/null", 9) == 0) {
        bash_last_exit_code = 0;
        return (Item){.item = i2it(0)};
    }

    int fd = open(fn->chars, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) {
        log_error("bash: cannot open '%s' for writing", fn->chars);
        bash_last_exit_code = 1;
        return (Item){.item = i2it(1)};
    }

    Item c_str = bash_to_string(content);
    String* c = it2s(c_str);
    if (c && c->len > 0) {
        write(fd, c->chars, c->len);
    }
    close(fd);
    bash_last_exit_code = 0;
    return (Item){.item = i2it(0)};
}

extern "C" Item bash_redirect_append(Item filename, Item content) {
    Item fn_str = bash_to_string(filename);
    String* fn = it2s(fn_str);
    if (!fn || fn->len == 0) {
        bash_last_exit_code = 1;
        return (Item){.item = i2it(1)};
    }

    if (fn->len == 9 && memcmp(fn->chars, "/dev/null", 9) == 0) {
        bash_last_exit_code = 0;
        return (Item){.item = i2it(0)};
    }

    int fd = open(fn->chars, O_WRONLY | O_CREAT | O_APPEND, 0644);
    if (fd < 0) {
        log_error("bash: cannot open '%s' for appending", fn->chars);
        bash_last_exit_code = 1;
        return (Item){.item = i2it(1)};
    }

    Item c_str = bash_to_string(content);
    String* c = it2s(c_str);
    if (c && c->len > 0) {
        write(fd, c->chars, c->len);
    }
    close(fd);
    bash_last_exit_code = 0;
    return (Item){.item = i2it(0)};
}

extern "C" Item bash_redirect_read(Item filename) {
    Item fn_str = bash_to_string(filename);
    String* fn = it2s(fn_str);
    if (!fn || fn->len == 0) {
        bash_last_exit_code = 1;
        return (Item){.item = s2it(heap_create_name("", 0))};
    }

    int fd = open(fn->chars, O_RDONLY);
    if (fd < 0) {
        log_error("bash: cannot open '%s' for reading", fn->chars);
        bash_last_exit_code = 1;
        return (Item){.item = s2it(heap_create_name("", 0))};
    }

    StrBuf* buf = strbuf_new();
    char tmp[4096];
    ssize_t n;
    while ((n = read(fd, tmp, sizeof(tmp))) > 0) {
        strbuf_append_str_n(buf, tmp, (size_t)n);
    }
    close(fd);

    Item result = (Item){.item = s2it(heap_create_name(buf->str, buf->length))};
    strbuf_free(buf);
    bash_last_exit_code = 0;
    return result;
}

// ============================================================================
// External command execution (posix_spawn)
// ============================================================================

#include <spawn.h>
#include <sys/wait.h>

extern char** environ;

// forward declarations (defined in capture section below)
static Item bash_stdin_item;
static bool bash_stdin_item_set;

extern "C" Item bash_exec_external(Item* argv, int argc) {
    if (argc < 1) {
        bash_last_exit_code = 127;
        return (Item){.item = i2it(127)};
    }

    // convert Item argv to char* argv
    const char** c_argv = (const char**)calloc(argc + 1, sizeof(char*));
    if (!c_argv) {
        bash_last_exit_code = 127;
        return (Item){.item = i2it(127)};
    }

    for (int i = 0; i < argc; i++) {
        c_argv[i] = bash_item_to_cstr(argv[i]);
    }
    c_argv[argc] = NULL;

    const char* cmd_name = c_argv[0];

    // resolve command path: if it contains '/', use directly; otherwise search PATH
    char resolved_path[4096];
    const char* exec_path = cmd_name;

    if (!strchr(cmd_name, '/')) {
        const char* path_env = getenv("PATH");
        if (!path_env) path_env = "/usr/bin:/bin";

        bool found = false;
        const char* p = path_env;
        while (*p) {
            const char* colon = strchr(p, ':');
            int dir_len = colon ? (int)(colon - p) : (int)strlen(p);
            if (dir_len == 0) {
                // empty component = current directory
                snprintf(resolved_path, sizeof(resolved_path), "%s", cmd_name);
            } else {
                snprintf(resolved_path, sizeof(resolved_path), "%.*s/%s", dir_len, p, cmd_name);
            }
            if (access(resolved_path, X_OK) == 0) {
                exec_path = resolved_path;
                found = true;
                break;
            }
            if (!colon) break;
            p = colon + 1;
        }
        if (!found) {
            log_debug("bash: %s: command not found", cmd_name);
            // write error to stderr like real bash
            fprintf(stderr, "%s: command not found\n", cmd_name);
            free(c_argv);
            bash_last_exit_code = 127;
            return (Item){.item = i2it(127)};
        }
    } else {
        // absolute or relative path — check it exists
        if (access(cmd_name, X_OK) != 0) {
            log_debug("bash: %s: No such file or directory", cmd_name);
            fprintf(stderr, "%s: No such file or directory\n", cmd_name);
            free(c_argv);
            bash_last_exit_code = 127;
            return (Item){.item = i2it(127)};
        }
    }

    // set up stdout pipe to capture output
    int stdout_pipe[2];
    if (pipe(stdout_pipe) != 0) {
        log_error("bash: pipe failed for command '%s'", cmd_name);
        free(c_argv);
        bash_last_exit_code = 1;
        return (Item){.item = i2it(1)};
    }

    // set up stdin pipe if stdin_item is set
    int stdin_pipe[2] = {-1, -1};
    bool has_stdin = bash_stdin_item_set;
    if (has_stdin) {
        if (pipe(stdin_pipe) != 0) {
            log_error("bash: stdin pipe failed for command '%s'", cmd_name);
            close(stdout_pipe[0]);
            close(stdout_pipe[1]);
            free(c_argv);
            bash_last_exit_code = 1;
            return (Item){.item = i2it(1)};
        }
    }

    // configure file actions for posix_spawn
    posix_spawn_file_actions_t actions;
    posix_spawn_file_actions_init(&actions);

    // redirect child stdout to our pipe
    posix_spawn_file_actions_addclose(&actions, stdout_pipe[0]);
    posix_spawn_file_actions_adddup2(&actions, stdout_pipe[1], STDOUT_FILENO);
    posix_spawn_file_actions_addclose(&actions, stdout_pipe[1]);

    // redirect child stdin from our pipe (if applicable)
    if (has_stdin) {
        posix_spawn_file_actions_addclose(&actions, stdin_pipe[1]);
        posix_spawn_file_actions_adddup2(&actions, stdin_pipe[0], STDIN_FILENO);
        posix_spawn_file_actions_addclose(&actions, stdin_pipe[0]);
    }

    pid_t pid;
    int spawn_err = posix_spawn(&pid, exec_path, &actions, NULL,
                                 (char* const*)c_argv, environ);
    posix_spawn_file_actions_destroy(&actions);

    if (spawn_err != 0) {
        log_error("bash: failed to spawn '%s': %s", cmd_name, strerror(spawn_err));
        close(stdout_pipe[0]);
        close(stdout_pipe[1]);
        if (has_stdin) { close(stdin_pipe[0]); close(stdin_pipe[1]); }
        free(c_argv);
        bash_last_exit_code = 127;
        return (Item){.item = i2it(127)};
    }

    // close write end of stdout pipe in parent
    close(stdout_pipe[1]);

    // write stdin data to child if needed, then close
    if (has_stdin) {
        close(stdin_pipe[0]); // close read end in parent
        Item stdin_val = bash_get_stdin_item();
        String* s = it2s(bash_to_string(stdin_val));
        if (s && s->len > 0) {
            write(stdin_pipe[1], s->chars, s->len);
        }
        close(stdin_pipe[1]);
    }

    // read child's stdout
    StrBuf* buf = strbuf_new();
    char tmp[4096];
    ssize_t n;
    while ((n = read(stdout_pipe[0], tmp, sizeof(tmp))) > 0) {
        strbuf_append_str_n(buf, tmp, (size_t)n);
    }
    close(stdout_pipe[0]);

    // wait for child
    int status = 0;
    waitpid(pid, &status, 0);

    int exit_code = 0;
    if (WIFEXITED(status)) {
        exit_code = WEXITSTATUS(status);
    } else {
        exit_code = 128;
    }

    // write captured output to bash stdout (capture-aware)
    if (buf->length > 0) {
        bash_raw_write(buf->str, (int)buf->length);
    }

    strbuf_free(buf);
    free(c_argv);
    bash_last_exit_code = exit_code;
    return (Item){.item = i2it(exit_code)};
}

// ============================================================================
// Output capture stack (for command substitution)
// ============================================================================

#define BASH_CAPTURE_STACK_MAX 32
static StrBuf* bash_capture_bufs[BASH_CAPTURE_STACK_MAX];
static int bash_capture_depth = 0;

// stdin item for pipeline stage passing (declared above, before bash_exec_external)

extern "C" void bash_begin_capture(void) {
    if (bash_capture_depth < BASH_CAPTURE_STACK_MAX) {
        bash_capture_bufs[bash_capture_depth] = strbuf_new();
        bash_capture_depth++;
    }
}

extern "C" Item bash_end_capture(void) {
    if (bash_capture_depth <= 0) {
        return (Item){.item = s2it(heap_create_name("", 0))};
    }
    bash_capture_depth--;
    StrBuf* buf = bash_capture_bufs[bash_capture_depth];
    // strip trailing newlines (bash behavior for command substitution)
    int len = (int)buf->length;
    while (len > 0 && buf->str[len - 1] == '\n') len--;
    Item result = (Item){.item = s2it(heap_create_name(buf->str, len))};
    strbuf_free(buf);
    return result;
}

// end capture without stripping trailing newlines (for pipelines)
extern "C" Item bash_end_capture_raw(void) {
    if (bash_capture_depth <= 0) {
        return (Item){.item = s2it(heap_create_name("", 0))};
    }
    bash_capture_depth--;
    StrBuf* buf = bash_capture_bufs[bash_capture_depth];
    Item result = (Item){.item = s2it(heap_create_name(buf->str, buf->length))};
    strbuf_free(buf);
    return result;
}

// write raw bytes to stdout or capture buffer
extern "C" void bash_raw_write(const char* data, int len) {
    if (bash_capture_depth > 0) {
        strbuf_append_str_n(bash_capture_bufs[bash_capture_depth - 1], data, len);
    } else {
        fwrite(data, 1, len, stdout);
    }
}

extern "C" void bash_raw_putc(char c) {
    if (bash_capture_depth > 0) {
        strbuf_append_char(bash_capture_bufs[bash_capture_depth - 1], c);
    } else {
        fputc(c, stdout);
    }
}

// ============================================================================
// Pipeline stdin item passing
// ============================================================================

extern "C" void bash_set_stdin_item(Item input) {
    bash_stdin_item = input;
    bash_stdin_item_set = true;
}

extern "C" Item bash_get_stdin_item(void) {
    if (!bash_stdin_item_set) {
        return (Item){.item = s2it(heap_create_name("", 0))};
    }
    return bash_stdin_item;
}

extern "C" void bash_clear_stdin_item(void) {
    bash_stdin_item = (Item){0};
    bash_stdin_item_set = false;
}

// ============================================================================
// Output
// ============================================================================

extern "C" void bash_write_heredoc(Item content, int is_herestring) {
    Item str = bash_to_string(content);
    String* s = it2s(str);
    if (s && s->len > 0) {
        // heredoc body from tree-sitter may include trailing newline
        int len = s->len;
        // trim trailing newline for heredoc (tree-sitter includes it before the delimiter)
        if (!is_herestring && len > 0 && s->chars[len - 1] == '\n') {
            len--;
        }
        if (len > 0) {
            bash_raw_write(s->chars, len);
        }
    }
    bash_raw_putc('\n');
}

extern "C" void bash_write_stdout(Item value) {
    Item str = bash_to_string(value);
    String* s = it2s(str);
    if (s && s->len > 0) {
        bash_raw_write(s->chars, s->len);
    }
}

extern "C" void bash_write_stderr(Item value) {
    Item str = bash_to_string(value);
    String* s = it2s(str);
    if (s && s->len > 0) {
        fwrite(s->chars, 1, s->len, stderr);
    }
}

// ============================================================================
// Runtime initialization
// ============================================================================

extern "C" void bash_runtime_init(void) {
    bash_last_exit_code = 0;
    bash_loop_control = 0;
    bash_loop_control_depth = 1;
    bash_opt_errexit = false;
    bash_opt_nounset = false;
    bash_opt_xtrace = false;
    bash_opt_pipefail = false;
    // clear trap state
    for (int i = 0; i < BASH_TRAP_NUM; i++) {
        bash_trap_handlers[i] = NULL;
        bash_trap_fired[i] = 0;
    }
    // reset function scope stack
    bash_func_scope_depth = 0;
    bash_env_import();
    log_debug("bash: runtime initialized");
}

extern "C" void bash_runtime_cleanup(void) {
    // free trap handlers and restore default signal actions
    static const int os_signals[] = { SIGHUP, SIGINT, SIGQUIT, SIGTERM };
    for (int i = 0; i < BASH_TRAP_NUM; i++) {
        if (bash_trap_handlers[i]) {
            free(bash_trap_handlers[i]);
            bash_trap_handlers[i] = NULL;
        }
        bash_trap_fired[i] = 0;
    }
    for (int i = 0; i < 4; i++) {
        struct sigaction sa;
        memset(&sa, 0, sizeof(sa));
        sa.sa_handler = SIG_DFL;
        sigemptyset(&sa.sa_mask);
        sigaction(os_signals[i], &sa, NULL);
    }
    if (bash_var_table) {
        hashmap_free(bash_var_table);
        bash_var_table = NULL;
    }
    if (bash_rt_func_table) {
        hashmap_free(bash_rt_func_table);
        bash_rt_func_table = NULL;
    }
    // free any leaked scope frames (e.g. if script exited early)
    while (bash_func_scope_depth > 0) {
        bash_func_scope_depth--;
        if (bash_func_scope_stack[bash_func_scope_depth]) {
            hashmap_free(bash_func_scope_stack[bash_func_scope_depth]);
            bash_func_scope_stack[bash_func_scope_depth] = NULL;
        }
    }
    log_debug("bash: runtime cleanup");
}

// ============================================================================
// Variable scope management (runtime)
// ============================================================================

// simple variable table using hashmap
typedef struct BashRtVar {
    const char* name;
    size_t name_len;
    Item value;
    bool is_export;
    int attributes;     // BashVarAttrFlags
} BashRtVar;

static uint64_t bash_rt_var_hash(const void *item, uint64_t seed0, uint64_t seed1) {
    const BashRtVar* v = (const BashRtVar*)item;
    return hashmap_sip(v->name, v->name_len, seed0, seed1);
}

static int bash_rt_var_cmp(const void *a, const void *b, void *udata) {
    const BashRtVar* va = (const BashRtVar*)a;
    const BashRtVar* vb = (const BashRtVar*)b;
    (void)udata;
    if (va->name_len != vb->name_len) return 1;
    return memcmp(va->name, vb->name, va->name_len);
}

static void bash_ensure_var_table(void) {
    if (!bash_var_table) {
        bash_var_table = hashmap_new(sizeof(BashRtVar), 64, 0, 0,
                                    bash_rt_var_hash, bash_rt_var_cmp, NULL, NULL);
    }
}

extern "C" void bash_set_var(Item name, Item value) {
    bash_ensure_var_table();
    String* s = it2s(name);
    if (!s) return;
    BashRtVar key = {.name = s->chars, .name_len = (size_t)s->len};

    // dynamic scoping: if the variable exists in any active function scope frame,
    // update it there rather than writing to the global table
    for (int i = bash_func_scope_depth - 1; i >= 0; i--) {
        const BashRtVar* found = (const BashRtVar*)hashmap_get(bash_func_scope_stack[i], &key);
        if (found) {
            if (found->attributes & BASH_ATTR_READONLY) {
                log_error("bash: %.*s: readonly variable", s->len, s->chars);
                return;
            }
            int attrs = found->attributes;
            Item final_value = value;
            if (attrs & BASH_ATTR_INTEGER)   final_value = bash_to_int(value);
            if (attrs & BASH_ATTR_LOWERCASE) final_value = bash_string_lower(bash_to_string(final_value), true);
            if (attrs & BASH_ATTR_UPPERCASE) final_value = bash_string_upper(bash_to_string(final_value), true);
            BashRtVar entry = {.name = s->chars, .name_len = (size_t)s->len,
                               .value = final_value, .is_export = found->is_export,
                               .attributes = attrs};
            hashmap_set(bash_func_scope_stack[i], &entry);
            return;
        }
    }

    // not in any local frame — use global table
    const BashRtVar* existing = (const BashRtVar*)hashmap_get(bash_var_table, &key);

    // check readonly
    if (existing && (existing->attributes & BASH_ATTR_READONLY)) {
        log_error("bash: %.*s: readonly variable", s->len, s->chars);
        return;
    }

    int attrs = existing ? existing->attributes : BASH_ATTR_NONE;
    bool was_export = existing ? existing->is_export : false;

    // apply attribute transformations
    Item final_value = value;
    if (attrs & BASH_ATTR_INTEGER)   final_value = bash_to_int(value);
    if (attrs & BASH_ATTR_LOWERCASE) final_value = bash_string_lower(bash_to_string(final_value), true);
    if (attrs & BASH_ATTR_UPPERCASE) final_value = bash_string_upper(bash_to_string(final_value), true);

    BashRtVar entry = {.name = s->chars, .name_len = (size_t)s->len,
                       .value = final_value, .is_export = was_export,
                       .attributes = attrs};
    hashmap_set(bash_var_table, &entry);
}

extern "C" Item bash_get_var(Item name) {
    bash_ensure_var_table();
    String* s = it2s(name);
    if (!s) return (Item){.item = s2it(heap_create_name("", 0))};
    BashRtVar key = {.name = s->chars, .name_len = (size_t)s->len};

    // dynamic scoping: walk scope stack from top (most recent) to bottom
    for (int i = bash_func_scope_depth - 1; i >= 0; i--) {
        const BashRtVar* found = (const BashRtVar*)hashmap_get(bash_func_scope_stack[i], &key);
        if (found) return found->value;
    }

    // fall through to global table
    const BashRtVar* found = (const BashRtVar*)hashmap_get(bash_var_table, &key);
    if (found) return found->value;
    return (Item){.item = s2it(NULL)};  // unset = null (distinguishable from empty string)
}

extern "C" void bash_set_local_var(Item name, Item value) {
    bash_ensure_var_table();
    String* s = it2s(name);
    if (!s) return;

    if (bash_func_scope_depth > 0) {
        // write to the top (current function) scope frame
        struct hashmap* frame = bash_func_scope_stack[bash_func_scope_depth - 1];
        BashRtVar key = {.name = s->chars, .name_len = (size_t)s->len};
        const BashRtVar* existing = (const BashRtVar*)hashmap_get(frame, &key);
        int attrs = existing ? existing->attributes : BASH_ATTR_NONE;
        Item final_value = value;
        if (attrs & BASH_ATTR_INTEGER)   final_value = bash_to_int(value);
        if (attrs & BASH_ATTR_LOWERCASE) final_value = bash_string_lower(bash_to_string(final_value), true);
        if (attrs & BASH_ATTR_UPPERCASE) final_value = bash_string_upper(bash_to_string(final_value), true);
        BashRtVar entry = {.name = s->chars, .name_len = (size_t)s->len,
                           .value = final_value, .is_export = false,
                           .attributes = attrs};
        hashmap_set(frame, &entry);
        log_debug("bash: local var '%.*s' set in scope frame %d", s->len, s->chars, bash_func_scope_depth - 1);
    } else {
        // no active function scope (local at top-level) — behaves like set_var
        bash_set_var(name, value);
    }
}

extern "C" void bash_export_var(Item name) {
    bash_ensure_var_table();
    String* s = it2s(name);
    if (!s) return;
    BashRtVar key = {.name = s->chars, .name_len = (size_t)s->len};
    BashRtVar* found = (BashRtVar*)hashmap_get(bash_var_table, &key);
    if (found) {
        BashRtVar updated = *found;
        updated.is_export = true;
        hashmap_set(bash_var_table, &updated);
        // sync to OS environment for child processes
        String* val_str = it2s(updated.value);
        if (val_str) {
            char name_buf[256];
            snprintf(name_buf, sizeof(name_buf), "%.*s", s->len, s->chars);
            char val_buf[4096];
            snprintf(val_buf, sizeof(val_buf), "%.*s", val_str->len, val_str->chars);
            setenv(name_buf, val_buf, 1);
        }
    } else {
        BashRtVar entry = {.name = s->chars, .name_len = (size_t)s->len,
                           .value = (Item){.item = s2it(heap_create_name("", 0))},
                           .is_export = true, .attributes = BASH_ATTR_NONE};
        hashmap_set(bash_var_table, &entry);
        // sync empty var to OS environment
        char name_buf[256];
        snprintf(name_buf, sizeof(name_buf), "%.*s", s->len, s->chars);
        setenv(name_buf, "", 1);
    }
}

extern "C" void bash_unset_var(Item name) {
    bash_ensure_var_table();
    String* s = it2s(name);
    if (!s) return;
    BashRtVar key = {.name = s->chars, .name_len = (size_t)s->len};
    // unset from topmost scope frame first, then global
    for (int i = bash_func_scope_depth - 1; i >= 0; i--) {
        if (hashmap_get(bash_func_scope_stack[i], &key)) {
            hashmap_delete(bash_func_scope_stack[i], &key);
            return;
        }
    }
    hashmap_delete(bash_var_table, &key);
}

// ============================================================================
// Variable attributes (declare/typeset)
// ============================================================================

extern "C" void bash_declare_var(Item name, int flags) {
    bash_ensure_var_table();
    String* s = it2s(name);
    if (!s) return;
    BashRtVar key = {.name = s->chars, .name_len = (size_t)s->len};
    const BashRtVar* existing = (const BashRtVar*)hashmap_get(bash_var_table, &key);

    BashRtVar entry;
    if (existing) {
        entry = *existing;
        entry.attributes |= flags;
    } else {
        entry = {.name = s->chars, .name_len = (size_t)s->len,
                 .value = (Item){.item = s2it(heap_create_name("", 0))},
                 .is_export = false, .attributes = flags};
    }
    if (flags & BASH_ATTR_EXPORT) entry.is_export = true;
    hashmap_set(bash_var_table, &entry);
}

extern "C" int bash_get_var_attrs(Item name) {
    bash_ensure_var_table();
    String* s = it2s(name);
    if (!s) return BASH_ATTR_NONE;
    BashRtVar key = {.name = s->chars, .name_len = (size_t)s->len};
    const BashRtVar* found = (const BashRtVar*)hashmap_get(bash_var_table, &key);
    if (found) return found->attributes;
    return BASH_ATTR_NONE;
}

extern "C" bool bash_is_assoc(Item name) {
    return (bash_get_var_attrs(name) & BASH_ATTR_ASSOC_ARRAY) != 0;
}

// ============================================================================
// Associative arrays (string-keyed hashmaps)
// ============================================================================

typedef struct BashAssocEntry {
    const char* key;
    size_t key_len;
    Item value;
} BashAssocEntry;

// wrapper struct stored as an Item (pointer)
typedef struct BashAssocArray {
    TypeId type_id;     // LMD_TYPE_MAP to distinguish from List (LMD_TYPE_ARRAY)
    uint8_t flags;
    struct hashmap* map;
} BashAssocArray;

static uint64_t bash_assoc_entry_hash(const void *item, uint64_t seed0, uint64_t seed1) {
    const BashAssocEntry* e = (const BashAssocEntry*)item;
    return hashmap_sip(e->key, e->key_len, seed0, seed1);
}

static int bash_assoc_entry_cmp(const void *a, const void *b, void *udata) {
    const BashAssocEntry* ea = (const BashAssocEntry*)a;
    const BashAssocEntry* eb = (const BashAssocEntry*)b;
    (void)udata;
    if (ea->key_len != eb->key_len) return 1;
    return memcmp(ea->key, eb->key, ea->key_len);
}

static BashAssocArray* bash_item_to_assoc(Item map) {
    BashAssocArray* aa = (BashAssocArray*)(uintptr_t)map.item;
    if (!aa || aa->type_id != LMD_TYPE_MAP) return NULL;
    return aa;
}

extern "C" Item bash_assoc_new(void) {
    BashAssocArray* aa = (BashAssocArray*)heap_calloc(sizeof(BashAssocArray), LMD_TYPE_RAW_POINTER);
    aa->type_id = LMD_TYPE_MAP;
    aa->flags = 0;
    aa->map = hashmap_new(sizeof(BashAssocEntry), 16, 0, 0,
                          bash_assoc_entry_hash, bash_assoc_entry_cmp, NULL, NULL);
    return (Item){.item = (uint64_t)(uintptr_t)aa};
}

extern "C" Item bash_assoc_set(Item map, Item key, Item value) {
    BashAssocArray* aa = bash_item_to_assoc(map);
    if (!aa) return map;
    String* k = it2s(bash_to_string(key));
    if (!k) return map;
    BashAssocEntry entry = {.key = k->chars, .key_len = (size_t)k->len, .value = value};
    hashmap_set(aa->map, &entry);
    return map;
}

extern "C" Item bash_assoc_get(Item map, Item key) {
    BashAssocArray* aa = bash_item_to_assoc(map);
    if (!aa) return (Item){.item = s2it(heap_create_name("", 0))};
    String* k = it2s(bash_to_string(key));
    if (!k) return (Item){.item = s2it(heap_create_name("", 0))};
    BashAssocEntry lookup = {.key = k->chars, .key_len = (size_t)k->len};
    const BashAssocEntry* found = (const BashAssocEntry*)hashmap_get(aa->map, &lookup);
    if (found) return found->value;
    return (Item){.item = s2it(heap_create_name("", 0))};
}

extern "C" Item bash_assoc_keys(Item map) {
    BashAssocArray* aa = bash_item_to_assoc(map);
    if (!aa) return bash_array_new();
    Item arr = bash_array_new();
    size_t iter = 0;
    void* item;
    while (hashmap_iter(aa->map, &iter, &item)) {
        const BashAssocEntry* e = (const BashAssocEntry*)item;
        Item key_item = (Item){.item = s2it(heap_create_name(e->key, (int)e->key_len))};
        bash_array_append(arr, key_item);
    }
    return arr;
}

extern "C" Item bash_assoc_values(Item map) {
    BashAssocArray* aa = bash_item_to_assoc(map);
    if (!aa) return bash_array_new();
    Item arr = bash_array_new();
    size_t iter = 0;
    void* item;
    while (hashmap_iter(aa->map, &iter, &item)) {
        const BashAssocEntry* e = (const BashAssocEntry*)item;
        bash_array_append(arr, e->value);
    }
    return arr;
}

extern "C" Item bash_assoc_unset(Item map, Item key) {
    BashAssocArray* aa = bash_item_to_assoc(map);
    if (!aa) return map;
    String* k = it2s(bash_to_string(key));
    if (!k) return map;
    BashAssocEntry lookup = {.key = k->chars, .key_len = (size_t)k->len};
    hashmap_delete(aa->map, &lookup);
    return map;
}

extern "C" Item bash_assoc_length(Item map) {
    BashAssocArray* aa = bash_item_to_assoc(map);
    if (!aa) return (Item){.item = i2it(0)};
    return (Item){.item = i2it((int)hashmap_count(aa->map))};
}

extern "C" int64_t bash_assoc_count(Item map) {
    BashAssocArray* aa = bash_item_to_assoc(map);
    if (!aa) return 0;
    return (int64_t)hashmap_count(aa->map);
}

// ============================================================================
// Positional parameters (runtime)
// ============================================================================

static Item* bash_positional_args = NULL;
static int bash_positional_count = 0;
static const char* bash_script_name = "";

// stack for saving/restoring positional params during function calls
#define BASH_POS_STACK_MAX 64
static struct { Item* args; int count; } bash_pos_stack[BASH_POS_STACK_MAX];
static int bash_pos_stack_depth = 0;

extern "C" void bash_push_positional(Item* args, int count) {
    if (bash_pos_stack_depth < BASH_POS_STACK_MAX) {
        bash_pos_stack[bash_pos_stack_depth].args = bash_positional_args;
        bash_pos_stack[bash_pos_stack_depth].count = bash_positional_count;
        bash_pos_stack_depth++;
    }
    bash_positional_args = args;
    bash_positional_count = count;
}

extern "C" void bash_pop_positional(void) {
    if (bash_pos_stack_depth > 0) {
        bash_pos_stack_depth--;
        bash_positional_args = bash_pos_stack[bash_pos_stack_depth].args;
        bash_positional_count = bash_pos_stack[bash_pos_stack_depth].count;
    }
}

extern "C" void bash_set_positional(Item* args, int count) {
    bash_positional_args = args;
    bash_positional_count = count;
}

extern "C" Item bash_get_positional(int index) {
    if (index < 1 || index > bash_positional_count) {
        return (Item){.item = s2it(heap_create_name("", 0))};
    }
    return bash_positional_args[index - 1];
}

extern "C" Item bash_get_arg_count(void) {
    return (Item){.item = i2it(bash_positional_count)};
}

extern "C" Item bash_get_all_args(void) {
    // return positional args as an array
    Item arr = bash_array_new();
    for (int i = 0; i < bash_positional_count; i++) {
        bash_array_append(arr, bash_positional_args[i]);
    }
    return arr;
}

extern "C" Item bash_get_all_args_string(void) {
    // return all positional args joined with spaces
    if (bash_positional_count == 0) {
        return (Item){.item = s2it(heap_create_name("", 0))};
    }
    StrBuf* sb = strbuf_new();
    for (int i = 0; i < bash_positional_count; i++) {
        if (i > 0) strbuf_append_char(sb, ' ');
        Item str_val = bash_to_string(bash_positional_args[i]);
        String* s = it2s(str_val);
        if (s && s->len > 0) strbuf_append_str_n(sb, s->chars, s->len);
    }
    String* result = heap_create_name(sb->str, sb->length);
    strbuf_free(sb);
    return (Item){.item = s2it(result)};
}

extern "C" Item bash_shift_args(int n) {
    if (n < 1) n = 1;
    if (n > bash_positional_count) n = bash_positional_count;
    bash_positional_args += n;
    bash_positional_count -= n;
    return (Item){.item = i2it(0)};
}

extern "C" Item bash_get_script_name(void) {
    return (Item){.item = s2it(heap_create_name(bash_script_name))};
}

// ============================================================================
// Scope lifecycle (runtime — for JIT-emitted scope push/pop)
// ============================================================================

extern "C" void bash_scope_push(void) {
    if (bash_func_scope_depth >= BASH_FUNC_SCOPE_STACK_MAX) {
        log_error("bash: scope stack overflow (max %d nested calls)", BASH_FUNC_SCOPE_STACK_MAX);
        return;
    }
    bash_func_scope_stack[bash_func_scope_depth] = hashmap_new(
        sizeof(BashRtVar), 16, 0, 0,
        bash_rt_var_hash, bash_rt_var_cmp, NULL, NULL);
    bash_func_scope_depth++;
    log_debug("bash: scope push → depth %d", bash_func_scope_depth);
}

extern "C" void bash_scope_pop(void) {
    if (bash_func_scope_depth <= 0) {
        log_error("bash: scope stack underflow");
        return;
    }
    bash_func_scope_depth--;
    hashmap_free(bash_func_scope_stack[bash_func_scope_depth]);
    bash_func_scope_stack[bash_func_scope_depth] = NULL;
    log_debug("bash: scope pop → depth %d", bash_func_scope_depth);
}

extern "C" void bash_scope_push_subshell(void) {
    // save the current var table and create a copy for the subshell
    bash_ensure_var_table();
    struct hashmap* copy = hashmap_new(sizeof(BashRtVar), 64, 0, 0,
                                       bash_rt_var_hash, bash_rt_var_cmp, NULL, NULL);
    size_t iter = 0;
    void* item;
    while (hashmap_iter(bash_var_table, &iter, &item)) {
        hashmap_set(copy, item);
    }
    // push the original onto the stack, use the copy as current
    if (bash_subshell_depth < BASH_SUBSHELL_STACK_MAX) {
        bash_subshell_stack[bash_subshell_depth] = bash_var_table;
        bash_subshell_depth++;
    }
    bash_var_table = copy;
}

extern "C" void bash_scope_pop_subshell(void) {
    // restore the saved var table, discard subshell copy
    if (bash_subshell_depth > 0) {
        bash_subshell_depth--;
        hashmap_free(bash_var_table);
        bash_var_table = bash_subshell_stack[bash_subshell_depth];
    }
}

// ============================================================================
// POSIX compatibility mode
// ============================================================================

void bash_set_posix_mode(bool mode) {
    bash_posix_mode = mode;
    log_info("bash: POSIX mode %s", mode ? "enabled" : "disabled");
}

bool bash_get_posix_mode(void) {
    return bash_posix_mode;
}

// ============================================================================
// Environment variable integration
// ============================================================================

extern "C" void bash_env_import(void) {
    bash_ensure_var_table();
    static const char* env_names[] = {
        "PATH", "HOME", "USER", "PWD", "SHELL", "LANG", "TERM",
        "EDITOR", "HOSTNAME", "LOGNAME", "TMPDIR", "LC_ALL", "LC_CTYPE",
        "OLDPWD", "SHLVL", "IFS", NULL
    };
    for (int i = 0; env_names[i]; i++) {
        const char* val = getenv(env_names[i]);
        if (val) {
            Item name_item = {.item = s2it(heap_create_name(env_names[i], (int)strlen(env_names[i])))};
            Item val_item = {.item = s2it(heap_create_name(val, (int)strlen(val)))};
            // set in var table with export flag
            BashRtVar entry = {.name = env_names[i], .name_len = strlen(env_names[i]),
                               .value = val_item, .is_export = true,
                               .attributes = BASH_ATTR_NONE};
            hashmap_set(bash_var_table, &entry);
            log_debug("bash: imported env %s=%s", env_names[i], val);
        }
    }
    // also set default IFS if not in env
    BashRtVar ifs_key = {.name = "IFS", .name_len = 3};
    if (!hashmap_get(bash_var_table, &ifs_key)) {
        Item ifs_val = {.item = s2it(heap_create_name(" \t\n", 3))};
        BashRtVar ifs_entry = {.name = "IFS", .name_len = 3,
                               .value = ifs_val, .is_export = false,
                               .attributes = BASH_ATTR_NONE};
        hashmap_set(bash_var_table, &ifs_entry);
    }
}

extern "C" void bash_env_sync_export(Item name) {
    bash_ensure_var_table();
    String* s = it2s(name);
    if (!s) return;
    BashRtVar key = {.name = s->chars, .name_len = (size_t)s->len};
    const BashRtVar* found = (const BashRtVar*)hashmap_get(bash_var_table, &key);
    if (found && found->is_export) {
        String* val_str = it2s(found->value);
        char name_buf[256];
        snprintf(name_buf, sizeof(name_buf), "%.*s", s->len, s->chars);
        if (val_str) {
            char val_buf[4096];
            snprintf(val_buf, sizeof(val_buf), "%.*s", val_str->len, val_str->chars);
            setenv(name_buf, val_buf, 1);
        } else {
            setenv(name_buf, "", 1);
        }
    }
}

// ============================================================================
// Script sourcing
// ============================================================================

// bash_source_file() is implemented in transpile_bash_mir.cpp
// because it needs access to the MIR transpiler infrastructure.
// bash_set_runtime() is also there — called by transpile_bash_to_mir.

// ============================================================================
// Runtime function registry
// ============================================================================

typedef struct BashRtFuncEntry {
    char name[128];
    size_t name_len;
    BashRtFuncPtr ptr;
} BashRtFuncEntry;

static uint64_t bash_rt_func_hash(const void* item, uint64_t seed0, uint64_t seed1) {
    const BashRtFuncEntry* e = (const BashRtFuncEntry*)item;
    return hashmap_sip(e->name, e->name_len, seed0, seed1);
}

static int bash_rt_func_cmp(const void* a, const void* b, void* udata) {
    const BashRtFuncEntry* ea = (const BashRtFuncEntry*)a;
    const BashRtFuncEntry* eb = (const BashRtFuncEntry*)b;
    (void)udata;
    if (ea->name_len != eb->name_len) return 1;
    return memcmp(ea->name, eb->name, ea->name_len);
}

static void bash_ensure_rt_func_table(void) {
    if (!bash_rt_func_table) {
        bash_rt_func_table = hashmap_new(sizeof(BashRtFuncEntry), 16, 0, 0,
                                         bash_rt_func_hash, bash_rt_func_cmp, NULL, NULL);
    }
}

extern "C" void bash_register_rt_func(const char* name, BashRtFuncPtr ptr) {
    bash_ensure_rt_func_table();
    BashRtFuncEntry entry;
    memset(&entry, 0, sizeof(entry));
    size_t len = strlen(name);
    if (len >= sizeof(entry.name)) len = sizeof(entry.name) - 1;
    memcpy(entry.name, name, len);
    entry.name_len = len;
    entry.ptr = ptr;
    hashmap_set(bash_rt_func_table, &entry);
    log_debug("bash: registered runtime function '%.*s'", (int)len, name);
}

extern "C" BashRtFuncPtr bash_lookup_rt_func(const char* name) {
    if (!bash_rt_func_table) return NULL;
    BashRtFuncEntry key;
    memset(&key, 0, sizeof(key));
    key.name_len = strlen(name);
    if (key.name_len >= sizeof(key.name)) key.name_len = sizeof(key.name) - 1;
    memcpy(key.name, name, key.name_len);
    const BashRtFuncEntry* found = (const BashRtFuncEntry*)hashmap_get(bash_rt_func_table, &key);
    return found ? found->ptr : NULL;
}

extern "C" Item bash_call_rt_func(Item name_item, Item* args, int argc) {
    if (!bash_rt_func_table) {
        bash_last_exit_code = 127;
        return (Item){.item = i2it(127)};
    }
    String* s = it2s(name_item);
    if (!s) {
        bash_last_exit_code = 127;
        return (Item){.item = i2it(127)};
    }
    BashRtFuncEntry key;
    memset(&key, 0, sizeof(key));
    size_t len = (size_t)s->len;
    if (len >= sizeof(key.name)) len = sizeof(key.name) - 1;
    memcpy(key.name, s->chars, len);
    key.name_len = len;
    const BashRtFuncEntry* found = (const BashRtFuncEntry*)hashmap_get(bash_rt_func_table, &key);
    if (found && found->ptr) {
        return found->ptr(args, argc);
    }
    log_debug("bash: runtime function '%.*s' not found", s->len, s->chars);
    bash_last_exit_code = 127;
    return (Item){.item = i2it(127)};
}

// ============================================================================
// Shell options (set -e, set -u, set -x, set -o pipefail)
// ============================================================================

extern "C" void bash_set_option(Item option, bool enable) {
    String* s = it2s(option);
    if (!s) return;
    if ((s->len == 1 && s->chars[0] == 'e') ||
        (s->len == 7 && memcmp(s->chars, "errexit", 7) == 0)) {
        bash_opt_errexit = enable;
        log_debug("bash: set errexit=%s", enable ? "on" : "off");
    } else if ((s->len == 1 && s->chars[0] == 'u') ||
               (s->len == 7 && memcmp(s->chars, "nounset", 7) == 0)) {
        bash_opt_nounset = enable;
        log_debug("bash: set nounset=%s", enable ? "on" : "off");
    } else if ((s->len == 1 && s->chars[0] == 'x') ||
               (s->len == 6 && memcmp(s->chars, "xtrace", 6) == 0)) {
        bash_opt_xtrace = enable;
        log_debug("bash: set xtrace=%s", enable ? "on" : "off");
    } else if (s->len == 8 && memcmp(s->chars, "pipefail", 8) == 0) {
        bash_opt_pipefail = enable;
        log_debug("bash: set pipefail=%s", enable ? "on" : "off");
    } else {
        log_debug("bash: set: unknown option '%.*s'", s->len, s->chars);
    }
}

extern "C" bool bash_get_option_errexit(void) {
    return bash_opt_errexit;
}

extern "C" bool bash_get_option_nounset(void) {
    return bash_opt_nounset;
}

extern "C" bool bash_get_option_xtrace(void) {
    return bash_opt_xtrace;
}

extern "C" bool bash_get_option_pipefail(void) {
    return bash_opt_pipefail;
}

// ============================================================================
// Signal handling / trap (Phase 8)
// ============================================================================

// map a signal name string to a trap index; returns -1 if unknown
static int bash_signal_name_to_idx(const char* name, int len) {
    if (len == 4 && memcmp(name, "EXIT", 4) == 0) return BASH_TRAP_IDX_EXIT;
    if (len == 3 && memcmp(name, "ERR", 3) == 0)  return BASH_TRAP_IDX_ERR;
    if (len == 5 && memcmp(name, "DEBUG", 5) == 0) return BASH_TRAP_IDX_DEBUG;
    if (len == 3 && memcmp(name, "HUP", 3) == 0)   return BASH_TRAP_IDX_HUP;
    if (len == 6 && memcmp(name, "SIGHUP", 6) == 0) return BASH_TRAP_IDX_HUP;
    if (len == 3 && memcmp(name, "INT", 3) == 0)   return BASH_TRAP_IDX_INT;
    if (len == 6 && memcmp(name, "SIGINT", 6) == 0) return BASH_TRAP_IDX_INT;
    if (len == 4 && memcmp(name, "QUIT", 4) == 0)  return BASH_TRAP_IDX_QUIT;
    if (len == 7 && memcmp(name, "SIGQUIT", 7) == 0) return BASH_TRAP_IDX_QUIT;
    if (len == 4 && memcmp(name, "TERM", 4) == 0)  return BASH_TRAP_IDX_TERM;
    if (len == 7 && memcmp(name, "SIGTERM", 7) == 0) return BASH_TRAP_IDX_TERM;
    return -1;
}

// get OS signal number for an OS-signal trap index (HUP..TERM); returns 0 otherwise
static int bash_trap_idx_to_signum(int idx) {
    static const int sig_map[] = { 0, 0, 0, SIGHUP, SIGINT, SIGQUIT, SIGTERM };
    return (idx >= 0 && idx < BASH_TRAP_NUM) ? sig_map[idx] : 0;
}

extern "C" void bash_trap_set(Item handler, Item signal_name) {
    String* sig = it2s(signal_name);
    if (!sig) return;

    int idx = bash_signal_name_to_idx(sig->chars, sig->len);
    if (idx < 0) {
        log_debug("bash: trap: unknown signal '%.*s'", sig->len, sig->chars);
        return;
    }

    // free existing handler
    if (bash_trap_handlers[idx]) {
        free(bash_trap_handlers[idx]);
        bash_trap_handlers[idx] = NULL;
    }

    String* h = it2s(handler);
    // '-' alone means reset to default
    if (!h || (h->len == 1 && h->chars[0] == '-')) {
        // restore default signal action for OS signals
        int signum = bash_trap_idx_to_signum(idx);
        if (signum) {
            struct sigaction sa;
            memset(&sa, 0, sizeof(sa));
            sa.sa_handler = SIG_DFL;
            sigemptyset(&sa.sa_mask);
            sigaction(signum, &sa, NULL);
        }
        log_debug("bash: trap reset for '%.*s'", sig->len, sig->chars);
        return;
    }

    // store handler code (null-terminated copy)
    bash_trap_handlers[idx] = (char*)malloc(h->len + 1);
    memcpy(bash_trap_handlers[idx], h->chars, h->len);
    bash_trap_handlers[idx][h->len] = '\0';

    // install OS signal handler if needed
    int signum = bash_trap_idx_to_signum(idx);
    if (signum) {
        struct sigaction sa;
        memset(&sa, 0, sizeof(sa));
        if (h->len == 0) {
            sa.sa_handler = SIG_IGN;   // empty string = ignore
        } else {
            sa.sa_handler = bash_os_signal_handler;
        }
        sigemptyset(&sa.sa_mask);
        sigaction(signum, &sa, NULL);
    }
    log_debug("bash: trap set for '%.*s': '%s'", sig->len, sig->chars, bash_trap_handlers[idx]);
}

extern "C" void bash_trap_run_exit(void) {
    if (!bash_trap_handlers[BASH_TRAP_IDX_EXIT]) return;
    if (bash_trap_handlers[BASH_TRAP_IDX_EXIT][0] == '\0') {
        // ignore (empty handler)
        free(bash_trap_handlers[BASH_TRAP_IDX_EXIT]);
        bash_trap_handlers[BASH_TRAP_IDX_EXIT] = NULL;
        return;
    }
    // steal and clear before running to prevent re-entry
    char* code = bash_trap_handlers[BASH_TRAP_IDX_EXIT];
    bash_trap_handlers[BASH_TRAP_IDX_EXIT] = NULL;
    log_debug("bash: running EXIT trap");
    String* code_str = heap_create_name(code, (int)strlen(code));
    Item code_item = {.item = s2it(code_str)};
    bash_eval_string(code_item);
    free(code);
}

extern "C" void bash_trap_check(void) {
    for (int idx = BASH_TRAP_IDX_HUP; idx < BASH_TRAP_NUM; idx++) {
        if (!bash_trap_fired[idx]) continue;
        bash_trap_fired[idx] = 0;  // clear before running
        if (!bash_trap_handlers[idx] || bash_trap_handlers[idx][0] == '\0') continue;
        log_debug("bash: running signal trap idx=%d", idx);
        String* code_str = heap_create_name(bash_trap_handlers[idx],
                                            (int)strlen(bash_trap_handlers[idx]));
        Item code_item = {.item = s2it(code_str)};
        bash_eval_string(code_item);
    }
}
