/**
 * Python Runtime Functions for Lambda
 *
 * Implements Python semantics on top of Lambda's type system.
 * All functions are callable from MIR JIT compiled code.
 */
#include "py_runtime.h"
#include "py_class.h"
#include "../lambda-data.hpp"
#include "../transpiler.hpp"
#include "../../lib/log.h"
#include "../../lib/hashmap.h"
#include "../../lib/strbuf.h"
#include <cstring>
#include <cmath>
#include <cstdlib>
#include <cstdio>

// Global Input context for Python runtime (for map_put, pool allocations)
Input* py_input = NULL;

extern Item _map_read_field(ShapeEntry* field, void* map_data);
extern "C" Item py_builtin_repr(Item obj);

// Forward declarations from lambda-data-runtime.cpp
extern Item _map_read_field(ShapeEntry* field, void* map_data);
extern Item _map_get(TypeMap* map_type, void* map_data, char* key, bool* is_found);

extern TypeMap EmptyMap;

// Exception handling state
static bool py_exception_pending = false;
static Item py_exception_value = {0};

// Module variable table
#define PY_MODULE_VAR_MAX 1024
static Item py_module_vars[PY_MODULE_VAR_MAX];
static int py_module_var_count = 0;

// Stop iteration sentinel
static Item py_stop_iteration_sentinel = {0};
static bool py_stop_iteration_initialized = false;

// ============================================================================
// Runtime initialization
// ============================================================================

extern "C" void py_runtime_set_input(void* input) {
    py_input = (Input*)input;
    py_init_builtin_classes();
}

// ============================================================================
// Type conversion
// ============================================================================

