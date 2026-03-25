/**
 * Python Built-in Functions for Lambda
 *
 * Implements Python's built-in functions (print, len, range, type, etc.)
 * and method dispatchers (str.upper(), list.append(), dict.keys(), etc.)
 */
#include "py_runtime.h"
#include "../lambda-data.hpp"
#include "../transpiler.hpp"
#include "../../lib/log.h"
#include "../../lib/strbuf.h"
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <cctype>

extern Input* py_input;

extern Item _map_read_field(ShapeEntry* field, void* map_data);
extern Item _map_get(TypeMap* map_type, void* map_data, char* key, bool* is_found);

// Forward declarations from py_runtime.cpp
extern "C" Item py_to_str(Item value);
extern "C" Item py_to_int(Item value);
extern "C" Item py_to_float(Item value);
extern "C" Item py_to_bool(Item value);
extern "C" bool py_is_truthy(Item value);
extern "C" Item py_eq(Item left, Item right);
extern "C" Item py_lt(Item left, Item right);
extern "C" Item py_list_new(int length);
extern "C" Item py_list_append(Item list, Item value);
extern "C" Item py_tuple_new(int length);
extern "C" Item py_tuple_set(Item tuple, int index, Item value);
extern "C" Item py_dict_new(void);
extern "C" Item py_dict_set(Item dict, Item key, Item value);
extern "C" Item py_range_new(Item start, Item stop, Item step);
extern "C" Item py_call_function(Item func, Item* args, int arg_count);
double py_get_number(Item value);

// ============================================================================
// print()
// ============================================================================

extern "C" Item py_print(Item* args, int argc) {
    for (int i = 0; i < argc; i++) {
        if (i > 0) printf(" ");
        Item str = py_to_str(args[i]);
        if (get_type_id(str) == LMD_TYPE_STRING) {
            String* s = it2s(str);
            printf("%.*s", (int)s->len, s->chars);
        }
    }
    printf("\n");
    return ItemNull;
}

extern "C" Item py_print_ex(Item* args, int argc, Item sep, Item end) {
    // sep defaults to " ", end defaults to "\n"
    const char* sep_str = " ";
    int sep_len = 1;
    if (get_type_id(sep) == LMD_TYPE_STRING) {
        String* s = it2s(sep);
        if (s) { sep_str = s->chars; sep_len = (int)s->len; }
    }
    const char* end_str = "\n";
    int end_len = 1;
    if (get_type_id(end) == LMD_TYPE_STRING) {
        String* s = it2s(end);
        if (s) { end_str = s->chars; end_len = (int)s->len; }
    }

    for (int i = 0; i < argc; i++) {
        if (i > 0) printf("%.*s", sep_len, sep_str);
        Item str = py_to_str(args[i]);
        if (get_type_id(str) == LMD_TYPE_STRING) {
            String* s = it2s(str);
            printf("%.*s", (int)s->len, s->chars);
        }
    }
    printf("%.*s", end_len, end_str);
    return ItemNull;
}

// ============================================================================
// len()
// ============================================================================

extern "C" Item py_builtin_len(Item obj) {
    TypeId type = get_type_id(obj);
    switch (type) {
    case LMD_TYPE_STRING: {
        String* s = it2s(obj);
        return (Item){.item = i2it(s ? s->len : 0)};
    }
    case LMD_TYPE_ARRAY: {
        Array* arr = it2arr(obj);
        return (Item){.item = i2it(arr ? arr->length : 0)};
    }
    case LMD_TYPE_MAP: {
        Map* m = it2map(obj);
        if (!m || !m->type) return (Item){.item = i2it(0)};
        return (Item){.item = i2it(((TypeMap*)m->type)->field_count)};
    }
    default:
        log_error("py: TypeError: object has no len()");
        return (Item){.item = i2it(0)};
    }
}

// ============================================================================
// type()
// ============================================================================

extern "C" Item py_builtin_type(Item obj) {
    TypeId type = get_type_id(obj);
    const char* name;
    switch (type) {
    case LMD_TYPE_NULL:   name = "<class 'NoneType'>"; break;
    case LMD_TYPE_BOOL:   name = "<class 'bool'>"; break;
    case LMD_TYPE_INT:    name = "<class 'int'>"; break;
    case LMD_TYPE_FLOAT:  name = "<class 'float'>"; break;
    case LMD_TYPE_STRING: name = "<class 'str'>"; break;
    case LMD_TYPE_ARRAY:  name = "<class 'list'>"; break;
    case LMD_TYPE_MAP:    name = "<class 'dict'>"; break;
    case LMD_TYPE_FUNC:   name = "<class 'function'>"; break;
    default:              name = "<class 'object'>"; break;
    }
    return (Item){.item = s2it(heap_create_name(name))};
}

// ============================================================================
// isinstance()
// ============================================================================

extern "C" Item py_builtin_isinstance(Item obj, Item classinfo) {
    // simplified: compare type name strings
    Item obj_type = py_builtin_type(obj);
    return py_eq(obj_type, classinfo);
}

// ============================================================================
// range()
// ============================================================================

extern "C" Item py_builtin_range(Item* args, int argc) {
    Item start, stop, step;
    if (argc == 1) {
        start = (Item){.item = i2it(0)};
        stop = args[0];
        step = (Item){.item = i2it(1)};
    } else if (argc == 2) {
        start = args[0];
        stop = args[1];
        step = (Item){.item = i2it(1)};
    } else if (argc >= 3) {
        start = args[0];
        stop = args[1];
        step = args[2];
    } else {
        return py_list_new(0);
    }
    return py_range_new(start, stop, step);
}

// ============================================================================
// Type constructors
// ============================================================================

extern "C" Item py_builtin_int(Item value) {
    return py_to_int(value);
}

extern "C" Item py_builtin_float(Item value) {
    return py_to_float(value);
}

extern "C" Item py_builtin_str(Item value) {
    return py_to_str(value);
}

extern "C" Item py_builtin_bool(Item value) {
    return py_to_bool(value);
}

// ============================================================================
// abs()
// ============================================================================

extern "C" Item py_builtin_abs(Item value) {
    TypeId type = get_type_id(value);
    if (type == LMD_TYPE_INT) {
        int64_t v = it2i(value);
        return (Item){.item = i2it(v < 0 ? -v : v)};
    }
    if (type == LMD_TYPE_FLOAT) {
        double d = it2d(value);
        double* ptr = (double*)heap_alloc(sizeof(double), LMD_TYPE_FLOAT);
        *ptr = fabs(d);
        return (Item){.item = d2it(ptr)};
    }
    return value;
}

// ============================================================================
// min(), max()
// ============================================================================

extern "C" Item py_builtin_min(Item* args, int argc) {
    if (argc == 0) return ItemNull;

    // if single iterable argument
    if (argc == 1 && get_type_id(args[0]) == LMD_TYPE_ARRAY) {
        Array* arr = it2arr(args[0]);
        if (!arr || arr->length == 0) return ItemNull;
        Item result = arr->items[0];
        for (int i = 1; i < arr->length; i++) {
            Item cmp = py_lt(arr->items[i], result);
            if (it2b(cmp)) result = arr->items[i];
        }
        return result;
    }

    // multiple args
    Item result = args[0];
    for (int i = 1; i < argc; i++) {
        Item cmp = py_lt(args[i], result);
        if (it2b(cmp)) result = args[i];
    }
    return result;
}

