#include "transpiler.hpp"
#include "../lib/log.h"
#include "utf_string.h"

#include <stdarg.h>
#include <time.h>
#include <errno.h>  // for errno checking
#if _WIN32
#include <windows.h>
#include <process.h>
// Windows doesn't have these macros, provide simple equivalents
#define WIFEXITED(status) (1)
#define WEXITSTATUS(status) (status)
#else
#include <sys/wait.h>  // for WIFEXITED, WEXITSTATUS
#endif
#include "validator/validator.hpp"

extern __thread EvalContext* context;

// External typeset function

#define stack_alloc(size) alloca(size);

#define Malloc malloc
#define Realloc realloc

Item _map_get(TypeMap* map_type, void* map_data, char *key, bool *is_found);

Bool is_truthy(Item item) {
    log_debug("is_truthy: item type %d", item._type_id);
    switch (item._type_id) {
    case LMD_TYPE_NULL:
        return BOOL_FALSE;
    case LMD_TYPE_ERROR:
        return BOOL_ERROR;
    case LMD_TYPE_BOOL:
        return item.bool_val ? BOOL_TRUE : BOOL_FALSE;
    default: // all other value considered truthy
        return item.item ? BOOL_TRUE : BOOL_FALSE;  // should null be considered ERROR?
    }
}

Item op_and(Bool a, Bool b) {
    return {.item = (a >= BOOL_ERROR || b >= BOOL_ERROR) ? ITEM_ERROR : (a && b) ? ITEM_TRUE : ITEM_FALSE};
}

Item op_or(Bool a, Bool b) {
    return {.item = (a >= BOOL_ERROR || b >= BOOL_ERROR) ? ITEM_ERROR : (a || b) ? ITEM_TRUE : ITEM_FALSE};
}

// list to item
Item v2it(List* list) {
    if (!list) { return ItemNull; }
    log_debug("v2it %p, length: %ld", list, list->length);
    if (list->length == 0) { return ItemNull; }
    if (list->length == 1) { return list->items[0]; }
    return {.list = list};
}

String *fn_strcat(String *left, String *right) {
    log_debug("fn_strcat %p, %p", left, right);
    if (!left || !right) {
        log_error("null pointer in fn_strcat: left=%p, right=%p", left, right);
        return NULL;
    }
    int left_len = left->len, right_len = right->len;
    log_debug("left len %d, right len %d", left_len, right_len);
    String *result = (String *)heap_alloc(sizeof(String) + left_len + right_len + 1, LMD_TYPE_STRING);
    if (!result) {
        log_error("failed to allocate memory for fn_strcat result");
        return NULL;
    }
    log_debug("str result %p", result);
    result->ref_cnt = 0;  result->len = left_len + right_len;
    memcpy(result->chars, left->chars, left_len);
    // copy the string and '\0'
    memcpy(result->chars + left_len, right->chars, right_len + 1);
    log_debug("fn_strcat result: %s", result->chars);
    return result;
}

Item fn_join(Item left, Item right) {
    // concat two scalars, or join two lists/arrays/maps, or join scalar with list/array, or join two binaries, else error
    TypeId left_type = get_type_id(left), right_type = get_type_id(right);
    log_debug("fn_join: %d, %d", left_type, right_type);
    if (left_type == LMD_TYPE_STRING || right_type == LMD_TYPE_STRING) {
        String *left_str = fn_string(left), *right_str = fn_string(right);
        String *result = fn_strcat(left_str, right_str);
        return {.item = s2it(result)};
    }
    else if (left_type == LMD_TYPE_SYMBOL || right_type == LMD_TYPE_SYMBOL) {
        String *left_str = fn_string(left), *right_str = fn_string(right);
        String *result = fn_strcat(left_str, right_str);
        return {.item = y2it(result)};
    }
    // todo: support binary concat
    else if (left_type == LMD_TYPE_LIST && right_type == LMD_TYPE_LIST) {
        List *left_list = left.list;  List *right_list = right.list;
        int total_len = left_list->length + right_list->length, total_extra = left_list->extra + right_list->extra;
        List *result_list = (List *)heap_calloc(sizeof(List) + sizeof(Item)*(total_len + total_extra), LMD_TYPE_LIST);
        result_list->type_id = LMD_TYPE_LIST;
        result_list->length = total_len;  result_list->extra = total_extra;
        result_list->items = (Item*)(result_list + 1);
        // copy the items
        memcpy(result_list->items, left_list->items, sizeof(Item)*left_list->length);
        memcpy(result_list->items + left_list->length, right_list->items, sizeof(Item)*right_list->length);
        // need to handle extra and ref_cnt
        return {.item = l2it(result_list)};
    }
    // else if (left_type == LMD_TYPE_ARRAY && right_type == LMD_TYPE_ARRAY) {
    // }
    else {
        log_error("fn_join: unsupported operand types: %d and %d", left_type, right_type);
        return ItemError;   // type mismatch
    }
}

String *str_repeat(String *str, int64_t times) {
    if (times <= 0) {
        // Return empty string
        String *result = (String *)heap_alloc(sizeof(String) + 1, LMD_TYPE_STRING);
        result->ref_cnt = 0;
        result->len = 0;
        result->chars[0] = '\0';
        return result;
    }

    size_t str_len = str->len;
    size_t total_len = str_len * times;
    String *result = (String *)heap_alloc(sizeof(String) + total_len + 1, LMD_TYPE_STRING);
    result->ref_cnt = 0;
    result->len = total_len;

    for (long i = 0; i < times; i++) {
        memcpy(result->chars + (i * str_len), str->chars, str_len);
    }
    result->chars[total_len] = '\0';

    return result;
}

// Unicode string normalization function
Item fn_normalize(Item str_item, Item type_item) {
    // normalize(string, 'nfc'|'nfd'|'nfkc'|'nfkd') - Unicode normalization
    if (str_item._type_id != LMD_TYPE_STRING) {
        log_debug("normalize: first argument must be a string, got type: %d", str_item._type_id);
        return ItemError;
    }

    String* str = str_item.get_string();
    if (!str || str->len == 0) {
        return str_item;  // Return empty string as-is
    }

    // Default to NFC if no type specified or invalid type
    int options = UTF8PROC_STABLE | UTF8PROC_COMPOSE;

    if (type_item._type_id == LMD_TYPE_STRING) {
        String* type_str = type_item.get_string();
        if (type_str && type_str->len > 0) {
            if (strncmp(type_str->chars, "nfd", 3) == 0) {
                options = UTF8PROC_STABLE | UTF8PROC_DECOMPOSE;
            } else if (strncmp(type_str->chars, "nfkc", 4) == 0) {
                options = UTF8PROC_STABLE | UTF8PROC_COMPOSE | UTF8PROC_COMPAT;
            } else if (strncmp(type_str->chars, "nfkd", 4) == 0) {
                options = UTF8PROC_STABLE | UTF8PROC_DECOMPOSE | UTF8PROC_COMPAT;
            }
            // Default case (nfc) already set above
        }
    }

    // Use utf8proc for Unicode normalization
    utf8proc_uint8_t* normalized = NULL;
    utf8proc_ssize_t normalized_len = utf8proc_map(
        (const utf8proc_uint8_t*)str->chars, str->len, &normalized, (utf8proc_option_t)options);

    if (normalized_len < 0) {
        log_debug("normalize: utf8proc_map failed with error: %zd", normalized_len);
        return ItemError;
    }

    // Create new string with normalized content
    String* result = (String*)heap_alloc(sizeof(String) + normalized_len + 1, LMD_TYPE_STRING);
    result->ref_cnt = 0;
    result->len = normalized_len;
    memcpy(result->chars, normalized, normalized_len);
    result->chars[normalized_len] = '\0';

    // Free the utf8proc allocated buffer
    free(normalized);
    return (Item){.item = s2it(result)};
}