extern "C" Item py_to_int(Item value) {
    TypeId type = get_type_id(value);

    switch (type) {
    case LMD_TYPE_NULL:
        // Python: int(None) raises TypeError, but we return 0 for simplicity
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

extern "C" Item py_to_float(Item value) {
    TypeId type = get_type_id(value);

    switch (type) {
    case LMD_TYPE_NULL:
        return (Item){.item = i2it(0)};
    case LMD_TYPE_BOOL: {
        double* ptr = (double*)heap_alloc(sizeof(double), LMD_TYPE_FLOAT);
        *ptr = it2b(value) ? 1.0 : 0.0;
        return (Item){.item = d2it(ptr)};
    }
    case LMD_TYPE_INT: {
        double* ptr = (double*)heap_alloc(sizeof(double), LMD_TYPE_FLOAT);
        *ptr = (double)it2i(value);
        return (Item){.item = d2it(ptr)};
    }
    case LMD_TYPE_FLOAT:
        return value;
    case LMD_TYPE_STRING: {
        String* str = it2s(value);
        if (!str || str->len == 0) return (Item){.item = i2it(0)};
        char* endptr;
        double d = strtod(str->chars, &endptr);
        if (endptr == str->chars) return (Item){.item = i2it(0)};
        double* ptr = (double*)heap_alloc(sizeof(double), LMD_TYPE_FLOAT);
        *ptr = d;
        return (Item){.item = d2it(ptr)};
    }
    default:
        return (Item){.item = i2it(0)};
    }
}

extern "C" Item py_to_str(Item value) {
    TypeId type = get_type_id(value);

    switch (type) {
    case LMD_TYPE_NULL:
        return (Item){.item = s2it(heap_create_name("None"))};
    case LMD_TYPE_BOOL:
        return (Item){.item = s2it(heap_create_name(it2b(value) ? "True" : "False"))};
    case LMD_TYPE_INT: {
        char buffer[32];
        snprintf(buffer, sizeof(buffer), "%lld", (long long)it2i(value));
        return (Item){.item = s2it(heap_create_name(buffer))};
    }
    case LMD_TYPE_FLOAT: {
        double d = it2d(value);
        if (isnan(d)) return (Item){.item = s2it(heap_create_name("nan"))};
        if (isinf(d)) return (Item){.item = s2it(heap_create_name(d > 0 ? "inf" : "-inf"))};
        char buffer[64];
        // Python shows .0 for whole numbers
        if (d == (double)(int64_t)d && isfinite(d)) {
            snprintf(buffer, sizeof(buffer), "%.1f", d);
        } else {
            snprintf(buffer, sizeof(buffer), "%.15g", d);
        }
        return (Item){.item = s2it(heap_create_name(buffer))};
    }
    case LMD_TYPE_STRING:
        return value;
    case LMD_TYPE_ARRAY: {
        Array* arr = it2arr(value);
        if (!arr) return (Item){.item = s2it(heap_create_name("[]"))};
        StrBuf* sb = strbuf_new();
        strbuf_append_char(sb, '[');
        for (int i = 0; i < arr->length; i++) {
            if (i > 0) strbuf_append_str(sb, ", ");
            Item elem_str = py_builtin_repr(arr->items[i]);
            if (get_type_id(elem_str) == LMD_TYPE_STRING) {
                String* s = it2s(elem_str);
                strbuf_append_str_n(sb, s->chars, s->len);
            }
        }
        strbuf_append_char(sb, ']');
        Item arr_result = (Item){.item = s2it(heap_create_name(sb->str))};
        strbuf_free(sb);
        return arr_result;
    }
    case LMD_TYPE_MAP: {
        // class instance: call __str__ dunder if available
        if (py_is_instance(value)) {
            Item str_method = py_getattr(value,
                (Item){.item = s2it(heap_create_name("__str__"))});
            if (get_type_id(str_method) != LMD_TYPE_NULL)
                return py_call_function(str_method, NULL, 0);
        }
        // class object: show <class 'Name'>
        if (py_is_class(value)) {
            Item cls_name = py_map_get_cstr(value, "__name__");
            if (get_type_id(cls_name) == LMD_TYPE_STRING) {
                String* n = it2s(cls_name);
                char buf[256];
                snprintf(buf, sizeof(buf), "<class '%.*s'>", (int)n->len, n->chars);
                return (Item){.item = s2it(heap_create_name(buf))};
            }
        }
        // plain map / dict: render as {k: v, ...} skipping __ fields
        Map* m = it2map(value);
        if (!m || !m->type) return (Item){.item = s2it(heap_create_name("{}"))};
        TypeMap* tm = (TypeMap*)m->type;
        StrBuf* sb = strbuf_new();
        strbuf_append_char(sb, '{');
        int idx = 0;
        ShapeEntry* field = tm->shape;
        while (field) {
            if (field->name && field->name->length >= 2 &&
                field->name->str[0] == '_' && field->name->str[1] == '_') {
                field = field->next;
                continue;
            }
            if (idx > 0) strbuf_append_str(sb, ", ");
            if (field->name)
                strbuf_append_str_n(sb, field->name->str, field->name->length);
            strbuf_append_str(sb, ": ");
            Item val = _map_read_field(field, m->data);
            Item val_str = py_builtin_repr(val);
            if (get_type_id(val_str) == LMD_TYPE_STRING) {
                String* s = it2s(val_str);
                strbuf_append_str_n(sb, s->chars, s->len);
            }
            field = field->next;
            idx++;
        }
        strbuf_append_char(sb, '}');
        Item map_result = (Item){.item = s2it(heap_create_name(sb->str))};
        strbuf_free(sb);
        return map_result;
    }
    case LMD_TYPE_FUNC:
        return (Item){.item = s2it(heap_create_name("<function>"))};
    default:
        return (Item){.item = s2it(heap_create_name("<object>"))};
    }
}

extern "C" Item py_to_bool(Item value) {
    return (Item){.item = b2it(py_is_truthy(value))};
}

extern "C" bool py_is_truthy(Item value) {
    TypeId type = get_type_id(value);

    switch (type) {
    case LMD_TYPE_NULL:
        return false;
    case LMD_TYPE_BOOL:
        return it2b(value);
    case LMD_TYPE_INT:
        return it2i(value) != 0;
    case LMD_TYPE_FLOAT: {
        double d = it2d(value);
        return d != 0.0 && !isnan(d);
    }
    case LMD_TYPE_STRING: {
        String* str = it2s(value);
        return str && str->len > 0;
    }
    case LMD_TYPE_ARRAY: {
        Array* arr = it2arr(value);
        return arr && arr->length > 0;
    }
    case LMD_TYPE_MAP: {
        Map* m = it2map(value);
        if (!m || !m->type) return false;
        // class instances: try __bool__ then __len__
        if (py_is_instance(value)) {
            Item bool_m = py_getattr(value,
                (Item){.item = s2it(heap_create_name("__bool__"))});
            if (get_type_id(bool_m) != LMD_TYPE_NULL) {
                Item r = py_call_function(bool_m, NULL, 0);
                if (get_type_id(r) == LMD_TYPE_BOOL) return it2b(r);
                if (get_type_id(r) == LMD_TYPE_INT)  return it2i(r) != 0;
            }
            Item len_m = py_getattr(value,
                (Item){.item = s2it(heap_create_name("__len__"))});
            if (get_type_id(len_m) != LMD_TYPE_NULL) {
                Item r = py_call_function(len_m, NULL, 0);
                if (get_type_id(r) == LMD_TYPE_INT) return it2i(r) != 0;
            }
            return true;  // instances are truthy by default
        }
        // plain dict: empty is falsy
        return ((TypeMap*)m->type)->field_count > 0;
    }
    default:
        return value.item != 0;
    }
}

// ============================================================================
// Helper: make numeric Item (int or float)
// ============================================================================

static Item py_make_number(double d) {
    if (isfinite(d) && d == (double)(int64_t)d && d >= INT56_MIN && d <= INT56_MAX) {
        return (Item){.item = i2it((int64_t)d)};
    }
    double* ptr = (double*)heap_alloc(sizeof(double), LMD_TYPE_FLOAT);
    *ptr = d;
    return (Item){.item = d2it(ptr)};
}

double py_get_number(Item value) {
    TypeId type = get_type_id(value);
    switch (type) {
    case LMD_TYPE_INT: return (double)it2i(value);
    case LMD_TYPE_FLOAT: return it2d(value);
    case LMD_TYPE_BOOL: return it2b(value) ? 1.0 : 0.0;
    default: return 0.0;
    }
}

// ============================================================================
// Arithmetic operators — Python semantics
// ============================================================================

extern "C" Item py_add(Item left, Item right) {
    TypeId lt = get_type_id(left);
    TypeId rt = get_type_id(right);

    // string + string → concatenation
    if (lt == LMD_TYPE_STRING && rt == LMD_TYPE_STRING) {
        return fn_join(left, right);
    }

    // int + int → int (unless overflow)
    if (lt == LMD_TYPE_INT && rt == LMD_TYPE_INT) {
        int64_t a = it2i(left), b = it2i(right);
        int64_t r = a + b;
        if (r >= INT56_MIN && r <= INT56_MAX) {
            return (Item){.item = i2it(r)};
        }
        // overflow → float
        return py_make_number((double)a + (double)b);
    }

    // numeric addition
    // dunder __add__ / __radd__ fallback for instances
    if (lt == LMD_TYPE_MAP && py_is_instance(left)) {
        Item bm = py_getattr(left, (Item){.item = s2it(heap_create_name("__add__"))});
        if (get_type_id(bm) != LMD_TYPE_NULL)
            return py_call_function(bm, &right, 1);
    }
    if (rt == LMD_TYPE_MAP && py_is_instance(right)) {
        Item bm = py_getattr(right, (Item){.item = s2it(heap_create_name("__radd__"))});
        if (get_type_id(bm) != LMD_TYPE_NULL)
            return py_call_function(bm, &left, 1);
    }
    return fn_add(left, right);
}

extern "C" Item py_subtract(Item left, Item right) {
    TypeId lt = get_type_id(left);
    TypeId rt = get_type_id(right);

    if (lt == LMD_TYPE_INT && rt == LMD_TYPE_INT) {
        int64_t a = it2i(left), b = it2i(right);
        int64_t r = a - b;
        if (r >= INT56_MIN && r <= INT56_MAX) {
            return (Item){.item = i2it(r)};
        }
        return py_make_number((double)a - (double)b);
    }

    return fn_sub(left, right);
}

extern "C" Item py_multiply(Item left, Item right) {
    TypeId lt = get_type_id(left);
    TypeId rt = get_type_id(right);

    // string * int → repeat (Python-specific)
    if (lt == LMD_TYPE_STRING && (rt == LMD_TYPE_INT || rt == LMD_TYPE_BOOL)) {
        String* str = it2s(left);
        int64_t count = (rt == LMD_TYPE_INT) ? it2i(right) : (it2b(right) ? 1 : 0);
        if (!str || count <= 0) return (Item){.item = s2it(heap_create_name(""))};
        if (count == 1) return left;

        size_t total_len = str->len * count;
        if (total_len > 1024 * 1024) total_len = 1024 * 1024; // limit

        char* buf = (char*)malloc(total_len + 1);
        if (!buf) return left;
        size_t pos = 0;
        for (int64_t i = 0; i < count && pos + str->len <= total_len; i++) {
            memcpy(buf + pos, str->chars, str->len);
            pos += str->len;
        }
        buf[pos] = '\0';
        Item result = (Item){.item = s2it(heap_strcpy(buf, pos))};
        free(buf);
        return result;
    }

    // list * int → repeat
    if (lt == LMD_TYPE_ARRAY && (rt == LMD_TYPE_INT || rt == LMD_TYPE_BOOL)) {
        Array* arr = it2arr(left);
        int64_t count = (rt == LMD_TYPE_INT) ? it2i(right) : (it2b(right) ? 1 : 0);
        if (!arr || count <= 0) {
            Array* empty = array();
            return (Item){.array = empty};
        }

        Array* result = array();
        for (int64_t c = 0; c < count; c++) {
            for (int i = 0; i < arr->length; i++) {
                array_push(result, arr->items[i]);
            }
        }
        return (Item){.array = result};
    }

    if (lt == LMD_TYPE_INT && rt == LMD_TYPE_INT) {
        int64_t a = it2i(left), b = it2i(right);
        // check overflow
        if (b != 0 && (a > INT56_MAX / (b > 0 ? b : -b) || a < INT56_MIN / (b > 0 ? b : -b))) {
            return py_make_number((double)a * (double)b);
        }
        int64_t r = a * b;
        if (r >= INT56_MIN && r <= INT56_MAX) {
            return (Item){.item = i2it(r)};
        }
        return py_make_number((double)a * (double)b);
    }

    return fn_mul(left, right);
}

extern "C" Item py_divide(Item left, Item right) {
    // Python true division always returns float
    double l = py_get_number(left);
    double r = py_get_number(right);
    if (r == 0.0) {
        log_error("py: ZeroDivisionError: division by zero");
        py_exception_pending = true;
        py_exception_value = (Item){.item = s2it(heap_create_name("ZeroDivisionError"))};
        return ItemNull;
    }
    double* ptr = (double*)heap_alloc(sizeof(double), LMD_TYPE_FLOAT);
    *ptr = l / r;
    return (Item){.item = d2it(ptr)};
}

extern "C" Item py_floor_divide(Item left, Item right) {
    TypeId lt = get_type_id(left);
    TypeId rt = get_type_id(right);

    if (lt == LMD_TYPE_INT && rt == LMD_TYPE_INT) {
        int64_t a = it2i(left), b = it2i(right);
        if (b == 0) {
            log_error("py: ZeroDivisionError: integer floor division by zero");
            py_exception_pending = true;
            py_exception_value = (Item){.item = s2it(heap_create_name("ZeroDivisionError"))};
            return ItemNull;
        }
        // Python floor division rounds toward negative infinity
        int64_t q = a / b;
        if ((a ^ b) < 0 && a % b != 0) q--;
        return (Item){.item = i2it(q)};
    }

    double l = py_get_number(left);
    double r = py_get_number(right);
    if (r == 0.0) {
        log_error("py: ZeroDivisionError: float floor division by zero");
        py_exception_pending = true;
        py_exception_value = (Item){.item = s2it(heap_create_name("ZeroDivisionError"))};
        return ItemNull;
    }
    return py_make_number(floor(l / r));
}

// ============================================================================
// String % formatting
// ============================================================================

static Item py_string_format_percent(Item left, Item right) {
    String* fmt = it2s(left);
    if (!fmt) return left;

    // collect format arguments: single value or tuple (array) of values
    Array* args_arr = NULL;
    Item single_arg = right;
    int nargs = 1;
    if (get_type_id(right) == LMD_TYPE_ARRAY) {
        args_arr = it2arr(right);
        nargs = args_arr ? args_arr->length : 0;
    }

    StrBuf* sb = strbuf_new();
    int arg_idx = 0;
    const char* p = fmt->chars;
    const char* end = p + fmt->len;

    while (p < end) {
        if (*p != '%') {
            strbuf_append_char(sb, *p++);
            continue;
        }
        p++; // skip '%'
        if (p >= end) break;

        // %% → literal %
        if (*p == '%') {
            strbuf_append_char(sb, '%');
            p++;
            continue;
        }

        // parse optional flags: -, +, 0, space
        bool flag_minus = false, flag_zero = false, flag_plus = false;
        while (p < end && (*p == '-' || *p == '+' || *p == '0' || *p == ' ')) {
            if (*p == '-') flag_minus = true;
            else if (*p == '0') flag_zero = true;
            else if (*p == '+') flag_plus = true;
            p++;
        }

        // parse optional width
        int width = 0;
        while (p < end && *p >= '0' && *p <= '9') {
            width = width * 10 + (*p - '0');
            p++;
        }

        // parse optional precision
        int precision = -1;
        if (p < end && *p == '.') {
            p++;
            precision = 0;
            while (p < end && *p >= '0' && *p <= '9') {
                precision = precision * 10 + (*p - '0');
                p++;
            }
        }

        if (p >= end) break;
        char spec = *p++;

        // get current argument
        Item arg;
        if (args_arr) {
            arg = (arg_idx < nargs) ? args_arr->items[arg_idx] : ItemNull;
        } else {
            arg = (arg_idx == 0) ? single_arg : ItemNull;
        }
        arg_idx++;

        char buf[128];
        const char* formatted = buf;
        int flen = 0;

        switch (spec) {
        case 's': {
            Item str_val = py_to_str(arg);
            String* s = it2s(str_val);
            if (s) {
                if (precision >= 0 && precision < (int)s->len) {
                    flen = precision;
                } else {
                    flen = s->len;
                }
                formatted = s->chars;
            }
            break;
        }
        case 'r': {
            Item repr_val = py_builtin_repr(arg);
            String* s = it2s(repr_val);
            if (s) { formatted = s->chars; flen = s->len; }
            break;
        }
        case 'd':
        case 'i': {
            int64_t val = (get_type_id(arg) == LMD_TYPE_INT) ? it2i(arg) : (int64_t)py_get_number(arg);
            flen = snprintf(buf, sizeof(buf), "%lld", (long long)val);
            break;
        }
        case 'f': {
            double val = py_get_number(arg);
            if (precision < 0) precision = 6;
            flen = snprintf(buf, sizeof(buf), "%.*f", precision, val);
            break;
        }
        case 'x': {
            int64_t val = (get_type_id(arg) == LMD_TYPE_INT) ? it2i(arg) : (int64_t)py_get_number(arg);
            flen = snprintf(buf, sizeof(buf), "%llx", (long long)val);
            break;
        }
        case 'o': {
            int64_t val = (get_type_id(arg) == LMD_TYPE_INT) ? it2i(arg) : (int64_t)py_get_number(arg);
            flen = snprintf(buf, sizeof(buf), "%llo", (long long)val);
            break;
        }
        default:
            strbuf_append_char(sb, '%');
            strbuf_append_char(sb, spec);
            continue;
        }

        // apply width padding
        if (width > 0 && flen < width) {
            char pad = (flag_zero && !flag_minus) ? '0' : ' ';
            if (flag_minus) {
                strbuf_append_str_n(sb, formatted, flen);
                for (int j = flen; j < width; j++) strbuf_append_char(sb, ' ');
            } else {
                // for zero-padding, handle sign
                int start = 0;
                if (flag_zero && flen > 0 && (formatted[0] == '-' || formatted[0] == '+')) {
                    strbuf_append_char(sb, formatted[0]);
                    start = 1;
                }
                for (int j = flen; j < width; j++) strbuf_append_char(sb, pad);
                strbuf_append_str_n(sb, formatted + start, flen - start);
            }
        } else {
            strbuf_append_str_n(sb, formatted, flen);
        }
    }

    Item result = (Item){.item = s2it(heap_create_name(sb->str ? sb->str : ""))};
    strbuf_free(sb);
    return result;
}

extern "C" Item py_modulo(Item left, Item right) {
    TypeId lt = get_type_id(left);
    TypeId rt = get_type_id(right);

    // string % args → format
    if (lt == LMD_TYPE_STRING) {
        return py_string_format_percent(left, right);
    }

    if (lt == LMD_TYPE_INT && rt == LMD_TYPE_INT) {
        int64_t a = it2i(left), b = it2i(right);
        if (b == 0) {
            log_error("py: ZeroDivisionError: integer modulo by zero");
            py_exception_pending = true;
            py_exception_value = (Item){.item = s2it(heap_create_name("ZeroDivisionError"))};
            return ItemNull;
        }
        // Python modulo: result has same sign as divisor
        int64_t r = a % b;
        if (r != 0 && (r ^ b) < 0) r += b;
        return (Item){.item = i2it(r)};
    }

    double l = py_get_number(left);
    double r = py_get_number(right);
    if (r == 0.0) {
        log_error("py: ZeroDivisionError: float modulo by zero");
        py_exception_pending = true;
        py_exception_value = (Item){.item = s2it(heap_create_name("ZeroDivisionError"))};
        return ItemNull;
    }
    return py_make_number(fmod(l, r) + (fmod(l, r) != 0 && ((l < 0) != (r < 0)) ? r : 0));
}

extern "C" Item py_power(Item left, Item right) {
    TypeId lt = get_type_id(left);
    TypeId rt = get_type_id(right);

    if (lt == LMD_TYPE_INT && rt == LMD_TYPE_INT) {
        int64_t base = it2i(left), exp = it2i(right);
        if (exp < 0) {
            // negative exponent → float
            double* ptr = (double*)heap_alloc(sizeof(double), LMD_TYPE_FLOAT);
            *ptr = pow((double)base, (double)exp);
            return (Item){.item = d2it(ptr)};
        }
        // positive integer exponent: compute by squaring
        int64_t result = 1;
        int64_t b = base;
        int64_t e = exp;
        while (e > 0) {
            if (e & 1) {
                // overflow check
                if (result > INT56_MAX / (b > 0 ? b : -b > 0 ? -b : 1)) {
                    return py_make_number(pow((double)base, (double)exp));
                }
                result *= b;
            }
            e >>= 1;
            if (e > 0) b *= b;
        }
        if (result >= INT56_MIN && result <= INT56_MAX) {
            return (Item){.item = i2it(result)};
        }
        return py_make_number(pow((double)base, (double)exp));
    }

    return fn_pow(left, right);
}

extern "C" Item py_negate(Item operand) {
    TypeId type = get_type_id(operand);
    if (type == LMD_TYPE_INT) {
        int64_t v = -it2i(operand);
        return (Item){.item = i2it(v)};
    }
    return fn_neg(operand);
}

extern "C" Item py_positive(Item operand) {
    // +x is identity for numbers
    return operand;
}

extern "C" Item py_bit_not(Item operand) {
    TypeId type = get_type_id(operand);
    if (type == LMD_TYPE_INT) {
        return (Item){.item = i2it(~it2i(operand))};
    }
    if (type == LMD_TYPE_BOOL) {
        return (Item){.item = i2it(~(it2b(operand) ? 1 : 0))};
    }
    return ItemNull;
}

// ============================================================================
// Bitwise operators
// ============================================================================

extern "C" Item py_bit_and(Item left, Item right) {
    return (Item){.item = i2it((int64_t)py_get_number(left) & (int64_t)py_get_number(right))};
}

extern "C" Item py_bit_or(Item left, Item right) {
    return (Item){.item = i2it((int64_t)py_get_number(left) | (int64_t)py_get_number(right))};
}

extern "C" Item py_bit_xor(Item left, Item right) {
    return (Item){.item = i2it((int64_t)py_get_number(left) ^ (int64_t)py_get_number(right))};
}

extern "C" Item py_lshift(Item left, Item right) {
    return (Item){.item = i2it((int64_t)py_get_number(left) << (int64_t)py_get_number(right))};
}

extern "C" Item py_rshift(Item left, Item right) {
    return (Item){.item = i2it((int64_t)py_get_number(left) >> (int64_t)py_get_number(right))};
}

// ============================================================================
// Comparison operators
// ============================================================================

extern "C" Item py_eq(Item left, Item right) {
    TypeId lt = get_type_id(left);
    TypeId rt = get_type_id(right);

    if (lt == LMD_TYPE_NULL && rt == LMD_TYPE_NULL) return (Item){.item = ITEM_TRUE};
    if (lt == LMD_TYPE_NULL || rt == LMD_TYPE_NULL) return (Item){.item = ITEM_FALSE};

    if (lt == LMD_TYPE_INT && rt == LMD_TYPE_INT) {
        return (Item){.item = b2it(it2i(left) == it2i(right))};
    }
    if ((lt == LMD_TYPE_INT || lt == LMD_TYPE_FLOAT) && (rt == LMD_TYPE_INT || rt == LMD_TYPE_FLOAT)) {
        return (Item){.item = b2it(py_get_number(left) == py_get_number(right))};
    }
    if (lt == LMD_TYPE_STRING && rt == LMD_TYPE_STRING) {
        String* a = it2s(left);
        String* b = it2s(right);
        if (a == b) return (Item){.item = ITEM_TRUE};
        if (a->len != b->len) return (Item){.item = ITEM_FALSE};
        return (Item){.item = b2it(memcmp(a->chars, b->chars, a->len) == 0)};
    }
    if (lt == LMD_TYPE_BOOL && rt == LMD_TYPE_BOOL) {
        return (Item){.item = b2it(it2b(left) == it2b(right))};
    }

    // instance identity (default object __eq__)
    if (lt == LMD_TYPE_MAP && rt == LMD_TYPE_MAP && left.item == right.item)
        return (Item){.item = ITEM_TRUE};

    // __eq__ dunder for instances
    if (lt == LMD_TYPE_MAP && py_is_instance(left)) {
        Item bm = py_getattr(left, (Item){.item = s2it(heap_create_name("__eq__"))});
        if (get_type_id(bm) != LMD_TYPE_NULL) {
            Item r = py_call_function(bm, &right, 1);
            if (get_type_id(r) != LMD_TYPE_NULL) return r;
        }
    }

    // mixed types: Python returns False for most
    return (Item){.item = ITEM_FALSE};
}

extern "C" Item py_ne(Item left, Item right) {
    Item eq = py_eq(left, right);
    return (Item){.item = b2it(!it2b(eq))};
}

extern "C" Item py_lt(Item left, Item right) {
    TypeId lt = get_type_id(left);
    TypeId rt = get_type_id(right);

    if ((lt == LMD_TYPE_INT || lt == LMD_TYPE_FLOAT) && (rt == LMD_TYPE_INT || rt == LMD_TYPE_FLOAT)) {
        return (Item){.item = b2it(py_get_number(left) < py_get_number(right))};
    }
    if (lt == LMD_TYPE_STRING && rt == LMD_TYPE_STRING) {
        String* a = it2s(left);
        String* b = it2s(right);
        int cmp = memcmp(a->chars, b->chars, a->len < b->len ? a->len : b->len);
        if (cmp == 0) return (Item){.item = b2it(a->len < b->len)};
        return (Item){.item = b2it(cmp < 0)};
    }

    // __lt__ dunder for instances
    if (lt == LMD_TYPE_MAP && py_is_instance(left)) {
        Item bm = py_getattr(left, (Item){.item = s2it(heap_create_name("__lt__"))});
        if (get_type_id(bm) != LMD_TYPE_NULL) {
            Item r = py_call_function(bm, &right, 1);
            if (get_type_id(r) != LMD_TYPE_NULL) return r;
        }
    }

    return (Item){.item = ITEM_FALSE};
}

extern "C" Item py_le(Item left, Item right) {
    Item lt_r = py_lt(left, right);
    Item eq_r = py_eq(left, right);
    return (Item){.item = b2it(it2b(lt_r) || it2b(eq_r))};
}

extern "C" Item py_gt(Item left, Item right) {
    return py_lt(right, left);
}

extern "C" Item py_ge(Item left, Item right) {
    return py_le(right, left);
}

extern "C" Item py_is(Item left, Item right) {
    // identity check: same tagged pointer
    return (Item){.item = b2it(left.item == right.item)};
}

extern "C" Item py_is_not(Item left, Item right) {
    return (Item){.item = b2it(left.item != right.item)};
}

extern "C" Item py_contains(Item container, Item value) {
    TypeId ct = get_type_id(container);

    if (ct == LMD_TYPE_STRING) {
        // "in" for strings: substring check
        String* haystack = it2s(container);
        String* needle = it2s(value);
        if (!haystack || !needle) return (Item){.item = ITEM_FALSE};
        if (needle->len == 0) return (Item){.item = ITEM_TRUE};
        if (needle->len > haystack->len) return (Item){.item = ITEM_FALSE};

        for (int64_t i = 0; i <= haystack->len - needle->len; i++) {
            if (memcmp(haystack->chars + i, needle->chars, needle->len) == 0) {
                return (Item){.item = ITEM_TRUE};
            }
        }
        return (Item){.item = ITEM_FALSE};
    }

    if (ct == LMD_TYPE_ARRAY) {
        Array* arr = it2arr(container);
        if (!arr) return (Item){.item = ITEM_FALSE};
        for (int i = 0; i < arr->length; i++) {
            Item eq = py_eq(arr->items[i], value);
            if (it2b(eq)) return (Item){.item = ITEM_TRUE};
        }
        return (Item){.item = ITEM_FALSE};
    }

    if (ct == LMD_TYPE_MAP) {
        // check if key exists in dict
        if (get_type_id(value) == LMD_TYPE_STRING) {
            String* key = it2s(value);
            Map* m = it2map(container);
            if (!m || !m->type) return (Item){.item = ITEM_FALSE};
            bool found = false;
            _map_get((TypeMap*)m->type, m->data, key->chars, &found);
            return (Item){.item = b2it(found)};
        }
        return (Item){.item = ITEM_FALSE};
    }

    // __contains__ dunder for instances
    if (ct == LMD_TYPE_MAP && py_is_instance(container)) {
        Item bm = py_getattr(container, (Item){.item = s2it(heap_create_name("__contains__"))});
        if (get_type_id(bm) != LMD_TYPE_NULL)
            return py_call_function(bm, &value, 1);
    }

    return (Item){.item = ITEM_FALSE};
}

// ============================================================================
// match/case pattern matching helpers (Phase B)
// ============================================================================

// Returns true Item if obj is a sequence (list/array) but not a string or mapping.
// Strings and bytes do not match sequence patterns per Python semantics.
extern "C" Item py_match_is_sequence(Item obj) {
    TypeId t = get_type_id(obj);
    if (t == LMD_TYPE_ARRAY) return (Item){.item = ITEM_TRUE};
    // tuples are also arrays in Lambda
    return (Item){.item = ITEM_FALSE};
}

// Returns true Item if obj is a mapping (dict/map, but not a class instance).
extern "C" Item py_match_is_mapping(Item obj) {
    TypeId t = get_type_id(obj);
    if (t != LMD_TYPE_MAP) return (Item){.item = ITEM_FALSE};
    // exclude class instances (they have __class__ key set) — only pure dicts match
    Map* m = it2map(obj);
    if (!m) return (Item){.item = ITEM_FALSE};
    bool found = false;
    _map_get((TypeMap*)m->type, m->data, "__class__", &found);
    if (found) return (Item){.item = ITEM_FALSE};
    return (Item){.item = ITEM_TRUE};
}

// Returns a new dict containing all key-value pairs from obj whose keys are NOT
// in the excluded_keys list (used for **rest in mapping patterns).
extern "C" Item py_match_mapping_rest(Item obj, Item excluded_keys) {
    Item result = py_dict_new();
    if (get_type_id(obj) != LMD_TYPE_MAP) return result;
    Map* m = it2map(obj);
    if (!m || !m->type || !m->data) return result;

    TypeMap* tm = (TypeMap*)m->type;
    uint64_t* data = (uint64_t*)m->data;

    // Walk shape linked list to iterate all fields
    for (ShapeEntry* e = tm->shape; e; e = e->next) {
        if (!e->name || !e->name->str) continue;
        // skip dunder keys (internal metadata)
        if (e->name->length >= 2 && e->name->str[0] == '_' && e->name->str[1] == '_') continue;

        // check if key should be excluded
        bool excluded = false;
        if (get_type_id(excluded_keys) == LMD_TYPE_ARRAY) {
            Array* keys = it2arr(excluded_keys);
            for (int j = 0; j < keys->length; j++) {
                Item k = keys->items[j];
                if (get_type_id(k) == LMD_TYPE_STRING) {
                    String* ks = it2s(k);
                    if (ks && ks->len == (int)e->name->length &&
                        memcmp(ks->chars, e->name->str, e->name->length) == 0) {
                        excluded = true;
                        break;
                    }
                }
            }
        }
        if (!excluded) {
            // key item
            Item key_item = (Item){.item = s2it(heap_strcpy((char*)e->name->str, (int64_t)e->name->length))};
            // value item: read from data at byte_offset
            int slot = (int)(e->byte_offset / 8);
            Item val = {.item = data[slot]};
            py_dict_set(result, key_item, val);
        }
    }
    return result;
}

// ============================================================================
// Object/attribute operations
// ============================================================================

#define PY_MAP_SIZE_CLASS 1

extern "C" Item py_new_object(void) {
    Map* m = (Map*)heap_calloc_class(sizeof(Map), LMD_TYPE_MAP, PY_MAP_SIZE_CLASS);
    m->type_id = LMD_TYPE_MAP;
    m->type = &EmptyMap;
    return (Item){.map = m};
}

extern "C" Item py_getattr(Item object, Item name) {
    if (get_type_id(name) != LMD_TYPE_STRING) return ItemNull;
    if (get_type_id(object) != LMD_TYPE_MAP) return ItemNull;

    String* key = it2s(name);
    if (!key) return ItemNull;
    Map* m = it2map(object);
    if (!m || !m->type) return ItemNull;

    // 1. super proxy: delegate to the *next* class in the MRO
    {
        bool found_flag = false;
        Item is_super = _map_get((TypeMap*)m->type, m->data, (char*)"__is_super__", &found_flag);
        if (found_flag && get_type_id(is_super) == LMD_TYPE_BOOL && it2b(is_super)) {
            bool f2 = false;
            Item type_item = _map_get((TypeMap*)m->type, m->data, (char*)"__type__", &f2);
            Item obj_item  = _map_get((TypeMap*)m->type, m->data, (char*)"__obj__",  &f2);
            // find type in obj.__class__.__mro__, start lookup from the next entry
            Item obj_cls = py_get_class(obj_item);
            Item mro = (get_type_id(obj_cls) == LMD_TYPE_MAP)
                       ? py_map_get_cstr(obj_cls, "__mro__")
                       : ItemNull;
            bool past = false;
            if (get_type_id(mro) == LMD_TYPE_ARRAY) {
                Array* mro_arr = it2arr(mro);
                for (int i = 0; i < mro_arr->length; i++) {
                    Item entry = mro_arr->items[i];
                    if (!past) {
                        if (entry.item == type_item.item) past = true;
                        continue;
                    }
                    // look in this entry
                    if (get_type_id(entry) != LMD_TYPE_MAP) continue;
                    Map* em = it2map(entry);
                    if (!em || !em->type) continue;
                    bool found2 = false;
                    Item res2 = _map_get((TypeMap*)em->type, em->data, key->chars, &found2);
                    if (found2) {
                        if (get_type_id(res2) == LMD_TYPE_FUNC)
                            return py_bind_method(res2, obj_item);
                        return res2;
                    }
                }
            }
            return ItemNull;
        }
    }

    // 2. direct field on this Map (own attribute for both instances and classes)
    {
        bool found = false;
        Item result = _map_get((TypeMap*)m->type, m->data, key->chars, &found);
        if (found) {
            // if this is an instance and the field is a function, it was stored
            // as an unbound method — bind it
            if (py_is_instance(object) && get_type_id(result) == LMD_TYPE_FUNC)
                return py_bind_method(result, object);
            return result;
        }
    }

    // 3. if this is an instance, walk __class__.__mro__
    if (py_is_instance(object)) {
        Item cls = py_get_class(object);
        Item raw = py_mro_lookup(cls, name);
        if (get_type_id(raw) != LMD_TYPE_NULL) {
            if (get_type_id(raw) == LMD_TYPE_FUNC)
                return py_bind_method(raw, object);
            // property descriptor check
            if (get_type_id(raw) == LMD_TYPE_MAP) {
                bool f = false;
                Item is_prop = _map_get((TypeMap*)it2map(raw)->type,
                                        it2map(raw)->data, (char*)"__is_property__", &f);
                if (f && get_type_id(is_prop) == LMD_TYPE_BOOL && it2b(is_prop)) {
                    bool f2 = false;
                    Item getter = _map_get((TypeMap*)it2map(raw)->type,
                                           it2map(raw)->data, (char*)"__get__", &f2);
                    if (f2 && get_type_id(getter) == LMD_TYPE_FUNC)
                        return py_call_function(py_bind_method(getter, object), NULL, 0);
                }
            }
            return raw;
        }
        return ItemNull;
    }

    // 4. if this is a class, walk its own MRO (for class attribute access)
    if (py_is_class(object)) {
        Item raw = py_mro_lookup(object, name);
        if (get_type_id(raw) != LMD_TYPE_NULL) return raw;
    }

    return ItemNull;
}

extern "C" Item py_setattr(Item object, Item name, Item value) {
    if (get_type_id(name) != LMD_TYPE_STRING) return object;
    if (get_type_id(object) != LMD_TYPE_MAP) return object;

    Map* m = it2map(object);
    if (!m || !py_input) return object;

    // property descriptor: if instance's class has a data descriptor (__set__), invoke it
    if (py_is_instance(object)) {
        Item cls = py_get_class(object);
        Item raw = py_mro_lookup(cls, name);
        if (get_type_id(raw) == LMD_TYPE_MAP) {
            Item is_prop = py_map_get_cstr(raw, "__is_property__");
            if (get_type_id(is_prop) == LMD_TYPE_BOOL && it2b(is_prop)) {
                Item setter = py_map_get_cstr(raw, "__set__");
                if (get_type_id(setter) == LMD_TYPE_FUNC) {
                    Item bound_setter = py_bind_method(setter, object);
                    Item args[1] = { value };
                    py_call_function(bound_setter, args, 1);
                    return value;
                }
                // read-only property — ignore write silently (matches CPython AttributeError)
                // for now just return without writing
                return value;
            }
        }
    }

    String* key = it2s(name);
    // always write to this object's own dict (instance or class dict)
    map_put(m, key, value, py_input);
    return value;
}

extern "C" Item py_hasattr(Item object, Item name) {
    // return True if py_getattr returns a non-null value
    Item result = py_getattr(object, name);
    return (Item){.item = b2it(get_type_id(result) != LMD_TYPE_NULL)};
}

// ============================================================================
// Collection operations
// ============================================================================

extern "C" Item py_list_new(int length) {
    Array* arr = array();
    if (length > 0) {
        arr->capacity = length + 4;
        arr->items = (Item*)malloc(arr->capacity * sizeof(Item));
        arr->length = length;
        for (int i = 0; i < length; i++) {
            arr->items[i] = ItemNull;
        }
    }
    return (Item){.array = arr};
}

extern "C" Item py_list_append(Item list, Item value) {
    if (get_type_id(list) != LMD_TYPE_ARRAY) return list;
    Array* arr = it2arr(list);
    array_push(arr, value);
    return list;
}

extern "C" Item py_list_get(Item list, Item index) {
    if (get_type_id(list) != LMD_TYPE_ARRAY) return ItemNull;
    Array* arr = it2arr(list);
    if (!arr) return ItemNull;

    int64_t idx = (get_type_id(index) == LMD_TYPE_INT) ? it2i(index) : 0;
    // Python supports negative indexing
    if (idx < 0) idx += arr->length;
    if (idx < 0 || idx >= arr->length) return ItemNull;
    return arr->items[idx];
}

extern "C" Item py_list_set(Item list, Item index, Item value) {
    if (get_type_id(list) != LMD_TYPE_ARRAY) return ItemNull;
    Array* arr = it2arr(list);
    if (!arr) return ItemNull;

    int64_t idx = (get_type_id(index) == LMD_TYPE_INT) ? it2i(index) : 0;
    if (idx < 0) idx += arr->length;
    if (idx < 0 || idx >= arr->length) return ItemNull;
    arr->items[idx] = value;
    return value;
}

extern "C" int64_t py_list_length(Item list) {
    if (get_type_id(list) != LMD_TYPE_ARRAY) return 0;
    Array* arr = it2arr(list);
    return arr ? arr->length : 0;
}

extern "C" Item py_dict_new(void) {
    return py_new_object();
}

extern "C" Item py_dict_get(Item dict, Item key) {
    return py_getattr(dict, key);
}

extern "C" Item py_dict_set(Item dict, Item key, Item value) {
    return py_setattr(dict, key, value);
}

extern "C" Item py_tuple_new(int length) {
    return py_list_new(length); // tuples use arrays internally
}

extern "C" Item py_tuple_set(Item tuple, int index, Item value) {
    if (get_type_id(tuple) != LMD_TYPE_ARRAY) return ItemNull;
    Array* arr = it2arr(tuple);
    if (!arr || index < 0 || index >= arr->length) return ItemNull;
    arr->items[index] = value;
    return value;
}

// ============================================================================
// Subscript and slice
// ============================================================================

extern "C" Item py_subscript_get(Item object, Item key) {
    TypeId ot = get_type_id(object);

    if (ot == LMD_TYPE_ARRAY) {
        return py_list_get(object, key);
    }
    if (ot == LMD_TYPE_MAP) {
        return py_getattr(object, key);
    }
    if (ot == LMD_TYPE_STRING) {
        String* str = it2s(object);
        if (!str) return ItemNull;
        int64_t idx = (get_type_id(key) == LMD_TYPE_INT) ? it2i(key) : 0;
        if (idx < 0) idx += str->len;
        if (idx < 0 || idx >= (int64_t)str->len) return ItemNull;
        return (Item){.item = s2it(heap_strcpy(str->chars + idx, 1))};
    }

    return ItemNull;
}

extern "C" Item py_subscript_set(Item object, Item key, Item value) {
    TypeId ot = get_type_id(object);

    if (ot == LMD_TYPE_ARRAY) {
        return py_list_set(object, key, value);
    }
    if (ot == LMD_TYPE_MAP) {
        return py_setattr(object, key, value);
    }

    return ItemNull;
}

// ============================================================================
// Slice operations
// ============================================================================

// resolve a slice index: handle None (null), negative indices, and clamping
static int64_t py_resolve_slice_idx(Item idx_item, int64_t len, int64_t default_val) {
    if (get_type_id(idx_item) == LMD_TYPE_NULL) return default_val;
    int64_t idx = (get_type_id(idx_item) == LMD_TYPE_INT) ? it2i(idx_item) : 0;
    if (idx < 0) idx += len;
    if (idx < 0) idx = 0;
    if (idx > len) idx = len;
    return idx;
}

extern "C" Item py_slice_get(Item object, Item start_item, Item stop_item, Item step_item) {
    TypeId ot = get_type_id(object);

    int64_t step = (get_type_id(step_item) == LMD_TYPE_NULL) ? 1 :
                   (get_type_id(step_item) == LMD_TYPE_INT) ? it2i(step_item) : 1;
    if (step == 0) {
        log_error("py: ValueError: slice step cannot be zero");
        return ItemNull;
    }

    if (ot == LMD_TYPE_ARRAY) {
        Array* arr = it2arr(object);
        if (!arr) return (Item){.array = array()};
        int64_t len = arr->length;

        int64_t start, stop;
        if (step > 0) {
            start = py_resolve_slice_idx(start_item, len, 0);
            stop = py_resolve_slice_idx(stop_item, len, len);
        } else {
            // for negative step, defaults are len-1 and -len-1 (clamped to -1)
            start = (get_type_id(start_item) == LMD_TYPE_NULL) ? len - 1 :
                    py_resolve_slice_idx(start_item, len, len - 1);
            stop = (get_type_id(stop_item) == LMD_TYPE_NULL) ? -1 :
                   py_resolve_slice_idx(stop_item, len, -1);
            // stop of -1 means "before index 0", clamp for the loop
            if (get_type_id(stop_item) != LMD_TYPE_NULL) {
                int64_t raw = it2i(stop_item);
                if (raw < 0) raw += len;
                stop = raw;
                if (stop < -1) stop = -1;
            }
        }

        Array* result = array();
        if (step > 0) {
            for (int64_t i = start; i < stop; i += step) {
                if (i >= 0 && i < len) array_push(result, arr->items[i]);
            }
        } else {
            for (int64_t i = start; i > stop; i += step) {
                if (i >= 0 && i < len) array_push(result, arr->items[i]);
            }
        }
        return (Item){.array = result};
    }

    if (ot == LMD_TYPE_STRING) {
        String* str = it2s(object);
        if (!str) return (Item){.item = s2it(heap_create_name(""))};
        int64_t len = str->len;

        int64_t start, stop;
        if (step > 0) {
            start = py_resolve_slice_idx(start_item, len, 0);
            stop = py_resolve_slice_idx(stop_item, len, len);
        } else {
            start = (get_type_id(start_item) == LMD_TYPE_NULL) ? len - 1 :
                    py_resolve_slice_idx(start_item, len, len - 1);
            stop = (get_type_id(stop_item) == LMD_TYPE_NULL) ? -1 :
                   py_resolve_slice_idx(stop_item, len, -1);
            if (get_type_id(stop_item) != LMD_TYPE_NULL) {
                int64_t raw = it2i(stop_item);
                if (raw < 0) raw += len;
                stop = raw;
                if (stop < -1) stop = -1;
            }
        }

        StrBuf* sb = strbuf_new();
        if (step > 0) {
            for (int64_t i = start; i < stop; i += step) {
                if (i >= 0 && i < len) strbuf_append_char(sb, str->chars[i]);
            }
        } else {
            for (int64_t i = start; i > stop; i += step) {
                if (i >= 0 && i < len) strbuf_append_char(sb, str->chars[i]);
            }
        }
        Item result = (Item){.item = s2it(heap_create_name(sb->str ? sb->str : ""))};
        strbuf_free(sb);
        return result;
    }

    return ItemNull;
}

extern "C" Item py_slice_set(Item object, Item start_item, Item stop_item, Item step_item, Item value) {
    TypeId ot = get_type_id(object);
    if (ot != LMD_TYPE_ARRAY) {
        log_error("py: TypeError: slice assignment only supported for lists");
        return ItemNull;
    }

    Array* arr = it2arr(object);
    if (!arr) return ItemNull;
    int64_t len = arr->length;

    int64_t step = (get_type_id(step_item) == LMD_TYPE_NULL) ? 1 :
                   (get_type_id(step_item) == LMD_TYPE_INT) ? it2i(step_item) : 1;
    if (step == 0) {
        log_error("py: ValueError: slice step cannot be zero");
        return ItemNull;
    }

    int64_t start, stop;
    if (step > 0) {
        start = py_resolve_slice_idx(start_item, len, 0);
        stop = py_resolve_slice_idx(stop_item, len, len);
    } else {
        start = (get_type_id(start_item) == LMD_TYPE_NULL) ? len - 1 :
                py_resolve_slice_idx(start_item, len, len - 1);
        stop = (get_type_id(stop_item) == LMD_TYPE_NULL) ? -1 :
               py_resolve_slice_idx(stop_item, len, -1);
        if (get_type_id(stop_item) != LMD_TYPE_NULL) {
            int64_t raw = it2i(stop_item);
            if (raw < 0) raw += len;
            stop = raw;
            if (stop < -1) stop = -1;
        }
    }

    // get replacement items from value (must be iterable/array)
    TypeId vt = get_type_id(value);
    Array* val_arr = NULL;
    if (vt == LMD_TYPE_ARRAY) {
        val_arr = it2arr(value);
    } else {
        log_error("py: TypeError: can only assign an iterable to a slice");
        return ItemNull;
    }
    int64_t val_len = val_arr ? val_arr->length : 0;

    if (step == 1) {
        // contiguous slice assignment: remove [start:stop], insert val_arr items
        if (start < 0) start = 0;
        if (stop > len) stop = len;
        if (start > stop) start = stop;
        int64_t remove_count = stop - start;
        int64_t insert_count = val_len;
        int64_t new_len = len - remove_count + insert_count;

        // resize the array
        if (new_len > len) {
            // grow: shift tail right
            for (int64_t i = 0; i < new_len - len; i++) {
                array_push(arr, ItemNull);
            }
            // shift tail from old stop position to new position
            for (int64_t i = len - 1; i >= stop; i--) {
                arr->items[i + insert_count - remove_count] = arr->items[i];
            }
        } else if (new_len < len) {
            // shrink: shift tail left
            for (int64_t i = stop; i < len; i++) {
                arr->items[i + insert_count - remove_count] = arr->items[i];
            }
            arr->length = new_len;
        }

        // insert replacement items
        for (int64_t i = 0; i < insert_count; i++) {
            arr->items[start + i] = val_arr->items[i];
        }
    } else {
        // extended slice: step != 1, replacement must have same length
        int64_t slice_count = 0;
        if (step > 0) {
            for (int64_t i = start; i < stop; i += step) slice_count++;
        } else {
            for (int64_t i = start; i > stop; i += step) slice_count++;
        }
        if (val_len != slice_count) {
            log_error("py: ValueError: attempt to assign sequence of size %lld to extended slice of size %lld",
                (long long)val_len, (long long)slice_count);
            return ItemNull;
        }
        int64_t vi = 0;
        if (step > 0) {
            for (int64_t i = start; i < stop && vi < val_len; i += step) {
                if (i >= 0 && i < arr->length) arr->items[i] = val_arr->items[vi];
                vi++;
            }
        } else {
            for (int64_t i = start; i > stop && vi < val_len; i += step) {
                if (i >= 0 && i < arr->length) arr->items[i] = val_arr->items[vi];
                vi++;
            }
        }
    }

    return object;
}

extern "C" Item py_format_value(Item value, Item spec_item) {
    // format a value according to a Python format spec string
    if (get_type_id(spec_item) != LMD_TYPE_STRING) {
        return py_to_str(value);
    }
    String* spec_str = it2s(spec_item);
    if (!spec_str || spec_str->len == 0) {
        return py_to_str(value);
    }

    const char* sp = spec_str->chars;
    int slen = spec_str->len;

    // parse format spec: [[fill]align][sign][#][0][width][.precision][type]
    char fill = ' ';
    char align = '\0';
    int width = 0;
    int precision = -1;
    char type = '\0';

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
    if (*sp == '0') { sp++; if (!align) { fill = '0'; align = '>'; } }

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

    char buf[256];
    const char* formatted = buf;
    int flen = 0;

    if (type == 'd' || type == 'n') {
        int64_t val = (get_type_id(value) == LMD_TYPE_INT) ? it2i(value) : (int64_t)py_get_number(value);
        flen = snprintf(buf, sizeof(buf), "%lld", (long long)val);
        if (flen >= (int)sizeof(buf)) flen = sizeof(buf) - 1;
    } else if (type == 'f' || type == 'F') {
        double val = py_get_number(value);
        if (precision < 0) precision = 6;
        flen = snprintf(buf, sizeof(buf), "%.*f", precision, val);
        if (flen >= (int)sizeof(buf)) flen = sizeof(buf) - 1;
    } else if (type == 'e' || type == 'E') {
        double val = py_get_number(value);
        if (precision < 0) precision = 6;
        flen = snprintf(buf, sizeof(buf), type == 'e' ? "%.*e" : "%.*E", precision, val);
        if (flen >= (int)sizeof(buf)) flen = sizeof(buf) - 1;
    } else if (type == 'x' || type == 'X') {
        int64_t val = (get_type_id(value) == LMD_TYPE_INT) ? it2i(value) : (int64_t)py_get_number(value);
        flen = snprintf(buf, sizeof(buf), type == 'x' ? "%llx" : "%llX", (long long)val);
        if (flen >= (int)sizeof(buf)) flen = sizeof(buf) - 1;
    } else if (type == 'o') {
        int64_t val = (get_type_id(value) == LMD_TYPE_INT) ? it2i(value) : (int64_t)py_get_number(value);
        flen = snprintf(buf, sizeof(buf), "%llo", (long long)val);
        if (flen >= (int)sizeof(buf)) flen = sizeof(buf) - 1;
    } else {
        // default: string conversion
        Item str_val = py_to_str(value);
        String* sv = it2s(str_val);
        if (sv) {
            formatted = sv->chars;
            flen = (precision >= 0 && precision < (int)sv->len) ? precision : sv->len;
        }
    }

    // apply width/alignment padding
    if (width > 0 && flen < width) {
        if (!align) align = (type == 'd' || type == 'f' || type == 'e' || type == 'x' || type == 'o') ? '>' : '<';
        int pad = width - flen;
        StrBuf* sb = strbuf_new();
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
        Item result = (Item){.item = s2it(heap_create_name(sb->str ? sb->str : ""))};
        strbuf_free(sb);
        return result;
    }

    // no padding needed
    if (formatted == buf) {
        buf[flen] = '\0';
        return (Item){.item = s2it(heap_create_name(buf))};
    }
    // already from a string, just return truncated if needed
    char tmp[256];
    int clen = flen < 255 ? flen : 255;
    memcpy(tmp, formatted, clen);
    tmp[clen] = '\0';
    return (Item){.item = s2it(heap_create_name(tmp))};
}

extern "C" Item py_exception_get_type(Item exception) {
    // if the exception is a plain string, it IS the type name (raised by runtime ops like py_divide)
    TypeId tid = get_type_id(exception);
    if (tid == LMD_TYPE_STRING) {
        return exception;
    }
    // for exception objects created by py_new_exception, get the "type" attribute
    Item type_attr = py_getattr(exception, (Item){.item = s2it(heap_create_name("type"))});
    if (get_type_id(type_attr) != LMD_TYPE_NULL) {
        return type_attr;
    }
    // for class instances (e.g. ValueError("msg")), use __class__.__name__
    Item cls = py_getattr(exception, (Item){.item = s2it(heap_create_name("__class__"))});
    if (get_type_id(cls) != LMD_TYPE_NULL) {
        Item name = py_getattr(cls, (Item){.item = s2it(heap_create_name("__name__"))});
        if (get_type_id(name) != LMD_TYPE_NULL) return name;
    }
    return ItemNull;
}

extern "C" Item py_builtin_open(Item path_item, Item mode_item) {
    // simplified: read entire file contents as a string (mode currently ignored, always "r")
    if (get_type_id(path_item) != LMD_TYPE_STRING) {
        log_error("py: TypeError: open() argument must be a string");
        return ItemNull;
    }
    String* path = it2s(path_item);
    if (!path) return ItemNull;

    char filepath[1024];
    int plen = path->len < 1023 ? path->len : 1023;
    memcpy(filepath, path->chars, plen);
    filepath[plen] = '\0';

    // extract mode string (default "r")
    const char* mode = "r";
    if (get_type_id(mode_item) == LMD_TYPE_STRING) {
        String* ms = it2s(mode_item);
        if (ms && ms->len > 0) mode = ms->chars;
    }

    FILE* f = fopen(filepath, mode);
    if (!f) {
        log_error("py: FileNotFoundError: No such file: '%s'", filepath);
        py_raise((Item){.item = s2it(heap_create_name("FileNotFoundError"))});
        return ItemNull;
    }

    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);

    if (size <= 0) {
        fclose(f);
        return (Item){.item = s2it(heap_create_name(""))};
    }

    char* buf = (char*)malloc(size + 1);
    size_t read_bytes = fread(buf, 1, size, f);
    buf[read_bytes] = '\0';
    fclose(f);

    Item result = (Item){.item = s2it(heap_create_name(buf))};
    free(buf);
    return result;
}

// ============================================================================
// Iterator protocol (simplified)
// ============================================================================

extern "C" Item py_get_iterator(Item iterable) {
    // create a stateful iterator object: {__data__: iterable, __idx__: 0, __len__: len}
    Item len_item = py_builtin_len(iterable);
    int64_t len = (get_type_id(len_item) == LMD_TYPE_INT) ? it2i(len_item) : 0;

    Item iter = py_new_object();
    py_setattr(iter, (Item){.item = s2it(heap_create_name("__data__"))}, iterable);
    py_setattr(iter, (Item){.item = s2it(heap_create_name("__idx__"))}, (Item){.item = i2it(0)});
    py_setattr(iter, (Item){.item = s2it(heap_create_name("__len__"))}, (Item){.item = i2it(len)});
    return iter;
}

extern "C" Item py_iterator_next(Item iterator) {
    // advance iterator: read __data__/__idx__/__len__, return next item or StopIteration
    if (get_type_id(iterator) != LMD_TYPE_MAP) {
        return py_stop_iteration();
    }

    Item data = py_getattr(iterator, (Item){.item = s2it(heap_create_name("__data__"))});
    Item idx_item = py_getattr(iterator, (Item){.item = s2it(heap_create_name("__idx__"))});
    Item len_item = py_getattr(iterator, (Item){.item = s2it(heap_create_name("__len__"))});

    if (get_type_id(idx_item) != LMD_TYPE_INT || get_type_id(len_item) != LMD_TYPE_INT) {
        return py_stop_iteration();
    }

    int64_t idx = it2i(idx_item);
    int64_t len = it2i(len_item);

    if (idx >= len) {
        return py_stop_iteration();
    }

    // get item at current index
    Item item = py_subscript_get(data, (Item){.item = i2it(idx)});

    // advance index in-place on the iterator Map
    py_setattr(iterator, (Item){.item = s2it(heap_create_name("__idx__"))}, (Item){.item = i2it(idx + 1)});

    return item;
}

extern "C" Item py_range_new(Item start, Item stop, Item step) {
    // create range as an array for simplicity
    int64_t s = (get_type_id(start) == LMD_TYPE_INT) ? it2i(start) : 0;
    int64_t e = (get_type_id(stop) == LMD_TYPE_INT) ? it2i(stop) : 0;
    int64_t st = (get_type_id(step) == LMD_TYPE_INT) ? it2i(step) : 1;

    if (st == 0) {
        log_error("py: ValueError: range() arg 3 must not be zero");
        return ItemNull;
    }

    Array* arr = array();
    if (st > 0) {
        for (int64_t i = s; i < e; i += st) {
            array_push(arr, (Item){.item = i2it(i)});
        }
    } else {
        for (int64_t i = s; i > e; i += st) {
            array_push(arr, (Item){.item = i2it(i)});
        }
    }
    return (Item){.array = arr};
}

// ============================================================================
// Function/closure
// ============================================================================

extern "C" Item py_new_function(void* func_ptr, int param_count) {
    // allocate a Function-like object
    if (!py_input) return ItemNull;
    Function* fn = (Function*)pool_calloc(py_input->pool, sizeof(Function));
    fn->type_id = LMD_TYPE_FUNC;
    fn->ptr = (fn_ptr)func_ptr;
    fn->arity = param_count;
    return (Item){.function = fn};
}

extern "C" Item py_new_closure(void* func_ptr, int param_count, uint64_t* env, int env_size) {
    if (!py_input) return ItemNull;
    Function* fn = (Function*)pool_calloc(py_input->pool, sizeof(Function));
    fn->type_id = LMD_TYPE_FUNC;
    fn->ptr = (fn_ptr)func_ptr;
    fn->arity = param_count;
    fn->closure_env = env;
    fn->closure_field_count = env_size;
    return (Item){.function = fn};
}

extern "C" uint64_t* py_alloc_env(int size) {
    if (!py_input) return NULL;
    return (uint64_t*)pool_calloc(py_input->pool, size * sizeof(uint64_t));
}

// set FN_FLAG_HAS_KWARGS on a function item (called at definition site for **kwargs functions)
extern "C" Item py_set_kwargs_flag(Item fn_item) {
    if (get_type_id(fn_item) != LMD_TYPE_FUNC) return fn_item;
    Function* fn = fn_item.function;
    if (fn) fn->flags |= FN_FLAG_HAS_KWARGS;
    return fn_item;
}

// merge all entries from src dict into dst dict (used for **dict splat at call sites)
extern "C" Item py_dict_merge(Item dst, Item src) {
    if (get_type_id(dst) != LMD_TYPE_MAP) return dst;
    if (get_type_id(src) != LMD_TYPE_MAP) return dst;
    Map* src_map = it2map(src);
    if (!src_map || !src_map->type) return dst;
    Map* dst_map = it2map(dst);
    if (!dst_map) return dst;
    TypeMap* stm = (TypeMap*)src_map->type;
    ShapeEntry* field = stm->shape;
    while (field) {
        Item val = _map_read_field(field, src_map->data);
        if (field->name && py_input) {
            String* key_name = heap_create_name(field->name->str);
            map_put(dst_map, key_name, val, py_input);
        }
        field = field->next;
    }
    return dst;
}

// call a function that may accept **kwargs: if FN_FLAG_HAS_KWARGS, append kwargs_map as last arg
extern "C" Item py_call_function_kw(Item func, Item* args, int arg_count, Item kwargs_map) {
    // bound method: prepend __self__ to args
    if (py_is_bound_method(func)) {
        Item fn_item = py_map_get_cstr(func, "__func__");
        Item self    = py_map_get_cstr(func, "__self__");
        Item new_args[17];
        new_args[0] = self;
        int na = arg_count > 16 ? 16 : arg_count;
        for (int i = 0; i < na; i++) new_args[i + 1] = args ? args[i] : ItemNull;
        return py_call_function_kw(fn_item, new_args, na + 1, kwargs_map);
    }

    // class construction — kwargs not forwarded to __init__ for now
    if (py_is_class(func)) {
        return py_call_function(func, args, arg_count);
    }

    if (get_type_id(func) != LMD_TYPE_FUNC) {
        log_error("py: TypeError: object is not callable (py_call_function_kw)");
        return ItemNull;
    }
    Function* fn = func.function;
    if (!fn || !fn->ptr) return ItemNull;

    // if the function doesn't take **kwargs, fall back to regular dispatch
    if (!(fn->flags & FN_FLAG_HAS_KWARGS)) {
        return py_call_function(func, args, arg_count);
    }

    int arity = fn->arity;
    bool is_closure = (fn->closure_field_count > 0 && fn->closure_env != NULL);

    // typedefs: regular params + kwargs map as last param (non-closure)
    typedef Item (*PyFuncKw0)(Item);
    typedef Item (*PyFuncKw1)(Item, Item);
    typedef Item (*PyFuncKw2)(Item, Item, Item);
    typedef Item (*PyFuncKw3)(Item, Item, Item, Item);
    typedef Item (*PyFuncKw4)(Item, Item, Item, Item, Item);
    typedef Item (*PyFuncKw5)(Item, Item, Item, Item, Item, Item);
    typedef Item (*PyFuncKw6)(Item, Item, Item, Item, Item, Item, Item);

    // typedefs: env + regular params + kwargs map (closure)
    typedef Item (*PyClosureKw1)(uint64_t*, Item);
    typedef Item (*PyClosureKw2)(uint64_t*, Item, Item);
    typedef Item (*PyClosureKw3)(uint64_t*, Item, Item, Item);
    typedef Item (*PyClosureKw4)(uint64_t*, Item, Item, Item, Item);
    typedef Item (*PyClosureKw5)(uint64_t*, Item, Item, Item, Item, Item);
    typedef Item (*PyClosureKw6)(uint64_t*, Item, Item, Item, Item, Item, Item);
    typedef Item (*PyClosureKw7)(uint64_t*, Item, Item, Item, Item, Item, Item, Item);

    // pad regular args with ItemNull
    Item padded[16];
    for (int i = 0; i < arity && i < 16; i++) {
        padded[i] = (i < arg_count && args) ? args[i] : ItemNull;
    }

    if (is_closure) {
        uint64_t* env = (uint64_t*)fn->closure_env;
        switch (arity) {
        case 0: return ((PyClosureKw1)fn->ptr)(env, kwargs_map);
        case 1: return ((PyClosureKw2)fn->ptr)(env, padded[0], kwargs_map);
        case 2: return ((PyClosureKw3)fn->ptr)(env, padded[0], padded[1], kwargs_map);
        case 3: return ((PyClosureKw4)fn->ptr)(env, padded[0], padded[1], padded[2], kwargs_map);
        case 4: return ((PyClosureKw5)fn->ptr)(env, padded[0], padded[1], padded[2], padded[3], kwargs_map);
        case 5: return ((PyClosureKw6)fn->ptr)(env, padded[0], padded[1], padded[2], padded[3], padded[4], kwargs_map);
        case 6: return ((PyClosureKw7)fn->ptr)(env, padded[0], padded[1], padded[2], padded[3], padded[4], padded[5], kwargs_map);
        default: return ItemNull;
        }
    }

    switch (arity) {
    case 0: return ((PyFuncKw0)fn->ptr)(kwargs_map);
    case 1: return ((PyFuncKw1)fn->ptr)(padded[0], kwargs_map);
    case 2: return ((PyFuncKw2)fn->ptr)(padded[0], padded[1], kwargs_map);
    case 3: return ((PyFuncKw3)fn->ptr)(padded[0], padded[1], padded[2], kwargs_map);
    case 4: return ((PyFuncKw4)fn->ptr)(padded[0], padded[1], padded[2], padded[3], kwargs_map);
    case 5: return ((PyFuncKw5)fn->ptr)(padded[0], padded[1], padded[2], padded[3], padded[4], kwargs_map);
    case 6: return ((PyFuncKw6)fn->ptr)(padded[0], padded[1], padded[2], padded[3], padded[4], padded[5], kwargs_map);
    default: return ItemNull;
    }
}

extern "C" Item py_call_function(Item func, Item* args, int arg_count) {
    // bound method: prepend __self__ to args
    if (py_is_bound_method(func)) {
        Item fn_item = py_map_get_cstr(func, "__func__");
        Item self    = py_map_get_cstr(func, "__self__");
        // build new args array with self prepended (max 16+1=17)
        Item new_args[17];
        new_args[0] = self;
        int na = arg_count > 16 ? 16 : arg_count;
        for (int i = 0; i < na; i++) new_args[i + 1] = args ? args[i] : ItemNull;
        return py_call_function(fn_item, new_args, na + 1);
    }

    // class call (construction): allocate instance + call __init__
    if (py_is_class(func)) {
        Item inst = py_new_instance(func);
        Item init_fn = py_mro_lookup(func,
            (Item){.item = s2it(heap_create_name("__init__"))});
        if (get_type_id(init_fn) != LMD_TYPE_NULL) {
            // call __init__(inst, *args) — prepend inst
            Item init_args[17];
            init_args[0] = inst;
            int na = arg_count > 16 ? 16 : arg_count;
            for (int i = 0; i < na; i++) init_args[i + 1] = args ? args[i] : ItemNull;
            py_call_function(init_fn, init_args, na + 1);
        }
        return inst;
    }

    if (get_type_id(func) != LMD_TYPE_FUNC) {
        // might be a built-in callable stored as a Map (e.g. property)
        log_error("py: TypeError: object is not callable");
        return ItemNull;
    }
    Function* fn = func.function;
    if (!fn || !fn->ptr) return ItemNull;

    int arity = fn->arity;
    bool is_closure = (fn->closure_field_count > 0 && fn->closure_env != NULL);

    // for closures, env is a hidden first argument — MIR function has arity+1 params
    // typedef for closure calls: first arg is uint64_t* (env), rest are Item
    typedef Item (*PyClosure1)(uint64_t*);
    typedef Item (*PyClosure2)(uint64_t*, Item);
    typedef Item (*PyClosure3)(uint64_t*, Item, Item);
    typedef Item (*PyClosure4)(uint64_t*, Item, Item, Item);
    typedef Item (*PyClosure5)(uint64_t*, Item, Item, Item, Item);
    typedef Item (*PyClosure6)(uint64_t*, Item, Item, Item, Item, Item);
    typedef Item (*PyClosure7)(uint64_t*, Item, Item, Item, Item, Item, Item);

    // pad missing args with ItemNull for default parameter handling
    Item padded[16];
    for (int i = 0; i < arity && i < 16; i++) {
        padded[i] = (i < arg_count && args) ? args[i] : ItemNull;
    }

    if (is_closure) {
        uint64_t* env = (uint64_t*)fn->closure_env;
        switch (arity) {
        case 0: return ((PyClosure1)fn->ptr)(env);
        case 1: return ((PyClosure2)fn->ptr)(env, padded[0]);
        case 2: return ((PyClosure3)fn->ptr)(env, padded[0], padded[1]);
        case 3: return ((PyClosure4)fn->ptr)(env, padded[0], padded[1], padded[2]);
        case 4: return ((PyClosure5)fn->ptr)(env, padded[0], padded[1], padded[2], padded[3]);
        case 5: return ((PyClosure6)fn->ptr)(env, padded[0], padded[1], padded[2], padded[3], padded[4]);
        case 6: return ((PyClosure7)fn->ptr)(env, padded[0], padded[1], padded[2], padded[3], padded[4], padded[5]);
        default: return ItemNull;
        }
    }

    // non-closure call
    typedef Item (*PyFunc0)();
    typedef Item (*PyFunc1)(Item);
    typedef Item (*PyFunc2)(Item, Item);
    typedef Item (*PyFunc3)(Item, Item, Item);
    typedef Item (*PyFunc4)(Item, Item, Item, Item);
    typedef Item (*PyFunc5)(Item, Item, Item, Item, Item);
    typedef Item (*PyFunc6)(Item, Item, Item, Item, Item, Item);

    switch (arity) {
    case 0: return ((PyFunc0)fn->ptr)();
    case 1: return ((PyFunc1)fn->ptr)(padded[0]);
    case 2: return ((PyFunc2)fn->ptr)(padded[0], padded[1]);
    case 3: return ((PyFunc3)fn->ptr)(padded[0], padded[1], padded[2]);
    case 4: return ((PyFunc4)fn->ptr)(padded[0], padded[1], padded[2], padded[3]);
    case 5: return ((PyFunc5)fn->ptr)(padded[0], padded[1], padded[2], padded[3], padded[4]);
    case 6: return ((PyFunc6)fn->ptr)(padded[0], padded[1], padded[2], padded[3], padded[4], padded[5]);
    default: return ItemNull;
    }
}

// ============================================================================
// Exception handling
// ============================================================================

extern "C" void py_raise(Item exception) {
    py_exception_pending = true;
    py_exception_value = exception;
    log_debug("py: exception raised");
}

extern "C" Item py_check_exception(void) {
    return (Item){.item = b2it(py_exception_pending)};
}

extern "C" Item py_clear_exception(void) {
    py_exception_pending = false;
    Item val = py_exception_value;
    py_exception_value = ItemNull;
    return val;
}

extern "C" Item py_new_exception(Item type_name, Item message) {
    Item obj = py_new_object();
    py_setattr(obj, (Item){.item = s2it(heap_create_name("type"))}, type_name);
    py_setattr(obj, (Item){.item = s2it(heap_create_name("message"))}, message);
    return obj;
}

// ============================================================================
// Context manager protocol (__enter__ / __exit__)
// ============================================================================

extern "C" Item py_context_enter(Item mgr) {
    Item enter_name = {.item = s2it(heap_create_name("__enter__"))};
    Item enter = py_getattr(mgr, enter_name);
    if (get_type_id(enter) == LMD_TYPE_NULL) {
        // duck-type: no __enter__ — return the object itself
        return mgr;
    }
    return py_call_function(enter, NULL, 0);
}

extern "C" Item py_context_exit(Item mgr, Item exc_type, Item exc_val, Item exc_tb) {
    Item exit_name = {.item = s2it(heap_create_name("__exit__"))};
    Item exit_fn = py_getattr(mgr, exit_name);
    if (get_type_id(exit_fn) == LMD_TYPE_NULL) {
        // duck-type: no __exit__ — act as if it returned False (don't suppress)
        return (Item){.item = b2it(false)};
    }
    Item args[3] = { exc_type, exc_val, exc_tb };
    return py_call_function(exit_fn, args, 3);
}

// Resolve an identifier that wasn't found in local/module scope.
// Checks builtin classes (ValueError, RuntimeError, etc.) and module vars by name.
extern "C" Item py_resolve_name_item(Item name_item) {
    if (get_type_id(name_item) != LMD_TYPE_STRING) return ItemNull;
    String* s = it2s(name_item);
    if (!s) return ItemNull;
    // check builtin classes
    Item cls = py_get_builtin_class(s->chars);
    if (get_type_id(cls) != LMD_TYPE_NULL) return cls;
    return ItemNull;
}

// ============================================================================
// Variadic args support
// ============================================================================

extern "C" Item py_build_list_from_args(Item* args, int64_t count) {
    Item list = py_list_new(0);
    for (int64_t i = 0; i < count; i++) {
        py_list_append(list, args[i]);
    }
    return list;
}

// ============================================================================
// Module variable table
// ============================================================================

extern "C" void py_set_module_var(int index, Item value) {
    if (index >= 0 && index < PY_MODULE_VAR_MAX) {
        py_module_vars[index] = value;
        if (index >= py_module_var_count) {
            py_module_var_count = index + 1;
        }
    }
}

extern "C" Item py_get_module_var(int index) {
    if (index >= 0 && index < PY_MODULE_VAR_MAX) {
        return py_module_vars[index];
    }
    return ItemNull;
}

extern "C" void py_reset_module_vars(void) {
    memset(py_module_vars, 0, py_module_var_count * sizeof(Item));
    py_module_var_count = 0;
}

// ============================================================================
// Stop iteration
// ============================================================================

extern "C" Item py_stop_iteration(void) {
    if (!py_stop_iteration_initialized) {
        py_stop_iteration_sentinel = py_new_exception(
            (Item){.item = s2it(heap_create_name("StopIteration"))},
            (Item){.item = s2it(heap_create_name(""))}
        );
        py_stop_iteration_initialized = true;
    }
    return py_stop_iteration_sentinel;
}

extern "C" bool py_is_stop_iteration(Item value) {
    if (!py_stop_iteration_initialized) return false;
    return value.item == py_stop_iteration_sentinel.item;
}