extern "C" Item py_builtin_max(Item* args, int argc) {
    if (argc == 0) return ItemNull;

    if (argc == 1 && get_type_id(args[0]) == LMD_TYPE_ARRAY) {
        Array* arr = it2arr(args[0]);
        if (!arr || arr->length == 0) return ItemNull;
        Item result = arr->items[0];
        for (int i = 1; i < arr->length; i++) {
            Item cmp = py_lt(result, arr->items[i]);
            if (it2b(cmp)) result = arr->items[i];
        }
        return result;
    }

    Item result = args[0];
    for (int i = 1; i < argc; i++) {
        Item cmp = py_lt(result, args[i]);
        if (it2b(cmp)) result = args[i];
    }
    return result;
}

// ============================================================================
// sum()
// ============================================================================

extern "C" Item py_builtin_sum(Item iterable) {
    if (get_type_id(iterable) != LMD_TYPE_ARRAY) return (Item){.item = i2it(0)};
    Array* arr = it2arr(iterable);
    if (!arr || arr->length == 0) return (Item){.item = i2it(0)};

    Item result = (Item){.item = i2it(0)};
    for (int i = 0; i < arr->length; i++) {
        result = fn_add(result, arr->items[i]);
    }
    return result;
}

// ============================================================================
// enumerate()
// ============================================================================

extern "C" Item py_builtin_enumerate(Item iterable) {
    if (get_type_id(iterable) != LMD_TYPE_ARRAY) return py_list_new(0);
    Array* arr = it2arr(iterable);
    if (!arr) return py_list_new(0);

    Array* result = array();
    for (int i = 0; i < arr->length; i++) {
        Item tuple = py_tuple_new(2);
        py_tuple_set(tuple, 0, (Item){.item = i2it(i)});
        py_tuple_set(tuple, 1, arr->items[i]);
        array_push(result, tuple);
    }
    return (Item){.array = result};
}

// ============================================================================
// zip()
// ============================================================================

extern "C" Item py_builtin_zip(Item* args, int argc) {
    if (argc == 0) return py_list_new(0);

    // find minimum length
    int min_len = INT32_MAX;
    for (int i = 0; i < argc; i++) {
        if (get_type_id(args[i]) != LMD_TYPE_ARRAY) return py_list_new(0);
        Array* arr = it2arr(args[i]);
        if (!arr) return py_list_new(0);
        if (arr->length < min_len) min_len = arr->length;
    }

    Array* result = array();
    for (int i = 0; i < min_len; i++) {
        Item tuple = py_tuple_new(argc);
        for (int j = 0; j < argc; j++) {
            Array* arr = it2arr(args[j]);
            py_tuple_set(tuple, j, arr->items[i]);
        }
        array_push(result, tuple);
    }
    return (Item){.array = result};
}

// ============================================================================
// sorted(), reversed()
// ============================================================================

// simple insertion sort for sorted()
extern "C" Item py_builtin_sorted(Item iterable) {
    if (get_type_id(iterable) != LMD_TYPE_ARRAY) return py_list_new(0);
    Array* src = it2arr(iterable);
    if (!src || src->length == 0) return py_list_new(0);

    // copy array
    Array* result = array();
    for (int i = 0; i < src->length; i++) {
        array_push(result, src->items[i]);
    }

    // insertion sort
    for (int i = 1; i < result->length; i++) {
        Item key = result->items[i];
        int j = i - 1;
        while (j >= 0 && it2b(py_lt(key, result->items[j]))) {
            result->items[j + 1] = result->items[j];
            j--;
        }
        result->items[j + 1] = key;
    }
    return (Item){.array = result};
}

extern "C" Item py_builtin_reversed(Item iterable) {
    if (get_type_id(iterable) != LMD_TYPE_ARRAY) return py_list_new(0);
    Array* src = it2arr(iterable);
    if (!src || src->length == 0) return py_list_new(0);

    Array* result = array();
    for (int i = src->length - 1; i >= 0; i--) {
        array_push(result, src->items[i]);
    }
    return (Item){.array = result};
}

// ============================================================================
// repr(), hash(), id()
// ============================================================================

extern "C" Item py_builtin_repr(Item obj) {
    TypeId type = get_type_id(obj);
    if (type == LMD_TYPE_STRING) {
        String* s = it2s(obj);
        // wrap in single quotes
        int buf_len = s->len + 3;
        char* buf = (char*)malloc(buf_len);
        buf[0] = '\'';
        memcpy(buf + 1, s->chars, s->len);
        buf[s->len + 1] = '\'';
        buf[s->len + 2] = '\0';
        Item result = (Item){.item = s2it(heap_strcpy(buf, s->len + 2))};
        free(buf);
        return result;
    }
    return py_to_str(obj);
}

extern "C" Item py_builtin_hash(Item obj) {
    TypeId type = get_type_id(obj);
    switch (type) {
    case LMD_TYPE_INT:    return obj;
    case LMD_TYPE_BOOL:   return (Item){.item = i2it(it2b(obj) ? 1 : 0)};
    case LMD_TYPE_STRING: {
        // simple string hash
        String* s = it2s(obj);
        uint64_t h = 5381;
        for (int64_t i = 0; i < s->len; i++) {
            h = ((h << 5) + h) + (uint8_t)s->chars[i];
        }
        return (Item){.item = i2it((int64_t)h)};
    }
    case LMD_TYPE_NULL:   return (Item){.item = i2it(0)};
    default:
        return (Item){.item = i2it((int64_t)obj.item)};
    }
}

extern "C" Item py_builtin_id(Item obj) {
    return (Item){.item = i2it((int64_t)obj.item)};
}

// ============================================================================
// input()
// ============================================================================

extern "C" Item py_builtin_input(Item prompt) {
    if (get_type_id(prompt) == LMD_TYPE_STRING) {
        String* s = it2s(prompt);
        printf("%.*s", (int)s->len, s->chars);
        fflush(stdout);
    }
    char buf[4096];
    if (fgets(buf, sizeof(buf), stdin)) {
        size_t len = strlen(buf);
        if (len > 0 && buf[len - 1] == '\n') buf[--len] = '\0';
        return (Item){.item = s2it(heap_strcpy(buf, len))};
    }
    return (Item){.item = s2it(heap_create_name(""))};
}

// ============================================================================
// ord(), chr()
// ============================================================================

extern "C" Item py_builtin_ord(Item ch) {
    if (get_type_id(ch) != LMD_TYPE_STRING) return (Item){.item = i2it(0)};
    String* s = it2s(ch);
    if (!s || s->len == 0) return (Item){.item = i2it(0)};
    return (Item){.item = i2it((uint8_t)s->chars[0])};
}

extern "C" Item py_builtin_chr(Item code) {
    int64_t c = (get_type_id(code) == LMD_TYPE_INT) ? it2i(code) : 0;
    if (c < 0 || c > 127) c = 0; // ASCII range for simplicity
    char buf[2] = {(char)c, '\0'};
    return (Item){.item = s2it(heap_strcpy(buf, 1))};
}

// ============================================================================
// round()
// ============================================================================

extern "C" Item py_builtin_round(Item x, Item ndigits) {
    double val = py_get_number(x);
    int64_t n = (get_type_id(ndigits) == LMD_TYPE_INT) ? it2i(ndigits) : 0;
    if (n == 0 && get_type_id(ndigits) != LMD_TYPE_INT) {
        // round(x) with no second arg → return int
        long long r = llround(val);
        return (Item){.item = i2it(r)};
    }
    double scale = pow(10.0, (double)n);
    double rounded = round(val * scale) / scale;
    double* ptr = (double*)heap_alloc(sizeof(double), LMD_TYPE_FLOAT);
    *ptr = rounded;
    return (Item){.item = d2it(ptr)};
}

// ============================================================================
// all(), any()
// ============================================================================

extern "C" Item py_builtin_all(Item iterable) {
    if (get_type_id(iterable) != LMD_TYPE_ARRAY) return (Item){.item = ITEM_TRUE};
    Array* arr = it2arr(iterable);
    if (!arr) return (Item){.item = ITEM_TRUE};
    for (int i = 0; i < arr->length; i++) {
        if (!py_is_truthy(arr->items[i])) return (Item){.item = ITEM_FALSE};
    }
    return (Item){.item = ITEM_TRUE};
}