Range* fn_to(Item item_a, Item item_b) {
    if ((item_a._type_id == LMD_TYPE_INT || item_a._type_id == LMD_TYPE_INT64 || item_a._type_id == LMD_TYPE_FLOAT) &&
        (item_b._type_id == LMD_TYPE_INT || item_b._type_id == LMD_TYPE_INT64 || item_b._type_id == LMD_TYPE_FLOAT)) {
        int64_t start = item_a._type_id == LMD_TYPE_INT ? item_a.get_int56() :
            item_a._type_id == LMD_TYPE_INT64 ? *(int64_t*)item_a.int64_ptr : (int64_t)*(double*)item_a.double_ptr;
        int64_t end = item_b._type_id == LMD_TYPE_INT ? item_b.get_int56() :
            item_b._type_id == LMD_TYPE_INT64 ? *(int64_t*)item_b.int64_ptr : (int64_t)*(double*)item_b.double_ptr;
        if (start > end) {
            // return empty range instead of NULL
            log_debug("Error: start of range is greater than end: %ld > %ld", start, end);
            Range *range = (Range *)heap_alloc(sizeof(Range), LMD_TYPE_RANGE);
            range->type_id = LMD_TYPE_RANGE;
            range->start = 0;  range->end = -1;  // Empty range
            range->length = 0;
            return range;
        }
        Range *range = (Range *)heap_alloc(sizeof(Range), LMD_TYPE_RANGE);
        range->type_id = LMD_TYPE_RANGE;
        range->start = start;  range->end = end;
        range->length = end - start + 1;
        log_debug("create range: %ld to %ld, length: %ld", range->start, range->end, range->length);
        return range;
    }
    else {
        log_error("unknown range type: %d, %d\n", item_a._type_id, item_b._type_id);
        return NULL;
    }
}

Function* to_fn(fn_ptr ptr) {
    log_debug("create fn %p", ptr);
    Function *fn = (Function*)calloc(1, sizeof(Function));
    fn->type_id = LMD_TYPE_FUNC;
    fn->ref_cnt = 1;
    fn->ptr = ptr;
    fn->closure_env = NULL;
    return fn;
}

// Create function with arity info
Function* to_fn_n(fn_ptr ptr, int arity) {
    log_debug("create fn %p with arity %d", ptr, arity);
    Function *fn = (Function*)calloc(1, sizeof(Function));
    fn->type_id = LMD_TYPE_FUNC;
    fn->ref_cnt = 1;
    fn->arity = (uint8_t)arity;
    fn->ptr = ptr;
    fn->closure_env = NULL;
    return fn;
}

// Create a closure with captured environment
Function* to_closure(fn_ptr ptr, int arity, void* env) {
    log_debug("create closure %p with arity %d and env %p", ptr, arity, env);
    Function* fn = (Function*)calloc(1, sizeof(Function));
    fn->type_id = LMD_TYPE_FUNC;
    fn->ref_cnt = 1;
    fn->fn_type = NULL;
    fn->arity = (uint8_t)arity;
    fn->ptr = ptr;
    fn->closure_env = env;
    return fn;
}

// Helper to validate that an Item is actually a function
static inline bool is_valid_function(Function* fn) {
    if (!fn) return false;
    // Check that the pointer looks like a Function by verifying type_id
    // Function struct starts with TypeId type_id which should be LMD_TYPE_FUNC
    TypeId type_id = *(TypeId*)fn;
    return type_id == LMD_TYPE_FUNC;
}

// Dynamic function dispatch for first-class functions
// For closures, env is passed as the first argument
Item fn_call(Function* fn, List* args) {
    if (!is_valid_function(fn)) {
        log_error("fn_call: invalid function (null or wrong type)");
        return ItemError;
    }
    if (!fn->ptr) {
        log_error("fn_call: null function pointer");
        return ItemError;
    }
    int arg_count = args ? (int)args->length : 0;
    void* env = fn->closure_env;
    log_debug("fn_call: arity=%d, arg_count=%d, env=%p", fn->arity, arg_count, env);
    
    // For closures, env is passed as first argument
    if (env) {
        switch (arg_count) {
            case 0: return ((Item(*)(void*))fn->ptr)(env);
            case 1: return ((Item(*)(void*,Item))fn->ptr)(env, args->items[0]);
            case 2: return ((Item(*)(void*,Item,Item))fn->ptr)(env, args->items[0], args->items[1]);
            case 3: return ((Item(*)(void*,Item,Item,Item))fn->ptr)(env, args->items[0], args->items[1], args->items[2]);
            case 4: return ((Item(*)(void*,Item,Item,Item,Item))fn->ptr)(env, args->items[0], args->items[1], args->items[2], args->items[3]);
            case 5: return ((Item(*)(void*,Item,Item,Item,Item,Item))fn->ptr)(env, args->items[0], args->items[1], args->items[2], args->items[3], args->items[4]);
            case 6: return ((Item(*)(void*,Item,Item,Item,Item,Item,Item))fn->ptr)(env, args->items[0], args->items[1], args->items[2], args->items[3], args->items[4], args->items[5]);
            case 7: return ((Item(*)(void*,Item,Item,Item,Item,Item,Item,Item))fn->ptr)(env, args->items[0], args->items[1], args->items[2], args->items[3], args->items[4], args->items[5], args->items[6]);
            default:
                log_error("fn_call: unsupported argument count %d for closure (max 7)", arg_count);
                return ItemError;
        }
    }
    
    // Non-closure dispatch
    switch (arg_count) {
        case 0: return ((Item(*)())fn->ptr)();
        case 1: return ((Item(*)(Item))fn->ptr)(args->items[0]);
        case 2: return ((Item(*)(Item,Item))fn->ptr)(args->items[0], args->items[1]);
        case 3: return ((Item(*)(Item,Item,Item))fn->ptr)(args->items[0], args->items[1], args->items[2]);
        case 4: return ((Item(*)(Item,Item,Item,Item))fn->ptr)(args->items[0], args->items[1], args->items[2], args->items[3]);
        case 5: return ((Item(*)(Item,Item,Item,Item,Item))fn->ptr)(args->items[0], args->items[1], args->items[2], args->items[3], args->items[4]);
        case 6: return ((Item(*)(Item,Item,Item,Item,Item,Item))fn->ptr)(args->items[0], args->items[1], args->items[2], args->items[3], args->items[4], args->items[5]);
        case 7: return ((Item(*)(Item,Item,Item,Item,Item,Item,Item))fn->ptr)(args->items[0], args->items[1], args->items[2], args->items[3], args->items[4], args->items[5], args->items[6]);
        case 8: return ((Item(*)(Item,Item,Item,Item,Item,Item,Item,Item))fn->ptr)(args->items[0], args->items[1], args->items[2], args->items[3], args->items[4], args->items[5], args->items[6], args->items[7]);
        default:
            log_error("fn_call: unsupported argument count %d (max 8)", arg_count);
            return ItemError;
    }
}

