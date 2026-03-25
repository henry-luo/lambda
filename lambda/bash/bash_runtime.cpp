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
#include "../lambda-data.hpp"
#include "../transpiler.hpp"
#include "../../lib/log.h"
#include "../../lib/hashmap.h"
#include "../../lib/strbuf.h"
#include <cstring>
#include <cstdlib>
#include <cstdio>
#include <cmath>

// ============================================================================
// Runtime state
// ============================================================================

static int bash_last_exit_code = 0;

// Loop control: 0=none, 1=break, 2=continue
static int bash_loop_control = 0;
static int bash_loop_control_depth = 1;

// Variable table (runtime)
static struct hashmap* bash_var_table = NULL;

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
    return (Item){.item = b2it(bash_coerce_int(left) == bash_coerce_int(right))};
}

extern "C" Item bash_arith_ne(Item left, Item right) {
    return (Item){.item = b2it(bash_coerce_int(left) != bash_coerce_int(right))};
}

extern "C" Item bash_arith_lt(Item left, Item right) {
    return (Item){.item = b2it(bash_coerce_int(left) < bash_coerce_int(right))};
}

extern "C" Item bash_arith_le(Item left, Item right) {
    return (Item){.item = b2it(bash_coerce_int(left) <= bash_coerce_int(right))};
}

extern "C" Item bash_arith_gt(Item left, Item right) {
    return (Item){.item = b2it(bash_coerce_int(left) > bash_coerce_int(right))};
}

extern "C" Item bash_arith_ge(Item left, Item right) {
    return (Item){.item = b2it(bash_coerce_int(left) >= bash_coerce_int(right))};
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
    bool result = (strcmp(l, r) == 0);
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

// regex match (=~) — simplified pattern match
extern "C" Item bash_test_regex(Item string, Item pattern) {
    // TODO: implement full regex matching with BASH_REMATCH
    char buf_s[256], buf_p[256];
    const char* s = bash_item_cstr(string, buf_s, sizeof(buf_s));
    const char* p = bash_item_cstr(pattern, buf_p, sizeof(buf_p));
    (void)s; (void)p;
    log_debug("bash: regex match not yet implemented");
    return (Item){.item = b2it(false)};
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
// Parameter expansion
// ============================================================================

extern "C" Item bash_expand_default(Item value, Item default_val) {
    // ${var:-default}: use default if var is unset or empty
    TypeId type = get_type_id(value);
    if (type == LMD_TYPE_NULL) return default_val;
    if (type == LMD_TYPE_STRING) {
        String* str = it2s(value);
        if (!str || str->len == 0) return default_val;
    }
    return value;
}

extern "C" Item bash_expand_assign_default(Item value, Item default_val) {
    // ${var:=default}: same check as :-, returns default (caller handles assignment)
    TypeId type = get_type_id(value);
    if (type == LMD_TYPE_NULL) return default_val;
    if (type == LMD_TYPE_STRING) {
        String* str = it2s(value);
        if (!str || str->len == 0) return default_val;
    }
    return value;
}

extern "C" Item bash_expand_alt(Item value, Item alt_val) {
    // ${var:+alt}: use alt if var IS set and non-empty
    TypeId type = get_type_id(value);
    if (type == LMD_TYPE_NULL) return (Item){.item = s2it(heap_create_name("", 0))};
    if (type == LMD_TYPE_STRING) {
        String* str = it2s(value);
        if (!str || str->len == 0) return (Item){.item = s2it(heap_create_name("", 0))};
    }
    return alt_val;
}

extern "C" Item bash_expand_error(Item value, Item msg) {
    // ${var:?msg}: error if unset or empty
    TypeId type = get_type_id(value);
    bool is_empty = (type == LMD_TYPE_NULL);
    if (type == LMD_TYPE_STRING) {
        String* str = it2s(value);
        if (!str || str->len == 0) is_empty = true;
    }
    if (is_empty) {
        String* m = it2s(bash_to_string(msg));
        log_error("bash: %s", m ? m->chars : "parameter null or not set");
    }
    return value;
}

// ============================================================================
// Array operations
// ============================================================================

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

extern "C" Item bash_array_all(Item arr) {
    // return the array as-is (it's already a list)
    return arr;
}

extern "C" Item bash_array_unset(Item arr, Item index) {
    List* list = it2list(arr);
    if (!list) return arr;
    int64_t idx = bash_coerce_int(index);
    if (idx < 0 || idx >= list->length) return arr;
    // in Bash, unset arr[i] sets element to empty, preserving indices
    list->items[idx] = (Item){.item = s2it(heap_create_name("", 0))};
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

extern "C" void bash_negate_exit_code(void) {
    bash_last_exit_code = (bash_last_exit_code == 0) ? 1 : 0;
}

// ============================================================================
// Output
// ============================================================================

extern "C" void bash_write_stdout(Item value) {
    Item str = bash_to_string(value);
    String* s = it2s(str);
    if (s && s->len > 0) {
        fwrite(s->chars, 1, s->len, stdout);
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
    log_debug("bash: runtime initialized");
}

extern "C" void bash_runtime_cleanup(void) {
    if (bash_var_table) {
        hashmap_free(bash_var_table);
        bash_var_table = NULL;
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
    BashRtVar entry = {.name = s->chars, .name_len = (size_t)s->len, .value = value, .is_export = false};
    // check if exists and preserve export flag
    const BashRtVar* existing = (const BashRtVar*)hashmap_get(bash_var_table, &entry);
    if (existing) entry.is_export = existing->is_export;
    hashmap_set(bash_var_table, &entry);
}

extern "C" Item bash_get_var(Item name) {
    bash_ensure_var_table();
    String* s = it2s(name);
    if (!s) return (Item){.item = s2it(heap_create_name("", 0))};
    BashRtVar key = {.name = s->chars, .name_len = (size_t)s->len};
    const BashRtVar* found = (const BashRtVar*)hashmap_get(bash_var_table, &key);
    if (found) return found->value;
    return (Item){.item = s2it(heap_create_name("", 0))};
}

extern "C" void bash_set_local_var(Item name, Item value) {
    // in Phase 1, local is the same as set_var (no function scope yet)
    bash_set_var(name, value);
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
    } else {
        BashRtVar entry = {.name = s->chars, .name_len = (size_t)s->len,
                           .value = (Item){.item = s2it(heap_create_name("", 0))},
                           .is_export = true};
        hashmap_set(bash_var_table, &entry);
    }
}

extern "C" void bash_unset_var(Item name) {
    bash_ensure_var_table();
    String* s = it2s(name);
    if (!s) return;
    BashRtVar key = {.name = s->chars, .name_len = (size_t)s->len};
    hashmap_delete(bash_var_table, &key);
}

// ============================================================================
// Positional parameters (runtime)
// ============================================================================

static Item* bash_positional_args = NULL;
static int bash_positional_count = 0;
static const char* bash_script_name = "";

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
    // Phase 1: no-op (flat scope). Phase 2 will implement dynamic scope stack.
    log_debug("bash: runtime scope push");
}

extern "C" void bash_scope_pop(void) {
    log_debug("bash: runtime scope pop");
}

extern "C" void bash_scope_push_subshell(void) {
    // Phase 1: snapshot vars (simplified — just log for now)
    log_debug("bash: runtime subshell scope push");
}

extern "C" void bash_scope_pop_subshell(void) {
    log_debug("bash: runtime subshell scope pop");
}