extern "C" Item py_builtin_any(Item iterable) {
    if (get_type_id(iterable) != LMD_TYPE_ARRAY) return (Item){.item = ITEM_FALSE};
    Array* arr = it2arr(iterable);
    if (!arr) return (Item){.item = ITEM_FALSE};
    for (int i = 0; i < arr->length; i++) {
        if (py_is_truthy(arr->items[i])) return (Item){.item = ITEM_TRUE};
    }
    return (Item){.item = ITEM_FALSE};
}

// ============================================================================
// bin(), oct(), hex()
// ============================================================================

extern "C" Item py_builtin_bin(Item n) {
    int64_t val = (get_type_id(n) == LMD_TYPE_INT) ? it2i(n) : (int64_t)py_get_number(n);
    char buf[68];
    int pos = 0;
    if (val < 0) { buf[pos++] = '-'; val = -val; }
    buf[pos++] = '0'; buf[pos++] = 'b';
    if (val == 0) {
        buf[pos++] = '0';
    } else {
        char tmp[64];
        int tpos = 0;
        uint64_t v = (uint64_t)val;
        while (v > 0) { tmp[tpos++] = '0' + (v & 1); v >>= 1; }
        for (int i = tpos - 1; i >= 0; i--) buf[pos++] = tmp[i];
    }
    buf[pos] = '\0';
    return (Item){.item = s2it(heap_strcpy(buf, pos))};
}

extern "C" Item py_builtin_oct(Item n) {
    int64_t val = (get_type_id(n) == LMD_TYPE_INT) ? it2i(n) : (int64_t)py_get_number(n);
    char buf[32];
    int len;
    if (val < 0) {
        len = snprintf(buf, sizeof(buf), "-0o%llo", (unsigned long long)(-val));
    } else {
        len = snprintf(buf, sizeof(buf), "0o%llo", (unsigned long long)val);
    }
    return (Item){.item = s2it(heap_strcpy(buf, len))};
}

extern "C" Item py_builtin_hex(Item n) {
    int64_t val = (get_type_id(n) == LMD_TYPE_INT) ? it2i(n) : (int64_t)py_get_number(n);
    char buf[32];
    int len;
    if (val < 0) {
        len = snprintf(buf, sizeof(buf), "-0x%llx", (unsigned long long)(-val));
    } else {
        len = snprintf(buf, sizeof(buf), "0x%llx", (unsigned long long)val);
    }
    return (Item){.item = s2it(heap_strcpy(buf, len))};
}

// ============================================================================
// divmod()
// ============================================================================

extern "C" Item py_builtin_divmod(Item a, Item b) {
    double da = py_get_number(a);
    double db = py_get_number(b);
    if (db == 0.0) {
        log_error("py: ZeroDivisionError: divmod()");
        return ItemNull;
    }
    int64_t quotient = (int64_t)floor(da / db);
    double remainder = da - quotient * db;
    Item tuple = py_tuple_new(2);
    py_tuple_set(tuple, 0, (Item){.item = i2it(quotient)});
    if (get_type_id(a) == LMD_TYPE_FLOAT || get_type_id(b) == LMD_TYPE_FLOAT) {
        double* ptr = (double*)heap_alloc(sizeof(double), LMD_TYPE_FLOAT);
        *ptr = remainder;
        py_tuple_set(tuple, 1, (Item){.item = d2it(ptr)});
    } else {
        py_tuple_set(tuple, 1, (Item){.item = i2it((int64_t)remainder)});
    }
    return tuple;
}

// ============================================================================
// pow()
// ============================================================================

extern "C" Item py_builtin_pow(Item base, Item exp, Item mod) {
    double b = py_get_number(base);
    double e = py_get_number(exp);
    if (get_type_id(mod) != LMD_TYPE_NULL) {
        // modular exponentiation (integer only)
        int64_t bv = (int64_t)b;
        int64_t ev = (int64_t)e;
        int64_t mv = (int64_t)py_get_number(mod);
        if (mv == 0) {
            log_error("py: ValueError: pow() 3rd argument cannot be 0");
            return ItemNull;
        }
        int64_t result = 1;
        bv = bv % mv;
        while (ev > 0) {
            if (ev & 1) result = (result * bv) % mv;
            ev >>= 1;
            bv = (bv * bv) % mv;
        }
        return (Item){.item = i2it(result)};
    }
    double result = pow(b, e);
    if (get_type_id(base) == LMD_TYPE_INT && get_type_id(exp) == LMD_TYPE_INT && e >= 0) {
        return (Item){.item = i2it((int64_t)result)};
    }
    double* ptr = (double*)heap_alloc(sizeof(double), LMD_TYPE_FLOAT);
    *ptr = result;
    return (Item){.item = d2it(ptr)};
}

// ============================================================================
// callable()
// ============================================================================

extern "C" Item py_builtin_callable(Item obj) {
    return (Item){.item = b2it(get_type_id(obj) == LMD_TYPE_FUNC)};
}

// ============================================================================
// sorted() with key= and reverse=
// ============================================================================

extern "C" Item py_builtin_sorted_ex(Item iterable, Item key_func, Item reverse_flag) {
    if (get_type_id(iterable) != LMD_TYPE_ARRAY) return py_list_new(0);
    Array* src = it2arr(iterable);
    if (!src || src->length == 0) return py_list_new(0);

    bool has_key = (get_type_id(key_func) == LMD_TYPE_FUNC);
    bool reverse = py_is_truthy(reverse_flag);

    // copy array
    Array* result = array();
    for (int i = 0; i < src->length; i++) array_push(result, src->items[i]);

    // compute sort keys if key function provided
    Item* keys = NULL;
    if (has_key) {
        keys = (Item*)malloc(result->length * sizeof(Item));
        for (int i = 0; i < result->length; i++) {
            Item arg = result->items[i];
            keys[i] = py_call_function(key_func, &arg, 1);
        }
    }

    // insertion sort using keys or direct values
    for (int i = 1; i < result->length; i++) {
        Item key_i = has_key ? keys[i] : result->items[i];
        Item val_i = result->items[i];
        Item saved_key = key_i;
        int j = i - 1;
        while (j >= 0) {
            Item key_j = has_key ? keys[j] : result->items[j];
            bool do_swap = reverse ? it2b(py_lt(key_j, saved_key)) : it2b(py_lt(saved_key, key_j));
            if (!do_swap) break;
            result->items[j + 1] = result->items[j];
            if (has_key) keys[j + 1] = keys[j];
            j--;
        }
        result->items[j + 1] = val_i;
        if (has_key) keys[j + 1] = saved_key;
    }

    if (keys) free(keys);
    return (Item){.array = result};
}

// ============================================================================
// list.sort() with key= and reverse=
// ============================================================================

extern "C" Item py_list_sort_ex(Item list_item, Item key_func, Item reverse_flag) {
    if (get_type_id(list_item) != LMD_TYPE_ARRAY) return ItemNull;
    Array* arr = it2arr(list_item);
    if (!arr || arr->length == 0) return ItemNull;

    bool has_key = (get_type_id(key_func) == LMD_TYPE_FUNC);
    bool reverse = py_is_truthy(reverse_flag);

    Item* keys = NULL;
    if (has_key) {
        keys = (Item*)malloc(arr->length * sizeof(Item));
        for (int i = 0; i < arr->length; i++) {
            Item arg = arr->items[i];
            keys[i] = py_call_function(key_func, &arg, 1);
        }
    }

    for (int i = 1; i < arr->length; i++) {
        Item key_i = has_key ? keys[i] : arr->items[i];
        Item val_i = arr->items[i];
        Item saved_key = key_i;
        int j = i - 1;
        while (j >= 0) {
            Item key_j = has_key ? keys[j] : arr->items[j];
            bool do_swap = reverse ? it2b(py_lt(key_j, saved_key)) : it2b(py_lt(saved_key, key_j));
            if (!do_swap) break;
            arr->items[j + 1] = arr->items[j];
            if (has_key) keys[j + 1] = keys[j];
            j--;
        }
        arr->items[j + 1] = val_i;
        if (has_key) keys[j + 1] = saved_key;
    }

    if (keys) free(keys);
    return ItemNull;
}