// Convenience wrappers for common arities (avoid List allocation)
// For closures, env is passed as the first argument
Item fn_call0(Function* fn) {
    if (!is_valid_function(fn)) {
        log_error("fn_call0: invalid function (null or wrong type)");
        return ItemError;
    }
    if (!fn->ptr) return ItemError;
    if (fn->closure_env) return ((Item(*)(void*))fn->ptr)(fn->closure_env);
    return ((Item(*)())fn->ptr)();
}

Item fn_call1(Function* fn, Item a) {
    if (!is_valid_function(fn)) {
        log_error("Error: attempting to call non-function value");
        return ItemError;
    }
    if (!fn->ptr) return ItemError;
    if (fn->closure_env) return ((Item(*)(void*,Item))fn->ptr)(fn->closure_env, a);
    return ((Item(*)(Item))fn->ptr)(a);
}

Item fn_call2(Function* fn, Item a, Item b) {
    if (!is_valid_function(fn)) {
        log_error("Error: attempting to call non-function value");
        return ItemError;
    }
    if (!fn->ptr) return ItemError;
    if (fn->closure_env) return ((Item(*)(void*,Item,Item))fn->ptr)(fn->closure_env, a, b);
    return ((Item(*)(Item,Item))fn->ptr)(a, b);
}

Item fn_call3(Function* fn, Item a, Item b, Item c) {
    if (!is_valid_function(fn)) {
        log_error("Error: attempting to call non-function value");
        return ItemError;
    }
    if (!fn->ptr) return ItemError;
    if (fn->closure_env) return ((Item(*)(void*,Item,Item,Item))fn->ptr)(fn->closure_env, a, b, c);
    return ((Item(*)(Item,Item,Item))fn->ptr)(a, b, c);
}

Bool fn_is(Item a, Item b) {
    log_debug("fn_is");
    TypeId b_type_id = get_type_id(b);
    if (b_type_id != LMD_TYPE_TYPE) {
        log_error("2nd argument must be a type, got type: %d", b_type_id);
        return BOOL_ERROR;
    }
    TypeType *type_b = (TypeType *)b.type;
    TypeId a_type_id = get_type_id(a);
    log_debug("is type %d, %d", a_type_id, type_b->type->type_id);
    switch (type_b->type->type_id) {
    case LMD_TYPE_ANY:
        return a_type_id == LMD_TYPE_ERROR ? BOOL_FALSE : BOOL_TRUE;
    case LMD_TYPE_INT:  case LMD_TYPE_INT64:  case LMD_TYPE_FLOAT:  case LMD_TYPE_DECIMAL:  case LMD_TYPE_NUMBER:
        return LMD_TYPE_INT <= a_type_id && a_type_id <= type_b->type->type_id;
    case LMD_TYPE_ARRAY: case LMD_TYPE_LIST: case LMD_TYPE_MAP: case LMD_TYPE_ELEMENT:
        if (type_b == &LIT_TYPE_ARRAY) {  // fast path
            log_debug("fast path array type check");
            return a_type_id == LMD_TYPE_RANGE || a_type_id == LMD_TYPE_ARRAY || a_type_id == LMD_TYPE_ARRAY_INT ||
                a_type_id == LMD_TYPE_ARRAY_INT64 || a_type_id == LMD_TYPE_ARRAY_FLOAT;
        } else {  // full type validation
            log_debug("full type validation for type: %d", type_b->type->type_id);
            ValidationResult* result = schema_validator_validate_type(context->validator, a.to_const(), type_b->type);
            if (result->error_count > 0) {
                print_validation_result(result);
                log_debug("type validation failed with %d errors", result->error_count);
            } else {
                log_debug("type validation succeeded");
            }
            return result->valid ? BOOL_TRUE : BOOL_FALSE;
        }
    default:
        return a_type_id == type_b->type->type_id;
    }
}

// 3-states comparison
Bool fn_eq(Item a_item, Item b_item) {
    log_debug("equal_comp expr");
    if (a_item._type_id != b_item._type_id) {
        // special case: null comparison with any type returns false (not error)
        // this allows idiomatic null checking like: if (x == null) ...
        if (a_item._type_id == LMD_TYPE_NULL || b_item._type_id == LMD_TYPE_NULL) {
            return BOOL_FALSE;
        }
        // number promotion - only for int/float types
        if (LMD_TYPE_INT <= a_item._type_id && a_item._type_id <= LMD_TYPE_NUMBER &&
            LMD_TYPE_INT <= b_item._type_id && b_item._type_id <= LMD_TYPE_NUMBER) {
            double a_val = it2d(a_item), b_val = it2d(b_item);
            return (a_val == b_val) ? BOOL_TRUE : BOOL_FALSE;
        }
        // Type mismatch error for equality comparisons (e.g., true == 1)
        return BOOL_ERROR;
    }

    if (a_item._type_id == LMD_TYPE_NULL) {
        return BOOL_TRUE; // null == null
    }
    else if (a_item._type_id == LMD_TYPE_BOOL) {
        return (a_item.bool_val == b_item.bool_val) ? BOOL_TRUE : BOOL_FALSE;
    }
    else if (a_item._type_id == LMD_TYPE_INT) {
        return (a_item.get_int56() == b_item.get_int56()) ? BOOL_TRUE : BOOL_FALSE;
    }
    else if (a_item._type_id == LMD_TYPE_INT64) {
        return (a_item.get_int64() == b_item.get_int64()) ? BOOL_TRUE : BOOL_FALSE;
    }
    else if (a_item._type_id == LMD_TYPE_FLOAT) {
        return (a_item.get_double() == b_item.get_double()) ? BOOL_TRUE : BOOL_FALSE;
    }
    else if (a_item._type_id == LMD_TYPE_DECIMAL) {
        Decimal* dec_a = a_item.get_decimal();  Decimal* dec_b = b_item.get_decimal();
        int cmp = mpd_cmp(dec_a->dec_val, dec_b->dec_val, context->decimal_ctx);
        if (cmp < 0) return BOOL_FALSE;
        else if (cmp > 0) return BOOL_FALSE;
        else return BOOL_TRUE;
    }
    else if (a_item._type_id == LMD_TYPE_DTIME) {
        DateTime dt_a = a_item.get_datetime();  DateTime dt_b = b_item.get_datetime();
        // todo: do a normalized field comparison
        return (dt_a.int64_val == dt_b.int64_val) ? BOOL_TRUE : BOOL_FALSE;
    }
    else if (a_item._type_id == LMD_TYPE_STRING || a_item._type_id == LMD_TYPE_SYMBOL ||
        a_item._type_id == LMD_TYPE_BINARY) {
        String *str_a = a_item.get_string();  String *str_b = b_item.get_string();
        bool result = (str_a->len == str_b->len && strncmp(str_a->chars, str_b->chars, str_a->len) == 0);
        return result ? BOOL_TRUE : BOOL_FALSE;
    }
    log_error("unknown comparing type %d\n", a_item._type_id);
    return BOOL_ERROR;
}

