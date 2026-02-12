#include "transpiler.hpp"
#include "lambda-decimal.hpp"
#include "lambda-error.h"
#include "../lib/log.h"
#include "utf_string.h"
#include "re2_wrapper.hpp"
#include <mpdecimal.h>  // needed for inline decimal operations

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

// forward declaration of static error string (defined later in this file)
extern String STR_ERROR;

// External path resolution function (implemented in path.c)
extern "C" Item path_resolve_for_iteration(Path* path);

// External path functions for path ++ operation
extern "C" Pool* eval_context_get_pool(EvalContext* ctx);
extern "C" Path* path_extend(Pool* pool, Path* base, const char* segment);
extern "C" Path* path_concat(Pool* pool, Path* base, Path* suffix);
extern "C" PathScheme path_get_scheme(Path* path);
extern "C" bool path_is_absolute(Path* path);

// External typeset function

#define stack_alloc(size) alloca(size);

#define Malloc malloc
#define Realloc realloc

Item _map_get(TypeMap* map_type, void* map_data, char *key, bool *is_found);

// =============================================================================
// Runtime Error Helper
// =============================================================================

/**
 * Set a runtime error in the current evaluation context.
 * Captures a stack trace using native frame pointer walking.
 */
static void set_runtime_error(LambdaErrorCode code, const char* format, ...) {
    if (!context) return;

    char message[1024];
    va_list args;
    va_start(args, format);
    vsnprintf(message, sizeof(message), format, args);
    va_end(args);

    // create error with current source location
    SourceLocation loc = {0};
    if (context->current_file) {
        loc.file = context->current_file;
    }

    LambdaError* error = err_create(code, message, &loc);

    // capture native stack trace via FP walking
    error->stack_trace = err_capture_stack_trace(context->debug_info, 32);

    // store in context
    if (context->last_error) {
        err_free(context->last_error);
    }
    context->last_error = error;

    log_error("runtime error [%d]: %s", code, message);
}

/**
 * Public API to set a runtime error from external modules (e.g., lambda-stack.cpp)
 * Does NOT capture stack trace since it may be called in low-stack situations.
 */
extern "C" void set_runtime_error_no_trace(LambdaErrorCode code, const char* message) {
    if (!context) return;

    SourceLocation loc = {0};
    if (context->current_file) {
        loc.file = context->current_file;
    }

    LambdaError* error = err_create(code, message, &loc);
    // Skip stack trace capture - may be called in low-stack conditions

    if (context->last_error) {
        err_free(context->last_error);
    }
    context->last_error = error;

    log_error("runtime error [%d]: %s", code, message);
}

/**
 * fn_error(message) - raise a user error with stack trace
 * This is the Lambda sys func that triggers a runtime error.
 */
Item fn_error(Item message) {
    const char* msg = "Error";
    if (get_type_id(message) == LMD_TYPE_STRING) {
        String* str = it2s(message);
        if (str && str->chars) {
            msg = str->chars;
        }
    }
    set_runtime_error(ERR_USER_ERROR, "%s", msg);
    return ItemError;
}