// ============================================================================
// map(), filter()
// ============================================================================

extern "C" Item py_builtin_map(Item func, Item iterable) {
    if (get_type_id(iterable) != LMD_TYPE_ARRAY) return py_list_new(0);
    Array* src = it2arr(iterable);
    if (!src) return py_list_new(0);

    Array* result = array();
    for (int i = 0; i < src->length; i++) {
        Item arg = src->items[i];
        Item val = py_call_function(func, &arg, 1);
        array_push(result, val);
    }
    return (Item){.array = result};
}

extern "C" Item py_builtin_filter(Item func, Item iterable) {
    if (get_type_id(iterable) != LMD_TYPE_ARRAY) return py_list_new(0);
    Array* src = it2arr(iterable);
    if (!src) return py_list_new(0);

    Array* result = array();
    for (int i = 0; i < src->length; i++) {
        Item arg = src->items[i];
        Item val = py_call_function(func, &arg, 1);
        if (py_is_truthy(val)) {
            array_push(result, src->items[i]);
        }
    }
    return (Item){.array = result};
}

// ============================================================================
// list(), dict(), set(), tuple() constructors
// ============================================================================

extern "C" Item py_builtin_list(Item iterable) {
    TypeId type = get_type_id(iterable);
    if (type == LMD_TYPE_ARRAY) {
        Array* src = it2arr(iterable);
        if (!src) return py_list_new(0);
        Array* result = array();
        for (int i = 0; i < src->length; i++) {
            array_push(result, src->items[i]);
        }
        return (Item){.array = result};
    }
    if (type == LMD_TYPE_STRING) {
        String* s = it2s(iterable);
        if (!s) return py_list_new(0);
        Array* result = array();
        for (int64_t i = 0; i < s->len; i++) {
            array_push(result, (Item){.item = s2it(heap_strcpy(s->chars + i, 1))});
        }
        return (Item){.array = result};
    }
    return py_list_new(0);
}

extern "C" Item py_builtin_dict(Item* args, int argc) {
    return py_dict_new();
}

extern "C" Item py_builtin_set(Item iterable) {
    // simplified: return unique list (no Set type yet)
    if (get_type_id(iterable) != LMD_TYPE_ARRAY) return py_list_new(0);
    Array* src = it2arr(iterable);
    if (!src) return py_list_new(0);

    Array* result = array();
    for (int i = 0; i < src->length; i++) {
        bool found = false;
        for (int j = 0; j < result->length; j++) {
            if (it2b(py_eq(src->items[i], result->items[j]))) {
                found = true;
                break;
            }
        }
        if (!found) array_push(result, src->items[i]);
    }
    return (Item){.array = result};
}

extern "C" Item py_builtin_tuple(Item iterable) {
    return py_builtin_list(iterable); // tuples are lists internally
}

// ============================================================================
// String methods dispatcher
// ============================================================================

static Item py_str_upper(String* s) {
    char* buf = (char*)malloc(s->len + 1);
    for (int64_t i = 0; i < s->len; i++) buf[i] = toupper((uint8_t)s->chars[i]);
    buf[s->len] = '\0';
    Item result = (Item){.item = s2it(heap_strcpy(buf, s->len))};
    free(buf);
    return result;
}

static Item py_str_lower(String* s) {
    char* buf = (char*)malloc(s->len + 1);
    for (int64_t i = 0; i < s->len; i++) buf[i] = tolower((uint8_t)s->chars[i]);
    buf[s->len] = '\0';
    Item result = (Item){.item = s2it(heap_strcpy(buf, s->len))};
    free(buf);
    return result;
}

static Item py_str_strip(String* s) {
    int64_t start = 0, end = s->len;
    while (start < end && isspace((uint8_t)s->chars[start])) start++;
    while (end > start && isspace((uint8_t)s->chars[end - 1])) end--;
    return (Item){.item = s2it(heap_strcpy(s->chars + start, end - start))};
}

static Item py_str_lstrip(String* s) {
    int64_t start = 0;
    while (start < s->len && isspace((uint8_t)s->chars[start])) start++;
    return (Item){.item = s2it(heap_strcpy(s->chars + start, s->len - start))};
}

static Item py_str_rstrip(String* s) {
    int64_t end = s->len;
    while (end > 0 && isspace((uint8_t)s->chars[end - 1])) end--;
    return (Item){.item = s2it(heap_strcpy(s->chars, end))};
}

static Item py_str_split(String* s, Item* args, int argc) {
    Array* result = array();

    if (argc == 0 || get_type_id(args[0]) == LMD_TYPE_NULL) {
        // split on whitespace
        int64_t i = 0;
        while (i < s->len) {
            while (i < s->len && isspace((uint8_t)s->chars[i])) i++;
            if (i >= s->len) break;
            int64_t start = i;
            while (i < s->len && !isspace((uint8_t)s->chars[i])) i++;
            array_push(result, (Item){.item = s2it(heap_strcpy(s->chars + start, i - start))});
        }
    } else if (get_type_id(args[0]) == LMD_TYPE_STRING) {
        String* sep = it2s(args[0]);
        if (sep->len == 0) return (Item){.array = result};

        int64_t start = 0;
        while (start <= s->len) {
            // find next occurrence of sep
            int64_t found = -1;
            for (int64_t i = start; i <= s->len - sep->len; i++) {
                if (memcmp(s->chars + i, sep->chars, sep->len) == 0) {
                    found = i;
                    break;
                }
            }
            if (found < 0) {
                array_push(result, (Item){.item = s2it(heap_strcpy(s->chars + start, s->len - start))});
                break;
            }
            array_push(result, (Item){.item = s2it(heap_strcpy(s->chars + start, found - start))});
            start = found + sep->len;
        }
    }

    return (Item){.array = result};
}

static Item py_str_join(String* s, Item* args, int argc) {
    if (argc == 0) return (Item){.item = s2it(heap_create_name(""))};
    Item iterable = args[0];
    if (get_type_id(iterable) != LMD_TYPE_ARRAY) return (Item){.item = s2it(heap_create_name(""))};

    Array* arr = it2arr(iterable);
    if (!arr || arr->length == 0) return (Item){.item = s2it(heap_create_name(""))};

    // calculate total length
    size_t total = 0;
    for (int i = 0; i < arr->length; i++) {
        Item str = py_to_str(arr->items[i]);
        if (get_type_id(str) == LMD_TYPE_STRING) {
            total += it2s(str)->len;
        }
        if (i > 0) total += s->len;
    }

    char* buf = (char*)malloc(total + 1);
    size_t pos = 0;
    for (int i = 0; i < arr->length; i++) {
        if (i > 0 && s->len > 0) {
            memcpy(buf + pos, s->chars, s->len);
            pos += s->len;
        }
        Item str = py_to_str(arr->items[i]);
        if (get_type_id(str) == LMD_TYPE_STRING) {
            String* part = it2s(str);
            memcpy(buf + pos, part->chars, part->len);
            pos += part->len;
        }
    }
    buf[pos] = '\0';

    Item result = (Item){.item = s2it(heap_strcpy(buf, pos))};
    free(buf);
    return result;
}