Bool fn_ne(Item a_item, Item b_item) {
    Bool result = fn_eq(a_item, b_item);
    if (result == BOOL_ERROR) { return BOOL_ERROR; }
    return !result;
}

// 3-state value/ordered comparison
Bool fn_lt(Item a_item, Item b_item) {
    log_debug("less_comp expr");
    if (a_item._type_id != b_item._type_id) {
        // number promotion - only for int/float types
        if (LMD_TYPE_INT <= a_item._type_id && a_item._type_id <= LMD_TYPE_NUMBER &&
            LMD_TYPE_INT <= b_item._type_id && b_item._type_id <= LMD_TYPE_NUMBER) {
            double a_val = it2d(a_item), b_val = it2d(b_item);
            return (a_val < b_val) ? BOOL_TRUE : BOOL_FALSE;
        }
        // Type mismatch error for equality comparisons (e.g., true == 1, "test" != null)
        // Note: null can only be compared to null, any other comparison is a type error
        return BOOL_ERROR;
    }

    if (a_item._type_id == LMD_TYPE_NULL) {
        return BOOL_ERROR;  // null does not support <, >, <=, >=
    }
    else if (a_item._type_id == LMD_TYPE_BOOL) {
        return BOOL_ERROR;  // bool does not support <, >, <=, >=
    }
    else if (a_item._type_id == LMD_TYPE_INT) {
        return (a_item.get_int56() < b_item.get_int56()) ? BOOL_TRUE : BOOL_FALSE;
    }
    else if (a_item._type_id == LMD_TYPE_INT64) {
        return (a_item.get_int64() < b_item.get_int64()) ? BOOL_TRUE : BOOL_FALSE;
    }
    else if (a_item._type_id == LMD_TYPE_FLOAT) {
        return (a_item.get_double() < b_item.get_double()) ? BOOL_TRUE : BOOL_FALSE;
    }
    else if (a_item._type_id == LMD_TYPE_DECIMAL) {
        Decimal* dec_a = a_item.get_decimal();  Decimal* dec_b = b_item.get_decimal();
        int cmp = mpd_cmp(dec_a->dec_val, dec_b->dec_val, context->decimal_ctx);
        return (cmp < 0) ? BOOL_TRUE : BOOL_FALSE;
    }
    else if (a_item._type_id == LMD_TYPE_DTIME) {
        DateTime dt_a = a_item.get_datetime();  DateTime dt_b = b_item.get_datetime();
        // todo: do a proper normalized field comparison
        return dt_a.int64_val < dt_b.int64_val ? BOOL_TRUE : BOOL_FALSE;
    }
    else if (a_item._type_id == LMD_TYPE_STRING || a_item._type_id == LMD_TYPE_SYMBOL ||
        a_item._type_id == LMD_TYPE_BINARY) {
        String *str_a = a_item.get_string();  String *str_b = b_item.get_string();
        bool result = strcmp(str_a->chars, str_b->chars) < 0;
        return result ? BOOL_TRUE : BOOL_FALSE;
    }
    log_error("unknown comparing type %d\n", a_item._type_id);
    return BOOL_ERROR;
}

// 3-state value/ordered comparison
Bool fn_gt(Item a_item, Item b_item) {
    log_debug("greater_comp expr");
    if (a_item._type_id != b_item._type_id) {
        // number promotion - only for int/float types
        if (LMD_TYPE_INT <= a_item._type_id && a_item._type_id <= LMD_TYPE_NUMBER &&
            LMD_TYPE_INT <= b_item._type_id && b_item._type_id <= LMD_TYPE_NUMBER) {
            double a_val = it2d(a_item), b_val = it2d(b_item);
            log_debug("fn_gt: a_val %f, b_val %f", a_val, b_val);
            return (a_val > b_val) ? BOOL_TRUE : BOOL_FALSE;
        }
        // Type mismatch error for equality comparisons (e.g., true == 1, "test" != null)
        // Note: null can only be compared to null, any other comparison is a type error
        return BOOL_ERROR;
    }

    if (a_item._type_id == LMD_TYPE_NULL) {
        return BOOL_ERROR;  // null does not support <, >, <=, >=
    }
    else if (a_item._type_id == LMD_TYPE_BOOL) {
        return BOOL_ERROR;  // bool does not support <, >, <=, >=
    }
    else if (a_item._type_id == LMD_TYPE_INT) {
        return (a_item.get_int56() > b_item.get_int56()) ? BOOL_TRUE : BOOL_FALSE;
    }
    else if (a_item._type_id == LMD_TYPE_INT64) {
        return (a_item.get_int64() > b_item.get_int64()) ? BOOL_TRUE : BOOL_FALSE;
    }
    else if (a_item._type_id == LMD_TYPE_FLOAT) {
        return (a_item.get_double() > b_item.get_double()) ? BOOL_TRUE : BOOL_FALSE;
    }
    else if (a_item._type_id == LMD_TYPE_DECIMAL) {
        Decimal* dec_a = a_item.get_decimal();  Decimal* dec_b = b_item.get_decimal();
        int cmp = mpd_cmp(dec_a->dec_val, dec_b->dec_val, context->decimal_ctx);
        return (cmp > 0) ? BOOL_TRUE : BOOL_FALSE;
    }
    else if (a_item._type_id == LMD_TYPE_DTIME) {
        DateTime dt_a = a_item.get_datetime();  DateTime dt_b = b_item.get_datetime();
        // todo: do a proper normalized field comparison
        return dt_a.int64_val > dt_b.int64_val ? BOOL_TRUE : BOOL_FALSE;
    }
    else if (a_item._type_id == LMD_TYPE_STRING || a_item._type_id == LMD_TYPE_SYMBOL ||
        a_item._type_id == LMD_TYPE_BINARY) {
        String *str_a = a_item.get_string();  String *str_b = b_item.get_string();
        bool result = strcmp(str_a->chars, str_b->chars) > 0;
        return result ? BOOL_TRUE : BOOL_FALSE;
    }
    log_error("unknown comparing type %d\n", a_item._type_id);
    return BOOL_ERROR;
}