Bool is_truthy(Item item) {
    log_debug("is_truthy: item=0x%llx, type=%d, bool_val=%d", (unsigned long long)item.item, item._type_id, (int)item.bool_val);
    switch (item._type_id) {
    case LMD_TYPE_NULL:
        return BOOL_FALSE;
    case LMD_TYPE_ERROR:
        return BOOL_FALSE;  // errors are falsy — use `if (^e)` to check for errors
    case LMD_TYPE_BOOL:
        log_debug("is_truthy: BOOL case, bool_val=%d, returning %d", (int)item.bool_val, item.bool_val ? BOOL_TRUE : BOOL_FALSE);
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
        return &STR_ERROR;
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
    GUARD_ERROR2(left, right);
    // concat two scalars, or join two lists/arrays/maps, or join scalar with list/array, or join two binaries, else error
    TypeId left_type = get_type_id(left), right_type = get_type_id(right);
    log_debug("fn_join: %d, %d", left_type, right_type);

    // Handle path concatenation first (path must be on the left)
    if (left_type == LMD_TYPE_PATH) {
        Path* left_path = left.path;
        Pool* pool = eval_context_get_pool(context);

        if (right_type == LMD_TYPE_STRING) {
            // path ++ string: add string as new segment
            String* right_str = right.get_string();
            Path* result = path_extend(pool, left_path, right_str->chars);
            return {.path = result};
        }
        else if (right_type == LMD_TYPE_SYMBOL) {
            // path ++ symbol: add symbol name as new segment
            Symbol* right_sym = right.get_symbol();
            Path* result = path_extend(pool, left_path, right_sym->chars);
            return {.path = result};
        }
        else if (right_type == LMD_TYPE_PATH) {
            // path ++ path: concat if right is relative, error if absolute
            Path* right_path = right.path;
            if (path_is_absolute(right_path)) {
                set_runtime_error(ERR_TYPE_MISMATCH, "fn_join: cannot concatenate path with absolute path");
                return ItemError;
            }
            // Concatenate relative/parent path to base path
            Path* result = path_concat(pool, left_path, right_path);
            return {.path = result};
        }
        else {
            set_runtime_error(ERR_TYPE_MISMATCH, "fn_join: path can only be concatenated with string, symbol, or relative path, got %s",
                type_info[right_type].name);
            return ItemError;
        }
    }

    if (left_type == LMD_TYPE_STRING || right_type == LMD_TYPE_STRING) {
        // null ++ string → string, string ++ null → string
        if (left_type == LMD_TYPE_NULL) return right;
        if (right_type == LMD_TYPE_NULL) return left;
        String *left_str = fn_string(left), *right_str = fn_string(right);
        String *result = fn_strcat(left_str, right_str);
        return {.item = s2it(result)};
    }
    else if (left_type == LMD_TYPE_SYMBOL || right_type == LMD_TYPE_SYMBOL) {
        // null ++ symbol → symbol, symbol ++ null → symbol
        if (left_type == LMD_TYPE_NULL) return right;
        if (right_type == LMD_TYPE_NULL) return left;
        String *left_str = fn_string(left), *right_str = fn_string(right);
        String *result = fn_strcat(left_str, right_str);
        // Create a proper Symbol from the concatenated string
        Symbol* sym = heap_create_symbol(result->chars, result->len);
        return {.item = y2it(sym)};
    }
    // merge two array-like types (List, Array, ArrayInt, ArrayInt64, ArrayFloat)
    else if ((left_type >= LMD_TYPE_ARRAY_INT && left_type <= LMD_TYPE_ARRAY) || left_type == LMD_TYPE_LIST) {
        if (!((right_type >= LMD_TYPE_ARRAY_INT && right_type <= LMD_TYPE_ARRAY) || right_type == LMD_TYPE_LIST)) {
            set_runtime_error(ERR_TYPE_MISMATCH, "fn_join: unsupported operand types: %s and %s",
                type_info[left_type].name, type_info[right_type].name);
            return ItemError;
        }
        // same-type optimization: direct memcpy of native items
        if (left_type == right_type) {
            if (left_type == LMD_TYPE_ARRAY_INT) {
                ArrayInt *la = left.array_int, *ra = right.array_int;
                int64_t total = la->length + ra->length;
                ArrayInt *result = (ArrayInt *)heap_calloc(sizeof(ArrayInt), LMD_TYPE_ARRAY_INT);
                result->type_id = LMD_TYPE_ARRAY_INT;
                result->length = total;  result->capacity = total;
                result->items = (int64_t*)malloc(total * sizeof(int64_t));
                memcpy(result->items, la->items, sizeof(int64_t)*la->length);
                memcpy(result->items + la->length, ra->items, sizeof(int64_t)*ra->length);
                return {.array_int = result};
            }
            else if (left_type == LMD_TYPE_ARRAY_INT64) {
                ArrayInt64 *la = left.array_int64, *ra = right.array_int64;
                int64_t total = la->length + ra->length;
                ArrayInt64 *result = (ArrayInt64 *)heap_calloc(sizeof(ArrayInt64), LMD_TYPE_ARRAY_INT64);
                result->type_id = LMD_TYPE_ARRAY_INT64;
                result->length = total;  result->capacity = total;
                result->items = (int64_t*)malloc(total * sizeof(int64_t));
                memcpy(result->items, la->items, sizeof(int64_t)*la->length);
                memcpy(result->items + la->length, ra->items, sizeof(int64_t)*ra->length);
                return {.array_int64 = result};
            }
            else if (left_type == LMD_TYPE_ARRAY_FLOAT) {
                ArrayFloat *la = left.array_float, *ra = right.array_float;
                int64_t total = la->length + ra->length;
                ArrayFloat *result = (ArrayFloat *)heap_calloc(sizeof(ArrayFloat), LMD_TYPE_ARRAY_FLOAT);
                result->type_id = LMD_TYPE_ARRAY_FLOAT;
                result->length = total;  result->capacity = total;
                result->items = (double*)malloc(total * sizeof(double));
                memcpy(result->items, la->items, sizeof(double)*la->length);
                memcpy(result->items + la->length, ra->items, sizeof(double)*ra->length);
                return {.array_float = result};
            }
            // LMD_TYPE_ARRAY or LMD_TYPE_LIST: both use Item* items (same struct layout)
            Array *la = left.array, *ra = right.array;
            int64_t total_len = la->length + ra->length;
            int64_t total_extra = la->extra + ra->extra;
            Array *result = (Array *)heap_calloc(sizeof(Array) + sizeof(Item)*(total_len + total_extra), left_type);
            result->type_id = left_type;
            result->length = total_len;  result->extra = total_extra;
            result->items = (Item*)(result + 1);
            memcpy(result->items, la->items, sizeof(Item)*la->length);
            memcpy(result->items + la->length, ra->items, sizeof(Item)*ra->length);
            return {.array = result};
        }
        // different types: produce generic Array, convert typed elements to Items
        int64_t left_len = fn_len(left), right_len = fn_len(right);
        int64_t total_len = left_len + right_len;
        Array *result = (Array *)heap_calloc(sizeof(Array) + sizeof(Item)*total_len, LMD_TYPE_ARRAY);
        result->type_id = LMD_TYPE_ARRAY;
        result->length = total_len;  result->extra = 0;
        result->items = (Item*)(result + 1);
        for (int64_t i = 0; i < left_len; i++)  result->items[i] = item_at(left, i);
        for (int64_t i = 0; i < right_len; i++) result->items[left_len + i] = item_at(right, i);
        return {.array = result};
    }
    else {
        set_runtime_error(ERR_TYPE_MISMATCH, "fn_join: unsupported operand types: %s and %s",
            type_info[left_type].name, type_info[right_type].name);
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
        log_debug("normalize: first argument must be a string, got type: %s", get_type_name(str_item._type_id));
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
        log_error("unknown range type: %s, %s", get_type_name(item_a._type_id), get_type_name(item_b._type_id));
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
    fn->name = NULL;
    return fn;
}

// Create function with arity and name for stack traces
Function* to_fn_named(fn_ptr ptr, int arity, const char* name) {
    log_debug("create fn %p with arity %d, name %s", ptr, arity, name ? name : "(null)");
    Function *fn = (Function*)calloc(1, sizeof(Function));
    fn->type_id = LMD_TYPE_FUNC;
    fn->ref_cnt = 1;
    fn->arity = (uint8_t)arity;
    fn->ptr = ptr;
    fn->closure_env = NULL;
    fn->name = name;  // should be interned string, no need to copy
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
    fn->name = NULL;
    return fn;
}

// Create a closure with captured environment and name for stack traces
Function* to_closure_named(fn_ptr ptr, int arity, void* env, const char* name) {
    log_debug("create closure %p with arity %d, env %p, name %s", ptr, arity, env, name ? name : "(null)");
    Function* fn = (Function*)calloc(1, sizeof(Function));
    fn->type_id = LMD_TYPE_FUNC;
    fn->ref_cnt = 1;
    fn->fn_type = NULL;
    fn->arity = (uint8_t)arity;
    fn->ptr = ptr;
    fn->closure_env = env;
    fn->name = name;  // should be interned string, no need to copy
    return fn;
}

// Helper to validate that an Item is actually a function
// When Items are passed to fn_call*, they get cast to Function*
// We need to check if this is a valid pointer to a Function struct
static inline bool is_valid_function(Function* fn) {
    if (!fn) return false;

    // Check if this looks like a tagged Item value rather than a real pointer
    // Tagged Items have the type_id in the high byte (bits 56-63)
    // Valid heap pointers have the high byte as 0 on most platforms
    uint64_t val = (uint64_t)fn;
    uint8_t high_byte = (val >> 56) & 0xFF;
    if (high_byte != 0) {
        // This is a tagged value (like ItemError), not a valid pointer
        return false;
    }

    // Reject small values that can't be valid heap pointers
    // Typical heap addresses are well above 4KB (first page is reserved)
    // On most systems, valid heap addresses are > 0x10000
    if (val < 0x10000) {
        return false;
    }

    // Now safe to dereference - check that type_id matches Function
    TypeId type_id = *(TypeId*)fn;
    return type_id == LMD_TYPE_FUNC;
}

// Dynamic function dispatch for first-class functions
// For closures, env is passed as the first argument
// Stack traces are captured via native stack walking (no push/pop needed)
Item fn_call(Function* fn, List* args) {
    if (!is_valid_function(fn)) {
        set_runtime_error(ERR_INVALID_CALL, "fn_call: invalid function (null or wrong type)");
        return ItemError;
    }
    if (!fn->ptr) {
        set_runtime_error(ERR_INVALID_CALL, "fn_call: null function pointer");
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
                set_runtime_error(ERR_ARGUMENT_COUNT_MISMATCH, "fn_call: unsupported argument count %d for closure (max 7)", arg_count);
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
        set_runtime_error(ERR_INVALID_CALL, "fn_call0: cannot call non-function value");
        return ItemError;
    }
    if (!fn->ptr) {
        set_runtime_error(ERR_INVALID_CALL, "fn_call0: null function pointer");
        return ItemError;
    }
    if (fn->closure_env) {
        return ((Item(*)(void*))fn->ptr)(fn->closure_env);
    } else {
        return ((Item(*)())fn->ptr)();
    }
}

Item fn_call1(Function* fn, Item a) {
    if (!is_valid_function(fn)) {
        set_runtime_error(ERR_INVALID_CALL, "fn_call1: cannot call non-function value");
        return ItemError;
    }
    if (!fn->ptr) {
        set_runtime_error(ERR_INVALID_CALL, "fn_call1: null function pointer");
        return ItemError;
    }
    if (fn->closure_env) {
        return ((Item(*)(void*,Item))fn->ptr)(fn->closure_env, a);
    } else {
        return ((Item(*)(Item))fn->ptr)(a);
    }
}

Item fn_call2(Function* fn, Item a, Item b) {
    if (!is_valid_function(fn)) {
        set_runtime_error(ERR_INVALID_CALL, "fn_call2: cannot call non-function value");
        return ItemError;
    }
    if (!fn->ptr) {
        set_runtime_error(ERR_INVALID_CALL, "fn_call2: null function pointer");
        return ItemError;
    }
    if (fn->closure_env) {
        return ((Item(*)(void*,Item,Item))fn->ptr)(fn->closure_env, a, b);
    } else {
        return ((Item(*)(Item,Item))fn->ptr)(a, b);
    }
}

Item fn_call3(Function* fn, Item a, Item b, Item c) {
    if (!is_valid_function(fn)) {
        set_runtime_error(ERR_INVALID_CALL, "fn_call3: cannot call non-function value");
        return ItemError;
    }
    if (!fn->ptr) {
        set_runtime_error(ERR_INVALID_CALL, "fn_call3: null function pointer");
        return ItemError;
    }
    if (fn->closure_env) {
        return ((Item(*)(void*,Item,Item,Item))fn->ptr)(fn->closure_env, a, b, c);
    } else {
        return ((Item(*)(Item,Item,Item))fn->ptr)(a, b, c);
    }
}

Bool fn_is(Item a, Item b) {
    log_debug("fn_is");
    TypeId b_type_id = get_type_id(b);

    if (b_type_id != LMD_TYPE_TYPE) {
        log_error("2nd argument must be a type or pattern, got type: %s", get_type_name(b_type_id));
        return BOOL_ERROR;
    }

    Type* b_type = b.type;  // all type variants now share type_id = LMD_TYPE_TYPE

    // Handle pattern matching: "str" is pattern
    if (b_type->kind == TYPE_KIND_PATTERN) {
        TypeId a_type_id = get_type_id(a);
        if (a_type_id != LMD_TYPE_STRING && a_type_id != LMD_TYPE_SYMBOL) {
            log_error("pattern matching requires string or symbol, got type: %s", get_type_name(a_type_id));
            return BOOL_ERROR;
        }
        TypePattern* pattern = (TypePattern*)b_type;
        const char* chars = a.get_chars();
        uint32_t len = a.get_len();
        log_debug("fn_is pattern matching: str=%.*s", (int)len, chars);
        return pattern_full_match_chars(pattern, chars, len) ? BOOL_TRUE : BOOL_FALSE;
    }

    // If b is a TypeUnary directly (kind = TYPE_KIND_UNARY), handle it directly
    if (b_type->kind == TYPE_KIND_UNARY) {
        TypeUnary* type_unary = (TypeUnary*)b_type;
        log_debug("fn_is: TypeUnary (direct), op=%d, min=%d, max=%d",
                  type_unary->op, type_unary->min_count, type_unary->max_count);
        // Use full type validation for occurrence types
        ValidationResult* result = schema_validator_validate_type(context->validator, a.to_const(), b_type);
        if (result->error_count > 0) {
            print_validation_result(result);
            log_debug("type validation failed with %d errors", result->error_count);
        } else {
            log_debug("type validation succeeded");
        }
        return result->valid ? BOOL_TRUE : BOOL_FALSE;
    }

    // If b is a TypeBinary directly (kind = TYPE_KIND_BINARY), handle it via validator
    if (b_type->kind == TYPE_KIND_BINARY) {
        TypeBinary* type_binary = (TypeBinary*)b_type;
        log_debug("fn_is: TypeBinary (direct), op=%d", type_binary->op);
        // Use full type validation for union/intersection types
        ValidationResult* result = schema_validator_validate_type(context->validator, a.to_const(), b_type);
        if (result->error_count > 0) {
            print_validation_result(result);
            log_debug("type validation failed with %d errors", result->error_count);
        } else {
            log_debug("type validation succeeded");
        }
        return result->valid ? BOOL_TRUE : BOOL_FALSE;
    }

    TypeType *type_b = (TypeType *)b_type;
    TypeId a_type_id = get_type_id(a);

    // Check if inner type is TypeUnary (occurrence operator: ?, +, *, [n], [n+], [n,m])
    log_debug("fn_is: checking inner type, type_b->type->kind=%d", type_b->type->kind);
    if (type_b->type->kind == TYPE_KIND_UNARY) {
        TypeUnary* type_unary = (TypeUnary*)type_b->type;
        log_debug("fn_is: TypeUnary detected, op=%d (REPEAT=%d, OPTIONAL=%d)",
                  type_unary->op, OPERATOR_REPEAT, OPERATOR_OPTIONAL);
        // Use full type validation for occurrence types
        log_debug("fn_is: TypeUnary occurrence operator detected, using validator");
        ValidationResult* result = schema_validator_validate_type(context->validator, a.to_const(), type_b->type);
        if (result->error_count > 0) {
            print_validation_result(result);
            log_debug("type validation failed with %d errors", result->error_count);
        } else {
            log_debug("type validation succeeded");
        }
        return result->valid ? BOOL_TRUE : BOOL_FALSE;
    }

    // Check if inner type is TypeBinary (union/intersection: |, &, \)
    if (type_b->type->kind == TYPE_KIND_BINARY) {
        TypeBinary* type_binary = (TypeBinary*)type_b->type;
        log_debug("fn_is: TypeBinary detected (wrapped), op=%d", type_binary->op);
        // Use full type validation for union/intersection types
        ValidationResult* result = schema_validator_validate_type(context->validator, a.to_const(), type_b->type);
        if (result->error_count > 0) {
            print_validation_result(result);
            log_debug("type validation failed with %d errors", result->error_count);
        } else {
            log_debug("type validation succeeded");
        }
        return result->valid ? BOOL_TRUE : BOOL_FALSE;
    }

    log_debug("is type %d, %d", a_type_id, type_b->type->type_id);
    switch (type_b->type->type_id) {
    case LMD_TYPE_ANY:
        return a_type_id == LMD_TYPE_ERROR ? BOOL_FALSE : BOOL_TRUE;
    case LMD_TYPE_INT:  case LMD_TYPE_INT64:  case LMD_TYPE_FLOAT:  case LMD_TYPE_DECIMAL:  case LMD_TYPE_NUMBER:
        return LMD_TYPE_INT <= a_type_id && a_type_id <= type_b->type->type_id;
    case LMD_TYPE_DTIME:
        if (a_type_id != LMD_TYPE_DTIME) return BOOL_FALSE;
        // sub-type checks: date and time are sub-types of datetime
        if (type_b->type == &TYPE_DATE) {
            DateTime dt = a.get_datetime();
            return (dt.precision == DATETIME_PRECISION_DATE_ONLY || dt.precision == DATETIME_PRECISION_YEAR_ONLY)
                ? BOOL_TRUE : BOOL_FALSE;
        }
        if (type_b->type == &TYPE_TIME) {
            DateTime dt = a.get_datetime();
            return (dt.precision == DATETIME_PRECISION_TIME_ONLY) ? BOOL_TRUE : BOOL_FALSE;
        }
        return BOOL_TRUE;  // is datetime (any precision matches)
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
        return (datetime_compare(&dt_a, &dt_b) == 0) ? BOOL_TRUE : BOOL_FALSE;
    }
    else if (a_item._type_id == LMD_TYPE_STRING || a_item._type_id == LMD_TYPE_BINARY) {
        const char* chars_a = a_item.get_chars();  const char* chars_b = b_item.get_chars();
        uint32_t len_a = a_item.get_len();  uint32_t len_b = b_item.get_len();
        bool result = (len_a == len_b && strncmp(chars_a, chars_b, len_a) == 0);
        return result ? BOOL_TRUE : BOOL_FALSE;
    }
    else if (a_item._type_id == LMD_TYPE_SYMBOL) {
        // for symbols, also compare namespace
        Symbol* sym_a = (Symbol*)a_item.symbol_ptr;
        Symbol* sym_b = (Symbol*)b_item.symbol_ptr;
        if (sym_a->len != sym_b->len) return BOOL_FALSE;
        if (strncmp(sym_a->chars, sym_b->chars, sym_a->len) != 0) return BOOL_FALSE;
        // compare namespaces - both NULL or same URL
        return target_equal(sym_a->ns, sym_b->ns) ? BOOL_TRUE : BOOL_FALSE;
    }
    log_error("unknown comparing type: %s", get_type_name(a_item._type_id));
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
        return (datetime_compare(&dt_a, &dt_b) < 0) ? BOOL_TRUE : BOOL_FALSE;
    }
    else if (a_item._type_id == LMD_TYPE_STRING || a_item._type_id == LMD_TYPE_SYMBOL ||
        a_item._type_id == LMD_TYPE_BINARY) {
        const char* chars_a = a_item.get_chars();  const char* chars_b = b_item.get_chars();
        bool result = strcmp(chars_a, chars_b) < 0;
        return result ? BOOL_TRUE : BOOL_FALSE;
    }
    log_error("unknown comparing type: %s", get_type_name(a_item._type_id));
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
        return (datetime_compare(&dt_a, &dt_b) > 0) ? BOOL_TRUE : BOOL_FALSE;
    }
    else if (a_item._type_id == LMD_TYPE_STRING || a_item._type_id == LMD_TYPE_SYMBOL ||
        a_item._type_id == LMD_TYPE_BINARY) {
        const char* chars_a = a_item.get_chars();  const char* chars_b = b_item.get_chars();
        bool result = strcmp(chars_a, chars_b) > 0;
        return result ? BOOL_TRUE : BOOL_FALSE;
    }
    log_error("unknown comparing type: %s", get_type_name(a_item._type_id));
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
String STR_ERROR = {.len = 7, .ref_cnt = 0, .chars = "<error>"};

String* fn_string(Item itm) {
    TypeId type_id = get_type_id(itm);
    switch (type_id) {
    case LMD_TYPE_NULL:
        return &STR_NULL;
    case LMD_TYPE_BOOL:
        return itm.bool_val ? &STR_TRUE : &STR_FALSE;
    case LMD_TYPE_STRING:  case LMD_TYPE_BINARY:
        return itm.get_string();
    case LMD_TYPE_SYMBOL: {
        // Symbol has different layout than String; create a String from symbol chars
        Symbol* sym = itm.get_symbol();
        if (!sym) return &STR_NULL;
        return heap_strcpy(sym->chars, sym->len);
    }
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
    case LMD_TYPE_PATH: {
        Path* path = (Path*)itm.item;
        if (!path) return &STR_NULL;
        StrBuf* sb = strbuf_new();
        path_to_string(path, sb);
        String* result = heap_strcpy(sb->str, sb->length);
        strbuf_free(sb);
        return result;
    }
    case LMD_TYPE_ERROR:
        return &STR_ERROR;  // static error string — never NULL, prevents crash in callers
    default:
        // for other types
        log_error("fn_string unhandled type: %s", get_type_name(itm._type_id));
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

TypePattern* const_pattern(int pattern_index) {
    ArrayList* type_list = (ArrayList*)context->type_list;
    if (pattern_index < 0 || pattern_index >= type_list->length) {
        log_error("const_pattern: invalid index %d", pattern_index);
        return nullptr;
    }
    Type* type = (Type*)(type_list->data[pattern_index]);
    if (type->kind != TYPE_KIND_PATTERN) {
        log_error("const_pattern: index %d is not a pattern, got type: %s (kind=%d)", pattern_index, get_type_name(type->type_id), type->kind);
        return nullptr;
    }
    log_debug("const_pattern %d -> %p", pattern_index, type);
    return (TypePattern*)type;
}

Type* fn_type(Item item) {
    TypeType *type = (TypeType *)calloc(1, sizeof(TypeType) + sizeof(Type));
    Type *item_type = (Type *)((uint8_t *)type + sizeof(TypeType));
    type->type = item_type;  type->type_id = LMD_TYPE_TYPE;
    if (item._type_id) {
        item_type->type_id = item._type_id;
    }
    else if (item.item == 0) {
        // handle raw 0 value (from y2it(nullptr) etc.) as null
        item_type->type_id = LMD_TYPE_NULL;
    }
    else if (item._type_id == LMD_TYPE_RAW_POINTER && item.container) {
        item_type->type_id = item.container->type_id;
    }
    else {
        // fallback to null for any other case
        item_type->type_id = LMD_TYPE_NULL;
    }
    return (Type*)type;
}

/**
 * fn_name(item) - return the (local) name of an element, function, or type
 * Returns the name as a symbol, or nullptr if the item doesn't have a name.
 */
Symbol* fn_name(Item item) {
    TypeId type_id = get_type_id(item);

    switch (type_id) {
    case LMD_TYPE_NULL:
    case LMD_TYPE_ERROR:
        return nullptr;
    case LMD_TYPE_ELEMENT: {
        // return the element's tag name as a symbol
        Element* elmt = item.element;
        if (!elmt || !elmt->type) return nullptr;
        TypeElmt* elmt_type = (TypeElmt*)elmt->type;
        if (elmt_type->name.str && elmt_type->name.length > 0) {
            return heap_create_symbol(elmt_type->name.str, elmt_type->name.length);
        }
        return nullptr;
    }
    case LMD_TYPE_FUNC: {
        // return the function's name as a symbol
        Function* fn = item.function;
        if (!fn) return nullptr;
        // function name is stored in the name field
        if (fn->name && strlen(fn->name) > 0) {
            return heap_create_symbol(fn->name, strlen(fn->name));
        }
        // We return null for anonymous functions
        return nullptr;
    }
    case LMD_TYPE_TYPE: {
        // return the type's name as a symbol (e.g., "int", "string", "User")
        // For built-in types, use the type name
        TypeType* type_type = (TypeType*)item.type;
        if (!type_type || !type_type->type) return nullptr;
        const char* name = get_type_name(type_type->type->type_id);
        if (name) {
            return heap_create_symbol(name, strlen(name));
        }
        return nullptr;
    }
    case LMD_TYPE_SYMBOL: {
        // for symbols, just return the symbol itself (it's already a name)
        return item.get_symbol();
    }
    default:
        return nullptr;
    }
}

// returns the TypeId of an item for use in MIR-compiled code
// declared extern "C" to allow calling from C code (path.c)
extern "C" TypeId item_type_id(Item item) {
    return item.type_id();
}

extern "C" Input* input_from_url(String* url, String* type, String* flavor, Url* cwd);
extern "C" Input* input_from_target(Target* target, String* type, String* flavor);

Input* input_data(Context* ctx, String* url, String* type, String* flavor) {
    log_debug("input_data at: %s, type: %s, flavor: %s",
        url ? url->chars : "null", type ? type->chars : "null",
        flavor ? flavor->chars : "null");
    // Pass NULL for cwd if ctx is NULL to avoid crash
    return input_from_url(url, type, flavor, ctx ? (Url*)ctx->cwd : NULL);
}

Item fn_input2(Item target_item, Item type) {
    String *type_str = NULL, *flavor_str = NULL;

    // Validate target type: must be string, symbol, or path
    TypeId target_type_id = get_type_id(target_item);
    if (target_type_id != LMD_TYPE_STRING && target_type_id != LMD_TYPE_SYMBOL && target_type_id != LMD_TYPE_PATH) {
        log_debug("input target must be a string, symbol, or path, got type: %s", get_type_name(target_type_id));
        return ItemNull;  // todo: push error
    }

    // Convert target Item to Target struct
    Url* cwd = context ? (Url*)context->cwd : NULL;
    Target* target = item_to_target(target_item.item, cwd);
    if (!target) {
        log_error("fn_input2: failed to convert item to target");
        return ItemNull;
    }

    log_debug("fn_input2: target scheme=%d, type=%d", target->scheme, target->type);

    TypeId type_id = get_type_id(type);
    if (type_id == LMD_TYPE_NULL) {
        // No type specified
        type_str = NULL;
    }
    else if (type_id == LMD_TYPE_STRING || type_id == LMD_TYPE_SYMBOL) {
        // Legacy behavior: type is a simple string/symbol
        type_str = fn_string(type);
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
                type_str = fn_string(input_type);
            }
            else {
        log_debug("input type must be a string or symbol, got type: %s", get_type_name(type_value_type));
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
                flavor_str = fn_string(input_flavor);
            }
            else {
                log_debug("input flavor must be a string or symbol, got type: %s", get_type_name(flavor_value_type));
                // todo: push error
                flavor_str = NULL;  // input flavor ignored
            }
        }
    }
    else {
        log_debug("input type must be a string, symbol, or map, got type: %s", get_type_name(type_id));
        target_free(target);
        return ItemNull;  // todo: push error
    }

    // Check if context is properly initialized
    if (!context) {
        log_debug("Error: context is NULL in fn_input");
        target_free(target);
        return ItemNull;
    }

    log_debug("input type: %s, flavor: %s", type_str ? type_str->chars : "null", flavor_str ? flavor_str->chars : "null");

    // Use the new target-based input function
    Input *input = input_from_target(target, type_str, flavor_str);
    target_free(target);

    // todo: input should be cached in context
    return (input && input->root.item) ? input->root : ItemNull;
}

// declared extern "C" to allow calling from C code (path.c)
extern "C" Item fn_input1(Item url) {
    return fn_input2(url, ItemNull);
}

extern "C" String* format_data(Item item, String* type, String* flavor, Pool *pool);

String* fn_format2(Item item, Item type) {
    if (get_type_id(item) == LMD_TYPE_ERROR || get_type_id(type) == LMD_TYPE_ERROR) {
        log_debug("fn_format2: error item received");
        return &STR_ERROR;
    }
    // datetime formatting: format(dt) or format(dt, pattern)
    TypeId item_type_id = get_type_id(item);
    if (item_type_id == LMD_TYPE_DTIME) {
        DateTime dt = item.get_datetime();
        StrBuf* buf = strbuf_new();
        TypeId type_id = get_type_id(type);

        if (type_id == LMD_TYPE_NULL) {
            // default: ISO 8601 format
            datetime_format_lambda(buf, &dt);
        }
        else if (type_id == LMD_TYPE_STRING || type_id == LMD_TYPE_SYMBOL) {
            const char* pattern_chars = type.get_chars();
            // check for named format presets
            if (strcmp(pattern_chars, "iso") == 0 || strcmp(pattern_chars, "iso8601") == 0) {
                datetime_format_iso8601(buf, &dt);
            }
            else if (strcmp(pattern_chars, "date") == 0) {
                // format date portion only: YYYY-MM-DD
                datetime_format_pattern(buf, &dt, "YYYY-MM-DD");
            }
            else if (strcmp(pattern_chars, "time") == 0) {
                // format time portion only: HH:mm:ss
                datetime_format_pattern(buf, &dt, "HH:mm:ss");
            }
            else {
                // custom pattern format
                datetime_format_pattern(buf, &dt, pattern_chars);
            }
        }
        else {
            log_error("format: datetime format must be a string pattern, got type: %s", get_type_name(type_id));
            datetime_format_lambda(buf, &dt);
        }

        size_t len = buf->length;
        String* result = (String*)heap_alloc(sizeof(String) + len + 1, LMD_TYPE_STRING);
        result->len = len;
        memcpy(result->chars, buf->str, len);
        result->chars[len] = '\0';
        strbuf_free(buf);
        return result;
    }

    TypeId type_id = get_type_id(type);
    String* type_str = NULL;
    String* flavor_str = NULL;

    if (type_id == LMD_TYPE_NULL) {
        type_str = NULL;  // use default
    }
    else if (type_id == LMD_TYPE_STRING || type_id == LMD_TYPE_SYMBOL) {
        // Legacy behavior: type is a simple string or symbol
        type_str = fn_string(type);
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
                type_str = fn_string(format_type);
            }
            else {
                log_debug("format type must be a string or symbol, got type: %s", get_type_name(type_value_type));
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
                flavor_str = fn_string(format_flavor);
            }
            else {
                log_debug("format flavor must be a string or symbol, got type: %s", get_type_name(flavor_value_type));
                // todo: push error
                flavor_str = NULL;  // format flavor ignored
            }
        }
    }
    else {
        log_debug("format type must be a string, symbol, or map, got type: %s", get_type_name(type_id));
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

    // For paths (like sys.os), resolve the path content first
    if (type_id == LMD_TYPE_PATH) {
        Path* path = item.path;
        if (path) {
            // Check if it's a sys path - these should be resolved to their content
            if (path_get_scheme(path) == PATH_SCHEME_SYS) {
                if (path->result == 0) {
                    Item resolved = path_resolve_for_iteration(path);
                    if (resolved.item == ItemError.item) return ItemNull;
                }
                if (path->result != 0) {
                    // Recurse with resolved content
                    return fn_member({.item = path->result}, key);
                }
            }

            // path metadata and structural property access
            if (key._type_id == LMD_TYPE_STRING || key._type_id == LMD_TYPE_SYMBOL) {
                String* key_str = key.get_string();
                if (key_str) {
                    const char* k = key_str->chars;

                    // structural properties (always available)
                    if (strcmp(k, "name") == 0) {
                        // return the leaf segment name (e.g. "file.txt")
                        if (path->name) return {.item = s2it(heap_create_name(path->name))};
                        return ItemNull;
                    }
                    if (strcmp(k, "path") == 0) {
                        // return the full OS path string (e.g. "./lib/file.txt")
                        StrBuf* sb = strbuf_new();
                        path_to_os_path(path, sb);
                        String* result = heap_strcpy(sb->str, sb->length);
                        strbuf_free(sb);
                        return {.item = s2it(result)};
                    }
                    if (strcmp(k, "extension") == 0) {
                        // return file extension (e.g. "txt" from "file.txt")
                        if (path->name) {
                            const char* dot = strrchr(path->name, '.');
                            if (dot && dot != path->name) {
                                return {.item = s2it(heap_create_name(dot + 1))};
                            }
                        }
                        return ItemNull;
                    }
                    if (strcmp(k, "scheme") == 0) {
                        const char* scheme_name = path_get_scheme_name(path);
                        if (scheme_name) return {.item = s2it(heap_create_name(scheme_name))};
                        return ItemNull;
                    }
                    if (strcmp(k, "depth") == 0) {
                        return {.item = i2it(path_depth(path))};
                    }

                    // metadata properties (require stat'd metadata)
                    if (path->meta && (path->flags & PATH_FLAG_META_LOADED)) {
                        if (strcmp(k, "size") == 0) return push_l(path->meta->size);
                        if (strcmp(k, "modified") == 0) return push_k(path->meta->modified);
                        if (strcmp(k, "is_dir") == 0) return {.item = b2it((path->meta->flags & PATH_META_IS_DIR) != 0)};
                        if (strcmp(k, "is_link") == 0) return {.item = b2it((path->meta->flags & PATH_META_IS_LINK) != 0)};
                        if (strcmp(k, "mode") == 0) return {.item = i2it(path->meta->mode)};
                    }
                }
            }
        }
        return ItemNull;
    }

    switch (type_id) {
    case LMD_TYPE_NULL:
        return ItemNull;  // null-safe: null.field returns null
    case LMD_TYPE_ERROR: {
        // error member properties: .code and .message
        const char* k = key.get_chars();
        if (!k) return item;  // error propagation

        if (strcmp(k, "code") == 0) {
            // return error code from context->last_error
            if (context && context->last_error) {
                return {.item = i2it(context->last_error->code)};
            }
            return {.item = i2it(ERR_USER_ERROR)};  // default error code
        }
        if (strcmp(k, "message") == 0) {
            // return error message from context->last_error
            if (context && context->last_error && context->last_error->message) {
                String* msg = heap_strcpy(context->last_error->message, strlen(context->last_error->message));
                return {.item = s2it(msg)};
            }
            String* msg = heap_create_name("Error");
            return {.item = s2it(msg)};  // default message
        }

        return item;  // error propagation for other properties
    }
    case LMD_TYPE_DTIME: {
        // datetime member properties
        DateTime dt = item.get_datetime();
        const char* k = key.get_chars();
        if (!k) return ItemNull;

        // date properties
        if (strcmp(k, "year") == 0)         return {.item = i2it(DATETIME_GET_YEAR(&dt))};
        if (strcmp(k, "month") == 0)        return {.item = i2it(DATETIME_GET_MONTH(&dt))};
        if (strcmp(k, "day") == 0)          return {.item = i2it(dt.day)};
        if (strcmp(k, "weekday") == 0)      return {.item = i2it(datetime_weekday(&dt))};
        if (strcmp(k, "yearday") == 0)      return {.item = i2it(datetime_yearday(&dt))};
        if (strcmp(k, "week") == 0)         return {.item = i2it(datetime_week_number(&dt))};
        if (strcmp(k, "quarter") == 0)      return {.item = i2it(datetime_quarter(&dt))};

        // time properties
        if (strcmp(k, "hour") == 0)         return {.item = i2it(dt.hour)};
        if (strcmp(k, "minute") == 0)       return {.item = i2it(dt.minute)};
        if (strcmp(k, "second") == 0)       return {.item = i2it(dt.second)};
        if (strcmp(k, "millisecond") == 0)  return {.item = i2it(dt.millisecond)};

        // timezone properties
        if (strcmp(k, "timezone") == 0) {
            if (!DATETIME_HAS_TIMEZONE(&dt)) return ItemNull;
            return {.item = i2it(DATETIME_GET_TZ_OFFSET(&dt))};
        }
        if (strcmp(k, "utc_offset") == 0) {
            if (!DATETIME_HAS_TIMEZONE(&dt)) return ItemNull;
            int tz = DATETIME_GET_TZ_OFFSET(&dt);
            char buf[8];
            int h = abs(tz) / 60, m = abs(tz) % 60;
            int len = snprintf(buf, sizeof(buf), "%c%02d:%02d", tz >= 0 ? '+' : '-', h, m);
            Symbol* sym = heap_create_symbol(buf, len);
            return {.item = y2it(sym)};
        }
        if (strcmp(k, "is_utc") == 0) {
            if (!DATETIME_HAS_TIMEZONE(&dt)) return {.item = b2it(false)};
            return {.item = b2it(DATETIME_GET_TZ_OFFSET(&dt) == 0)};
        }

        // meta properties
        if (strcmp(k, "unix") == 0)           return push_l(datetime_to_unix_ms(&dt));
        if (strcmp(k, "is_date") == 0)        return {.item = b2it(dt.precision == DATETIME_PRECISION_DATE_ONLY || dt.precision == DATETIME_PRECISION_YEAR_ONLY)};
        if (strcmp(k, "is_time") == 0)        return {.item = b2it(dt.precision == DATETIME_PRECISION_TIME_ONLY)};
        if (strcmp(k, "is_leap_year") == 0)   return {.item = b2it(datetime_is_leap_year_dt(&dt))};
        if (strcmp(k, "days_in_month") == 0)  return {.item = i2it(datetime_days_in_month_dt(&dt))};

        // extraction properties (return transformed datetime)
        if (strcmp(k, "date") == 0) {
            DateTime result = dt;
            result.hour = 0;
            result.minute = 0;
            result.second = 0;
            result.millisecond = 0;
            result.precision = DATETIME_PRECISION_DATE_ONLY;
            return push_k(result);
        }
        if (strcmp(k, "time") == 0) {
            DateTime result;
            memset(&result, 0, sizeof(DateTime));
            DATETIME_SET_YEAR_MONTH(&result, 1970, 1);
            result.day = 1;
            result.hour = dt.hour;
            result.minute = dt.minute;
            result.second = dt.second;
            result.millisecond = dt.millisecond;
            result.tz_offset_biased = dt.tz_offset_biased;
            result.precision = DATETIME_PRECISION_TIME_ONLY;
            result.format_hint = dt.format_hint;
            return push_k(result);
        }
        if (strcmp(k, "utc") == 0) {
            DateTime* result = datetime_to_utc(context->pool, &dt);
            if (result) return push_k(*result);
            return ItemNull;
        }
        if (strcmp(k, "local") == 0) {
            DateTime* result = datetime_to_local(context->pool, &dt);
            if (result) return push_k(*result);
            return ItemNull;
        }

        log_debug("fn_member: unknown datetime property '%s'", k);
        return ItemNull;
    }
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
            const char* k = key.get_chars();
            if (k && strcmp(k, "length") == 0) {
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
        const char* chars = item.get_chars();
        size = chars ? utf8_char_count(chars) : 0;
        break;
    }
    case LMD_TYPE_PATH: {
        // Lazy evaluation: resolve path content if not cached, then get length
        Path* path_val = item.path;
        if (!path_val) { size = 0; break; }
        // Check if content is already resolved (cached in path->result)
        if (path_val->result == 0) {
            // Use path_resolve_for_iteration which handles:
            // - Directories: returns list of child paths
            // - Files: returns parsed content
            // - Wildcards: expands to list of matching paths
            // - Non-existent: returns null (len = 0)
            // - Access errors: returns error
            Item resolved = path_resolve_for_iteration(path_val);
            // result is now cached in path_val->result
            if (resolved.item == ItemError.item) {
                return INT64_ERROR;
            }
        }
        // Get length from cached result
        Item cached = {.item = path_val->result};
        if (cached.item == 0) {
            size = 0;  // null path -> length 0
        } else {
            size = fn_len(cached);
        }
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
    GUARD_ERROR3(str_item, start_item, end_item);
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
Bool fn_contains(Item str_item, Item substr_item) {
    GUARD_BOOL_ERROR2(str_item, substr_item);
    if (get_type_id(str_item) != LMD_TYPE_STRING) {
        log_debug("fn_contains: first argument must be a string");
        return BOOL_ERROR;
    }

    if (get_type_id(substr_item) != LMD_TYPE_STRING) {
        log_debug("fn_contains: second argument must be a string");
        return BOOL_ERROR;
    }

    String* str = str_item.get_string();
    String* substr = substr_item.get_string();

    if (!str || !substr) {
        return BOOL_FALSE;
    }

    if (substr->len == 0) {
        return BOOL_TRUE; // empty string is contained in any string
    }

    if (str->len == 0 || substr->len > str->len) {
        return BOOL_FALSE;
    }

    // simple byte-based search for now - could be optimized with KMP or Boyer-Moore
    for (int i = 0; i <= str->len - substr->len; i++) {
        if (memcmp(str->chars + i, substr->chars, substr->len) == 0) {
            return BOOL_TRUE;
        }
    }

    return BOOL_FALSE;
}

// starts_with(str, prefix) - check if string starts with prefix
Bool fn_starts_with(Item str_item, Item prefix_item) {
    GUARD_BOOL_ERROR2(str_item, prefix_item);
    TypeId str_type = get_type_id(str_item);
    TypeId prefix_type = get_type_id(prefix_item);

    // null prefix (empty string "") matches any string
    if (prefix_type == LMD_TYPE_NULL) {
        return BOOL_TRUE;
    }

    if ((str_type != LMD_TYPE_STRING && str_type != LMD_TYPE_SYMBOL) ||
        (prefix_type != LMD_TYPE_STRING && prefix_type != LMD_TYPE_SYMBOL)) {
        log_debug("fn_starts_with: arguments must be strings or symbols");
        return BOOL_ERROR;
    }

    const char* str_chars = str_item.get_chars();
    uint32_t str_len = str_item.get_len();
    const char* prefix_chars = prefix_item.get_chars();
    uint32_t prefix_len = prefix_item.get_len();

    if (!str_chars || !prefix_chars) {
        return BOOL_FALSE;
    }

    if (prefix_len == 0) {
        return BOOL_TRUE;  // empty prefix matches any string
    }

    if (str_len < prefix_len) {
        return BOOL_FALSE;
    }

    return (memcmp(str_chars, prefix_chars, prefix_len) == 0) ? BOOL_TRUE : BOOL_FALSE;
}

// ends_with(str, suffix) - check if string ends with suffix
Bool fn_ends_with(Item str_item, Item suffix_item) {
    GUARD_BOOL_ERROR2(str_item, suffix_item);
    TypeId str_type = get_type_id(str_item);
    TypeId suffix_type = get_type_id(suffix_item);

    // null suffix (empty string "") matches any string
    if (suffix_type == LMD_TYPE_NULL) {
        return BOOL_TRUE;
    }

    if ((str_type != LMD_TYPE_STRING && str_type != LMD_TYPE_SYMBOL) ||
        (suffix_type != LMD_TYPE_STRING && suffix_type != LMD_TYPE_SYMBOL)) {
        log_debug("fn_ends_with: arguments must be strings or symbols");
        return BOOL_ERROR;
    }

    const char* str_chars = str_item.get_chars();
    uint32_t str_len = str_item.get_len();
    const char* suffix_chars = suffix_item.get_chars();
    uint32_t suffix_len = suffix_item.get_len();

    if (!str_chars || !suffix_chars) {
        return BOOL_FALSE;
    }

    if (suffix_len == 0) {
        return BOOL_TRUE;  // empty suffix matches any string
    }

    if (str_len < suffix_len) {
        return BOOL_FALSE;
    }

    size_t offset = str_len - suffix_len;
    return (memcmp(str_chars + offset, suffix_chars, suffix_len) == 0) ? BOOL_TRUE : BOOL_FALSE;
}

// index_of(str, sub) - find first occurrence of substring, returns -1 if not found
int64_t fn_index_of(Item str_item, Item sub_item) {
    // propagate error inputs as INT64_ERROR (distinct from -1 = 'not found')
    if (get_type_id(str_item) == LMD_TYPE_ERROR || get_type_id(sub_item) == LMD_TYPE_ERROR) {
        return INT64_ERROR;
    }
    TypeId str_type = get_type_id(str_item);
    TypeId sub_type = get_type_id(sub_item);

    if ((str_type != LMD_TYPE_STRING && str_type != LMD_TYPE_SYMBOL) ||
        (sub_type != LMD_TYPE_STRING && sub_type != LMD_TYPE_SYMBOL)) {
        log_debug("fn_index_of: arguments must be strings or symbols");
        return -1;
    }

    const char* str_chars = str_item.get_chars();
    uint32_t str_len = str_item.get_len();
    const char* sub_chars = sub_item.get_chars();
    uint32_t sub_len = sub_item.get_len();

    if (!str_chars || !sub_chars) {
        return -1;
    }

    if (sub_len == 0) {
        return 0;  // empty substring is at position 0
    }

    if (str_len < sub_len) {
        return -1;
    }

    // byte-based search, then convert byte offset to char offset
    for (size_t i = 0; i <= str_len - sub_len; i++) {
        if (memcmp(str_chars + i, sub_chars, sub_len) == 0) {
            // convert byte offset to character offset
            int64_t char_index = utf8_char_count_n(str_chars, i);
            return char_index;
        }
    }

    return -1;
}

// last_index_of(str, sub) - find last occurrence of substring, returns -1 if not found
int64_t fn_last_index_of(Item str_item, Item sub_item) {
    // propagate error inputs as INT64_ERROR (distinct from -1 = 'not found')
    if (get_type_id(str_item) == LMD_TYPE_ERROR || get_type_id(sub_item) == LMD_TYPE_ERROR) {
        return INT64_ERROR;
    }
    TypeId str_type = get_type_id(str_item);
    TypeId sub_type = get_type_id(sub_item);

    if ((str_type != LMD_TYPE_STRING && str_type != LMD_TYPE_SYMBOL) ||
        (sub_type != LMD_TYPE_STRING && sub_type != LMD_TYPE_SYMBOL)) {
        log_debug("fn_last_index_of: arguments must be strings or symbols");
        return -1;
    }

    const char* str_chars = str_item.get_chars();
    uint32_t str_len = str_item.get_len();
    const char* sub_chars = sub_item.get_chars();
    uint32_t sub_len = sub_item.get_len();

    if (!str_chars || !sub_chars) {
        return -1;
    }

    if (sub_len == 0) {
        // empty substring is at the end
        int64_t char_len = utf8_char_count(str_chars);
        return char_len;
    }

    if (str_len < sub_len) {
        return -1;
    }

    // search from end to beginning
    for (size_t i = str_len - sub_len + 1; i > 0; i--) {
        size_t pos = i - 1;
        if (memcmp(str_chars + pos, sub_chars, sub_len) == 0) {
            // convert byte offset to character offset
            int64_t char_index = utf8_char_count_n(str_chars, pos);
            return char_index;
        }
    }

    return -1;
}

// Helper: check if character is ASCII whitespace
static inline bool is_ascii_whitespace(unsigned char c) {
    return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\v' || c == '\f';
}

// trim(str) - remove leading and trailing whitespace
Item fn_trim(Item str_item) {
    GUARD_ERROR1(str_item);
    TypeId str_type = get_type_id(str_item);

    if (str_type != LMD_TYPE_STRING && str_type != LMD_TYPE_SYMBOL) {
        log_debug("fn_trim: argument must be a string or symbol");
        return ItemError;
    }

    const char* chars = str_item.get_chars();
    uint32_t len = str_item.get_len();
    if (!chars || len == 0) {
        return str_item;
    }

    // find start (skip leading whitespace)
    size_t start = 0;
    while (start < len && is_ascii_whitespace((unsigned char)chars[start])) {
        start++;
    }

    // find end (skip trailing whitespace)
    size_t end = len;
    while (end > start && is_ascii_whitespace((unsigned char)chars[end - 1])) {
        end--;
    }

    if (start >= end) {
        // return empty string/symbol
        if (str_type == LMD_TYPE_SYMBOL) {
            return {.item = y2it(heap_create_symbol("", 0))};
        }
        String* empty = (String *)heap_alloc(sizeof(String) + 1, LMD_TYPE_STRING);
        empty->len = 0;
        empty->chars[0] = '\0';
        return {.item = s2it(empty)};
    }

    size_t result_len = end - start;
    if (str_type == LMD_TYPE_SYMBOL) {
        return {.item = y2it(heap_create_symbol(chars + start, result_len))};
    }

    String* result = (String *)heap_alloc(sizeof(String) + result_len + 1, LMD_TYPE_STRING);
    result->len = result_len;
    memcpy(result->chars, chars + start, result_len);
    result->chars[result_len] = '\0';
    return {.item = s2it(result)};
}

// trim_start(str) - remove leading whitespace
Item fn_trim_start(Item str_item) {
    GUARD_ERROR1(str_item);
    TypeId str_type = get_type_id(str_item);

    if (str_type != LMD_TYPE_STRING && str_type != LMD_TYPE_SYMBOL) {
        log_debug("fn_trim_start: argument must be a string or symbol");
        return ItemError;
    }

    const char* chars = str_item.get_chars();
    uint32_t len = str_item.get_len();
    if (!chars || len == 0) {
        return str_item;
    }

    // find start (skip leading whitespace)
    size_t start = 0;
    while (start < len && is_ascii_whitespace((unsigned char)chars[start])) {
        start++;
    }

    if (start == 0) {
        return str_item;  // no leading whitespace
    }

    size_t result_len = len - start;
    if (str_type == LMD_TYPE_SYMBOL) {
        return {.item = y2it(heap_create_symbol(chars + start, result_len))};
    }

    String* result = (String *)heap_alloc(sizeof(String) + result_len + 1, LMD_TYPE_STRING);
    result->len = result_len;
    memcpy(result->chars, chars + start, result_len);
    result->chars[result_len] = '\0';
    return {.item = s2it(result)};
}

// trim_end(str) - remove trailing whitespace
Item fn_trim_end(Item str_item) {
    GUARD_ERROR1(str_item);
    TypeId str_type = get_type_id(str_item);

    if (str_type != LMD_TYPE_STRING && str_type != LMD_TYPE_SYMBOL) {
        log_debug("fn_trim_end: argument must be a string or symbol");
        return ItemError;
    }

    const char* chars = str_item.get_chars();
    uint32_t slen = str_item.get_len();
    if (!chars || slen == 0) {
        return str_item;
    }

    // find end (skip trailing whitespace)
    size_t end = slen;
    while (end > 0 && is_ascii_whitespace((unsigned char)chars[end - 1])) {
        end--;
    }

    if (end == slen) {
        return str_item;  // no trailing whitespace
    }

    if (end == 0) {
        // return empty string/symbol
        if (str_type == LMD_TYPE_SYMBOL) {
            return {.item = y2it(heap_create_symbol("", 0))};
        }
        String* empty = (String *)heap_alloc(sizeof(String) + 1, LMD_TYPE_STRING);
        empty->len = 0;
        empty->chars[0] = '\0';
        return {.item = s2it(empty)};
    }

    if (str_type == LMD_TYPE_SYMBOL) {
        return {.item = y2it(heap_create_symbol(chars, end))};
    }

    String* result = (String *)heap_alloc(sizeof(String) + end + 1, LMD_TYPE_STRING);
    result->len = end;
    memcpy(result->chars, chars, end);
    result->chars[end] = '\0';
    return {.item = s2it(result)};
}

// lower(str) - convert string to lowercase (ASCII only for now)
Item fn_lower(Item str_item) {
    GUARD_ERROR1(str_item);
    TypeId str_type = get_type_id(str_item);

    if (str_type != LMD_TYPE_STRING && str_type != LMD_TYPE_SYMBOL) {
        log_debug("fn_lower: argument must be a string or symbol");
        return ItemError;
    }

    const char* chars = str_item.get_chars();
    uint32_t len = str_item.get_len();
    if (!chars || len == 0) {
        return str_item;
    }

    // Check if any uppercase characters exist (optimization)
    bool has_upper = false;
    for (uint32_t i = 0; i < len; i++) {
        if (chars[i] >= 'A' && chars[i] <= 'Z') {
            has_upper = true;
            break;
        }
    }
    if (!has_upper) {
        return str_item;  // already lowercase
    }

    if (str_type == LMD_TYPE_SYMBOL) {
        // create new lowercase symbol - use stack buffer for small strings, malloc for large
        char stack_buf[256];
        char* lower_chars = (len < sizeof(stack_buf)) ? stack_buf : (char*)malloc(len + 1);
        for (uint32_t i = 0; i < len; i++) {
            char c = chars[i];
            lower_chars[i] = (c >= 'A' && c <= 'Z') ? (c + 32) : c;
        }
        lower_chars[len] = '\0';
        Symbol* sym = heap_create_symbol(lower_chars, len);
        if (lower_chars != stack_buf) free(lower_chars);
        return {.item = y2it(sym)};
    }

    String* result = (String *)heap_alloc(sizeof(String) + len + 1, LMD_TYPE_STRING);
    result->len = len;
    for (uint32_t i = 0; i < len; i++) {
        char c = chars[i];
        result->chars[i] = (c >= 'A' && c <= 'Z') ? (c + 32) : c;
    }
    result->chars[len] = '\0';
    return {.item = s2it(result)};
}

// upper(str) - convert string to uppercase (ASCII only for now)
Item fn_upper(Item str_item) {
    GUARD_ERROR1(str_item);
    TypeId str_type = get_type_id(str_item);

    if (str_type != LMD_TYPE_STRING && str_type != LMD_TYPE_SYMBOL) {
        log_debug("fn_upper: argument must be a string or symbol");
        return ItemError;
    }

    const char* chars = str_item.get_chars();
    uint32_t len = str_item.get_len();
    if (!chars || len == 0) {
        return str_item;
    }

    // Check if any lowercase characters exist (optimization)
    bool has_lower = false;
    for (uint32_t i = 0; i < len; i++) {
        if (chars[i] >= 'a' && chars[i] <= 'z') {
            has_lower = true;
            break;
        }
    }
    if (!has_lower) {
        return str_item;  // already uppercase
    }

    if (str_type == LMD_TYPE_SYMBOL) {
        // create new uppercase symbol - use stack buffer for small strings, malloc for large
        char stack_buf[256];
        char* upper_chars = (len < sizeof(stack_buf)) ? stack_buf : (char*)malloc(len + 1);
        for (uint32_t i = 0; i < len; i++) {
            char c = chars[i];
            upper_chars[i] = (c >= 'a' && c <= 'z') ? (c - 32) : c;
        }
        upper_chars[len] = '\0';
        Symbol* sym = heap_create_symbol(upper_chars, len);
        if (upper_chars != stack_buf) free(upper_chars);
        return {.item = y2it(sym)};
    }

    String* result = (String *)heap_alloc(sizeof(String) + len + 1, LMD_TYPE_STRING);
    result->len = len;
    for (uint32_t i = 0; i < len; i++) {
        char c = chars[i];
        result->chars[i] = (c >= 'a' && c <= 'z') ? (c - 32) : c;
    }
    result->chars[len] = '\0';
    return {.item = s2it(result)};
}

// split(str, sep) - split string by separator, returns list of strings
Item fn_split(Item str_item, Item sep_item) {
    GUARD_ERROR1(str_item);
    TypeId str_type = get_type_id(str_item);
    TypeId sep_type = get_type_id(sep_item);

    // null separator (empty string "") means split into individual characters
    bool null_sep = (sep_type == LMD_TYPE_NULL);

    if ((str_type != LMD_TYPE_STRING && str_type != LMD_TYPE_SYMBOL) ||
        (!null_sep && sep_type != LMD_TYPE_STRING && sep_type != LMD_TYPE_SYMBOL)) {
        log_debug("fn_split: arguments must be strings or symbols");
        return ItemError;
    }

    const char* str_chars = str_item.get_chars();
    uint32_t str_len = str_item.get_len();
    const char* sep_chars = null_sep ? nullptr : sep_item.get_chars();
    uint32_t sep_len = null_sep ? 0 : sep_item.get_len();

    // disable string merging in list_push so split results stay separate
    bool saved_merging = false;
    if (context) {
        saved_merging = context->disable_string_merging;
        context->disable_string_merging = true;
    }

    List* result = list();

    if (!str_chars || str_len == 0) {
        if (context) { context->disable_string_merging = saved_merging; }
        return {.list = result};  // empty list for empty string
    }

    if (!sep_chars || sep_len == 0) {
        // split into individual characters
        const char* p = str_chars;
        const char* end = str_chars + str_len;
        while (p < end) {
            int char_len = utf8_char_len(*p);
            if (char_len <= 0) char_len = 1;  // fallback for invalid UTF-8

            String* part = (String *)heap_alloc(sizeof(String) + char_len + 1, LMD_TYPE_STRING);
            part->len = char_len;
            memcpy(part->chars, p, char_len);
            part->chars[char_len] = '\0';

            list_push(result, {.item = s2it(part)});
            p += char_len;
        }
        if (context) { context->disable_string_merging = saved_merging; }
        return {.list = result};
    }

    // split by separator
    const char* start = str_chars;
    const char* end = str_chars + str_len;
    const char* p = start;

    while (p <= end - sep_len) {
        if (memcmp(p, sep_chars, sep_len) == 0) {
            // found separator
            size_t part_len = p - start;
            String* part = (String *)heap_alloc(sizeof(String) + part_len + 1, LMD_TYPE_STRING);
            part->len = part_len;
            memcpy(part->chars, start, part_len);
            part->chars[part_len] = '\0';

            list_push(result, {.item = s2it(part)});

            p += sep_len;
            start = p;
        } else {
            p++;
        }
    }

    // add the last part
    size_t part_len = end - start;
    String* part = (String *)heap_alloc(sizeof(String) + part_len + 1, LMD_TYPE_STRING);
    part->len = part_len;
    memcpy(part->chars, start, part_len);
    part->chars[part_len] = '\0';

    list_push(result, {.item = s2it(part)});

    if (context) { context->disable_string_merging = saved_merging; }
    return {.list = result};
}

// str_join(strs, sep) - join list of strings with separator
Item fn_str_join(Item list_item, Item sep_item) {
    GUARD_ERROR2(list_item, sep_item);
    TypeId list_type = get_type_id(list_item);
    TypeId sep_type = get_type_id(sep_item);

    if (list_type != LMD_TYPE_LIST && list_type != LMD_TYPE_ARRAY) {
        log_debug("fn_str_join: first argument must be a list or array");
        return ItemError;
    }

    // allow null/empty separator - treat as empty string
    const char* sep_chars = nullptr;
    size_t sep_len = 0;

    if (sep_type == LMD_TYPE_STRING || sep_type == LMD_TYPE_SYMBOL) {
        sep_chars = sep_item.get_chars();
        sep_len = sep_chars ? sep_item.get_len() : 0;
    } else if (sep_type != LMD_TYPE_NULL) {
        log_debug("fn_str_join: separator must be a string, symbol, or null");
        return ItemError;
    }

    // calculate total length
    size_t total_len = 0;
    int64_t count = 0;

    if (list_type == LMD_TYPE_LIST) {
        List* lst = list_item.list;
        count = lst->length;
        for (int64_t i = 0; i < count; i++) {
            Item item = lst->items[i];
            TypeId item_type = get_type_id(item);
            if (item_type == LMD_TYPE_STRING || item_type == LMD_TYPE_SYMBOL) {
                total_len += item.get_len();
            }
        }
    } else {
        Array* arr = list_item.array;
        count = arr->length;
        for (int64_t i = 0; i < count; i++) {
            Item item = arr->items[i];
            TypeId item_type = get_type_id(item);
            if (item_type == LMD_TYPE_STRING || item_type == LMD_TYPE_SYMBOL) {
                total_len += item.get_len();
            }
        }
    }

    if (count > 1 && sep_len > 0) {
        total_len += (count - 1) * sep_len;
    }

    // allocate result
    String* result = (String *)heap_alloc(sizeof(String) + total_len + 1, LMD_TYPE_STRING);
    result->len = total_len;

    // build result
    char* p = result->chars;

    if (list_type == LMD_TYPE_LIST) {
        List* lst = list_item.list;
        for (int64_t i = 0; i < count; i++) {
            if (i > 0 && sep_len > 0) {
                memcpy(p, sep_chars, sep_len);
                p += sep_len;
            }
            Item item = lst->items[i];
            TypeId item_type = get_type_id(item);
            if (item_type == LMD_TYPE_STRING || item_type == LMD_TYPE_SYMBOL) {
                const char* s_chars = item.get_chars();
                uint32_t s_len = item.get_len();
                if (s_chars && s_len > 0) {
                    memcpy(p, s_chars, s_len);
                    p += s_len;
                }
            }
        }
    } else {
        Array* arr = list_item.array;
        for (int64_t i = 0; i < count; i++) {
            if (i > 0 && sep_len > 0) {
                memcpy(p, sep_chars, sep_len);
                p += sep_len;
            }
            Item item = arr->items[i];
            TypeId item_type = get_type_id(item);
            if (item_type == LMD_TYPE_STRING || item_type == LMD_TYPE_SYMBOL) {
                const char* s_chars = item.get_chars();
                uint32_t s_len = item.get_len();
                if (s_chars && s_len > 0) {
                    memcpy(p, s_chars, s_len);
                    p += s_len;
                }
            }
        }
    }

    result->chars[total_len] = '\0';
    return {.item = s2it(result)};
}

// replace(str, old, new) - replace all occurrences of old with new
Item fn_replace(Item str_item, Item old_item, Item new_item) {
    GUARD_ERROR3(str_item, old_item, new_item);
    TypeId str_type = get_type_id(str_item);
    TypeId old_type = get_type_id(old_item);
    TypeId new_type = get_type_id(new_item);

    if ((str_type != LMD_TYPE_STRING && str_type != LMD_TYPE_SYMBOL) ||
        (old_type != LMD_TYPE_STRING && old_type != LMD_TYPE_SYMBOL) ||
        (new_type != LMD_TYPE_STRING && new_type != LMD_TYPE_SYMBOL)) {
        log_debug("fn_replace: all arguments must be strings or symbols");
        return ItemError;
    }

    const char* str_chars = str_item.get_chars();
    uint32_t str_len = str_item.get_len();
    const char* old_chars = old_item.get_chars();
    uint32_t old_len = old_item.get_len();
    const char* new_chars = new_item.get_chars();
    uint32_t new_len_val = new_item.get_len();

    if (!str_chars || str_len == 0) {
        return str_item;
    }

    if (!old_chars || old_len == 0) {
        return str_item;  // nothing to replace
    }

    // count occurrences
    int count = 0;
    const char* p = str_chars;
    const char* end = str_chars + str_len;
    while (p <= end - old_len) {
        if (memcmp(p, old_chars, old_len) == 0) {
            count++;
            p += old_len;
        } else {
            p++;
        }
    }

    if (count == 0) {
        return str_item;  // no occurrences found
    }

    // calculate new length
    size_t new_str_len = new_chars ? new_len_val : 0;
    size_t new_len = str_len + count * (new_str_len - old_len);

    // allocate result - preserve type
    if (str_type == LMD_TYPE_SYMBOL) {
        char* buf = (char*)pool_alloc(context->pool, new_len + 1);
        char* dest = buf;
        p = str_chars;
        while (p <= end - old_len) {
            if (memcmp(p, old_chars, old_len) == 0) {
                if (new_str_len > 0) {
                    memcpy(dest, new_chars, new_str_len);
                    dest += new_str_len;
                }
                p += old_len;
            } else {
                *dest++ = *p++;
            }
        }
        // copy remaining
        while (p < end) {
            *dest++ = *p++;
        }
        *dest = '\0';
        return {.item = y2it(heap_create_symbol(buf, new_len))};
    }

    String* result = (String *)heap_alloc(sizeof(String) + new_len + 1, LMD_TYPE_STRING);
    result->len = new_len;

    // build result
    char* dest = result->chars;
    p = str_chars;
    while (p <= end - old_len) {
        if (memcmp(p, old_chars, old_len) == 0) {
            if (new_str_len > 0) {
                memcpy(dest, new_chars, new_str_len);
                dest += new_str_len;
            }
            p += old_len;
        } else {
            *dest++ = *p++;
        }
    }
    // copy remaining
    while (p < end) {
        *dest++ = *p++;
    }
    result->chars[new_len] = '\0';

    return {.item = s2it(result)};
}

// normalize with 1 arg (defaults to NFC)
Item fn_normalize1(Item str_item) {
    Item nfc_type = {.item = s2it(heap_strcpy((char*)"nfc", 3))};
    return fn_normalize(str_item, nfc_type);
}

// datetime() - creates current DateTime in UTC
DateTime fn_datetime0() {
    DateTime dt;
    memset(&dt, 0, sizeof(DateTime));

    // get current time
    time_t now = time(NULL);
    struct tm* tm_utc = gmtime(&now);
    if (tm_utc) {
        DATETIME_SET_YEAR_MONTH(&dt, tm_utc->tm_year + 1900, tm_utc->tm_mon + 1);
        dt.day = tm_utc->tm_mday;
        dt.hour = tm_utc->tm_hour;
        dt.minute = tm_utc->tm_min;
        dt.second = tm_utc->tm_sec;
        dt.millisecond = 0;
    }

    // set as UTC timezone
    DATETIME_SET_TZ_OFFSET(&dt, 0);
    dt.precision = DATETIME_PRECISION_DATE_TIME;
    dt.format_hint = DATETIME_FORMAT_ISO8601_UTC;

    return dt;
}

// datetime(str) - parse datetime from string
DateTime fn_datetime1(Item arg) {
    log_debug("fn_datetime1: parse from arg");
    TypeId arg_type = get_type_id(arg);

    // propagate error inputs
    if (arg_type == LMD_TYPE_ERROR) return DATETIME_MAKE_ERROR();

    if (arg_type == LMD_TYPE_STRING || arg_type == LMD_TYPE_SYMBOL) {
        const char* chars = arg.get_chars();
        uint32_t len = arg.get_len();
        DateTime* parsed = datetime_parse_lambda(context->pool, chars);
        if (parsed) {
            DateTime dt = *parsed;
            dt.precision = DATETIME_PRECISION_DATE_TIME;
            return dt;
        }
        log_error("datetime: failed to parse string '%.*s'", (int)len, chars);
    }
    else if (arg_type == LMD_TYPE_INT || arg_type == LMD_TYPE_INT64) {
        // interpret as unix milliseconds
        int64_t ms = arg_type == LMD_TYPE_INT ? (int64_t)arg.get_int56() : arg.get_int64();
        DateTime* parsed = datetime_from_unix_ms(context->pool, ms);
        if (parsed) {
            DateTime dt = *parsed;
            dt.precision = DATETIME_PRECISION_DATE_TIME;
            return dt;
        }
    }
    else if (arg_type == LMD_TYPE_DTIME) {
        return arg.get_datetime();
    }

    return DATETIME_MAKE_ERROR();  // error sentinel instead of all-zeros
}

// date() - current date in UTC (date-only precision)
DateTime fn_date0() {
    DateTime dt = fn_datetime0();
    dt.hour = 0;
    dt.minute = 0;
    dt.second = 0;
    dt.millisecond = 0;
    dt.precision = DATETIME_PRECISION_DATE_ONLY;
    return dt;
}

// date(dt) - extract date portion from datetime
DateTime fn_date1(Item arg) {
    log_debug("fn_date1: extract date from arg");
    TypeId arg_type = get_type_id(arg);

    // propagate error inputs
    if (arg_type == LMD_TYPE_ERROR) return DATETIME_MAKE_ERROR();

    if (arg_type == LMD_TYPE_DTIME) {
        DateTime dt = arg.get_datetime();
        dt.hour = 0;
        dt.minute = 0;
        dt.second = 0;
        dt.millisecond = 0;
        dt.precision = DATETIME_PRECISION_DATE_ONLY;
        return dt;
    }
    else if (arg_type == LMD_TYPE_STRING || arg_type == LMD_TYPE_SYMBOL) {
        const char* chars = arg.get_chars();
        uint32_t len = arg.get_len();
        DateTime* parsed = datetime_parse_lambda(context->pool, chars);
        if (parsed) {
            DateTime dt = *parsed;
            dt.hour = 0;
            dt.minute = 0;
            dt.second = 0;
            dt.millisecond = 0;
            dt.precision = DATETIME_PRECISION_DATE_ONLY;
            return dt;
        }
        log_error("date: failed to parse string '%.*s'", (int)len, chars);
    }

    return DATETIME_MAKE_ERROR();  // error sentinel instead of all-zeros
}

// date(y, m, d) - construct date from year, month, day
DateTime fn_date3(Item y, Item m, Item d) {
    log_debug("fn_date3: construct date from y/m/d");

    // propagate error inputs
    GUARD_DATETIME_ERROR3(y, m, d);

    DateTime dt;
    memset(&dt, 0, sizeof(DateTime));

    int year = (int)(get_type_id(y) == LMD_TYPE_INT ? y.get_int56() : (get_type_id(y) == LMD_TYPE_INT64 ? (int)y.get_int64() : 0));
    int month = (int)(get_type_id(m) == LMD_TYPE_INT ? m.get_int56() : (get_type_id(m) == LMD_TYPE_INT64 ? (int)m.get_int64() : 0));
    int day_val = (int)(get_type_id(d) == LMD_TYPE_INT ? d.get_int56() : (get_type_id(d) == LMD_TYPE_INT64 ? (int)d.get_int64() : 0));

    // validate ranges
    if (month < 1 || month > 12) {
        log_error("date: month must be 1-12, got %d", month);
        return DATETIME_MAKE_ERROR();
    }
    if (day_val < 1 || day_val > 31) {
        log_error("date: day must be 1-31, got %d", day_val);
        return DATETIME_MAKE_ERROR();
    }

    DATETIME_SET_YEAR_MONTH(&dt, year, month);
    dt.day = day_val;
    dt.precision = DATETIME_PRECISION_DATE_ONLY;

    return dt;
}

// time() - current time in UTC (time-only precision)
DateTime fn_time0() {
    DateTime dt = fn_datetime0();
    // keep hour/minute/second/millisecond, set default date to match parsed literals
    DateTime result;
    memset(&result, 0, sizeof(DateTime));
    DATETIME_SET_YEAR_MONTH(&result, 1970, 1);
    result.day = 1;
    result.hour = dt.hour;
    result.minute = dt.minute;
    result.second = dt.second;
    result.millisecond = dt.millisecond;
    result.tz_offset_biased = dt.tz_offset_biased;
    result.precision = DATETIME_PRECISION_TIME_ONLY;
    result.format_hint = dt.format_hint;
    return result;
}

// time(dt) - extract time portion from datetime
DateTime fn_time1(Item arg) {
    log_debug("fn_time1: extract time from arg");
    TypeId arg_type = get_type_id(arg);

    // propagate error inputs
    if (arg_type == LMD_TYPE_ERROR) return DATETIME_MAKE_ERROR();

    if (arg_type == LMD_TYPE_DTIME) {
        DateTime src = arg.get_datetime();
        DateTime dt;
        memset(&dt, 0, sizeof(DateTime));
        DATETIME_SET_YEAR_MONTH(&dt, 1970, 1);
        dt.day = 1;
        dt.hour = src.hour;
        dt.minute = src.minute;
        dt.second = src.second;
        dt.millisecond = src.millisecond;
        dt.tz_offset_biased = src.tz_offset_biased;
        dt.precision = DATETIME_PRECISION_TIME_ONLY;
        dt.format_hint = src.format_hint;
        return dt;
    }
    else if (arg_type == LMD_TYPE_STRING || arg_type == LMD_TYPE_SYMBOL) {
        const char* chars = arg.get_chars();
        DateTime* parsed = datetime_parse_lambda(context->pool, chars);
        if (parsed) {
            DateTime dt;
            memset(&dt, 0, sizeof(DateTime));
            DATETIME_SET_YEAR_MONTH(&dt, 1970, 1);
            dt.day = 1;
            dt.hour = parsed->hour;
            dt.minute = parsed->minute;
            dt.second = parsed->second;
            dt.millisecond = parsed->millisecond;
            dt.tz_offset_biased = parsed->tz_offset_biased;
            dt.precision = DATETIME_PRECISION_TIME_ONLY;
            dt.format_hint = parsed->format_hint;
            return dt;
        }
        log_error("time: failed to parse string '%.*s'", (int)arg.get_len(), arg.get_chars());
    }

    return DATETIME_MAKE_ERROR();  // error sentinel instead of all-zeros
}

// time(h, m, s) - construct time from hour, minute, second
DateTime fn_time3(Item h, Item m, Item s) {
    log_debug("fn_time3: construct time from h/m/s");

    // propagate error inputs
    GUARD_DATETIME_ERROR3(h, m, s);

    DateTime dt;
    memset(&dt, 0, sizeof(DateTime));

    int hour_val = (int)(get_type_id(h) == LMD_TYPE_INT ? h.get_int56() : (get_type_id(h) == LMD_TYPE_INT64 ? (int)h.get_int64() : 0));
    int minute_val = (int)(get_type_id(m) == LMD_TYPE_INT ? m.get_int56() : (get_type_id(m) == LMD_TYPE_INT64 ? (int)m.get_int64() : 0));
    int second_val = (int)(get_type_id(s) == LMD_TYPE_INT ? s.get_int56() : (get_type_id(s) == LMD_TYPE_INT64 ? (int)s.get_int64() : 0));

    // validate ranges
    if (hour_val < 0 || hour_val > 23) {
        log_error("time: hour must be 0-23, got %d", hour_val);
        return DATETIME_MAKE_ERROR();
    }
    if (minute_val < 0 || minute_val > 59) {
        log_error("time: minute must be 0-59, got %d", minute_val);
        return DATETIME_MAKE_ERROR();
    }
    if (second_val < 0 || second_val > 59) {
        log_error("time: second must be 0-59, got %d", second_val);
        return DATETIME_MAKE_ERROR();
    }

    // set default date values to match parsed time-only literals
    DATETIME_SET_YEAR_MONTH(&dt, 1970, 1);
    dt.day = 1;
    dt.hour = hour_val;
    dt.minute = minute_val;
    dt.second = second_val;
    dt.precision = DATETIME_PRECISION_TIME_ONLY;

    return dt;
}

// justnow() - current datetime with millisecond precision
DateTime fn_justnow() {
    DateTime dt;
    memset(&dt, 0, sizeof(DateTime));

    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);

    struct tm* tm_utc = gmtime(&ts.tv_sec);
    if (tm_utc) {
        DATETIME_SET_YEAR_MONTH(&dt, tm_utc->tm_year + 1900, tm_utc->tm_mon + 1);
        dt.day = tm_utc->tm_mday;
        dt.hour = tm_utc->tm_hour;
        dt.minute = tm_utc->tm_min;
        dt.second = tm_utc->tm_sec;
        dt.millisecond = (int)(ts.tv_nsec / 1000000);
    }

    DATETIME_SET_TZ_OFFSET(&dt, 0);
    dt.precision = DATETIME_PRECISION_DATE_TIME;
    dt.format_hint = DATETIME_FORMAT_ISO8601_UTC;

    return dt;
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