static Item py_str_replace(String* s, Item* args, int argc) {
    if (argc < 2) return (Item){.item = s2it(s)};
    if (get_type_id(args[0]) != LMD_TYPE_STRING || get_type_id(args[1]) != LMD_TYPE_STRING) {
        return (Item){.item = s2it(s)};
    }

    String* old_str = it2s(args[0]);
    String* new_str = it2s(args[1]);
    if (old_str->len == 0) return (Item){.item = s2it(s)};

    // count occurrences
    int count = 0;
    for (int64_t i = 0; i <= s->len - old_str->len; i++) {
        if (memcmp(s->chars + i, old_str->chars, old_str->len) == 0) count++;
    }
    if (count == 0) return (Item){.item = s2it(s)};

    size_t new_len = s->len + (size_t)count * (new_str->len - old_str->len);
    char* buf = (char*)malloc(new_len + 1);
    size_t pos = 0;
    int64_t i = 0;
    while (i < s->len) {
        if (i <= s->len - old_str->len &&
            memcmp(s->chars + i, old_str->chars, old_str->len) == 0) {
            memcpy(buf + pos, new_str->chars, new_str->len);
            pos += new_str->len;
            i += old_str->len;
        } else {
            buf[pos++] = s->chars[i++];
        }
    }
    buf[pos] = '\0';

    Item result = (Item){.item = s2it(heap_strcpy(buf, pos))};
    free(buf);
    return result;
}

static Item py_str_find(String* s, Item* args, int argc) {
    if (argc == 0 || get_type_id(args[0]) != LMD_TYPE_STRING) return (Item){.item = i2it(-1)};
    String* sub = it2s(args[0]);
    if (sub->len > s->len) return (Item){.item = i2it(-1)};

    for (int64_t i = 0; i <= s->len - sub->len; i++) {
        if (memcmp(s->chars + i, sub->chars, sub->len) == 0) {
            return (Item){.item = i2it(i)};
        }
    }
    return (Item){.item = i2it(-1)};
}

static Item py_str_startswith(String* s, Item* args, int argc) {
    if (argc == 0 || get_type_id(args[0]) != LMD_TYPE_STRING) return (Item){.item = ITEM_FALSE};
    String* prefix = it2s(args[0]);
    if (prefix->len > s->len) return (Item){.item = ITEM_FALSE};
    return (Item){.item = b2it(memcmp(s->chars, prefix->chars, prefix->len) == 0)};
}

static Item py_str_endswith(String* s, Item* args, int argc) {
    if (argc == 0 || get_type_id(args[0]) != LMD_TYPE_STRING) return (Item){.item = ITEM_FALSE};
    String* suffix = it2s(args[0]);
    if (suffix->len > s->len) return (Item){.item = ITEM_FALSE};
    return (Item){.item = b2it(memcmp(s->chars + s->len - suffix->len, suffix->chars, suffix->len) == 0)};
}

static Item py_str_count(String* s, Item* args, int argc) {
    if (argc == 0 || get_type_id(args[0]) != LMD_TYPE_STRING) return (Item){.item = i2it(0)};
    String* sub = it2s(args[0]);
    if (sub->len == 0 || sub->len > s->len) return (Item){.item = i2it(0)};

    int64_t count = 0;
    for (int64_t i = 0; i <= s->len - sub->len; i++) {
        if (memcmp(s->chars + i, sub->chars, sub->len) == 0) {
            count++;
            i += sub->len - 1; // non-overlapping
        }
    }
    return (Item){.item = i2it(count)};
}

static Item py_str_isdigit(String* s) {
    if (s->len == 0) return (Item){.item = ITEM_FALSE};
    for (int64_t i = 0; i < s->len; i++) {
        if (!isdigit((uint8_t)s->chars[i])) return (Item){.item = ITEM_FALSE};
    }
    return (Item){.item = ITEM_TRUE};
}

static Item py_str_isalpha(String* s) {
    if (s->len == 0) return (Item){.item = ITEM_FALSE};
    for (int64_t i = 0; i < s->len; i++) {
        if (!isalpha((uint8_t)s->chars[i])) return (Item){.item = ITEM_FALSE};
    }
    return (Item){.item = ITEM_TRUE};
}