Bool fn_le(Item a_item, Item b_item) {
    log_debug("fn_le expr");
    Bool result = fn_gt(a_item, b_item);
    log_debug("fn_le result %d", result);
    if (result == BOOL_ERROR) return BOOL_ERROR;
    return !result;
}

Bool fn_ge(Item a_item, Item b_item) {
    Bool result = fn_lt(a_item, b_item);
    if (result == BOOL_ERROR) return BOOL_ERROR;
    return !result;
}

Bool fn_not(Item item) {
    // invert the truthiness of the item, not just logic NOT
    log_debug("fn_not expr");
    Bool result = is_truthy(item);
    if (result == BOOL_ERROR) return BOOL_ERROR;
    return !result;
}

// 3-state AND with short-circuiting truthy idiom
Item fn_and(Item a_item, Item b_item) {
    log_debug("fn_and called with types: %d, %d", a_item._type_id, b_item._type_id);
    // fast path for boolean types
    if (a_item._type_id == LMD_TYPE_BOOL && b_item._type_id == LMD_TYPE_BOOL) {
        log_debug("fn_and: bool fast path");
        return {.item = b2it(a_item.bool_val && b_item.bool_val)};
    }
    // fallback to generic truthy idiom
    log_debug("fn_and: generic truth fallback");
    Bool a_truth = is_truthy(a_item);
    if (a_truth == BOOL_ERROR) return ItemError;
    if (a_truth == BOOL_FALSE) return a_item; // short-circuit return
    if (b_item._type_id == LMD_TYPE_ERROR) return ItemError;
    return b_item;  // always return b_item, no matter truthy or falsy
}

// 3-state OR with short-circuiting truthy idiom
Item fn_or(Item a_item, Item b_item) {
    // fast path for boolean types
    if (a_item._type_id == LMD_TYPE_BOOL && b_item._type_id == LMD_TYPE_BOOL) {
        return {.item = b2it(a_item.bool_val || b_item.bool_val)};
    }
    // fallback to generic truthy idiom
    Bool a_truth = is_truthy(a_item);
    if (a_truth == BOOL_ERROR) return ItemError;
    if (a_truth == BOOL_TRUE) return a_item; // short-circuit return
    if (b_item._type_id == LMD_TYPE_ERROR) return ItemError;
    return b_item;  // always return b_item, no matter truthy or falsy
}

Bool fn_in(Item a_item, Item b_item) {
    log_debug("fn_in");
    if (b_item._type_id) { // b is scalar
        if (b_item._type_id == LMD_TYPE_STRING && a_item._type_id == LMD_TYPE_STRING) {
            String *str_a = a_item.get_string();  String *str_b = b_item.get_string();
            return str_a->len <= str_b->len && strstr(str_b->chars, str_a->chars) != NULL;
        }
    }
    else { // b is container
        TypeId b_type = b_item.container->type_id;
        if (b_type == LMD_TYPE_LIST) {
            List *list = b_item.list;
            for (int i = 0; i < list->length; i++) {
                if (fn_eq(list->items[i], a_item) == BOOL_TRUE) {
                    return true;
                }
            }
            return false;
        }
        else if (b_type == LMD_TYPE_RANGE) {
            Range *range = b_item.range;
            int64_t a_val = it2l(a_item);
            return range->start <= a_val && a_val <= range->end;
        }
        else if (b_type == LMD_TYPE_ARRAY) {
            Array *arr = b_item.array;
            for (int i = 0; i < arr->length; i++) {
                if (fn_eq(arr->items[i], a_item) == BOOL_TRUE) {
                    return true;
                }
            }
            return false;
        }
        else if (b_type == LMD_TYPE_ARRAY_INT) {
            ArrayInt *arr = b_item.array_int;
            for (int i = 0; i < arr->length; i++) {
                if (arr->items[i] == it2l(a_item)) {
                    return true;
                }
            }
            return false;
        }
        else if (b_type == LMD_TYPE_MAP) {
            // check if a is in map
        }
        else if (b_type == LMD_TYPE_ELEMENT) {
            // check if a is in element
        }
        else {
            log_debug("invalid type %d", b_type);
        }
    }
    return false;
}

String STR_NULL = {.len = 4, .ref_cnt = 0, .chars = "null"};
String STR_TRUE = {.len = 4, .ref_cnt = 0, .chars = "true"};
String STR_FALSE = {.len = 5, .ref_cnt = 0, .chars = "false"};

