#include "transpiler.hpp"
#include "lambda-decimal.hpp"
#include "lambda-error.h"
#include "../lib/log.h"
#include "../lib/url.h"
#include "../lib/str.h"
#include "utf_string.h"
#include "re2_wrapper.hpp"
#include <mpdecimal.h>  // needed for inline decimal operations

#include <stdarg.h>
#include <time.h>
#include <errno.h>  // for errno checking
#if _WIN32
#include <windows.h>
#include <process.h>
#include <sys/timeb.h>  // for struct timespec
#include <pthread_time.h>  // for clock_gettime
// Windows doesn't have these macros, provide simple equivalents
#define WIFEXITED(status) (1)
#define WEXITSTATUS(status) (status)
#else
#include <sys/wait.h>  // for WIFEXITED, WEXITSTATUS
#endif
#include "validator/validator.hpp"

extern __thread EvalContext* context;

// create_match_map helper for find() (implemented in re2_wrapper.cpp)
extern "C" Map* create_match_map_ext(const char* match_str, size_t match_len, int64_t index);

// forward declaration of static error string (defined later in this file)
extern String STR_ERROR;

// External path resolution function (implemented in path.c)
extern "C" Item path_resolve_for_iteration(Path* path);

// forward declaration from lambda-data.cpp
void array_set(Array* arr, int index, Item itm);

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

extern "C" void* heap_data_alloc(size_t size);
extern "C" void* heap_data_calloc(size_t size);

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
    switch (item._type_id) {
    case LMD_TYPE_NULL:
        return BOOL_FALSE;
    case LMD_TYPE_ERROR:
        return BOOL_FALSE;  // errors are falsy — use `if (^e)` to check for errors
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
    if (list->length == 0) { return ItemNull; }
    if (list->length == 1) { return list->items[0]; }
    return {.list = list};
}

String *fn_strcat(String *left, String *right) {
    if (!left || !right) {
        log_error("null pointer in fn_strcat: left=%p, right=%p", left, right);
        return &STR_ERROR;
    }
    int left_len = left->len, right_len = right->len;
    String *result = (String *)heap_alloc(sizeof(String) + left_len + right_len + 1, LMD_TYPE_STRING);
    if (!result) {
        log_error("failed to allocate memory for fn_strcat result");
        return NULL;
    }
    result->len = left_len + right_len;
    result->is_ascii = left->is_ascii && right->is_ascii;
    memcpy(result->chars, left->chars, left_len);
    // copy the string and '\0'
    memcpy(result->chars + left_len, right->chars, right_len + 1);
    return result;
}

Item fn_join(Item left, Item right) {
    GUARD_ERROR2(left, right);
    // concat two scalars, or join two lists/arrays/maps, or join scalar with list/array, or join two binaries, else error
    TypeId left_type = get_type_id(left), right_type = get_type_id(right);

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
    // merge two array-like types (List, Array, ArrayInt, ArrayInt64, ArrayFloat, Range)
    else if ((left_type >= LMD_TYPE_ARRAY_INT && left_type <= LMD_TYPE_ARRAY) || left_type == LMD_TYPE_LIST || left_type == LMD_TYPE_RANGE) {
        if (!((right_type >= LMD_TYPE_ARRAY_INT && right_type <= LMD_TYPE_ARRAY) || right_type == LMD_TYPE_LIST || right_type == LMD_TYPE_RANGE)) {
            set_runtime_error(ERR_TYPE_MISMATCH, "fn_join: unsupported operand types: %s and %s",
                type_info[left_type].name, type_info[right_type].name);
            return ItemError;
        }
        // same-type optimization: direct memcpy of native items (not for Range)
        if (left_type == right_type && left_type != LMD_TYPE_RANGE) {
            if (left_type == LMD_TYPE_ARRAY_INT) {
                ArrayInt *la = left.array_int, *ra = right.array_int;
                int64_t total = la->length + ra->length;
                ArrayInt *result = (ArrayInt *)heap_calloc(sizeof(ArrayInt), LMD_TYPE_ARRAY_INT);
                result->type_id = LMD_TYPE_ARRAY_INT;
                result->length = total;  result->capacity = total;
                result->items = (int64_t*)heap_data_alloc(total * sizeof(int64_t));
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
                result->items = (int64_t*)heap_data_alloc(total * sizeof(int64_t));
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
                result->items = (double*)heap_data_alloc(total * sizeof(double));
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
    else if (left_type <= LMD_TYPE_BINARY && right_type <= LMD_TYPE_BINARY) {
        // scalar ++ scalar: convert both to string and concatenate
        if (left_type == LMD_TYPE_NULL) return right;
        if (right_type == LMD_TYPE_NULL) return left;
        String *left_str = fn_string(left), *right_str = fn_string(right);
        String *result = fn_strcat(left_str, right_str);
        return {.item = s2it(result)};
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
        result->len = 0;
        result->is_ascii = 1;
        result->chars[0] = '\0';
        return result;
    }

    size_t str_len = str->len;
    size_t total_len = str_len * times;
    String *result = (String *)heap_alloc(sizeof(String) + total_len + 1, LMD_TYPE_STRING);
    result->len = total_len;
    result->is_ascii = str->is_ascii;

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
    result->len = normalized_len;
    result->is_ascii = str_is_ascii((const char*)normalized, normalized_len) ? 1 : 0;
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
        return range;
    }
    else {
        log_error("unknown range type: %s, %s", get_type_name(item_a._type_id), get_type_name(item_b._type_id));
        return NULL;
    }
}

Function* to_fn(fn_ptr ptr) {
    Function *fn = (Function*)heap_calloc(sizeof(Function), LMD_TYPE_FUNC);
    fn->type_id = LMD_TYPE_FUNC;
    fn->ptr = ptr;
    fn->closure_env = NULL;
    return fn;
}

// Create function with arity info
Function* to_fn_n(fn_ptr ptr, int arity) {
    Function *fn = (Function*)heap_calloc(sizeof(Function), LMD_TYPE_FUNC);
    fn->type_id = LMD_TYPE_FUNC;
    fn->arity = (uint8_t)arity;
    fn->ptr = ptr;
    fn->closure_env = NULL;
    fn->name = NULL;
    return fn;
}

// Create function with arity and name for stack traces
Function* to_fn_named(fn_ptr ptr, int arity, const char* name) {
    Function *fn = (Function*)heap_calloc(sizeof(Function), LMD_TYPE_FUNC);
    fn->type_id = LMD_TYPE_FUNC;
    fn->arity = (uint8_t)arity;
    fn->ptr = ptr;
    fn->closure_env = NULL;
    fn->name = name;  // should be interned string, no need to copy
    return fn;
}

// Create a closure with captured environment
Function* to_closure(fn_ptr ptr, int arity, void* env) {
    Function* fn = (Function*)heap_calloc(sizeof(Function), LMD_TYPE_FUNC);
    fn->type_id = LMD_TYPE_FUNC;
    fn->fn_type = NULL;
    fn->arity = (uint8_t)arity;
    fn->closure_field_count = 0;  // caller sets after creation if env has Item fields
    fn->ptr = ptr;
    fn->closure_env = env;
    fn->name = NULL;
    return fn;
}

// Create a closure with captured environment and name for stack traces
Function* to_closure_named(fn_ptr ptr, int arity, void* env, const char* name) {
    Function* fn = (Function*)heap_calloc(sizeof(Function), LMD_TYPE_FUNC);
    fn->type_id = LMD_TYPE_FUNC;
    fn->fn_type = NULL;
    fn->arity = (uint8_t)arity;
    fn->closure_field_count = 0;  // caller sets after creation if env has Item fields
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

    // For closures, env is passed as first argument (closures never have FN_FLAG_BOXED_RET)
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

    // Non-closure dispatch: check if function returns RetItem (_b boxed wrapper)
    if (fn->flags & FN_FLAG_BOXED_RET) {
        RetItem ri;
        switch (arg_count) {
            case 0: ri = ((RetItem(*)())fn->ptr)(); break;
            case 1: ri = ((RetItem(*)(Item))fn->ptr)(args->items[0]); break;
            case 2: ri = ((RetItem(*)(Item,Item))fn->ptr)(args->items[0], args->items[1]); break;
            case 3: ri = ((RetItem(*)(Item,Item,Item))fn->ptr)(args->items[0], args->items[1], args->items[2]); break;
            case 4: ri = ((RetItem(*)(Item,Item,Item,Item))fn->ptr)(args->items[0], args->items[1], args->items[2], args->items[3]); break;
            case 5: ri = ((RetItem(*)(Item,Item,Item,Item,Item))fn->ptr)(args->items[0], args->items[1], args->items[2], args->items[3], args->items[4]); break;
            case 6: ri = ((RetItem(*)(Item,Item,Item,Item,Item,Item))fn->ptr)(args->items[0], args->items[1], args->items[2], args->items[3], args->items[4], args->items[5]); break;
            case 7: ri = ((RetItem(*)(Item,Item,Item,Item,Item,Item,Item))fn->ptr)(args->items[0], args->items[1], args->items[2], args->items[3], args->items[4], args->items[5], args->items[6]); break;
            case 8: ri = ((RetItem(*)(Item,Item,Item,Item,Item,Item,Item,Item))fn->ptr)(args->items[0], args->items[1], args->items[2], args->items[3], args->items[4], args->items[5], args->items[6], args->items[7]); break;
            default:
                log_error("fn_call: unsupported argument count %d (max 8)", arg_count);
                return ItemError;
        }
        return ri_to_item(ri);
    }

    // Non-closure, Item-returning dispatch (legacy path)
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
// For _b boxed functions (FN_FLAG_BOXED_RET), cast to RetItem return and convert via ri_to_item()
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
    } else if (fn->flags & FN_FLAG_BOXED_RET) {
        return ri_to_item(((RetItem(*)())fn->ptr)());
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
    } else if (fn->flags & FN_FLAG_BOXED_RET) {
        return ri_to_item(((RetItem(*)(Item))fn->ptr)(a));
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
    } else if (fn->flags & FN_FLAG_BOXED_RET) {
        return ri_to_item(((RetItem(*)(Item,Item))fn->ptr)(a, b));
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
    } else if (fn->flags & FN_FLAG_BOXED_RET) {
        return ri_to_item(((RetItem(*)(Item,Item,Item))fn->ptr)(a, b, c));
    } else {
        return ((Item(*)(Item,Item,Item))fn->ptr)(a, b, c);
    }
}

Bool fn_is(Item a, Item b) {
    TypeId b_type_id = get_type_id(b);

    if (b_type_id != LMD_TYPE_TYPE) {
        // RHS is a value, not a type — treat as equality comparison
        return fn_eq(a, b);
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
        return pattern_full_match_chars(pattern, chars, len) ? BOOL_TRUE : BOOL_FALSE;
    }

    // Handle constrained type: base_type where (constraint)
    if (b_type->kind == TYPE_KIND_CONSTRAINED) {
        TypeConstrained* constrained = (TypeConstrained*)b_type;

        // First check if value matches the base type
        // Need to check against the inner base type directly
        TypeId a_type_id = get_type_id(a);
        Type* base = constrained->base;

        // Simple base type check (for primitive types like int, string, etc.)
        if (base->type_id != LMD_TYPE_TYPE) {
            // Base is a primitive type like int, string, etc.
            if (a_type_id != base->type_id) {
                // Allow numeric coercion: int is valid for int64, float is valid for float64, etc.
                if (!(LMD_TYPE_INT <= a_type_id && a_type_id <= base->type_id &&
                      LMD_TYPE_INT <= base->type_id && base->type_id <= LMD_TYPE_NUMBER)) {
                    return BOOL_FALSE;
                }
            }
        }
        // TODO: handle complex base types (arrays, maps, etc.) via recursive fn_is call

        // For now, return true if base type matches - full constraint eval requires context setup
        // TODO: implement constraint evaluation by setting _pipe_item = a and evaluating constraint
        return BOOL_TRUE;
    }

    // If b is a TypeUnary directly (kind = TYPE_KIND_UNARY), handle it directly
    if (b_type->kind == TYPE_KIND_UNARY) {
        TypeUnary* type_unary = (TypeUnary*)b_type;
        // Use full type validation for occurrence types
        ValidationResult* result = schema_validator_validate_type(context->validator, a.to_const(), b_type);
        if (result->error_count > 0) {
            print_validation_result(result);
        } else {
        }
        return result->valid ? BOOL_TRUE : BOOL_FALSE;
    }

    // If b is a TypeBinary directly (kind = TYPE_KIND_BINARY), handle it via validator
    if (b_type->kind == TYPE_KIND_BINARY) {
        TypeBinary* type_binary = (TypeBinary*)b_type;
        // Use full type validation for union/intersection types
        ValidationResult* result = schema_validator_validate_type(context->validator, a.to_const(), b_type);
        if (result->error_count > 0) {
            print_validation_result(result);
        } else {
        }
        return result->valid ? BOOL_TRUE : BOOL_FALSE;
    }

    TypeType *type_b = (TypeType *)b_type;
    TypeId a_type_id = get_type_id(a);

    // Check if inner type is TypeUnary (occurrence operator: ?, +, *, [n], [n+], [n,m])
    if (type_b->type->kind == TYPE_KIND_UNARY) {
        TypeUnary* type_unary = (TypeUnary*)type_b->type;
        // Use full type validation for occurrence types
        ValidationResult* result = schema_validator_validate_type(context->validator, a.to_const(), type_b->type);
        if (result->error_count > 0) {
            print_validation_result(result);
        } else {
        }
        return result->valid ? BOOL_TRUE : BOOL_FALSE;
    }

    // Check if inner type is TypeBinary (union/intersection: |, &, \)
    if (type_b->type->kind == TYPE_KIND_BINARY) {
        TypeBinary* type_binary = (TypeBinary*)type_b->type;
        // Use full type validation for union/intersection types
        ValidationResult* result = schema_validator_validate_type(context->validator, a.to_const(), type_b->type);
        if (result->error_count > 0) {
            print_validation_result(result);
        } else {
        }
        return result->valid ? BOOL_TRUE : BOOL_FALSE;
    }

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
    case LMD_TYPE_ARRAY: case LMD_TYPE_LIST: case LMD_TYPE_MAP: case LMD_TYPE_ELEMENT: case LMD_TYPE_OBJECT:
        if (type_b == &LIT_TYPE_ARRAY) {  // fast path
            return a_type_id == LMD_TYPE_RANGE || a_type_id == LMD_TYPE_ARRAY || a_type_id == LMD_TYPE_ARRAY_INT ||
                a_type_id == LMD_TYPE_ARRAY_INT64 || a_type_id == LMD_TYPE_ARRAY_FLOAT;
        }
        else if (type_b == &LIT_TYPE_LIST) {  // fast path for generic list type
            return a_type_id == LMD_TYPE_LIST;
        }
        else if (type_b->type->type_id == LMD_TYPE_OBJECT && type_b->type != &TYPE_OBJECT) {
            // Nominal type check for named object types (e.g., c is Calc)
            if (a_type_id != LMD_TYPE_OBJECT) return BOOL_FALSE;
            Object* obj = (Object*)(uintptr_t)a.item;
            TypeObject* obj_type = (TypeObject*)obj->type;
            TypeObject* expected = (TypeObject*)type_b->type;
            // walk inheritance chain
            TypeObject* walk = obj_type;
            bool nominal_match = false;
            while (walk) {
                if (walk == expected) { nominal_match = true; break; }
                walk = walk->base;
            }
            if (!nominal_match) return BOOL_FALSE;
            // check object constraint (field-level + object-level that constraints)
            if (expected->constraint_fn) {
                if (!expected->constraint_fn(a.item)) {
                    return BOOL_FALSE;
                }
            }
            return BOOL_TRUE;
        }
        else if (type_b->type == &TYPE_OBJECT) {
            // Generic "object" type check — any object matches
            return a_type_id == LMD_TYPE_OBJECT ? BOOL_TRUE : BOOL_FALSE;
        }
        else {  // full type validation
            ValidationResult* result = schema_validator_validate_type(context->validator, a.to_const(), type_b->type);
            if (result->error_count > 0) {
                print_validation_result(result);
            } else {
            }
            return result->valid ? BOOL_TRUE : BOOL_FALSE;
        }
    default:
        return a_type_id == type_b->type->type_id;
    }
}