extern "C" Item py_string_method(Item str_item, Item method_name, Item* args, int argc) {
    if (get_type_id(str_item) != LMD_TYPE_STRING || get_type_id(method_name) != LMD_TYPE_STRING) {
        return ItemNull;
    }

    String* s = it2s(str_item);
    String* method = it2s(method_name);
    if (!s || !method) return ItemNull;

    if (strcmp(method->chars, "upper") == 0) return py_str_upper(s);
    if (strcmp(method->chars, "lower") == 0) return py_str_lower(s);
    if (strcmp(method->chars, "strip") == 0) return py_str_strip(s);
    if (strcmp(method->chars, "lstrip") == 0) return py_str_lstrip(s);
    if (strcmp(method->chars, "rstrip") == 0) return py_str_rstrip(s);
    if (strcmp(method->chars, "split") == 0) return py_str_split(s, args, argc);
    if (strcmp(method->chars, "join") == 0) return py_str_join(s, args, argc);
    if (strcmp(method->chars, "replace") == 0) return py_str_replace(s, args, argc);
    if (strcmp(method->chars, "find") == 0) return py_str_find(s, args, argc);
    if (strcmp(method->chars, "startswith") == 0) return py_str_startswith(s, args, argc);
    if (strcmp(method->chars, "endswith") == 0) return py_str_endswith(s, args, argc);
    if (strcmp(method->chars, "count") == 0) return py_str_count(s, args, argc);
    if (strcmp(method->chars, "isdigit") == 0) return py_str_isdigit(s);
    if (strcmp(method->chars, "isalpha") == 0) return py_str_isalpha(s);
    if (strcmp(method->chars, "title") == 0) {
        char* buf = (char*)malloc(s->len + 1);
        bool next_upper = true;
        for (int64_t i = 0; i < s->len; i++) {
            if (isspace((uint8_t)s->chars[i]) || !isalpha((uint8_t)s->chars[i])) {
                buf[i] = s->chars[i];
                next_upper = true;
            } else if (next_upper) {
                buf[i] = toupper((uint8_t)s->chars[i]);
                next_upper = false;
            } else {
                buf[i] = tolower((uint8_t)s->chars[i]);
            }
        }
        buf[s->len] = '\0';
        Item result = (Item){.item = s2it(heap_strcpy(buf, s->len))};
        free(buf);
        return result;
    }
    if (strcmp(method->chars, "capitalize") == 0) {
        char* buf = (char*)malloc(s->len + 1);
        for (int64_t i = 0; i < s->len; i++) {
            buf[i] = (i == 0) ? toupper((uint8_t)s->chars[i]) : tolower((uint8_t)s->chars[i]);
        }
        buf[s->len] = '\0';
        Item result = (Item){.item = s2it(heap_strcpy(buf, s->len))};
        free(buf);
        return result;
    }
    if (strcmp(method->chars, "format") == 0) {
        // implement str.format() with {} positional and {N} indexed replacement
        StrBuf* sb = strbuf_new();
        int auto_idx = 0;
        const char* p = s->chars;
        const char* end = p + s->len;

        while (p < end) {
            if (*p == '{') {
                if (p + 1 < end && *(p + 1) == '{') {
                    // escaped {{ → literal {
                    strbuf_append_char(sb, '{');
                    p += 2;
                    continue;
                }
                p++; // skip '{'
                // parse field: optional index/name, optional :format_spec
                int idx = -1;
                const char* spec_start = NULL;
                int spec_len = 0;

                if (p < end && *p >= '0' && *p <= '9') {
                    // explicit index {N}
                    idx = 0;
                    while (p < end && *p >= '0' && *p <= '9') {
                        idx = idx * 10 + (*p - '0');
                        p++;
                    }
                } else if (p < end && *p == ':') {
                    // auto-index with format spec
                    idx = auto_idx++;
                } else if (p < end && *p == '}') {
                    // auto-index {}
                    idx = auto_idx++;
                }

                // check for format spec
                if (p < end && *p == ':') {
                    p++; // skip ':'
                    spec_start = p;
                    while (p < end && *p != '}') p++;
                    spec_len = (int)(p - spec_start);
                }

                if (p < end && *p == '}') p++; // skip '}'

                // get argument
                Item arg = (idx >= 0 && idx < argc) ? args[idx] : ItemNull;

                // apply format spec if present
                if (spec_start && spec_len > 0) {
                    char spec[64];
                    int slen = spec_len < 63 ? spec_len : 63;
                    memcpy(spec, spec_start, slen);
                    spec[slen] = '\0';

                    // parse format spec: [[fill]align][sign][#][0][width][grouping_option][.precision][type]
                    char fill = ' ';
                    char align = '\0';
                    int width = 0;
                    int precision = -1;
                    char type = '\0';
                    const char* sp = spec;

                    // check for fill+align or just align
                    if (slen >= 2 && (sp[1] == '<' || sp[1] == '>' || sp[1] == '^')) {
                        fill = sp[0];
                        align = sp[1];
                        sp += 2;
                    } else if (slen >= 1 && (sp[0] == '<' || sp[0] == '>' || sp[0] == '^')) {
                        align = sp[0];
                        sp++;
                    }

                    // check for 0-padding
                    bool zero_pad = false;
                    if (*sp == '0') { zero_pad = true; sp++; if (!align) { fill = '0'; align = '>'; } }

                    // width
                    while (*sp >= '0' && *sp <= '9') { width = width * 10 + (*sp - '0'); sp++; }

                    // precision
                    if (*sp == '.') {
                        sp++;
                        precision = 0;
                        while (*sp >= '0' && *sp <= '9') { precision = precision * 10 + (*sp - '0'); sp++; }
                    }

                    // type
                    if (*sp) type = *sp;

                    char buf[128];
                    const char* formatted = buf;
                    int flen = 0;

                    if (type == 'd' || type == 'n') {
                        int64_t val = (get_type_id(arg) == LMD_TYPE_INT) ? it2i(arg) : (int64_t)py_get_number(arg);
                        flen = snprintf(buf, sizeof(buf), "%lld", (long long)val);
                    } else if (type == 'f' || type == 'F') {
                        double val = py_get_number(arg);
                        if (precision < 0) precision = 6;
                        flen = snprintf(buf, sizeof(buf), "%.*f", precision, val);
                    } else if (type == 'e' || type == 'E') {
                        double val = py_get_number(arg);
                        if (precision < 0) precision = 6;
                        flen = snprintf(buf, sizeof(buf), type == 'e' ? "%.*e" : "%.*E", precision, val);
                    } else if (type == 'x' || type == 'X') {
                        int64_t val = (get_type_id(arg) == LMD_TYPE_INT) ? it2i(arg) : (int64_t)py_get_number(arg);
                        flen = snprintf(buf, sizeof(buf), type == 'x' ? "%llx" : "%llX", (long long)val);
                    } else if (type == 'o') {
                        int64_t val = (get_type_id(arg) == LMD_TYPE_INT) ? it2i(arg) : (int64_t)py_get_number(arg);
                        flen = snprintf(buf, sizeof(buf), "%llo", (long long)val);
                    } else if (type == 'b') {
                        int64_t val = (get_type_id(arg) == LMD_TYPE_INT) ? it2i(arg) : (int64_t)py_get_number(arg);
                        // binary format
                        if (val == 0) { buf[0] = '0'; flen = 1; }
                        else {
                            uint64_t v = val < 0 ? (uint64_t)(-val) : (uint64_t)val;
                            int pos = 0;
                            char tmp[66];
                            while (v > 0) { tmp[pos++] = '0' + (v & 1); v >>= 1; }
                            if (val < 0) { buf[0] = '-'; flen = 1; }
                            else flen = 0;
                            for (int j = pos - 1; j >= 0; j--) buf[flen++] = tmp[j];
                        }
                        buf[flen] = '\0';
                    } else {
                        // default: string conversion, apply precision as max width
                        Item str_val = py_to_str(arg);
                        String* sv = it2s(str_val);
                        if (sv) {
                            formatted = sv->chars;
                            flen = (precision >= 0 && precision < (int)sv->len) ? precision : sv->len;
                        }
                    }

                    // apply width/alignment padding
                    if (width > 0 && flen < width) {
                        if (!align) align = (type == 'd' || type == 'f' || type == 'e' || type == 'x' || type == 'o' || type == 'b') ? '>' : '<';
                        int pad = width - flen;
                        if (align == '<') {
                            strbuf_append_str_n(sb, formatted, flen);
                            for (int j = 0; j < pad; j++) strbuf_append_char(sb, fill);
                        } else if (align == '^') {
                            int left_pad = pad / 2;
                            int right_pad = pad - left_pad;
                            for (int j = 0; j < left_pad; j++) strbuf_append_char(sb, fill);
                            strbuf_append_str_n(sb, formatted, flen);
                            for (int j = 0; j < right_pad; j++) strbuf_append_char(sb, fill);
                        } else { // '>'
                            for (int j = 0; j < pad; j++) strbuf_append_char(sb, fill);
                            strbuf_append_str_n(sb, formatted, flen);
                        }
                    } else {
                        strbuf_append_str_n(sb, formatted, flen);
                    }
                } else {
                    // no format spec: just convert to string
                    Item str_val = py_to_str(arg);
                    if (get_type_id(str_val) == LMD_TYPE_STRING) {
                        String* sv = it2s(str_val);
                        strbuf_append_str_n(sb, sv->chars, sv->len);
                    }
                }
            } else if (*p == '}' && p + 1 < end && *(p + 1) == '}') {
                // escaped }} → literal }
                strbuf_append_char(sb, '}');
                p += 2;
            } else {
                strbuf_append_char(sb, *p++);
            }
        }

        Item result = (Item){.item = s2it(heap_create_name(sb->str ? sb->str : ""))};
        strbuf_free(sb);
        return result;
    }

    if (strcmp(method->chars, "index") == 0) {
        if (argc == 0 || get_type_id(args[0]) != LMD_TYPE_STRING) return (Item){.item = i2it(-1)};
        String* sub = it2s(args[0]);
        if (sub->len > s->len) {
            log_error("py: ValueError: substring not found");
            return (Item){.item = i2it(-1)};
        }
        for (int64_t i = 0; i <= s->len - sub->len; i++) {
            if (memcmp(s->chars + i, sub->chars, sub->len) == 0)
                return (Item){.item = i2it(i)};
        }
        log_error("py: ValueError: substring not found");
        return (Item){.item = i2it(-1)};
    }
    if (strcmp(method->chars, "rfind") == 0) {
        if (argc == 0 || get_type_id(args[0]) != LMD_TYPE_STRING) return (Item){.item = i2it(-1)};
        String* sub = it2s(args[0]);
        if (sub->len > s->len) return (Item){.item = i2it(-1)};
        for (int64_t i = s->len - sub->len; i >= 0; i--) {
            if (memcmp(s->chars + i, sub->chars, sub->len) == 0)
                return (Item){.item = i2it(i)};
        }
        return (Item){.item = i2it(-1)};
    }
    if (strcmp(method->chars, "splitlines") == 0) {
        Array* result = array();
        int64_t start = 0;
        for (int64_t i = 0; i <= s->len; i++) {
            if (i == s->len || s->chars[i] == '\n') {
                int64_t end = i;
                if (end > start && s->chars[end - 1] == '\r') end--;
                array_push(result, (Item){.item = s2it(heap_strcpy(s->chars + start, end - start))});
                start = i + 1;
            }
        }
        return (Item){.array = result};
    }
    if (strcmp(method->chars, "isalnum") == 0) {
        if (s->len == 0) return (Item){.item = ITEM_FALSE};
        for (int64_t i = 0; i < s->len; i++) {
            if (!isalnum((uint8_t)s->chars[i])) return (Item){.item = ITEM_FALSE};
        }
        return (Item){.item = ITEM_TRUE};
    }
    if (strcmp(method->chars, "isspace") == 0) {
        if (s->len == 0) return (Item){.item = ITEM_FALSE};
        for (int64_t i = 0; i < s->len; i++) {
            if (!isspace((uint8_t)s->chars[i])) return (Item){.item = ITEM_FALSE};
        }
        return (Item){.item = ITEM_TRUE};
    }
    if (strcmp(method->chars, "islower") == 0) {
        if (s->len == 0) return (Item){.item = ITEM_FALSE};
        bool has_cased = false;
        for (int64_t i = 0; i < s->len; i++) {
            if (isalpha((uint8_t)s->chars[i])) {
                has_cased = true;
                if (!islower((uint8_t)s->chars[i])) return (Item){.item = ITEM_FALSE};
            }
        }
        return (Item){.item = b2it(has_cased)};
    }
    if (strcmp(method->chars, "isupper") == 0) {
        if (s->len == 0) return (Item){.item = ITEM_FALSE};
        bool has_cased = false;
        for (int64_t i = 0; i < s->len; i++) {
            if (isalpha((uint8_t)s->chars[i])) {
                has_cased = true;
                if (!isupper((uint8_t)s->chars[i])) return (Item){.item = ITEM_FALSE};
            }
        }
        return (Item){.item = b2it(has_cased)};
    }
    if (strcmp(method->chars, "swapcase") == 0) {
        char* buf = (char*)malloc(s->len + 1);
        for (int64_t i = 0; i < s->len; i++) {
            if (islower((uint8_t)s->chars[i])) buf[i] = toupper((uint8_t)s->chars[i]);
            else if (isupper((uint8_t)s->chars[i])) buf[i] = tolower((uint8_t)s->chars[i]);
            else buf[i] = s->chars[i];
        }
        buf[s->len] = '\0';
        Item result = (Item){.item = s2it(heap_strcpy(buf, s->len))};
        free(buf);
        return result;
    }
    if (strcmp(method->chars, "center") == 0) {
        if (argc == 0 || get_type_id(args[0]) != LMD_TYPE_INT) return (Item){.item = s2it(s)};
        int64_t width = it2i(args[0]);
        if (width <= s->len) return (Item){.item = s2it(s)};
        char fill = ' ';
        if (argc >= 2 && get_type_id(args[1]) == LMD_TYPE_STRING) {
            String* fc = it2s(args[1]);
            if (fc && fc->len > 0) fill = fc->chars[0];
        }
        int64_t pad = width - s->len;
        int64_t left = pad / 2;
        int64_t right = pad - left;
        char* buf = (char*)malloc(width + 1);
        for (int64_t i = 0; i < left; i++) buf[i] = fill;
        memcpy(buf + left, s->chars, s->len);
        for (int64_t i = 0; i < right; i++) buf[left + s->len + i] = fill;
        buf[width] = '\0';
        Item result = (Item){.item = s2it(heap_strcpy(buf, width))};
        free(buf);
        return result;
    }
    if (strcmp(method->chars, "ljust") == 0) {
        if (argc == 0 || get_type_id(args[0]) != LMD_TYPE_INT) return (Item){.item = s2it(s)};
        int64_t width = it2i(args[0]);
        if (width <= s->len) return (Item){.item = s2it(s)};
        char fill = ' ';
        if (argc >= 2 && get_type_id(args[1]) == LMD_TYPE_STRING) {
            String* fc = it2s(args[1]);
            if (fc && fc->len > 0) fill = fc->chars[0];
        }
        char* buf = (char*)malloc(width + 1);
        memcpy(buf, s->chars, s->len);
        for (int64_t i = s->len; i < width; i++) buf[i] = fill;
        buf[width] = '\0';
        Item result = (Item){.item = s2it(heap_strcpy(buf, width))};
        free(buf);
        return result;
    }
    if (strcmp(method->chars, "rjust") == 0) {
        if (argc == 0 || get_type_id(args[0]) != LMD_TYPE_INT) return (Item){.item = s2it(s)};
        int64_t width = it2i(args[0]);
        if (width <= s->len) return (Item){.item = s2it(s)};
        char fill = ' ';
        if (argc >= 2 && get_type_id(args[1]) == LMD_TYPE_STRING) {
            String* fc = it2s(args[1]);
            if (fc && fc->len > 0) fill = fc->chars[0];
        }
        int64_t pad = width - s->len;
        char* buf = (char*)malloc(width + 1);
        for (int64_t i = 0; i < pad; i++) buf[i] = fill;
        memcpy(buf + pad, s->chars, s->len);
        buf[width] = '\0';
        Item result = (Item){.item = s2it(heap_strcpy(buf, width))};
        free(buf);
        return result;
    }
    if (strcmp(method->chars, "zfill") == 0) {
        if (argc == 0 || get_type_id(args[0]) != LMD_TYPE_INT) return (Item){.item = s2it(s)};
        int64_t width = it2i(args[0]);
        if (width <= s->len) return (Item){.item = s2it(s)};
        int64_t pad = width - s->len;
        char* buf = (char*)malloc(width + 1);
        int64_t start = 0;
        if (s->len > 0 && (s->chars[0] == '+' || s->chars[0] == '-')) {
            buf[0] = s->chars[0];
            start = 1;
            for (int64_t i = 0; i < pad; i++) buf[start + i] = '0';
            memcpy(buf + start + pad, s->chars + 1, s->len - 1);
        } else {
            for (int64_t i = 0; i < pad; i++) buf[i] = '0';
            memcpy(buf + pad, s->chars, s->len);
        }
        buf[width] = '\0';
        Item result = (Item){.item = s2it(heap_strcpy(buf, width))};
        free(buf);
        return result;
    }

    log_debug("py: str.%s() not implemented", method->chars);
    return ItemNull;
}