String* fn_string(Item itm) {
    TypeId type_id = get_type_id(itm);
    switch (type_id) {
    case LMD_TYPE_NULL:
        return &STR_NULL;
    case LMD_TYPE_BOOL:
        return itm.bool_val ? &STR_TRUE : &STR_FALSE;
    case LMD_TYPE_STRING:  case LMD_TYPE_SYMBOL:  case LMD_TYPE_BINARY:
        return itm.get_string();
    case LMD_TYPE_DTIME: {
        DateTime *dt = (DateTime*)itm.datetime_ptr;
        if (dt) {
            // Debug: Print the datetime precision and basic info
            log_debug("fn_string debug: DateTime precision=%d, hour=%d, minute=%d, second=%d, ms=%d",
                   dt->precision, dt->hour, dt->minute, dt->second, dt->millisecond);

            // Format datetime in Lambda format based on precision
            char buf[64];
            int len = 0;

            switch (dt->precision) {
                case DATETIME_PRECISION_YEAR_ONLY:
                    len = snprintf(buf, sizeof(buf), "t'%04d'", DATETIME_GET_YEAR(dt));
                    break;

                case DATETIME_PRECISION_DATE_ONLY:
                    len = snprintf(buf, sizeof(buf), "t'%04d-%02d-%02d'",
                        DATETIME_GET_YEAR(dt), DATETIME_GET_MONTH(dt), dt->day);
                    break;

                case DATETIME_PRECISION_TIME_ONLY: {
                    // Debug: Print the datetime values we're formatting
                    log_debug("fn_string debug: formatting time-only: %02d:%02d:%02d.%03d, tz_offset=%d",
                           dt->hour, dt->minute, dt->second, dt->millisecond,
                           DATETIME_HAS_TIMEZONE(dt) ? DATETIME_GET_TZ_OFFSET(dt) : -999);

                    // Format time-only without 'T' prefix
                    len = snprintf(buf, sizeof(buf), "t'%02d:%02d:%02d",
                        dt->hour, dt->minute, dt->second);

                    // Add milliseconds if non-zero
                    if (dt->millisecond > 0) {
                        len += snprintf(buf + len, sizeof(buf) - len, ".%03d", dt->millisecond);
                    }

                    // Add timezone - use 'z' for UTC (+00:00)
                    if (DATETIME_HAS_TIMEZONE(dt)) {
                        int tz_offset = DATETIME_GET_TZ_OFFSET(dt);
                        if (tz_offset == 0) {
                            len += snprintf(buf + len, sizeof(buf) - len, "z");
                        } else {
                            int hours = abs(tz_offset) / 60;
                            int minutes = abs(tz_offset) % 60;
                            len += snprintf(buf + len, sizeof(buf) - len, "%+03d:%02d",
                                tz_offset >= 0 ? hours : -hours, minutes);
                        }
                    }

                    len += snprintf(buf + len, sizeof(buf) - len, "'");
                    break;
                }

                case DATETIME_PRECISION_DATE_TIME:
                default: {
                    // Format full datetime with 'T' separator
                    len = snprintf(buf, sizeof(buf), "t'%04d-%02d-%02dT%02d:%02d:%02d",
                        DATETIME_GET_YEAR(dt), DATETIME_GET_MONTH(dt), dt->day,
                        dt->hour, dt->minute, dt->second);

                    // Add milliseconds if non-zero
                    if (dt->millisecond > 0) {
                        len += snprintf(buf + len, sizeof(buf) - len, ".%03d", dt->millisecond);
                    }

                    // Add timezone - use 'z' for UTC (+00:00)
                    if (DATETIME_HAS_TIMEZONE(dt)) {
                        int tz_offset = DATETIME_GET_TZ_OFFSET(dt);
                        if (tz_offset == 0) {
                            len += snprintf(buf + len, sizeof(buf) - len, "z");
                        } else {
                            int hours = abs(tz_offset) / 60;
                            int minutes = abs(tz_offset) % 60;
                            len += snprintf(buf + len, sizeof(buf) - len, "%+03d:%02d",
                                tz_offset >= 0 ? hours : -hours, minutes);
                        }
                    }

                    len += snprintf(buf + len, sizeof(buf) - len, "'");
                    break;
                }
            }

            return heap_strcpy(buf, len);
        } else {
            return &STR_NULL;
        }
    }
    case LMD_TYPE_INT: {
        char buf[32];
        int64_t int_val = itm.get_int56();
        snprintf(buf, sizeof(buf), "%lld", (long long)int_val);
        int len = strlen(buf);
        return heap_strcpy(buf, len);
    }
    case LMD_TYPE_INT64: {
        char buf[32];
        int64_t long_val = itm.get_int64();
        snprintf(buf, sizeof(buf), "%ld", long_val);
        int len = strlen(buf);
        return heap_strcpy(buf, len);
    }
    case LMD_TYPE_FLOAT: {
        char buf[32];
        double dval = itm.get_double();
        snprintf(buf, sizeof(buf), "%g", dval);
        int len = strlen(buf);
        return heap_strcpy(buf, len);
    }
    case LMD_TYPE_DECIMAL:  case LMD_TYPE_RANGE:  case LMD_TYPE_LIST:  case LMD_TYPE_ARRAY:
    case LMD_TYPE_ARRAY_INT:  case LMD_TYPE_ARRAY_INT64:  case LMD_TYPE_ARRAY_FLOAT:
    case LMD_TYPE_MAP:  case LMD_TYPE_ELEMENT: {
        StrBuf* sb = strbuf_new();
        print_item(sb, itm, 1, null);  // make list print as list, instead of beaking onto multiple lines
        String* result = heap_strcpy(sb->str, sb->length);
        strbuf_free(sb);
        return result;
    }
    case LMD_TYPE_ERROR:
        return NULL;
    default:
        // for other types
        log_error("fn_string unhandled type %d", itm._type_id);
        return &STR_NULL;
    }
}

Type* base_type(TypeId type_id) {
    return (type_id <= 0 || type_id > LMD_TYPE_ERROR) ?
        &LIT_TYPE_ERROR : ((TypeInfo*)context->type_info)[type_id].lit_type;
}

Type* const_type(int type_index) {
    ArrayList* type_list = (ArrayList*)context->type_list;
    if (type_index < 0 || type_index >= type_list->length) {
        return &LIT_TYPE_ERROR;
    }
    Type* type = (Type*)(type_list->data[type_index]);
        log_debug("const_type %d, %d, %p", type_index, type->type_id, type);
    return type;
}

Type* fn_type(Item item) {
    TypeType *type = (TypeType *)calloc(1, sizeof(TypeType) + sizeof(Type));
    Type *item_type = (Type *)((uint8_t *)type + sizeof(TypeType));
    type->type = item_type;  type->type_id = LMD_TYPE_TYPE;
    if (item._type_id) {
        item_type->type_id = item._type_id;
    }
    else if (item._type_id == LMD_TYPE_RAW_POINTER) {
        item_type->type_id = item.container->type_id;
    }
    return (Type*)type;
}

extern "C" Input* input_from_url(String* url, String* type, String* flavor, Url* cwd);

Input* input_data(Context* ctx, String* url, String* type, String* flavor) {
    log_debug("input_data at: %s, type: %s, flavor: %s",
        url ? url->chars : "null", type ? type->chars : "null",
        flavor ? flavor->chars : "null");
    // Pass NULL for cwd if ctx is NULL to avoid crash
    return input_from_url(url, type, flavor, ctx ? (Url*)ctx->cwd : NULL);
}