// IEEE NaN check: expr is nan
Bool fn_is_nan(Item a) {
    TypeId tid = get_type_id(a);
    if (tid == LMD_TYPE_FLOAT) {
        double val = a.get_double();
        return __builtin_isnan(val) ? BOOL_TRUE : BOOL_FALSE;
    }
    return BOOL_FALSE;  // non-float values are never NaN
}

// maximum recursion depth for structural equality comparison
#define EQ_MAX_DEPTH 256

// forward declaration for recursive structural equality
static Bool fn_eq_depth(Item a_item, Item b_item, int depth);

// forward declaration: get field value from a map's packed data by shape entry
static Item _map_field_value(TypeMap* map_type, void* data, ShapeEntry* field);

// helper: structural equality for list/array items (element-wise comparison)
static Bool list_eq(List* a, List* b, int depth) {
    if (a == b) return BOOL_TRUE;  // pointer identity fast-path
    if (a->length != b->length) return BOOL_FALSE;
    for (int64_t i = 0; i < a->length; i++) {
        Bool r = fn_eq_depth(a->items[i], b->items[i], depth + 1);
        if (r == BOOL_ERROR) return BOOL_ERROR;
        if (r == BOOL_FALSE) return BOOL_FALSE;
    }
    return BOOL_TRUE;
}

// helper: structural equality for typed int arrays
static Bool array_int_eq(ArrayInt* a, ArrayInt* b) {
    if (a == b) return BOOL_TRUE;
    if (a->length != b->length) return BOOL_FALSE;
    return (memcmp(a->items, b->items, a->length * sizeof(int64_t)) == 0) ? BOOL_TRUE : BOOL_FALSE;
}

// helper: structural equality for typed int64 arrays
static Bool array_int64_eq(ArrayInt64* a, ArrayInt64* b) {
    if (a == b) return BOOL_TRUE;
    if (a->length != b->length) return BOOL_FALSE;
    return (memcmp(a->items, b->items, a->length * sizeof(int64_t)) == 0) ? BOOL_TRUE : BOOL_FALSE;
}

// helper: structural equality for typed float arrays
static Bool array_float_eq(ArrayFloat* a, ArrayFloat* b) {
    if (a == b) return BOOL_TRUE;
    if (a->length != b->length) return BOOL_FALSE;
    // element-wise comparison to respect NaN != NaN
    for (int64_t i = 0; i < a->length; i++) {
        if (a->items[i] != b->items[i]) return BOOL_FALSE;
    }
    return BOOL_TRUE;
}

// helper: structural equality for maps (order-independent key-value comparison)
static Bool map_eq(Map* a, Map* b, int depth) {
    if (a == b) return BOOL_TRUE;  // pointer identity fast-path
    TypeMap* type_a = (TypeMap*)a->type;
    TypeMap* type_b = (TypeMap*)b->type;
    if (!type_a || !type_b) return BOOL_FALSE;
    if (type_a->length != type_b->length) return BOOL_FALSE;

    // same-shape fast path: keys are identical in same order, compare values by slot
    if (type_a == type_b) {
        ShapeEntry* field = type_a->shape;
        while (field) {
            Item val_a = _map_field_value(type_a, a->data, field);
            Item val_b = _map_field_value(type_b, b->data, field);
            Bool r = fn_eq_depth(val_a, val_b, depth + 1);
            if (r == BOOL_ERROR) return BOOL_ERROR;
            if (r == BOOL_FALSE) return BOOL_FALSE;
            field = field->next;
        }
        return BOOL_TRUE;
    }

    // different shapes: for each key in A, look up in B by name+namespace
    ShapeEntry* field_a = type_a->shape;
    while (field_a) {
        // find matching field in B by name and namespace
        ShapeEntry* field_b = type_b->shape;
        bool found = false;
        while (field_b) {
            if (strview_equal(field_a->name, field_b->name->str) &&
                target_equal(field_a->ns, field_b->ns)) {
                found = true;
                break;
            }
            field_b = field_b->next;
        }
        if (!found) return BOOL_FALSE;

        Item val_a = _map_field_value(type_a, a->data, field_a);
        Item val_b = _map_field_value(type_b, b->data, field_b);
        Bool r = fn_eq_depth(val_a, val_b, depth + 1);
        if (r == BOOL_ERROR) return BOOL_ERROR;
        if (r == BOOL_FALSE) return BOOL_FALSE;
        field_a = field_a->next;
    }
    // key-count check already done above — no extra keys in B
    return BOOL_TRUE;
}

// helper: structural equality for elements (tag + attrs + children)
static Bool element_eq(Element* a, Element* b, int depth) {
    if (a == b) return BOOL_TRUE;  // pointer identity fast-path

    // compare tag names
    TypeElmt* type_a = (TypeElmt*)a->type;
    TypeElmt* type_b = (TypeElmt*)b->type;
    if (type_a->name.length != type_b->name.length) return BOOL_FALSE;
    if (type_a->name.length > 0 &&
        strncmp(type_a->name.str, type_b->name.str, type_a->name.length) != 0) return BOOL_FALSE;
    // compare namespaces
    if (!target_equal(type_a->ns, type_b->ns)) return BOOL_FALSE;

    // compare attributes (map part) — use map_eq on the element's map fields
    Bool attr_r = map_eq((Map*)a, (Map*)b, depth);
    if (attr_r != BOOL_TRUE) return attr_r;

    // compare children (list part)
    if (a->length != b->length) return BOOL_FALSE;
    for (int64_t i = 0; i < a->length; i++) {
        Bool r = fn_eq_depth(a->items[i], b->items[i], depth + 1);
        if (r == BOOL_ERROR) return BOOL_ERROR;
        if (r == BOOL_FALSE) return BOOL_FALSE;
    }
    return BOOL_TRUE;
}

// helper: structural equality for VMaps (virtual maps)
static Bool vmap_eq(VMap* a, VMap* b, int depth) {
    if (a == b) return BOOL_TRUE;
    int64_t count_a = a->vtable->count(a->data);
    int64_t count_b = b->vtable->count(b->data);
    if (count_a != count_b) return BOOL_FALSE;

    // iterate A's keys and look up each in B
    for (int64_t i = 0; i < count_a; i++) {
        Item key = a->vtable->key_at(a->data, i);
        Item val_a = a->vtable->value_at(a->data, i);
        Item val_b = b->vtable->get(b->data, key);
        // if key not found in B, val_b will be null but val_a shouldn't be
        Bool r = fn_eq_depth(val_a, val_b, depth + 1);
        if (r == BOOL_ERROR) return BOOL_ERROR;
        if (r == BOOL_FALSE) return BOOL_FALSE;
    }
    return BOOL_TRUE;
}

// helper: cross-type array equality (e.g. array[int] vs array[float])
// promotes each element via fn_eq_depth to leverage numeric promotion
static Bool cross_array_eq(Item a_item, Item b_item, int depth) {
    // get lengths for both array types
    int64_t len_a = 0, len_b = 0;
    TypeId a_tid = get_type_id(a_item);
    TypeId b_tid = get_type_id(b_item);

    if (a_tid == LMD_TYPE_ARRAY)       len_a = a_item.array->length;
    else if (a_tid == LMD_TYPE_ARRAY_INT)   len_a = a_item.array_int->length;
    else if (a_tid == LMD_TYPE_ARRAY_INT64) len_a = a_item.array_int64->length;
    else if (a_tid == LMD_TYPE_ARRAY_FLOAT) len_a = a_item.array_float->length;

    if (b_tid == LMD_TYPE_ARRAY)       len_b = b_item.array->length;
    else if (b_tid == LMD_TYPE_ARRAY_INT)   len_b = b_item.array_int->length;
    else if (b_tid == LMD_TYPE_ARRAY_INT64) len_b = b_item.array_int64->length;
    else if (b_tid == LMD_TYPE_ARRAY_FLOAT) len_b = b_item.array_float->length;

    if (len_a != len_b) return BOOL_FALSE;

    for (int64_t i = 0; i < len_a; i++) {
        Item elem_a, elem_b;
        // extract element from a
        if (a_tid == LMD_TYPE_ARRAY)       elem_a = a_item.array->items[i];
        else if (a_tid == LMD_TYPE_ARRAY_INT)   elem_a = {.item = i2it(a_item.array_int->items[i])};
        else if (a_tid == LMD_TYPE_ARRAY_INT64) elem_a = push_l(a_item.array_int64->items[i]);
        else                                    elem_a = push_d(a_item.array_float->items[i]);
        // extract element from b
        if (b_tid == LMD_TYPE_ARRAY)       elem_b = b_item.array->items[i];
        else if (b_tid == LMD_TYPE_ARRAY_INT)   elem_b = {.item = i2it(b_item.array_int->items[i])};
        else if (b_tid == LMD_TYPE_ARRAY_INT64) elem_b = push_l(b_item.array_int64->items[i]);
        else                                    elem_b = push_d(b_item.array_float->items[i]);

        Bool r = fn_eq_depth(elem_a, elem_b, depth + 1);
        if (r == BOOL_ERROR) return BOOL_ERROR;
        if (r == BOOL_FALSE) return BOOL_FALSE;
    }
    return BOOL_TRUE;
}

// helper: get element at index from any sequence type (list, array, range)
static inline Item seq_get_element(Item item, TypeId tid, int64_t i) {
    switch (tid) {
    case LMD_TYPE_LIST:        return item.list->items[i];
    case LMD_TYPE_ARRAY:       return item.array->items[i];
    case LMD_TYPE_ARRAY_INT:   return {.item = i2it(item.array_int->items[i])};
    case LMD_TYPE_ARRAY_INT64: return push_l(item.array_int64->items[i]);
    case LMD_TYPE_ARRAY_FLOAT: return push_d(item.array_float->items[i]);
    case LMD_TYPE_RANGE:       return {.item = i2it(item.range->start + i)};
    default:                   return ItemNull;
    }
}

// helper: get length from any sequence type
static inline int64_t seq_get_length(Item item, TypeId tid) {
    switch (tid) {
    case LMD_TYPE_LIST:        return item.list->length;
    case LMD_TYPE_ARRAY:       return item.array->length;
    case LMD_TYPE_ARRAY_INT:   return item.array_int->length;
    case LMD_TYPE_ARRAY_INT64: return item.array_int64->length;
    case LMD_TYPE_ARRAY_FLOAT: return item.array_float->length;
    case LMD_TYPE_RANGE:       return item.range->length;
    default:                   return -1;
    }
}

// helper: cross-type sequence equality (range vs list, list vs array, etc.)
// element-wise comparison with numeric promotion
static Bool cross_seq_eq(Item a_item, TypeId a_tid, Item b_item, TypeId b_tid, int depth) {
    int64_t len_a = seq_get_length(a_item, a_tid);
    int64_t len_b = seq_get_length(b_item, b_tid);
    if (len_a != len_b) return BOOL_FALSE;

    for (int64_t i = 0; i < len_a; i++) {
        Item elem_a = seq_get_element(a_item, a_tid, i);
        Item elem_b = seq_get_element(b_item, b_tid, i);
        Bool r = fn_eq_depth(elem_a, elem_b, depth + 1);
        if (r == BOOL_ERROR) return BOOL_ERROR;
        if (r == BOOL_FALSE) return BOOL_FALSE;
    }
    return BOOL_TRUE;
}