// ============================================================================
// List methods dispatcher
// ============================================================================

extern "C" Item py_list_method(Item list_item, Item method_name, Item* args, int argc) {
    if (get_type_id(list_item) != LMD_TYPE_ARRAY || get_type_id(method_name) != LMD_TYPE_STRING) {
        return ItemNull;
    }

    Array* arr = it2arr(list_item);
    String* method = it2s(method_name);
    if (!arr || !method) return ItemNull;

    if (strcmp(method->chars, "append") == 0 && argc >= 1) {
        array_push(arr, args[0]);
        return ItemNull;
    }
    if (strcmp(method->chars, "extend") == 0 && argc >= 1) {
        if (get_type_id(args[0]) == LMD_TYPE_ARRAY) {
            Array* other = it2arr(args[0]);
            for (int i = 0; i < other->length; i++) {
                array_push(arr, other->items[i]);
            }
        }
        return ItemNull;
    }
    if (strcmp(method->chars, "insert") == 0 && argc >= 2) {
        int64_t idx = (get_type_id(args[0]) == LMD_TYPE_INT) ? it2i(args[0]) : 0;
        if (idx < 0) idx += arr->length;
        if (idx < 0) idx = 0;
        if (idx > arr->length) idx = arr->length;
        // shift elements right
        array_push(arr, ItemNull); // expand
        for (int i = arr->length - 1; i > idx; i--) {
            arr->items[i] = arr->items[i - 1];
        }
        arr->items[idx] = args[1];
        return ItemNull;
    }
    if (strcmp(method->chars, "pop") == 0) {
        if (arr->length == 0) return ItemNull;
        int64_t idx = arr->length - 1;
        if (argc >= 1 && get_type_id(args[0]) == LMD_TYPE_INT) {
            idx = it2i(args[0]);
            if (idx < 0) idx += arr->length;
        }
        if (idx < 0 || idx >= arr->length) return ItemNull;
        Item val = arr->items[idx];
        // shift left
        for (int i = (int)idx; i < arr->length - 1; i++) {
            arr->items[i] = arr->items[i + 1];
        }
        arr->length--;
        return val;
    }
    if (strcmp(method->chars, "remove") == 0 && argc >= 1) {
        for (int i = 0; i < arr->length; i++) {
            if (it2b(py_eq(arr->items[i], args[0]))) {
                for (int j = i; j < arr->length - 1; j++) {
                    arr->items[j] = arr->items[j + 1];
                }
                arr->length--;
                return ItemNull;
            }
        }
        return ItemNull;
    }
    if (strcmp(method->chars, "clear") == 0) {
        arr->length = 0;
        return ItemNull;
    }
    if (strcmp(method->chars, "index") == 0 && argc >= 1) {
        for (int i = 0; i < arr->length; i++) {
            if (it2b(py_eq(arr->items[i], args[0]))) {
                return (Item){.item = i2it(i)};
            }
        }
        return (Item){.item = i2it(-1)};
    }
    if (strcmp(method->chars, "count") == 0 && argc >= 1) {
        int64_t count = 0;
        for (int i = 0; i < arr->length; i++) {
            if (it2b(py_eq(arr->items[i], args[0]))) count++;
        }
        return (Item){.item = i2it(count)};
    }
    if (strcmp(method->chars, "reverse") == 0) {
        for (int i = 0; i < arr->length / 2; i++) {
            Item tmp = arr->items[i];
            arr->items[i] = arr->items[arr->length - 1 - i];
            arr->items[arr->length - 1 - i] = tmp;
        }
        return ItemNull;
    }
    if (strcmp(method->chars, "sort") == 0) {
        // insertion sort in-place
        for (int i = 1; i < arr->length; i++) {
            Item key = arr->items[i];
            int j = i - 1;
            while (j >= 0 && it2b(py_lt(key, arr->items[j]))) {
                arr->items[j + 1] = arr->items[j];
                j--;
            }
            arr->items[j + 1] = key;
        }
        return ItemNull;
    }
    if (strcmp(method->chars, "copy") == 0) {
        Array* copy = array();
        for (int i = 0; i < arr->length; i++) {
            array_push(copy, arr->items[i]);
        }
        return (Item){.array = copy};
    }

    log_debug("py: list.%s() not implemented", method->chars);
    return ItemNull;
}