Item fn_input2(Item url, Item type) {
    String *url_str, *type_str = NULL, *flavor_str = NULL;
    if (url._type_id != LMD_TYPE_STRING && url._type_id != LMD_TYPE_SYMBOL) {
        log_debug("input url must be a string or symbol, got type %d", url._type_id);
        return ItemNull;  // todo: push error
    }
    else { url_str = url.get_string(); }
    log_debug("input url: %s", url_str->chars);

    TypeId type_id = get_type_id(type);
    if (type_id == LMD_TYPE_NULL) {
        // No type specified
        type_str = NULL;
    }
    else if (type_id == LMD_TYPE_STRING || type_id == LMD_TYPE_SYMBOL) {
        // Legacy behavior: type is a simple string/symbol
        type_str = type.get_string();
    }
    else if (type_id == LMD_TYPE_MAP) {
        log_debug("input type is a map");
        // New behavior: type is a map with options
        Map* options_map = type.map;

        // Extract 'type' from map
        bool is_found;
        Item input_type = _map_get((TypeMap*)options_map->type, options_map->data, "type", &is_found);
        if (!is_found || !input_type.item || input_type._type_id == LMD_TYPE_NULL) { // missing 'type' key
            type_str = NULL;
        } else {
            TypeId type_value_type = get_type_id(input_type);
            if (type_value_type == LMD_TYPE_STRING || type_value_type == LMD_TYPE_SYMBOL) {
                type_str = input_type.get_string();
            }
            else {
        log_debug("input type must be a string or symbol, got type %d", type_value_type);
                // todo: push error
                type_str = NULL;  // input type ignored
            }
        }

        // Extract 'flavor' from map
        Item input_flavor = _map_get((TypeMap*)options_map->type, options_map->data, "flavor", &is_found);
        if (!is_found || !input_flavor.item || input_flavor._type_id == LMD_TYPE_NULL) { // missing 'flavor' key
            flavor_str = NULL;
        } else {
            TypeId flavor_value_type = get_type_id(input_flavor);
            if (flavor_value_type == LMD_TYPE_STRING || flavor_value_type == LMD_TYPE_SYMBOL) {
                flavor_str = input_flavor.get_string();
            }
            else {
                log_debug("input flavor must be a string or symbol, got type %d", flavor_value_type);
                // todo: push error
                flavor_str = NULL;  // input flavor ignored
            }
        }
    }
    else {
        log_debug("input type must be a string, symbol, or map, got type %d", type_id);
        return ItemNull;  // todo: push error
    }

    // Check if context is properly initialized
    if (!context) {
        log_debug("Error: context is NULL in fn_input");
        return ItemNull;
    }

    log_debug("input type: %s, flavor: %s", type_str ? type_str->chars : "null", flavor_str ? flavor_str->chars : "null");
    Input *input = input_data(context, url_str, type_str, flavor_str);
    // todo: input should be cached in context
    return (input && input->root.item) ? input->root : ItemNull;
}

Item fn_input1(Item url) {
    return fn_input2(url, ItemNull);
}

extern "C" String* format_data(Item item, String* type, String* flavor, Pool *pool);

String* fn_format2(Item item, Item type) {
    TypeId type_id = get_type_id(type);
    String* type_str = NULL;
    String* flavor_str = NULL;

    if (type_id == LMD_TYPE_NULL) {
        type_str = NULL;  // use default
    }
    else if (type_id == LMD_TYPE_STRING || type_id == LMD_TYPE_SYMBOL) {
        // Legacy behavior: type is a simple string or symbol
        type_str = type.get_string();
    }
    else if (type_id == LMD_TYPE_MAP) {
        log_debug("format type is a map");
        // New behavior: type is a map with options
        Map* options_map = type.map;

        // Extract 'type' from map
        bool is_found;
        Item format_type = _map_get((TypeMap*)options_map->type, options_map->data, "type", &is_found);
        if (!is_found || !format_type.item || format_type._type_id == LMD_TYPE_NULL) { // missing 'type' key
            type_str = NULL;
        } else {
            TypeId type_value_type = get_type_id(format_type);
            if (type_value_type == LMD_TYPE_STRING || type_value_type == LMD_TYPE_SYMBOL) {
                type_str = format_type.get_string();
            }
            else {
                log_debug("format type must be a string or symbol, got type %d", type_value_type);
                // todo: push error
                type_str = NULL;  // format type ignored
            }
        }

        // Extract 'flavor' from map
        Item format_flavor = _map_get((TypeMap*)options_map->type, options_map->data, "flavor", &is_found);
        if (!is_found || !format_flavor.item || format_flavor._type_id == LMD_TYPE_NULL) { // missing 'flavor' key
            flavor_str = NULL;
        } else {
            TypeId flavor_value_type = get_type_id(format_flavor);
            if (flavor_value_type == LMD_TYPE_STRING || flavor_value_type == LMD_TYPE_SYMBOL) {
                flavor_str = format_flavor.get_string();
            }
            else {
                log_debug("format flavor must be a string or symbol, got type %d", flavor_value_type);
                // todo: push error
                flavor_str = NULL;  // format flavor ignored
            }
        }
    }
    else {
        log_debug("format type must be a string, symbol, or map, got type %d", type_id);
        return NULL;  // todo: push error
    }

    log_debug("format item type: %s, flavor: %s", type_str ? type_str->chars : "null", flavor_str ? flavor_str->chars : "null");
    String* result = format_data(item, type_str, flavor_str, context->heap->pool);
    if (result) {
         arraylist_append(context->heap->entries, (void*)s2it(result));
    }
    return result;
}

String* fn_format1(Item item) {
    return fn_format2(item, ItemNull);
}

#include "../lib/utf.h"

// generic field access function for any type
Item fn_index(Item item, Item index_item) {
    // Determine the type and delegate to appropriate getter
    int64_t index = -1;
    switch (index_item._type_id) {
    case LMD_TYPE_INT:
        index = index_item.get_int56();
        break;
    case LMD_TYPE_INT64:
        index = index_item.get_int64();
        break;
    case LMD_TYPE_FLOAT: {
        double dval = index_item.get_double();
        // check dval is an integer
        if (dval == (int64_t)dval) {
            index = (int64_t)dval;
        } else {
        log_debug("index must be an integer, got float %g", dval);
            return ItemNull;  // todo: push error
        }
        break;
    }
    case LMD_TYPE_STRING:  case LMD_TYPE_SYMBOL:
        return fn_member(item, index_item);
    default:
        log_debug("invalid index type %d", index_item._type_id);
        return ItemNull;
    }

    log_debug("fn_index item index: %ld", index);
    return item_at(item, index);
}

Item fn_member(Item item, Item key) {
    TypeId type_id = get_type_id(item);
    switch (type_id) {
    case LMD_TYPE_MAP: {
        Map *map = item.map;
        return map_get(map, key);
    }
    case LMD_TYPE_ELEMENT: {
        Element *elmt = item.element;
        return elmt_get(elmt, key);
    }
    case LMD_TYPE_LIST: {
        // Handle built-in properties for List type
        if (key._type_id == LMD_TYPE_STRING || key._type_id == LMD_TYPE_SYMBOL) {
            String *key_str = key.get_string();
            if (key_str && strcmp(key_str->chars, "length") == 0) {
                List *list = item.list;
                return {.item = i2it(list->length)};
            }
        }
        return ItemNull;
    }
    // todo: built-in properties for other types
    default:
        return ItemNull;
    }
}