// 3-states comparison with depth tracking for structural equality
static Bool fn_eq_depth(Item a_item, Item b_item, int depth) {
    if (depth > EQ_MAX_DEPTH) {
        log_error("eq depth limit exceeded: structural equality recursion too deep (> %d)", EQ_MAX_DEPTH);
        return BOOL_ERROR;
    }

    if (a_item._type_id != b_item._type_id) {
        // special case: type(null) == null, type(error) == error
        // when one side is a type value and the other is null/error,
        // compare the type's inner type_id against the value's type_id
        if (a_item._type_id == LMD_TYPE_NULL || b_item._type_id == LMD_TYPE_NULL ||
            a_item._type_id == LMD_TYPE_ERROR || b_item._type_id == LMD_TYPE_ERROR) {
            // check if either side is a type value (container pointer with type_id == LMD_TYPE_TYPE)
            Item type_item, value_item;
            if (get_type_id(a_item) == LMD_TYPE_TYPE) {
                type_item = a_item; value_item = b_item;
            } else if (get_type_id(b_item) == LMD_TYPE_TYPE) {
                type_item = b_item; value_item = a_item;
            } else {
                // neither side is a type value — use existing null/error semantics
                return BOOL_FALSE;
            }
            // compare type's inner type_id against the value's type_id
            TypeType* tt = (TypeType*)type_item.type;
            return (tt->type->type_id == get_type_id(value_item)) ? BOOL_TRUE : BOOL_FALSE;
        }
        // number promotion - only for int/float types
        if (LMD_TYPE_INT <= a_item._type_id && a_item._type_id <= LMD_TYPE_NUMBER &&
            LMD_TYPE_INT <= b_item._type_id && b_item._type_id <= LMD_TYPE_NUMBER) {
            double a_val = it2d(a_item), b_val = it2d(b_item);
            return (a_val == b_val) ? BOOL_TRUE : BOOL_FALSE;
        }

        // cross-type sequence comparison: range, list, and array types
        // all represent ordered sequences and can be compared element-wise
        TypeId a_tid = (a_item._type_id == LMD_TYPE_RAW_POINTER) ? get_type_id(a_item) : (TypeId)a_item._type_id;
        TypeId b_tid = (b_item._type_id == LMD_TYPE_RAW_POINTER) ? get_type_id(b_item) : (TypeId)b_item._type_id;
        bool a_is_seq = (a_tid == LMD_TYPE_LIST || a_tid == LMD_TYPE_ARRAY ||
                         a_tid == LMD_TYPE_ARRAY_INT || a_tid == LMD_TYPE_ARRAY_INT64 ||
                         a_tid == LMD_TYPE_ARRAY_FLOAT || a_tid == LMD_TYPE_RANGE);
        bool b_is_seq = (b_tid == LMD_TYPE_LIST || b_tid == LMD_TYPE_ARRAY ||
                         b_tid == LMD_TYPE_ARRAY_INT || b_tid == LMD_TYPE_ARRAY_INT64 ||
                         b_tid == LMD_TYPE_ARRAY_FLOAT || b_tid == LMD_TYPE_RANGE);
        if (a_is_seq && b_is_seq) {
            return cross_seq_eq(a_item, a_tid, b_item, b_tid, depth);
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
        int cmp = decimal_cmp_items(a_item, b_item);
        return (cmp == 0) ? BOOL_TRUE : BOOL_FALSE;
    }
    else if (a_item._type_id == LMD_TYPE_DTIME) {
        DateTime dt_a = a_item.get_datetime();  DateTime dt_b = b_item.get_datetime();
        return (datetime_compare(&dt_a, &dt_b) == 0) ? BOOL_TRUE : BOOL_FALSE;
    }
    else if (a_item._type_id == LMD_TYPE_STRING || a_item._type_id == LMD_TYPE_BINARY) {
        // pointer identity fast-path (interned strings, same allocation)
        if (a_item.string_ptr == b_item.string_ptr) return BOOL_TRUE;
        const char* chars_a = a_item.get_chars();  const char* chars_b = b_item.get_chars();
        uint32_t len_a = a_item.get_len();  uint32_t len_b = b_item.get_len();
        bool result = (len_a == len_b && memcmp(chars_a, chars_b, len_a) == 0);
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
    else if (a_item._type_id == LMD_TYPE_RAW_POINTER) {
        // both are container/pointer types - resolve actual type_id
        TypeId a_tid = get_type_id(a_item);
        TypeId b_tid = get_type_id(b_item);

        // pointer identity fast-path for all container types
        if (a_item.item == b_item.item) return BOOL_TRUE;

        // type values
        if (a_tid == LMD_TYPE_TYPE && b_tid == LMD_TYPE_TYPE) {
            // compare type values by their inner type
            TypeType* a_tt = (TypeType*)a_item.type;
            TypeType* b_tt = (TypeType*)b_item.type;
            return (a_tt->type->type_id == b_tt->type->type_id) ? BOOL_TRUE : BOOL_FALSE;
        }

        // structural equality for container types requires same resolved type
        if (a_tid != b_tid) {
            // cross-type sequence comparison (array[int] vs array[float], list vs range, etc.)
            bool a_is_seq = (a_tid == LMD_TYPE_LIST || a_tid == LMD_TYPE_ARRAY ||
                             a_tid == LMD_TYPE_ARRAY_INT || a_tid == LMD_TYPE_ARRAY_INT64 ||
                             a_tid == LMD_TYPE_ARRAY_FLOAT || a_tid == LMD_TYPE_RANGE);
            bool b_is_seq = (b_tid == LMD_TYPE_LIST || b_tid == LMD_TYPE_ARRAY ||
                             b_tid == LMD_TYPE_ARRAY_INT || b_tid == LMD_TYPE_ARRAY_INT64 ||
                             b_tid == LMD_TYPE_ARRAY_FLOAT || b_tid == LMD_TYPE_RANGE);
            if (a_is_seq && b_is_seq) {
                return cross_seq_eq(a_item, a_tid, b_item, b_tid, depth);
            }
            return BOOL_ERROR;
        }

        // list structural equality
        if (a_tid == LMD_TYPE_LIST) {
            return list_eq(a_item.list, b_item.list, depth);
        }
        // generic array (array of Items) structural equality
        if (a_tid == LMD_TYPE_ARRAY) {
            return list_eq((List*)a_item.array, (List*)b_item.array, depth);
        }
        // typed array equality
        if (a_tid == LMD_TYPE_ARRAY_INT) {
            return array_int_eq(a_item.array_int, b_item.array_int);
        }
        if (a_tid == LMD_TYPE_ARRAY_INT64) {
            return array_int64_eq(a_item.array_int64, b_item.array_int64);
        }
        if (a_tid == LMD_TYPE_ARRAY_FLOAT) {
            return array_float_eq(a_item.array_float, b_item.array_float);
        }
        // map structural equality (order-independent)
        if (a_tid == LMD_TYPE_MAP) {
            return map_eq(a_item.map, b_item.map, depth);
        }
        // element structural equality (tag + attrs + children)
        if (a_tid == LMD_TYPE_ELEMENT) {
            return element_eq(a_item.element, b_item.element, depth);
        }
        // vmap structural equality
        if (a_tid == LMD_TYPE_VMAP) {
            return vmap_eq(a_item.vmap, b_item.vmap, depth);
        }
        // range equality (start, end, length)
        if (a_tid == LMD_TYPE_RANGE) {
            Range* ra = a_item.range;
            Range* rb = b_item.range;
            return (ra->start == rb->start && ra->end == rb->end && ra->length == rb->length)
                ? BOOL_TRUE : BOOL_FALSE;
        }
        // object structural equality (same as map, fields must match)
        if (a_tid == LMD_TYPE_OBJECT) {
            Object* oa = a_item.object;
            Object* ob = b_item.object;
            return map_eq((Map*)oa, (Map*)ob, depth);
        }
        // function reference equality
        if (a_tid == LMD_TYPE_FUNC) {
            return (a_item.function == b_item.function) ? BOOL_TRUE : BOOL_FALSE;
        }
        log_error("eq unsupported container type: %s", get_type_name(a_tid));
        return BOOL_ERROR;
    }
    log_error("unknown comparing type: %s", get_type_name(a_item._type_id));
    return BOOL_ERROR;
}

// Raw-pointer string equality for MIR inline fast path.
// Called when pointer identity check fails — compare content directly.
// Avoids boxing overhead and type dispatch of fn_eq.
Bool fn_str_eq_ptr(String* a, String* b) {
    if (a == b) return BOOL_TRUE;
    if (!a || !b) return BOOL_FALSE;
    if (a->len != b->len) return BOOL_FALSE;
    if (a->len == 0) return BOOL_TRUE;
    return memcmp(a->chars, b->chars, a->len) == 0 ? BOOL_TRUE : BOOL_FALSE;
}

// Raw-pointer symbol equality for MIR inline fast path.
Bool fn_sym_eq_ptr(Symbol* a, Symbol* b) {
    if (a == b) return BOOL_TRUE;
    if (!a || !b) return BOOL_FALSE;
    if (a->len != b->len) return BOOL_FALSE;
    if (a->len > 0 && strncmp(a->chars, b->chars, a->len) != 0) return BOOL_FALSE;
    return target_equal(a->ns, b->ns) ? BOOL_TRUE : BOOL_FALSE;
}

// 3-states comparison (public entry point)
Bool fn_eq(Item a_item, Item b_item) {
    return fn_eq_depth(a_item, b_item, 0);
}

Bool fn_ne(Item a_item, Item b_item) {
    Bool result = fn_eq(a_item, b_item);
    if (result == BOOL_ERROR) { return BOOL_ERROR; }
    return !result;
}

// 3-state value/ordered comparison
Bool fn_lt(Item a_item, Item b_item) {
    if (a_item._type_id != b_item._type_id) {
        // null comparison with any type returns false for ordered comparisons
        if (a_item._type_id == LMD_TYPE_NULL || b_item._type_id == LMD_TYPE_NULL) {
            return BOOL_FALSE;
        }
        // number promotion - only for int/float types
        if (LMD_TYPE_INT <= a_item._type_id && a_item._type_id <= LMD_TYPE_NUMBER &&
            LMD_TYPE_INT <= b_item._type_id && b_item._type_id <= LMD_TYPE_NUMBER) {
            double a_val = it2d(a_item), b_val = it2d(b_item);
            return (a_val < b_val) ? BOOL_TRUE : BOOL_FALSE;
        }
        // Type mismatch error for ordered comparisons
        return BOOL_ERROR;
    }

    if (a_item._type_id == LMD_TYPE_NULL) {
        return BOOL_FALSE;  // null < null = false
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
        int cmp = decimal_cmp_items(a_item, b_item);
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
    if (a_item._type_id != b_item._type_id) {
        // null comparison with any type returns false for ordered comparisons
        if (a_item._type_id == LMD_TYPE_NULL || b_item._type_id == LMD_TYPE_NULL) {
            return BOOL_FALSE;
        }
        // number promotion - only for int/float types
        if (LMD_TYPE_INT <= a_item._type_id && a_item._type_id <= LMD_TYPE_NUMBER &&
            LMD_TYPE_INT <= b_item._type_id && b_item._type_id <= LMD_TYPE_NUMBER) {
            double a_val = it2d(a_item), b_val = it2d(b_item);
            return (a_val > b_val) ? BOOL_TRUE : BOOL_FALSE;
        }
        // Type mismatch error for ordered comparisons
        return BOOL_ERROR;
    }

    if (a_item._type_id == LMD_TYPE_NULL) {
        return BOOL_FALSE;  // null > null = false
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
        int cmp = decimal_cmp_items(a_item, b_item);
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
    Bool result = fn_gt(a_item, b_item);
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
    Bool result = is_truthy(item);
    if (result == BOOL_ERROR) return BOOL_ERROR;
    return !result;
}

// 3-state AND with short-circuiting truthy idiom
Item fn_and(Item a_item, Item b_item) {
    // fast path for boolean types
    if (a_item._type_id == LMD_TYPE_BOOL && b_item._type_id == LMD_TYPE_BOOL) {
        return {.item = b2it(a_item.bool_val && b_item.bool_val)};
    }
    // fallback to generic truthy idiom
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

// helper: get field value from a map/element's packed data by shape entry
static Item _map_field_value(TypeMap* map_type, void* data, ShapeEntry* field) {
    if (!data || !field) return ItemNull;
    TypeId type_id = field->type->type_id;
    void* field_ptr = (char*)data + field->byte_offset;
    switch (type_id) {
    case LMD_TYPE_NULL: {
        void* ptr = *(void**)field_ptr;
        if (ptr) return {.container = (Container*)ptr};
        return ItemNull;
    }
    case LMD_TYPE_BOOL:   return {.item = b2it(*(bool*)field_ptr)};
    case LMD_TYPE_INT:    return {.item = i2it(*(int64_t*)field_ptr)};
    case LMD_TYPE_INT64:  return push_l(*(int64_t*)field_ptr);
    case LMD_TYPE_FLOAT:  return push_d(*(double*)field_ptr);
    case LMD_TYPE_DTIME:  return push_k(*(DateTime*)field_ptr);
    case LMD_TYPE_DECIMAL: return {.item = c2it(*(char**)field_ptr)};
    case LMD_TYPE_STRING: return {.item = s2it(*(char**)field_ptr)};
    case LMD_TYPE_SYMBOL: return {.item = y2it(*(char**)field_ptr)};
    case LMD_TYPE_BINARY: return {.item = x2it(*(char**)field_ptr)};
    case LMD_TYPE_RANGE: case LMD_TYPE_ARRAY: case LMD_TYPE_ARRAY_INT:
    case LMD_TYPE_ARRAY_INT64: case LMD_TYPE_ARRAY_FLOAT:
    case LMD_TYPE_LIST: case LMD_TYPE_MAP: case LMD_TYPE_ELEMENT: case LMD_TYPE_OBJECT: {
        Container* c = *(Container**)field_ptr;
        if (!c) return ItemNull;
        return {.container = c};
    }
    default: return ItemNull;
    }
}

// recursive helper: collect all items matching type_val into result array
static void query_collect(Item data, Item type_val, bool self_inclusive, Array* result, int depth) {
    if (depth > 1000) return;  // prevent infinite recursion
    if (!data.item) return;

    // check if this item matches the type (only at root level when self-inclusive)
    if (self_inclusive) {
        Bool match = fn_is(data, type_val);
        if (match == BOOL_TRUE) {
            array_push(result, data);
        }
    }

    // recurse into attributes and children (both ? and .? do this)
    TypeId type_id = get_type_id(data);
    if (type_id == LMD_TYPE_ELEMENT) {
        Element* elmt = data.element;
        // recurse into attributes
        TypeElmt* elmt_type = (TypeElmt*)elmt->type;
        ShapeEntry* field = elmt_type->shape;
        while (field) {
            if (field->name) {
                Item val = _map_field_value((TypeMap*)elmt_type, elmt->data, field);
                // always recurse with self_inclusive=true so descendants are fully searched
                if (val.item) query_collect(val, type_val, true, result, depth + 1);
            }
            field = field->next;
        }
        // recurse into content items
        for (int64_t i = 0; i < elmt->length; i++) {
            query_collect(elmt->items[i], type_val, true, result, depth + 1);
        }
    } else if (type_id == LMD_TYPE_MAP || type_id == LMD_TYPE_OBJECT) {
        Map* map = data.map;
        TypeMap* map_type = (TypeMap*)map->type;
        ShapeEntry* field = map_type->shape;
        while (field) {
            if (field->name) {
                Item val = _map_field_value(map_type, map->data, field);
                if (val.item) query_collect(val, type_val, true, result, depth + 1);
            }
            field = field->next;
        }
    } else if (type_id == LMD_TYPE_LIST) {
        List* lst = data.list;
        for (int64_t i = 0; i < lst->length; i++) {
            query_collect(lst->items[i], type_val, true, result, depth + 1);
        }
    } else if (type_id == LMD_TYPE_ARRAY) {
        Array* arr = (Array*)data.array;
        for (int64_t i = 0; i < arr->length; i++) {
            query_collect(arr->items[i], type_val, true, result, depth + 1);
        }
    } else if (type_id == LMD_TYPE_ARRAY_INT) {
        ArrayInt* arr = data.array_int;
        for (int64_t i = 0; i < arr->length; i++) {
            Item val = array_int_get(arr, (int)i);
            query_collect(val, type_val, true, result, depth + 1);
        }
    } else if (type_id == LMD_TYPE_ARRAY_INT64) {
        ArrayInt64* arr = data.array_int64;
        for (int64_t i = 0; i < arr->length; i++) {
            Item val = array_int64_get(arr, (int)i);
            query_collect(val, type_val, true, result, depth + 1);
        }
    } else if (type_id == LMD_TYPE_ARRAY_FLOAT) {
        ArrayFloat* arr = data.array_float;
        for (int64_t i = 0; i < arr->length; i++) {
            Item val = array_float_get(arr, (int)i);
            query_collect(val, type_val, true, result, depth + 1);
        }
    }
}

Item fn_query(Item data, Item type_val, int direct) {
    TypeId type_tid = get_type_id(type_val);
    if (type_tid != LMD_TYPE_TYPE) {
        log_error("query: 2nd argument must be a type, got: %s", get_type_name(type_tid));
        return ItemNull;
    }
    // use array (not list) to avoid automatic string merging
    Array* result = array_plain();
    result->is_spreadable = true;
    // direct=1 means .? (self-inclusive), direct=0 means ? (not self-inclusive)
    query_collect(data, type_val, direct != 0, result, 0);
    return {.array = result};
}

// child-level query: collect direct attributes + children matching type_val (one level only)
static void child_query_collect(Item data, Item type_val, Array* result) {
    if (!data.item) return;

    TypeId type_id = get_type_id(data);
    if (type_id == LMD_TYPE_ELEMENT) {
        Element* elmt = data.element;
        // check attribute values
        TypeElmt* elmt_type = (TypeElmt*)elmt->type;
        ShapeEntry* field = elmt_type->shape;
        while (field) {
            if (field->name) {
                Item val = _map_field_value((TypeMap*)elmt_type, elmt->data, field);
                if (val.item && fn_is(val, type_val) == BOOL_TRUE) {
                    array_push(result, val);
                }
            }
            field = field->next;
        }
        // check direct children
        for (int64_t i = 0; i < elmt->length; i++) {
            Item child = elmt->items[i];
            if (child.item && fn_is(child, type_val) == BOOL_TRUE) {
                array_push(result, child);
            }
        }
    } else if (type_id == LMD_TYPE_MAP || type_id == LMD_TYPE_OBJECT) {
        Map* map = data.map;
        TypeMap* map_type = (TypeMap*)map->type;
        ShapeEntry* field = map_type->shape;
        while (field) {
            if (field->name) {
                Item val = _map_field_value(map_type, map->data, field);
                if (val.item && fn_is(val, type_val) == BOOL_TRUE) {
                    array_push(result, val);
                }
            }
            field = field->next;
        }
    } else if (type_id == LMD_TYPE_ARRAY) {
        Array* arr = (Array*)data.array;
        if (arr->is_spreadable) {
            // spreadable array (from previous query): distribute child query to each item
            for (int64_t i = 0; i < arr->length; i++) {
                child_query_collect(arr->items[i], type_val, result);
            }
        } else {
            for (int64_t i = 0; i < arr->length; i++) {
                Item child = arr->items[i];
                if (child.item && fn_is(child, type_val) == BOOL_TRUE) {
                    array_push(result, child);
                }
            }
        }
    } else if (type_id == LMD_TYPE_LIST) {
        List* lst = data.list;
        for (int64_t i = 0; i < lst->length; i++) {
            Item child = lst->items[i];
            if (child.item && fn_is(child, type_val) == BOOL_TRUE) {
                array_push(result, child);
            }
        }
    } else if (type_id == LMD_TYPE_ARRAY_INT) {
        ArrayInt* arr = data.array_int;
        for (int64_t i = 0; i < arr->length; i++) {
            Item val = array_int_get(arr, (int)i);
            if (fn_is(val, type_val) == BOOL_TRUE) {
                array_push(result, val);
            }
        }
    } else if (type_id == LMD_TYPE_ARRAY_INT64) {
        ArrayInt64* arr = data.array_int64;
        for (int64_t i = 0; i < arr->length; i++) {
            Item val = array_int64_get(arr, (int)i);
            if (fn_is(val, type_val) == BOOL_TRUE) {
                array_push(result, val);
            }
        }
    } else if (type_id == LMD_TYPE_ARRAY_FLOAT) {
        ArrayFloat* arr = data.array_float;
        for (int64_t i = 0; i < arr->length; i++) {
            Item val = array_float_get(arr, (int)i);
            if (fn_is(val, type_val) == BOOL_TRUE) {
                array_push(result, val);
            }
        }
    }
}

// child-level query: expr[T] where T is a type
Item fn_child_query(Item data, Item type_val) {
    Array* result = array_plain();
    result->is_spreadable = true;
    child_query_collect(data, type_val, result);
    return {.array = result};
}

Bool fn_in(Item a_item, Item b_item) {
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
            int64_t a_val = it2l(a_item);
            for (int64_t i = 0; i < arr->length; i++) {
                if (arr->items[i] == a_val) {
                    return true;
                }
            }
            return false;
        }
        else if (b_type == LMD_TYPE_ARRAY_INT64) {
            ArrayInt64 *arr = b_item.array_int64;
            int64_t a_val = it2l(a_item);
            for (int64_t i = 0; i < arr->length; i++) {
                if (arr->items[i] == a_val) {
                    return true;
                }
            }
            return false;
        }
        else if (b_type == LMD_TYPE_ARRAY_FLOAT) {
            ArrayFloat *arr = b_item.array_float;
            double a_val = it2d(a_item);
            for (int64_t i = 0; i < arr->length; i++) {
                if (arr->items[i] == a_val) {
                    return true;
                }
            }
            return false;
        }
        else if (b_type == LMD_TYPE_MAP || b_type == LMD_TYPE_OBJECT) {
            // value membership: check if a matches any field value
            Map *map = b_item.map;
            TypeMap* map_type = (TypeMap*)map->type;
            if (!map_type) return false;
            ShapeEntry* field = map_type->shape;
            while (field) {
                Item val = _map_field_value(map_type, map->data, field);
                if (fn_eq(val, a_item) == BOOL_TRUE) {
                    return true;
                }
                field = field->next;
            }
            return false;
        }
        else if (b_type == LMD_TYPE_VMAP) {
            // value membership: iterate all values and compare
            VMap* vm = b_item.vmap;
            int64_t count = vm->vtable->count(vm->data);
            for (int64_t i = 0; i < count; i++) {
                Item val = vm->vtable->value_at(vm->data, i);
                if (fn_eq(val, a_item) == BOOL_TRUE) {
                    return true;
                }
            }
            return false;
        }
        else if (b_type == LMD_TYPE_ELEMENT) {
            Element* elmt = b_item.element;
            TypeElmt* elmt_type = (TypeElmt*)elmt->type;
            // check attribute values
            if (elmt_type) {
                ShapeEntry* field = elmt_type->shape;
                while (field) {
                    Item val = _map_field_value((TypeMap*)elmt_type, elmt->data, field);
                    if (fn_eq(val, a_item) == BOOL_TRUE) {
                        return true;
                    }
                    field = field->next;
                }
            }
            // check children (value membership)
            for (int64_t i = 0; i < elmt->length; i++) {
                if (fn_eq(elmt->items[i], a_item) == BOOL_TRUE) {
                    return true;
                }
            }
            return false;
        }
        else {
            log_debug("invalid type %d", b_type);
        }
    }
    return false;
}

String STR_NULL = {.len = 4, .is_ascii = 1, .chars = "null"};
String STR_TRUE = {.len = 4, .is_ascii = 1, .chars = "true"};
String STR_FALSE = {.len = 5, .is_ascii = 1, .chars = "false"};
String STR_ERROR = {.len = 7, .is_ascii = 1, .chars = "<error>"};

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
    case LMD_TYPE_MAP:  case LMD_TYPE_ELEMENT:  case LMD_TYPE_OBJECT: {
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
    case LMD_TYPE_TYPE: {
        // convert Type value to its name string, e.g. type(123) → "int"
        TypeType* type_type = (TypeType*)itm.type;
        if (!type_type || !type_type->type) return &STR_NULL;
        const char* name = get_type_name(type_type->type->type_id);
        if (name) return heap_strcpy((char*)name, strlen(name));
        return &STR_NULL;
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
    return (TypePattern*)type;
}

Type* fn_type(Item item) {
    TypeType *type = (TypeType *)heap_calloc(sizeof(TypeType) + sizeof(Type), LMD_TYPE_TYPE);
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
    // normalize specialized types to their base type for consistent comparison
    // e.g., array[int], array[int64], array[float] all map to 'array'
    //        vmap maps to 'map'
    TypeId tid = item_type->type_id;
    if (tid == LMD_TYPE_ARRAY_INT || tid == LMD_TYPE_ARRAY_INT64 || tid == LMD_TYPE_ARRAY_FLOAT) {
        item_type->type_id = LMD_TYPE_ARRAY;
    } else if (tid == LMD_TYPE_VMAP) {
        item_type->type_id = LMD_TYPE_MAP;
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

RetItem fn_input2(Item target_item, Item type) {
    // Dry-run mode: return fabricated input data
    if (g_dry_run) {
        log_debug("dry-run: fabricated input() call");
        const char* content = "{\"name\": \"dry-run\", \"version\": \"1.0\", \"items\": [1, 2, 3], \"active\": true}";
        // check type hint for more realistic fabrication
        TypeId tid = get_type_id(type);
        if (tid == LMD_TYPE_STRING || tid == LMD_TYPE_SYMBOL) {
            String* ts = fn_string(type);
            if (ts && ts->chars) {
                if (strcmp(ts->chars, "html") == 0) content = "<html><head><title>Mock</title></head><body><p>Dry-run</p></body></html>";
                else if (strcmp(ts->chars, "text") == 0 || strcmp(ts->chars, "txt") == 0) content = "Dry-run fabricated content.\nLine 2.\nLine 3: 42, 3.14\n";
                else if (strcmp(ts->chars, "csv") == 0) content = "name,age,city\nAlice,30,NYC\nBob,25,LA\n";
                else if (strcmp(ts->chars, "yaml") == 0 || strcmp(ts->chars, "yml") == 0) content = "name: dry-run\nversion: 1\n";
                else if (strcmp(ts->chars, "xml") == 0) content = "<root><item id=\"1\">mock</item></root>";
                else if (strcmp(ts->chars, "markdown") == 0 || strcmp(ts->chars, "md") == 0) content = "# Mock\n\nDry-run content.\n";
            }
        }
        String* result_str = heap_strcpy((char*)content, strlen(content));
        return ri_ok({.item = s2it(result_str)});
    }
    String *type_str = NULL, *flavor_str = NULL;

    // Validate target type: must be string, symbol, or path
    TypeId target_type_id = get_type_id(target_item);
    if (target_type_id != LMD_TYPE_STRING && target_type_id != LMD_TYPE_SYMBOL && target_type_id != LMD_TYPE_PATH) {
        log_debug("input target must be a string, symbol, or path, got type: %s", get_type_name(target_type_id));
        return ri_ok(ItemNull);  // todo: push error
    }

    // Convert target Item to Target struct
    Url* cwd = context ? (Url*)context->cwd : NULL;
    Target* target = item_to_target(target_item.item, cwd);
    if (!target) {
        log_error("fn_input2: failed to convert item to target");
        return ri_ok(ItemNull);
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
    else if (type_id == LMD_TYPE_MAP || type_id == LMD_TYPE_OBJECT) {
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
        return ri_ok(ItemNull);  // todo: push error
    }

    // Check if context is properly initialized
    if (!context) {
        log_debug("Error: context is NULL in fn_input");
        target_free(target);
        return ri_ok(ItemNull);
    }

    log_debug("input type: %s, flavor: %s", type_str ? type_str->chars : "null", flavor_str ? flavor_str->chars : "null");

    // Use the new target-based input function
    Input *input = input_from_target(target, type_str, flavor_str);
    target_free(target);

    // todo: input should be cached in context
    return ri_ok((input && input->root.item) ? input->root : ItemNull);
}

// declared extern "C" to allow calling from C code (path.c)
extern "C" RetItem fn_input1(Item url) {
    return fn_input2(url, ItemNull);
}

// parse(str, format) - parse a string into Lambda data structures
// Unlike input() which reads from files/URLs, parse() works on in-memory strings.
// 2nd arg can be a format symbol ('json, 'yaml, etc.) or an options map like input().
extern "C" Input* input_from_source(const char* source, Url* url, String* type, String* flavor);

RetItem fn_parse2(Item str_item, Item type) {
    GUARD_ERROR_RI2(str_item, type);

    // first arg must be a string
    TypeId str_type = get_type_id(str_item);
    if (str_type != LMD_TYPE_STRING) {
        log_error("parse: 1st argument must be a string, got type: %s", get_type_name(str_type));
        return item_to_ri(ItemError);
    }
    String* str = str_item.get_string();
    if (!str || !str->chars) return ri_ok(ItemNull);

    // parse the 2nd argument (format symbol or options map) - same logic as fn_input2
    String* type_str = NULL;
    String* flavor_str = NULL;

    TypeId type_id = get_type_id(type);
    if (type_id == LMD_TYPE_NULL) {
        type_str = NULL;  // auto-detect
    }
    else if (type_id == LMD_TYPE_STRING || type_id == LMD_TYPE_SYMBOL) {
        type_str = fn_string(type);
    }
    else if (type_id == LMD_TYPE_MAP || type_id == LMD_TYPE_OBJECT) {
        Map* options_map = type.map;
        bool is_found;

        // extract 'type' from map
        Item input_type = _map_get((TypeMap*)options_map->type, options_map->data, "type", &is_found);
        if (is_found && input_type.item && input_type._type_id != LMD_TYPE_NULL) {
            TypeId type_value_type = get_type_id(input_type);
            if (type_value_type == LMD_TYPE_STRING || type_value_type == LMD_TYPE_SYMBOL) {
                type_str = fn_string(input_type);
            }
        }

        // extract 'flavor' from map
        Item input_flavor = _map_get((TypeMap*)options_map->type, options_map->data, "flavor", &is_found);
        if (is_found && input_flavor.item && input_flavor._type_id != LMD_TYPE_NULL) {
            TypeId flavor_value_type = get_type_id(input_flavor);
            if (flavor_value_type == LMD_TYPE_STRING || flavor_value_type == LMD_TYPE_SYMBOL) {
                flavor_str = fn_string(input_flavor);
            }
        }
    }
    else {
        log_error("parse: 2nd argument must be a format symbol or options map, got type: %s", get_type_name(type_id));
        return item_to_ri(ItemError);
    }

    // create a dummy URL for the parser infrastructure (no actual file)
    Url* dummy_url = url_parse("parse://inline");

    log_debug("fn_parse2: type=%s, flavor=%s", type_str ? type_str->chars : "auto", flavor_str ? flavor_str->chars : "null");

    Input* input = input_from_source(str->chars, dummy_url, type_str, flavor_str);
    return ri_ok((input && input->root.item) ? input->root : ItemNull);
}

RetItem fn_parse1(Item str_item) {
    return fn_parse2(str_item, ItemNull);
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
        result->is_ascii = 1;  // datetime format strings are always ASCII
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
    else if (type_id == LMD_TYPE_MAP || type_id == LMD_TYPE_OBJECT) {
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
         // re-allocate as GC-tracked string (format_data uses pool directly, no GCHeader)
         result = heap_strcpy(result->chars, result->len);
    }
    return result;
}

String* fn_format1(Item item) {
    return fn_format2(item, ItemNull);
}

// generic field access function for any type
Item fn_index(Item item, Item index_item) {
    // Quick null/error guard: indexing null returns null to propagate
    // null safely through algorithms. Operations like null[field]
    // should not crash or return a wrong result.
    TypeId item_type = get_type_id(item);
    if (item_type == LMD_TYPE_NULL || item_type == LMD_TYPE_ERROR) {
        return ItemNull;
    }

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
    default: {
        // check for type value (pointer-based, so _type_id is 0)
        TypeId index_type = get_type_id(index_item);
        if (index_type == LMD_TYPE_TYPE) {
            return fn_child_query(item, index_item);
        }
        // range index: s[i to j] → slice(item, start, end+1)
        if (index_type == LMD_TYPE_RANGE) {
            Range* rng = index_item.range;
            if (rng) {
                Item start_it = {.item = i2it(rng->start)};
                Item end_it = {.item = i2it(rng->end + 1)}; // range end is inclusive, slice end is exclusive
                return fn_slice(item, start_it, end_it);
            }
            return ItemNull;
        }
        // for VMap, support arbitrary key types (int, float, etc.)
        TypeId item_type = get_type_id(item);
        if (item_type == LMD_TYPE_VMAP) {
            VMap* vm = item.vmap;
            if (vm && vm->vtable) return vm->vtable->get(vm->data, index_item);
            return ItemNull;
        }
        log_debug("invalid index type %d", index_item._type_id);
        return ItemNull;
    }
    }

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
                    if (strcmp(k, "parent") == 0) {
                        // return the parent path (go up one directory level)
                        if (path->parent && path->parent->parent) {
                            // path->parent is the next segment up in the linked list
                            return {.path = path->parent};
                        }
                        return ItemNull;
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
    case LMD_TYPE_OBJECT: {
        Object* obj = item.object;
        // First try field access
        bool is_found = false;
        char* key_str = NULL;
        if (key._type_id == LMD_TYPE_STRING || key._type_id == LMD_TYPE_SYMBOL) {
            key_str = (char*)key.get_chars();
        }
        if (key_str) {
            Item field_val = _map_get((TypeMap*)obj->type, obj->data, key_str, &is_found);
            if (is_found) return field_val;
        }
        // Field not found — check method table
        if (key_str) {
            TypeObject* obj_type = (TypeObject*)obj->type;
            TypeMethod* method = obj_type->methods;
            while (method) {
                if (strcmp(method->name->str, key_str) == 0 && method->fn) {
                    // Create a bound method: closure where closure_env = boxed self Item
                    // fn_call will pass closure_env as first arg (the _self parameter)
                    Function* bound = to_closure_named(
                        method->fn->ptr,
                        method->fn->arity,
                        (void*)(uintptr_t)item.item,  // boxed self as closure_env
                        method->fn->name);
                    return {.item = (uint64_t)(uintptr_t)bound};
                }
                method = method->next;
            }
            // Walk inheritance chain
            TypeObject* base = obj_type->base;
            while (base) {
                TypeMethod* bmethod = base->methods;
                while (bmethod) {
                    if (strcmp(bmethod->name->str, key_str) == 0 && bmethod->fn) {
                        Function* bound = to_closure_named(
                            bmethod->fn->ptr,
                            bmethod->fn->arity,
                            (void*)(uintptr_t)item.item,
                            bmethod->fn->name);
                        return {.item = (uint64_t)(uintptr_t)bound};
                    }
                    bmethod = bmethod->next;
                }
                base = base->base;
            }
        }
        return ItemNull;
    }
    case LMD_TYPE_VMAP: {
        VMap *vm = item.vmap;
        if (!vm || !vm->vtable) return ItemNull;
        return vm->vtable->get(vm->data, key);
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
    case LMD_TYPE_OBJECT: {
        size = 0;
        break;
    }
    case LMD_TYPE_VMAP: {
        VMap* vm = item.vmap;
        if (vm && vm->vtable) size = vm->vtable->count(vm->data);
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
        if (!chars) { size = 0; break; }
        String* str = item.get_string();
        size = str->is_ascii ? (int64_t)str->len : (int64_t)str_utf8_count(chars, str->len);
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

// Native len variants — type-specialized, avoid Item type switch overhead
// Used when compile-time type is known.

extern "C" int64_t fn_len_l(List* list) {
    if (!list) return 0;
    return list->length;
}

extern "C" int64_t fn_len_a(Array* arr) {
    if (!arr) return 0;
    return arr->length;
}

extern "C" int64_t fn_len_s(String* str) {
    if (!str) return 0;
    return str->is_ascii ? (int64_t)str->len : (int64_t)str_utf8_count(str->chars, str->len);
}

extern "C" int64_t fn_len_e(Element* elmt) {
    if (!elmt) return 0;
    return elmt->length;
}

// substring system function - extracts a substring from start to end (exclusive)
Item fn_substring(Item str_item, Item start_item, Item end_item) {
    GUARD_ERROR3(str_item, start_item, end_item);
    TypeId str_tid = get_type_id(str_item);
    if (str_tid != LMD_TYPE_STRING && str_tid != LMD_TYPE_SYMBOL) {
        log_debug("fn_substring: first argument must be a string or symbol");
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

    // use type-aware accessors: String and Symbol have different struct layouts
    // String: {len, is_ascii, chars[]}, Symbol: {len, ns*, chars[]}
    const char* chars = str_item.get_chars();
    uint32_t str_len = str_item.get_len();
    if (!chars || str_len == 0) {
        return str_item; // return empty string/symbol
    }

    // helper: return result as string or symbol depending on input type
    bool is_symbol = (str_tid == LMD_TYPE_SYMBOL);

    // is_ascii only exists on String, not Symbol; symbols use UTF-8 path
    bool is_ascii = false;
    if (!is_symbol) {
        is_ascii = str_item.get_string()->is_ascii;
    }

    int64_t start = it2l(start_item);
    int64_t end = it2l(end_item);

    // ASCII fast path: char index == byte index
    if (is_ascii) {
        int64_t char_len = (int64_t)str_len;
        if (start < 0) start = char_len + start;
        if (end < 0) end = char_len + end;
        if (start < 0) start = 0;
        if (end > char_len) end = char_len;
        if (start >= end) {
            if (is_symbol) return {.item = y2it(heap_create_symbol("", 0))};
            String* empty = (String *)heap_alloc(sizeof(String) + 1, LMD_TYPE_STRING);
            empty->len = 0;
            empty->is_ascii = 1;
            empty->chars[0] = '\0';
            return {.item = s2it(empty)};
        }
        long result_len = (long)(end - start);
        if (is_symbol) return {.item = y2it(heap_create_symbol(chars + start, result_len))};
        String* result = (String *)heap_alloc(sizeof(String) + result_len + 1, LMD_TYPE_STRING);
        result->len = result_len;
        result->is_ascii = 1;
        memcpy(result->chars, chars + start, result_len);
        result->chars[result_len] = '\0';
        return {.item = s2it(result)};
    }

    // handle negative indices (count from end)
    int64_t char_len = (int64_t)str_utf8_count(chars, str_len);
    if (start < 0) start = char_len + start;
    if (end < 0) end = char_len + end;

    // clamp to valid range
    if (start < 0) start = 0;
    if (end > char_len) end = char_len;
    if (start >= end) {
        if (is_symbol) return {.item = y2it(heap_create_symbol("", 0))};
        String* empty = (String *)heap_alloc(sizeof(String) + 1, LMD_TYPE_STRING);
        empty->len = 0;
        empty->is_ascii = 1;
        empty->chars[0] = '\0';
        return {.item = s2it(empty)};
    }

    // convert char indices to byte indices
    long byte_start = (long)str_utf8_char_to_byte(chars, str_len, (size_t)start);
    long byte_end = (long)str_utf8_char_to_byte(chars, str_len, (size_t)end);

    if (byte_start >= str_len || byte_end < 0) {
        if (is_symbol) return {.item = y2it(heap_create_symbol("", 0))};
        String* empty = (String *)heap_alloc(sizeof(String) + 1, LMD_TYPE_STRING);
        empty->len = 0;
        empty->is_ascii = 1;
        empty->chars[0] = '\0';
        return {.item = s2it(empty)};
    }

    long result_len = byte_end - byte_start;
    if (is_symbol) return {.item = y2it(heap_create_symbol(chars + byte_start, result_len))};
    String* result = (String *)heap_alloc(sizeof(String) + result_len + 1, LMD_TYPE_STRING);
    result->len = result_len;
    result->is_ascii = 0;  // UTF-8 path — not necessarily ASCII
    memcpy(result->chars, chars + byte_start, result_len);
    result->chars[result_len] = '\0';

    return {.item = s2it(result)};
}

// contains system function - checks if a string contains a substring
Bool fn_contains(Item str_item, Item substr_item) {
    GUARD_BOOL_ERROR2(str_item, substr_item);
    // null doesn't contain anything, and nothing contains null
    if (get_type_id(str_item) == LMD_TYPE_NULL || get_type_id(substr_item) == LMD_TYPE_NULL) {
        return BOOL_FALSE;
    }
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

    // null string doesn't start with anything
    if (str_type == LMD_TYPE_NULL) {
        return BOOL_FALSE;
    }

    // null prefix matches any string (like empty prefix)
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

    // null string doesn't end with anything
    if (str_type == LMD_TYPE_NULL) {
        return BOOL_FALSE;
    }

    // null suffix matches any string (like empty suffix)
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
    bool is_ascii = str_item.get_string()->is_ascii;
    for (size_t i = 0; i <= str_len - sub_len; i++) {
        if (memcmp(str_chars + i, sub_chars, sub_len) == 0) {
            // convert byte offset to character offset
            int64_t char_index = is_ascii ? (int64_t)i : (int64_t)str_utf8_count(str_chars, i);
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
        String* str = str_item.get_string();
        int64_t char_len = str->is_ascii ? (int64_t)str->len : (int64_t)str_utf8_count(str_chars, str_len);
        return char_len;
    }

    if (str_len < sub_len) {
        return -1;
    }

    // search from end to beginning
    bool is_ascii = str_item.get_string()->is_ascii;
    for (size_t i = str_len - sub_len + 1; i > 0; i--) {
        size_t pos = i - 1;
        if (memcmp(str_chars + pos, sub_chars, sub_len) == 0) {
            // convert byte offset to character offset
            int64_t char_index = is_ascii ? (int64_t)pos : (int64_t)str_utf8_count(str_chars, pos);
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

    // null trims to empty string
    if (str_type == LMD_TYPE_NULL) return ItemNull;

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
        // empty result normalized to null
        return ItemNull;
    }

    size_t result_len = end - start;
    if (str_type == LMD_TYPE_SYMBOL) {
        return {.item = y2it(heap_create_symbol(chars + start, result_len))};
    }

    String* result = (String *)heap_alloc(sizeof(String) + result_len + 1, LMD_TYPE_STRING);
    result->len = result_len;
    result->is_ascii = str_is_ascii(chars + start, result_len) ? 1 : 0;
    memcpy(result->chars, chars + start, result_len);
    result->chars[result_len] = '\0';
    return {.item = s2it(result)};
}

// trim_start(str) - remove leading whitespace
Item fn_trim_start(Item str_item) {
    GUARD_ERROR1(str_item);
    TypeId str_type = get_type_id(str_item);

    // null trims to null
    if (str_type == LMD_TYPE_NULL) return ItemNull;

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
    if (result_len == 0) {
        return ItemNull;
    }
    if (str_type == LMD_TYPE_SYMBOL) {
        return {.item = y2it(heap_create_symbol(chars + start, result_len))};
    }

    String* result = (String *)heap_alloc(sizeof(String) + result_len + 1, LMD_TYPE_STRING);
    result->len = result_len;
    result->is_ascii = str_is_ascii(chars + start, result_len) ? 1 : 0;
    memcpy(result->chars, chars + start, result_len);
    result->chars[result_len] = '\0';
    return {.item = s2it(result)};
}

// trim_end(str) - remove trailing whitespace
Item fn_trim_end(Item str_item) {
    GUARD_ERROR1(str_item);
    TypeId str_type = get_type_id(str_item);

    // null trims to null
    if (str_type == LMD_TYPE_NULL) return ItemNull;

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
        // empty result normalized to null
        return ItemNull;
    }

    if (str_type == LMD_TYPE_SYMBOL) {
        return {.item = y2it(heap_create_symbol(chars, end))};
    }

    String* result = (String *)heap_alloc(sizeof(String) + end + 1, LMD_TYPE_STRING);
    result->len = end;
    result->is_ascii = str_is_ascii(chars, end) ? 1 : 0;
    memcpy(result->chars, chars, end);
    result->chars[end] = '\0';
    return {.item = s2it(result)};
}

// lower(str) - convert string to lowercase (ASCII only for now)
Item fn_lower(Item str_item) {
    GUARD_ERROR1(str_item);
    TypeId str_type = get_type_id(str_item);

    // null lowercased is null
    if (str_type == LMD_TYPE_NULL) return ItemNull;

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
    result->is_ascii = str_item.get_string()->is_ascii;  // case conversion preserves ASCII status
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

    // null uppercased is null
    if (str_type == LMD_TYPE_NULL) return ItemNull;

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
    result->is_ascii = str_item.get_string()->is_ascii;  // case conversion preserves ASCII status
    for (uint32_t i = 0; i < len; i++) {
        char c = chars[i];
        result->chars[i] = (c >= 'a' && c <= 'z') ? (c - 32) : c;
    }
    result->chars[len] = '\0';
    return {.item = s2it(result)};
}

// url_resolve(base, relative) - resolve a relative URL against a base URL
Item fn_url_resolve(Item base_item, Item relative_item) {
    GUARD_ERROR2(base_item, relative_item);
    TypeId base_type = get_type_id(base_item);
    TypeId rel_type = get_type_id(relative_item);

    if ((base_type != LMD_TYPE_STRING && base_type != LMD_TYPE_SYMBOL) ||
        (rel_type != LMD_TYPE_STRING && rel_type != LMD_TYPE_SYMBOL)) {
        log_debug("fn_url_resolve: arguments must be strings");
        return ItemError;
    }

    const char* base_chars = base_item.get_chars();
    const char* rel_chars = relative_item.get_chars();
    if (!base_chars || !rel_chars) {
        return ItemNull;
    }

    // parse the base URL
    Url* base_url = url_parse(base_chars);
    if (!base_url || !base_url->is_valid) {
        log_debug("fn_url_resolve: invalid base URL '%s'", base_chars);
        if (base_url) url_destroy(base_url);
        return ItemNull;
    }

    // resolve the relative URL against the base
    Url* resolved = url_resolve_relative(rel_chars, base_url);
    if (!resolved || !resolved->is_valid) {
        log_debug("fn_url_resolve: failed to resolve '%s' against '%s'", rel_chars, base_chars);
        if (resolved) url_destroy(resolved);
        url_destroy(base_url);
        return ItemNull;
    }

    // get the href string from the resolved URL
    const char* href = url_get_href(resolved);
    if (!href) {
        url_destroy(resolved);
        url_destroy(base_url);
        return ItemNull;
    }

    uint32_t len = (uint32_t)strlen(href);
    String* result = (String*)heap_alloc(sizeof(String) + len + 1, LMD_TYPE_STRING);
    result->len = len;
    result->is_ascii = 1;  // URLs are ASCII
    memcpy(result->chars, href, len);
    result->chars[len] = '\0';

    url_destroy(resolved);
    url_destroy(base_url);
    return {.item = s2it(result)};
}

// split(str, sep) - split string by separator, returns list of strings
Item fn_split(Item str_item, Item sep_item) {
    GUARD_ERROR1(str_item);
    TypeId str_type = get_type_id(str_item);
    TypeId sep_type = get_type_id(sep_item);

    // null string splits to empty list
    if (str_type == LMD_TYPE_NULL) return {.list = list()};

    // null separator means split on whitespace (Python convention)
    bool null_sep = (sep_type == LMD_TYPE_NULL);

    // pattern-based split: split(str, pattern)
    if (sep_type == LMD_TYPE_TYPE) {
        Type* type = (Type*)(sep_item.item & 0x00FFFFFFFFFFFFFF);
        if (type && type->kind == TYPE_KIND_PATTERN) {
            if (str_type != LMD_TYPE_STRING && str_type != LMD_TYPE_SYMBOL) {
                log_debug("fn_split: first argument must be a string for pattern split");
                return ItemError;
            }
            TypePattern* pattern = (TypePattern*)type;
            const char* str_chars = str_item.get_chars();
            uint32_t str_len = str_item.get_len();
            return {.list = pattern_split(pattern, str_chars, str_len, false)};
        }
    }

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

    if (null_sep) {
        // null separator: split on whitespace (like Python str.split(None))
        // strips leading/trailing whitespace, splits on runs of whitespace
        const char* p = str_chars;
        const char* end = str_chars + str_len;
        while (p < end) {
            // skip whitespace
            while (p < end && (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r' || *p == '\f' || *p == '\v')) {
                p++;
            }
            if (p >= end) break;
            // find end of word
            const char* word_start = p;
            while (p < end && *p != ' ' && *p != '\t' && *p != '\n' && *p != '\r' && *p != '\f' && *p != '\v') {
                p++;
            }
            size_t word_len = p - word_start;
            String* part = (String *)heap_alloc(sizeof(String) + word_len + 1, LMD_TYPE_STRING);
            part->len = word_len;
            part->is_ascii = str_is_ascii(word_start, word_len) ? 1 : 0;
            memcpy(part->chars, word_start, word_len);
            part->chars[word_len] = '\0';
            list_push(result, {.item = s2it(part)});
        }
        if (context) { context->disable_string_merging = saved_merging; }
        return {.list = result};
    }

    if (!sep_chars || sep_len == 0) {
        // empty string separator: split into individual characters
        const char* p = str_chars;
        const char* end = str_chars + str_len;
        while (p < end) {
            int char_len = (int)str_utf8_char_len((unsigned char)*p);
            if (char_len <= 0) char_len = 1;  // fallback for invalid UTF-8

            String* part = (String *)heap_alloc(sizeof(String) + char_len + 1, LMD_TYPE_STRING);
            part->len = char_len;
            part->is_ascii = (char_len == 1 && (unsigned char)*p < 128) ? 1 : 0;
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
            part->is_ascii = str_is_ascii(start, part_len) ? 1 : 0;
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
    part->is_ascii = str_is_ascii(start, part_len) ? 1 : 0;
    memcpy(part->chars, start, part_len);
    part->chars[part_len] = '\0';

    list_push(result, {.item = s2it(part)});

    if (context) { context->disable_string_merging = saved_merging; }
    return {.list = result};
}

// split(str, sep, keep_delim) - 3-arg version with keep_delim boolean
Item fn_split3(Item str_item, Item sep_item, Item keep_item) {
    GUARD_ERROR1(str_item);
    TypeId str_type = get_type_id(str_item);
    TypeId sep_type = get_type_id(sep_item);
    TypeId keep_type = get_type_id(keep_item);

    bool keep_delim = (keep_type == LMD_TYPE_BOOL && it2b(keep_item));

    // null string splits to empty list
    if (str_type == LMD_TYPE_NULL) return {.list = list()};

    // pattern-based split with keep_delim
    if (sep_type == LMD_TYPE_TYPE) {
        Type* type = (Type*)(sep_item.item & 0x00FFFFFFFFFFFFFF);
        if (type && type->kind == TYPE_KIND_PATTERN) {
            if (str_type != LMD_TYPE_STRING && str_type != LMD_TYPE_SYMBOL) {
                log_debug("fn_split3: first argument must be a string for pattern split");
                return ItemError;
            }
            TypePattern* pattern = (TypePattern*)type;
            const char* str_chars = str_item.get_chars();
            uint32_t str_len = str_item.get_len();
            return {.list = pattern_split(pattern, str_chars, str_len, keep_delim)};
        }
    }

    // string-based split with keep_delim
    if (!keep_delim) {
        // no keep_delim, delegate to 2-arg version
        return fn_split(str_item, sep_item);
    }

    // keep_delim string split
    if ((str_type != LMD_TYPE_STRING && str_type != LMD_TYPE_SYMBOL) ||
        (sep_type != LMD_TYPE_STRING && sep_type != LMD_TYPE_SYMBOL)) {
        log_debug("fn_split3: arguments must be strings or symbols");
        return ItemError;
    }

    const char* str_chars = str_item.get_chars();
    uint32_t str_len = str_item.get_len();
    const char* sep_chars = sep_item.get_chars();
    uint32_t sep_len = sep_item.get_len();

    bool saved_merging = false;
    if (context) {
        saved_merging = context->disable_string_merging;
        context->disable_string_merging = true;
    }

    List* result = list();
    if (!str_chars || str_len == 0 || !sep_chars || sep_len == 0) {
        if (context) { context->disable_string_merging = saved_merging; }
        return {.list = result};
    }

    const char* start = str_chars;
    const char* end = str_chars + str_len;
    const char* p = start;

    while (p <= end - sep_len) {
        if (memcmp(p, sep_chars, sep_len) == 0) {
            // push part before separator
            size_t part_len = p - start;
            String* part = (String*)heap_alloc(sizeof(String) + part_len + 1, LMD_TYPE_STRING);
            part->len = part_len;
            part->is_ascii = str_is_ascii(start, part_len) ? 1 : 0;
            memcpy(part->chars, start, part_len);
            part->chars[part_len] = '\0';
            list_push(result, {.item = s2it(part)});

            // push the delimiter
            String* delim = (String*)heap_alloc(sizeof(String) + sep_len + 1, LMD_TYPE_STRING);
            delim->len = sep_len;
            delim->is_ascii = str_is_ascii(sep_chars, sep_len) ? 1 : 0;
            memcpy(delim->chars, sep_chars, sep_len);
            delim->chars[sep_len] = '\0';
            list_push(result, {.item = s2it(delim)});

            p += sep_len;
            start = p;
        } else {
            p++;
        }
    }

    // add the last part
    size_t part_len = end - start;
    String* part = (String*)heap_alloc(sizeof(String) + part_len + 1, LMD_TYPE_STRING);
    part->len = part_len;
    part->is_ascii = str_is_ascii(start, part_len) ? 1 : 0;
    memcpy(part->chars, start, part_len);
    part->chars[part_len] = '\0';
    list_push(result, {.item = s2it(part)});

    if (context) { context->disable_string_merging = saved_merging; }
    return {.list = result};
}

// ord(str) - return Unicode code point of first character
int64_t fn_ord(Item str_item) {
    TypeId type = get_type_id(str_item);

    if (type != LMD_TYPE_STRING && type != LMD_TYPE_SYMBOL) {
        log_debug("fn_ord: argument must be a string or symbol, got type %d", type);
        return 0;
    }

    const char* chars = str_item.get_chars();
    uint32_t byte_len = str_item.get_len();

    if (!chars || byte_len == 0) {
        return 0;
    }

    uint32_t codepoint = 0;
    int decoded = str_utf8_decode(chars, byte_len, &codepoint);
    if (decoded <= 0) {
        return 0; // invalid UTF-8
    }
    return (int64_t)codepoint;
}

// chr(int) - return 1-char string from Unicode code point
Item fn_chr(Item cp_item) {
    GUARD_ERROR1(cp_item);
    TypeId type = get_type_id(cp_item);

    // return empty string for null/invalid input
    if (type == LMD_TYPE_NULL) {
        String* empty = heap_strcpy("", 0);
        return {.item = s2it(empty)};
    }

    int64_t codepoint = it2l(cp_item);
    if (codepoint < 0 || codepoint > 0x10FFFF) {
        log_debug("fn_chr: code point %lld out of range (0..0x10FFFF)", (long long)codepoint);
        String* empty = heap_strcpy("", 0);
        return {.item = s2it(empty)};
    }

    char buf[4];
    size_t len = str_utf8_encode((uint32_t)codepoint, buf, sizeof(buf));
    if (len == 0) {
        String* empty = heap_strcpy("", 0);
        return {.item = s2it(empty)};  // invalid code point (e.g., surrogate)
    }

    String* result = heap_strcpy(buf, (int)len);
    return {.item = s2it(result)};
}

// join(strs, sep) - join list of strings with separator
Item fn_join2(Item list_item, Item sep_item) {
    GUARD_ERROR2(list_item, sep_item);
    TypeId list_type = get_type_id(list_item);
    TypeId sep_type = get_type_id(sep_item);

    // null list joins to null
    if (list_type == LMD_TYPE_NULL) return ItemNull;

    if (list_type != LMD_TYPE_LIST && list_type != LMD_TYPE_ARRAY) {
        log_debug("fn_join2: first argument must be a list or array");
        return ItemError;
    }

    // allow null/empty separator - treat as empty string
    const char* sep_chars = nullptr;
    size_t sep_len = 0;

    if (sep_type == LMD_TYPE_STRING || sep_type == LMD_TYPE_SYMBOL) {
        sep_chars = sep_item.get_chars();
        sep_len = sep_chars ? sep_item.get_len() : 0;
    } else if (sep_type != LMD_TYPE_NULL) {
        log_debug("fn_join2: separator must be a string, symbol, or null");
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
    result->is_ascii = 0;  // safe default for join

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

    // null string has nothing to replace
    if (str_type == LMD_TYPE_NULL) return ItemNull;
    // null search pattern means nothing to find — return string unchanged
    if (old_type == LMD_TYPE_NULL) return str_item;
    // null replacement — treat as empty string (delete matches)
    // note: transpiler converts "" to ITEM_NULL, so we must handle this without calling get_chars on null
    bool new_is_null = (new_type == LMD_TYPE_NULL);
    if (new_is_null) new_type = LMD_TYPE_STRING;

    // pattern-based replacement: replace(str, pattern, repl_str)
    if (old_type == LMD_TYPE_TYPE) {
        Type* type = (Type*)(old_item.item & 0x00FFFFFFFFFFFFFF);
        if (type && type->kind == TYPE_KIND_PATTERN) {
            if (str_type != LMD_TYPE_STRING && str_type != LMD_TYPE_SYMBOL) {
                log_debug("fn_replace: first argument must be a string for pattern replace");
                return ItemError;
            }
            if (new_type != LMD_TYPE_STRING && new_type != LMD_TYPE_SYMBOL) {
                log_debug("fn_replace: third argument must be a string for pattern replace");
                return ItemError;
            }
            TypePattern* pattern = (TypePattern*)type;
            const char* str_chars = str_item.get_chars();
            uint32_t str_len = str_item.get_len();
            const char* repl_chars = new_is_null ? "" : new_item.get_chars();
            uint32_t repl_len = new_is_null ? 0 : new_item.get_len();

            if (!str_chars || str_len == 0) return str_item;

            String* result = pattern_replace_all(pattern, str_chars, str_len,
                                                  repl_chars ? repl_chars : "", repl_chars ? repl_len : 0);
            if (!result) return str_item;
            return {.item = s2it(result)};
        }
    }

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
    const char* new_chars = new_is_null ? "" : new_item.get_chars();
    uint32_t new_len_val = new_is_null ? 0 : new_item.get_len();

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
    result->is_ascii = 0;  // safe default for replace

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

// find(str, pattern_or_string) -> [{value, index}, ...]
// Finds all non-overlapping matches in the source string.
// Second argument can be a string literal or a compiled pattern.
Item fn_find2(Item source_item, Item pattern_item) {
    GUARD_ERROR2(source_item, pattern_item);
    TypeId source_type = get_type_id(source_item);
    TypeId pattern_type = get_type_id(pattern_item);

    // null source -> empty list
    if (source_type == LMD_TYPE_NULL) return {.list = list()};

    if (source_type != LMD_TYPE_STRING && source_type != LMD_TYPE_SYMBOL) {
        log_debug("fn_find: first argument must be a string or symbol");
        return ItemError;
    }

    const char* str_chars = source_item.get_chars();
    uint32_t str_len = source_item.get_len();

    if (!str_chars || str_len == 0) return {.list = list()};

    // pattern argument: check if it's a TypePattern
    if (pattern_type == LMD_TYPE_TYPE) {
        Type* type = (Type*)(pattern_item.item & 0x00FFFFFFFFFFFFFF);
        if (type && type->kind == TYPE_KIND_PATTERN) {
            TypePattern* pattern = (TypePattern*)type;
            return {.list = pattern_find_all(pattern, str_chars, str_len)};
        }
    }

    // plain string search
    if (pattern_type != LMD_TYPE_STRING && pattern_type != LMD_TYPE_SYMBOL) {
        log_debug("fn_find: second argument must be a string, symbol, or pattern");
        return ItemError;
    }

    const char* needle = pattern_item.get_chars();
    uint32_t needle_len = pattern_item.get_len();

    List* result = list();
    if (!needle || needle_len == 0) return {.list = result};

    // find all non-overlapping occurrences of the literal substring
    const char* p = str_chars;
    const char* end = str_chars + str_len;
    while (p <= end - needle_len) {
        if (memcmp(p, needle, needle_len) == 0) {
            int64_t index = (int64_t)(p - str_chars);
            Map* m = create_match_map_ext(p, needle_len, index);
            list_push(result, {.map = m});
            p += needle_len;
        } else {
            p++;
        }
    }

    return {.list = result};
}

// find(str, pattern, options) -> [{value, index}, ...]
// 3-arg version with options map (for future: {limit, ignore_case})
Item fn_find3(Item source_item, Item pattern_item, Item options_item) {
    // for now, ignore options and delegate to 2-arg version
    (void)options_item;
    return fn_find2(source_item, pattern_item);
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
// Returns the previous vargs pointer so it can be restored after the call
List* set_vargs(List* vargs) {
    List* prev = current_vargs;
    current_vargs = vargs;
    return prev;
}

// Restore previous variadic arguments after a variadic function completes
void restore_vargs(List* prev) {
    current_vargs = prev;
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

// ============================================================================
// COMPOUND ASSIGNMENT SUPPORT (procedural only)
// ============================================================================

// Convert a specialized array (ArrayInt, ArrayInt64, ArrayFloat) to a generic Array in place.
// The struct pointer stays the same — only the type_id and items buffer change.
// All specialized array types share the same struct layout (Container + items_ptr + length + extra + capacity),
// so this in-place conversion is safe.
static void convert_specialized_to_generic(Array* arr) {
    TypeId old_type = arr->type_id;
    int64_t len = arr->length;

    // need room for items + extras (float/int64 values stored at end of buffer)
    int64_t new_capacity = len * 2 + 4;
    if (new_capacity < 8) new_capacity = 8;

    Item* new_items = (Item*)heap_data_calloc(new_capacity * sizeof(Item));
    if (!new_items) {
        log_error("convert_specialized_to_generic: allocation failed");
        return;
    }

    if (old_type == LMD_TYPE_ARRAY_INT) {
        int64_t* old_items = ((ArrayInt*)arr)->items;
        // int56 values pack directly into Item — no extra area needed
        for (int64_t i = 0; i < len; i++) {
            new_items[i] = {.item = i2it(old_items[i])};
        }
        // old_items abandoned in data zone — reclaimed by GC
    } else if (old_type == LMD_TYPE_ARRAY_INT64) {
        int64_t* old_items = ((ArrayInt64*)arr)->items;
        // int64 values need to be stored in the extra area (end of buffer)
        int64_t extra_count = 0;
        for (int64_t i = 0; i < len; i++) {
            int64_t* slot = (int64_t*)(new_items + (new_capacity - extra_count - 1));
            *slot = old_items[i];
            new_items[i] = {.item = l2it(slot)};
            extra_count++;
        }
        arr->extra = extra_count;
        // old_items abandoned in data zone — reclaimed by GC
    } else if (old_type == LMD_TYPE_ARRAY_FLOAT) {
        double* old_items = ((ArrayFloat*)arr)->items;
        // double values need to be stored in the extra area (end of buffer)
        int64_t extra_count = 0;
        for (int64_t i = 0; i < len; i++) {
            double* slot = (double*)(new_items + (new_capacity - extra_count - 1));
            *slot = old_items[i];
            new_items[i] = {.item = d2it(slot)};
            extra_count++;
        }
        arr->extra = extra_count;
        // old_items abandoned in data zone — reclaimed by GC
    } else {
        // shouldn't happen — caller should only call for specialized types
        return;
    }

    arr->items = new_items;
    arr->capacity = new_capacity;
    arr->type_id = LMD_TYPE_ARRAY;
    log_debug("convert_specialized_to_generic: converted type %d to generic Array, len=%lld", old_type, len);
}

// array indexed assignment: arr[i] = val
// Handles Array, ArrayInt, ArrayInt64, ArrayFloat
void fn_array_set(Array* arr, int index, Item value) {
    if (!arr || ((uintptr_t)arr >> 56)) {
        static int _err_count = 0;
        _err_count++;
        if (_err_count <= 10) {
            log_error("fn_array_set: null or invalid array pointer (ptr=%p, idx=%d, val_type=%d, count=%d)",
                      (void*)arr, index, get_type_id(value), _err_count);
        }
        return;
    }
    TypeId arr_type = arr->type_id;

    // support negative indexing
    int64_t len = arr->length;
    if (index < 0) index = (int)len + index;
    if (index < 0 || index >= (int)len) {
        log_error("fn_array_set: index %d out of bounds (length %lld)", index, len);
        return;
    }

    switch (arr_type) {
    case LMD_TYPE_ARRAY: {
        // generic Array with Item* items — use internal array_set
        array_set(arr, index, value);
        break;
    }
    case LMD_TYPE_ARRAY_INT: {
        TypeId val_type = get_type_id(value);
        if (val_type == LMD_TYPE_INT) {
            // compatible: store int56 directly
            ArrayInt* ai = (ArrayInt*)arr;
            ai->items[index] = (int64_t)value.get_int56();
        } else if (val_type == LMD_TYPE_INT64) {
            // int64 might fit in int56 — try, else convert
            int64_t lval = value.get_int64();
            if (lval >= INT56_MIN && lval <= INT56_MAX) {
                ArrayInt* ai = (ArrayInt*)arr;
                ai->items[index] = lval;
            } else {
                convert_specialized_to_generic(arr);
                array_set(arr, index, value);
            }
        } else {
            // incompatible type — convert to generic array first
            convert_specialized_to_generic(arr);
            array_set(arr, index, value);
        }
        break;
    }
    case LMD_TYPE_ARRAY_INT64: {
        TypeId val_type = get_type_id(value);
        if (val_type == LMD_TYPE_INT64) {
            ArrayInt64* ai64 = (ArrayInt64*)arr;
            ai64->items[index] = value.get_int64();
        } else if (val_type == LMD_TYPE_INT) {
            // widen int56 to int64
            ArrayInt64* ai64 = (ArrayInt64*)arr;
            ai64->items[index] = (int64_t)value.get_int56();
        } else {
            convert_specialized_to_generic(arr);
            array_set(arr, index, value);
        }
        break;
    }
    case LMD_TYPE_ARRAY_FLOAT: {
        TypeId val_type = get_type_id(value);
        if (val_type == LMD_TYPE_FLOAT) {
            ArrayFloat* af = (ArrayFloat*)arr;
            af->items[index] = value.get_double();
        } else if (val_type == LMD_TYPE_INT) {
            // widen int to double
            ArrayFloat* af = (ArrayFloat*)arr;
            af->items[index] = (double)value.get_int56();
        } else if (val_type == LMD_TYPE_INT64) {
            // widen int64 to double (may lose precision)
            ArrayFloat* af = (ArrayFloat*)arr;
            af->items[index] = (double)value.get_int64();
        } else {
            convert_specialized_to_generic(arr);
            array_set(arr, index, value);
        }
        break;
    }
    case LMD_TYPE_LIST:
    case LMD_TYPE_ELEMENT: {
        // List and Element have Item* items — use array_set directly
        array_set(arr, index, value);
        break;
    }
    default:
        log_error("fn_array_set: unsupported array type %d", arr_type);
        break;
    }
}

// helper: decrement ref count of the value stored at a field pointer
static void map_field_decrement_ref(void* field_ptr, TypeId field_type) {
    switch (field_type) {
    case LMD_TYPE_STRING: case LMD_TYPE_SYMBOL: case LMD_TYPE_BINARY: {
        String* old_str = *(String**)field_ptr;
        break;
    }
    case LMD_TYPE_ARRAY: case LMD_TYPE_ARRAY_INT: case LMD_TYPE_ARRAY_INT64:
    case LMD_TYPE_ARRAY_FLOAT: case LMD_TYPE_RANGE: case LMD_TYPE_LIST:
    case LMD_TYPE_MAP: case LMD_TYPE_ELEMENT: case LMD_TYPE_OBJECT: {
        Container* old_c = *(Container**)field_ptr;
        break;
    }
    case LMD_TYPE_NULL: {
        // a container might have been stored via a prior transition
        void* old_ptr = *(void**)field_ptr;
        if (old_ptr) {
            Container* old_c = (Container*)old_ptr;
        }
        break;
    }
    default: break;
    }
}

// helper: store a value at a field pointer, incrementing ref count as needed
static void map_field_store(void* field_ptr, Item value, TypeId value_type) {
    switch (value_type) {
    case LMD_TYPE_NULL:  *(void**)field_ptr = NULL; break;
    case LMD_TYPE_UNDEFINED:  *(bool*)field_ptr = false; break;
    case LMD_TYPE_BOOL:  *(bool*)field_ptr = value.bool_val; break;
    case LMD_TYPE_INT:   *(int64_t*)field_ptr = value.get_int56(); break;
    case LMD_TYPE_INT64: *(int64_t*)field_ptr = value.get_int64(); break;
    case LMD_TYPE_FLOAT: *(double*)field_ptr = value.get_double(); break;
    case LMD_TYPE_DTIME: *(DateTime*)field_ptr = value.get_datetime(); break;
    case LMD_TYPE_STRING: case LMD_TYPE_SYMBOL: case LMD_TYPE_BINARY: {
        String* s = value.get_string();
        *(String**)field_ptr = s;
        break;
    }
    case LMD_TYPE_ARRAY: case LMD_TYPE_ARRAY_INT: case LMD_TYPE_ARRAY_INT64:
    case LMD_TYPE_ARRAY_FLOAT: case LMD_TYPE_RANGE: case LMD_TYPE_LIST:
    case LMD_TYPE_MAP: case LMD_TYPE_ELEMENT: case LMD_TYPE_OBJECT: {
        Container* c = value.container;
        *(Container**)field_ptr = c;
        break;
    }
    case LMD_TYPE_FUNC: case LMD_TYPE_VMAP: case LMD_TYPE_DECIMAL:
    case LMD_TYPE_TYPE: {
        // store as opaque pointer (low 56 bits)
        *(void**)field_ptr = (void*)(uintptr_t)(value.item & 0x00FFFFFFFFFFFFFF);
        break;
    }
    case LMD_TYPE_ERROR:
        // store error as null to prevent cascading corruption
        *(void**)field_ptr = NULL;
        break;
    default:
        log_error("map_field_store: unsupported type %d", value_type);
        break;
    }
}

// rebuild a map/element shape when a field's type changes
// creates new ShapeEntry chain + TypeMap/TypeElmt, allocates new data buffer, copies fields
// For markup containers (!is_heap), uses runtime pool instead of calloc/free to avoid
// corrupting input pool memory. The is_data_migrated flag tracks this transition.
static void map_rebuild_for_type_change(void** type_slot, void** data_slot, int* cap_slot,
                                        TypeId container_type_id,
                                        Container* container,
                                        ShapeEntry* changed_entry,
                                        TypeId new_value_type, Item new_value) {
    TypeMap* old_map_type = (TypeMap*)*type_slot;
    void* old_data = *data_slot;

    // count existing fields
    int field_count = 0;
    ShapeEntry* e = old_map_type->shape;
    while (e) { field_count++; e = e->next; }
    if (field_count <= 0 || field_count > 64) {
        log_error("map_rebuild: invalid field count %d", field_count);
        return;
    }

    // build new shape chain with updated type for the changed field
    ShapeEntry* first = NULL;
    ShapeEntry* prev = NULL;
    ShapeEntry* last = NULL;
    int64_t byte_offset = 0;

    e = old_map_type->shape;
    while (e) {
        TypeId ft = (e == changed_entry) ? new_value_type : e->type->type_id;

        // allocate ShapeEntry + embedded StrView
        ShapeEntry* ne = (ShapeEntry*)pool_calloc(context->pool,
            sizeof(ShapeEntry) + sizeof(StrView));
        if (!ne) {
            log_error("map_rebuild: ShapeEntry allocation failed");
            return;
        }
        StrView* nv = (StrView*)((char*)ne + sizeof(ShapeEntry));
        nv->str = e->name->str;
        nv->length = e->name->length;
        ne->name = nv;
        ne->type = type_info[ft].type;
        ne->byte_offset = byte_offset;
        ne->next = NULL;
        ne->ns = e->ns;

        if (!first) first = ne;
        if (prev) prev->next = ne;
        last = ne;
        prev = ne;
        byte_offset += type_info[ft].byte_size;
        e = e->next;
    }
    int64_t new_byte_size = byte_offset;

    // allocate new data buffer
    // For heap containers: calloc (consistent with map_fill, freed by free_container)
    // For markup containers: pool_calloc from runtime pool (data migrated from input pool)
    bool use_pool = !container->is_heap;
    void* new_data;
    if (use_pool) {
        new_data = pool_calloc(context->pool, new_byte_size > 0 ? new_byte_size : 1);
    } else {
        new_data = heap_data_calloc(new_byte_size > 0 ? new_byte_size : 1);
    }
    if (!new_data) {
        log_error("map_rebuild: data allocation failed");
        return;
    }

    // Re-read old_data after allocation: GC may have fired during
    // heap_data_calloc, compacting *data_slot from nursery to tenured.
    // The local old_data would then point to freed nursery memory.
    old_data = *data_slot;

    // copy field values from old buffer to new buffer
    ShapeEntry* old_e = old_map_type->shape;
    ShapeEntry* new_e = first;
    while (old_e && new_e) {
        void* old_field = (char*)old_data + old_e->byte_offset;
        void* new_field = (char*)new_data + new_e->byte_offset;

        if (old_e == changed_entry) {
            // decrement old value's ref count
            map_field_decrement_ref(old_field, old_e->type->type_id);
            // store the new value (with ref count increment)
            map_field_store(new_field, new_value, new_value_type);
        } else {
            // unchanged field — copy bytes (ref count unchanged, same pointers)
            int sz = type_info[old_e->type->type_id].byte_size;
            memcpy(new_field, old_field, sz);
        }
        old_e = old_e->next;
        new_e = new_e->next;
    }

    // create new TypeMap or TypeElmt (never mutate shared types)
    ArrayList* tl = (ArrayList*)context->type_list;

    if (container_type_id == LMD_TYPE_ELEMENT) {
        TypeElmt* old_et = (TypeElmt*)old_map_type;
        TypeElmt* new_et = (TypeElmt*)alloc_type(context->pool,
            LMD_TYPE_ELEMENT, sizeof(TypeElmt));
        new_et->shape = first;
        new_et->last = last;
        new_et->length = field_count;
        new_et->byte_size = new_byte_size;
        new_et->name = old_et->name;
        new_et->content_length = old_et->content_length;
        new_et->ns = old_et->ns;
        new_et->type_index = tl->length;
        arraylist_append(tl, new_et);
        *type_slot = new_et;
    } else {
        TypeMap* new_mt = (TypeMap*)alloc_type(context->pool,
            LMD_TYPE_MAP, sizeof(TypeMap));
        new_mt->shape = first;
        new_mt->last = last;
        new_mt->length = field_count;
        new_mt->byte_size = new_byte_size;
        new_mt->type_index = tl->length;
        arraylist_append(tl, new_mt);
        *type_slot = new_mt;
    }

    // replace data
    *data_slot = new_data;
    *cap_slot = (int)new_byte_size;

    // free old data buffer with correct allocator
    if (old_data) {
        if (container->is_heap) {
            // old_data is in GC data zone — abandoned, reclaimed by GC
        } else if (container->is_data_migrated) {
            // previous mutation already migrated data to runtime pool
            pool_free(context->pool, old_data);
        }
        // else: old data is in input pool — don't free (managed by input lifecycle)
    }

    // mark data as migrated for future mutations
    if (use_pool) {
        container->is_data_migrated = 1;
    }

    log_debug("map_rebuild: type change complete, fields=%d, byte_size=%ld, migrated=%d",
              field_count, new_byte_size, container->is_data_migrated);
}

// map/element field assignment: obj.field = val
// Supports in-place update (same type), numeric coercion (INT↔FLOAT, INT↔INT64),
// and shape rebuild for type changes (e.g. int→string, null→container).
void fn_map_set(Item map_item, Item key, Item value) {
    TypeId map_type_id = get_type_id(map_item);

    // VMap: in-place mutation via vtable
    if (map_type_id == LMD_TYPE_VMAP) {
        VMap* vm = map_item.vmap;
        if (!vm || !vm->vtable) {
            log_error("fn_map_set: null vmap or vtable");
            return;
        }
        vm->vtable->set(vm->data, key, value);
        return;
    }

    // support both Map and Element (Element has map-like attributes)
    TypeMap* map_type = NULL;
    void** type_slot = NULL;
    void** data_slot = NULL;
    int* cap_slot = NULL;

    if (map_type_id == LMD_TYPE_MAP) {
        Map* mp = map_item.map;
        if (!mp) { log_error("fn_map_set: null map"); return; }
        type_slot = &mp->type;
        data_slot = &mp->data;
        cap_slot = &mp->data_cap;
    } else if (map_type_id == LMD_TYPE_OBJECT) {
        Object* obj = map_item.object;
        if (!obj) { log_error("fn_map_set: null object"); return; }
        type_slot = &obj->type;
        data_slot = &obj->data;
        cap_slot = &obj->data_cap;
    } else if (map_type_id == LMD_TYPE_ELEMENT) {
        Element* el = (Element*)map_item.container;
        if (!el) { log_error("fn_map_set: null element"); return; }
        type_slot = &el->type;
        data_slot = &el->data;
        cap_slot = &el->data_cap;
    } else {
        log_error("fn_map_set: not a map or element (type=%d)", map_type_id);
        return;
    }

    map_type = (TypeMap*)*type_slot;
    if (!map_type || !map_type->shape) {
        log_error("fn_map_set: no shape");
        return;
    }

    // get key as C string
    const char* key_cstr = NULL;
    TypeId key_type = get_type_id(key);
    if (key_type == LMD_TYPE_STRING) {
        String* s = key.get_string();
        key_cstr = s ? s->chars : NULL;
    } else if (key_type == LMD_TYPE_SYMBOL) {
        Symbol* sym = key.get_symbol();
        key_cstr = sym ? sym->chars : NULL;
    } else {
        log_error("fn_map_set: key must be string or symbol");
        return;
    }
    if (!key_cstr) {
        log_error("fn_map_set: null key string");
        return;
    }

    // find field in shape
    TypeId value_type = get_type_id(value);
    ShapeEntry* entry = map_type->shape;
    while (entry) {
        if (strcmp(entry->name->str, key_cstr) == 0) {
            TypeId field_type = entry->type->type_id;
            void* field_ptr = (char*)*data_slot + entry->byte_offset;

            if (field_type == value_type) {
                // same type — fast path: in-place update
                map_field_decrement_ref(field_ptr, field_type);
                map_field_store(field_ptr, value, value_type);
                return;
            }

            // FLOAT field + INT value → widen int to double (lossless, no reshape)
            if (field_type == LMD_TYPE_FLOAT && value_type == LMD_TYPE_INT) {
                *(double*)field_ptr = (double)value.get_int56();
                return;
            }

            // INT ↔ INT64 promotion — fast path (same byte size)
            if ((field_type == LMD_TYPE_INT && value_type == LMD_TYPE_INT64) ||
                (field_type == LMD_TYPE_INT64 && value_type == LMD_TYPE_INT)) {
                map_field_store(field_ptr, value, value_type);
                return;
            }

            // NULL → INT/FLOAT/STRING/FUNC fast path — same byte size (all 8 bytes)
            // Avoids map_rebuild_for_type_change which allocates from data zone and
            // can trigger GC compaction that corrupts field data at scale.
            // Safe for JS constructor objects (non-pooled shapes).
            if (field_type == LMD_TYPE_NULL) {
                int old_bsz = type_info[field_type].byte_size;
                int new_bsz = type_info[value_type].byte_size;
                if (old_bsz == new_bsz) {
                    map_field_store(field_ptr, value, value_type);
                    entry->type = type_info[value_type].type;
                    return;
                }
            }

            // Container/null type change — fast path (same byte size = sizeof(void*))
            // NULL↔MAP, NULL↔ARRAY, MAP↔ELEMENT, etc. all store pointers,
            // and _map_read_field handles null pointers for all container types.
            // No shape rebuild needed — just update the value in-place.
            {
                bool old_is_ptr = (field_type == LMD_TYPE_NULL || field_type == LMD_TYPE_MAP ||
                    field_type == LMD_TYPE_ELEMENT || field_type == LMD_TYPE_OBJECT ||
                    field_type == LMD_TYPE_ARRAY || field_type == LMD_TYPE_ARRAY_INT ||
                    field_type == LMD_TYPE_ARRAY_INT64 || field_type == LMD_TYPE_ARRAY_FLOAT ||
                    field_type == LMD_TYPE_LIST || field_type == LMD_TYPE_RANGE);
                bool new_is_ptr = (value_type == LMD_TYPE_NULL || value_type == LMD_TYPE_MAP ||
                    value_type == LMD_TYPE_ELEMENT || value_type == LMD_TYPE_OBJECT ||
                    value_type == LMD_TYPE_ARRAY || value_type == LMD_TYPE_ARRAY_INT ||
                    value_type == LMD_TYPE_ARRAY_INT64 || value_type == LMD_TYPE_ARRAY_FLOAT ||
                    value_type == LMD_TYPE_LIST || value_type == LMD_TYPE_RANGE);
                if (old_is_ptr && new_is_ptr) {
                    map_field_decrement_ref(field_ptr, field_type);
                    map_field_store(field_ptr, value, value_type);
                    // Update the ShapeEntry type so GC can properly trace
                    // container pointers stored in formerly-NULL fields.
                    // IMPORTANT: Don't downgrade to NULL when field was a container,
                    // because the ShapeEntry is SHARED across all instances of the class.
                    // If we set it to NULL, GC would skip tracing container pointers
                    // in other instances that still hold live arrays/maps/etc.
                    if (value_type != LMD_TYPE_NULL) {
                        entry->type = type_info[value_type].type;
                    }
                    return;
                }
            }

            // type change — rebuild shape with new field type
            log_debug("fn_map_set: type change for '%s' (%d → %d), rebuilding shape",
                      key_cstr, field_type, value_type);
            Container* cont = map_item.container;
            map_rebuild_for_type_change(type_slot, data_slot, cap_slot,
                                        map_type_id, cont, entry,
                                        value_type, value);
            return;
        }
        entry = entry->next;
    }
    log_error("fn_map_set: field '%s' not found", key_cstr);
}