// ============================================================================
// Dict methods dispatcher
// ============================================================================

extern "C" Item py_dict_method(Item dict_item, Item method_name, Item* args, int argc) {
    if (get_type_id(dict_item) != LMD_TYPE_MAP || get_type_id(method_name) != LMD_TYPE_STRING) {
        return ItemNull;
    }

    Map* m = it2map(dict_item);
    String* method = it2s(method_name);
    if (!m || !method || !m->type) return ItemNull;

    TypeMap* tm = (TypeMap*)m->type;
    if (strcmp(method->chars, "keys") == 0) {
        Array* result = array();
        ShapeEntry* field = tm->shape;
        while (field) {
            if (field->name) array_push(result, (Item){.item = s2it(heap_create_name(field->name->str))});
            field = field->next;
        }
        return (Item){.array = result};
    }
    if (strcmp(method->chars, "values") == 0) {
        Array* result = array();
        ShapeEntry* field = tm->shape;
        while (field) {
            array_push(result, _map_read_field(field, m->data));
            field = field->next;
        }
        return (Item){.array = result};
    }
    if (strcmp(method->chars, "items") == 0) {
        Array* result = array();
        ShapeEntry* field = tm->shape;
        while (field) {
            Item tuple = py_tuple_new(2);
            if (field->name) py_tuple_set(tuple, 0, (Item){.item = s2it(heap_create_name(field->name->str))});
            py_tuple_set(tuple, 1, _map_read_field(field, m->data));
            array_push(result, tuple);
            field = field->next;
        }
        return (Item){.array = result};
    }
    if (strcmp(method->chars, "get") == 0 && argc >= 1) {
        if (get_type_id(args[0]) != LMD_TYPE_STRING) return (argc >= 2) ? args[1] : ItemNull;
        String* key = it2s(args[0]);
        bool found = false;
        Item val = _map_get(tm, m->data, key->chars, &found);
        if (found) return val;
        return (argc >= 2) ? args[1] : ItemNull;
    }
    if (strcmp(method->chars, "update") == 0 && argc >= 1) {
        if (get_type_id(args[0]) == LMD_TYPE_MAP) {
            Map* other = it2map(args[0]);
            if (other && other->type) {
                TypeMap* otm = (TypeMap*)other->type;
                ShapeEntry* field = otm->shape;
                while (field) {
                    Item val = _map_read_field(field, other->data);
                    if (field->name) {
                        String* key_name = heap_create_name(field->name->str);
                        if (py_input) map_put(m, key_name, val, py_input);
                    }
                    field = field->next;
                }
            }
        }
        return ItemNull;
    }
    if (strcmp(method->chars, "pop") == 0 && argc >= 1) {
        if (get_type_id(args[0]) != LMD_TYPE_STRING) {
            return (argc >= 2) ? args[1] : ItemNull;
        }
        String* key = it2s(args[0]);
        bool found = false;
        Item val = _map_get(tm, m->data, key->chars, &found);
        // NOTE: we don't actually remove the key (would need shape manipulation)
        return found ? val : ((argc >= 2) ? args[1] : ItemNull);
    }
    if (strcmp(method->chars, "clear") == 0) {
        m->type = &EmptyMap;
        m->data = NULL;
        return ItemNull;
    }
    if (strcmp(method->chars, "copy") == 0) {
        // shallow copy via walking shape entries
        Item new_dict = py_dict_new();
        Map* nm = it2map(new_dict);
        ShapeEntry* field = tm->shape;
        while (field) {
            Item val = _map_read_field(field, m->data);
            if (field->name && py_input) {
                String* key_name = heap_create_name(field->name->str);
                map_put(nm, key_name, val, py_input);
            }
            field = field->next;
        }
        return new_dict;
    }
    if (strcmp(method->chars, "setdefault") == 0 && argc >= 1) {
        if (get_type_id(args[0]) != LMD_TYPE_STRING) return (argc >= 2) ? args[1] : ItemNull;
        String* key = it2s(args[0]);
        bool found = false;
        Item val = _map_get(tm, m->data, key->chars, &found);
        if (found) return val;
        Item def_val = (argc >= 2) ? args[1] : ItemNull;
        if (py_input) map_put(m, heap_create_name(key->chars), def_val, py_input);
        return def_val;
    }
    if (strcmp(method->chars, "popitem") == 0) {
        // return last key-value pair
        ShapeEntry* field = tm->shape;
        ShapeEntry* last = NULL;
        while (field) { last = field; field = field->next; }
        if (!last || !last->name) return ItemNull;
        Item tuple = py_tuple_new(2);
        py_tuple_set(tuple, 0, (Item){.item = s2it(heap_create_name(last->name->str))});
        py_tuple_set(tuple, 1, _map_read_field(last, m->data));
        return tuple;
    }

    log_debug("py: dict.%s() not implemented", method->chars);
    return ItemNull;
}