// length of an item's content, relates to indexed access, i.e. item[index]
int64_t fn_len(Item item) {
    TypeId type_id = get_type_id(item);
    log_debug("fn_len item: %d", type_id);
    int64_t size = 0;
    switch (type_id) {
    case LMD_TYPE_LIST:
        size = item.list->length;
        break;
    case LMD_TYPE_RANGE:
        size = item.range->length;
        break;
    case LMD_TYPE_ARRAY:
        size = item.array->length;
        break;
    case LMD_TYPE_ARRAY_INT:
        size = item.array_int->length;
        break;
    case LMD_TYPE_ARRAY_INT64:
        size = item.array_int64->length;
        break;
    case LMD_TYPE_ARRAY_FLOAT:
        size = item.array_float->length;
        break;
    case LMD_TYPE_MAP: {
        size = 0;
        break;
    }
    case LMD_TYPE_ELEMENT: {
        Element *elmt = item.element;
        size = elmt->length;
        break;
    }
    case LMD_TYPE_STRING:  case LMD_TYPE_SYMBOL:  case LMD_TYPE_BINARY: {
        // returns the length of the string
        // todo: binary length
        String *str = item.get_string();  // todo:: should return char length
        size = str ? utf8_char_count(str->chars) : 0;
        break;
    }
    case LMD_TYPE_ERROR:
        return INT64_ERROR;
    default: // NULL and scalar types
        size = 0;
        break;
    }
    return size;
}

// substring system function - extracts a substring from start to end (exclusive)
Item fn_substring(Item str_item, Item start_item, Item end_item) {
    if (get_type_id(str_item) != LMD_TYPE_STRING) {
        log_debug("fn_substring: first argument must be a string");
        return ItemError;
    }

    if (get_type_id(start_item) != LMD_TYPE_INT && get_type_id(start_item) != LMD_TYPE_INT64) {
        log_debug("fn_substring: start index must be an integer");
        return ItemError;
    }

    if (get_type_id(end_item) != LMD_TYPE_INT && get_type_id(end_item) != LMD_TYPE_INT64) {
        log_debug("fn_substring: end index must be an integer");
        return ItemError;
    }

    String* str = str_item.get_string();
    if (!str || str->len == 0) {
        return str_item; // return empty string
    }

    int64_t start = it2l(start_item);
    int64_t end = it2l(end_item);

    // handle negative indices (count from end)
    int64_t char_len = utf8_char_count(str->chars);
    if (start < 0) start = char_len + start;
    if (end < 0) end = char_len + end;

    // clamp to valid range
    if (start < 0) start = 0;
    if (end > char_len) end = char_len;
    if (start >= end) {
        // return empty string
        String* empty = (String *)heap_alloc(sizeof(String) + 1, LMD_TYPE_STRING);
        empty->len = 0;
        empty->chars[0] = '\0';
        return {.item = s2it(empty)};
    }

    // convert char indices to byte indices
    long byte_start = utf8_char_to_byte_offset(str->chars, start);
    long byte_end = utf8_char_to_byte_offset(str->chars, end);

    if (byte_start >= str->len || byte_end < 0) {
        // return empty string
        String* empty = (String *)heap_alloc(sizeof(String) + 1, LMD_TYPE_STRING);
        empty->len = 0;
        empty->chars[0] = '\0';
        return {.item = s2it(empty)};
    }

    long result_len = byte_end - byte_start;
    String* result = (String *)heap_alloc(sizeof(String) + result_len + 1, LMD_TYPE_STRING);
    result->len = result_len;
    memcpy(result->chars, str->chars + byte_start, result_len);
    result->chars[result_len] = '\0';

    return {.item = s2it(result)};
}

// contains system function - checks if a string contains a substring
Item fn_contains(Item str_item, Item substr_item) {
    if (get_type_id(str_item) != LMD_TYPE_STRING) {
        log_debug("fn_contains: first argument must be a string");
        return ItemError;
    }

    if (get_type_id(substr_item) != LMD_TYPE_STRING) {
        log_debug("fn_contains: second argument must be a string");
        return ItemError;
    }

    String* str = str_item.get_string();
    String* substr = substr_item.get_string();

    if (!str || !substr) {
        return {.item = b2it(false)};
    }

    if (substr->len == 0) {
        return {.item = b2it(true)}; // empty string is contained in any string
    }

    if (str->len == 0 || substr->len > str->len) {
        return {.item = b2it(false)};
    }

    // simple byte-based search for now - could be optimized with KMP or Boyer-Moore
    for (int i = 0; i <= str->len - substr->len; i++) {
        if (memcmp(str->chars + i, substr->chars, substr->len) == 0) {
            return {.item = b2it(true)};
        }
    }

    return {.item = b2it(false)};
}

// Static DateTime instance to avoid dynamic allocation issues
static DateTime static_dt;
static bool static_dt_initialized = false;

// DateTime system function - creates a current DateTime
DateTime fn_datetime() {
    // Use a static DateTime to avoid heap allocation issues - this is not roubust, not thread-safe
    if (!static_dt_initialized) {
        memset(&static_dt, 0, sizeof(DateTime));
        static_dt_initialized = true;
    }

    // Get current time
    time_t now = time(NULL);
    struct tm* tm_utc = gmtime(&now);
    if (tm_utc) {
        // Set date and time from current UTC time
        DATETIME_SET_YEAR_MONTH(&static_dt, tm_utc->tm_year + 1900, tm_utc->tm_mon + 1);
        static_dt.day = tm_utc->tm_mday;
        static_dt.hour = tm_utc->tm_hour;
        static_dt.minute = tm_utc->tm_min;
        static_dt.second = tm_utc->tm_sec;
        static_dt.millisecond = 0;
    }

    // Set as UTC timezone
    DATETIME_SET_TZ_OFFSET(&static_dt, 0);
    static_dt.precision = DATETIME_PRECISION_DATE_TIME;
    static_dt.format_hint = DATETIME_FORMAT_ISO8601_UTC;

    return static_dt;
}

// Thread-local storage for current variadic arguments list
// Set by variadic function calls, accessed by fn_varg functions
__thread List* current_vargs = NULL;

// Set current variadic arguments (called before variadic function body)
void set_vargs(List* vargs) {
    current_vargs = vargs;
}

// varg() - returns all variadic arguments as a list
Item fn_varg0() {
    if (current_vargs == NULL) {
        log_debug("fn_varg0: no variadic args, returning empty list");
        // return an empty list - allocate directly without frame context
        List* empty = (List*)heap_calloc(sizeof(List), LMD_TYPE_LIST);
        empty->type_id = LMD_TYPE_LIST;
        empty->length = 0;
        empty->capacity = 0;
        empty->items = NULL;
        return {.list = empty};
    }
    return {.list = current_vargs};
}

// varg(n) - returns the nth variadic argument
Item fn_varg1(Item index_item) {
    int64_t index = it2l(index_item);
    if (current_vargs == NULL) {
        log_debug("fn_varg1: no variadic args available");
        return ItemNull;
    }
    if (index < 0 || index >= current_vargs->length) {
        log_debug("fn_varg1: index %lld out of bounds (length %lld)", index, current_vargs->length);
        return ItemNull;
    }
    return current_vargs->items[index];
}
