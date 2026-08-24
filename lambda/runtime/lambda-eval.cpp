#include "transpiler.hpp"
#include "lambda-number-types.hpp"
#include "lambda-number-runtime.hpp"
#include "heap_api.h"
#include "lambda/runtime/gc/gc_heap.h"
#include "../core/lambda-decimal.hpp"
#include "lambda-error.h"
#include "recovery_frame.h"
#include "type_contract.hpp"
#include "concurrency.h"
#include "hosted-call-dispatch.hpp"
#include "interp.hpp"
#include <limits.h>
#include "../../lib/log.h"
#include "../../lib/memtrack.h"
#include "../../lib/url.h"
#include "../../lib/checked_math.hpp"
#include "../../lib/file.h"
#include "../../lib/str.h"
#include "../core/utf_string.h"
#include "re2_wrapper.hpp"
#include <utf8proc.h>
#include <mpdecimal.h>  // needed for inline decimal operations

#include <stdarg.h>
#include <time.h>
#include <errno.h>  // for errno checking
#include <math.h>
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
#include "../validator/validator.hpp"
#include "../input/input.hpp"
#include "../input/html5/html5_parser.h"

extern __thread EvalContext* context;
extern "C" void cow_profile_note_mutable_value(void);
extern "C" void cow_profile_note_map_admit_clone(void);
extern "C" void cow_profile_note_map_admit_bytes(uint64_t bytes);

static Item publish_recovery_fault_item(Context* runtime_context,
                                        uint64_t raw_fault_item) {
    if (!runtime_context || raw_fault_item == ITEM_ERROR) return ItemError;
    Item fault_item{.item = raw_fault_item};
    LambdaError* fallback = it2err(fault_item);
    if (!fallback) return ItemError;
    if (context && (Context*)context == runtime_context) {
        if (context->last_error && context->last_error != fallback) {
            err_free(context->last_error);
        }
        context->last_error = fallback;
    }
    return fault_item;
}

extern "C" Item lambda_recovery_frame_fault_item(
        Context* runtime_context, LambdaRecoveryFrame* frame) {
    if (!frame || !frame->fault.active) return ItemError;
    return publish_recovery_fault_item(runtime_context,
        lambda_recovery_frame_static_fault_item(frame));
}

extern "C" Item lambda_recovery_publish_fault_item(
        Context* runtime_context, LambdaFaultReason reason,
        LambdaErrorCode prior_error_code) {
    return publish_recovery_fault_item(runtime_context,
        lambda_recovery_frame_static_fault_item_for(reason, prior_error_code));
}

extern "C" void lambda_recovery_publish_fault(
        Context* runtime_context, LambdaFaultReason reason,
        LambdaErrorCode prior_error_code) {
    (void)lambda_recovery_publish_fault_item(runtime_context, reason,
        prior_error_code);
}

Item vmap_get_by_item(VMap* vm, Item key);
extern "C" void vmap_set(Item vmap_item, Item key, Item value);

// create_match_map helper for find() (implemented in re2_wrapper.cpp)
extern "C" Map* create_match_map_ext(const char* match_str, size_t match_len, int64_t index);

// forward declaration of static error string (defined later in this file)
extern String& STR_ERROR;

// External path resolution function (implemented in path.c)
extern "C" Item path_resolve_for_iteration(Path* path);

extern Item _map_read_field(ShapeEntry* field, void* map_data);

// External path functions for path ++ operation
extern "C" Pool* eval_context_get_pool(void);
extern "C" Path* path_extend(Pool* pool, Path* base, const char* segment);
extern "C" Path* path_concat(Pool* pool, Path* base, Path* suffix);
extern "C" Path* path_qualify_default(Pool* pool, Path* path);
extern "C" PathScheme path_get_scheme(Path* path);
extern "C" bool path_is_absolute(Path* path);
extern "C" TypeMap* js_typemap_clone_for_mutation_pub(Item obj);
extern "C" TypeMap* js_typemap_transition_for_type(Item obj, ShapeEntry* entry,
    NameId operation_name_id, TypeId value_type);

// External typeset function

#define stack_alloc(size) alloca(size);

#define Malloc malloc
#define Realloc realloc


Item _map_get(TypeMap* map_type, void* map_data, const char *key, bool *is_found);

// =============================================================================
// Runtime Error Helper
// =============================================================================

static LambdaError* create_runtime_error(LambdaErrorCode code, const char* message) {
    if (!context) return NULL;
    SourceLocation loc = {0};
    if (context->current_file) loc.file = context->current_file;
    LambdaError* error = err_create(code, message, &loc);
    if (!error) {
        if (lambda_recovery_frame_raise_fault(LAMBDA_FAULT_OUT_OF_MEMORY, code)) return NULL;
        (void)lambda_recovery_publish_fault_item((Context*)context,
            LAMBDA_FAULT_OUT_OF_MEMORY, code);
    }
    return error;
}

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

    LambdaError* error = create_runtime_error(code, message);
    if (!error) return;

    // capture native stack trace via FP walking
    error->raw_stack_trace = err_capture_raw_stack_trace(context->debug_info, 32);

    // store in context
    if (context->last_error) {
        err_free(context->last_error);
    }
    context->last_error = error;

    log_error("runtime error [%d]: %s", code, message);
}

static void set_input_parse_error(const char* function_name, Input* input,
                                  String* type) {
    if (input && input->parse_error_message) {
        set_runtime_error(ERR_PARSE_ERROR, "%s: %s", function_name,
            input->parse_error_message);
    } else if (type) {
        set_runtime_error(ERR_PARSE_ERROR,
            "%s: parser failed for format '%s'", function_name, type->chars);
    } else {
        set_runtime_error(ERR_PARSE_ERROR,
            "%s: parser failed during format auto-detection", function_name);
    }
}

/**
 * Public API to set a runtime error from external modules (e.g., lambda-stack.cpp)
 * Does NOT capture stack trace since it may be called in low-stack situations.
 */
extern "C" void set_runtime_error_no_trace(LambdaErrorCode code, const char* message) {
    if (!context) return;

    LambdaError* error = create_runtime_error(code, message);
    if (!error) return;
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
        if (str) {
            msg = str->chars;
        }
    }
    set_runtime_error(ERR_USER_ERROR, "%s", msg);
    if (context && context->heap && context->heap->gc) {
        SourceLocation loc = {0};
        if (context->current_file) {
            loc.file = context->current_file;
        }
        LambdaError* error = err_create_heap(ERR_USER_ERROR, msg, &loc);
        if (error) {
            error->raw_stack_trace = err_capture_raw_stack_trace(context->debug_info, 32);
            return err2it(error);
        }
    }
    return ItemError;
}

Bool is_truthy(Item item) {
    TypeId type_id = get_type_id(item);
    switch (type_id) {
    case LMD_TYPE_NULL:
        return BOOL_FALSE;
    case LMD_TYPE_ERROR:
        return BOOL_FALSE;  // errors are falsy; use `is error` to test them
    case LMD_TYPE_BOOL:
        return item.bool_val ? BOOL_TRUE : BOOL_FALSE;
    case LMD_TYPE_FLOAT:
    case LMD_TYPE_FLOAT64: {
        // Lambda truthiness treats every number as truthy; inline floats must not
        // inherit JS-style zero/NaN falsiness from their raw double payload.
        return BOOL_TRUE;
    }
    case LMD_TYPE_STRING: {
        String* str = item.get_safe_string();
        // Empty string is a real value, but it is in the language's falsy set.
        return (str && str->len > 0) ? BOOL_TRUE : BOOL_FALSE;
    }
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
    return {.array = list};
}

static bool string_is_owned_buffer(String* str, gc_header_t** out_header) {
    if (out_header) *out_header = NULL;
    if (!str || !str->is_buffer) return false;
    // is_buffer is set only by string_buffer_copy_join after the constructor
    // audit has cleared every non-GC String's flags. Avoiding a heap-wide
    // ownership lookup keeps the append fast path proportional to the copy.
    gc_header_t* header = gc_get_header(str);
    if (!header || header->type_tag != LMD_TYPE_STRING ||
            header->alloc_size < sizeof(String) + 1) return false;
    if (out_header) *out_header = header;
    return true;
}

static String* string_buffer_copy_join(String* left, String* right, size_t capacity) {
    if (!left || !right || capacity > UINT32_MAX ||
            left->len > capacity || right->len > capacity - left->len) return NULL;
    size_t bytes = sizeof(String) + capacity + 1;
    if (bytes > (size_t)INT_MAX) return NULL;

    // Allocation may relocate both sources. Their exact roots remain valid
    // until the new buffer has copied both halves of the concatenation.
    RootFrame roots(2);
    Rooted<String*> rooted_left(roots, left);
    Rooted<String*> rooted_right(roots, right);
    String* result = (String*)heap_alloc((int)bytes, LMD_TYPE_STRING);
    if (!result) return NULL;
    left = rooted_left.get();
    right = rooted_right.get();
    size_t left_len = left->len;
    size_t right_len = right->len;
    result->len = (uint32_t)(left_len + right_len);
    result->flags = 0;
    result->is_ascii = left->is_ascii && right->is_ascii;
    result->is_buffer = 1;
    memcpy(result->chars, left->chars, left_len);
    memcpy(result->chars + left_len, right->chars, right_len + 1);
    return result;
}

String *fn_strcat(String *left, String *right) {
    if (!left || !right || left->len > UINT32_MAX - right->len) {
        log_error("fn_strcat: invalid concat operands");
        return &STR_ERROR;
    }
    size_t required = (size_t)left->len + right->len;
    gc_header_t* left_header = NULL;
    if (left != right && string_is_owned_buffer(left, &left_header)) {
        size_t capacity = (size_t)left_header->alloc_size - sizeof(String) - 1;
        if (required <= capacity) {
            // MIR publishes a buffer only while its binding is exclusive; a
            // generic join freezes first, so this in-place append cannot alter
            // an alias that observes the prior immutable value.
            size_t old_len = left->len;
            memcpy(left->chars + old_len, right->chars, (size_t)right->len + 1);
            left->len = (uint32_t)required;
            left->is_ascii = left->is_ascii && right->is_ascii;
            return left;
        }
        size_t grown = capacity < 64 ? 64 : capacity;
        while (grown < required) {
            if (grown > UINT32_MAX / 2) {
                grown = required;
                break;
            }
            grown *= 2;
        }
        return string_buffer_copy_join(left, right, grown);
    }

    // A fresh concat starts at its exact size. This keeps ordinary short-lived
    // concatenations compact; only a proven builder pays geometric spare space
    // when its next append actually requires growth.
    return string_buffer_copy_join(left, right, required);
}

String *fn_string_freeze(String *str) {
    if (str && str->is_buffer) {
        // The MIR owner is about to publish this value through an ordinary
        // read, so later concatenation must allocate rather than alter an alias.
        str->is_buffer = 0;
    }
    return str;
}

static String* fn_concat_string_items(Item left, Item right) {
    // The second conversion can collect the first conversion result before
    // fn_strcat receives it, so conversion temporaries need native homes too.
    RootFrame roots(2);
    Rooted<String*> left_str(roots, fn_string(left));
    Rooted<String*> right_str(roots, (String*)NULL);
    right_str.set(fn_string(right));
    String* result = fn_strcat(left_str.get(), right_str.get());
    // Generic fn_join has no MIR ownership proof. Its result may immediately
    // escape through a container, call, or alias, so it is immutable on return.
    if (result && result != &STR_ERROR) result->is_buffer = 0;
    return result;
}

Item fn_join(Item left, Item right) {
    GUARD_ERROR2(left, right);
    // concat two scalars, or join two lists/arrays/maps, or join scalar with list/array, or join two binaries, else error
    TypeId left_type = get_type_id(left), right_type = get_type_id(right);

    if (left_type == LMD_TYPE_NULL &&
        (right_type == LMD_TYPE_ARRAY_NUM || right_type == LMD_TYPE_ARRAY || right_type == LMD_TYPE_RANGE)) {
        return right;
    }
    if (right_type == LMD_TYPE_NULL &&
        (left_type == LMD_TYPE_ARRAY_NUM || left_type == LMD_TYPE_ARRAY || left_type == LMD_TYPE_RANGE)) {
        return left;
    }

    // Handle path concatenation first (path must be on the left)
    if (left_type == LMD_TYPE_PATH) {
        Path* left_path = left.path;
        Pool* pool = eval_context_get_pool();

        if (right_type == LMD_TYPE_STRING) {
            // path ++ string: add string as new segment
            String* right_str = right.get_safe_string();
            Path* result = path_extend(pool, left_path, right_str->chars);
            return {.path = result};
        }
        else if (right_type == LMD_TYPE_SYMBOL) {
            // path ++ symbol: add symbol name as new segment
            Symbol* right_sym = right.get_safe_symbol();
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

    if (left_type == LMD_TYPE_BINARY && right_type == LMD_TYPE_BINARY) {
        Binary* result = heap_binary_concat(left.get_safe_binary(), right.get_safe_binary());
        if (!result) {
            set_runtime_error(ERR_OUT_OF_MEMORY, "fn_join: failed to concatenate binary operands");
            return ItemError;
        }
        return {.item = x2it(result)};
    }

    if (left_type == LMD_TYPE_STRING || right_type == LMD_TYPE_STRING) {
        // null ++ string → string, string ++ null → string
        if (left_type == LMD_TYPE_NULL) return right;
        if (right_type == LMD_TYPE_NULL) return left;
        String *result = fn_concat_string_items(left, right);
        return {.item = s2it(result)};
    }
    else if (left_type == LMD_TYPE_SYMBOL || right_type == LMD_TYPE_SYMBOL) {
        // null ++ symbol → symbol, symbol ++ null → symbol
        if (left_type == LMD_TYPE_NULL) return right;
        if (right_type == LMD_TYPE_NULL) return left;
        String *result = fn_concat_string_items(left, right);
        // Symbol allocation is another safepoint before it copies the inline
        // characters from the intermediate String.
        RootFrame roots(1);
        Rooted<String*> rooted_result(roots, result);
        // Create a proper Symbol from the concatenated string
        result = rooted_result.get();
        Symbol* sym = heap_create_symbol(result->chars, result->len);
        return {.item = y2it(sym)};
    }
    // merge two array-like types (List, Array, ArrayNum, Range)
    else if (left_type == LMD_TYPE_ARRAY_NUM || left_type == LMD_TYPE_ARRAY || left_type == LMD_TYPE_RANGE) {
        if (!(right_type == LMD_TYPE_ARRAY_NUM || right_type == LMD_TYPE_ARRAY || right_type == LMD_TYPE_RANGE)) {
            set_runtime_error(ERR_TYPE_MISMATCH, "fn_join: unsupported operand types: %s and %s",
                type_info[left_type].name, type_info[right_type].name);
            return ItemError;
        }
        // same-type optimization: direct memcpy of native items (not for Range)
        if (left_type == right_type && left_type != LMD_TYPE_RANGE) {
            if (left_type == LMD_TYPE_ARRAY_NUM) {
                ArrayNum *la = left.array_num, *ra = right.array_num;
                int64_t total = la->length + ra->length;
                ArrayNum *result = (ArrayNum *)heap_calloc(sizeof(ArrayNum), LMD_TYPE_ARRAY_NUM);
                result->type_id = LMD_TYPE_ARRAY_NUM;
                result->set_elem_type(la->get_elem_type());
                result->length = total;  result->capacity = total;
                RootFrame roots(1);
                Rooted<ArrayNum*> rooted_result(roots, result);
                size_t elem_size = ELEM_TYPE_SIZE[la->get_elem_type() >> 4];
                result->data = heap_data_alloc(total * elem_size);
                result = rooted_result.get();
                memcpy(result->data, la->data, elem_size*la->length);
                memcpy((char*)result->data + elem_size*la->length, ra->data, elem_size*ra->length);
                return {.array_num = result};
            }
            // LMD_TYPE_ARRAY or LMD_TYPE_ARRAY: both use Item* items (same struct layout)
            Array *la = left.array, *ra = right.array;
            int64_t total_len = la->length + ra->length;
            int64_t total_extra = la->extra + ra->extra;
            Array *result = (Array *)heap_calloc(sizeof(Array) + sizeof(Item)*(total_len + total_extra), left_type);
            result->type_id = left_type;
            result->length = total_len;
            result->capacity = total_len + total_extra;
            result->extra = 0;
            result->items = (Item*)(result + 1);
            // Source tail pointers cannot survive after either operand dies.
            array_copy_owned_items(result, 0, la->items, la->length);
            array_copy_owned_items(result, la->length, ra->items, ra->length);
            return {.array = result};
        }
        // different types: produce generic Array, convert typed elements to Items
        int64_t left_len = fn_len(left), right_len = fn_len(right);
        int64_t total_len = left_len + right_len;
        // A typed source can expose one external scalar payload per element;
        // reserve the exact worst-case tail before copy-in discovers the mix.
        Array *result = (Array *)heap_calloc(sizeof(Array) + sizeof(Item)*(total_len * 2), LMD_TYPE_ARRAY);
        result->type_id = LMD_TYPE_ARRAY;
        result->length = total_len;
        result->capacity = total_len * 2;
        result->extra = 0;
        result->items = (Item*)(result + 1);
        for (int64_t i = 0; i < left_len; i++) array_set(result, i, item_at(left, i));
        for (int64_t i = 0; i < right_len; i++) array_set(result, left_len + i, item_at(right, i));
        return {.array = result};
    }
    else if (left_type <= LMD_TYPE_BINARY && right_type <= LMD_TYPE_BINARY) {
        // scalar ++ scalar: convert both to string and concatenate
        if (left_type == LMD_TYPE_NULL) return right;
        if (right_type == LMD_TYPE_NULL) return left;
        String *result = fn_concat_string_items(left, right);
        return {.item = s2it(result)};
    }
    else {
        set_runtime_error(ERR_TYPE_MISMATCH, "fn_join: unsupported operand types: %s and %s",
            type_info[left_type].name, type_info[right_type].name);
        return ItemError;   // type mismatch
    }
}

static bool array_has_item(Array* arr, Item item) {
    if (!arr) return false;
    for (int64_t i = 0; i < (int64_t)arr->length; i++) {
        if (fn_eq(arr->items[i], item) == BOOL_TRUE) return true;
    }
    return false;
}

Item fn_union(Item left, Item right) {
    GUARD_ERROR2(left, right);
    Array* result = array();
    int64_t left_len = fn_len(left);
    int64_t right_len = fn_len(right);

    // Phase 6 routes value-level `|` here; preserve set order while removing duplicates.
    for (int64_t i = 0; i < left_len; i++) {
        Item item = item_at(left, i);
        if (!array_has_item(result, item)) array_push(result, item);
    }
    for (int64_t i = 0; i < right_len; i++) {
        Item item = item_at(right, i);
        if (!array_has_item(result, item)) array_push(result, item);
    }
    return { .array = result };
}

String *str_repeat(String *str, int64_t times) {
    if (times <= 0) {
        // Return empty string
        String *result = (String *)heap_alloc(sizeof(String) + 1, LMD_TYPE_STRING);
        if (!result) return NULL;
        result->len = 0;
        result->flags = 0;
        result->is_ascii = 1;
        result->chars[0] = '\0';
        return result;
    }

    size_t str_len = str->len;
    size_t total_len;
    // guard str_len * times and the allocation size against overflow / heap_alloc's int param.
    if (!lam::checked_mul(str_len, (size_t)times, &total_len) ||
        total_len + sizeof(String) + 1 > (size_t)INT_MAX) {
        return NULL;
    }
    String *result = (String *)heap_alloc((int)(sizeof(String) + total_len + 1), LMD_TYPE_STRING);
    if (!result) return NULL;
    result->len = total_len;
    result->flags = 0;
    result->is_ascii = str->is_ascii;

    for (long i = 0; i < times; i++) {
        memcpy(result->chars + (i * str_len), str->chars, str_len);
    }
    result->chars[total_len] = '\0';

    return result;
}

// Unicode string normalization function
Item fn_normalize(Item str_item, Item type_item) {
    // an error mode must not be mistaken for an omitted mode and silently default to NFC
    GUARD_ERROR2(str_item, type_item);
    // normalize(string, 'nfc'|'nfd'|'nfkc'|'nfkd') - Unicode normalization
    if (str_item._type_id != LMD_TYPE_STRING) {
        log_debug("normalize: first argument must be a string, got type: %s", get_type_name(str_item._type_id));
        return ItemError;
    }

    String* str = str_item.get_safe_string();
    if (!str || str->len == 0) {
        return str_item;  // Return empty string as-is
    }

    // Default to NFC if no type specified or invalid type
    int options = UTF8PROC_STABLE | UTF8PROC_COMPOSE;

    if (type_item._type_id == LMD_TYPE_STRING) {
        String* type_str = type_item.get_safe_string();
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
    result->flags = 0;
    result->is_ascii = str_is_ascii((const char*)normalized, normalized_len) ? 1 : 0;
    memcpy(result->chars, normalized, normalized_len);
    result->chars[normalized_len] = '\0';

    // utf8proc_map allocates outside Lambda memtrack; keep the allocator pair matched.
    free_utf8proc_result((char*)normalized);
    return (Item){.item = s2it(result)};
}

static bool item_single_string_codepoint(Item item, uint32_t* codepoint) {
    if (!codepoint || get_type_id(item) != LMD_TYPE_STRING) return false;
    const char* chars = item.get_chars();
    uint32_t length = item.get_len();
    if (!chars || length == 0) return false;
    uint32_t value = 0;
    int decoded = str_utf8_decode(chars, length, &value);
    if (decoded <= 0 || (uint32_t)decoded != length) return false;
    *codepoint = value;
    return true;
}

static bool range_contains_item(Range* range, Item item) {
    if (!range) return false;
    if (range->is_char) {
        uint32_t codepoint = 0;
        return item_single_string_codepoint(item, &codepoint) &&
            range->start <= (int64_t)codepoint && (int64_t)codepoint <= range->end;
    }
    int64_t value = 0;
    return lambda_item_to_int64_exact(item, &value) &&
        range->start <= value && value <= range->end;
}

static bool range_type_contains_item(TypeRange* range, Item item) {
    if (!range) return false;
    if (range->is_char) {
        uint32_t start = 0;
        uint32_t end = 0;
        uint32_t value = 0;
        if (!item_single_string_codepoint(range->start, &start) ||
                !item_single_string_codepoint(range->end, &end) ||
                !item_single_string_codepoint(item, &value)) return false;
        return start <= value && value <= end;
    }
    int64_t start = 0;
    int64_t end = 0;
    int64_t value = 0;
    return lambda_item_to_int64_exact(range->start, &start) &&
        lambda_item_to_int64_exact(range->end, &end) &&
        lambda_item_to_int64_exact(item, &value) &&
        start <= value && value <= end;
}

// One owner for the bad-range-bound diagnosis. The JIT's counted-range lowering
// (T19-3) re-checks the int53 band itself, because its bounds arrive in the
// TOTAL int lane where a saturated or null value is an ordinary i64 sentinel;
// its cold arm reports through here so the message never forks from fn_to's.
Item fn_range_bound_error(Item item_a, Item item_b) {
    log_error("fn_to: range bounds must be exact integers or single-codepoint strings, got %s, %s",
        get_type_name(get_type_id(item_a)), get_type_name(get_type_id(item_b)));
    return ItemError;
}

Item fn_to(Item item_a, Item item_b) {
    GUARD_ERROR2(item_a, item_b);
    int64_t start = 0;
    int64_t end = 0;
    if (lambda_item_to_int64_exact(item_a, &start) &&
            lambda_item_to_int64_exact(item_b, &end)) {
        // Formal semantics 4.8: an `int` range requires its bounds within
        // +/-(2^53 - 1). A range is a sequence of CONSECUTIVE integers, so it
        // needs a well-defined successor -- x + 1 must differ from x -- which
        // in binary64 holds precisely to 2^53. Above it the spacing exceeds 1,
        // consecutive integers no longer all exist, and the request stops
        // denoting a sequence: at 2^70 the spacing is 2^18, so `big to big + 2`
        // silently yielded a one-element range. Refuse rather than guess; a
        // larger sequence is written `1n to N` over `integer`, which is exact
        // at every magnitude. This is also what keeps start/end/length proven
        // i64 (plan item G3), so `Range` needs no widening.
        if (start < INT53_MIN || start > INT53_MAX ||
                end < INT53_MIN || end > INT53_MAX) {
            set_runtime_error(ERR_INDEX_OUT_OF_BOUNDS,
                "int range bound outside +/-(2^53 - 1); use an `integer` range "
                "(`1n to N`) for larger sequences");
            return ItemError;
        }
        if (start > end) {
            // return empty range instead of NULL
            log_debug("Error: start of range is greater than end: %ld > %ld", start, end);
            Range *range = (Range *)heap_alloc(sizeof(Range), LMD_TYPE_RANGE);
            range->type_id = LMD_TYPE_RANGE;
            range->start = 0;  range->end = -1;  // Empty range
            range->length = 0;
            range->is_char = false;
            return {.range = range};
        }
        Range *range = (Range *)heap_alloc(sizeof(Range), LMD_TYPE_RANGE);
        range->type_id = LMD_TYPE_RANGE;
        range->start = start;  range->end = end;
        range->length = end - start + 1;
        range->is_char = false;
        return {.range = range};
    }

    uint32_t char_start = 0;
    uint32_t char_end = 0;
    if (item_single_string_codepoint(item_a, &char_start) &&
            item_single_string_codepoint(item_b, &char_end)) {
        Range* range = (Range*)heap_alloc(sizeof(Range), LMD_TYPE_RANGE);
        range->type_id = LMD_TYPE_RANGE;
        range->is_char = true;
        if (char_start > char_end) {
            range->start = 0;
            range->end = -1;
            range->length = 0;
        } else {
            range->start = (int64_t)char_start;
            range->end = (int64_t)char_end;
            range->length = (int64_t)char_end - (int64_t)char_start + 1;
        }
        return {.range = range};
    }
    else {
        return fn_range_bound_error(item_a, item_b);
    }
}

Function* to_fn(fn_ptr ptr) {
    Function *fn = (Function*)heap_calloc(sizeof(Function), LMD_TYPE_FUNC);
    fn->type_id = LMD_TYPE_FUNC;
    fn->entry_abi = FN_ENTRY_ABI_UNKNOWN;
    fn->ptr = ptr;
    fn->closure_env = NULL;
    return fn;
}

// Create function with arity info
Function* to_fn_n(fn_ptr ptr, int arity) {
    Function *fn = (Function*)heap_calloc(sizeof(Function), LMD_TYPE_FUNC);
    fn->type_id = LMD_TYPE_FUNC;
    fn->entry_abi = FN_ENTRY_ABI_UNKNOWN;
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
    fn->entry_abi = FN_ENTRY_ABI_UNKNOWN;
    fn->arity = (uint8_t)arity;
    fn->ptr = ptr;
    fn->closure_env = NULL;
    fn->name = name;  // should be interned string, no need to copy
    return fn;
}

Function* to_sys_fn_named(fn_ptr ptr, int arity, const char* name) {
    Function *fn = to_fn_named(ptr, arity, name);
    // builtin C functions have heterogeneous ABIs; this value is for identity/name.
    fn->is_system_function_ref = 1;
    fn->entry_abi = FN_ENTRY_ABI_FOREIGN;
    return fn;
}

void lambda_function_mark_mir_context_abi(Function* fn) {
    if (!fn) return;
    Context* runtime = (Context*)context;
    if (!runtime) {
        log_error("mir-context-abi: missing TLS owner while publishing function");
        return;
    }
    // A generated Function may outlive its creating activation. Capture the
    // TLS owner here so later native dispatch can forward it only to MIR code.
    fn->requires_runtime_context = 1;
    fn->runtime_context = runtime;
}

void lambda_function_mark_lambda_boxed_function(Function* fn) {
    if (!fn) return;
    fn->entry_abi = FN_ENTRY_ABI_LAMBDA_BOXED_FUNCTION;
}

void lambda_function_mark_lambda_boxed_procedure(Function* fn) {
    if (!fn) return;
    fn->entry_abi = FN_ENTRY_ABI_LAMBDA_BOXED_PROCEDURE;
}

void lambda_function_mark_mir_public_return_shape(Function* fn, uint32_t shape) {
    if (!fn) return;
    // Unknown remains fail-closed: a missed publication site must resolve the
    // slot rather than let a pending Item cross a dynamic call boundary.
    if (shape > LAMBDA_MIR_PUBLIC_RETURN_ITEM_COMPANION) {
        shape = LAMBDA_MIR_PUBLIC_RETURN_UNKNOWN;
    }
    fn->mir_public_return_shape = shape;
}

void lambda_function_set_type(Function* fn, void* fn_type) {
    if (!fn) return;
    // First-class calls need the semantic signature for optional and variadic
    // arity checks. The source TypeFunc outlives the compiled module, so this
    // records metadata only; it never changes the function's physical ABI.
    fn->fn_type = fn_type;
}

// Create a closure with captured environment
Function* to_closure(fn_ptr ptr, int arity, void* env) {
    Function* fn = (Function*)heap_calloc(sizeof(Function), LMD_TYPE_FUNC);
    fn->type_id = LMD_TYPE_FUNC;
    fn->entry_abi = FN_ENTRY_ABI_UNKNOWN;
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
    fn->entry_abi = FN_ENTRY_ABI_UNKNOWN;
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


typedef enum LambdaDynamicCallMode {
    LAMBDA_DYNAMIC_CALL_FUNCTION,
    LAMBDA_DYNAMIC_CALL_PROCEDURE,
} LambdaDynamicCallMode;

static Item lambda_dynamic_call_error(LambdaErrorCode code, const char* caller,
        const char* detail) {
    char message[256];
    snprintf(message, sizeof(message), "%s: %s", caller, detail);
    set_runtime_error(code, "%s", message);
    if (context && context->heap && context->heap->gc) {
        SourceLocation loc = {0};
        if (context->current_file) loc.file = context->current_file;
        LambdaError* error = err_create_heap(code, message, &loc);
        if (error) {
            error->raw_stack_trace = err_capture_raw_stack_trace(context->debug_info, 32);
            return err2it(error);
        }
    }
    return ItemError;
}

static Item lambda_dynamic_argument_limit_error(const char* caller, int64_t count,
        const char* kind) {
    char detail[192];
    snprintf(detail, sizeof(detail), "%s count %lld exceeds Core Lambda limit %d",
        kind, (long long)count, LAMBDA_MAX_FUNCTION_ARGS);
    return lambda_dynamic_call_error(ERR_FUNCTION_ARGUMENT_LIMIT, caller, detail);
}

static bool lambda_dynamic_abi_is_core(FunctionEntryAbi abi) {
    // The interpreted entry is a Core Lambda callable too: it carries the same
    // TypeFunc signature and needs the same optional/rest adaptation. Only its
    // final invocation differs (AI7).
    return abi == FN_ENTRY_ABI_LAMBDA_BOXED_FUNCTION ||
        abi == FN_ENTRY_ABI_LAMBDA_BOXED_PROCEDURE ||
        abi == FN_ENTRY_ABI_LAMBDA_INTERPRETED;
}

static bool lambda_dynamic_signature_has_var_parameter(const TypeFunc* signature) {
    if (!signature) return false;
    for (const TypeParam* param = signature->param; param; param = param->next) {
        if (param->is_var_param) return true;
    }
    return false;
}

static Item lambda_dynamic_check_signature(Function* fn, int actual,
        LambdaDynamicCallMode mode, const char* caller, int* physical_count,
        bool* needs_adapter) {
    if (actual > LAMBDA_MAX_FUNCTION_ARGS) {
        return lambda_dynamic_argument_limit_error(caller, actual, "argument");
    }
    if (fn->arity > LAMBDA_MAX_FUNCTION_ARGS) {
        return lambda_dynamic_argument_limit_error(caller, fn->arity, "function arity");
    }
    if (mode == LAMBDA_DYNAMIC_CALL_FUNCTION &&
            !lambda_dynamic_abi_is_core(fn->entry_abi) &&
            fn->entry_abi != FN_ENTRY_ABI_HOST_ADAPTER) {
        // Procedure methods are boxed entries too; ordinary object.method()
        // reaches this function-mode dispatcher even when the method is `pn`.
        return lambda_dynamic_call_error(ERR_UNSUPPORTED_DYNAMIC_ABI, caller,
            "dynamic expression call requires a boxed Lambda callable entry");
    }
    if (mode == LAMBDA_DYNAMIC_CALL_PROCEDURE &&
            fn->entry_abi != FN_ENTRY_ABI_LAMBDA_BOXED_PROCEDURE) {
        return lambda_dynamic_call_error(ERR_UNSUPPORTED_DYNAMIC_ABI, caller,
            "task launch requires a boxed Lambda procedure entry");
    }
    if (!fn->ptr && fn->entry_abi != FN_ENTRY_ABI_LAMBDA_INTERPRETED) {
        return lambda_dynamic_call_error(ERR_INVALID_CALL, caller,
            "function has no native entry");
    }
    if (fn->is_system_function_ref) {
        return lambda_dynamic_call_error(ERR_UNSUPPORTED_DYNAMIC_ABI, caller,
            "builtin references do not expose the boxed dynamic-call ABI");
    }

    TypeFunc* signature = (TypeFunc*)fn->fn_type;
    if (lambda_dynamic_abi_is_core(fn->entry_abi) &&
            (!signature || signature->type_id != LMD_TYPE_FUNC) &&
            mode != LAMBDA_DYNAMIC_CALL_PROCEDURE) {
        // A Core boxed pointer without its semantic signature cannot safely
        // materialize optional/rest slots or reject mutable parameters.
        return lambda_dynamic_call_error(ERR_UNSUPPORTED_DYNAMIC_ABI, caller,
            "boxed Lambda entry is missing its function signature");
    }

    int required = fn->arity;
    int fixed = fn->arity;
    bool variadic = false;
    if (signature && signature->type_id == LMD_TYPE_FUNC) {
        if (lambda_dynamic_signature_has_var_parameter(signature)) {
            return lambda_dynamic_call_error(ERR_UNSUPPORTED_DYNAMIC_ABI, caller,
                "dynamic dispatch of a function with `var` parameters is deferred");
        }
        required = signature->required_param_count;
        fixed = signature->param_count;
        variadic = signature->is_variadic;
    } else if (lambda_dynamic_abi_is_core(fn->entry_abi)) {
        // Async task root lowering has a generated procedure wrapper but no
        // source TypeFunc in its scheduler frame. It has no optional/rest
        // adapter; retain the exact fixed arity supplied by the launcher.
        fixed = fn->arity;
    }
    int physical = fixed + (variadic ? 1 : 0);
    if (physical > LAMBDA_MAX_FUNCTION_ARGS) {
        return lambda_dynamic_argument_limit_error(caller, physical, "formal slot");
    }
    if (fn->entry_abi == FN_ENTRY_ABI_HOST_ADAPTER && fn->closure_env &&
            physical >= LAMBDA_MAX_FUNCTION_ARGS) {
        return lambda_dynamic_argument_limit_error(caller, physical + 1,
            "hosted native argument");
    }
    if (actual < required || (!variadic && actual > fixed)) {
        char detail[192];
        snprintf(detail, sizeof(detail), "function '%s' expects %d%s argument%s, got %d",
            fn->name ? fn->name : "<anonymous>", required,
            variadic ? " or more" : "", required == 1 && !variadic ? "" : "s", actual);
        return lambda_dynamic_call_error(ERR_ARGUMENT_COUNT_MISMATCH, caller, detail);
    }
    if (fn->entry_abi == FN_ENTRY_ABI_HOST_ADAPTER &&
            (variadic || actual != fixed)) {
        return lambda_dynamic_call_error(ERR_UNSUPPORTED_DYNAMIC_ABI, caller,
            "host adapter does not expose optional or rest argument adaptation");
    }
    *physical_count = physical;
    *needs_adapter = variadic || actual < fixed;
    return ItemNull;
}

static Item lambda_dynamic_invoke_host_adapter(Function* fn, const Item* args,
        int count, const char* caller) {
    // hosted callbacks use the shared Item-only ABI.  A legacy closure
    // environment is normalized as the first explicit Item so Core and JS do
    // not cast the same callback through different hidden-prefix prototypes.
    int native_count = count + (fn->closure_env ? 1 : 0);
    if (native_count > LAMBDA_MAX_FUNCTION_ARGS) {
        return lambda_dynamic_argument_limit_error(caller, native_count,
            "hosted native argument");
    }
    Item env_item = (Item){.item = (uint64_t)(uintptr_t)fn->closure_env};
    return lambda_hosted_item_invoke_by_count((void*)fn->ptr,
        args, count, fn->closure_env != NULL, env_item);
}

template <typename IndexSequence>
struct LambdaDynamicNativeInvoker;

template <int... indices>
struct LambdaDynamicNativeInvoker<LambdaHostedItemIndexSequence<indices...>> {
    static Item resolve_public_result(Function* fn, Item value) {
        if (fn && fn->mir_public_return_shape ==
                LAMBDA_MIR_PUBLIC_RETURN_ITEM) {
            return value;
        }
        return lambda_item_resolve_pending_slot(value);
    }

    static Item invoke(Function* fn, const Item* args, uint64_t* result_home,
            const char* caller) {
        // A published Core entry must retain its hidden context operand. This
        // blocks raw/native bodies from being reinterpreted as boxed calls —
        // and return convention v3 (RVO3) leans on it, because the casts below
        // are single-result C prototypes and must never be applied to a
        // pair-returning body.
        //
        // The compiled wrapper hands lane 2 back through
        // `Context::mir_companion_slot` (RV12), so its result is resolved
        // after the call rather than receiving caller-owned storage first.
        if (!fn->requires_runtime_context) {
            return lambda_dynamic_call_error(ERR_UNSUPPORTED_DYNAMIC_ABI, caller,
                "boxed Lambda entry is missing its context ABI metadata");
        }
        (void)result_home;
        Context* runtime = fn->runtime_context;
        if (!runtime) {
            return lambda_dynamic_call_error(ERR_UNSUPPORTED_DYNAMIC_ABI, caller,
                "boxed Lambda entry has no owning runtime context");
        }
        if (eval_context_tls_runtime() != runtime) {
            return lambda_dynamic_call_error(ERR_UNSUPPORTED_DYNAMIC_ABI, caller,
                "boxed Lambda entry belongs to a different runtime context");
        }
        // RV12: no trailing home operand; resolve lane 2 from the context slot
        // immediately, before anything else can clobber it (RV4.2).
        if (fn->closure_env) {
            return resolve_public_result(fn,
                ((Item (*)(Context*, void*, LambdaHostedItem<indices>...))fn->ptr)(
                    runtime, fn->closure_env, args[indices]...));
        }
        return resolve_public_result(fn,
            ((Item (*)(Context*, LambdaHostedItem<indices>...))fn->ptr)(
                runtime, args[indices]...));
    }
};

template <int count>
static Item lambda_dynamic_invoke_native(Function* fn, const Item* args,
        uint64_t* result_home, const char* caller) {
    typedef typename LambdaHostedMakeIndexSequence<count>::Type Indices;
    return LambdaDynamicNativeInvoker<Indices>::invoke(fn, args, result_home, caller);
}

static Item lambda_dynamic_invoke_by_count(Function* fn, const Item* args,
        int count, uint64_t* result_home, const char* caller) {
    if (fn->entry_abi == FN_ENTRY_ABI_HOST_ADAPTER) {
        return lambda_dynamic_invoke_host_adapter(fn, args, count, caller);
    }
    if (fn->entry_abi == FN_ENTRY_ABI_LAMBDA_INTERPRETED) {
        // T0 values retain the source entry until this one dispatch boundary.
        // Upgrade before selecting a C prototype: a published satellite uses
        // the ordinary generated boxed ABI, while a cold/pinned definition
        // must keep the exact interpreter call path (D8.1.1v2 §5.3).
        if (!interp_promote_function_if_hot(fn)) {
            return interp_call(fn, args, count);
        }
    }
    // This is the Core boxed ABI dispatch; hosted native callbacks use the
    // shared Item-only switch in hosted-call-dispatch.hpp above.
    switch (count) {
    case 0: return lambda_dynamic_invoke_native<0>(fn, args, result_home, caller);
    case 1: return lambda_dynamic_invoke_native<1>(fn, args, result_home, caller);
    case 2: return lambda_dynamic_invoke_native<2>(fn, args, result_home, caller);
    case 3: return lambda_dynamic_invoke_native<3>(fn, args, result_home, caller);
    case 4: return lambda_dynamic_invoke_native<4>(fn, args, result_home, caller);
    case 5: return lambda_dynamic_invoke_native<5>(fn, args, result_home, caller);
    case 6: return lambda_dynamic_invoke_native<6>(fn, args, result_home, caller);
    case 7: return lambda_dynamic_invoke_native<7>(fn, args, result_home, caller);
    case 8: return lambda_dynamic_invoke_native<8>(fn, args, result_home, caller);
    case 9: return lambda_dynamic_invoke_native<9>(fn, args, result_home, caller);
    case 10: return lambda_dynamic_invoke_native<10>(fn, args, result_home, caller);
    case 11: return lambda_dynamic_invoke_native<11>(fn, args, result_home, caller);
    case 12: return lambda_dynamic_invoke_native<12>(fn, args, result_home, caller);
    case 13: return lambda_dynamic_invoke_native<13>(fn, args, result_home, caller);
    case 14: return lambda_dynamic_invoke_native<14>(fn, args, result_home, caller);
    case 15: return lambda_dynamic_invoke_native<15>(fn, args, result_home, caller);
    case 16: return lambda_dynamic_invoke_native<16>(fn, args, result_home, caller);
    default: return lambda_dynamic_argument_limit_error(caller, count, "physical argument");
    }
}

static Item lambda_dynamic_call(Function* fn, List* args, uint64_t* result_home,
        LambdaDynamicCallMode mode, const char* caller) {
    int64_t source_actual = args ? args->length : 0;
    if (source_actual < 0 || source_actual > LAMBDA_MAX_FUNCTION_ARGS) {
        return lambda_dynamic_argument_limit_error(caller, source_actual,
            "argument");
    }
    int actual = (int)source_actual;
    if (!is_valid_function(fn)) {
        return lambda_dynamic_call_error(ERR_INVALID_CALL, caller,
            "cannot call non-function value");
    }
    // A LambdaJS function begins with the common type id but has a distinct
    // layout. Validate this byte before reading Core Function metadata.
    if (fn->entry_abi != FN_ENTRY_ABI_LAMBDA_BOXED_FUNCTION &&
            fn->entry_abi != FN_ENTRY_ABI_LAMBDA_BOXED_PROCEDURE &&
            fn->entry_abi != FN_ENTRY_ABI_HOST_ADAPTER &&
            fn->entry_abi != FN_ENTRY_ABI_LAMBDA_INTERPRETED) {
        return lambda_dynamic_call_error(ERR_UNSUPPORTED_DYNAMIC_ABI, caller,
            "function does not publish a Core boxed dynamic-call entry");
    }

    int physical = 0;
    bool needs_adapter = false;
    // Keep the public *_into diagnostic stable while convenience wrappers
    // retain their historical names for non-function values.
    const char* signature_caller = strncmp(caller, "fn_call", 7) == 0
        ? "fn_call_into" : caller;
    Item checked = lambda_dynamic_check_signature(fn, actual, mode,
        signature_caller,
        &physical, &needs_adapter);
    if (checked.item != ITEM_NULL) return checked;

    // Root the function plus every source Item before `list()` can allocate.
    // This exact span is the native equivalent of LambdaJS's adapter span.
    RootSpan source_roots((size_t)actual + 1);
    if (!source_roots.valid()) {
        return lambda_dynamic_call_error(ERR_INVALID_CALL, caller,
            "dynamic boxed dispatch requires an active Lambda runtime");
    }
    uint64_t* source_words = source_roots.words();
    source_words[0] = (uint64_t)(uintptr_t)fn;
    for (int i = 0; i < actual; i++) source_words[i + 1] = args->items[i].item;
    Item* source_items = (Item*)(void*)(source_words + 1);

    if (!needs_adapter) {
        Function* rooted_fn = (Function*)(uintptr_t)source_words[0];
        return lambda_dynamic_invoke_by_count(rooted_fn, source_items, actual,
            result_home, caller);
    }

    RootSpan adapter_roots((size_t)physical);
    if (!adapter_roots.valid()) {
        return lambda_dynamic_call_error(ERR_INVALID_CALL, caller,
            "could not reserve dynamic-call adapter roots");
    }
    uint64_t* adapter_words = adapter_roots.words();
    TypeFunc* signature = (TypeFunc*)fn->fn_type;
    int fixed = signature->param_count;
    for (int i = 0; i < fixed; i++) {
        adapter_words[i] = i < actual ? source_words[i + 1] : ITEM_MISSING_ARGUMENT;
    }
    if (signature->is_variadic) {
        adapter_words[fixed] = ITEM_NULL;
        if (actual > fixed) {
            List* rest = list();
            if (!rest) {
                return lambda_dynamic_call_error(ERR_OUT_OF_MEMORY, caller,
                    "could not allocate rest argument list");
            }
            adapter_words[fixed] = (uint64_t)(uintptr_t)rest;
            for (int i = fixed; i < actual; i++) {
                // Reload both rooted values after each append: growth may collect.
                rest = (List*)(uintptr_t)adapter_words[fixed];
                list_push(rest, (Item){.item = source_words[i + 1]});
            }
        }
    }
    Function* rooted_fn = (Function*)(uintptr_t)source_words[0];
    return lambda_dynamic_invoke_by_count(rooted_fn,
        (Item*)(void*)adapter_words, physical, result_home, caller);
}

Item fn_call_into(Function* fn, List* args, uint64_t* result_home) {
    return lambda_dynamic_call(fn, args, result_home, LAMBDA_DYNAMIC_CALL_FUNCTION,
        "fn_call_into");
}

static Item lambda_dynamic_public_non_function_error(Function* fn, const char* caller) {
    if (is_valid_function(fn)) return ItemNull;
    // The result-home helper is invisible to Lambda code; retain the public
    // arity wrapper in a non-function diagnostic.
    return lambda_dynamic_call_error(ERR_INVALID_CALL, caller,
        "cannot call non-function value");
}

Item fn_call0_into(Function* fn, uint64_t* result_home) {
    Item invalid = lambda_dynamic_public_non_function_error(fn, "fn_call0");
    if (invalid.item != ITEM_NULL) return invalid;
    return lambda_dynamic_call(fn, NULL, result_home,
        LAMBDA_DYNAMIC_CALL_FUNCTION, "fn_call0");
}

Item fn_call1_into(Function* fn, Item a, uint64_t* result_home) {
    Item invalid = lambda_dynamic_public_non_function_error(fn, "fn_call1");
    if (invalid.item != ITEM_NULL) return invalid;
    List args = {.length = 1, .items = &a};
    return lambda_dynamic_call(fn, &args, result_home,
        LAMBDA_DYNAMIC_CALL_FUNCTION, "fn_call1");
}

Item fn_call2_into(Function* fn, Item a, Item b, uint64_t* result_home) {
    Item invalid = lambda_dynamic_public_non_function_error(fn, "fn_call2");
    if (invalid.item != ITEM_NULL) return invalid;
    Item values[] = {a, b};
    List args = {.length = 2, .items = values};
    return lambda_dynamic_call(fn, &args, result_home,
        LAMBDA_DYNAMIC_CALL_FUNCTION, "fn_call2");
}

Item fn_call3_into(Function* fn, Item a, Item b, Item c, uint64_t* result_home) {
    Item invalid = lambda_dynamic_public_non_function_error(fn, "fn_call3");
    if (invalid.item != ITEM_NULL) return invalid;
    Item values[] = {a, b, c};
    List args = {.length = 3, .items = values};
    return lambda_dynamic_call(fn, &args, result_home,
        LAMBDA_DYNAMIC_CALL_FUNCTION, "fn_call3");
}

Item fn_call(Function* fn, List* args) {
    return lambda_dynamic_call(fn, args, NULL, LAMBDA_DYNAMIC_CALL_FUNCTION, "fn_call");
}

Item fn_call0(Function* fn) { return fn_call(fn, NULL); }

Item fn_call1(Function* fn, Item a) {
    List args = {.length = 1, .items = &a};
    return fn_call(fn, &args);
}

Item fn_call2(Function* fn, Item a, Item b) {
    Item values[] = {a, b};
    List args = {.length = 2, .items = values};
    return fn_call(fn, &args);
}

Item fn_call3(Function* fn, Item a, Item b, Item c) {
    Item values[] = {a, b, c};
    List args = {.length = 3, .items = values};
    return fn_call(fn, &args);
}

// Concurrency intentionally selects the procedure entry kind; an ordinary
// function cannot be launched as a task merely because its pointer is boxed.
extern "C" Item lambda_concurrency_fn_call_procedure_into(Function* fn, List* args,
        uint64_t* result_home) {
    return lambda_dynamic_call(fn, args, result_home,
        LAMBDA_DYNAMIC_CALL_PROCEDURE, "lambda task launch");
}

extern "C" Function* lambda_concurrency_to_closure(fn_ptr ptr, int arity, void* env) {
    return to_closure(ptr, arity, env);
}

// Trampolines for calling _b boxed wrapper functions from MIR Direct code.
// _b wrappers generated by transpile-mir.cpp return plain Item values; routing
// through C keeps the function-pointer call ABI stable across platforms.
extern "C" {
Item fn_call_boxed_0(void* fp) {
    return ((Item(*)())fp)();
}
Item fn_call_boxed_1(void* fp, Item a) {
    return ((Item(*)(Item))fp)(a);
}
Item fn_call_boxed_2(void* fp, Item a, Item b) {
    return ((Item(*)(Item,Item))fp)(a, b);
}
Item fn_call_boxed_3(void* fp, Item a, Item b, Item c) {
    return ((Item(*)(Item,Item,Item))fp)(a, b, c);
}
Item fn_call_boxed_4(void* fp, Item a, Item b, Item c, Item d) {
    return ((Item(*)(Item,Item,Item,Item))fp)(a, b, c, d);
}
Item fn_call_boxed_5(void* fp, Item a, Item b, Item c, Item d, Item e) {
    return ((Item(*)(Item,Item,Item,Item,Item))fp)(a, b, c, d, e);
}
Item fn_call_boxed_6(void* fp, Item a, Item b, Item c, Item d, Item e, Item f) {
    return ((Item(*)(Item,Item,Item,Item,Item,Item))fp)(a, b, c, d, e, f);
}
Item fn_call_boxed_7(void* fp, Item a, Item b, Item c, Item d, Item e, Item f, Item g) {
    return ((Item(*)(Item,Item,Item,Item,Item,Item,Item))fp)(a, b, c, d, e, f, g);
}
Item fn_call_boxed_8(void* fp, Item a, Item b, Item c, Item d, Item e, Item f, Item g, Item h) {
    return ((Item(*)(Item,Item,Item,Item,Item,Item,Item,Item))fp)(a, b, c, d, e, f, g, h);
}

// RV12: the public wrapper returns a pending Item and leaves lane 2 in
// `Context::mir_companion_slot`.  These C trampolines keep result_home because
// they are also the explicit ownership boundary for hosted/native callbacks.
Item fn_call_boxed_0_into(void* fp, uint64_t* result_home) {
    Context* runtime = (Context*)context;
    if (!runtime) return ItemError;
    (void)result_home;
    return lambda_item_resolve_pending_slot(
        ((Item(*)(Context*))fp)(runtime));
}

#define EXPAND_BOXED_CALL_PARAMS(...) __VA_ARGS__
#define DEFINE_BOXED_CALL_INTO(count, params, args) \
    Item fn_call_boxed_##count##_into(void* fp, \
            EXPAND_BOXED_CALL_PARAMS params, uint64_t* result_home) { \
        Context* runtime = (Context*)context; \
        if (!runtime) return ItemError; \
        (void)result_home; \
        return lambda_item_resolve_pending_slot( \
            ((Item(*)(Context*, EXPAND_BOXED_CALL_PARAMS params))fp)( \
                runtime, EXPAND_BOXED_CALL_PARAMS args)); \
    }

DEFINE_BOXED_CALL_INTO(1, (Item a), (a))
DEFINE_BOXED_CALL_INTO(2, (Item a, Item b), (a, b))
DEFINE_BOXED_CALL_INTO(3, (Item a, Item b, Item c), (a, b, c))
DEFINE_BOXED_CALL_INTO(4, (Item a, Item b, Item c, Item d), (a, b, c, d))
DEFINE_BOXED_CALL_INTO(5, (Item a, Item b, Item c, Item d, Item e), (a, b, c, d, e))
DEFINE_BOXED_CALL_INTO(6, (Item a, Item b, Item c, Item d, Item e, Item f), (a, b, c, d, e, f))
DEFINE_BOXED_CALL_INTO(7, (Item a, Item b, Item c, Item d, Item e, Item f, Item g), (a, b, c, d, e, f, g))
DEFINE_BOXED_CALL_INTO(8, (Item a, Item b, Item c, Item d, Item e, Item f, Item g, Item h), (a, b, c, d, e, f, g, h))
DEFINE_BOXED_CALL_INTO(9, (Item a, Item b, Item c, Item d, Item e, Item f, Item g, Item h, Item i), (a, b, c, d, e, f, g, h, i))
DEFINE_BOXED_CALL_INTO(10, (Item a, Item b, Item c, Item d, Item e, Item f, Item g, Item h, Item i, Item j), (a, b, c, d, e, f, g, h, i, j))
DEFINE_BOXED_CALL_INTO(11, (Item a, Item b, Item c, Item d, Item e, Item f, Item g, Item h, Item i, Item j, Item k), (a, b, c, d, e, f, g, h, i, j, k))
DEFINE_BOXED_CALL_INTO(12, (Item a, Item b, Item c, Item d, Item e, Item f, Item g, Item h, Item i, Item j, Item k, Item l), (a, b, c, d, e, f, g, h, i, j, k, l))
DEFINE_BOXED_CALL_INTO(13, (Item a, Item b, Item c, Item d, Item e, Item f, Item g, Item h, Item i, Item j, Item k, Item l, Item m), (a, b, c, d, e, f, g, h, i, j, k, l, m))
DEFINE_BOXED_CALL_INTO(14, (Item a, Item b, Item c, Item d, Item e, Item f, Item g, Item h, Item i, Item j, Item k, Item l, Item m, Item n), (a, b, c, d, e, f, g, h, i, j, k, l, m, n))
DEFINE_BOXED_CALL_INTO(15, (Item a, Item b, Item c, Item d, Item e, Item f, Item g, Item h, Item i, Item j, Item k, Item l, Item m, Item n, Item o), (a, b, c, d, e, f, g, h, i, j, k, l, m, n, o))
DEFINE_BOXED_CALL_INTO(16, (Item a, Item b, Item c, Item d, Item e, Item f, Item g, Item h, Item i, Item j, Item k, Item l, Item m, Item n, Item o, Item p), (a, b, c, d, e, f, g, h, i, j, k, l, m, n, o, p))
#undef DEFINE_BOXED_CALL_INTO
#undef EXPAND_BOXED_CALL_PARAMS
} // extern "C"


static bool numeric_type_subsumes(Type* actual, Type* target) {
    if (!actual || !target) return false;
    if (target == &TYPE_NUMBER) return IS_NUMERIC_ID(actual->type_id);
    if (actual == &TYPE_NUMBER) return target == &TYPE_NUMBER;
    LambdaNumericKind actual_kind = lambda_numeric_kind_from_type(actual);
    LambdaNumericKind target_kind = lambda_numeric_kind_from_type(target);
    if (actual_kind != LAMBDA_NUM_INVALID && target_kind != LAMBDA_NUM_INVALID) {
        return lambda_numeric_kind_exactly_embeds(actual_kind, target_kind);
    }
    return actual->type_id == target->type_id;
}

static Type* item_static_type_for_is(Item item, Type* scratch) {
    TypeId type_id = get_type_id(item);
    // C16 surface rule, same as fn_type(): the shared poison reports as `int`,
    // so `nan is int` holds. `nan is float` still holds too, via int <= float
    // in numeric_type_subsumes().
    if (lambda_item_is_merged_poison(item.item)) type_id = LMD_TYPE_INT;
    if (type_id == LMD_TYPE_TYPE) {
        TypeType* tt = (TypeType*)item.type;
        return tt ? tt->type : NULL;
    }
    if (type_id == LMD_TYPE_DECIMAL) {
        Decimal* decimal = item.get_decimal();
        return decimal && decimal->unlimited == DECIMAL_BIGINT ?
            &TYPE_INTEGER_VALUE : &TYPE_DECIMAL;
    }
    scratch->type_id = type_id;
    scratch->kind = 0;
    if (type_id == LMD_TYPE_NUM_SIZED) {
        scratch->kind = item.get_num_type();
    }
    return scratch;
}

static bool item_type_is_integer_subtype(Item item, TypeId type_id) {
    if (type_id == LMD_TYPE_DECIMAL) {
        Decimal* dec = item.get_decimal();
        return dec && dec->unlimited == DECIMAL_BIGINT;
    }
    Type actual = {.type_id = type_id};
    if (type_id == LMD_TYPE_NUM_SIZED) actual.kind = item.get_num_type();
    return numeric_type_subsumes(&actual, &TYPE_INTEGER);
}

static Type* runtime_boundary_unwrap_type(Type* type) {
    while (type && type->type_id == LMD_TYPE_TYPE &&
            !type_is_global_meta_type(type) && type->kind == TYPE_KIND_SIMPLE) {
        // Global meta-types have only the compact prefix; no TypeType payload
        // exists to unwrap until that invariant has been established.
        Type* inner = ((TypeType*)type)->type;
        if (!inner) break;
        type = inner;
    }
    if (type && type->type_id == LMD_TYPE_TYPE &&
            type->kind == TYPE_KIND_CONSTRAINED) {
        Type* base = ((TypeConstrained*)type)->base;
        if (base) return runtime_boundary_unwrap_type(base);
    }
    return type;
}

static bool runtime_validate_value_against_type(Item item, Type* expected,
        ValidationResult** validation) {
    if (!context || !context->validator) return false;
    // A caller that wants no diagnostic wants a predicate: run the
    // allocation-free fast mode. A caller that passed `validation` is building
    // an error message, so it needs the reporting walk.
    bool want_report = (validation != NULL);
    context->validator->set_fast_mode(!want_report);
    ValidationResult* result = schema_validator_validate_type(context->validator,
        item.to_const(), expected);
    context->validator->set_fast_mode(false);
    if (validation) *validation = result;
    return result && result->valid;
}

static bool runtime_type_admit_value(Item value, Type* expected, Item* converted);

static bool literal_type_matches_item(Type* expected, Item item) {
    if (!expected || !expected->is_literal ||
            (expected->type_id != LMD_TYPE_STRING && expected->type_id != LMD_TYPE_SYMBOL)) {
        return false;
    }
    TypeId actual_id = get_type_id(item);
    if (actual_id != expected->type_id) return false;
    TypeString* literal = (TypeString*)expected;
    const char* actual_chars = item.get_chars();
    return literal->string && actual_chars &&
        literal->string->len == item.get_len() &&
        memcmp(literal->string->chars, actual_chars, item.get_len()) == 0;
}

bool lambda_type_matches(Item item, Type* expected) {
    expected = runtime_boundary_unwrap_type(expected);
    if (!expected) return false;
    TypeId actual_id = get_type_id(item);
    if (actual_id == LMD_TYPE_ERROR) return lambda_type_accepts_error(expected);
    if (expected->type_id == LMD_TYPE_ANY) return true;
    // Literal aliases used to enter the pattern builder; compare their content directly now that literal-only forms are ordinary type values.
    if (expected->is_literal &&
            (expected->type_id == LMD_TYPE_STRING || expected->type_id == LMD_TYPE_SYMBOL)) {
        return literal_type_matches_item(expected, item);
    }
    if (expected == &TYPE_MAP && actual_id == LMD_TYPE_VMAP) {
        // Host-backed VMaps implement Lambda's map interface but retain a distinct
        // physical tag; the global map contract must not reject them at a native call.
        return true;
    }

    if (expected->type_id == LMD_TYPE_TYPE && expected->kind == TYPE_KIND_PATTERN) {
        TypePattern* pattern = (TypePattern*)expected;
        if (pattern->is_symbol) {
            if (actual_id != LMD_TYPE_SYMBOL) return false;
        } else if (actual_id != LMD_TYPE_STRING) {
            return false;
        }
        return pattern_full_match_chars(pattern, item.get_chars(), item.get_len());
    }
    if (expected->type_id == LMD_TYPE_RANGE && expected->kind == TYPE_KIND_RANGE) {
        return range_type_contains_item((TypeRange*)expected, item);
    }
    if ((expected->type_id == LMD_TYPE_TYPE &&
            (expected->kind == TYPE_KIND_BINARY || expected->kind == TYPE_KIND_UNARY)) ||
            ((expected->type_id == LMD_TYPE_MAP || expected->type_id == LMD_TYPE_ELEMENT) &&
             expected != &TYPE_MAP && expected != &TYPE_ELMT)) {
        // TYPE_MAP and TYPE_ELMT are compact global Type prefixes, not shaped
        // TypeMap/TypeElmt payloads. Their tag match below is complete; casting
        // either singleton into the validator would read past its allocation.
        return runtime_validate_value_against_type(item, expected, NULL);
    }

    Type actual_scratch = {.type_id = LMD_TYPE_NULL};
    Type* actual_type = item_static_type_for_is(item, &actual_scratch);
    switch (expected->type_id) {
    case LMD_TYPE_TYPE:
        if (expected == &TYPE_NUMBER) return numeric_type_subsumes(actual_type, expected);
        if (expected == &TYPE_INTEGER) return item_type_is_integer_subtype(item, actual_id);
        return actual_id == LMD_TYPE_TYPE;
    case LMD_TYPE_INT:
    case LMD_TYPE_INT64:
    case LMD_TYPE_FLOAT:
    case LMD_TYPE_FLOAT64:
    case LMD_TYPE_DECIMAL:
    case LMD_TYPE_NUM_SIZED:
    case LMD_TYPE_UINT64:
        return numeric_type_subsumes(actual_type, expected);
    case LMD_TYPE_DTIME:
        if (actual_id != LMD_TYPE_DTIME) return false;
        if (expected == &TYPE_DATE) {
            DateTime dt = item.get_datetime();
            return dt.precision == DATETIME_PRECISION_DATE_ONLY ||
                dt.precision == DATETIME_PRECISION_YEAR_ONLY;
        }
        if (expected == &TYPE_TIME) {
            return item.get_datetime().precision == DATETIME_PRECISION_TIME_ONLY;
        }
        return true;
    case LMD_TYPE_ARRAY:
        return actual_id == LMD_TYPE_RANGE || actual_id == LMD_TYPE_ARRAY ||
            actual_id == LMD_TYPE_ARRAY_NUM;
    case LMD_TYPE_OBJECT:
        if (expected == &TYPE_OBJECT) return actual_id == LMD_TYPE_OBJECT;
        if (actual_id != LMD_TYPE_OBJECT) return false;
        {
            Object* obj = (Object*)(uintptr_t)item.item;
            TypeObject* actual_object = obj ? (TypeObject*)obj->type : NULL;
            TypeObject* wanted = (TypeObject*)expected;
            for (TypeObject* walk = actual_object; walk; walk = walk->base) {
                if (walk == wanted) {
                    return !wanted->constraint_fn || wanted->constraint_fn(item.item);
                }
            }
            return false;
        }
    default:
        return actual_id == expected->type_id;
    }
}

static void runtime_value_summary(Item item, char* buffer, size_t capacity) {
    TypeId type_id = get_type_id(item);
    if (type_id == LMD_TYPE_STRING || type_id == LMD_TYPE_SYMBOL) {
        const char* chars = item.get_chars();
        uint32_t len = item.get_len();
        int shown = len > 80 ? 80 : (int)len;
        snprintf(buffer, capacity, "%s '%.*s%s'", get_type_name(type_id), shown,
            chars ? chars : "", len > 80 ? "…" : "");
        return;
    }
    if (type_id == LMD_TYPE_INT) {
        snprintf(buffer, capacity, "int %lld", (long long)lambda_int_item_to_i64(item));
        return;
    }
    if (type_id == LMD_TYPE_FLOAT || type_id == LMD_TYPE_FLOAT64) {
        snprintf(buffer, capacity, "float %.17g", item.get_double());
        return;
    }
    snprintf(buffer, capacity, "%s", get_type_name(type_id));
}

static void runtime_validation_detail(ValidationResult* result, char* buffer,
        size_t capacity) {
    if (!result || !result->errors || !buffer || capacity == 0) return;
    ValidationError* error = result->errors;
    const char* message = error->message ? error->message->chars : "validation failed";
    if (error->path && context && context->pool) {
        String* path = format_validation_path(error->path, context->pool);
        snprintf(buffer, capacity, "; validator at %s: %s",
            path ? path->chars : "<value>", message);
        return;
    }
    snprintf(buffer, capacity, "; validator: %s", message);
}

static Item lambda_type_error_with_validation(Item actual, Type* expected,
        const char* boundary, ValidationResult* validation) {
    char expected_name[128];
    char actual_summary[192];
    char message[512];
    char validation_detail[320] = {};
    lambda_type_format_name(expected, expected_name, sizeof(expected_name));
    runtime_value_summary(actual, actual_summary, sizeof(actual_summary));
    runtime_validation_detail(validation, validation_detail, sizeof(validation_detail));
    snprintf(message, sizeof(message),
        "type check at %s failed: expected %s, got %s%s",
        boundary ? boundary : "typed boundary", expected_name, actual_summary,
        validation_detail);
    set_runtime_error(ERR_TYPE_MISMATCH, "%s", message);
    if (context && context->heap && context->heap->gc) {
        SourceLocation loc = {0};
        if (context->current_file) loc.file = context->current_file;
        LambdaError* error = err_create_heap(ERR_TYPE_MISMATCH, message, &loc);
        if (error) {
            error->raw_stack_trace = err_capture_raw_stack_trace(context->debug_info, 32);
            return err2it(error);
        }
    }
    return ItemError;
}

Item lambda_type_error(Item actual, Type* expected, const char* boundary) {
    return lambda_type_error_with_validation(actual, expected, boundary, NULL);
}

Item lambda_type_check(Item value, Type* expected, const char* boundary) {
    // Error-transparent contracts (`any`, `error`, or an explicit union) keep
    // an incoming error value.  Plain T short-circuits it unchanged so callers
    // retain the original diagnostic instead of receiving a misleading second
    // type mismatch.
    if (get_type_id(value) == LMD_TYPE_ERROR) return value;

    Item converted = ItemError;
    if (runtime_type_admit_value(value, expected, &converted)) {
        return converted;
    }

    // Shape/union/occurrence matching is delegated to the validator. Reuse its
    // first concrete failure here so a rejected dynamic binding identifies the
    // offending member/index instead of merely saying "got map".
    Type* contract = runtime_boundary_unwrap_type(expected);
    ValidationResult* validation = NULL;
    if (contract && ((contract->type_id == LMD_TYPE_TYPE &&
            (contract->kind == TYPE_KIND_BINARY || contract->kind == TYPE_KIND_UNARY)) ||
            contract->type_id == LMD_TYPE_MAP ||
            contract->type_id == LMD_TYPE_ELEMENT)) {
        (void)runtime_validate_value_against_type(value, contract, &validation);
    }
    return lambda_type_error_with_validation(value, expected, boundary, validation);
}

Bool fn_is(Item a, Item b) {
    TypeId b_type_id = get_type_id(b);
    if (b_type_id == LMD_TYPE_RANGE) {
        // A runtime range in `is` is the value-space set denoted by its bounds;
        // comparing the container identity would discard the range predicate.
        return range_contains_item(b.range, a) ? BOOL_TRUE : BOOL_FALSE;
    }
    if (b_type_id != LMD_TYPE_TYPE) return fn_eq(a, b);

    Type* b_type = b.type;
    if (b_type->kind == TYPE_KIND_PATTERN) {
        TypeId a_type_id = get_type_id(a);
        if (!is_text_type_id(a_type_id)) {
            log_error("pattern matching requires string or symbol, got type: %s", get_type_name(a_type_id));
            return BOOL_ERROR;
        }
        TypePattern* pattern = (TypePattern*)b_type;
        // pattern tags carry the value domain; content alone is not enough to
        // make a string and symbol type interchangeable under `is`.
        if ((pattern->is_symbol && a_type_id != LMD_TYPE_SYMBOL) ||
                (!pattern->is_symbol && a_type_id != LMD_TYPE_STRING)) {
            return BOOL_FALSE;
        }
        return pattern_full_match_chars(pattern, a.get_chars(), a.get_len()) ? BOOL_TRUE : BOOL_FALSE;
    }

    // Constraints are intentionally base-type-only until validator predicate
    // evaluation ships. `is` retains that interim rule independently from
    // assignment-boundary matching, where runtime validation owns diagnostics.
    if (b_type->kind == TYPE_KIND_CONSTRAINED) {
        TypeConstrained* constrained = (TypeConstrained*)b_type;
        TypeId a_type_id = get_type_id(a);
        Type* base = constrained->base;
        if (base->type_id != LMD_TYPE_TYPE) {
            if (a_type_id != base->type_id) {
                Type actual_scratch = {};
                Type* actual_type = item_static_type_for_is(a, &actual_scratch);
                if (!numeric_type_subsumes(actual_type, base)) return BOOL_FALSE;
            }
        }
        return BOOL_TRUE;
    }

    if (b_type->kind == TYPE_KIND_UNARY || b_type->kind == TYPE_KIND_BINARY) {
        ValidationResult* result = schema_validator_validate_type(context->validator, a.to_const(), b_type);
        return result->valid ? BOOL_TRUE : BOOL_FALSE;
    }

    TypeType* type_b = (TypeType*)b_type;
    if (type_b->type && type_b->type->type_id == LMD_TYPE_RANGE &&
            type_b->type->kind == TYPE_KIND_RANGE) {
        return range_type_contains_item((TypeRange*)type_b->type, a)
            ? BOOL_TRUE : BOOL_FALSE;
    }
    if (type_b->type->is_literal &&
            (type_b->type->type_id == LMD_TYPE_STRING || type_b->type->type_id == LMD_TYPE_SYMBOL)) {
        return literal_type_matches_item(type_b->type, a) ? BOOL_TRUE : BOOL_FALSE;
    }
    TypeId a_type_id = get_type_id(a);
    Type actual_type_scratch = {.type_id = LMD_TYPE_NULL};
    Type* actual_type = item_static_type_for_is(a, &actual_type_scratch);
    if (type_b->type->type_id != LMD_TYPE_NUM_SIZED &&
            (type_b->type->kind == TYPE_KIND_UNARY || type_b->type->kind == TYPE_KIND_BINARY)) {
        ValidationResult* result = schema_validator_validate_type(context->validator, a.to_const(), type_b->type);
        return result->valid ? BOOL_TRUE : BOOL_FALSE;
    }

    switch (type_b->type->type_id) {
    case LMD_TYPE_ANY:
        // `is any` uses the validator form of top: errors remain inspectable
        // values, but are not ordinary validated data.
        return a_type_id == LMD_TYPE_ERROR ? BOOL_FALSE : BOOL_TRUE;
    case LMD_TYPE_TYPE:
        if (type_b->type == &TYPE_NUMBER) {
            return numeric_type_subsumes(actual_type, type_b->type) ? BOOL_TRUE : BOOL_FALSE;
        }
        if (type_b->type == &TYPE_INTEGER) {
            if (a_type_id == LMD_TYPE_TYPE) {
                return numeric_type_subsumes(actual_type, type_b->type) ? BOOL_TRUE : BOOL_FALSE;
            }
            return item_type_is_integer_subtype(a, a_type_id) ? BOOL_TRUE : BOOL_FALSE;
        }
        return a_type_id == LMD_TYPE_TYPE ? BOOL_TRUE : BOOL_FALSE;
    case LMD_TYPE_INT:
    case LMD_TYPE_INT64:
    case LMD_TYPE_FLOAT:
    case LMD_TYPE_FLOAT64:
    case LMD_TYPE_DECIMAL:
    case LMD_TYPE_NUM_SIZED:
    case LMD_TYPE_UINT64:
        return numeric_type_subsumes(actual_type, type_b->type) ? BOOL_TRUE : BOOL_FALSE;
    case LMD_TYPE_DTIME:
        if (a_type_id != LMD_TYPE_DTIME) return BOOL_FALSE;
        if (type_b->type == &TYPE_DATE) {
            DateTime dt = a.get_datetime();
            return (dt.precision == DATETIME_PRECISION_DATE_ONLY || dt.precision == DATETIME_PRECISION_YEAR_ONLY)
                ? BOOL_TRUE : BOOL_FALSE;
        }
        if (type_b->type == &TYPE_TIME) {
            DateTime dt = a.get_datetime();
            return dt.precision == DATETIME_PRECISION_TIME_ONLY ? BOOL_TRUE : BOOL_FALSE;
        }
        return BOOL_TRUE;
    case LMD_TYPE_ARRAY:
    case LMD_TYPE_MAP:
    case LMD_TYPE_ELEMENT:
    case LMD_TYPE_OBJECT:
        if (type_b == &LIT_TYPE_ARRAY) {
            return a_type_id == LMD_TYPE_RANGE || a_type_id == LMD_TYPE_ARRAY ||
                a_type_id == LMD_TYPE_ARRAY_NUM ? BOOL_TRUE : BOOL_FALSE;
        }
        if (type_b == &LIT_TYPE_LIST) return BOOL_FALSE;
        if (type_b->type->type_id == LMD_TYPE_OBJECT && type_b->type != &TYPE_OBJECT) {
            if (a_type_id != LMD_TYPE_OBJECT) return BOOL_FALSE;
            Object* obj = (Object*)(uintptr_t)a.item;
            TypeObject* walk = (TypeObject*)obj->type;
            TypeObject* expected = (TypeObject*)type_b->type;
            bool nominal_match = false;
            while (walk) {
                if (walk == expected) { nominal_match = true; break; }
                walk = walk->base;
            }
            if (!nominal_match) return BOOL_FALSE;
            return (!expected->constraint_fn || expected->constraint_fn(a.item)) ? BOOL_TRUE : BOOL_FALSE;
        }
        if (type_b->type == &TYPE_OBJECT) {
            return a_type_id == LMD_TYPE_OBJECT ? BOOL_TRUE : BOOL_FALSE;
        }
        {
            ValidationResult* result = schema_validator_validate_type(context->validator, a.to_const(), type_b->type);
            return result->valid ? BOOL_TRUE : BOOL_FALSE;
        }
    default:
        return a_type_id == type_b->type->type_id ? BOOL_TRUE : BOOL_FALSE;
    }
}

// IEEE NaN check: expr is nan
Bool fn_is_nan(Item a) {
    TypeId tid = get_type_id(a);
    if (is_float_type_id(tid)) {
        double val = a.get_double();
        return __builtin_isnan(val) ? BOOL_TRUE : BOOL_FALSE;
    }
    // `int` and `float` share one nan (formal semantics 4), and a shared value
    // types as its narrowest -- so a nan reports `int` and would miss the float
    // arm above. Its native form is the same double either way.
    if (tid == LMD_TYPE_INT) {
        return __builtin_isnan(lambda_int_item_value(a)) ? BOOL_TRUE : BOOL_FALSE;
    }
    if (tid == LMD_TYPE_DECIMAL) return decimal_item_is_nan(a) ? BOOL_TRUE : BOOL_FALSE;
    return BOOL_FALSE;  // non-float values are never NaN
}

// maximum recursion depth for structural equality comparison
#define EQ_MAX_DEPTH 256

// forward declaration for recursive structural equality
static Bool fn_eq_depth(Item a_item, Item b_item, int depth);

static Bool numeric_items_equal_exact(Item a_item, Item b_item) {
    LambdaNumericComparison comparison = lambda_numeric_compare(a_item, b_item);
    return comparison.valid && !comparison.unordered && comparison.order == 0 ?
        BOOL_TRUE : BOOL_FALSE;
}

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

static bool array_num_shape_eq(ArrayNum* a, ArrayNum* b) {
    int a_ndim = 1, b_ndim = 1;
    int64_t a_dims_stack[32], b_dims_stack[32];
    a_dims_stack[0] = a ? a->length : 0;
    b_dims_stack[0] = b ? b->length : 0;

    if (a && a->is_ndim && a->extra) {
        ArrayNumShape* s = (ArrayNumShape*)(uintptr_t)a->extra;
        if (s && s->ndim >= 1 && s->ndim <= 32) {
            a_ndim = s->ndim;
            int64_t* dims = array_num_shape_dims(s);
            for (int i = 0; i < a_ndim; i++) a_dims_stack[i] = dims[i];
        }
    }
    if (b && b->is_ndim && b->extra) {
        ArrayNumShape* s = (ArrayNumShape*)(uintptr_t)b->extra;
        if (s && s->ndim >= 1 && s->ndim <= 32) {
            b_ndim = s->ndim;
            int64_t* dims = array_num_shape_dims(s);
            for (int i = 0; i < b_ndim; i++) b_dims_stack[i] = dims[i];
        }
    }

    if (a_ndim != b_ndim) return false;
    for (int i = 0; i < a_ndim; i++) {
        if (a_dims_stack[i] != b_dims_stack[i]) return false;
    }
    return true;
}

static inline bool array_num_elem_type_is_float(ArrayNumElemType et) {
    return et == ELEM_FLOAT16 || et == ELEM_FLOAT32 || et == ELEM_FLOAT64;
}

// helper: structural equality for typed numeric arrays
static Bool array_num_eq(ArrayNum* a, ArrayNum* b, int depth) {
    if (a == b) return BOOL_TRUE;
    if (a->length != b->length) return BOOL_FALSE;
    if (!array_num_shape_eq(a, b)) {
        // n-D arrays expose their shape as structure; equal flat storage alone is insufficient.
        return BOOL_FALSE;
    }
    if (a->get_elem_type() != b->get_elem_type()) {
        // different elem types compare as values; double promotion loses high int64/u64 bits.
        for (int64_t i = 0; i < a->length; i++) {
            Item val_a = array_num_read_item(a, i);
            Item val_b = array_num_read_item(b, i);
            Bool r = fn_eq_depth(val_a, val_b, depth + 1);
            if (r != BOOL_TRUE) return r;
        }
        return BOOL_TRUE;
    }
    if (a->get_elem_type() == ELEM_FLOAT64) {
        // element-wise comparison to respect NaN != NaN
        for (int64_t i = 0; i < a->length; i++) {
            if (a->float_items[i] != b->float_items[i]) return BOOL_FALSE;
        }
        return BOOL_TRUE;
    }
    if (array_num_elem_type_is_float(a->get_elem_type())) {
        for (int64_t i = 0; i < a->length; i++) {
            Item val_a = array_num_read_item(a, i);
            Item val_b = array_num_read_item(b, i);
            Bool r = fn_eq_depth(val_a, val_b, depth + 1);
            if (r != BOOL_TRUE) return r;
        }
        return BOOL_TRUE;
    }
    uint8_t elem_size = ELEM_TYPE_SIZE[a->get_elem_type() >> 4];
    return (elem_size && memcmp(a->data, b->data, (size_t)a->length * elem_size) == 0) ? BOOL_TRUE : BOOL_FALSE;
}

static ShapeEntry* map_find_matching_field(TypeMap* map_type, ShapeEntry* needle) {
    if (!map_type || !needle || !needle->name) return NULL;
    ShapeEntry* hit = typemap_hash_lookup(map_type, needle->name->str, (int)needle->name->length);
    if (hit && target_equal(needle->ns, hit->ns)) return hit;

    // hash lookup is name-only; namespace collisions must fall back to the full shape chain.
    FOR_EACH_MAP_FIELD(map_type, field) {
        if (strview_equal(needle->name, field->name->str) &&
            target_equal(needle->ns, field->ns)) {
            return field;
        }
    }
    return NULL;
}

// helper: structural equality for map-shaped data (order-independent key-value comparison)
static Bool map_data_eq(TypeMap* type_a, void* data_a, TypeMap* type_b, void* data_b, int depth) {
    if (type_a == type_b && data_a == data_b) return BOOL_TRUE;  // pointer identity fast-path
    if (!type_a || !type_b) return BOOL_FALSE;
    if (type_a->length != type_b->length) return BOOL_FALSE;

    // same-shape fast path: keys are identical in same order, compare values by slot
    if (type_a == type_b) {
        FOR_EACH_MAP_FIELD(type_a, field) {
            Item val_a = _map_field_value(type_a, data_a, field);
            Item val_b = _map_field_value(type_b, data_b, field);
            Bool r = fn_eq_depth(val_a, val_b, depth + 1);
            if (r == BOOL_ERROR) return BOOL_ERROR;
            if (r == BOOL_FALSE) return BOOL_FALSE;
        }
        return BOOL_TRUE;
    }

    // different shapes: for each key in A, look up in B by name+namespace
    FOR_EACH_MAP_FIELD(type_a, field_a) {
        ShapeEntry* field_b = map_find_matching_field(type_b, field_a);
        if (!field_b) return BOOL_FALSE;

        Item val_a = _map_field_value(type_a, data_a, field_a);
        Item val_b = _map_field_value(type_b, data_b, field_b);
        Bool r = fn_eq_depth(val_a, val_b, depth + 1);
        if (r == BOOL_ERROR) return BOOL_ERROR;
        if (r == BOOL_FALSE) return BOOL_FALSE;
    }
    // key-count check already done above — no extra keys in B
    return BOOL_TRUE;
}

// helper: structural equality for maps (order-independent key-value comparison)
static Bool map_eq(Map* a, Map* b, int depth) {
    if (a == b) return BOOL_TRUE;  // pointer identity fast-path
    return map_data_eq((TypeMap*)a->type, a->data, (TypeMap*)b->type, b->data, depth);
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

    // Element stores list fields before attr type/data, so attrs must compare by explicit attr storage.
    Bool attr_r = map_data_eq((TypeMap*)type_a, a->data, (TypeMap*)type_b, b->data, depth);
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
    Item a_item = {.vmap = a};
    Item b_item = {.vmap = b};
    // Handle equality is capability identity; two empty handle carriers must
    // never collapse to structural VMap equality.
    if (lambda_task_handle_is(a_item) || lambda_task_handle_is(b_item)) return BOOL_FALSE;
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

// helper: get element at index from any sequence type (list, array, range)
static inline Item seq_get_element(Item item, TypeId tid, int64_t i) {
    switch (tid) {
    case LMD_TYPE_ARRAY:        return array_get(item.array, i);
    case LMD_TYPE_ARRAY_NUM:   return array_num_get(item.array_num, i);
    case LMD_TYPE_RANGE:
        return item.range->is_char
            ? fn_chr((Item){.item = i2it(item.range->start + i)})
            : (Item){.item = i2it(item.range->start + i)};
    default:                   return ItemNull;
    }
}

// helper: get length from any sequence type
// For N-D ArrayNum, returns shape[0] (leading-axis count, NumPy-compatible).
// 1-D ArrayNum returns total length.
static inline int64_t seq_get_length(Item item, TypeId tid) {
    switch (tid) {
    case LMD_TYPE_ARRAY:        return item.array->length;
    case LMD_TYPE_ARRAY_NUM:   return array_num_iter_count(item.array_num);
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

static Bool function_eq(Function* a, Function* b, int depth) {
    if (a == b) return BOOL_TRUE;
    if (!a || !b) return BOOL_FALSE;
    if (a->ptr != b->ptr || a->arity != b->arity || a->entry_abi != b->entry_abi ||
            a->returns_ret_item != b->returns_ret_item ||
            a->has_kwargs != b->has_kwargs ||
            a->is_generator != b->is_generator ||
            a->is_coroutine != b->is_coroutine ||
            a->is_system_function_ref != b->is_system_function_ref ||
            a->requires_scalar_result_home != b->requires_scalar_result_home ||
            a->requires_runtime_context != b->requires_runtime_context) return BOOL_FALSE;
    if (a->closure_field_count != b->closure_field_count) return BOOL_FALSE;
    if (a->closure_field_count > 0) {
        if (!a->closure_env || !b->closure_env) return BOOL_FALSE;
        Item* a_fields = (Item*)a->closure_env;
        Item* b_fields = (Item*)b->closure_env;
        for (uint8_t i = 0; i < a->closure_field_count; i++) {
            Bool r = fn_eq_depth(a_fields[i], b_fields[i], depth + 1);
            if (r != BOOL_TRUE) return r;
        }
        return BOOL_TRUE;
    }
    if (a->closure_env || b->closure_env) {
        // bound methods store the receiver as closure_env without a field count.
        return (a->closure_env == b->closure_env) ? BOOL_TRUE : BOOL_FALSE;
    }
    return BOOL_TRUE;
}

// 3-states comparison with depth tracking for structural equality
static Bool fn_eq_depth(Item a_item, Item b_item, int depth) {
    if (depth > EQ_MAX_DEPTH) {
        // Equality depth is a language-visible runtime failure, not a native
        // fault. Return the existing three-state error lane so every caller
        // unwinds through its own completion path (D1.4v3/DI15v2).
        set_runtime_error(ERR_RUNTIME_ERROR,
            "structural equality recursion too deep (> %d)", EQ_MAX_DEPTH);
        return BOOL_ERROR;
    }

    TypeId a_type_id = get_type_id(a_item);
    TypeId b_type_id = get_type_id(b_item);
    if (a_type_id == LMD_TYPE_COMPLEX || b_type_id == LMD_TYPE_COMPLEX) {
        if (a_type_id == LMD_TYPE_COMPLEX && b_type_id == LMD_TYPE_COMPLEX) {
            Complex* a = a_item.get_complex();
            Complex* b = b_item.get_complex();
            if (!a || !b || isnan(a->real) || isnan(a->imag) ||
                    isnan(b->real) || isnan(b->imag)) return BOOL_FALSE;
            return (a->real == b->real && a->imag == b->imag) ? BOOL_TRUE : BOOL_FALSE;
        }
        Item complex_item = a_type_id == LMD_TYPE_COMPLEX ? a_item : b_item;
        Item real_item = a_type_id == LMD_TYPE_COMPLEX ? b_item : a_item;
        Complex* value = complex_item.get_complex();
        if (!value || value->imag != 0.0 || isnan(value->real) ||
                isnan(value->imag) || !is_numeric_type_id(get_type_id(real_item))) {
            return BOOL_FALSE;
        }
        // Native evaluator helpers use the TLS-only RootFrame ABI.
        RootFrame roots(2);
        Rooted<Item> rooted_complex(roots, complex_item);
        Rooted<Item> rooted_real(roots, real_item);
        value = rooted_complex.get().get_complex();
        // push_d() may collect before comparing an exact decimal or wide-integer peer.
        return value ? numeric_items_equal_exact(push_d(value->real), rooted_real.get()) : BOOL_FALSE;
    }
    if (IS_NUMERIC_ID(a_type_id) && IS_NUMERIC_ID(b_type_id)) {
        return numeric_items_equal_exact(a_item, b_item);
    }
    if (a_type_id != b_type_id) {
        // special case: type(null) == null, type(error) == error
        // when one side is a type value and the other is null/error,
        // compare the type's inner type_id against the value's type_id
        if (a_type_id == LMD_TYPE_NULL || b_type_id == LMD_TYPE_NULL ||
            a_type_id == LMD_TYPE_ERROR || b_type_id == LMD_TYPE_ERROR) {
            // check if either side is a type value (container pointer with type_id == LMD_TYPE_TYPE)
            Item type_item, value_item;
            if (a_type_id == LMD_TYPE_TYPE) {
                type_item = a_item; value_item = b_item;
            } else if (b_type_id == LMD_TYPE_TYPE) {
                type_item = b_item; value_item = a_item;
            } else {
                // neither side is a type value — use existing null/error semantics
                return BOOL_FALSE;
            }
            // compare type's inner type_id against the value's type_id
            TypeType* tt = (TypeType*)type_item.type;
            return (tt->type->type_id == get_type_id(value_item)) ? BOOL_TRUE : BOOL_FALSE;
        }
        // cross-type sequence comparison: range, list, and array types
        // all represent ordered sequences and can be compared element-wise
        TypeId a_tid = a_type_id;
        TypeId b_tid = b_type_id;
        bool a_is_seq = (a_tid == LMD_TYPE_ARRAY ||
                         a_tid == LMD_TYPE_ARRAY_NUM || a_tid == LMD_TYPE_RANGE);
        bool b_is_seq = (b_tid == LMD_TYPE_ARRAY ||
                         b_tid == LMD_TYPE_ARRAY_NUM || b_tid == LMD_TYPE_RANGE);
        if (a_is_seq && b_is_seq) {
            return cross_seq_eq(a_item, a_tid, b_item, b_tid, depth);
        }

        // equality is total across families; incompatible concrete families are unequal.
        return BOOL_FALSE;
    }

    if (a_type_id == LMD_TYPE_NULL) {
        return BOOL_TRUE; // null == null
    }
    else if (a_type_id == LMD_TYPE_ERROR) {
        // error values are poison for equality: even two errors are not equal.
        return BOOL_FALSE;
    }
    else if (a_type_id == LMD_TYPE_BOOL) {
        return (a_item.bool_val == b_item.bool_val) ? BOOL_TRUE : BOOL_FALSE;
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

        if (a_tid == LMD_TYPE_ERROR || b_tid == LMD_TYPE_ERROR) {
            // error containers must not become equal through pointer identity.
            return BOOL_FALSE;
        }

        // pointer identity fast-path for all container types
        if (a_item.item == b_item.item) return BOOL_TRUE;

        if (a_tid == LMD_TYPE_PATH && b_tid == LMD_TYPE_PATH) {
            return path_equal(a_item.path, b_item.path) ? BOOL_TRUE : BOOL_FALSE;
        }

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
            bool a_is_seq = (a_tid == LMD_TYPE_ARRAY ||
                             a_tid == LMD_TYPE_ARRAY_NUM || a_tid == LMD_TYPE_RANGE);
            bool b_is_seq = (b_tid == LMD_TYPE_ARRAY ||
                             b_tid == LMD_TYPE_ARRAY_NUM || b_tid == LMD_TYPE_RANGE);
            if (a_is_seq && b_is_seq) {
                return cross_seq_eq(a_item, a_tid, b_item, b_tid, depth);
            }
            return BOOL_FALSE;
        }

        // list structural equality
        if (a_tid == LMD_TYPE_ARRAY) {
            return list_eq(a_item.array, b_item.array, depth);
        }
        // generic array (array of Items) structural equality
        if (a_tid == LMD_TYPE_ARRAY) {
            return list_eq((List*)a_item.array, (List*)b_item.array, depth);
        }
        // typed array equality
        if (a_tid == LMD_TYPE_ARRAY_NUM) {
            return array_num_eq(a_item.array_num, b_item.array_num, depth);
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
            return (ra->is_char == rb->is_char && ra->start == rb->start &&
                    ra->end == rb->end && ra->length == rb->length)
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
            return function_eq(a_item.function, b_item.function, depth);
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

static int total_type_rank(Item item) {
    TypeId tid = get_type_id(item);
    if (is_float_type_id(tid) && isnan(item.get_double())) return 14;
    if (tid == LMD_TYPE_DECIMAL && decimal_item_is_nan(item)) return 14;
    switch (tid) {
    case LMD_TYPE_NULL: return 0;
    case LMD_TYPE_BOOL: return item.bool_val ? 2 : 1;
    case LMD_TYPE_INT: case LMD_TYPE_INT64: case LMD_TYPE_FLOAT: case LMD_TYPE_FLOAT64:
    case LMD_TYPE_DECIMAL: case LMD_TYPE_NUM_SIZED: case LMD_TYPE_UINT64:
    case LMD_TYPE_COMPLEX:
        return 3;
    case LMD_TYPE_DTIME: return 4;
    case LMD_TYPE_SYMBOL: return 5;
    case LMD_TYPE_STRING: return 6;
    case LMD_TYPE_BINARY: return 7;
    case LMD_TYPE_RANGE: case LMD_TYPE_ARRAY: case LMD_TYPE_ARRAY_NUM:
        return 8;
    case LMD_TYPE_MAP: case LMD_TYPE_VMAP:
        return 9;
    case LMD_TYPE_OBJECT:
        return 10;
    case LMD_TYPE_ELEMENT:
        return 11;
    case LMD_TYPE_TYPE:
        return 12;
    case LMD_TYPE_FUNC:
        return 13;
    case LMD_TYPE_ERROR:
        return 15;
    default:
        return 16;
    }
}

static int total_complex_component_cmp(double left, double right) {
    if (isnan(left) || isnan(right)) {
        if (isnan(left) && isnan(right)) {
            uint64_t left_bits, right_bits;
            __builtin_memcpy(&left_bits, &left, sizeof(left_bits));
            __builtin_memcpy(&right_bits, &right, sizeof(right_bits));
            return (left_bits > right_bits) - (left_bits < right_bits);
        }
        return isnan(left) ? 1 : -1;
    }
    return left < right ? -1 : left > right ? 1 : 0;
}

static int total_complex_cmp(Item a_item, Item b_item) {
    TypeId a_tid = get_type_id(a_item);
    TypeId b_tid = get_type_id(b_item);
    bool a_complex = a_tid == LMD_TYPE_COMPLEX;
    bool b_complex = b_tid == LMD_TYPE_COMPLEX;
    Complex* a = a_complex ? a_item.get_complex() : NULL;
    Complex* b = b_complex ? b_item.get_complex() : NULL;
    bool a_real = !a_complex || (a && a->imag == 0.0);
    bool b_real = !b_complex || (b && b->imag == 0.0);
    if (a_real && b_real) {
        RootFrame roots(2);
        Rooted<Item> rooted_a(roots, a_item);
        Rooted<Item> rooted_b(roots, b_item);
        a = a_complex ? rooted_a.get().get_complex() : NULL;
        b = b_complex ? rooted_b.get().get_complex() : NULL;
        Item ar = a_complex ? push_d(a->real) : rooted_a.get();
        Item br = b_complex ? push_d(b->real) : rooted_b.get();
        LambdaNumericComparison comparison = lambda_numeric_compare(ar, br);
        return comparison.valid && !comparison.unordered ? comparison.order : 0;
    }
    if (a_real != b_real) return a_real ? -1 : 1;
    int real_cmp = total_complex_component_cmp(a->real, b->real);
    return real_cmp ? real_cmp : total_complex_component_cmp(a->imag, b->imag);
}

static int total_byte_cmp(Item a_item, Item b_item) {
    const char* chars_a = a_item.get_chars();
    const char* chars_b = b_item.get_chars();
    uint32_t len_a = a_item.get_len();
    uint32_t len_b = b_item.get_len();
    uint32_t min_len = len_a < len_b ? len_a : len_b;
    int cmp = min_len ? memcmp(chars_a, chars_b, min_len) : 0;
    if (cmp != 0) return cmp < 0 ? -1 : 1;
    return (len_a > len_b) - (len_a < len_b);
}

static int shape_entry_name_cmp(ShapeEntry* a, ShapeEntry* b) {
    if (!a && !b) return 0;
    if (!a) return -1;
    if (!b) return 1;
    int ns_cmp = (a->ns > b->ns) - (a->ns < b->ns);
    if (ns_cmp != 0) return ns_cmp;
    const char* a_chars = a->name ? a->name->str : "";
    const char* b_chars = b->name ? b->name->str : "";
    int a_len = a->name ? (int)a->name->length : 0;
    int b_len = b->name ? (int)b->name->length : 0;
    int min_len = a_len < b_len ? a_len : b_len;
    int cmp = min_len ? memcmp(a_chars, b_chars, (size_t)min_len) : 0;
    if (cmp != 0) return cmp < 0 ? -1 : 1;
    return (a_len > b_len) - (a_len < b_len);
}

static int strview_total_cmp(StrView a, StrView b) {
    size_t min_len = a.length < b.length ? a.length : b.length;
    int cmp = min_len ? memcmp(a.str, b.str, min_len) : 0;
    if (cmp != 0) return cmp < 0 ? -1 : 1;
    return (a.length > b.length) - (a.length < b.length);
}

static ShapeEntry* map_next_sorted_field(TypeMap* type, ShapeEntry* previous) {
    ShapeEntry* best = NULL;
    FOR_EACH_MAP_FIELD(type, field) {
        if (previous && shape_entry_name_cmp(field, previous) <= 0) continue;
        if (!best || shape_entry_name_cmp(field, best) < 0) best = field;
    }
    return best;
}

static int map_data_total_cmp(TypeMap* type_a, void* data_a, TypeMap* type_b, void* data_b) {
    int64_t len_a = type_a ? type_a->length : 0;
    int64_t len_b = type_b ? type_b->length : 0;
    int len_cmp = (len_a > len_b) - (len_a < len_b);
    if (len_cmp != 0) return len_cmp;

    ShapeEntry* prev_a = NULL;
    ShapeEntry* prev_b = NULL;
    for (int64_t i = 0; i < len_a; i++) {
        ShapeEntry* field_a = map_next_sorted_field(type_a, prev_a);
        ShapeEntry* field_b = map_next_sorted_field(type_b, prev_b);
        int key_cmp = shape_entry_name_cmp(field_a, field_b);
        if (key_cmp != 0) return key_cmp;
        int val_cmp = total_cmp(_map_field_value(type_a, data_a, field_a),
                                _map_field_value(type_b, data_b, field_b));
        if (val_cmp != 0) return val_cmp;
        prev_a = field_a;
        prev_b = field_b;
    }
    return 0;
}

int total_cmp(Item a_item, Item b_item) {
    TypeId total_a_tid = get_type_id(a_item);
    TypeId total_b_tid = get_type_id(b_item);
    if ((total_a_tid == LMD_TYPE_COMPLEX || total_b_tid == LMD_TYPE_COMPLEX) &&
            is_numeric_type_id(total_a_tid) && is_numeric_type_id(total_b_tid)) {
        return total_complex_cmp(a_item, b_item);
    }
    int rank_a = total_type_rank(a_item);
    int rank_b = total_type_rank(b_item);
    if (rank_a != rank_b) return (rank_a > rank_b) - (rank_a < rank_b);

    TypeId a_tid = get_type_id(a_item);
    TypeId b_tid = get_type_id(b_item);
    if (rank_a == 3) {
        LambdaNumericComparison comparison = lambda_numeric_compare(a_item, b_item);
        if (comparison.valid && !comparison.unordered) return comparison.order;
        // A nan is unordered against every number, so the ORDER has to place it
        // by fiat or it is not an order at all: answering 0 made the relation
        // intransitive (1 == nan and nan == 3, yet 1 != 3), and a sort over an
        // array containing one then returned whatever its traversal happened to
        // produce. Nans sort after all numbers, as IEEE's own totalOrder does,
        // and are equivalent among themselves. Section 6's order is total; this
        // is where that is made true for poison.
        bool a_nan = fn_is_nan(a_item) == BOOL_TRUE;
        bool b_nan = fn_is_nan(b_item) == BOOL_TRUE;
        if (a_nan != b_nan) return a_nan ? 1 : -1;
        return 0;
    }
    if (a_tid == LMD_TYPE_DTIME) {
        DateTime dt_a = a_item.get_datetime();
        DateTime dt_b = b_item.get_datetime();
        return datetime_compare(&dt_a, &dt_b);
    }
    if (a_tid == LMD_TYPE_SYMBOL) {
        Symbol* sym_a = a_item.get_symbol();
        Symbol* sym_b = b_item.get_symbol();
        int ns_cmp = (sym_a->ns > sym_b->ns) - (sym_a->ns < sym_b->ns);
        if (ns_cmp != 0) return ns_cmp;
        return total_byte_cmp(a_item, b_item);
    }
    if (a_tid == LMD_TYPE_STRING || a_tid == LMD_TYPE_BINARY) {
        return total_byte_cmp(a_item, b_item);
    }
    if (rank_a == 8) {
        int64_t len_a = seq_get_length(a_item, a_tid);
        int64_t len_b = seq_get_length(b_item, b_tid);
        int64_t min_len = len_a < len_b ? len_a : len_b;
        for (int64_t i = 0; i < min_len; i++) {
            int cmp = total_cmp(seq_get_element(a_item, a_tid, i), seq_get_element(b_item, b_tid, i));
            if (cmp != 0) return cmp;
        }
        return (len_a > len_b) - (len_a < len_b);
    }
    if (rank_a == 9) {
        bool a_handle = a_tid == LMD_TYPE_VMAP && lambda_task_handle_is(a_item);
        bool b_handle = b_tid == LMD_TYPE_VMAP && lambda_task_handle_is(b_item);
        if (a_handle || b_handle) {
            if (a_handle != b_handle) return a_handle ? 1 : -1;
            uintptr_t a_ptr = (uintptr_t)a_item.vmap;
            uintptr_t b_ptr = (uintptr_t)b_item.vmap;
            return (a_ptr > b_ptr) - (a_ptr < b_ptr);
        }
        if (a_tid == LMD_TYPE_VMAP || b_tid == LMD_TYPE_VMAP) {
            int64_t len_a = a_tid == LMD_TYPE_VMAP ? a_item.vmap->vtable->count(a_item.vmap->data)
                                                   : ((TypeMap*)a_item.map->type)->length;
            int64_t len_b = b_tid == LMD_TYPE_VMAP ? b_item.vmap->vtable->count(b_item.vmap->data)
                                                   : ((TypeMap*)b_item.map->type)->length;
            return (len_a > len_b) - (len_a < len_b);
        }
        return map_data_total_cmp((TypeMap*)a_item.map->type, a_item.map->data,
                                  (TypeMap*)b_item.map->type, b_item.map->data);
    }
    if (a_tid == LMD_TYPE_OBJECT) {
        return map_data_total_cmp((TypeMap*)a_item.object->type, a_item.object->data,
                                  (TypeMap*)b_item.object->type, b_item.object->data);
    }
    if (a_tid == LMD_TYPE_ELEMENT) {
        TypeElmt* type_a = (TypeElmt*)a_item.element->type;
        TypeElmt* type_b = (TypeElmt*)b_item.element->type;
        int tag_cmp = strview_total_cmp(type_a->name, type_b->name);
        if (tag_cmp != 0) return tag_cmp;
        int attr_cmp = map_data_total_cmp((TypeMap*)type_a, a_item.element->data,
                                          (TypeMap*)type_b, b_item.element->data);
        if (attr_cmp != 0) return attr_cmp;
        int64_t len_a = a_item.element->length;
        int64_t len_b = b_item.element->length;
        int64_t min_len = len_a < len_b ? len_a : len_b;
        for (int64_t i = 0; i < min_len; i++) {
            int child_cmp = total_cmp(a_item.element->items[i], b_item.element->items[i]);
            if (child_cmp != 0) return child_cmp;
        }
        return (len_a > len_b) - (len_a < len_b);
    }
    if (a_tid == LMD_TYPE_TYPE) {
        TypeType* at = (TypeType*)a_item.type;
        TypeType* bt = (TypeType*)b_item.type;
        TypeId aid = at && at->type ? at->type->type_id : LMD_TYPE_NULL;
        TypeId bid = bt && bt->type ? bt->type->type_id : LMD_TYPE_NULL;
        return (aid > bid) - (aid < bid);
    }
    return (a_item.item > b_item.item) - (a_item.item < b_item.item);
}

// 3-state value/ordered comparison
// Scalar 3-state ordered comparison (BOOL_TRUE/BOOL_FALSE/BOOL_ERROR).  The
// public fn_lt/fn_gt/fn_le/fn_ge wrappers keep symbolic comparisons scalar-only;
// explicit keyword operators route vector masks through vec_cmp.
static bool datetime_magnitude_comparable(DateTime* a, DateTime* b) {
    bool a_time = a->precision == DATETIME_PRECISION_TIME_ONLY;
    bool b_time = b->precision == DATETIME_PRECISION_TIME_ONLY;
    return a_time == b_time;
}

// Ordering outcome for a scalar pair. `UNORDERED` is kept distinct from `LT`
// so that `<=`/`>=` cannot be derived by negating `>`/`<`: with a NaN operand
// no ordered relation holds, and `!(a > b)` would wrongly report `a <= b`
// (Lambda_Formal_Semantics.md 6.1 — poison stays incomparable, IEEE).
typedef enum ScalarOrder {
    SCALAR_ORDER_LT,
    SCALAR_ORDER_EQ,
    SCALAR_ORDER_GT,
    SCALAR_ORDER_UNORDERED,
    SCALAR_ORDER_INVALID,
} ScalarOrder;

static ScalarOrder scalar_order(Item a_item, Item b_item) {
    TypeId a_type_id = get_type_id(a_item);
    TypeId b_type_id = get_type_id(b_item);
    if (IS_NUMERIC_ID(a_type_id) && IS_NUMERIC_ID(b_type_id)) {
        LambdaNumericComparison comparison = lambda_numeric_compare(a_item, b_item);
        if (!comparison.valid) return SCALAR_ORDER_INVALID;
        if (comparison.unordered) return SCALAR_ORDER_UNORDERED;
        return comparison.order < 0 ? SCALAR_ORDER_LT :
            comparison.order > 0 ? SCALAR_ORDER_GT : SCALAR_ORDER_EQ;
    }
    if (a_type_id != b_type_id) {
        // Type mismatch error for ordered comparisons
        return SCALAR_ORDER_INVALID;
    }

    if (a_type_id == LMD_TYPE_NULL) {
        return SCALAR_ORDER_INVALID;  // public wrappers absorb null before scalar comparison
    }
    else if (a_type_id == LMD_TYPE_BOOL) {
        return SCALAR_ORDER_INVALID;  // bool does not support <, >, <=, >=
    }
    else if (a_type_id == LMD_TYPE_DTIME) {
        DateTime dt_a = a_item.get_datetime();  DateTime dt_b = b_item.get_datetime();
        if (!datetime_magnitude_comparable(&dt_a, &dt_b)) return SCALAR_ORDER_INVALID;
        int cmp = datetime_compare(&dt_a, &dt_b);
        return cmp < 0 ? SCALAR_ORDER_LT : cmp > 0 ? SCALAR_ORDER_GT : SCALAR_ORDER_EQ;
    }
    else if (a_type_id == LMD_TYPE_STRING) {
        const char* chars_a = a_item.get_chars();  const char* chars_b = b_item.get_chars();
        uint32_t len_a = a_item.get_len();  uint32_t len_b = b_item.get_len();
        uint32_t min_len = len_a < len_b ? len_a : len_b;
        int cmp = min_len ? memcmp(chars_a, chars_b, min_len) : 0;
        // strings/binaries are length-prefixed; embedded NUL bytes participate in ordering.
        if (cmp != 0) return cmp < 0 ? SCALAR_ORDER_LT : SCALAR_ORDER_GT;
        return len_a < len_b ? SCALAR_ORDER_LT :
            len_a > len_b ? SCALAR_ORDER_GT : SCALAR_ORDER_EQ;
    }
    log_error("unknown comparing type: %s", get_type_name(a_type_id));
    return SCALAR_ORDER_INVALID;
}

// The four ordered relations all read the same ordering; an unordered pair
// satisfies none of them, and an invalid pair errors.
static inline Bool scalar_order_holds(ScalarOrder order, bool lt, bool eq, bool gt) {
    switch (order) {
    case SCALAR_ORDER_LT: return lt ? BOOL_TRUE : BOOL_FALSE;
    case SCALAR_ORDER_EQ: return eq ? BOOL_TRUE : BOOL_FALSE;
    case SCALAR_ORDER_GT: return gt ? BOOL_TRUE : BOOL_FALSE;
    case SCALAR_ORDER_UNORDERED: return BOOL_FALSE;
    default: return BOOL_ERROR;
    }
}

Bool fn_lt_scalar(Item a_item, Item b_item) {
    return scalar_order_holds(scalar_order(a_item, b_item), true, false, false);
}

Bool fn_gt_scalar(Item a_item, Item b_item) {
    return scalar_order_holds(scalar_order(a_item, b_item), false, false, true);
}

Bool fn_le_scalar(Item a_item, Item b_item) {
    return scalar_order_holds(scalar_order(a_item, b_item), true, true, false);
}

Bool fn_ge_scalar(Item a_item, Item b_item) {
    return scalar_order_holds(scalar_order(a_item, b_item), false, true, true);
}

Item fn_lt(Item a_item, Item b_item) {
    if (get_type_id(a_item) == LMD_TYPE_NULL || get_type_id(b_item) == LMD_TYPE_NULL)
        return ItemNull;
    Bool r = fn_lt_scalar(a_item, b_item);
    return (r == BOOL_ERROR) ? ItemError : (Item){ .item = b2it(r) };
}

Item fn_gt(Item a_item, Item b_item) {
    if (get_type_id(a_item) == LMD_TYPE_NULL || get_type_id(b_item) == LMD_TYPE_NULL)
        return ItemNull;
    Bool r = fn_gt_scalar(a_item, b_item);
    return (r == BOOL_ERROR) ? ItemError : (Item){ .item = b2it(r) };
}

Item fn_le(Item a_item, Item b_item) {
    if (get_type_id(a_item) == LMD_TYPE_NULL || get_type_id(b_item) == LMD_TYPE_NULL)
        return ItemNull;
    Bool r = fn_le_scalar(a_item, b_item);
    return (r == BOOL_ERROR) ? ItemError : (Item){ .item = b2it(r) };
}

Item fn_ge(Item a_item, Item b_item) {
    if (get_type_id(a_item) == LMD_TYPE_NULL || get_type_id(b_item) == LMD_TYPE_NULL)
        return ItemNull;
    Bool r = fn_ge_scalar(a_item, b_item);
    return (r == BOOL_ERROR) ? ItemError : (Item){ .item = b2it(r) };
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
    (void)map_type;
    if (!data || !field || !field->type) return ItemNull;
    if (!field->name) {
        // Spread attributes occupy unnamed slots containing a raw Map pointer;
        // reading that slot as its inferred ANY type corrupts equality and ordering.
        Map* nested_map = map_shape_field_to_map(data, field);
        return nested_map ? (Item){.map = nested_map} : ItemNull;
    }
    // packed map slots must use the canonical reader so query/equality handle
    // every scalar field type the same way as MapReader and serializers.
    return map_shape_field_to_item(data, field);
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
        FOR_EACH_MAP_FIELD(elmt_type, field) {
            if (field->name) {
                Item val = _map_field_value((TypeMap*)elmt_type, elmt->data, field);
                // always recurse with self_inclusive=true so descendants are fully searched
                if (val.item) query_collect(val, type_val, true, result, depth + 1);
            }
        }
        // recurse into content items
        for (int64_t i = 0; i < elmt->length; i++) {
            query_collect(elmt->items[i], type_val, true, result, depth + 1);
        }
    } else if (type_id == LMD_TYPE_MAP || type_id == LMD_TYPE_OBJECT) {
        Map* map = data.map;
        TypeMap* map_type = (TypeMap*)map->type;
        FOR_EACH_MAP_FIELD(map_type, field) {
            if (field->name) {
                Item val = _map_field_value(map_type, map->data, field);
                if (val.item) query_collect(val, type_val, true, result, depth + 1);
            }
        }
    } else if (type_id == LMD_TYPE_ARRAY) {
        // runtime lists and arrays share LMD_TYPE_ARRAY, so one branch must cover both.
        Array* arr = (Array*)data.array;
        for (int64_t i = 0; i < arr->length; i++) {
            query_collect(arr->items[i], type_val, true, result, depth + 1);
        }
    } else if (type_id == LMD_TYPE_ARRAY_NUM) {
        ArrayNum* arr = data.array_num;
        for (int64_t i = 0; i < arr->length; i++) {
            Item val = array_num_get(arr, i);
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
        FOR_EACH_MAP_FIELD(elmt_type, field) {
            if (field->name) {
                Item val = _map_field_value((TypeMap*)elmt_type, elmt->data, field);
                if (val.item && fn_is(val, type_val) == BOOL_TRUE) {
                    array_push(result, val);
                }
            }
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
        FOR_EACH_MAP_FIELD(map_type, field) {
            if (field->name) {
                Item val = _map_field_value(map_type, map->data, field);
                if (val.item && fn_is(val, type_val) == BOOL_TRUE) {
                    array_push(result, val);
                }
            }
        }
    } else if (type_id == LMD_TYPE_ARRAY) {
        // runtime lists and arrays share LMD_TYPE_ARRAY; keep spreadable query arrays here too.
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
    } else if (type_id == LMD_TYPE_ARRAY_NUM) {
        ArrayNum* arr = data.array_num;
        for (int64_t i = 0; i < arr->length; i++) {
            Item val = array_num_get(arr, i);
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

static int64_t array_num_find_equal(ArrayNum* array, Item needle, bool reverse) {
    if (!array) return -1;
    RootFrame roots(2);
    Rooted<ArrayNum*> rooted_array(roots, array);
    Rooted<Item> rooted_needle(roots, needle);
    int64_t index = reverse ? array->length - 1 : 0;
    int64_t limit = reverse ? -1 : array->length;
    int64_t step = reverse ? -1 : 1;
    for (; index != limit; index += step) {
        // Compact arrays can contain sized and ordinary numeric lanes. Compare
        // boxed lane values through the language equality relation so an
        // arithmetic-derived `integer` is never truncated through binary64.
        Item value = array_num_get(rooted_array.get(), index);
        if (fn_eq(value, rooted_needle.get()) == BOOL_TRUE) return index;
    }
    return -1;
}

Bool fn_in(Item a_item, Item b_item) {
    // Formal semantics 7.6: the membership operators are value computations, so
    // an error operand propagates. `in` searches ONE level, so an error nested
    // deeper is invisible to it -- answering `false` would read as "this data is
    // error-free" when it may not be, which is worse than no answer. (An error
    // cannot be found by search anyway: `in` matches by equality and
    // `error == error` is false by 5.1, so the participating reading would have
    // answered a confident false in every case.) Asking whether data contains an
    // error anywhere needs a deep check, not this operator.
    if (get_type_id(a_item) == LMD_TYPE_ERROR ||
            get_type_id(b_item) == LMD_TYPE_ERROR) {
        return BOOL_ERROR;
    }
    if (b_item._type_id) { // b is scalar
        if (b_item._type_id == LMD_TYPE_STRING && a_item._type_id == LMD_TYPE_STRING) {
            String *str_a = a_item.get_safe_string();
            String *str_b = b_item.get_safe_string();
            return str_a->len <= str_b->len && strstr(str_b->chars, str_a->chars) != NULL;
        }
        if (b_item._type_id == LMD_TYPE_BINARY) {
            Binary* bin = b_item.get_safe_binary();
            if (!bin) return BOOL_FALSE;
            const uint8_t* bytes = binary_data(bin);
            // Membership follows Lambda numeric equality so 173, 173u8, and
            // 173.0 all match the same byte without silently narrowing 256.
            for (uint32_t i = 0; i < binary_length(bin); i++) {
                Item byte = {.item = u8_to_item(bytes[i])};
                if (fn_eq(a_item, byte) == BOOL_TRUE) return BOOL_TRUE;
            }
            return BOOL_FALSE;
        }
    }
    else { // b is container
        TypeId b_type = b_item.container->type_id;
        if (b_type == LMD_TYPE_ARRAY) {
            List *list = b_item.array;
            for (int i = 0; i < list->length; i++) {
                if (fn_eq(list->items[i], a_item) == BOOL_TRUE) {
                    return true;
                }
            }
            return false;
        }
        else if (b_type == LMD_TYPE_RANGE) {
            return range_contains_item(b_item.range, a_item);
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
        else if (b_type == LMD_TYPE_ARRAY_NUM) {
            return array_num_find_equal(b_item.array_num, a_item, false) >= 0;
        }
        else if (b_type == LMD_TYPE_MAP || b_type == LMD_TYPE_OBJECT) {
            // value membership: check if a matches any field value
            Map *map = b_item.map;
            TypeMap* map_type = (TypeMap*)map->type;
            if (!map_type) return false;
            FOR_EACH_MAP_FIELD(map_type, field) {
                Item val = _map_field_value(map_type, map->data, field);
                if (fn_eq(val, a_item) == BOOL_TRUE) {
                    return true;
                }
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
                FOR_EACH_MAP_FIELD(elmt_type, field) {
                    Item val = _map_field_value((TypeMap*)elmt_type, elmt->data, field);
                    if (fn_eq(val, a_item) == BOOL_TRUE) {
                        return true;
                    }
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

static bool shape_has_named_key(TypeMap* map_type, Item key_item) {
    TypeId key_type = get_type_id(key_item);
    if (!is_text_type_id(key_type)) return false;
    const char* key = key_item.get_chars();
    uint32_t key_len = key_item.get_len();
    if (!key) return false;

    ShapeEntry* hit = typemap_hash_lookup(map_type, key, (int)key_len);
    if (hit && hit->name && hit->name->length == key_len &&
        memcmp(hit->name->str, key, key_len) == 0) {
        return true;
    }

    FOR_EACH_MAP_FIELD(map_type, field) {
        if (field->name && field->name->length == key_len &&
            memcmp(field->name->str, key, key_len) == 0) {
            return true;
        }
    }
    return false;
}

Bool fn_at(Item a_item, Item b_item) {
    // Formal semantics 7.6: the membership operators are value computations, so
    // an error operand propagates. `in` searches ONE level, so an error nested
    // deeper is invisible to it -- answering `false` would read as "this data is
    // error-free" when it may not be, which is worse than no answer. (An error
    // cannot be found by search anyway: `in` matches by equality and
    // `error == error` is false by 5.1, so the participating reading would have
    // answered a confident false in every case.) Asking whether data contains an
    // error anywhere needs a deep check, not this operator.
    if (get_type_id(a_item) == LMD_TYPE_ERROR ||
            get_type_id(b_item) == LMD_TYPE_ERROR) {
        return BOOL_ERROR;
    }
    TypeId b_type = get_type_id(b_item);
    if (b_type == LMD_TYPE_ARRAY || b_type == LMD_TYPE_ARRAY_NUM ||
        b_type == LMD_TYPE_RANGE) {
        // Formal semantics 8.0.1: `at` tests NAME presence, and a name is a
        // symbol key only -- an integer key is a key but not a name. Arrays key
        // their items by index, so they have no names and `1 at xs` is false.
        // This is what makes `at` worth having: it is the only way to range
        // over just the *named* members, which is meaningful on an element,
        // where `for (k at e)` gives the attributes without the children.
        // Admitting indices here would delete that capability. Iteration always
        // agreed (`for (k at xs)` yields nothing); membership was the outlier.
        // An index bound is written explicitly: `i < len(xs)`.
        return BOOL_FALSE;
    }
    if (b_type == LMD_TYPE_MAP || b_type == LMD_TYPE_OBJECT) {
        return shape_has_named_key((TypeMap*)b_item.map->type, a_item) ? BOOL_TRUE : BOOL_FALSE;
    }
    if (b_type == LMD_TYPE_ELEMENT) {
        return shape_has_named_key((TypeMap*)b_item.element->type, a_item) ? BOOL_TRUE : BOOL_FALSE;
    }
    if (b_type == LMD_TYPE_VMAP) {
        VMap* vm = b_item.vmap;
        if (!vm || !vm->vtable) return BOOL_FALSE;
        int64_t count = vm->vtable->count(vm->data);
        for (int64_t i = 0; i < count; i++) {
            if (fn_eq(vm->vtable->key_at(vm->data, i), a_item) == BOOL_TRUE) {
                return BOOL_TRUE;
            }
        }
    }
    return BOOL_FALSE;
}

// use struct overlays for static String with flexible array member
static struct { uint32_t len; uint8_t is_ascii; char chars[5]; } _str_null  = {4, 1, "null"};
static struct { uint32_t len; uint8_t is_ascii; char chars[5]; } _str_true  = {4, 1, "true"};
static struct { uint32_t len; uint8_t is_ascii; char chars[6]; } _str_false = {5, 1, "false"};
static struct { uint32_t len; uint8_t is_ascii; char chars[8]; } _str_error = {7, 1, "<error>"};
String& STR_NULL  = reinterpret_cast<String&>(_str_null);
String& STR_TRUE  = reinterpret_cast<String&>(_str_true);
String& STR_FALSE = reinterpret_cast<String&>(_str_false);
String& STR_ERROR = reinterpret_cast<String&>(_str_error);

static int fn_datetime_append_suffix(DateTime* dt, char* buf, size_t buf_size,
        int len) {
    if (dt->millisecond > 0) {
        len += snprintf(buf + len, buf_size - (size_t)len, ".%03d", dt->millisecond);
    }
    if (DATETIME_HAS_TIMEZONE(dt)) {
        int tz_offset = DATETIME_GET_TZ_OFFSET(dt);
        if (tz_offset == 0) {
            len += snprintf(buf + len, buf_size - (size_t)len, "z");
        } else {
            int hours = abs(tz_offset) / 60;
            int minutes = abs(tz_offset) % 60;
            len += snprintf(buf + len, buf_size - (size_t)len, "%+03d:%02d",
                tz_offset >= 0 ? hours : -hours, minutes);
        }
    }
    return len + snprintf(buf + len, buf_size - (size_t)len, "'");
}

String* fn_string(Item itm) {
    TypeId type_id = get_type_id(itm);
    switch (type_id) {
    case LMD_TYPE_NULL:
        return &STR_NULL;
    case LMD_TYPE_BOOL:
        return itm.bool_val ? &STR_TRUE : &STR_FALSE;
    case LMD_TYPE_STRING:
        return itm.get_safe_string();
    case LMD_TYPE_BINARY: {
        Binary* bin = itm.get_safe_binary();
        StrBuf* sb = strbuf_new_cap(bin ? (size_t)binary_length(bin) * 2 + 6 : 6);
        if (!sb) return &STR_ERROR;
        // String conversion is deliberately lossless and readable; raw UTF-8
        // decoding requires an explicit codec rather than implicit text.
        format_binary_literal(sb, bin);
        String* result = heap_strcpy(sb->str, (int64_t)sb->length);
        strbuf_free(sb);
        return result ? result : &STR_ERROR;
    }
    case LMD_TYPE_SYMBOL: {
        // Symbol has different layout than String; create a String from symbol chars
        // heap_strcpy may collect before copying its source. Keep the Symbol's
        // owning Item exact-rooted so its inline character buffer stays live.
        RootFrame roots(1);
        Rooted<Item> rooted(roots, itm);
        Symbol* sym = rooted.get().get_safe_symbol();
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

                    len = fn_datetime_append_suffix(dt, buf, sizeof(buf), len);
                    break;
                }

                case DATETIME_PRECISION_DATE_TIME:
                default: {
                    // Format full datetime with 'T' separator
                    len = snprintf(buf, sizeof(buf), "t'%04d-%02d-%02dT%02d:%02d:%02d",
                        DATETIME_GET_YEAR(dt), DATETIME_GET_MONTH(dt), dt->day,
                        dt->hour, dt->minute, dt->second);

                    len = fn_datetime_append_suffix(dt, buf, sizeof(buf), len);
                    break;
                }
            }

            return heap_strcpy(buf, len);
        } else {
            return &STR_NULL;
        }
    }
    case LMD_TYPE_INT: {
        // THE int renderer -- covers the whole C16 domain and the poison. The
        // i64 conversion this replaced clamped every value above 2^63, so
        // `string(2^70)` returned INT64_MAX while printing it was correct.
        StrBuf* sb = strbuf_new_cap(40);
        print_int_value(sb, lambda_int_item_value(itm));
        String* result = heap_strcpy(sb->str, (int64_t)sb->length);
        strbuf_free(sb);
        return result;
    }
    case LMD_TYPE_COMPLEX: {
        RootFrame roots(1);
        Rooted<Item> rooted(roots, itm);
        StrBuf* sb = strbuf_new_cap(64);
        if (!sb) return &STR_ERROR;
        print_item(sb, rooted.get(), 1, null);
        String* result = heap_strcpy(sb->str, sb->length);
        strbuf_free(sb);
        return result ? result : &STR_ERROR;
    }
    case LMD_TYPE_INT64: {
        char buf[32];
        int64_t long_val = itm.get_int64();
        snprintf(buf, sizeof(buf), "%lld", (long long)long_val);
        int len = strlen(buf);
        return heap_strcpy(buf, len);
    }
    case LMD_TYPE_FLOAT:
    case LMD_TYPE_FLOAT64: {
        char buf[32];
        double dval = itm.get_double();
        snprintf(buf, sizeof(buf), "%g", dval);
        int len = strlen(buf);
        return heap_strcpy(buf, len);
    }
    case LMD_TYPE_NUM_SIZED: {
        char buf[32];
        NumSizedType st = itm.get_num_type();
        if (st == NUM_FLOAT16 || st == NUM_FLOAT32) {
            snprintf(buf, sizeof(buf), "%g", itm.get_num_sized_as_double());
        } else {
            snprintf(buf, sizeof(buf), "%" PRId64, itm.get_num_sized_as_int64());
        }
        int len = strlen(buf);
        return heap_strcpy(buf, len);
    }
    case LMD_TYPE_UINT64: {
        char buf[32];
        snprintf(buf, sizeof(buf), "%" PRIu64, itm.get_uint64());
        int len = strlen(buf);
        return heap_strcpy(buf, len);
    }
    case LMD_TYPE_DECIMAL:  case LMD_TYPE_RANGE:  case LMD_TYPE_ARRAY:
    case LMD_TYPE_ARRAY_NUM:
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
        const char* name;
        if (type_type->type == &TYPE_INTEGER) {
            name = "integer";
        } else if (type_type->type == &TYPE_NUMBER) {
            name = "number";
        } else if (type_type->type->type_id == LMD_TYPE_NUM_SIZED) {
            name = get_num_sized_type_name((NumSizedType)type_type->type->kind);
        } else {
            name = get_type_name(type_type->type->type_id);
        }
        if (name) return heap_strcpy((char*)name, strlen(name));
        return &STR_NULL;
    }
    case LMD_TYPE_ERROR:
        // pointer-returning ABI cannot carry ItemError; never render an error as ordinary text.
        return nullptr;
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

Type* const_type(int64_t type_index) {
    ArrayList* type_list = (ArrayList*)context->type_list;
    if (type_index < 0 || type_index >= type_list->length) {
        return &LIT_TYPE_ERROR;
    }
    Type* type = (Type*)(type_list->data[type_index]);
    return type;
}

TypePattern* const_pattern(int64_t pattern_index) {
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

// MIR Direct module-type-list-aware wrappers: save/restore context->type_list so that
// cross-module calls use this module's own type_list.
Type* const_type_with_tl(int64_t type_index, void* type_list_ptr) {
    void* saved = context->type_list;
    context->type_list = type_list_ptr;
    Type* r = const_type(type_index);
    context->type_list = saved;
    return r;
}
TypePattern* const_pattern_with_tl(int64_t pattern_index, void* type_list_ptr) {
    void* saved = context->type_list;
    context->type_list = type_list_ptr;
    TypePattern* r = const_pattern(pattern_index);
    context->type_list = saved;
    return r;
}

Type* fn_type(Item item) {
    TypeType *type = (TypeType *)heap_calloc(sizeof(TypeType) + sizeof(Type), LMD_TYPE_TYPE);
    Type *item_type = (Type *)((uint8_t *)type + sizeof(TypeType));
    type->type = item_type;  type->type_id = LMD_TYPE_TYPE;
    TypeId resolved_type = get_type_id(item);
    // C16 surface rule: the poison values `int` and `float` share report as the
    // narrower domain, the same convention that makes `type(1)` be `int`. Only
    // the surface answer moves -- the decoder still sees them as doubles.
    if (lambda_item_is_merged_poison(item.item)) resolved_type = LMD_TYPE_INT;
    if (resolved_type == LMD_TYPE_OBJECT && item.object && item.object->type) {
        // preserve nominal object metadata so name(type(obj)) can report the declared type.
        type->type = (Type*)item.object->type;
        return (Type*)type;
    }
    if (resolved_type == LMD_TYPE_ELEMENT && item.element && item.element->type) {
        // preserve tag metadata for element type values instead of collapsing to generic element.
        type->type = (Type*)item.element->type;
        return (Type*)type;
    }
    if (resolved_type == LMD_TYPE_MAP && item.map && item.map->type) {
        TypeMap* map_type = (TypeMap*)item.map->type;
        if (map_type->struct_name) {
            // keep named map aliases visible while anonymous maps still collapse to generic map.
            type->type = (Type*)map_type;
            return (Type*)type;
        }
    }
    if (resolved_type == LMD_TYPE_DECIMAL) {
        Decimal* dec = item.get_decimal();
        if (dec && dec->unlimited == DECIMAL_BIGINT) {
            // integer is a language type carried by Decimal storage; hide the carrier from type().
            type->type = &TYPE_INTEGER;
            return (Type*)type;
        }
    }
    if (resolved_type != LMD_TYPE_RAW_POINTER) {
        // self-tagged floats do not carry LMD_TYPE_FLOAT in the raw high byte.
        item_type->type_id = resolved_type;
    }
    else if (item.item == 0) {
        // handle raw 0 value (from y2it(nullptr) etc.) as null
        item_type->type_id = LMD_TYPE_NULL;
    }
    else if (item.container) {
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
    if (tid == LMD_TYPE_ARRAY_NUM) {
        item_type->type_id = LMD_TYPE_ARRAY;
    } else if (tid == LMD_TYPE_VMAP) {
        item_type->type_id = LMD_TYPE_MAP;
    } else if (tid == LMD_TYPE_NUM_SIZED) {
        // preserve the sub-type in the kind field for sized numerics
        item_type->kind = item.get_num_type();
    }
    return (Type*)type;
}

static Symbol* fn_name_symbol_from_chars(const char* name, size_t len) {
    if (!name || len == 0) return nullptr;
    return heap_create_symbol(name, len);
}

static Symbol* fn_name_symbol_from_strview(StrView name) {
    return fn_name_symbol_from_chars(name.str, name.length);
}

static Symbol* fn_name_from_type(Type* type) {
    if (!type) return nullptr;
    if (type == &TYPE_INTEGER) return fn_name_symbol_from_chars("integer", 7);
    if (type == &TYPE_NUMBER) return fn_name_symbol_from_chars("number", 6);
    switch (type->type_id) {
    case LMD_TYPE_OBJECT: {
        TypeObject* obj_type = (TypeObject*)type;
        Symbol* obj_name = fn_name_symbol_from_strview(obj_type->type_name);
        if (obj_name) return obj_name;
        return obj_type->struct_name
            ? fn_name_symbol_from_chars(obj_type->struct_name, strlen(obj_type->struct_name))
            : nullptr;
    }
    case LMD_TYPE_ELEMENT: {
        TypeElmt* elmt_type = (TypeElmt*)type;
        Symbol* elmt_name = fn_name_symbol_from_strview(elmt_type->name);
        if (elmt_name) return elmt_name;
        const char* fallback = get_type_name(type->type_id);
        return fn_name_symbol_from_chars(fallback, fallback ? strlen(fallback) : 0);
    }
    case LMD_TYPE_MAP: {
        TypeMap* map_type = (TypeMap*)type;
        if (map_type->struct_name) {
            return fn_name_symbol_from_chars(map_type->struct_name, strlen(map_type->struct_name));
        }
        break;
    }
    default:
        break;
    }
    const char* name = get_type_name(type->type_id);
    return fn_name_symbol_from_chars(name, name ? strlen(name) : 0);
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
        // name() reads intrinsic tag metadata; user attrs like name: must only affect .name.
        Element* elmt = item.element;
        if (!elmt || !elmt->type) return nullptr;
        return fn_name_from_type((Type*)elmt->type);
    }
    case LMD_TYPE_OBJECT: {
        // name() reads nominal object metadata instead of the object's user field named "name".
        Object* obj = item.object;
        if (!obj || !obj->type) return nullptr;
        return fn_name_from_type((Type*)obj->type);
    }
    case LMD_TYPE_FUNC: {
        // return the function's name as a symbol
        Function* fn = item.function;
        if (!fn) return nullptr;
        // function name is stored in the name field
        if (fn->name && strlen(fn->name) > 0) return fn_name_symbol_from_chars(fn->name, strlen(fn->name));
        // We return null for anonymous functions
        return nullptr;
    }
    case LMD_TYPE_TYPE: {
        TypeType* type_type = (TypeType*)item.type;
        if (!type_type || !type_type->type) return nullptr;
        return fn_name_from_type(type_type->type);
    }
    case LMD_TYPE_SYMBOL: {
        // for symbols, just return the symbol itself (it's already a name)
        return item.get_safe_symbol();
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

extern "C" Input* input_from_target(Target* target, String* type, String* flavor);
extern "C" Input* input_from_target_with_name_parent(Target* target, String* type,
    String* flavor, NamePool* name_parent);

static bool input_schema_collect_type(Type* type, NamePool* name_pool,
        ArrayList* visited) {
    if (!type || !name_pool || !visited) return true;
    for (int i = 0; i < visited->length; i++) {
        if (arraylist_get(visited, i) == type) return true;
    }
    if (!arraylist_append(visited, type)) return false;

    // Declarations wrap their semantic type.  Follow that edge before
    // examining fields so schema names are admitted exactly once regardless
    // of whether the caller supplied a named declaration or its body.
    Type* semantic_type = type_field_unwrap_simple_decl(type);
    if (semantic_type != type) {
        return input_schema_collect_type(semantic_type, name_pool, visited);
    }

    if (type->type_id == LMD_TYPE_MAP || type->type_id == LMD_TYPE_ELEMENT ||
            type->type_id == LMD_TYPE_OBJECT) {
        TypeMap* map_type = (TypeMap*)type;
        if (type->type_id == LMD_TYPE_ELEMENT) {
            TypeElmt* element_type = (TypeElmt*)type;
            if (element_type->name.str && element_type->name.length > 0 &&
                    !name_pool_create_len(name_pool, element_type->name.str,
                        element_type->name.length)) {
                return false;
            }
        }
        for (ShapeEntry* field = map_type->shape; field; field = field->next) {
            if (field->name && !name_pool_create_len(name_pool,
                    field->name->str, field->name->length)) return false;
            if (!input_schema_collect_type(field->type, name_pool, visited)) return false;
        }
        if (type->type_id == LMD_TYPE_OBJECT) {
            if (!input_schema_collect_type(((TypeObject*)type)->base,
                    name_pool, visited)) return false;
        }
        return true;
    }
    if (type->type_id == LMD_TYPE_ARRAY) {
        return input_schema_collect_type(((TypeArray*)type)->nested,
            name_pool, visited);
    }
    if (type->type_id == LMD_TYPE_TYPE) {
        switch (type->kind) {
        case TYPE_KIND_SIMPLE:
            return input_schema_collect_type(((TypeType*)type)->type,
                name_pool, visited);
        case TYPE_KIND_UNARY:
            return input_schema_collect_type(((TypeUnary*)type)->operand,
                name_pool, visited);
        case TYPE_KIND_BINARY:
            return input_schema_collect_type(((TypeBinary*)type)->left,
                    name_pool, visited) &&
                input_schema_collect_type(((TypeBinary*)type)->right,
                    name_pool, visited);
        case TYPE_KIND_CONSTRAINED:
            return input_schema_collect_type(((TypeConstrained*)type)->base,
                name_pool, visited);
        case TYPE_KIND_PARAM: {
            TypeParam* param = (TypeParam*)type;
            return input_schema_collect_type(param->full_type, name_pool, visited) &&
                input_schema_collect_type(param->contract_type, name_pool, visited);
        }
        default:
            return true;
        }
    }
    if (type->type_id == LMD_TYPE_FUNC) {
        TypeFunc* fn = (TypeFunc*)type;
        if (!input_schema_collect_type(fn->returned, name_pool, visited) ||
                !input_schema_collect_type(fn->inferred_return, name_pool, visited) ||
                !input_schema_collect_type(fn->return_contract, name_pool, visited) ||
                !input_schema_collect_type(fn->error_type, name_pool, visited)) return false;
        for (TypeParam* param = fn->param; param; param = param->next) {
            if (!input_schema_collect_type(param->full_type, name_pool, visited) ||
                    !input_schema_collect_type(param->contract_type, name_pool, visited)) return false;
        }
    }
    return true;
}

static bool input_schema_prepare_name_parent(Type* schema_type, NamePool* name_pool) {
    if (!schema_type || !name_pool) return true;
    ArrayList* visited = arraylist_new(16);
    if (!visited) return false;
    bool ok = input_schema_collect_type(schema_type, name_pool, visited);
    arraylist_free(visited);
    return ok;
}

RetItem fn_input2(Item target_item, Item type) {
    GUARD_ERROR_RI2(target_item, type);

    // Dry-run mode: return fabricated input data
    if (g_dry_run) {
        log_debug("dry-run: fabricated input() call");
        const char* content = "{\"name\": \"dry-run\", \"version\": \"1.0\", \"items\": [1, 2, 3], \"active\": true}";
        // check type hint for more realistic fabrication
        TypeId tid = get_type_id(type);
        if (is_text_type_id(tid)) {
            String* ts = fn_string(type);
            if (ts) {
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
    Type* schema_type = NULL;

    // Validate target type: must be string, symbol, or path
    TypeId target_type_id = get_type_id(target_item);
    if (!is_text_type_id(target_type_id) && target_type_id != LMD_TYPE_PATH) {
        set_runtime_error(ERR_TYPE_MISMATCH,
            "input target must be a string, symbol, or path, got type: %s",
            get_type_name(target_type_id));
        return item_to_ri(ItemError);
    }

    // Convert target Item to Target struct
    Url* cwd = context ? (Url*)context->cwd : NULL;
    Target* target = item_to_target(target_item.item, cwd);
    if (!target) {
        set_runtime_error(ERR_INVALID_URL, "input: failed to resolve target");
        return item_to_ri(ItemError);
    }

    log_debug("fn_input2: target scheme=%d, type=%d", target->scheme, target->type);

    TypeId type_id = get_type_id(type);
    if (type_id == LMD_TYPE_NULL) {
        // No type specified
        type_str = NULL;
    }
    else if (is_text_type_id(type_id)) {
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
            if (is_text_type_id(type_value_type)) {
                type_str = fn_string(input_type);
            }
            else {
                set_runtime_error(ERR_TYPE_MISMATCH,
                    "input type option must be a string or symbol, got type: %s",
                    get_type_name(type_value_type));
                target_free(target);
                return item_to_ri(ItemError);
            }
        }

        // Extract 'flavor' from map
        Item input_flavor = _map_get((TypeMap*)options_map->type, options_map->data, "flavor", &is_found);
        if (!is_found || !input_flavor.item || input_flavor._type_id == LMD_TYPE_NULL) { // missing 'flavor' key
            flavor_str = NULL;
        } else {
            TypeId flavor_value_type = get_type_id(input_flavor);
            if (is_text_type_id(flavor_value_type)) {
                flavor_str = fn_string(input_flavor);
            }
            else {
                set_runtime_error(ERR_TYPE_MISMATCH,
                    "input flavor option must be a string or symbol, got type: %s",
                    get_type_name(flavor_value_type));
                target_free(target);
                return item_to_ri(ItemError);
            }
        }

        Item input_schema = _map_get((TypeMap*)options_map->type, options_map->data,
            "schema", &is_found);
        if (is_found && input_schema.item && input_schema._type_id != LMD_TYPE_NULL) {
            if (get_type_id(input_schema) != LMD_TYPE_TYPE) {
                return item_to_ri(lambda_type_error(input_schema, &TYPE_TYPE,
                    "input schema option"));
            }
            schema_type = input_schema.type;
        }
    }
    else {
        set_runtime_error(ERR_TYPE_MISMATCH,
            "input format must be a string, symbol, map, or null, got type: %s",
            get_type_name(type_id));
        target_free(target);
        return item_to_ri(ItemError);
    }

    // Check if context is properly initialized
    if (!context) {
        set_runtime_error(ERR_INVALID_STATE, "input: runtime context is not initialized");
        target_free(target);
        return item_to_ri(ItemError);
    }

    // Pre-register exact schema fields before parsing so known Input names hit
    // the retained static/dynamic parent; parser-only misses stay id-less per
    // D3.4.4v2 and D4.6.2v2.
    if (schema_type && !input_schema_prepare_name_parent(schema_type,
            context->name_pool)) {
        set_runtime_error(ERR_OUT_OF_MEMORY, "input: failed to prepare schema names");
        target_free(target);
        return item_to_ri(ItemError);
    }

    log_debug("input type: %s, flavor: %s", type_str ? type_str->chars : "null", flavor_str ? flavor_str->chars : "null");

    // A schema-backed input inherits the schema/runtime pool; a schemaless
    // input keeps the existing id-less Input seam required by D4.6.2v2.
    Input *input = schema_type
        ? input_from_target_with_name_parent(target, type_str, flavor_str,
            context->name_pool)
        : input_from_target(target, type_str, flavor_str);
    if (!input) {
        // input() is a can-raise effect: a loader failure must not become a
        // successful null, or callers cannot distinguish failed execution from
        // a valid input whose root happens to be null.
        set_runtime_error(ERR_IO_ERROR, "input: failed to load target '%s'",
            target->original ? target->original : "<path target>");
    } else if (input->parse_failed) {
        set_input_parse_error("input", input, type_str);
        target_free(target);
        return item_to_ri(ItemError);
    }
    target_free(target);

    // todo: input should be cached in context
    Item result = (input && input->root.item) ? input->root : ItemNull;
    if (schema_type && result.item) {
        // Keep input schema admission on the shared TE-10 boundary so its
        // conversion and validator-path diagnostics cannot diverge from bindings.
        return item_to_ri(lambda_type_check(result, schema_type, "input schema"));
    }
    return input ? ri_ok(result) : item_to_ri(ItemError);
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
        set_runtime_error(ERR_TYPE_MISMATCH,
            "parse: 1st argument must be a string, got type: %s",
            get_type_name(str_type));
        return item_to_ri(ItemError);
    }
    String* str = str_item.get_safe_string();
    if (!str) {
        set_runtime_error(ERR_INVALID_STATE, "parse: string value is unavailable");
        return item_to_ri(ItemError);
    }

    // parse the 2nd argument (format symbol or options map) - same logic as fn_input2
    String* type_str = NULL;
    String* flavor_str = NULL;

    TypeId type_id = get_type_id(type);
    if (type_id == LMD_TYPE_NULL) {
        type_str = NULL;  // auto-detect
    }
    else if (is_text_type_id(type_id)) {
        type_str = fn_string(type);
    }
    else if (type_id == LMD_TYPE_MAP || type_id == LMD_TYPE_OBJECT) {
        Map* options_map = type.map;
        bool is_found;

        // extract 'type' from map
        Item input_type = _map_get((TypeMap*)options_map->type, options_map->data, "type", &is_found);
        if (is_found && input_type.item && input_type._type_id != LMD_TYPE_NULL) {
            TypeId type_value_type = get_type_id(input_type);
            if (is_text_type_id(type_value_type)) {
                type_str = fn_string(input_type);
            } else {
                set_runtime_error(ERR_TYPE_MISMATCH,
                    "parse: type option must be a string or symbol, got type: %s",
                    get_type_name(type_value_type));
                return item_to_ri(ItemError);
            }
        }

        // extract 'flavor' from map
        Item input_flavor = _map_get((TypeMap*)options_map->type, options_map->data, "flavor", &is_found);
        if (is_found && input_flavor.item && input_flavor._type_id != LMD_TYPE_NULL) {
            TypeId flavor_value_type = get_type_id(input_flavor);
            if (is_text_type_id(flavor_value_type)) {
                flavor_str = fn_string(input_flavor);
            } else {
                set_runtime_error(ERR_TYPE_MISMATCH,
                    "parse: flavor option must be a string or symbol, got type: %s",
                    get_type_name(flavor_value_type));
                return item_to_ri(ItemError);
            }
        }
    }
    else {
        set_runtime_error(ERR_TYPE_MISMATCH,
            "parse: 2nd argument must be a format symbol or options map, got type: %s",
            get_type_name(type_id));
        return item_to_ri(ItemError);
    }

    // create a dummy URL for the parser infrastructure (no actual file)
    Url* dummy_url = url_parse("parse://inline");
    if (!dummy_url) {
        set_runtime_error(ERR_INVALID_STATE,
            "parse: failed to initialize the in-memory parser URL");
        return item_to_ri(ItemError);
    }

    log_debug("fn_parse2: type=%s, flavor=%s", type_str ? type_str->chars : "auto", flavor_str ? flavor_str->chars : "null");

    Input* input = input_from_source(str->chars, dummy_url, type_str, flavor_str);
    if (!input) {
        set_runtime_error(ERR_OUT_OF_MEMORY,
            "parse: failed to initialize parser for format '%s'",
            type_str ? type_str->chars : "auto");
        return item_to_ri(ItemError);
    }
    // ItemNull is a valid parsed value, so only the explicit parser status or
    // an error root may convert this result into the non-admissive error path.
    if (input->parse_failed || get_type_id(input->root) == LMD_TYPE_ERROR) {
        set_input_parse_error("parse", input, type_str);
        return item_to_ri(ItemError);
    }
    return ri_ok(input->root);
}

RetItem fn_parse1(Item str_item) {
    return fn_parse2(str_item, ItemNull);
}

Item fn_parse_html_fragment1(Item str_item) {
    GUARD_ERROR1(str_item);

    // HTML fragment parsing is non-admissive: null is reserved for a valid
    // absence, not for a rejected argument or failed parser allocation.
    TypeId str_type = get_type_id(str_item);
    if (str_type != LMD_TYPE_STRING) {
        set_runtime_error(ERR_TYPE_MISMATCH,
            "parse_html_fragment: argument must be a string, got type: %s",
            get_type_name(str_type));
        return ItemError;
    }

    String* str = str_item.get_safe_string();
    if (!str) {
        set_runtime_error(ERR_INVALID_STATE,
            "parse_html_fragment: string value is unavailable");
        return ItemError;
    }

    Url* dummy_url = url_parse("parse://html-fragment");
    if (!dummy_url) {
        set_runtime_error(ERR_INVALID_STATE,
            "parse_html_fragment: failed to initialize parser URL");
        return ItemError;
    }
    Input* input = InputManager::create_input(dummy_url);
    if (!input) {
        set_runtime_error(ERR_OUT_OF_MEMORY,
            "parse_html_fragment: failed to allocate parser input");
        return ItemError;
    }

    Html5Parser* parser = html5_fragment_parser_create(input->pool, input->arena, input);
    if (!parser) {
        set_runtime_error(ERR_OUT_OF_MEMORY,
            "parse_html_fragment: failed to allocate HTML fragment parser");
        return ItemError;
    }
    if (!html5_fragment_parse(parser, str->chars)) {
        set_runtime_error(ERR_PARSE_ERROR,
            "parse_html_fragment: failed to parse the HTML fragment");
        return ItemError;
    }

    Element* fragment = html5_fragment_get_body(parser);
    input->root = fragment ? Item{.element = fragment} : ItemNull;
    if (!fragment) {
        set_runtime_error(ERR_PARSE_ERROR,
            "parse_html_fragment: parser produced no fragment body");
        return ItemError;
    }
    return input->root;
}

// MIR JIT wrappers: RetItem-returning functions adapted to return Item only.
// RetItem is a 16-byte struct which has ABI issues with MIR's i64 return type.
// These wrappers extract .value, converting errors to ItemError.
extern "C" Item fn_parse1_mir(Item str_item) {
    RetItem ri = fn_parse1(str_item);
    return ri.err ? ItemError : ri.value;
}

extern "C" Item fn_parse2_mir(Item str_item, Item type) {
    RetItem ri = fn_parse2(str_item, type);
    return ri.err ? ItemError : ri.value;
}

extern "C" Item fn_input1_mir(Item url) {
    RetItem ri = fn_input1(url);
    return ri.err ? ItemError : ri.value;
}

extern "C" Item fn_input2_mir(Item url, Item options) {
    RetItem ri = fn_input2(url, options);
    return ri.err ? ItemError : ri.value;
}

// Procedural function wrappers (pn_* functions also return RetItem)
extern "C" Item pn_cmd1_mir(Item cmd) {
    RetItem ri = pn_cmd1(cmd);
    return ri.err ? ItemError : ri.value;
}
extern "C" Item pn_cmd2_mir(Item cmd, Item args) {
    RetItem ri = pn_cmd2(cmd, args);
    return ri.err ? ItemError : ri.value;
}
extern "C" Item pn_fetch_mir(Item url, Item options) {
    RetItem ri = pn_fetch(url, options);
    return ri.err ? ItemError : ri.value;
}
extern "C" Item pn_output2_mir(Item source, Item target) {
    RetItem ri = pn_output2(source, target);
    return ri.err ? ItemError : ri.value;
}
extern "C" Item pn_output3_mir(Item source, Item target, Item options) {
    RetItem ri = pn_output3(source, target, options);
    return ri.err ? ItemError : ri.value;
}
extern "C" Item pn_io_copy_mir(Item src, Item dst) {
    RetItem ri = pn_io_copy(src, dst);
    return ri.err ? ItemError : ri.value;
}
extern "C" Item pn_io_read_mir(Item target) {
    return ri_to_item(pn_io_read(target));
}
extern "C" Item pn_io_move_mir(Item src, Item dst) {
    RetItem ri = pn_io_move(src, dst);
    return ri.err ? ItemError : ri.value;
}
extern "C" Item pn_io_delete_mir(Item path) {
    RetItem ri = pn_io_delete(path);
    return ri.err ? ItemError : ri.value;
}
extern "C" Item pn_io_mkdir_mir(Item path) {
    RetItem ri = pn_io_mkdir(path);
    return ri.err ? ItemError : ri.value;
}
extern "C" Item pn_io_touch_mir(Item path) {
    RetItem ri = pn_io_touch(path);
    return ri.err ? ItemError : ri.value;
}
extern "C" Item pn_io_symlink_mir(Item target, Item link) {
    RetItem ri = pn_io_symlink(target, link);
    return ri.err ? ItemError : ri.value;
}
extern "C" Item pn_io_chmod_mir(Item path, Item mode) {
    RetItem ri = pn_io_chmod(path, mode);
    return ri.err ? ItemError : ri.value;
}
extern "C" Item pn_io_rename_mir(Item old_path, Item new_path) {
    RetItem ri = pn_io_rename(old_path, new_path);
    return ri.err ? ItemError : ri.value;
}
extern "C" Item pn_io_fetch1_mir(Item target) {
    RetItem ri = pn_io_fetch1(target);
    return ri.err ? ItemError : ri.value;
}
extern "C" Item pn_io_fetch2_mir(Item target, Item options) {
    RetItem ri = pn_io_fetch2(target, options);
    return ri.err ? ItemError : ri.value;
}
extern "C" Item pn_output_append_mir(Item source, Item target) {
    RetItem ri = pn_output_append(source, target);
    return ri.err ? ItemError : ri.value;
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
        else if (is_text_type_id(type_id)) {
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
        result->flags = 0;
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
    else if (is_text_type_id(type_id)) {
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
            if (is_text_type_id(type_value_type)) {
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
            if (is_text_type_id(flavor_value_type)) {
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
    if (item_type == LMD_TYPE_VMAP) {
        // VMap integer-looking keys are still map keys, not sequence offsets.
        VMap* vm = item.vmap;
        // Host-backed VMaps resolve through their host hook without a storage
        // vtable; rejecting them here made callback metadata appear absent.
        if (vm && (vm->host_type || vm->vtable)) return vmap_get_by_item(vm, index_item);
        return ItemNull;
    }

    // Arithmetic-derived semantic integers are decimal-backed Items. Sequence
    // access must consume their exact value instead of rejecting the storage tag.
    int64_t index = -1;
    if (lambda_item_to_int64_exact(index_item, &index)) {
        // Numeric keys are positional only for indexable faces. Sending a
        // numeric key to item_at for a map/object used to log an unsupported
        // type even though S7.1.1v2 requires a quiet null read.
        switch (item_type) {
        case LMD_TYPE_ARRAY:
        case LMD_TYPE_ARRAY_NUM:
        case LMD_TYPE_RANGE:
        case LMD_TYPE_ELEMENT:
        case LMD_TYPE_STRING:
        case LMD_TYPE_SYMBOL:
        case LMD_TYPE_BINARY:
        case LMD_TYPE_PATH:
        case LMD_TYPE_VMAP:
            return item_at(item, index);
        default:
            return ItemNull;
        }
    }

    TypeId index_type = get_type_id(index_item);
    switch (index_type) {
    case LMD_TYPE_STRING:  case LMD_TYPE_SYMBOL:
        return fn_member(item, index_item);
    default: {
        // check for type value (pointer-based, so _type_id is 0)
        if (index_type == LMD_TYPE_TYPE) {
            return fn_child_query(item, index_item);
        }
        // range index: s[i to j] → slice(item, start, end+1)
        if (index_type == LMD_TYPE_RANGE) {
            Range* rng = index_item.range;
            if (rng) {
                if (rng->is_char) {
                    log_error("fn_index: character ranges cannot be used as slice bounds");
                    return ItemError;
                }
                Item start_it = {.item = i2it(rng->start)};
                Item end_it = {.item = i2it(rng->end + 1)}; // range end is inclusive, slice end is exclusive
                return fn_slice(item, start_it, end_it);
            }
            return ItemNull;
        }
        // boolean mask index: arr[mask] — select elements where mask is true
        if (index_type == LMD_TYPE_ARRAY_NUM && item_type == LMD_TYPE_ARRAY_NUM) {
            return fn_mask_index(item, index_item);
        }
        log_debug("invalid index type %d", index_item._type_id);
        return ItemNull;
    }
    }

    return ItemNull;
}

// All dynamic member stores enter through this boundary so a key-domain
// mismatch cannot be truncated into index zero (or silently dropped by a map
// setter). Reads remain total in fn_index; writes are hard language errors.
Item fn_index_set(Item item, Item key, Item value) {
    TypeId item_type = get_type_id(item);
    if (item_type == LMD_TYPE_ERROR) return item;

    int64_t index = 0;
    bool has_index_key = lambda_item_to_int64_exact(key, &index);
    bool has_name_key = is_text_type_id(get_type_id(key));

    if (item_type == LMD_TYPE_ARRAY || item_type == LMD_TYPE_ARRAY_NUM) {
        if (!has_index_key) {
            set_runtime_error(ERR_TYPE_MISMATCH,
                "invalid sequence member write: key must be an exact non-negative integer");
            return ItemError;
        }
        return fn_array_set(item.array, index, value);
    }
    if (item_type == LMD_TYPE_ELEMENT) {
        if (has_index_key) return fn_array_set(item.array, index, value);
        if (has_name_key) return fn_map_set(item, key, value);
        set_runtime_error(ERR_TYPE_MISMATCH,
            "invalid element member write: key must be an exact integer or string/symbol");
        return ItemError;
    }
    if (item_type == LMD_TYPE_MAP || item_type == LMD_TYPE_OBJECT ||
            item_type == LMD_TYPE_VMAP) {
        if (has_name_key) return fn_map_set(item, key, value);
        set_runtime_error(ERR_TYPE_MISMATCH,
            "invalid named member write: key must be string or symbol");
        return ItemError;
    }

    set_runtime_error(ERR_TYPE_MISMATCH,
        "invalid member write: base has no writable member face");
    return ItemError;
}

int64_t fn_int64_index(Item item) {
    int64_t value = INT64_MIN;
    return lambda_item_to_int64_exact(item, &value) ? value : INT64_MIN;
}

static Item lambda_bind_object_method(TypeMethod* method, Item self) {
    Function* bound = to_closure_named(method->compiled_fn, method->arity,
        (void*)(uintptr_t)self.item, method->compiled_name);
    // A method table keeps raw code, so reattach its TypeFunc when rebuilding
    // the bound closure; dynamic dispatch needs it to validate and marshal calls.
    lambda_function_set_type(bound, method->fn_type);
    lambda_function_mark_mir_context_abi(bound);
    if (method->is_proc) {
        lambda_function_mark_lambda_boxed_procedure(bound);
    } else {
        lambda_function_mark_lambda_boxed_function(bound);
    }
    return {.item = (uint64_t)(uintptr_t)bound};
}

// The single authority for a Path's built-in properties, shared by fn_member
// (the ANY-typed member lane) and item_attr (the statically-typed lane, which
// the T0 interpreter and the C transpiler also use). The two used to carry
// diverging copies — item_attr lacked path/extension/scheme/depth/mode and
// fn_member lacked is_file and metadata auto-load — so the value of `p.extension`
// depended on which lane compiled the access (tier mismatch, SI3v2/D3.3.1v2).
extern "C" bool path_is_property_name(const char* k) {
    if (!k) return false;
    static const char* const names[] = {
        "name", "is_dir", "is_file", "is_link", "size", "modified",
        "path", "extension", "scheme", "depth", "parent", "mode"};
    for (size_t i = 0; i < sizeof(names) / sizeof(names[0]); i++) {
        if (strcmp(k, names[i]) == 0) return true;
    }
    return false;
}

extern "C" Item path_property_get(Path* path, const char* k) {
    if (!path || !k) return ItemNull;

    // structural properties (always available)
    if (strcmp(k, "name") == 0) {
        // return the leaf segment name (e.g. "file.txt")
        if (path->name) return {.item = s2it(heap_create_name(path->name))};
        return ItemNull;
    }
    if (strcmp(k, "path") == 0) {
        // return the full OS path string (e.g. "./lib/file.txt")
        StrBuf* sb = strbuf_new();
        path_to_os_path(path_qualify_default(eval_context_get_pool(), path), sb);
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
        // return the parent path (go up one directory level);
        // path->parent is the next segment up in the linked list
        if (path->parent && path->parent->parent) return {.path = path->parent};
        return ItemNull;
    }

    // metadata properties (require stat'd metadata; loaded on first access)
    if (strcmp(k, "size") == 0 || strcmp(k, "modified") == 0 ||
        strcmp(k, "is_dir") == 0 || strcmp(k, "is_file") == 0 ||
        strcmp(k, "is_link") == 0 || strcmp(k, "mode") == 0) {
        if (!(path->flags & PATH_FLAG_META_LOADED)) {
            path_load_metadata(path);
        }
        PathMeta* meta = path->meta;
        if (!meta) {
            // path doesn't exist or couldn't be stat'd
            if (strcmp(k, "size") == 0) return {.item = i2it(-1)};  // -1 for unknown/error
            if (strcmp(k, "modified") == 0 || strcmp(k, "mode") == 0) return ItemNull;
            return {.item = b2it(false)};  // false for boolean flags
        }
        // v5: a byte count is an `int` (the int53 band reaches 9 PB), so it
        // boxes inline. `box_int64_value` here was a pre-v5 leftover from
        // 32-bit `int`, and it was the only reason this path could touch the
        // number stack.
        if (strcmp(k, "size") == 0) return {.item = i2it(meta->size)};
        if (strcmp(k, "modified") == 0) return push_k(meta->modified);
        if (strcmp(k, "is_dir") == 0) return {.item = b2it((meta->flags & PATH_META_IS_DIR) != 0)};
        if (strcmp(k, "is_file") == 0) return {.item = b2it((meta->flags & PATH_META_IS_DIR) == 0)};
        if (strcmp(k, "is_link") == 0) return {.item = b2it((meta->flags & PATH_META_IS_LINK) != 0)};
        if (strcmp(k, "mode") == 0) return {.item = i2it(meta->mode)};
    }

    log_debug("path_property_get: unknown path property '%s'", k);
    return ItemNull;
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
            if (is_text_type_id(key._type_id)) {
                const char* k = key.get_chars();
                if (k && path_is_property_name(k)) return path_property_get(path, k);
            }
            Pool* pool = context ? context->pool : NULL;
            if (get_type_id(key) == LMD_TYPE_INT && key.int_val >= 0) {
                return {.item = (uint64_t)(uintptr_t)path_extend_int(pool, path, key.int_val)};
            }
            if (get_type_id(key) == LMD_TYPE_INT64 && key.get_int64() >= 0) {
                return {.item = (uint64_t)(uintptr_t)path_extend_int(pool, path, key.get_int64())};
            }
            if (is_text_type_id(key._type_id) && key.get_chars()) {
                return {.item = (uint64_t)(uintptr_t)path_extend(pool, path, key.get_chars())};
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
        // v5: unix-ms is an `int` (the int53 band reaches year 285,000), so it
        // boxes inline rather than taking a number home — same pre-v5 leftover.
        if (strcmp(k, "unix") == 0)           return {.item = i2it(datetime_to_unix_ms(&dt))};
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
        if (is_text_type_id(key._type_id)) {
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
                if (strcmp(method->name->str, key_str) == 0 && method->compiled_fn) {
                    return lambda_bind_object_method(method, item);
                }
                method = method->next;
            }
            // Walk inheritance chain
            TypeObject* base = obj_type->base;
            while (base) {
                TypeMethod* bmethod = base->methods;
                while (bmethod) {
                    if (strcmp(bmethod->name->str, key_str) == 0 && bmethod->compiled_fn) {
                        return lambda_bind_object_method(bmethod, item);
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
        // A host projection is a map even when it has no local vtable; its
        // properties are supplied by vmap_get_by_item's host dispatch.
        if (!vm || (!vm->host_type && !vm->vtable)) return ItemNull;
        return vmap_get_by_item(vm, key);
    }
    case LMD_TYPE_ELEMENT: {
        Element *elmt = item.element;
        return elmt_get(elmt, key);
    }
    case LMD_TYPE_ARRAY:
    case LMD_TYPE_ARRAY_NUM: {
        // Handle built-in properties for List type
        if (is_text_type_id(key._type_id)) {
            const char* k = key.get_chars();
            if (k && strcmp(k, "length") == 0) {
                List *list = item.array;
                return {.item = i2it(list->length)};
            }
        }
        int64_t index = -1;
        // Numeric dotted members are IntKey access (`a.1`), not NameKey
        // lookup; route them through the existing indexed-array helper.
        if (lambda_item_to_int64_exact(key, &index)) {
            return item_at(item, index);
        }
        return ItemNull;
    }
    // todo: built-in properties for other types
    default:
        return ItemNull;
    }
}

extern "C" Item fn_member_by_id(Item item, NameId name_id) {
    // A generated static member name is already resolved in the active module
    // state. Maps and objects can consume that identity directly; only the
    // id-less/input and non-map property paths need the legacy boxed fallback.
    if (name_id == NAME_ID_NONE) return ItemNull;
    TypeId type_id = get_type_id(item);
    bool is_found = false;
    switch (type_id) {
    case LMD_TYPE_MAP: {
        Map* map = item.map;
        if (map) {
            Item result = map_get_by_name_id((Container*)map,
                (TypeMap*)map->type, map->data, name_id, &is_found);
            if (is_found) return result;
        }
        break;
    }
    case LMD_TYPE_OBJECT: {
        Object* object = item.object;
        if (object) {
            Item result = map_get_by_name_id((Container*)object,
                (TypeMap*)object->type, object->data, name_id, &is_found);
            if (is_found) return result;
        }
        break;
    }
    // Elements have content/attribute lookup rules beyond their map-shaped
    // storage (for example a literal attribute can share an element shape
    // identity). Keep them on fn_member's spelling-aware path until those
    // rules are represented in the raw NameId ABI (D4.6.1v2-D4.6.2v2).
    default:
        break;
    }

    // Id-less input shapes and built-in properties still require spelling
    // semantics. This is cold relative to generated map/object field hops and
    // preserves fn_member's path, datetime, vmap, array, and element rules.
    Item key = lambda_name_id_to_item(name_id);
    return fn_member(item, key);
}

// length of an item's content, relates to indexed access, i.e. item[index]
int64_t fn_len(Item item) {
    TypeId type_id = get_type_id(item);
    int64_t size = 0;
    switch (type_id) {
    case LMD_TYPE_ARRAY:
        size = item.array->length;
        break;
    case LMD_TYPE_RANGE:
        size = item.range->length;
        break;
    case LMD_TYPE_ARRAY_NUM:
        size = array_num_iter_count(item.array_num);
        break;
    case LMD_TYPE_MAP:
    case LMD_TYPE_OBJECT: {
        // A map is iterable in Lambda exactly like an array -- `for (v in m)`
        // walks its entries -- so its length is that entry count. Both arms
        // were hardcoded to 0, which made `len(m)` disagree with the loop that
        // ranges over the same map, and made a populated map compare equal to
        // an empty one through `len`. OBJECT extends MAP and shares the shape.
        Map* map = item.map;
        TypeMap* map_type = map ? (TypeMap*)map->type : NULL;
        size = map_type ? map_type->length : 0;
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
    case LMD_TYPE_BINARY:
        // Binary length is a byte count, including embedded NUL bytes.
        size = item.get_len();
        break;
    case LMD_TYPE_STRING:  case LMD_TYPE_SYMBOL: {
        // returns the character length of text values
        const char* chars = item.get_chars();
        if (!chars) { size = 0; break; }
        bool is_ascii = false;
        uint32_t len = item.get_len();
        if (type_id == LMD_TYPE_STRING) {
            String* str = item.get_safe_string();
            is_ascii = str->is_ascii != 0;
        }
        size = is_ascii ? (int64_t)len : (int64_t)str_utf8_count(chars, len);
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
                // 7.6: `len` is total -- an unresolvable path iterates zero
                // times. Returning INT64_ERROR here leaked the sentinel out of
                // the one function the spec guarantees never fails.
                size = 0;
                break;
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
        // Formal semantics 7.6: `len` is a value function, so an error
        // propagates -- iterating an error yields an error, not nothing, which
        // is what distinguishes it from `len(null)` = 0. The parameter is
        // `any \ error` (7.7), so an error is rejected at the call boundary and
        // never reaches this body; the arm is a defensive floor. It answers 0
        // rather than the retired INT64_ERROR sentinel, which a double lane
        // cannot reject (INT64_MAX is an ordinary int under C16) and which once
        // reached callers as a real length -- `err |> ~` took it as an
        // iteration bound and attempted repeated 2 GB allocations.
        size = 0;
        break;
    default: // NULL and scalar types
        size = 0;
        break;
    }
    return size;
}

// Native len variants — type-specialized, avoid Item type switch overhead
// Used when compile-time type is known. The transpiler must unbox Items to raw
// pointers before calling these (emit_unbox_container strips tag bits).

extern "C" int64_t fn_len_l(List* list) {
    if (!list) return 0;
    // Runtime check: nested numeric literals may have been auto-promoted from
    // List/Array to N-D ArrayNum (the JIT dispatched based on static type).
    if (list->type_id == LMD_TYPE_ARRAY_NUM) {
        return array_num_iter_count((ArrayNum*)list);
    }
    return list->length;
}

extern "C" int64_t fn_len_a(Array* arr) {
    if (!arr) return 0;
    // Runtime check: static type may say Array but actual could be ArrayNum
    // (e.g. nested literals auto-promoted to N-D). For N-D ArrayNum, return
    // shape[0] (leading axis) to match NumPy semantics.
    if (arr->type_id == LMD_TYPE_ARRAY_NUM) {
        return array_num_iter_count((ArrayNum*)arr);
    }
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
    if (!is_text_type_id(str_tid)) {
        log_debug("fn_substring: first argument must be a string or symbol");
        return ItemError;
    }

    int64_t start = 0;
    int64_t end = 0;
    LambdaSliceStatus status = lambda_slice_offsets(start_item, end_item, &start, &end);
    if (status == LAMBDA_SLICE_ABSENT) return ItemNull;
    if (status == LAMBDA_SLICE_INVALID) {
        log_debug("fn_substring: start and end must be integer-valued numbers");
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
        String* str = str_item.get_safe_string();
        if (!str) return ItemNull;
        is_ascii = str->is_ascii != 0;
    }

    // ASCII fast path: char index == byte index
    if (is_ascii) {
        int64_t char_len = (int64_t)str_len;
        // negative offsets clamp, they do not wrap from the end
        lambda_clamp_slice_range(char_len, &start, &end);
        if (start >= end) {
            if (is_symbol) return {.item = y2it(heap_create_symbol("", 0))};
            String* empty = (String *)heap_alloc(sizeof(String) + 1, LMD_TYPE_STRING);
            empty->len = 0;
            empty->flags = 0;
            empty->is_ascii = 1;
            empty->chars[0] = '\0';
            return {.item = s2it(empty)};
        }
        long result_len = (long)(end - start);
        if (is_symbol) return {.item = y2it(heap_create_symbol(chars + start, result_len))};
        String* result = (String *)heap_alloc(sizeof(String) + result_len + 1, LMD_TYPE_STRING);
        result->len = result_len;
        result->flags = 0;
        result->is_ascii = 1;
        memcpy(result->chars, chars + start, result_len);
        result->chars[result_len] = '\0';
        return {.item = s2it(result)};
    }

    // negative offsets clamp, they do not wrap from the end
    int64_t char_len = (int64_t)str_utf8_count(chars, str_len);
    lambda_clamp_slice_range(char_len, &start, &end);
    if (start >= end) {
        if (is_symbol) return {.item = y2it(heap_create_symbol("", 0))};
        String* empty = (String *)heap_alloc(sizeof(String) + 1, LMD_TYPE_STRING);
        empty->len = 0;
        empty->flags = 0;
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
        empty->flags = 0;
        empty->is_ascii = 1;
        empty->chars[0] = '\0';
        return {.item = s2it(empty)};
    }

    long result_len = byte_end - byte_start;
    if (is_symbol) return {.item = y2it(heap_create_symbol(chars + byte_start, result_len))};
    String* result = (String *)heap_alloc(sizeof(String) + result_len + 1, LMD_TYPE_STRING);
    result->len = result_len;
    result->flags = 0;
    result->is_ascii = 0;  // UTF-8 path — not necessarily ASCII
    memcpy(result->chars, chars + byte_start, result_len);
    result->chars[result_len] = '\0';

    return {.item = s2it(result)};
}

// contains system function - checks if a string contains a substring
Bool fn_contains(Item str_item, Item substr_item) {
    GUARD_BOOL_ERROR2(str_item, substr_item);
    TypeId coll_type = get_type_id(str_item);
    // null doesn't contain anything, and nothing contains null
    if (coll_type == LMD_TYPE_NULL || get_type_id(substr_item) == LMD_TYPE_NULL) {
        return BOOL_FALSE;
    }

    // --- List/Array: check if any element equals the item ---
    if (coll_type == LMD_TYPE_ARRAY) {
        List* list = str_item.array;
        if (!list) return BOOL_FALSE;
        for (int64_t i = 0; i < list->length; i++) {
            if (fn_eq(list->items[i], substr_item) == BOOL_TRUE) {
                return BOOL_TRUE;
            }
        }
        return BOOL_FALSE;
    }
    if (coll_type == LMD_TYPE_ARRAY_NUM) {
        ArrayNum* arr = str_item.array_num;
        return array_num_find_equal(arr, substr_item, false) >= 0 ? BOOL_TRUE : BOOL_FALSE;
    }

    // --- Map: check if any key matches ---
    if (coll_type == LMD_TYPE_MAP) {
        Map* map = str_item.map;
        if (!map) return BOOL_FALSE;
        Item val = map_get(map, substr_item);
        return (get_type_id(val) != LMD_TYPE_NULL) ? BOOL_TRUE : BOOL_FALSE;
    }

    // --- Element: check attribute keys and children ---
    if (coll_type == LMD_TYPE_ELEMENT) {
        Element* el = str_item.element;
        if (!el) return BOOL_FALSE;
        // check children
        for (int64_t i = 0; i < el->length; i++) {
            if (fn_eq(el->items[i], substr_item) == BOOL_TRUE) {
                return BOOL_TRUE;
            }
        }
        return BOOL_FALSE;
    }

    // --- String: substring check ---
    if (coll_type != LMD_TYPE_STRING) {
        log_debug("fn_contains: first argument must be a string, list, map, or element");
        return BOOL_ERROR;
    }

    if (get_type_id(substr_item) != LMD_TYPE_STRING) {
        log_debug("fn_contains: second argument must be a string for string contains");
        return BOOL_ERROR;
    }

    String* str = str_item.get_safe_string();
    String* substr = substr_item.get_safe_string();

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
    for (uint32_t i = 0; i <= str->len - substr->len; i++) {
        if (memcmp(str->chars + i, substr->chars, substr->len) == 0) {
            return BOOL_TRUE;
        }
    }

    return BOOL_FALSE;
}

// starts_with native: String* in, Bool out (no Item boxing)
Bool fn_starts_with_str(String* str, String* prefix) {
    if (!str) return BOOL_FALSE;
    if (!prefix || prefix->len == 0) return BOOL_TRUE;
    if (str->len < prefix->len) return BOOL_FALSE;
    return (memcmp(str->chars, prefix->chars, prefix->len) == 0) ? BOOL_TRUE : BOOL_FALSE;
}

// starts_with(str, prefix) - check if string starts with prefix
Bool fn_starts_with(Item str_item, Item prefix_item) {
    GUARD_BOOL_ERROR2(str_item, prefix_item);
    TypeId str_type = get_type_id(str_item);
    TypeId prefix_type = get_type_id(prefix_item);

    if (str_type == LMD_TYPE_NULL) return BOOL_FALSE;
    if (prefix_type == LMD_TYPE_NULL) return BOOL_TRUE;

    if ((!is_text_type_id(str_type)) ||
        (!is_text_type_id(prefix_type))) {
        log_debug("fn_starts_with: arguments must be strings or symbols");
        return BOOL_ERROR;
    }

    const char* str_chars = str_item.get_chars();
    uint32_t str_len = str_item.get_len();
    const char* prefix_chars = prefix_item.get_chars();
    uint32_t prefix_len = prefix_item.get_len();

    if (!str_chars || !prefix_chars) return BOOL_FALSE;
    if (prefix_len == 0) return BOOL_TRUE;
    if (str_len < prefix_len) return BOOL_FALSE;

    return (memcmp(str_chars, prefix_chars, prefix_len) == 0) ? BOOL_TRUE : BOOL_FALSE;
}

// ends_with native: String* in, Bool out (no Item boxing)
Bool fn_ends_with_str(String* str, String* suffix) {
    if (!str) return BOOL_FALSE;
    if (!suffix || suffix->len == 0) return BOOL_TRUE;
    if (str->len < suffix->len) return BOOL_FALSE;
    size_t offset = str->len - suffix->len;
    return (memcmp(str->chars + offset, suffix->chars, suffix->len) == 0) ? BOOL_TRUE : BOOL_FALSE;
}

// ends_with(str, suffix) - check if string ends with suffix
Bool fn_ends_with(Item str_item, Item suffix_item) {
    GUARD_BOOL_ERROR2(str_item, suffix_item);
    TypeId str_type = get_type_id(str_item);
    TypeId suffix_type = get_type_id(suffix_item);

    if (str_type == LMD_TYPE_NULL) return BOOL_FALSE;
    if (suffix_type == LMD_TYPE_NULL) return BOOL_TRUE;

    if ((!is_text_type_id(str_type)) ||
        (!is_text_type_id(suffix_type))) {
        log_debug("fn_ends_with: arguments must be strings or symbols");
        return BOOL_ERROR;
    }

    const char* str_chars = str_item.get_chars();
    uint32_t str_len = str_item.get_len();
    const char* suffix_chars = suffix_item.get_chars();
    uint32_t suffix_len = suffix_item.get_len();

    if (!str_chars || !suffix_chars) return BOOL_FALSE;
    if (suffix_len == 0) return BOOL_TRUE;
    if (str_len < suffix_len) return BOOL_FALSE;

    size_t offset = str_len - suffix_len;
    return (memcmp(str_chars + offset, suffix_chars, suffix_len) == 0) ? BOOL_TRUE : BOOL_FALSE;
}

// index_of raw search helper — -1 is retained only for C/JS adapters.
// The public Lambda wrapper below converts absence to null.
static int64_t fn_index_of_raw_impl(Item str_item, Item sub_item, bool reverse) {
    // This raw helper keeps the C-family -1 ABI for foreign adapters. Lambda's
    // public wrapper maps its broad-input no-answer result to null.
    if (get_type_id(str_item) == LMD_TYPE_ERROR || get_type_id(sub_item) == LMD_TYPE_ERROR) {
        return -1;
    }
    TypeId coll_type = get_type_id(str_item);
    if (coll_type == LMD_TYPE_ARRAY) {
        List* list = str_item.array;
        if (!list) return -1;
        if (!reverse) {
            for (int64_t i = 0; i < list->length; i++) {
                if (fn_eq(list->items[i], sub_item) == BOOL_TRUE) return i;
            }
        } else {
            for (int64_t i = list->length - 1; i >= 0; i--) {
                if (fn_eq(list->items[i], sub_item) == BOOL_TRUE) return i;
            }
        }
        return -1;
    }
    if (coll_type == LMD_TYPE_ARRAY_NUM) {
        return array_num_find_equal(str_item.array_num, sub_item, reverse);
    }

    TypeId sub_type = get_type_id(sub_item);
    if (!is_text_type_id(coll_type) || !is_text_type_id(sub_type)) {
        log_debug("fn_%sindex_of: arguments must be strings/symbols or first arg must be a list",
            reverse ? "last_" : "");
        return -1;
    }
    const char* str_chars = str_item.get_chars();
    uint32_t str_len = str_item.get_len();
    const char* sub_chars = sub_item.get_chars();
    uint32_t sub_len = sub_item.get_len();
    if (!str_chars || !sub_chars) return -1;

    bool is_ascii = false;
    if (coll_type == LMD_TYPE_STRING) {
        String* str = str_item.get_safe_string();
        if (!str) return -1;
        is_ascii = str->is_ascii != 0;
    } else {
        is_ascii = str_is_ascii(str_chars, str_len);
    }
    if (sub_len == 0) {
        if (!reverse) return 0;
        return is_ascii ? (int64_t)str_len : (int64_t)str_utf8_count(str_chars, str_len);
    }
    if (str_len < sub_len) return -1;

    if (!reverse) {
        for (size_t i = 0; i <= str_len - sub_len; i++) {
            if (memcmp(str_chars + i, sub_chars, sub_len) == 0)
                return is_ascii ? (int64_t)i : (int64_t)str_utf8_count(str_chars, i);
        }
    } else {
        for (size_t i = str_len - sub_len + 1; i > 0; i--) {
            size_t pos = i - 1;
            if (memcmp(str_chars + pos, sub_chars, sub_len) == 0)
                return is_ascii ? (int64_t)pos : (int64_t)str_utf8_count(str_chars, pos);
        }
    }
    return -1;
}

int64_t fn_index_of_raw(Item str_item, Item sub_item) {
    return fn_index_of_raw_impl(str_item, sub_item, false);
}

// Lambda's public search result uses null for absence. Keep the raw -1 helper
// separate because JavaScript's String#indexOf family requires that sentinel.
static Item fn_nullable_int_result(int64_t result) {
    return result < 0 ? ItemNull : (Item){.item = i2it(result)};
}

Item fn_index_of(Item str_item, Item sub_item) {
    // preserve the original error payload; only a successful no-match becomes null.
    GUARD_ERROR2(str_item, sub_item);
    return fn_nullable_int_result(fn_index_of_raw(str_item, sub_item));
}

// last_index_of raw search helper — -1 is retained only for C/JS adapters.
// The public Lambda wrapper below converts absence to null.
int64_t fn_last_index_of_raw(Item str_item, Item sub_item) {
    return fn_index_of_raw_impl(str_item, sub_item, true);
}

Item fn_last_index_of(Item str_item, Item sub_item) {
    GUARD_ERROR2(str_item, sub_item);
    return fn_nullable_int_result(fn_last_index_of_raw(str_item, sub_item));
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

    if (!is_text_type_id(str_type)) {
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
        // Trimming can legitimately produce an empty string; only solid symbols collapse to null.
        if (str_type == LMD_TYPE_SYMBOL) return ItemNull;
        String* empty = heap_strcpy("", 0);
        return {.item = s2it(empty)};
    }

    size_t result_len = end - start;
    if (str_type == LMD_TYPE_SYMBOL) {
        return {.item = y2it(heap_create_symbol(chars + start, result_len))};
    }

    String* result = (String *)heap_alloc(sizeof(String) + result_len + 1, LMD_TYPE_STRING);
    result->len = result_len;
    result->flags = 0;
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

    if (!is_text_type_id(str_type)) {
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
        // Trimming can legitimately produce an empty string; only solid symbols collapse to null.
        if (str_type == LMD_TYPE_SYMBOL) return ItemNull;
        String* empty = heap_strcpy("", 0);
        return {.item = s2it(empty)};
    }
    if (str_type == LMD_TYPE_SYMBOL) {
        return {.item = y2it(heap_create_symbol(chars + start, result_len))};
    }

    String* result = (String *)heap_alloc(sizeof(String) + result_len + 1, LMD_TYPE_STRING);
    result->len = result_len;
    result->flags = 0;
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

    if (!is_text_type_id(str_type)) {
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
        // Trimming can legitimately produce an empty string; only solid symbols collapse to null.
        if (str_type == LMD_TYPE_SYMBOL) return ItemNull;
        String* empty = heap_strcpy("", 0);
        return {.item = s2it(empty)};
    }

    if (str_type == LMD_TYPE_SYMBOL) {
        return {.item = y2it(heap_create_symbol(chars, end))};
    }

    String* result = (String *)heap_alloc(sizeof(String) + end + 1, LMD_TYPE_STRING);
    result->len = end;
    result->flags = 0;
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

    if (!is_text_type_id(str_type)) {
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
        char* lower_chars = (len < sizeof(stack_buf)) ? stack_buf : (char*)mem_alloc(len + 1, MEM_CAT_EVAL);
        for (uint32_t i = 0; i < len; i++) {
            char c = chars[i];
            lower_chars[i] = (c >= 'A' && c <= 'Z') ? (c + 32) : c;
        }
        lower_chars[len] = '\0';
        Symbol* sym = heap_create_symbol(lower_chars, len);
        if (lower_chars != stack_buf) mem_free(lower_chars);
        return {.item = y2it(sym)};
    }

    String* result = (String *)heap_alloc(sizeof(String) + len + 1, LMD_TYPE_STRING);
    result->len = len;
    result->flags = 0;
    String* src = str_item.get_safe_string();
    result->is_ascii = src ? src->is_ascii : 0;  // case conversion preserves ASCII status
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

    if (!is_text_type_id(str_type)) {
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
        char* upper_chars = (len < sizeof(stack_buf)) ? stack_buf : (char*)mem_alloc(len + 1, MEM_CAT_EVAL);
        for (uint32_t i = 0; i < len; i++) {
            char c = chars[i];
            upper_chars[i] = (c >= 'a' && c <= 'z') ? (c - 32) : c;
        }
        upper_chars[len] = '\0';
        Symbol* sym = heap_create_symbol(upper_chars, len);
        if (upper_chars != stack_buf) mem_free(upper_chars);
        return {.item = y2it(sym)};
    }

    String* result = (String *)heap_alloc(sizeof(String) + len + 1, LMD_TYPE_STRING);
    result->len = len;
    result->flags = 0;
    String* src = str_item.get_safe_string();
    result->is_ascii = src ? src->is_ascii : 0;  // case conversion preserves ASCII status
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

    if ((!is_text_type_id(base_type)) ||
        (!is_text_type_id(rel_type))) {
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
    result->flags = 0;
    result->is_ascii = 1;  // URLs are ASCII
    memcpy(result->chars, href, len);
    result->chars[len] = '\0';

    url_destroy(resolved);
    url_destroy(base_url);
    return {.item = s2it(result)};
}

static String* split_heap_string_slice(Rooted<Item>& rooted_source, size_t offset, size_t len) {
    String* part = (String*)heap_alloc(sizeof(String) + len + 1, LMD_TYPE_STRING);
    // The allocation can compact the source string, so reload its interior
    // character address from the exact root before examining or copying it.
    const char* chars = rooted_source.get().get_chars() + offset;
    part->len = (uint32_t)len;
    part->flags = 0;
    part->is_ascii = str_is_ascii(chars, len) ? 1 : 0;
    memcpy(part->chars, chars, len);
    part->chars[len] = '\0';
    return part;
}

static bool literal_type_pattern_item(Item type_item, Item* literal_item);
static TypePattern* runtime_pattern_from_type(Type* type);

// split(str, sep) - split string by separator, returns list of strings
Item fn_split(Item str_item, Item sep_item) {
    // every split operand participates in dispatch, so no error may fall through to null or whitespace handling
    GUARD_ERROR2(str_item, sep_item);
    TypeId str_type = get_type_id(str_item);
    TypeId sep_type = get_type_id(sep_item);

    Item literal_pattern = {.item = 0};
    if (literal_type_pattern_item(sep_item, &literal_pattern)) {
        sep_item = literal_pattern;
        sep_type = LMD_TYPE_STRING;
    }

    // typed array split: split(arr, n) → n equal parts along axis 0
    if (str_type == LMD_TYPE_ARRAY_NUM) {
        int64_t n = 0;
        if (!lambda_item_to_int64_exact(sep_item, &n)) {
            log_error("split: section count must be an integer-valued number");
            return ItemError;
        }
        return fn_array_split(str_item, n, 0);
    }

    // null string splits to empty list
    if (str_type == LMD_TYPE_NULL) { List* e = list(); e->is_content = 1; return {.array = e}; }

    // null separator means split on whitespace (Python convention)
    bool null_sep = (sep_type == LMD_TYPE_NULL);

    // pattern-based split: split(str, pattern)
    if (sep_type == LMD_TYPE_TYPE) {
        Type* type = (Type*)(sep_item.item & 0x00FFFFFFFFFFFFFF);
        TypePattern* pattern = runtime_pattern_from_type(type);
        if (pattern) {
            if (pattern->is_symbol) {
                log_error("fn_split: symbol-domain patterns cannot search strings");
                return ItemError;
            }
            if (!is_text_type_id(str_type)) {
                log_debug("fn_split: first argument must be a string for pattern split");
                return ItemError;
            }
            List* ps = pattern_split(pattern, str_item, false);
            if (ps) ps->is_content = 1;
            return {.array = ps};
        }
    }

    if ((!is_text_type_id(str_type)) ||
        (!null_sep && !is_text_type_id(sep_type))) {
        log_debug("fn_split: arguments must be strings or symbols");
        return ItemError;
    }

    RootFrame roots(3);
    Rooted<Item> rooted_str(roots, str_item);
    Rooted<Item> rooted_sep(roots, sep_item);
    Rooted<List*> rooted_result(roots, (List*)NULL);

    uint32_t str_len = str_item.get_len();
    uint32_t sep_len = null_sep ? 0 : sep_item.get_len();

    // disable string merging in list_push so split results stay separate
    bool saved_merging = false;
    if (context) {
        saved_merging = context->disable_string_merging;
        context->disable_string_merging = true;
    }

    List* result = list();
    rooted_result.set(result);
    result->is_content = 1;

    if (!rooted_str.get().get_chars() || str_len == 0) {
        if (context) { context->disable_string_merging = saved_merging; }
        return {.array = rooted_result.get()};  // empty list for empty string
    }

    if (null_sep) {
        // null separator: split on whitespace (like Python str.split(None))
        // strips leading/trailing whitespace, splits on runs of whitespace
        size_t p = 0;
        while (p < str_len) {
            const char* str_chars = rooted_str.get().get_chars();
            // skip whitespace
            while (p < str_len && (str_chars[p] == ' ' || str_chars[p] == '\t' || str_chars[p] == '\n' ||
                    str_chars[p] == '\r' || str_chars[p] == '\f' || str_chars[p] == '\v')) {
                p++;
            }
            if (p >= str_len) break;
            // find end of word
            size_t word_start = p;
            while (p < str_len && str_chars[p] != ' ' && str_chars[p] != '\t' && str_chars[p] != '\n' &&
                    str_chars[p] != '\r' && str_chars[p] != '\f' && str_chars[p] != '\v') {
                p++;
            }
            String* part = split_heap_string_slice(rooted_str, word_start, p - word_start);
            list_push(rooted_result.get(), {.item = s2it(part)});
        }
        if (context) { context->disable_string_merging = saved_merging; }
        return {.array = rooted_result.get()};
    }

    if (!rooted_sep.get().get_chars() || sep_len == 0) {
        // empty string separator: split into individual characters
        size_t p = 0;
        while (p < str_len) {
            const char* str_chars = rooted_str.get().get_chars();
            int char_len = (int)str_utf8_char_len((unsigned char)str_chars[p]);
            if (char_len <= 0) char_len = 1;  // fallback for invalid UTF-8
            String* part = split_heap_string_slice(rooted_str, p, (size_t)char_len);
            list_push(rooted_result.get(), {.item = s2it(part)});
            p += char_len;
        }
        if (context) { context->disable_string_merging = saved_merging; }
        return {.array = rooted_result.get()};
    }

    // split by separator
    size_t start = 0;
    size_t p = 0;

    while (p + sep_len <= str_len) {
        const char* str_chars = rooted_str.get().get_chars();
        const char* sep_chars = rooted_sep.get().get_chars();
        if (memcmp(str_chars + p, sep_chars, sep_len) == 0) {
            // found separator
            size_t part_len = p - start;
            String* part = split_heap_string_slice(rooted_str, start, part_len);
            list_push(rooted_result.get(), {.item = s2it(part)});

            p += sep_len;
            start = p;
        } else {
            p++;
        }
    }

    // add the last part
    String* part = split_heap_string_slice(rooted_str, start, str_len - start);
    list_push(rooted_result.get(), {.item = s2it(part)});

    if (context) { context->disable_string_merging = saved_merging; }
    return {.array = rooted_result.get()};
}

// split(str, sep, keep_delim) - 3-arg version with keep_delim boolean
// split(arr, n, axis) - typed array split into n equal parts along axis
Item fn_split3(Item str_item, Item sep_item, Item keep_item) {
    GUARD_ERROR3(str_item, sep_item, keep_item);
    TypeId str_type = get_type_id(str_item);
    TypeId sep_type = get_type_id(sep_item);
    TypeId keep_type = get_type_id(keep_item);

    Item literal_pattern = {.item = 0};
    if (literal_type_pattern_item(sep_item, &literal_pattern)) {
        sep_item = literal_pattern;
        sep_type = LMD_TYPE_STRING;
    }

    // typed array split with explicit axis: split(arr, n, axis)
    if (str_type == LMD_TYPE_ARRAY_NUM) {
        int64_t n = 0, axis = 0;
        if (!lambda_item_to_int64_exact(sep_item, &n) ||
                !lambda_item_to_int64_exact(keep_item, &axis)) {
            log_error("split: section count and axis must be integer-valued numbers");
            return ItemError;
        }
        return fn_array_split(str_item, n, axis);
    }

    bool keep_delim = (keep_type == LMD_TYPE_BOOL && it2b(keep_item));

    // null string splits to empty list
    if (str_type == LMD_TYPE_NULL) { List* e = list(); e->is_content = 1; return {.array = e}; }

    // pattern-based split with keep_delim
    if (sep_type == LMD_TYPE_TYPE) {
        Type* type = (Type*)(sep_item.item & 0x00FFFFFFFFFFFFFF);
        TypePattern* pattern = runtime_pattern_from_type(type);
        if (pattern) {
            if (pattern->is_symbol) {
                log_error("fn_split3: symbol-domain patterns cannot search strings");
                return ItemError;
            }
            if (!is_text_type_id(str_type)) {
                log_debug("fn_split3: first argument must be a string for pattern split");
                return ItemError;
            }
            List* ps = pattern_split(pattern, str_item, keep_delim);
            if (ps) ps->is_content = 1;
            return {.array = ps};
        }
    }

    // string-based split with keep_delim
    if (!keep_delim) {
        // no keep_delim, delegate to 2-arg version
        return fn_split(str_item, sep_item);
    }

    // keep_delim string split
    if ((!is_text_type_id(str_type)) ||
        (!is_text_type_id(sep_type))) {
        log_debug("fn_split3: arguments must be strings or symbols");
        return ItemError;
    }

    uint32_t str_len = str_item.get_len();
    uint32_t sep_len = sep_item.get_len();

    RootFrame roots(3);
    Rooted<Item> rooted_str(roots, str_item);
    Rooted<Item> rooted_sep(roots, sep_item);
    Rooted<List*> rooted_result(roots, (List*)NULL);

    bool saved_merging = false;
    if (context) {
        saved_merging = context->disable_string_merging;
        context->disable_string_merging = true;
    }

    List* result = list();
    rooted_result.set(result);
    result->is_content = 1;
    if (!rooted_str.get().get_chars() || str_len == 0 || !rooted_sep.get().get_chars() || sep_len == 0) {
        if (context) { context->disable_string_merging = saved_merging; }
        return {.array = rooted_result.get()};
    }

    size_t start = 0;
    size_t p = 0;

    while (p + sep_len <= str_len) {
        const char* str_chars = rooted_str.get().get_chars();
        const char* sep_chars = rooted_sep.get().get_chars();
        if (memcmp(str_chars + p, sep_chars, sep_len) == 0) {
            // push part before separator
            size_t part_len = p - start;
            String* part = split_heap_string_slice(rooted_str, start, part_len);
            list_push(rooted_result.get(), {.item = s2it(part)});

            // push the delimiter
            String* delim = split_heap_string_slice(rooted_sep, 0, sep_len);
            list_push(rooted_result.get(), {.item = s2it(delim)});

            p += sep_len;
            start = p;
        } else {
            p++;
        }
    }

    // add the last part
    String* part = split_heap_string_slice(rooted_str, start, str_len - start);
    list_push(rooted_result.get(), {.item = s2it(part)});

    if (context) { context->disable_string_merging = saved_merging; }
    return {.array = rooted_result.get()};
}

// ord raw helper — -1 is retained only for C/JS adapters.
// The public Lambda wrapper below converts absence to null.
int64_t fn_ord_str(String* str) {
    // 0 is U+0000, a real character, so it cannot stand for "no answer".
    if (!str || str->len == 0) return -1;
    uint32_t codepoint = 0;
    int decoded = str_utf8_decode(str->chars, str->len, &codepoint);
    if (decoded <= 0) return -1;
    return (int64_t)codepoint;
}

int64_t fn_ord_raw(Item str_item) {
    TypeId type = get_type_id(str_item);

    // Code points are non-negative, so the raw -1 sentinel cannot collide with
    // a real answer. The Lambda wrapper normalizes it to the uniform null value.
    if (!is_text_type_id(type)) {
        log_debug("fn_ord: argument must be a string or symbol, got type %d", type);
        return -1;
    }

    const char* chars = str_item.get_chars();
    uint32_t byte_len = str_item.get_len();

    // an empty string has no first character, so it has no ordinal
    if (!chars || byte_len == 0) {
        return -1;
    }

    uint32_t codepoint = 0;
    int decoded = str_utf8_decode(chars, byte_len, &codepoint);
    if (decoded <= 0) {
        return -1; // invalid UTF-8
    }
    return (int64_t)codepoint;
}

Item fn_ord(Item str_item) {
    GUARD_ERROR1(str_item);
    return fn_nullable_int_result(fn_ord_raw(str_item));
}

Item fn_ord_str_item(String* str) {
    return fn_nullable_int_result(fn_ord_str(str));
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

    int64_t codepoint = 0;
    if (!lambda_item_to_int64_exact(cp_item, &codepoint) ||
            codepoint < 0 || codepoint > 0x10FFFF) {
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

    String* result = heap_strcpy(buf, len);
    return {.item = s2it(result)};
}

// join(strs, sep) - join list of strings with separator
Item fn_join2(Item list_item, Item sep_item) {
    GUARD_ERROR2(list_item, sep_item);
    TypeId list_type = get_type_id(list_item);
    TypeId sep_type = get_type_id(sep_item);

    // null list joins to null
    if (list_type == LMD_TYPE_NULL) return ItemNull;

    if (list_type != LMD_TYPE_ARRAY && list_type != LMD_TYPE_ARRAY) {
        log_debug("fn_join2: first argument must be a list or array");
        return ItemError;
    }

    // allow null/empty separator - treat as empty string
    const char* sep_chars = nullptr;
    size_t sep_len = 0;

    if (is_text_type_id(sep_type)) {
        sep_chars = sep_item.get_chars();
        sep_len = sep_chars ? sep_item.get_len() : 0;
    } else if (sep_type != LMD_TYPE_NULL) {
        log_debug("fn_join2: separator must be a string, symbol, or null");
        return ItemError;
    }

    RootFrame roots(2);
    Rooted<Item> rooted_list(roots, list_item);
    Rooted<Item> rooted_sep(roots, sep_item);

    // calculate total length
    size_t total_len = 0;
    int64_t count = 0;

    if (list_type == LMD_TYPE_ARRAY) {
        List* lst = list_item.array;
        count = lst->length;
        for (int64_t i = 0; i < count; i++) {
            Item item = lst->items[i];
            TypeId item_type = get_type_id(item);
            if (is_text_type_id(item_type)) {
                total_len += item.get_len();
            }
        }
    } else {
        Array* arr = list_item.array;
        count = arr->length;
        for (int64_t i = 0; i < count; i++) {
            Item item = arr->items[i];
            TypeId item_type = get_type_id(item);
            if (is_text_type_id(item_type)) {
                total_len += item.get_len();
            }
        }
    }

    if (count > 1 && sep_len > 0) {
        total_len += (count - 1) * sep_len;
    }

    // allocate result
    String* result = (String *)heap_alloc(sizeof(String) + total_len + 1, LMD_TYPE_STRING);
    // Result allocation may compact container storage and can reclaim the
    // separator unless both semantic inputs remain exact-rooted.
    list_item = rooted_list.get();
    sep_item = rooted_sep.get();
    sep_chars = is_text_type_id(sep_type) ? sep_item.get_chars() : nullptr;
    result->len = total_len;
    result->flags = 0;
    result->is_ascii = 0;  // safe default for join

    // build result
    char* p = result->chars;

    if (list_type == LMD_TYPE_ARRAY) {
        List* lst = list_item.array;
        for (int64_t i = 0; i < count; i++) {
            if (i > 0 && sep_len > 0) {
                memcpy(p, sep_chars, sep_len);
                p += sep_len;
            }
            Item item = lst->items[i];
            TypeId item_type = get_type_id(item);
            if (is_text_type_id(item_type)) {
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
            if (is_text_type_id(item_type)) {
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

typedef struct FindReplaceOptions {
    int64_t limit;
    int64_t last;
    bool has_limit;
    bool has_last;
    bool ignore_case;
} FindReplaceOptions;

static bool parse_find_replace_options(Item options_item, FindReplaceOptions* options) {
    options->limit = 0;
    options->last = 0;
    options->has_limit = false;
    options->has_last = false;
    options->ignore_case = false;
    TypeId options_type = get_type_id(options_item);
    if (options_type == LMD_TYPE_NULL) return true;
    if (options_type != LMD_TYPE_MAP && options_type != LMD_TYPE_OBJECT) {
        log_debug("parse_find_replace_options: options must be a map");
        return false;
    }

    Map* options_map = (options_type == LMD_TYPE_MAP) ? options_item.map : (Map*)options_item.object;
    bool is_found = false;
    Item limit_item = _map_get((TypeMap*)options_map->type, options_map->data, "limit", &is_found);
    if (is_found && get_type_id(limit_item) != LMD_TYPE_NULL) {
        int64_t limit = 0;
        if (!lambda_item_to_int64_exact(limit_item, &limit)) {
            log_debug("parse_find_replace_options: limit must be an integer");
            return false;
        }
        if (limit < 0) {
            log_debug("parse_find_replace_options: limit must be non-negative");
            return false;
        }
        options->limit = limit;
        options->has_limit = true;
    }

    is_found = false;
    Item last_item = _map_get((TypeMap*)options_map->type, options_map->data, "last", &is_found);
    if (is_found && get_type_id(last_item) != LMD_TYPE_NULL) {
        int64_t last = 0;
        if (!lambda_item_to_int64_exact(last_item, &last)) {
            log_debug("parse_find_replace_options: last must be an integer");
            return false;
        }
        if (last < 0) {
            log_debug("parse_find_replace_options: last must be non-negative");
            return false;
        }
        options->last = last;
        options->has_last = true;
    }
    if (options->has_limit && options->has_last) {
        log_debug("parse_find_replace_options: limit and last cannot be used together");
        return false;
    }

    is_found = false;
    Item ignore_item = _map_get((TypeMap*)options_map->type, options_map->data, "ignore_case", &is_found);
    if (is_found && get_type_id(ignore_item) != LMD_TYPE_NULL) {
        if (get_type_id(ignore_item) != LMD_TYPE_BOOL) {
            log_debug("parse_find_replace_options: ignore_case must be bool");
            return false;
        }
        options->ignore_case = it2b(ignore_item);
    }
    return true;
}

static unsigned char ascii_case_fold(unsigned char c) {
    return (c >= 'A' && c <= 'Z') ? (unsigned char)(c + ('a' - 'A')) : c;
}

static bool literal_match_at(const char* src, const char* needle, size_t len, bool ignore_case) {
    if (!ignore_case) return memcmp(src, needle, len) == 0;
    for (size_t i = 0; i < len; i++) {
        if (ascii_case_fold((unsigned char)src[i]) != ascii_case_fold((unsigned char)needle[i])) {
            return false;
        }
    }
    return true;
}

static int64_t options_legacy_pattern_limit(FindReplaceOptions options) {
    if (options.has_last) return options.last == 0 ? 0 : -options.last;
    if (options.has_limit) return options.limit;
    return 0;
}

static void select_match_window(int64_t total, FindReplaceOptions options, int64_t* first, int64_t* count) {
    *first = 0;
    if (total <= 0) {
        *count = 0;
        return;
    }
    if (options.has_last) {
        if (options.last <= 0) {
            *count = 0;
            return;
        }
        int64_t requested = options.last;
        if (requested < total) {
            *first = total - requested;
            *count = requested;
        } else {
            *count = total;
        }
    } else if (options.has_limit) {
        *count = options.limit < total ? options.limit : total;
    } else {
        *count = total;
    }
}

static int64_t count_literal_matches(const char* str_chars, size_t str_len,
                                     const char* needle, size_t needle_len,
                                     bool ignore_case) {
    if (!str_chars || !needle || str_len == 0 || needle_len == 0 || needle_len > str_len) return 0;
    int64_t count = 0;
    const char* p = str_chars;
    const char* end = str_chars + str_len;
    while (p <= end - needle_len) {
        if (literal_match_at(p, needle, needle_len, ignore_case)) {
            count++;
            p += needle_len;
        } else {
            p++;
        }
    }
    return count;
}

static bool item_string_is_ascii(Item item, TypeId item_type) {
    if (item_type == LMD_TYPE_NULL || item_type == LMD_TYPE_SYMBOL) return true;
    if (item_type != LMD_TYPE_STRING) return false;
    String* str = item.get_safe_string();
    return str && str->is_ascii != 0;
}

static bool literal_type_pattern_item(Item type_item, Item* literal_item) {
    if (!literal_item || get_type_id(type_item) != LMD_TYPE_TYPE) return false;
    Type* type = type_item.type;
    if (!type) return false;
    // TypePattern shares the TYPE tag but has no TypeType payload; only the
    // simple wrapper layout may be unwrapped before inspecting a literal.
    if (type->type_id == LMD_TYPE_TYPE && type->kind == TYPE_KIND_SIMPLE) {
        type = ((TypeType*)type)->type;
    }
    if (!type || !type->is_literal || type->type_id != LMD_TYPE_STRING) return false;
    String* literal = ((TypeString*)type)->string;
    if (!literal) return false;
    *literal_item = (Item){.item = s2it(literal)};
    return true;
}

static TypePattern* runtime_pattern_from_type(Type* type) {
    if (!type) return nullptr;
    if (type->kind == TYPE_KIND_PATTERN) return (TypePattern*)type;
    const char* error_msg = nullptr;
    return compile_literal_type_pattern(context ? context->pool : nullptr,
        type, false, &error_msg);
}

static Item fn_replace_impl(Item str_item, Item old_item, Item new_item, FindReplaceOptions options) {
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

    // Literal-only type aliases remain exact string needles for the search APIs.
    Item literal_pattern = {.item = 0};
    if (literal_type_pattern_item(old_item, &literal_pattern)) {
        old_item = literal_pattern;
        old_type = LMD_TYPE_STRING;
    }

    // pattern-based replacement: replace(str, pattern, repl_str[, options])
    if (old_type == LMD_TYPE_TYPE) {
        Type* type = (Type*)(old_item.item & 0x00FFFFFFFFFFFFFF);
        TypePattern* pattern = runtime_pattern_from_type(type);
        if (pattern) {
            if (pattern->is_symbol) {
                log_error("fn_replace: symbol-domain patterns cannot search strings");
                return ItemError;
            }
            if (!is_text_type_id(str_type)) {
                log_debug("fn_replace: first argument must be a string for pattern replace");
                return ItemError;
            }
            if (!is_text_type_id(new_type)) {
                log_debug("fn_replace: third argument must be a string for pattern replace");
                return ItemError;
            }
            const char* str_chars = str_item.get_chars();
            uint32_t str_len = str_item.get_len();
            const char* repl_chars = new_is_null ? "" : new_item.get_chars();
            uint32_t repl_len = new_is_null ? 0 : new_item.get_len();

            if (!str_chars || str_len == 0) return str_item;

            if ((options.has_limit && options.limit == 0) ||
                (options.has_last && options.last == 0)) return str_item;
            int64_t pattern_limit = options_legacy_pattern_limit(options);
            String* result = (!options.ignore_case && !options.has_limit && !options.has_last)
                ? pattern_replace_all(pattern, str_chars, str_len,
                                      repl_chars ? repl_chars : "", repl_chars ? repl_len : 0)
                : pattern_replace_all_options(pattern, str_chars, str_len,
                                              repl_chars ? repl_chars : "", repl_chars ? repl_len : 0,
                                              pattern_limit, options.ignore_case);
            if (!result) return str_item;
            return {.item = s2it(result)};
        }
    }

    if ((!is_text_type_id(str_type)) ||
        (!is_text_type_id(old_type)) ||
        (!is_text_type_id(new_type))) {
        log_debug("fn_replace: all arguments must be strings or symbols");
        return ItemError;
    }

    const char* str_chars = str_item.get_chars();
    uint32_t str_len = str_item.get_len();
    const char* old_chars = old_item.get_chars();
    uint32_t old_len = old_item.get_len();
    const char* new_chars = new_is_null ? "" : new_item.get_chars();
    uint32_t new_len_val = new_is_null ? 0 : new_item.get_len();

    if (!str_chars || str_len == 0) return str_item;
    if (!old_chars || old_len == 0) return str_item;  // nothing to replace

    int64_t total = count_literal_matches(str_chars, str_len, old_chars, old_len, options.ignore_case);
    int64_t first = 0, replace_count = 0;
    // Limit is applied before replacement so first/last-N semantics match find().
    select_match_window(total, options, &first, &replace_count);
    if (replace_count == 0) return str_item;

    size_t replacement_len = new_chars ? new_len_val : 0;
    size_t new_len = str_len + (size_t)replace_count * (replacement_len - old_len);

    char* dest = NULL;
    String* result = NULL;
    char* symbol_buf = NULL;
    if (str_type == LMD_TYPE_SYMBOL) {
        symbol_buf = (char*)pool_alloc(context->pool, new_len + 1);
        dest = symbol_buf;
    } else {
        result = (String *)heap_alloc(sizeof(String) + new_len + 1, LMD_TYPE_STRING);
        result->len = new_len;
        // Preserve is_ascii when both source and replacement are ASCII; otherwise byte-indexing callers may fall back to UTF-8 scans.
        bool src_ascii = item_string_is_ascii(str_item, str_type);
        bool repl_ascii = new_is_null || item_string_is_ascii(new_item, new_type);
        result->flags = 0;
        result->is_ascii = (src_ascii && repl_ascii) ? 1 : 0;
        dest = result->chars;
    }

    const char* p = str_chars;
    const char* end = str_chars + str_len;
    int64_t ordinal = 0;
    int64_t replaced = 0;
    while (p <= end - old_len) {
        if (literal_match_at(p, old_chars, old_len, options.ignore_case)) {
            bool selected = ordinal >= first && replaced < replace_count;
            if (selected) {
                if (replacement_len > 0) {
                    memcpy(dest, new_chars, replacement_len);
                    dest += replacement_len;
                }
                replaced++;
            } else {
                memcpy(dest, p, old_len);
                dest += old_len;
            }
            ordinal++;
            p += old_len;
        } else {
            *dest++ = *p++;
        }
    }
    while (p < end) *dest++ = *p++;
    *dest = '\0';

    if (str_type == LMD_TYPE_SYMBOL) return {.item = y2it(heap_create_symbol(symbol_buf, new_len))};
    return {.item = s2it(result)};
}

// replace(str, old, new) - replace all occurrences of old with new
Item fn_replace(Item str_item, Item old_item, Item new_item) {
    GUARD_ERROR3(str_item, old_item, new_item);
    FindReplaceOptions options = {0, 0, false, false, false};
    return fn_replace_impl(str_item, old_item, new_item, options);
}

Item fn_replace3(Item str_item, Item old_item, Item new_item) {
    return fn_replace(str_item, old_item, new_item);
}

Item fn_replace4(Item str_item, Item old_item, Item new_item, Item options_item) {
    GUARD_ERROR3(str_item, old_item, new_item);
    if (get_type_id(options_item) == LMD_TYPE_ERROR) return options_item;
    FindReplaceOptions options;
    if (!parse_find_replace_options(options_item, &options)) return ItemError;
    return fn_replace_impl(str_item, old_item, new_item, options);
}

static Item fn_find_impl(Item source_item, Item pattern_item, FindReplaceOptions options) {
    TypeId source_type = get_type_id(source_item);
    TypeId pattern_type = get_type_id(pattern_item);

    Item literal_pattern = {.item = 0};
    if (literal_type_pattern_item(pattern_item, &literal_pattern)) {
        pattern_item = literal_pattern;
        pattern_type = LMD_TYPE_STRING;
    }

    // null source -> empty list
    if (source_type == LMD_TYPE_NULL) { List* e = list(); e->is_content = 1; return {.array = e}; }

    if (!is_text_type_id(source_type)) {
        log_debug("fn_find: first argument must be a string or symbol");
        return ItemError;
    }

    const char* str_chars = source_item.get_chars();
    uint32_t str_len = source_item.get_len();

    if (!str_chars || str_len == 0) { List* e = list(); e->is_content = 1; return {.array = e}; }

    // pattern argument: check if it's a TypePattern
    if (pattern_type == LMD_TYPE_TYPE) {
        Type* type = (Type*)(pattern_item.item & 0x00FFFFFFFFFFFFFF);
        TypePattern* pattern = runtime_pattern_from_type(type);
        if (pattern) {
            if (pattern->is_symbol) {
                log_error("fn_find: symbol-domain patterns cannot search strings");
                return ItemError;
            }
            if ((options.has_limit && options.limit == 0) ||
                (options.has_last && options.last == 0)) {
                List* e = list();
                e->is_content = 1;
                return {.array = e};
            }
            int64_t pattern_limit = options_legacy_pattern_limit(options);
            List* r = pattern_find_all_options(pattern, str_chars, str_len, pattern_limit, options.ignore_case);
            if (r) r->is_content = 1;
            return {.array = r};
        }
    }

    // plain string search
    if (!is_text_type_id(pattern_type)) {
        log_debug("fn_find: second argument must be a string, symbol, or pattern");
        return ItemError;
    }

    const char* needle = pattern_item.get_chars();
    uint32_t needle_len = pattern_item.get_len();

    List* result = list();
    result->is_content = 1;
    RootFrame roots(2);
    Rooted<List*> rooted_result(roots, result);
    Rooted<Map*> rooted_match(roots, (Map*)NULL);
    if (!needle || needle_len == 0) return {.array = rooted_result.get()};

    int64_t total = count_literal_matches(str_chars, str_len, needle, needle_len, options.ignore_case);
    int64_t first = 0, selected_count = 0;
    // Limit selects the visible match window; replacement uses the same helper.
    select_match_window(total, options, &first, &selected_count);
    if (selected_count == 0) return {.array = rooted_result.get()};

    const char* p = str_chars;
    const char* end = str_chars + str_len;
    int64_t ordinal = 0;
    int64_t pushed = 0;
    while (p <= end - needle_len) {
        if (literal_match_at(p, needle, needle_len, options.ignore_case)) {
            if (ordinal >= first && pushed < selected_count) {
                int64_t index = (int64_t)(p - str_chars);
                Map* m = create_match_map_ext(p, needle_len, index);
                // The match is not reachable from the result until list_push
                // finishes, and that push may grow the list and collect.
                rooted_match.set(m);
                list_push(rooted_result.get(), {.map = rooted_match.get()});
                rooted_match.set((Map*)NULL);
                pushed++;
            }
            ordinal++;
            p += needle_len;
            if (pushed >= selected_count) break;
        } else {
            p++;
        }
    }

    return {.array = rooted_result.get()};
}

// find(str, pattern_or_string) -> [{value, index}, ...]
// Finds all non-overlapping matches in the source string.
// Second argument can be a string literal or a compiled pattern.
Item fn_find2(Item source_item, Item pattern_item) {
    GUARD_ERROR2(source_item, pattern_item);
    FindReplaceOptions options = {0, 0, false, false, false};
    return fn_find_impl(source_item, pattern_item, options);
}

// find(str, pattern, options) -> [{value, index}, ...]
Item fn_find3(Item source_item, Item pattern_item, Item options_item) {
    GUARD_ERROR3(source_item, pattern_item, options_item);
    FindReplaceOptions options;
    if (!parse_find_replace_options(options_item, &options)) return ItemError;
    return fn_find_impl(source_item, pattern_item, options);
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

    if (is_text_type_id(arg_type)) {
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
    else {
        // interpret as unix milliseconds
        int64_t ms = 0;
        if (lambda_item_to_int64_exact(arg, &ms)) {
            DateTime* parsed = datetime_from_unix_ms(context->pool, ms);
            if (parsed) {
                DateTime dt = *parsed;
                dt.precision = DATETIME_PRECISION_DATE_TIME;
                return dt;
            }
        }
        if (arg_type == LMD_TYPE_DTIME) return arg.get_datetime();
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
    else if (is_text_type_id(arg_type)) {
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

    int64_t year_value = 0, month_value = 0, day_value = 0;
    if (!lambda_item_to_int64_exact(y, &year_value) ||
            !lambda_item_to_int64_exact(m, &month_value) ||
            !lambda_item_to_int64_exact(d, &day_value) ||
            year_value < INT_MIN || year_value > INT_MAX) {
        log_error("date: year, month, and day must be integer-valued numbers");
        return DATETIME_MAKE_ERROR();
    }
    int year = (int)year_value;
    int month = (int)month_value;
    int day_val = (int)day_value;

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
    else if (is_text_type_id(arg_type)) {
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

    int64_t hour_value = 0, minute_value = 0, second_value = 0;
    if (!lambda_item_to_int64_exact(h, &hour_value) ||
            !lambda_item_to_int64_exact(m, &minute_value) ||
            !lambda_item_to_int64_exact(s, &second_value)) {
        log_error("time: hour, minute, and second must be integer-valued numbers");
        return DATETIME_MAKE_ERROR();
    }
    int hour_val = (int)hour_value;
    int minute_val = (int)minute_value;
    int second_val = (int)second_value;

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

// Set current variadic arguments (called before variadic function body)
// Returns the previous vargs pointer so it can be restored after the call
List* set_vargs(List* vargs) {
    List* prev = context ? context->current_vargs : NULL;
    if (context) context->current_vargs = vargs;
    return prev;
}

// Restore previous variadic arguments after a variadic function completes
void restore_vargs(List* prev) {
    if (context) context->current_vargs = prev;
}

// varg() - returns all variadic arguments as a list
Item fn_varg0() {
    List* current_vargs = context ? context->current_vargs : NULL;
    if (current_vargs == NULL) {
        log_debug("fn_varg0: no variadic args, returning empty list");
        // return an empty list - allocate directly without frame context
        List* empty = (List*)heap_calloc(sizeof(List), LMD_TYPE_ARRAY);
        empty->type_id = LMD_TYPE_ARRAY;
        empty->length = 0;
        empty->capacity = 0;
        empty->items = NULL;
        return {.array = empty};
    }
    return {.array = current_vargs};
}

// varg(n) - returns the nth variadic argument
Item fn_varg1(Item index_item) {
    int64_t index = 0;
    if (!lambda_item_to_int64_exact(index_item, &index)) {
        log_debug("fn_varg1: index must be an integer-valued number");
        return ItemNull;
    }
    List* current_vargs = context ? context->current_vargs : NULL;
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
static bool array_native_lane_supported(const LaneStorageDesc* desc) {
    return desc && (desc->kind == LANE_STORAGE_POINTER || (desc->nullable &&
        (desc->kind == LANE_STORAGE_INT || desc->kind == LANE_STORAGE_BOOL ||
         desc->kind == LANE_STORAGE_FLOAT64 || desc->kind == LANE_STORAGE_ITEM ||
         desc->kind == LANE_STORAGE_SIZED_I64)));
}

static bool runtime_array_representation_matches_contract(Item value,
        const LaneStorageDesc* desc) {
    if (!desc) return false;
    TypeId value_type = get_type_id(value);
    if (value_type == LMD_TYPE_ARRAY) {
        return array_native_lane_matches_desc(value.array, desc);
    }
    if (value_type != LMD_TYPE_ARRAY_NUM || desc->nullable || !value.array_num) {
        return false;
    }
    // ArrayNum is the legacy non-nullable carrier for the three scalar lanes;
    // its element tag is already a whole-container representation proof.
    switch (desc->kind) {
    case LANE_STORAGE_INT:
        return value.array_num->get_elem_type() == ELEM_INT;
    case LANE_STORAGE_BOOL:
        // bool[] uses the non-nullable packed byte lane just like int[] and
        // float[] use their legacy ArrayNum lanes. Omitting this proof sent a
        // valid bool store through the post-write validator, which treated the
        // still-correct ArrayNum carrier as an untyped array contract failure.
        return value.array_num->get_elem_type() == ELEM_BOOL;
    case LANE_STORAGE_FLOAT64:
        return value.array_num->get_elem_type() == ELEM_FLOAT64;
    default:
        return false;
    }
}

static bool array_rebuild_native_lane(Item source, const LaneStorageDesc* desc,
        Item* rebuilt) {
    if (!rebuilt || !array_native_lane_supported(desc)) return false;
    TypeId source_type = get_type_id(source);
    if (source_type != LMD_TYPE_ARRAY && source_type != LMD_TYPE_ARRAY_NUM) return false;
    Array* source_array = source.array;
    if (!source_array || source_array->has_js_props ||
            (source_type == LMD_TYPE_ARRAY_NUM &&
             (source_array->is_view || source_array->is_ndim))) {
        // A view aliases its ArrayNum backing. Replacing it would silently
        // sever write-through, so nullable mutation through views is deferred.
        return false;
    }

    RootFrame roots(2);
    Rooted<Item> rooted_source(roots, source);
    Rooted<Array*> rooted_result(roots, array_plain());
    Array* result = rooted_result.get();
    if (!result) return false;
    source_array = rooted_source.get().array;
    int64_t length = source_array->length;
    if (length < 0) return false;
    result->flags = source_array->flags;
    result->is_heap = 1;
    result->is_static = 0;
    result->is_immortal = 0;
    result->is_data_migrated = 0;
    result->array_flags = 0;
    array_native_lane_configure(result, desc);
    result->length = length;
    result->capacity = length;
    if (length > 0) {
        size_t bytes = 0;
        if (!lam::checked_mul((size_t)length, sizeof(Item), &bytes)) return false;
        result->items = (Item*)heap_data_calloc(bytes);
        result = rooted_result.get();
        if (!result->items) return false;
    }
    for (int64_t index = 0; index < length; index++) {
        Item value = item_at(rooted_source.get(), index);
        result = rooted_result.get();
        if (!array_native_lane_store(result, index, value)) return false;
    }
    *rebuilt = {.array = rooted_result.get()};
    return true;
}

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

    if (old_type == LMD_TYPE_ARRAY_NUM) {
        ArrayNum* num_arr = (ArrayNum*)arr;
        ArrayNumElemType etype = num_arr->get_elem_type();
        if (etype == ELEM_FLOAT64) {
            double* old_items = num_arr->float_items;
            int64_t extra_count = 0;
            for (int64_t i = 0; i < len; i++) {
                double* slot = (double*)(new_items + (new_capacity - extra_count - 1));
                *slot = old_items[i];
                new_items[i] = lambda_float_ptr_to_item(slot);
                extra_count++;
            }
            arr->extra = extra_count;
        } else if (etype == ELEM_INT64) {
            int64_t* old_items = num_arr->items;
            int64_t extra_count = 0;
            for (int64_t i = 0; i < len; i++) {
                int64_t* slot = (int64_t*)(new_items + (new_capacity - extra_count - 1));
                *slot = old_items[i];
                new_items[i] = {.item = l2it(slot)};
                extra_count++;
            }
            arr->extra = extra_count;
        } else if (etype == ELEM_UINT64) {
            uint64_t* old_items = (uint64_t*)num_arr->data;
            int64_t extra_count = 0;
            for (int64_t i = 0; i < len; i++) {
                uint64_t* slot = (uint64_t*)(new_items + (new_capacity - extra_count - 1));
                *slot = old_items[i];
                new_items[i] = {.item = u2it(slot)};
                extra_count++;
            }
            arr->extra = extra_count;
        } else if (etype == ELEM_BOOL) {
            // packed bools are one byte each; the i64-lane fallback below read
            // eight of them per element and produced integers like 65537.
            // Bools are inline-tagged, so no extra slot storage is needed.
            uint8_t* old_items = (uint8_t*)num_arr->data;
            for (int64_t i = 0; i < len; i++) {
                new_items[i] = {.item = b2it(old_items[i] ? BOOL_TRUE : BOOL_FALSE)};
            }
        } else {
            // v5: ELEM_INT is an i64 LANE array; box each lane through the encoder.
            int64_t* old_items = num_arr->items;
            for (int64_t i = 0; i < len; i++) {
                new_items[i] = {.item = lambda_int_box_lane(old_items[i])};
            }
        }
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
Item fn_array_set(Array* arr, int64_t index, Item value) {
    if (!arr || ((uintptr_t)arr >> 56)) {
        static int _err_count = 0;
        _err_count++;
        if (_err_count <= 10) {
            log_error("fn_array_set: null or invalid array pointer (ptr=%p, idx=%lld, val_type=%d, count=%d)",
                      (void*)arr, (long long)index, get_type_id(value), _err_count);
        }
        return ItemError;
    }
    TypeId arr_type = arr->type_id;
    if (arr->is_static) {
        log_error("fn_array_set: cannot mutate static array");
        return ItemError;
    }

    int64_t len = arr->length;
    if (index < 0 || index >= len) {
        // indexed writes must be fail-stop; silent log-and-continue hid mutation bugs from pn callers.
        set_runtime_error(ERR_INDEX_OUT_OF_BOUNDS,
            "fn_array_set: index %lld out of bounds (length %lld)",
            (long long)index, (long long)len);
        return ItemError;
    }

    switch (arr_type) {
    case LMD_TYPE_ARRAY: {
        // generic Array with Item* items — use internal array_set
        if (array_has_native_lane(arr)) {
            if (!array_native_lane_store(arr, index, value)) {
                set_runtime_error(ERR_TYPE_MISMATCH,
                    "fn_array_set: incompatible write to native lane array");
                return ItemError;
            }
        } else {
            array_set(arr, index, value);
        }
        break;
    }
    case LMD_TYPE_ARRAY_NUM: {
        ArrayNum* num_arr = (ArrayNum*)arr;
        if (num_arr->is_view) {
            if (!num_arr->is_mutable_view) {
                log_error("fn_array_set: cannot mutate a read-only view; copy() first");
                return ItemError;
            }
            // mutable view: write through via the element setter, which coerces to
            // the element type, handles every compact type, and never detaches the
            // view's aliased storage (data is pre-offset, so data[index] hits base).
            array_num_set_item(num_arr, index, value);
            break;
        }
        TypeId val_type = get_type_id(value);
        ArrayNumElemType etype = num_arr->get_elem_type();
        if (etype == ELEM_FLOAT64) {
            if (val_type == LMD_TYPE_FLOAT) {
                num_arr->float_items[index] = value.get_double();
            } else if (val_type == LMD_TYPE_INT) {
                num_arr->float_items[index] = lambda_int_item_value(value);
            } else if (val_type == LMD_TYPE_INT64) {
                num_arr->float_items[index] = (double)value.get_int64();
            } else if (val_type == LMD_TYPE_UINT64) {
                num_arr->float_items[index] = (double)value.get_uint64();
            } else {
                convert_specialized_to_generic(arr);
                array_set(arr, index, value);
            }
        } else if (etype == ELEM_INT64) {
            if (val_type == LMD_TYPE_INT64) {
                num_arr->items[index] = value.get_int64();
            } else if (val_type == LMD_TYPE_INT) {
                num_arr->items[index] = (int64_t)lambda_int_item_to_i64(value);
            } else {
                convert_specialized_to_generic(arr);
                array_set(arr, index, value);
            }
        } else if (etype == ELEM_UINT64) {
            if (val_type == LMD_TYPE_UINT64) {
                ((uint64_t*)num_arr->data)[index] = value.get_uint64();
            } else if (val_type == LMD_TYPE_INT) {
                ((uint64_t*)num_arr->data)[index] = (uint64_t)lambda_int_item_to_i64(value);
            } else {
                // Preserve the old elements as owner-backed u64 Items before
                // widening an inferred typed array to a heterogeneous array.
                convert_specialized_to_generic(arr);
                array_set(arr, index, value);
            }
        } else if (etype == ELEM_BOOL) {
            if (val_type == LMD_TYPE_BOOL) {
                // Every bool write keeps the packed carrier, so a valid
                // bool[] parameter never loses its representation proof by
                // being mutated.
                array_num_set_item(num_arr, index, value);
            } else {
                // A non-bool write must widen exactly like the numeric lanes.
                // Coercing it through array_num_set_item stored `false` for a
                // string — silent data loss on an untyped `fill(n, true)`
                // array, which the language allows to hold mixed values.
                // A *declared* bool[] never reaches here with a non-bool: the
                // checked setter admits the element first.
                convert_specialized_to_generic(arr);
                array_set(arr, index, value);
            }
        } else {
            // ELEM_INT -- v5: an i64 LANE array. The whole int domain fits,
            // poison included, because poison rides as a lane sentinel.
            if (val_type == LMD_TYPE_INT) {
                num_arr->items[index] = lambda_int_item_to_lane(value.item);
            } else if (val_type == LMD_TYPE_INT64) {
                int64_t lval = value.get_int64();
                if (lval >= INT53_MIN && lval <= INT53_MAX) {
                    num_arr->items[index] = lval;
                } else {
                    convert_specialized_to_generic(arr);
                    array_set(arr, index, value);
                }
            } else {
                convert_specialized_to_generic(arr);
                array_set(arr, index, value);
            }
        }
        break;
    }
    case LMD_TYPE_ELEMENT: {
        // Element has Item* items — use array_set directly
        array_set(arr, index, value);
        break;
    }
    default:
        log_error("fn_array_set: unsupported array type %d", arr_type);
        return ItemError;
    }
    return ItemNull;
}

// helper: decrement ref count of the value stored at a field pointer
static void map_field_decrement_ref(void* field_ptr, TypeId field_type) {
    switch (field_type) {
    case LMD_TYPE_STRING: case LMD_TYPE_SYMBOL: case LMD_TYPE_BINARY: {
        break;
    }
    case LMD_TYPE_ARRAY: case LMD_TYPE_ARRAY_NUM:
    case LMD_TYPE_RANGE:
    case LMD_TYPE_MAP: case LMD_TYPE_ELEMENT: case LMD_TYPE_OBJECT: {
        break;
    }
    case LMD_TYPE_NULL: {
        // a container might have been stored via a prior transition
        void* old_ptr = *(void**)field_ptr;
        if (old_ptr) {
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
    // Maps use int's canonical i64 lane too, so the nullable form can reserve
    // ItemNull without changing either the field width or its shape offset.
    case LMD_TYPE_INT:   *(int64_t*)field_ptr = lambda_int_item_to_lane(value.item); break;
    case LMD_TYPE_INT64: *(int64_t*)field_ptr = value.get_int64(); break;
    case LMD_TYPE_UINT64: *(uint64_t*)field_ptr = value.get_uint64(); break;
    case LMD_TYPE_NUM_SIZED:
        // Keep the packed Item: the TypeId alone does not carry i8 versus u32.
        *(Item*)field_ptr = value;
        break;
    case LMD_TYPE_FLOAT:
    case LMD_TYPE_FLOAT64: *(double*)field_ptr = value.get_double(); break;
    case LMD_TYPE_DTIME: *(DateTime**)field_ptr = value.get_datetime_ptr(); break;
    case LMD_TYPE_STRING: {
        *(String**)field_ptr = value.get_safe_string();
        break;
    }
    case LMD_TYPE_SYMBOL: {
        *(Symbol**)field_ptr = value.get_safe_symbol();
        break;
    }
    case LMD_TYPE_BINARY: {
        *(Binary**)field_ptr = value.get_safe_binary();
        break;
    }
    case LMD_TYPE_COMPLEX:
        *(Complex**)field_ptr = value.get_complex();
        break;
    case LMD_TYPE_ARRAY: case LMD_TYPE_ARRAY_NUM: case LMD_TYPE_RANGE:
    case LMD_TYPE_MAP: case LMD_TYPE_ELEMENT: case LMD_TYPE_OBJECT: {
        Container* c = value.container;
        *(Container**)field_ptr = c;
        break;
    }
    case LMD_TYPE_ANY: {
        TypeId actual_type = get_type_id(value);
        TypedItem titem = {.type_id = actual_type, .item = value.item};
        switch (actual_type) {
        case LMD_TYPE_NULL:
        case LMD_TYPE_ERROR:
        case LMD_TYPE_UNDEFINED:
            break;
        case LMD_TYPE_BOOL:
            titem.bool_val = value.bool_val;
            break;
        case LMD_TYPE_INT:
            // TypedItem's int arm follows the shaped-field carrier: the double
            // view, exact over the whole band and for poison.
            titem.double_val = lambda_int_item_value(value);
            break;
        case LMD_TYPE_INT64:
            titem.long_val = value.get_int64();
            break;
        case LMD_TYPE_UINT64:
            titem.uint64_val = value.get_uint64();
            break;
        case LMD_TYPE_FLOAT:
        case LMD_TYPE_FLOAT64:
            titem.double_val = value.get_double();
            break;
        case LMD_TYPE_DTIME:
            titem.datetime_ptr = value.get_datetime_ptr();
            break;
        case LMD_TYPE_DECIMAL:
            // Abstract numeric map fields use TypedItem storage; preserve the
            // decimal owner so integer-domain decimals round-trip as values.
            titem.decimal = value.get_decimal();
            break;
        case LMD_TYPE_STRING:
            titem.string = value.get_safe_string();
            break;
        case LMD_TYPE_BINARY:
            titem.binary = value.get_safe_binary();
            break;
        case LMD_TYPE_COMPLEX:
            titem.pointer = value.get_complex();
            break;
        case LMD_TYPE_SYMBOL:
            titem.symbol = value.get_safe_symbol();
            break;
        case LMD_TYPE_ARRAY: case LMD_TYPE_ARRAY_NUM: case LMD_TYPE_RANGE:
        case LMD_TYPE_MAP: case LMD_TYPE_VMAP:
        case LMD_TYPE_ELEMENT: case LMD_TYPE_OBJECT:
            titem.container = value.container;
            break;
        case LMD_TYPE_TYPE:
            titem.type = value.type;
            break;
        case LMD_TYPE_FUNC:
            titem.function = value.function;
            break;
        case LMD_TYPE_PATH:
            titem.path = value.path;
            break;
        default:
            titem = {.type_id = LMD_TYPE_ERROR};
            break;
        }
        *(TypedItem*)field_ptr = titem;
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

struct MutableCloneEntry {
    void* src;
    Item dst;
};

struct MutableCloneContext {
    HashMap* visited;
};

static TypeMap* mutable_shape_type(TypeId container_type, Type* type) {
    // Generic maps/objects/elements carry compact global descriptors. Treating
    // those two-byte prefixes as TypeMap headers reads unrelated memory during
    // COW; only an allocated descriptor with the matching container tag owns a
    // shape chain and byte-size contract.
    if (!type || type == &TYPE_MAP || type == &TYPE_OBJECT || type == &TYPE_ELMT ||
            !typemap_ptr_is_plausible(type) || type->type_id != container_type) {
        return NULL;
    }
    return (TypeMap*)type;
}

static uint64_t mutable_clone_entry_hash(const void* item, uint64_t seed0, uint64_t seed1) {
    uintptr_t ptr = (uintptr_t)((const MutableCloneEntry*)item)->src;
    return hashmap_sip(&ptr, sizeof(ptr), seed0, seed1);
}

static int mutable_clone_entry_cmp(const void* a, const void* b, void* udata) {
    (void)udata;
    const MutableCloneEntry* ea = (const MutableCloneEntry*)a;
    const MutableCloneEntry* eb = (const MutableCloneEntry*)b;
    if (ea->src == eb->src) return 0;
    return ea->src < eb->src ? -1 : 1;
}

static MutableCloneContext mutable_clone_context_new() {
    MutableCloneContext clone_ctx = {0};
    clone_ctx.visited = hashmap_new(sizeof(MutableCloneEntry), 16, 0, 0,
        mutable_clone_entry_hash, mutable_clone_entry_cmp, NULL, NULL);
    return clone_ctx;
}

static void mutable_clone_context_free(MutableCloneContext* clone_ctx) {
    if (clone_ctx && clone_ctx->visited) {
        hashmap_free(clone_ctx->visited);
        clone_ctx->visited = NULL;
    }
}

static bool mutable_clone_lookup(MutableCloneContext* clone_ctx, void* src, Item* dst) {
    if (!clone_ctx || !clone_ctx->visited || !src) return false;
    MutableCloneEntry probe = {.src = src};
    const MutableCloneEntry* found = (const MutableCloneEntry*)hashmap_get(clone_ctx->visited, &probe);
    if (!found) return false;
    if (dst) *dst = found->dst;
    return true;
}

static void mutable_clone_register(MutableCloneContext* clone_ctx, void* src, Item dst) {
    if (!clone_ctx || !clone_ctx->visited || !src) return;
    MutableCloneEntry entry = {.src = src, .dst = dst};
    hashmap_set(clone_ctx->visited, &entry);
}

static Item clone_mutable_item(Item value, MutableCloneContext* clone_ctx);

static bool mutable_clone_needs_container_path(Item value) {
    switch (get_type_id(value)) {
    case LMD_TYPE_ARRAY:
    case LMD_TYPE_ARRAY_NUM:
    case LMD_TYPE_MAP:
    case LMD_TYPE_OBJECT:
    case LMD_TYPE_ELEMENT:
        return true;
    default:
        return false;
    }
}

static void* mutable_clone_owner_data(Item owner) {
    switch (get_type_id(owner)) {
    case LMD_TYPE_MAP: return owner.map ? owner.map->data : NULL;
    case LMD_TYPE_OBJECT: return owner.object ? owner.object->data : NULL;
    case LMD_TYPE_ELEMENT: return owner.element ? owner.element->data : NULL;
    default: return NULL;
    }
}

static void clone_mutable_shape_data(TypeMap* map_type, Item dst_owner, Item src_owner,
                                     MutableCloneContext* clone_ctx) {
    if (!map_type || !mutable_clone_owner_data(dst_owner) ||
            !mutable_clone_owner_data(src_owner)) return;
    ShapeEntry* entry = map_type->shape;
    while (entry) {
        void* src_data = mutable_clone_owner_data(src_owner);
        if (!entry->name) {
            // Map spread slots are recursive raw Map* links, not normal typed
            // fields; storing a TypedItem here makes _map_get treat tag bytes
            // as a nested map pointer.
            Map* nested_src = map_shape_field_to_map(src_data, entry);
            Item field_clone = nested_src ? clone_mutable_item({.map = nested_src}, clone_ctx) : ItemNull;
            // Recursive cloning can compact the destination data zone; reload
            // its owner-relative field address only after the recursion.
            void* dst_field = (char*)mutable_clone_owner_data(dst_owner) + entry->byte_offset;
            *(Map**)dst_field = get_type_id(field_clone) == LMD_TYPE_MAP ?
                field_clone.map : NULL;
        } else {
            Item field_value = _map_read_field(entry, src_data);
            Item field_clone = clone_mutable_item(field_value, clone_ctx);
            // Cloned maps must keep the original physical slot layout. Extended
            // contracts such as `string | error` use the raw Item lane even
            // though their semantic TypeId is LMD_TYPE_TYPE.
            void* dst_field = (char*)mutable_clone_owner_data(dst_owner) + entry->byte_offset;
            if (map_shape_field_store_native_lane(dst_field, entry, field_clone)) {
                // Nullable native slots must be reconstructed through their
                // lane writer; map_field_store would treat ItemNull as a raw
                // pointer and corrupt a packed lane during COW.
            } else if (shape_entry_uses_raw_item_storage(entry)) {
                *(Item*)dst_field = field_clone;
            } else {
                map_field_store(dst_field, field_clone, shape_entry_storage_type_id(entry));
            }
        }
        entry = entry->next;
    }
}

static void clone_mutable_container_flags(Container* dst, Container* src) {
    if (!dst || !src) return;
    // COW clones must become runtime-owned mutable containers even when the
    // source was a static literal or input-pool document node.
    dst->flags = src->flags;
    dst->is_heap = 1;
    dst->is_static = 0;
    dst->is_data_migrated = 0;
}

static Item clone_mutable_array(Array* src, MutableCloneContext* clone_ctx) {
    if (!src) return ItemNull;
    Item existing;
    if (mutable_clone_lookup(clone_ctx, src, &existing)) return existing;
    RootFrame roots(2);
    Rooted<Array*> rooted_src(roots, src);
    Rooted<Array*> rooted_dst(roots, (Array*)NULL);
    Array* dst = array_plain();
    if (!dst) return ItemNull;
    rooted_dst.set(dst);
    src = rooted_src.get();
    clone_mutable_container_flags((Container*)dst, (Container*)src);
    dst->type_id = LMD_TYPE_ARRAY;
    // Mutable values may contain cycles through arrays/maps; register the
    // destination before cloning children so back-edges keep object identity.
    mutable_clone_register(clone_ctx, src, {.array = rooted_dst.get()});
    if (array_has_native_lane(src)) {
        dst->array_flags = src->array_flags;
        dst->map_kind = src->map_kind;
        dst->reserved_state = src->reserved_state;
        dst->length = src->length;
        dst->capacity = src->capacity;
        if (src->capacity > 0) {
            size_t bytes = 0;
            if (!lam::checked_mul((size_t)src->capacity, sizeof(Item), &bytes)) return ItemNull;
            dst->items = (Item*)heap_data_alloc(bytes);
            dst = rooted_dst.get();
            src = rooted_src.get();
            if (!dst->items) return ItemNull;
            memcpy(dst->items, src->items, bytes);
        }
        return {.array = rooted_dst.get()};
    }
    for (int64_t i = 0; i < src->length; i++) {
        src = rooted_src.get();
        Item child = clone_mutable_item(src->items[i], clone_ctx);
        array_push(rooted_dst.get(), child);
    }
    return {.array = rooted_dst.get()};
}

static Item clone_mutable_array_num(ArrayNum* src, MutableCloneContext* clone_ctx) {
    if (!src) return ItemNull;
    if (src->is_view) {
        // Numeric views are explicit aliases; cloning them during var binding
        // breaks the write-through contract for mutable subviews.
        return {.array_num = src};
    }
    Item existing;
    if (mutable_clone_lookup(clone_ctx, src, &existing)) return existing;
    RootFrame roots(2);
    Rooted<ArrayNum*> rooted_src(roots, src);
    Rooted<ArrayNum*> rooted_dst(roots, (ArrayNum*)NULL);
    ArrayNumElemType elem_type = src->get_elem_type();
    ArrayNum* dst = array_num_new(elem_type, src->length);
    if (!dst) return ItemNull;
    rooted_dst.set(dst);
    src = rooted_src.get();
    clone_mutable_container_flags((Container*)dst, (Container*)src);
    dst->type_id = LMD_TYPE_ARRAY_NUM;
    dst->set_elem_type(elem_type);
    dst->is_view = 0;
    dst->is_mutable_view = 0;
    mutable_clone_register(clone_ctx, src, {.array_num = rooted_dst.get()});

    int elem_size = ELEM_TYPE_SIZE[elem_type >> 4];
    if (src->data && dst->data && elem_size > 0 && src->length > 0) {
        memcpy(dst->data, src->data, (size_t)src->length * (size_t)elem_size);
    }
    if (src->is_ndim && !src->is_view && src->extra) {
        ArrayNumShape* shape = (ArrayNumShape*)src->extra;
        size_t shape_bytes = sizeof(ArrayNumShape) + 2 * (size_t)shape->ndim * sizeof(int64_t);
        ArrayNumShape* dst_shape = (ArrayNumShape*)heap_data_calloc(shape_bytes);
        if (dst_shape) {
            // Shape allocation can compact both the source descriptor and the
            // destination data buffer; reload both through their exact owners.
            src = rooted_src.get();
            dst = rooted_dst.get();
            shape = (ArrayNumShape*)src->extra;
            memcpy(dst_shape, shape, shape_bytes);
            dst_shape->base = NULL;
            dst->extra = (int64_t)dst_shape;
            dst->is_ndim = 1;
        }
    }
    return {.array_num = rooted_dst.get()};
}

static Item clone_mutable_map(Item src_item, MutableCloneContext* clone_ctx) {
    Map* src = src_item.map;
    if (!src || !src->type) return ItemNull;
    TypeMap* source_type = mutable_shape_type(LMD_TYPE_MAP, (Type*)src->type);
    Item existing;
    if (mutable_clone_lookup(clone_ctx, src, &existing)) return existing;
    RootFrame roots(2);
    Rooted<Map*> rooted_src(roots, src);
    Rooted<Map*> rooted_dst(roots, (Map*)NULL);
    Map* dst = (Map*)heap_calloc(sizeof(Map), LMD_TYPE_MAP);
    if (!dst) return ItemNull;
    rooted_dst.set(dst);
    src = rooted_src.get();
    clone_mutable_container_flags((Container*)dst, (Container*)src);
    dst->type_id = LMD_TYPE_MAP;
    dst->map_kind = src->map_kind;
    dst->type = src->type;
    int data_cap = src->data_cap > 0 ? src->data_cap :
        (source_type ? source_type->byte_size : 0);
    dst->data_cap = data_cap;
    // Register before descending into fields because map graphs can cycle
    // through `any` fields or spread-map slots.
    mutable_clone_register(clone_ctx, src, {.map = rooted_dst.get()});
    if (data_cap > 0) {
        rooted_dst.get()->data = heap_data_calloc((size_t)data_cap);
        if (source_type) {
            clone_mutable_shape_data(source_type, {.map = rooted_dst.get()},
                {.map = rooted_src.get()}, clone_ctx);
        } else if (rooted_src.get()->data) {
            // An opaque generic map has no shape metadata to guide child
            // ownership, but its raw data still needs to survive the clone.
            memcpy(rooted_dst.get()->data, rooted_src.get()->data, (size_t)data_cap);
        }
    }
    return {.map = rooted_dst.get()};
}

static Item clone_mutable_object(Item src_item, MutableCloneContext* clone_ctx) {
    Object* src = src_item.object;
    if (!src || !src->type) return ItemNull;
    TypeMap* source_type = mutable_shape_type(LMD_TYPE_OBJECT, (Type*)src->type);
    Item existing;
    if (mutable_clone_lookup(clone_ctx, src, &existing)) return existing;
    RootFrame roots(2);
    Rooted<Object*> rooted_src(roots, src);
    Rooted<Object*> rooted_dst(roots, (Object*)NULL);
    Object* dst = (Object*)heap_calloc(sizeof(Object), LMD_TYPE_OBJECT);
    if (!dst) return ItemNull;
    rooted_dst.set(dst);
    src = rooted_src.get();
    clone_mutable_container_flags((Container*)dst, (Container*)src);
    dst->type_id = LMD_TYPE_OBJECT;
    dst->map_kind = src->map_kind;
    dst->type = src->type;
    int data_cap = src->data_cap > 0 ? src->data_cap :
        (source_type ? source_type->byte_size : 0);
    dst->data_cap = data_cap;
    mutable_clone_register(clone_ctx, src, {.object = rooted_dst.get()});
    if (data_cap > 0) {
        rooted_dst.get()->data = heap_data_calloc((size_t)data_cap);
        if (source_type) {
            clone_mutable_shape_data(source_type, {.object = rooted_dst.get()},
                {.object = rooted_src.get()}, clone_ctx);
        } else if (rooted_src.get()->data) {
            memcpy(rooted_dst.get()->data, rooted_src.get()->data, (size_t)data_cap);
        }
    }
    return {.object = rooted_dst.get()};
}

static Item clone_mutable_element(Item src_item, MutableCloneContext* clone_ctx) {
    Element* src = src_item.element;
    if (!src || !src->type) return ItemNull;
    TypeMap* source_type = mutable_shape_type(LMD_TYPE_ELEMENT, (Type*)src->type);
    Item existing;
    if (mutable_clone_lookup(clone_ctx, src, &existing)) return existing;
    RootFrame roots(2);
    Rooted<Element*> rooted_src(roots, src);
    Rooted<Element*> rooted_dst(roots, (Element*)NULL);
    Element* dst = (Element*)heap_calloc(sizeof(Element), LMD_TYPE_ELEMENT);
    if (!dst) return ItemNull;
    rooted_dst.set(dst);
    src = rooted_src.get();
    clone_mutable_container_flags((Container*)dst, (Container*)src);
    dst->type_id = LMD_TYPE_ELEMENT;
    dst->map_kind = src->map_kind;
    dst->type = src->type;
    mutable_clone_register(clone_ctx, src, {.element = rooted_dst.get()});

    for (int64_t i = 0; i < src->length; i++) {
        src = rooted_src.get();
        Item child = clone_mutable_item(src->items[i], clone_ctx);
        array_push((Array*)rooted_dst.get(), child);
    }

    int data_cap = src->data_cap > 0 ? src->data_cap :
        (source_type ? source_type->byte_size : 0);
    dst->data_cap = data_cap;
    if (data_cap > 0) {
        rooted_dst.get()->data = heap_data_calloc((size_t)data_cap);
        if (source_type) {
            clone_mutable_shape_data(source_type, {.element = rooted_dst.get()},
                {.element = rooted_src.get()}, clone_ctx);
        } else if (rooted_src.get()->data) {
            memcpy(rooted_dst.get()->data, rooted_src.get()->data, (size_t)data_cap);
        }
    }
    return {.element = rooted_dst.get()};
}

static Item clone_mutable_item(Item value, MutableCloneContext* clone_ctx) {
    switch (get_type_id(value)) {
    case LMD_TYPE_ARRAY: return clone_mutable_array(value.array, clone_ctx);
    case LMD_TYPE_ARRAY_NUM: return clone_mutable_array_num(value.array_num, clone_ctx);
    case LMD_TYPE_MAP: return clone_mutable_map(value, clone_ctx);
    case LMD_TYPE_OBJECT: return clone_mutable_object(value, clone_ctx);
    case LMD_TYPE_ELEMENT: return clone_mutable_element(value, clone_ctx);
    default: return value;
    }
}

Item fn_mutable_value(Item value) {
    cow_profile_note_mutable_value();
    // Scalar var bindings never need alias isolation; avoid allocating the
    // cycle-tracking table on this hot path.
    if (!mutable_clone_needs_container_path(value)) return value;
    MutableCloneContext clone_ctx = mutable_clone_context_new();
    Item cloned = clone_mutable_item(value, &clone_ctx);
    mutable_clone_context_free(&clone_ctx);
    return cloned;
}

bool cow_item_is_container(Item value) {
    switch (get_type_id(value)) {
    case LMD_TYPE_ARRAY:
    case LMD_TYPE_ARRAY_NUM:
    case LMD_TYPE_MAP:
    case LMD_TYPE_OBJECT:
    case LMD_TYPE_ELEMENT:
    case LMD_TYPE_VMAP:
        return true;
    default:
        return false;
    }
}

static Container* cow_item_container(Item value) {
    if (!cow_item_is_container(value)) return NULL;
    return value.container;
}

typedef struct CowProfileTypeCounters {
    uint64_t share_marks;
    uint64_t unique_mutations;
    uint64_t shared_copies;
    uint64_t copied_bytes;
} CowProfileTypeCounters;

typedef struct CowProfileCounters {
    CowProfileTypeCounters by_type[LMD_TYPE_COUNT];
    uint64_t vmap_snapshots;
    uint64_t vmap_rejections;
    uint64_t mutable_value_calls;
    uint64_t map_admit_calls;
    uint64_t map_admit_relation_cache_hits;
    uint64_t map_admit_relation_cache_misses;
    uint64_t map_admit_exact_shape_hits;
    uint64_t map_admit_storage_compatible_hits;
    uint64_t map_admit_readonly_validations;
    uint64_t map_admit_reifications;
    uint64_t map_admit_deep_clone_calls;
    uint64_t map_admit_fields_visited;
    uint64_t map_admit_bytes_copied;
    uint64_t array_checked_store_calls;
    uint64_t array_checked_store_direct;
    uint64_t array_checked_store_unique_inplace;
    uint64_t array_checked_store_cow_detach;
    uint64_t array_checked_store_rebuild;
    uint64_t array_checked_store_full_clone;
    uint64_t array_checked_store_bytes_copied;
} CowProfileCounters;

static CowProfileCounters g_cow_profile = {};
static int g_cow_profile_enabled = -1;
static bool g_cow_profile_registered = false;

static bool cow_profile_truthy(const char* value) {
    return value && value[0] && strcmp(value, "0") != 0 &&
        strcmp(value, "false") != 0 && strcmp(value, "off") != 0 &&
        strcmp(value, "no") != 0;
}

static bool cow_profile_enabled(void) {
    if (g_cow_profile_enabled >= 0) return g_cow_profile_enabled != 0;
    g_cow_profile_enabled = cow_profile_truthy(getenv("COW_EXEC_PROFILE")) ? 1 : 0;
    if (g_cow_profile_enabled && !g_cow_profile_registered) {
        // Register once so a release benchmark can emit its counters without
        // putting any output or allocation into the raw mutation path.
        atexit(cow_profile_dump);
        g_cow_profile_registered = true;
    }
    return g_cow_profile_enabled != 0;
}

struct CowProfileLifetime {
    CowProfileLifetime() {
        // Initialize once at process startup so a no-COW workload can still
        // prove its zero-copy hot path in the requested release profile.
        (void)cow_profile_enabled();
    }
};

static CowProfileLifetime g_cow_profile_lifetime;

static CowProfileTypeCounters* cow_profile_for(Item value) {
    TypeId type_id = get_type_id(value);
    if (type_id >= LMD_TYPE_COUNT) return NULL;
    return &g_cow_profile.by_type[type_id];
}

static uint64_t cow_one_level_copy_bytes(Item value) {
    switch (get_type_id(value)) {
    case LMD_TYPE_ARRAY:
        return sizeof(Array) + (uint64_t)value.array->length * sizeof(Item);
    case LMD_TYPE_MAP:
        return sizeof(Map) + (uint64_t)value.map->data_cap;
    case LMD_TYPE_ARRAY_NUM: {
        ArrayNum* array = value.array_num;
        if (!array) return 0;
        int elem_size = ELEM_TYPE_SIZE[array->get_elem_type() >> 4];
        return sizeof(ArrayNum) + (uint64_t)(elem_size > 0 ? elem_size : 0) *
            (uint64_t)array->length;
    }
    case LMD_TYPE_OBJECT:
        return sizeof(Object) + (uint64_t)value.object->data_cap;
    case LMD_TYPE_ELEMENT:
        return sizeof(Element) + (uint64_t)value.element->data_cap +
            (uint64_t)value.element->length * sizeof(Item);
    case LMD_TYPE_VMAP:
        // Backend storage is opaque here; snapshot count records the exact
        // backend event while this is the stable VMap header cost.
        return sizeof(VMap);
    default:
        return 0;
    }
}

void cow_profile_note_mutable_value(void) {
    if (cow_profile_enabled()) g_cow_profile.mutable_value_calls++;
}

void cow_profile_note_map_admit_clone(void) {
    if (cow_profile_enabled()) g_cow_profile.map_admit_deep_clone_calls++;
}

void cow_profile_note_map_admit_bytes(uint64_t bytes) {
    if (cow_profile_enabled()) g_cow_profile.map_admit_bytes_copied += bytes;
}

void cow_profile_note_vmap_snapshot(void) {
    if (cow_profile_enabled()) g_cow_profile.vmap_snapshots++;
}

void cow_profile_note_vmap_rejection(void) {
    if (cow_profile_enabled()) g_cow_profile.vmap_rejections++;
}

void cow_profile_dump(void) {
    if (!cow_profile_enabled()) return;
    create_dir("temp");
    const char* output_path = getenv("COW_EXEC_PROFILE_OUT");
    if (!output_path || !output_path[0]) output_path = "temp/cow_exec_profile.tsv";

    StrBuf* output = strbuf_new();
    if (!output) return;
    strbuf_append_str(output, "type\tshare_marks\tunique_mutations\tshared_copies\tcopied_bytes\n");
    for (int i = 0; i < LMD_TYPE_COUNT; i++) {
        CowProfileTypeCounters* counters = &g_cow_profile.by_type[i];
        if (counters->share_marks == 0 && counters->unique_mutations == 0 &&
                counters->shared_copies == 0 && counters->copied_bytes == 0) continue;
        strbuf_append_str(output, get_type_name((TypeId)i));
        strbuf_append_char(output, '\t');
        strbuf_append_uint64(output, counters->share_marks);
        strbuf_append_char(output, '\t');
        strbuf_append_uint64(output, counters->unique_mutations);
        strbuf_append_char(output, '\t');
        strbuf_append_uint64(output, counters->shared_copies);
        strbuf_append_char(output, '\t');
        strbuf_append_uint64(output, counters->copied_bytes);
        strbuf_append_char(output, '\n');
    }
    strbuf_append_str(output, "vmap_snapshots\t");
    strbuf_append_uint64(output, g_cow_profile.vmap_snapshots);
    strbuf_append_str(output, "\nvmap_rejections\t");
    strbuf_append_uint64(output, g_cow_profile.vmap_rejections);
    strbuf_append_str(output, "\nfn_mutable_value_calls\t");
    strbuf_append_uint64(output, g_cow_profile.mutable_value_calls);
    strbuf_append_str(output, "\nmap_admit_calls\t");
    strbuf_append_uint64(output, g_cow_profile.map_admit_calls);
    strbuf_append_str(output, "\nmap_admit_relation_cache_hits\t");
    strbuf_append_uint64(output, g_cow_profile.map_admit_relation_cache_hits);
    strbuf_append_str(output, "\nmap_admit_relation_cache_misses\t");
    strbuf_append_uint64(output, g_cow_profile.map_admit_relation_cache_misses);
    strbuf_append_str(output, "\nmap_admit_exact_shape_hits\t");
    strbuf_append_uint64(output, g_cow_profile.map_admit_exact_shape_hits);
    strbuf_append_str(output, "\nmap_admit_storage_compatible_hits\t");
    strbuf_append_uint64(output, g_cow_profile.map_admit_storage_compatible_hits);
    strbuf_append_str(output, "\nmap_admit_readonly_validations\t");
    strbuf_append_uint64(output, g_cow_profile.map_admit_readonly_validations);
    strbuf_append_str(output, "\nmap_admit_reifications\t");
    strbuf_append_uint64(output, g_cow_profile.map_admit_reifications);
    strbuf_append_str(output, "\nmap_admit_deep_clone_calls\t");
    strbuf_append_uint64(output, g_cow_profile.map_admit_deep_clone_calls);
    strbuf_append_str(output, "\nmap_admit_fields_visited\t");
    strbuf_append_uint64(output, g_cow_profile.map_admit_fields_visited);
    strbuf_append_str(output, "\nmap_admit_bytes_copied\t");
    strbuf_append_uint64(output, g_cow_profile.map_admit_bytes_copied);
    strbuf_append_str(output, "\narray_checked_store_calls\t");
    strbuf_append_uint64(output, g_cow_profile.array_checked_store_calls);
    strbuf_append_str(output, "\narray_checked_store_direct\t");
    strbuf_append_uint64(output, g_cow_profile.array_checked_store_direct);
    strbuf_append_str(output, "\narray_checked_store_unique_inplace\t");
    strbuf_append_uint64(output, g_cow_profile.array_checked_store_unique_inplace);
    strbuf_append_str(output, "\narray_checked_store_cow_detach\t");
    strbuf_append_uint64(output, g_cow_profile.array_checked_store_cow_detach);
    strbuf_append_str(output, "\narray_checked_store_rebuild\t");
    strbuf_append_uint64(output, g_cow_profile.array_checked_store_rebuild);
    strbuf_append_str(output, "\narray_checked_store_full_clone\t");
    strbuf_append_uint64(output, g_cow_profile.array_checked_store_full_clone);
    strbuf_append_str(output, "\narray_checked_store_bytes_copied\t");
    strbuf_append_uint64(output, g_cow_profile.array_checked_store_bytes_copied);
    strbuf_append_char(output, '\n');
    if (write_text_file_atomic(output_path, output->str ? output->str : "") != 0) {
        log_error("cow profile: failed to write '%s'", output_path);
    }
    strbuf_free(output);
}

Item cow_mark_shared(Item value) {
    Container* container = cow_item_container(value);
    if (container && (container->cow_state & COW_STATE_SHARED) == 0) {
        if (cow_profile_enabled()) {
            CowProfileTypeCounters* counters = cow_profile_for(value);
            if (counters) counters->share_marks++;
        }
        container->cow_state |= COW_STATE_SHARED;
    }
    return value;
}

Item cow_bind_var(Item value) {
    // ArrayNum views keep their Stage-2 mutable-view contract; generic COW
    // ownership starts only at the core Item-container subset.
    if (get_type_id(value) == LMD_TYPE_ARRAY_NUM) return fn_mutable_value(value);
    return cow_mark_shared(value);
}

static void cow_mark_shape_children(TypeMap* type, void* data) {
    if (!type || !data) return;
    for (ShapeEntry* entry = type->shape; entry; entry = entry->next) {
        if (!entry->name) {
            Map* spread = map_shape_field_to_map(data, entry);
            if (spread) cow_mark_shared({.map = spread});
            continue;
        }
        cow_mark_shared(_map_read_field(entry, data));
    }
}

static Item cow_clone_array_one_level(Array* source) {
    if (!source) return ItemError;
    RootFrame roots(2);
    Rooted<Array*> rooted_source(roots, source);
    Rooted<Array*> rooted_copy(roots, array_plain());
    if (!rooted_copy.get()) return ItemError;
    source = rooted_source.get();
    clone_mutable_container_flags((Container*)rooted_copy.get(), (Container*)source);
    if (array_has_native_lane(source)) {
        // Nullable typed-array reification stores raw lane words in a generic
        // Array. Treating those words as Items during COW cloning makes values
        // such as integer lane `1` look like a pointer and dereferences it in
        // get_type_id; copy the physical lane and mark only decoded references.
        Array* copy = rooted_copy.get();
        copy->array_flags = source->array_flags;
        copy->map_kind = source->map_kind;
        copy->reserved_state = source->reserved_state;
        copy->length = source->length;
        copy->capacity = source->capacity;
        if (source->capacity > 0) {
            size_t bytes = 0;
            if (!lam::checked_mul((size_t)source->capacity, sizeof(Item), &bytes)) {
                return ItemError;
            }
            copy->items = (Item*)heap_data_alloc(bytes);
            if (!copy->items) return ItemError;
            memcpy(copy->items, source->items, bytes);
        }
        for (int64_t index = 0; index < source->length; index++) {
            cow_mark_shared(array_native_lane_read(source, index));
        }
        copy->cow_state &= ~COW_STATE_SHARED;
        return {.array = copy};
    }
    for (int64_t index = 0; index < source->length; index++) {
        source = rooted_source.get();
        Item child = source->items[index];
        cow_mark_shared(child);
        array_push(rooted_copy.get(), child);
    }
    rooted_copy.get()->cow_state &= ~COW_STATE_SHARED;
    return {.array = rooted_copy.get()};
}

static Item cow_clone_map_like_one_level(Item source_item) {
    TypeId type_id = get_type_id(source_item);
    Container* source = source_item.container;
    if (!source) return ItemError;
    RootFrame roots(2);
    Rooted<Item> rooted_source(roots, source_item);
    Rooted<Item> rooted_copy(roots, ItemNull);
    size_t size = type_id == LMD_TYPE_MAP ? sizeof(Map) :
        type_id == LMD_TYPE_OBJECT ? sizeof(Object) : sizeof(Element);
    Container* copy = (Container*)heap_calloc(size, type_id);
    if (!copy) return ItemError;
    rooted_copy.set({.container = copy});
    source = rooted_source.get().container;
    clone_mutable_container_flags(copy, source);
    copy->type_id = type_id;
    copy->map_kind = source->map_kind;

    TypeMap* type = NULL;
    void* source_data = NULL;
    void** copy_data = NULL;
    int* copy_cap = NULL;
    int source_cap = 0;
    if (type_id == LMD_TYPE_MAP) {
        Map* src = rooted_source.get().map;
        Map* dst = rooted_copy.get().map;
        dst->type = src->type;
        type = mutable_shape_type(type_id, (Type*)src->type);
        source_data = src->data;
        copy_data = &dst->data;
        copy_cap = &dst->data_cap;
        source_cap = src->data_cap;
    } else if (type_id == LMD_TYPE_OBJECT) {
        Object* src = rooted_source.get().object;
        Object* dst = rooted_copy.get().object;
        dst->type = src->type;
        type = mutable_shape_type(type_id, (Type*)src->type);
        source_data = src->data;
        copy_data = &dst->data;
        copy_cap = &dst->data_cap;
        source_cap = src->data_cap;
    } else {
        Element* src = rooted_source.get().element;
        Element* dst = rooted_copy.get().element;
        dst->type = src->type;
        type = mutable_shape_type(type_id, (Type*)src->type);
        source_data = src->data;
        copy_data = &dst->data;
        copy_cap = &dst->data_cap;
        source_cap = src->data_cap;
        for (int64_t index = 0; index < src->length; index++) {
            src = rooted_source.get().element;
            dst = rooted_copy.get().element;
            Item child = src->items[index];
            cow_mark_shared(child);
            array_push((Array*)dst, child);
        }
    }
    int data_cap = source_cap > 0 ? source_cap : (type ? type->byte_size : 0);
    if (source_data && data_cap > 0) {
        *copy_data = heap_data_calloc((size_t)data_cap);
        if (!*copy_data) return ItemError;
        if (type_id == LMD_TYPE_MAP) {
            source_data = rooted_source.get().map->data;
            copy_data = &rooted_copy.get().map->data;
            copy_cap = &rooted_copy.get().map->data_cap;
        } else if (type_id == LMD_TYPE_OBJECT) {
            source_data = rooted_source.get().object->data;
            copy_data = &rooted_copy.get().object->data;
            copy_cap = &rooted_copy.get().object->data_cap;
        } else {
            source_data = rooted_source.get().element->data;
            copy_data = &rooted_copy.get().element->data;
            copy_cap = &rooted_copy.get().element->data_cap;
        }
        // Shape data stores scalar payloads by value and container references by
        // pointer, so byte-copying preserves layout while child marks establish COW.
        memcpy(*copy_data, source_data, (size_t)data_cap);
        *copy_cap = data_cap;
        cow_mark_shape_children(type, source_data);
    }
    rooted_copy.get().container->cow_state &= ~COW_STATE_SHARED;
    return rooted_copy.get();
}

static Item cow_clone_one_level(Item source) {
    switch (get_type_id(source)) {
    case LMD_TYPE_ARRAY: return cow_clone_array_one_level(source.array);
    case LMD_TYPE_ARRAY_NUM: return clone_mutable_array_num(source.array_num, NULL);
    case LMD_TYPE_MAP:
    case LMD_TYPE_OBJECT:
    case LMD_TYPE_ELEMENT: return cow_clone_map_like_one_level(source);
    default: return source;
    }
}

Item cow_prepare_write(Item old) {
    Container* container = cow_item_container(old);
    if (!container) return old;
    if (!container->is_static && !container->is_immortal &&
            (container->cow_state & COW_STATE_SHARED) == 0) {
        if (cow_profile_enabled()) {
            CowProfileTypeCounters* counters = cow_profile_for(old);
            if (counters) counters->unique_mutations++;
        }
        return old;
    }

    Item replacement = get_type_id(old) == LMD_TYPE_VMAP
        ? vmap_clone_for_cow(old) : cow_clone_one_level(old);
    if (get_type_id(replacement) == LMD_TYPE_ERROR) return replacement;
    if (cow_profile_enabled()) {
        CowProfileTypeCounters* counters = cow_profile_for(old);
        if (counters) {
            counters->shared_copies++;
            counters->copied_bytes += cow_one_level_copy_bytes(old);
        }
    }

    Container* replacement_container = cow_item_container(replacement);
    if (replacement_container) replacement_container->cow_state &= ~COW_STATE_SHARED;
    return replacement;
}

static ShapeEntry* map_find_shape_entry(TypeMap* tm, const char* key_cstr,
    size_t key_len);
Item cow_path_set(Item owner, Item path, Item value);

static ShapeEntry* runtime_named_map_field(Type* expected, Item key) {
    expected = runtime_boundary_unwrap_type(expected);
    if (!expected || expected->type_id != LMD_TYPE_MAP) return NULL;
    const char* chars = NULL;
    uint32_t length = 0;
    TypeId key_type = get_type_id(key);
    if (key_type == LMD_TYPE_STRING || key_type == LMD_TYPE_SYMBOL) {
        chars = key.get_chars();
        length = key.get_len();
    }
    if (!chars) return NULL;
    return map_find_shape_entry((TypeMap*)expected, chars, length);
}

static bool map_extend_open_shape(Item map_item, Item key, Item value) {
    if (get_type_id(map_item) != LMD_TYPE_MAP) {
        return false;
    }
    Map* map = map_item.map;
    if (!map || !map->type || !map->data || !context || !context->pool) return false;
    const char* key_chars = NULL;
    uint32_t key_length = 0;
    TypeId key_type = get_type_id(key);
    if (key_type == LMD_TYPE_STRING) {
        String* key_string = key.get_safe_string();
        key_chars = key_string ? key_string->chars : NULL;
        key_length = key_string ? key_string->len : 0;
    } else if (key_type == LMD_TYPE_SYMBOL) {
        Symbol* key_symbol = key.get_safe_symbol();
        key_chars = key_symbol ? key_symbol->chars : NULL;
        key_length = key_symbol ? key_symbol->len : 0;
    }
    if (!key_chars) return false;

    TypeMap* old_type = (TypeMap*)map->type;
    int old_count = 0;
    int64_t new_size = 0;
    for (ShapeEntry* entry = old_type->shape; entry; entry = entry->next) {
        old_count++;
        new_size += type_info[shape_entry_storage_type_id(entry)].byte_size;
    }
    TypeId value_type = get_type_id(value);
    new_size += type_info[value_type].byte_size;

    RootFrame roots(2);
    Rooted<Map*> rooted_map(roots, map);
    Rooted<Item> rooted_value(roots, value);
    TypeMap* new_type = (TypeMap*)alloc_type(context->pool, LMD_TYPE_MAP, sizeof(TypeMap));
    if (!new_type) return false;
    void* new_data = heap_data_calloc(new_size > 0 ? (size_t)new_size : 1);
    if (!new_data) return false;
    map = rooted_map.get();
    old_type = (TypeMap*)map->type;

    ShapeEntry* first = NULL;
    ShapeEntry* last = NULL;
    int64_t offset = 0;
    for (ShapeEntry* old = old_type->shape; old; old = old->next) {
        ShapeEntry* entry = (ShapeEntry*)pool_calloc(context->pool, sizeof(ShapeEntry));
        if (!entry) return false;
        *entry = *old;
        entry->byte_offset = offset;
        entry->next = NULL;
        int size = type_info[shape_entry_storage_type_id(old)].byte_size;
        memcpy((char*)new_data + offset, (char*)map->data + old->byte_offset, (size_t)size);
        offset += size;
        if (last) last->next = entry;
        else first = entry;
        last = entry;
    }

    ShapeEntry* added = (ShapeEntry*)pool_calloc(context->pool,
        sizeof(ShapeEntry) + sizeof(StrView));
    if (!added) return false;
    StrView* name = (StrView*)((char*)added + sizeof(ShapeEntry));
    char* name_copy = (char*)pool_calloc(context->pool, (size_t)key_length + 1);
    if (!name_copy) return false;
    memcpy(name_copy, key_chars, key_length);
    name->str = name_copy;
    name->length = key_length;
    added->name = name;
    added->name_hash = typemap_name_hash(name->str, (int)name->length);
    added->name_id = NAME_ID_NONE;
    added->key_kind = NAME_KEY_STRING;
    added->type = type_info[value_type].type;
    added->byte_offset = offset;
    map_field_store((char*)new_data + offset, rooted_value.get(), value_type);
    if (last) last->next = added;
    else first = added;

    new_type->shape = first;
    new_type->last = added;
    new_type->length = old_count + 1;
    new_type->byte_size = new_size;
    new_type->type_index = ((ArrayList*)context->type_list)->length;
    new_type->has_named_shape = old_type->has_named_shape;
    new_type->is_trusted_contract = false;
    new_type->struct_name = old_type->struct_name;
    new_type->is_private_clone = true;
    typemap_hash_build(new_type, context->pool);
    arraylist_append((ArrayList*)context->type_list, new_type);

    // The candidate is private. Publishing the new shape/data pair together
    // makes the additional open field immediately match its physical slot.
    map = rooted_map.get();
    map->type = new_type;
    map->data = new_data;
    map->data_cap = (int)new_size;
    return true;
}

static Item lambda_map_set_checked_impl(Item owner, Item key, Item value, Type* expected,
        const char* boundary, bool publish_in_place) {
    // Typed map writes are transactional at the root boundary. Clone first,
    // then validate/store on the private candidate; a failed field check or
    // post-state check therefore cannot corrupt the original map or a COW
    // snapshot that still observes it.
    Type* contract = runtime_boundary_unwrap_type(expected);
    ShapeEntry* field = runtime_named_map_field(contract, key);
    Item checked_value = field ? lambda_type_check(value, field->type, boundary) : value;
    if (get_type_id(checked_value) == LMD_TYPE_ERROR) return checked_value;

    // A var parameter arrives after the caller has detached its writable root.
    // Check the pre-state before a direct store: known members are already
    // checked above and open members preserve map conformance, so this keeps
    // the in/out write transactional without hiding the replacement in the
    // callee's local binding.
    if (publish_in_place && !lambda_type_matches(owner, contract)) {
        return lambda_type_error(owner, contract, boundary);
    }

    RootFrame roots(3);
    Rooted<Item> rooted_owner(roots, owner);
    Rooted<Item> rooted_key(roots, key);
    Rooted<Item> rooted_value(roots, checked_value);
    Rooted<Item> rooted_candidate(roots, publish_in_place ? rooted_owner.get() :
        cow_clone_one_level(rooted_owner.get()));
    if (get_type_id(rooted_candidate.get()) == LMD_TYPE_ERROR) return rooted_candidate.get();
    if (!publish_in_place && rooted_candidate.get().item == rooted_owner.get().item) {
        // Only VMap currently falls through the generic clone helper. Its
        // separate COW implementation is already private-by-construction.
        rooted_candidate.set(vmap_clone_for_cow(rooted_owner.get()));
        if (get_type_id(rooted_candidate.get()) == LMD_TYPE_ERROR) return rooted_candidate.get();
    }
    if (field) {
        Item set_result = fn_map_set(rooted_candidate.get(), rooted_key.get(), rooted_value.get());
        if (get_type_id(set_result) == LMD_TYPE_ERROR) return set_result;
    } else if (!map_extend_open_shape(rooted_candidate.get(), rooted_key.get(),
                   rooted_value.get())) {
        return lambda_type_error(rooted_value.get(), contract, boundary);
    }
    if (!lambda_type_matches(rooted_candidate.get(), contract)) {
        ValidationResult* validation = NULL;
        (void)runtime_validate_value_against_type(rooted_candidate.get(), contract, &validation);
        return lambda_type_error_with_validation(rooted_candidate.get(), contract, boundary, validation);
    }
    return rooted_candidate.get();
}

Item lambda_map_set_checked(Item owner, Item key, Item value, Type* expected,
        const char* boundary) {
    return lambda_map_set_checked_impl(owner, key, value, expected, boundary, false);
}

Item lambda_map_set_checked_inplace(Item owner, Item key, Item value, Type* expected,
        const char* boundary) {
    return lambda_map_set_checked_impl(owner, key, value, expected, boundary, true);
}

Item lambda_map_path_set_checked(Item owner, Item path, Item value, Type* expected,
        const char* boundary) {
    // Nested annotated writes validate the proposed root, not merely the final
    // slot. Start from a detached top-level map so a failed post-state check
    // cannot mutate the old root or a snapshot that still observes it.
    Type* contract = runtime_boundary_unwrap_type(expected);
    if (!contract || contract->type_id != LMD_TYPE_MAP) {
        return lambda_type_error(value, expected, boundary);
    }
    RootFrame roots(3);
    Rooted<Item> rooted_owner(roots, owner);
    Rooted<Item> rooted_path(roots, path);
    Rooted<Item> rooted_value(roots, value);
    Rooted<Item> rooted_candidate(roots, cow_clone_one_level(rooted_owner.get()));
    if (get_type_id(rooted_candidate.get()) == LMD_TYPE_ERROR ||
            rooted_candidate.get().item == rooted_owner.get().item) {
        return lambda_type_error(rooted_owner.get(), contract, boundary);
    }
    Item write_result = cow_path_set(rooted_candidate.get(), rooted_path.get(),
        rooted_value.get());
    if (get_type_id(write_result) == LMD_TYPE_ERROR) return write_result;
    Item converted_root = lambda_type_check(rooted_candidate.get(), contract, boundary);
    if (get_type_id(converted_root) == LMD_TYPE_ERROR) return converted_root;
    return converted_root;
}

static Type* runtime_array_contract_element(Type* expected) {
    expected = runtime_boundary_unwrap_type(expected);
    if (!expected) return NULL;
    if (expected->type_id == LMD_TYPE_ARRAY) {
        // `list` is the compact open-sequence type, not a TypeArray payload;
        // reading nested/item_patterns from it crosses the adjacent compact
        // type globals and turns a valid list boundary into a bad Type*.
        if (expected == &TYPE_LIST) return NULL;
        TypeArray* array = (TypeArray*)expected;
        if (array->item_patterns || !array->nested) return NULL;
        return runtime_boundary_unwrap_type(array->nested);
    }
    if (expected->type_id == LMD_TYPE_TYPE && expected->kind == TYPE_KIND_UNARY) {
        TypeUnary* occurrence = (TypeUnary*)expected;
        if (occurrence->op == OPERATOR_REPEAT) {
            return runtime_boundary_unwrap_type(occurrence->operand);
        }
    }
    return NULL;
}

static Item lambda_array_set_checked_impl(Item owner, int64_t index, Item value, Type* expected,
        const char* boundary, bool publish_in_place, const LaneStorageDesc* lane_hint) {
    if (cow_profile_enabled()) {
        g_cow_profile.array_checked_store_calls++;
        if (publish_in_place) g_cow_profile.array_checked_store_unique_inplace++;
    }
    // An annotated array owns a clean element contract. Create the candidate
    // before touching the original so a bad dynamic value cannot trigger
    // ArrayNum-to-Array degradation or leak through a COW snapshot.
    Type* element_type = runtime_array_contract_element(expected);
    if (!element_type) return lambda_type_error(value, expected, boundary);
    Item checked_value = lambda_type_check(value, element_type, boundary);
    if (get_type_id(checked_value) == LMD_TYPE_ERROR) return checked_value;
    LaneStorageDesc lane_desc = {};
    bool has_lane_contract = false;
    if (lane_hint) {
        lane_desc = *lane_hint;
        has_lane_contract = true;
    } else {
        has_lane_contract = lambda_type_lane_storage_desc(element_type, &lane_desc);
    }
    bool owner_representation_proven = has_lane_contract &&
        runtime_array_representation_matches_contract(owner, &lane_desc);
    if (publish_in_place && !owner_representation_proven &&
            !lambda_type_matches(owner, expected)) {
        return lambda_type_error(owner, expected, boundary);
    }

    RootFrame roots(3);
    Rooted<Item> rooted_owner(roots, owner);
    Rooted<Item> rooted_value(roots, checked_value);
    Item candidate = publish_in_place ? rooted_owner.get() :
        cow_prepare_write(rooted_owner.get());
    Rooted<Item> rooted_candidate(roots, candidate);
    if (!publish_in_place && cow_profile_enabled() &&
            rooted_candidate.get().item != rooted_owner.get().item) {
        if (get_type_id(rooted_owner.get()) == LMD_TYPE_ARRAY_NUM) {
            g_cow_profile.array_checked_store_full_clone++;
        } else {
            g_cow_profile.array_checked_store_cow_detach++;
        }
        g_cow_profile.array_checked_store_bytes_copied +=
            cow_one_level_copy_bytes(rooted_owner.get());
    }
    TypeId candidate_type = get_type_id(rooted_candidate.get());
    if (candidate_type != LMD_TYPE_ARRAY && candidate_type != LMD_TYPE_ARRAY_NUM &&
            candidate_type != LMD_TYPE_ELEMENT) {
        return lambda_type_error(rooted_owner.get(), expected, boundary);
    }
    if (candidate_type == LMD_TYPE_ARRAY_NUM && array_native_lane_supported(&lane_desc) &&
            !runtime_array_representation_matches_contract(rooted_candidate.get(), &lane_desc)) {
        if (publish_in_place) {
            // The current pn-var ABI owns an ArrayNum pointer. Publishing an
            // Array replacement needs a separate ABI transition, so reject it.
            set_runtime_error(ERR_TYPE_MISMATCH,
                "nullable native array write through pn var is not implemented");
            return ItemError;
        }
        Item rebuilt = ItemNull;
        if (cow_profile_enabled()) g_cow_profile.array_checked_store_rebuild++;
        if (!array_rebuild_native_lane(rooted_candidate.get(), &lane_desc, &rebuilt)) {
            return lambda_type_error(rooted_candidate.get(), expected, boundary);
        }
        rooted_candidate.set(rebuilt);
        candidate_type = LMD_TYPE_ARRAY;
    }
    Item set_result = fn_array_set(rooted_candidate.get().array, index, rooted_value.get());
    if (get_type_id(set_result) == LMD_TYPE_ERROR) return set_result;
    bool post_store_proven = has_lane_contract &&
        runtime_array_representation_matches_contract(rooted_candidate.get(), &lane_desc);
    if (cow_profile_enabled() && post_store_proven) {
        g_cow_profile.array_checked_store_direct++;
    }
    if (!post_store_proven && !lambda_type_matches(rooted_candidate.get(), expected)) {
        ValidationResult* validation = NULL;
        (void)runtime_validate_value_against_type(rooted_candidate.get(), expected, &validation);
        // The validator inspected the rebuilt array. Reporting only the leaf
        // value hid that candidate context even though its path identifies the
        // failed index.
        return lambda_type_error_with_validation(rooted_candidate.get(), expected, boundary,
            validation);
    }
    return rooted_candidate.get();
}

Item lambda_array_set_checked(Item owner, int64_t index, Item value, Type* expected,
        const char* boundary) {
    return lambda_array_set_checked_impl(owner, index, value, expected, boundary, false, NULL);
}

Item lambda_array_set_checked_item(Item owner, Item key, Item value, Type* expected,
        const char* boundary) {
    int64_t index = 0;
    if (!lambda_item_to_int64_exact(key, &index)) {
        set_runtime_error(ERR_TYPE_MISMATCH,
            "invalid typed array member write: key must be an exact integer");
        return ItemError;
    }
    return lambda_array_set_checked(owner, index, value, expected, boundary);
}

Item lambda_array_set_checked_inplace(Item owner, int64_t index, Item value, Type* expected,
        const char* boundary) {
    return lambda_array_set_checked_impl(owner, index, value, expected, boundary, true, NULL);
}

Item lambda_array_set_checked_inplace_item(Item owner, Item key, Item value, Type* expected,
        const char* boundary) {
    int64_t index = 0;
    if (!lambda_item_to_int64_exact(key, &index)) {
        set_runtime_error(ERR_TYPE_MISMATCH,
            "invalid typed array member write: key must be an exact integer");
        return ItemError;
    }
    return lambda_array_set_checked_inplace(owner, index, value, expected, boundary);
}

static LaneStorageDesc lambda_array_lane_hint(Type* expected, uint8_t lane_kind,
        uint8_t lane_nullable, uint8_t lane_byte_size) {
    LaneStorageDesc hint = {};
    Type* element_type = runtime_array_contract_element(expected);
    hint.semantic_contract = element_type;
    hint.base_contract = element_type;
    hint.kind = lane_kind;
    hint.nullable = lane_nullable;
    hint.byte_size = lane_byte_size;
    return hint;
}

Item lambda_array_set_checked_lane(Item owner, int64_t index, Item value, Type* expected,
        const char* boundary, uint8_t lane_kind, uint8_t lane_nullable,
        uint8_t lane_byte_size) {
    LaneStorageDesc hint = lambda_array_lane_hint(expected, lane_kind, lane_nullable,
        lane_byte_size);
    return lambda_array_set_checked_impl(owner, index, value, expected, boundary, false, &hint);
}

Item lambda_array_set_checked_inplace_lane(Item owner, int64_t index, Item value,
        Type* expected, const char* boundary, uint8_t lane_kind, uint8_t lane_nullable,
        uint8_t lane_byte_size) {
    LaneStorageDesc hint = lambda_array_lane_hint(expected, lane_kind, lane_nullable,
        lane_byte_size);
    return lambda_array_set_checked_impl(owner, index, value, expected, boundary, true, &hint);
}

Item array_set_cow(Item owner, Item key, Item value) {
    Item replacement = cow_prepare_write(owner);
    if (get_type_id(replacement) == LMD_TYPE_ERROR) return replacement;
    TypeId type_id = get_type_id(replacement);
    if (type_id != LMD_TYPE_ARRAY && type_id != LMD_TYPE_ARRAY_NUM &&
            type_id != LMD_TYPE_ELEMENT) {
        log_error("cow array mutation rejected non-array owner type %d", type_id);
        return ItemError;
    }
    int64_t index = 0;
    if (!lambda_item_to_int64_exact(key, &index)) {
        set_runtime_error(ERR_TYPE_MISMATCH,
            "invalid sequence member write: key must be an exact integer");
        return ItemError;
    }
    // COW receives the semantic Item value from a procedural assignment. The
    // canonical setter must decide whether the compact lane can admit it or
    // whether the array must widen; calling array_num_set_item directly would
    // coerce an incompatible value and silently lose the required Item shape.
    if (fn_array_set(replacement.array, index, value).item == ItemError.item) return ItemError;
    return replacement;
}

Item member_set_cow(Item owner, Item key, Item value) {
    Item replacement = cow_prepare_write(owner);
    if (get_type_id(replacement) == LMD_TYPE_ERROR) return replacement;

    Item result = fn_index_set(replacement, key, value);
    if (get_type_id(result) == LMD_TYPE_ERROR) return result;
    return replacement;
}

Item map_set_cow(Item owner, Item key, Item value) {
    Item replacement = cow_prepare_write(owner);
    if (get_type_id(replacement) == LMD_TYPE_ERROR) return replacement;
    TypeId type_id = get_type_id(replacement);
    if (type_id != LMD_TYPE_MAP && type_id != LMD_TYPE_OBJECT &&
            type_id != LMD_TYPE_ELEMENT && type_id != LMD_TYPE_VMAP) {
        log_error("cow map mutation rejected non-map owner type %d", type_id);
        return ItemError;
    }
    if (type_id == LMD_TYPE_VMAP) return vmap_set_cow(replacement, key, value);
    Item result = fn_map_set(replacement, key, value);
    if (get_type_id(result) == LMD_TYPE_ERROR) return result;
    return replacement;
}

Item cow_path_set_raw(Item owner, Item key, Item value) {
    // The compiler links each copied child before descending, so this helper
    // must remain a rooted raw write and never perform another COW decision.
    RootFrame roots(3);
    Rooted<Item> rooted_owner(roots, owner);
    Rooted<Item> rooted_key(roots, key);
    Rooted<Item> rooted_value(roots, value);
    TypeId owner_type = get_type_id(rooted_owner.get());
    int64_t index = 0;
    if ((owner_type == LMD_TYPE_ARRAY || owner_type == LMD_TYPE_ELEMENT ||
            owner_type == LMD_TYPE_ARRAY_NUM) &&
            lambda_item_to_int64_exact(rooted_key.get(), &index)) {
        if (owner_type == LMD_TYPE_ARRAY_NUM) {
            array_num_set_item(rooted_owner.get().array_num, index, rooted_value.get());
        } else if (fn_array_set(rooted_owner.get().array, index, rooted_value.get()).item == ItemError.item) {
            return ItemError;
        }
        return rooted_owner.get();
    }
    if (owner_type == LMD_TYPE_MAP || owner_type == LMD_TYPE_OBJECT ||
            owner_type == LMD_TYPE_ELEMENT || owner_type == LMD_TYPE_VMAP) {
        if (owner_type == LMD_TYPE_MAP &&
                !runtime_named_map_field((Type*)rooted_owner.get().map->type,
                    rooted_key.get()) &&
                !map_extend_open_shape(rooted_owner.get(), rooted_key.get(),
                    rooted_value.get())) {
            log_error("cow path mutation could not extend map shape");
            return ItemError;
        }
        fn_map_set(rooted_owner.get(), rooted_key.get(), rooted_value.get());
        return rooted_owner.get();
    }
    log_error("cow path mutation rejected owner type %d", owner_type);
    return ItemError;
}

Item cow_path_set(Item owner, Item path, Item value) {
    if (get_type_id(path) != LMD_TYPE_ARRAY || !path.array || path.array->length <= 0) {
        log_error("cow path mutation requires a non-empty array path");
        return ItemError;
    }

    // Every spine link may allocate while it is detached, so all live owners,
    // keys, and the incoming value need exact roots across raw setter calls.
    RootFrame roots(7);
    Rooted<Item> rooted_owner(roots, owner);
    Rooted<Item> rooted_path(roots, path);
    Rooted<Item> rooted_value(roots, value);
    Rooted<Item> rooted_current(roots, ItemNull);
    Rooted<Item> rooted_child(roots, ItemNull);
    Rooted<Item> rooted_key(roots, ItemNull);
    Rooted<Item> rooted_replacement(roots, ItemNull);

    rooted_current.set(cow_prepare_write(rooted_owner.get()));
    if (get_type_id(rooted_current.get()) == LMD_TYPE_ERROR) return ItemError;
    // The top-level replacement survives every child detach; returning an
    // unrooted local here used a pre-compaction address for nested writes.
    rooted_replacement.set(rooted_current.get());

    for (int64_t i = 0; i < rooted_path.get().array->length; i++) {
        rooted_key.set(item_at(rooted_path.get(), i));
        if (get_type_id(rooted_key.get()) == LMD_TYPE_NULL) return ItemError;
        if (i + 1 == rooted_path.get().array->length) {
            if (get_type_id(cow_path_set_raw(rooted_current.get(), rooted_key.get(), rooted_value.get())) ==
                    LMD_TYPE_ERROR) return ItemError;
            return rooted_replacement.get();
        }

        rooted_child.set(fn_index(rooted_current.get(), rooted_key.get()));
        if (!cow_item_is_container(rooted_child.get()) &&
                get_type_id(rooted_child.get()) != LMD_TYPE_ARRAY_NUM) {
            log_error("cow path mutation encountered a non-container child");
            return ItemError;
        }
        rooted_child.set(cow_prepare_write(rooted_child.get()));
        if (get_type_id(rooted_child.get()) == LMD_TYPE_ERROR) return ItemError;
        if (get_type_id(cow_path_set_raw(rooted_current.get(), rooted_key.get(), rooted_child.get())) ==
                LMD_TYPE_ERROR) return ItemError;
        rooted_current.set(rooted_child.get());
    }
    return ItemError;
}

// rebuild a map/element shape when a field's type changes
// creates new ShapeEntry chain + TypeMap/TypeElmt, allocates new data buffer, copies fields
// For markup containers (!is_heap), uses runtime pool instead of calloc/free to avoid
// corrupting input pool memory. The is_data_migrated flag tracks this transition.
static void map_rebuild_for_type_change(void** type_slot, void** data_slot, int* cap_slot,
                                        TypeId container_type_id,
                                        Container* container,
                                        ShapeEntry* changed_entry,
                                        Type* new_field_contract, Item new_value) {
    TypeMap* old_map_type = (TypeMap*)*type_slot;
    void* old_data = NULL;
    if (!new_field_contract) {
        log_error("map_rebuild: missing replacement field contract");
        return;
    }

    // count existing fields
    int field_count = 0;
    ShapeEntry* e = old_map_type->shape;
    while (e) { field_count++; e = e->next; }
    // No artificial upper bound: rebuild only uses heap/pool allocation per field,
    // no stack arrays or fixed-size buffers. globalThis legitimately has 100+ fields.
    if (field_count <= 0) {
        log_error("map_rebuild: invalid field count %d", field_count);
        return;
    }

    // Constructor fields form a fixed-width prefix; properties appended later
    // remain packed and must not inherit the prefix's 8-byte write invariant.
    int fixed_slot_count = typemap_fixed_slot_prefix_count(old_map_type);

    // build new shape chain with updated type for the changed field
    ShapeEntry* first = NULL;
    ShapeEntry* prev = NULL;
    ShapeEntry* last = NULL;

    e = old_map_type->shape;
    int field_index = 0;
    int64_t byte_offset = (int64_t)fixed_slot_count * (int64_t)sizeof(void*);
    while (e) {
        Type* field_contract = e == changed_entry ? new_field_contract : e->type;

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
        ne->name_hash = e->name_hash ? e->name_hash : typemap_name_hash(nv->str, (int)nv->length);
        ne->name_id = e->name_id;
        ne->key_kind = e->key_kind;
        // Retain an unchanged field's full contract. Replacing `number` or a
        // union with its LMD_TYPE_TYPE carrier makes the packed Item look like
        // a Type* and corrupts it on the next validation read.
        ne->type = field_contract;
        bool fixed_slot = field_index < fixed_slot_count;
        ne->byte_offset = fixed_slot ? e->byte_offset : byte_offset;
        ne->next = NULL;
        ne->ns = e->ns;
        // Preserve property attribute flags (JSPD_IS_ACCESSOR, NW, NE, NC, etc.)
        // and default_value across shape rebuild. Without this, accessors lose
        // their IS_ACCESSOR flag and writable/enum/config flags get reset to JS
        // defaults whenever a sibling field's type transitions trigger a rebuild.
        ne->flags = e->flags;
        ne->default_value = e->default_value;

        if (!first) first = ne;
        if (prev) prev->next = ne;
        last = ne;
        prev = ne;
        if (!fixed_slot) {
            byte_offset += type_info[type_field_storage_type_id(field_contract)].byte_size;
        }
        field_index++;
        e = e->next;
    }
    int64_t new_byte_size = byte_offset;

    // allocate new data buffer
    // For heap containers: calloc (consistent with map_fill, freed by free_container)
    // For markup containers: pool_calloc from runtime pool (data migrated from input pool)
    bool use_pool = !container->is_heap;
    // Shape rebuild is already a cold path. A structured frame avoids leaving
    // registered native addresses behind on a non-local recovery edge while
    // keeping both unpublished owners exact through the data allocation.
    RootFrame roots(2);
    Rooted<Container*> rooted_container(roots, container);
    Rooted<Item> rooted_value(roots, new_value);

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
    container = rooted_container.get();
    new_value = rooted_value.get();

    // Re-read old_data after allocation: GC may have fired during
    // heap_data_calloc, compacting *data_slot from nursery to tenured.
    // The local old_data would then point to freed nursery memory.
    old_data = *data_slot;

    // copy field values from old buffer to new buffer
    ShapeEntry* old_e = old_map_type->shape;
    ShapeEntry* new_e = first;
    field_index = 0;
    while (old_e && new_e) {
        void* old_field = (char*)old_data + old_e->byte_offset;
        void* new_field = (char*)new_data + new_e->byte_offset;

        if (old_e == changed_entry) {
            // decrement old value's ref count
            map_field_decrement_ref(old_field, old_e->type->type_id);
            // The new ShapeEntry owns the semantic contract. In particular,
            // `int?` must encode null as its lane sentinel rather than using
            // the historical raw Item fallback selected by TypeId alone.
            if (!map_shape_field_store_native_lane(new_field, new_e, new_value)) {
            // Store according to the rebuilt shape, not the source Item tag.
            // Composite contracts such as `int[]` use the self-describing
            // TypedItem slot even though their current value is an Array
            // pointer; using the value tag here leaves `_map_read_field`
            // interpreting that pointer as an uninitialized TypedItem.
            map_field_store(new_field, new_value,
                shape_entry_storage_type_id(new_e));
            }
        } else {
            // unchanged field — copy bytes (ref count unchanged, same pointers)
            int sz = field_index < fixed_slot_count ? (int)sizeof(void*) :
                type_info[shape_entry_storage_type_id(old_e)].byte_size;
            memcpy(new_field, old_field, sz);
        }
        field_index++;
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

        // Populate/grow hash table for O(1) property lookup.
        typemap_hash_build((TypeMap*)new_et, context->pool);

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
        new_mt->has_named_shape = old_map_type->has_named_shape;
        new_mt->is_trusted_contract = false;
        new_mt->struct_name = old_map_type->struct_name;
        new_mt->is_private_clone = old_map_type->is_private_clone;
        new_mt->is_shared_constructor_shape = false;
        new_mt->is_transition_shared_shape = false;
        new_mt->transitions = NULL;
        new_mt->js_meta = old_map_type->js_meta;

        // Populate/grow hash table for O(1) property lookup.
        typemap_hash_build(new_mt, context->pool);

        // Preserve only the verified fixed constructor prefix. Appended packed
        // fields are never valid targets for slot-indexed stores.
        if (fixed_slot_count > 0) {
            ShapeEntry** entries = (ShapeEntry**)pool_calloc(context->pool,
                fixed_slot_count * sizeof(ShapeEntry*));
            if (entries) {
                ShapeEntry* se = first;
                for (int i = 0; i < fixed_slot_count && se; i++, se = se->next) {
                    entries[i] = se;
                }
                new_mt->slot_entries = entries;
                new_mt->slot_count = fixed_slot_count;
            }
        }

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
static bool map_shared_ctor_shape_should_detach_for_type(TypeMap* tm,
        TypeId field_type, TypeId value_type) {
    if (!typemap_is_shared_shape(tm)) return false;
    if (field_type == value_type) return false;
    // FLOAT stores its numeric payload in the same double carrier as INT.
    if (field_type == LMD_TYPE_FLOAT && value_type == LMD_TYPE_INT) return false;
    return true;
}

static ShapeEntry* map_find_shape_entry(TypeMap* tm, const char* key_cstr, size_t key_len) {
    if (!tm || !key_cstr) return NULL;
    if (key_len <= INT_MAX) {
        ShapeEntry* hit = typemap_hash_lookup(tm, key_cstr, (int)key_len);
        if (hit) return hit;
    }
    ShapeEntry* entry = tm->shape;
    while (entry) {
        if (entry->name && entry->name->str && entry->name->length == key_len &&
                memcmp(entry->name->str, key_cstr, key_len) == 0) {
            return entry;
        }
        entry = entry->next;
    }
    return NULL;
}

static bool runtime_type_admit_map(Item value, Type* expected, Item* converted,
        bool relation_proven = false) {
    if (get_type_id(value) != LMD_TYPE_MAP || !value.map || !expected ||
            expected->type_id != LMD_TYPE_MAP) {
        return false;
    }

    // A map conversion must isolate only the root being reified. Its fields and
    // unchanged nested containers remain shared until a later write detaches
    // them; deep cloning here made validation cost grow with the full graph.
    if (cow_profile_enabled()) {
        cow_profile_note_map_admit_bytes(cow_one_level_copy_bytes(value));
    }
    RootFrame roots(3);
    // COW already records whether this root has aliases. Reifying a unique
    // map in place publishes the verified contract to later boundary reads;
    // cloning every admission kept the original generic shape alive and made
    // the same recursive tree node pay the conversion repeatedly. Shared or
    // static roots still detach through the normal COW path.
    Rooted<Item> rooted_candidate(roots, cow_prepare_write(value));
    Rooted<Item> rooted_field(roots, ItemNull);
    Rooted<Item> rooted_converted(roots, ItemNull);
    if (get_type_id(rooted_candidate.get()) != LMD_TYPE_MAP) {
        return false;
    }

    TypeMap* expected_map = (TypeMap*)expected;
    for (ShapeEntry* expected_field = expected_map->shape; expected_field;
            expected_field = expected_field->next) {
        if (!expected_field->name || !expected_field->name->str || !expected_field->type) {
            continue;
        }
        if (cow_profile_enabled()) g_cow_profile.map_admit_fields_visited++;
        Map* candidate_map = rooted_candidate.get().map;
        TypeMap* candidate_type = candidate_map ? (TypeMap*)candidate_map->type : NULL;
        ShapeEntry* candidate_field = map_find_shape_entry(candidate_type,
            expected_field->name->str, expected_field->name->length);
        if (!candidate_field) return false;

        rooted_field.set(_map_read_field(candidate_field, candidate_map->data));
        Item field_converted = ItemNull;
        if (!runtime_type_admit_value(rooted_field.get(), expected_field->type,
                &field_converted)) {
            return false;
        }
        rooted_converted.set(field_converted);

        candidate_map = rooted_candidate.get().map;
        candidate_type = candidate_map ? (TypeMap*)candidate_map->type : NULL;
        candidate_field = map_find_shape_entry(candidate_type,
            expected_field->name->str, expected_field->name->length);
        if (!candidate_field || !candidate_field->type) return false;
        LaneStorageDesc expected_lane = {};
        LaneStorageDesc candidate_lane = {};
        bool expected_native_lane = shape_entry_uses_native_lane(expected_field,
            &expected_lane);
        bool candidate_native_lane = shape_entry_uses_native_lane(candidate_field,
            &candidate_lane);
        bool value_changed = rooted_converted.get().item != rooted_field.get().item;
        // A dynamic ANY field can hold the right value while retaining a
        // different packed width/offset. Direct MIR reads use the admitted
        // contract's layout, so publish that layout instead of accepting the
        // value-only match and letting later fields be read at the wrong byte.
        bool semantic_reification = !lambda_type_contract_semantically_compatible(
            candidate_field->type, expected_field->type);
        bool storage_reification = candidate_field->byte_offset != expected_field->byte_offset ||
            shape_entry_storage_type_id(candidate_field) !=
                shape_entry_storage_type_id(expected_field);
        if (candidate_native_lane != expected_native_lane) storage_reification = true;
        if (candidate_native_lane && expected_native_lane &&
                (candidate_lane.kind != expected_lane.kind ||
                    candidate_lane.byte_size != expected_lane.byte_size ||
                    candidate_lane.nullable != expected_lane.nullable ||
                    candidate_lane.base_contract != expected_lane.base_contract)) {
            storage_reification = true;
        }
        bool needs_reification = semantic_reification || storage_reification ||
            (expected_native_lane && !candidate_native_lane);
        if (!value_changed && !needs_reification) continue;

        if (needs_reification) {
            // `fn_map_set` deliberately widens an int back into a float slot.
            // Boundary admission instead changes the detached candidate shape so
            // the stored field's observable tag satisfies the named contract.
            // The nullable case also changes its physical carrier even though
            // the source int already satisfies the optional semantic contract.
            map_rebuild_for_type_change((void**)&candidate_map->type,
                &candidate_map->data, &candidate_map->data_cap, LMD_TYPE_MAP,
                (Container*)candidate_map, candidate_field,
                expected_field->type,
                rooted_converted.get());
        } else {
            String* field_name = heap_create_name(expected_field->name->str,
                expected_field->name->length);
            if (!field_name) return false;
            rooted_field.set({.item = s2it(field_name)});
            fn_map_set(rooted_candidate.get(), rooted_field.get(), rooted_converted.get());
        }
    }

    if (relation_proven && rooted_candidate.get().map->data_cap >= expected_map->byte_size) {
        // Reification constructed the expected lane layout field by field; use
        // the canonical contract descriptor so later exact admissions remain
        // O(1) instead of validating this freshly converted root again.
        rooted_candidate.get().map->type = expected_map;
    }
    // relation_proven means every field already passed the structural semantic
    // relation; the loop above owns the conversion proof and a graph-wide
    // validator would rescan unchanged child maps.
    if (!relation_proven && !lambda_type_matches(rooted_candidate.get(), expected)) return false;
    *converted = rooted_candidate.get();
    return true;
}

static bool runtime_type_admit_array(Item value, Type* expected, Item* converted) {
    Type* element_type = runtime_array_contract_element(expected);
    TypeId source_type = get_type_id(value);
    if (!element_type || (source_type != LMD_TYPE_ARRAY &&
            source_type != LMD_TYPE_ARRAY_NUM && source_type != LMD_TYPE_ELEMENT)) {
        return false;
    }

    RootFrame roots(3);
    Rooted<Item> rooted_candidate(roots, fn_mutable_value(value));
    Rooted<Item> rooted_element(roots, ItemNull);
    Rooted<Item> rooted_converted(roots, ItemNull);
    TypeId candidate_type = get_type_id(rooted_candidate.get());
    if ((candidate_type != LMD_TYPE_ARRAY && candidate_type != LMD_TYPE_ARRAY_NUM &&
            candidate_type != LMD_TYPE_ELEMENT) || rooted_candidate.get().item == value.item) {
        return false;
    }

    int64_t length = rooted_candidate.get().array->length;
    for (int64_t index = 0; index < length; index++) {
        rooted_element.set(item_at(rooted_candidate.get(), index));
        Item element_converted = ItemNull;
        if (!runtime_type_admit_value(rooted_element.get(), element_type,
                &element_converted)) {
            return false;
        }
        rooted_converted.set(element_converted);
        if (rooted_converted.get().item == rooted_element.get().item) continue;

        if (get_type_id(rooted_candidate.get()) == LMD_TYPE_ARRAY_NUM) {
            // A Float ArrayNum has no per-item tags. Reify it before publishing
            // converted elements so `3.0 -> int` cannot remain physically float.
            convert_specialized_to_generic(rooted_candidate.get().array);
        }
        if (get_type_id(fn_array_set(rooted_candidate.get().array, index,
                rooted_converted.get())) == LMD_TYPE_ERROR) {
            return false;
        }
    }

    LaneStorageDesc lane_desc = {};
    if (lambda_type_lane_storage_desc(element_type, &lane_desc) &&
            array_native_lane_supported(&lane_desc) &&
            !array_native_lane_matches_desc(rooted_candidate.get().array, &lane_desc)) {
        Item rebuilt = ItemNull;
        if (!array_rebuild_native_lane(rooted_candidate.get(), &lane_desc, &rebuilt)) {
            return false;
        }
        rooted_candidate.set(rebuilt);
    }
    if (!lambda_type_matches(rooted_candidate.get(), expected)) return false;
    *converted = rooted_candidate.get();
    return true;
}

static MapContractRelation runtime_map_contract_relation_cached(
        const TypeMap* candidate, const TypeMap* expected) {
    if (!candidate || !expected) return MAP_CONTRACT_INCOMPATIBLE;
    Heap* heap = context ? context->heap : NULL;
    if (!heap) return lambda_map_contract_relation(candidate, expected);

    for (uint32_t i = 0; i < LAMBDA_MAP_CONTRACT_CACHE_CAPACITY; i++) {
        LambdaMapContractCacheEntry* entry = &heap->map_contract_cache[i];
        if (entry->candidate == candidate && entry->expected == expected) {
            if (cow_profile_enabled()) {
                g_cow_profile.map_admit_relation_cache_hits++;
            }
            return (MapContractRelation)entry->relation;
        }
    }

    if (cow_profile_enabled()) g_cow_profile.map_admit_relation_cache_misses++;
    MapContractRelation relation = lambda_map_contract_relation(candidate, expected);
    uint32_t slot = heap->map_contract_cache_next++ %
        LAMBDA_MAP_CONTRACT_CACHE_CAPACITY;
    heap->map_contract_cache[slot].candidate = candidate;
    heap->map_contract_cache[slot].expected = expected;
    heap->map_contract_cache[slot].relation = (uint8_t)relation;
    return relation;
}

static bool runtime_type_admit_value(Item value, Type* expected, Item* converted) {
    if (!converted) return false;
    expected = runtime_boundary_unwrap_type(expected);
    if (!expected) return false;

    LambdaNumericKind source_kind = lambda_numeric_kind_from_item(value);
    LambdaNumericKind target_kind = lambda_numeric_kind_from_type(expected);
    if (source_kind != LAMBDA_NUM_INVALID && target_kind != LAMBDA_NUM_INVALID &&
            target_kind != LAMBDA_NUM_INTEGER) {
        // A declared concrete numeric boundary establishes the destination
        // representation, even when membership would accept the source tag.
        // Checking lambda_type_matches first left an int-tagged value at a
        // float boundary, after which a native float body could unbox it as
        // the wrong physical lane.
        return lambda_numeric_boundary_admit(value, expected, converted);
    }

    // A nullable concrete array needs its own carrier even when its current
    // values also satisfy the narrower T[] contract. This is the covariant
    // int[] -> int?[] boundary; do not lose the destination's nullability.
    if ((get_type_id(value) == LMD_TYPE_ARRAY || get_type_id(value) == LMD_TYPE_ARRAY_NUM)) {
        Type* element_type = runtime_array_contract_element(expected);
        LaneStorageDesc lane_desc = {};
        if (element_type && lambda_type_lane_storage_desc(element_type, &lane_desc) &&
                array_native_lane_supported(&lane_desc) &&
                !array_native_lane_matches_desc(value.array, &lane_desc)) {
            return runtime_type_admit_array(value, expected, converted);
        }
    }

    // A trusted compiler-built map contract is an admission certificate only
    // when the candidate carries that exact TypeMap. Dynamic and JS-created
    // shapes still take the validator/conversion path even if their bytes line up.
    if (get_type_id(value) == LMD_TYPE_MAP && expected->type_id == LMD_TYPE_MAP &&
            expected != &TYPE_MAP) {
        if (cow_profile_enabled()) g_cow_profile.map_admit_calls++;
        TypeMap* expected_map = (TypeMap*)expected;
        TypeMap* candidate_map = value.map ? (TypeMap*)value.map->type : NULL;
        if (candidate_map && typemap_ptr_is_plausible(candidate_map)) {
            MapContractRelation relation = runtime_map_contract_relation_cached(
                candidate_map, expected_map);
            if (relation == MAP_CONTRACT_EXACT_TRUSTED) {
                if (cow_profile_enabled()) g_cow_profile.map_admit_exact_shape_hits++;
                *converted = value;
                return true;
            }
            if (relation == MAP_CONTRACT_STORAGE_COMPATIBLE) {
                if (cow_profile_enabled()) {
                    g_cow_profile.map_admit_storage_compatible_hits++;
                    g_cow_profile.map_admit_readonly_validations++;
                }
                // The candidate shape is maintained by every map write: a
                // type-changing store retags or rebuilds its ShapeEntry before
                // publishing the value. Once every field has the same semantic
                // contract and physical lane as the expected shape, rescanning
                // the packed value through the schema validator only repeats
                // the proof already made by the shape relation.
                *converted = value;
                return true;
            }
            if (relation == MAP_CONTRACT_NEEDS_REIFICATION ||
                    relation == MAP_CONTRACT_INCOMPATIBLE) {
                if (cow_profile_enabled()) g_cow_profile.map_admit_reifications++;
                // The static relation deliberately rejects erased `any` fields,
                // but the runtime value may still satisfy the named contract.
                // Route that conservative result through field admission rather
                // than letting the generic validator accept a physically
                // incompatible shape that direct MIR reads would misaddress.
                return runtime_type_admit_map(value, expected, converted,
                    relation == MAP_CONTRACT_NEEDS_REIFICATION);
            }
        }
    }

    // Preserve an already conforming union member rather than arbitrarily
    // re-representing it through an earlier union arm.
    if (lambda_type_matches(value, expected)) {
        *converted = value;
        return true;
    }

    if (expected->type_id == LMD_TYPE_TYPE && expected->kind == TYPE_KIND_BINARY &&
            ((TypeBinary*)expected)->op == OPERATOR_UNION) {
        TypeBinary* union_type = (TypeBinary*)expected;
        return runtime_type_admit_value(value, union_type->left, converted) ||
            runtime_type_admit_value(value, union_type->right, converted);
    }

    if (source_kind != LAMBDA_NUM_INVALID && target_kind != LAMBDA_NUM_INVALID) {
        // A numeric pair reached this DEFERRED boundary but cannot be represented
        // exactly in the target contract. Do not let type-directional membership
        // turn that failed conversion into a truncating native unbox.
        return lambda_numeric_boundary_admit(value, expected, converted);
    }

    if (expected->type_id == LMD_TYPE_MAP) {
        return runtime_type_admit_map(value, expected, converted);
    }
    if (expected->type_id == LMD_TYPE_TYPE && expected->kind == TYPE_KIND_UNARY &&
            ((TypeUnary*)expected)->op == OPERATOR_REPEAT) {
        return runtime_type_admit_array(value, expected, converted);
    }
    return false;
}

static ShapeEntry* map_detach_shared_ctor_shape_for_type(Item map_item,
        TypeMap** map_type_slot, void** type_slot, const char* key_cstr,
        size_t key_len, NameRef key_ref, ShapeEntry* entry, TypeId value_type) {
    if (!map_type_slot || !*map_type_slot || !entry || !entry->type) return entry;
    TypeId field_type = entry->type->type_id;
    if (!map_shared_ctor_shape_should_detach_for_type(*map_type_slot, field_type, value_type)) {
        return entry;
    }
    NameId operation_name_id = key_ref ? property_key_id(key_ref) : NAME_ID_NONE;
    TypeMap* transition = js_typemap_transition_for_type(map_item, entry,
        operation_name_id, value_type);
    if (transition) {
        *map_type_slot = transition;
        if (type_slot) *type_slot = transition;
        ShapeEntry* refreshed = key_ref && property_key_id(key_ref) != NAME_ID_NONE
            ? typemap_hash_lookup_by_name_id(transition,
                property_key_id(key_ref), property_key_hash(key_ref))
            : map_find_shape_entry(transition, key_cstr, key_len);
        if (refreshed) return refreshed;
    }
    TypeMap* clone = js_typemap_clone_for_mutation_pub(map_item);
    if (!clone) return entry;
    *map_type_slot = clone;
    if (type_slot) *type_slot = clone;
    ShapeEntry* refreshed = key_ref && property_key_id(key_ref) != NAME_ID_NONE
        ? typemap_hash_lookup_by_name_id(clone,
            property_key_id(key_ref), property_key_hash(key_ref))
        : map_find_shape_entry(clone, key_cstr, key_len);
    // Ordinary Input and runtime strings use the id-less byte seam; identity
    // keys recover the cloned slot by NameId.
    return refreshed ? refreshed : entry;
}

Item fn_map_set(Item map_item, Item key, Item value) {
    TypeId map_type_id = get_type_id(map_item);

    // VMap: in-place mutation via vtable
    if (map_type_id == LMD_TYPE_VMAP) {
        if (!is_text_type_id(get_type_id(key))) {
            set_runtime_error(ERR_TYPE_MISMATCH,
                "invalid named member write: key must be string or symbol");
            return ItemError;
        }
        // VMap backing storage is lazy for host wrappers; route all writes
        // through the public setter so ordinary maps allocate on first write.
        vmap_set(map_item, key, value);
        return ItemNull;
    }

    // support both Map and Element (Element has map-like attributes)
    TypeMap* map_type = NULL;
    void** type_slot = NULL;
    void** data_slot = NULL;
    int* cap_slot = NULL;

    if (map_type_id == LMD_TYPE_MAP) {
        Map* mp = map_item.map;
        if (!mp) { log_error("fn_map_set: null map"); return ItemError; }
        if (mp->is_static) { log_error("fn_map_set: cannot mutate static map"); return ItemError; }
        type_slot = &mp->type;
        data_slot = &mp->data;
        cap_slot = &mp->data_cap;
    } else if (map_type_id == LMD_TYPE_OBJECT) {
        Object* obj = map_item.object;
        if (!obj) { log_error("fn_map_set: null object"); return ItemError; }
        if (obj->is_static) { log_error("fn_map_set: cannot mutate static object"); return ItemError; }
        type_slot = &obj->type;
        data_slot = &obj->data;
        cap_slot = &obj->data_cap;
    } else if (map_type_id == LMD_TYPE_ELEMENT) {
        Element* el = (Element*)map_item.container;
        if (!el) { log_error("fn_map_set: null element"); return ItemError; }
        if (el->is_static) { log_error("fn_map_set: cannot mutate static element"); return ItemError; }
        type_slot = &el->type;
        data_slot = &el->data;
        cap_slot = &el->data_cap;
    } else {
        log_error("fn_map_set: not a map or element (type=%d)", map_type_id);
        return ItemError;
    }

    map_type = (TypeMap*)*type_slot;
    if (!map_type || !map_type->shape) {
        log_error("fn_map_set: no shape");
        return ItemError;
    }

    // get key text; Lambda strings/symbols are length-bearing even when their
    // chars are NUL-terminated, and JS property names may contain any byte.
    const char* key_cstr = NULL;
    size_t key_len = 0;
    String* key_string = NULL;
    TypeId key_type = get_type_id(key);
    if (key_type == LMD_TYPE_STRING) {
        key_string = key.get_safe_string();
        key_cstr = key_string ? key_string->chars : NULL;
        key_len = key_string ? key_string->len : 0;
    } else if (key_type == LMD_TYPE_SYMBOL) {
        Symbol* sym = key.get_safe_symbol();
        key_cstr = sym ? sym->chars : NULL;
        key_len = sym ? sym->len : 0;
    } else {
        log_error("fn_map_set: key must be string or symbol");
        return ItemError;
    }
    if (!key_cstr) {
        log_error("fn_map_set: null key string");
        return ItemError;
    }
    NameRef key_ref = key_type == LMD_TYPE_STRING &&
        string_is_pooled(key_string) ? key_string : NULL;
    NameId key_id = key_ref ? property_key_id(key_ref) : NAME_ID_NONE;
    bool identity_key = key_ref && property_key_requires_identity(key_ref);

    // find field in shape
    TypeId value_type = get_type_id(value);
    ShapeEntry* entry = map_type->shape;
    while (entry) {
        bool name_matches = false;
        if (identity_key) {
            // Symbol/private diagnostic bytes are not property identity.
            name_matches = entry->name_id != NAME_ID_NONE && entry->name_id == key_id;
        } else if (entry->key_kind == NAME_KEY_STRING) {
            // Ordinary Input fields have no NameId, so this is the explicit
            // byte-confirmation seam; pointer equality is never identity.
            name_matches = shape_field_name_equals(entry, key_cstr, key_len);
        }
        if (name_matches) {
            TypeId field_type = entry->type->type_id;
            entry = map_detach_shared_ctor_shape_for_type(map_item, &map_type,
                type_slot, key_cstr, key_len, key_ref, entry, value_type);
            if (!entry || !entry->type) return ItemError;
            // A reserved constructor slot becomes observable only at the
            // source assignment that reaches this storage write.
            if (map_type_id == LMD_TYPE_MAP) {
                map_ctor_initialize_offset(map_item.map, entry->byte_offset);
            }
            field_type = entry->type->type_id;
            void* field_ptr = (char*)*data_slot + entry->byte_offset;

            LaneStorageDesc lane = {};
            if (shape_entry_uses_native_lane(entry, &lane) &&
                    map_shape_field_store_native_lane(field_ptr, entry, value)) {
                return ItemNull;
            }

            if (field_type == value_type) {
                // same type — fast path: in-place update
                map_field_decrement_ref(field_ptr, field_type);
                map_field_store(field_ptr, value, value_type);
                return ItemNull;
            }

            // FLOAT field + INT value → widen int to double (lossless, no reshape)
            if (field_type == LMD_TYPE_FLOAT && value_type == LMD_TYPE_INT) {
                *(double*)field_ptr = lambda_int_item_value(value);
                return ItemNull;
            }

            // `int` and `int64` both occupy eight bytes, but their slot
            // carriers differ (double versus int64_t).  Rebuild the field so
            // subsequent reads use the carrier that was actually written.

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
                    return ItemNull;
                }
            }

            // Container/null type change — fast path (same byte size = sizeof(void*))
            // NULL↔MAP, NULL↔ARRAY, MAP↔ELEMENT, etc. all store pointers,
            // and _map_read_field handles null pointers for all container types.
            // No shape rebuild needed — just update the value in-place.
            // UNDEFINED and BOOL are included because:
            //   - For UNDEFINED: _map_read_field ignores the stored value entirely
            //     (returns a constant), so an 8-byte slot with zeros is fine.
            //   - For BOOL: stored as 1 byte but in shaped (JS constructor) objects
            //     the slot is always 8 bytes. Avoiding rebuild preserves slot alignment.
            //   - Key invariant: shaped objects have all 8-byte slots. Rebuild would
            //     break JIT-compiled code that uses hardcoded byte_offset = slot*8.
            {
                bool old_is_ptr = (field_type == LMD_TYPE_NULL || field_type == LMD_TYPE_MAP ||
                    field_type == LMD_TYPE_VMAP ||
                    field_type == LMD_TYPE_ELEMENT || field_type == LMD_TYPE_OBJECT ||
                    field_type == LMD_TYPE_ARRAY || field_type == LMD_TYPE_ARRAY_NUM ||
                    field_type == LMD_TYPE_RANGE ||
                    field_type == LMD_TYPE_UNDEFINED || field_type == LMD_TYPE_BOOL);
                bool new_is_ptr = (value_type == LMD_TYPE_NULL || value_type == LMD_TYPE_MAP ||
                    value_type == LMD_TYPE_VMAP ||
                    value_type == LMD_TYPE_ELEMENT || value_type == LMD_TYPE_OBJECT ||
                    value_type == LMD_TYPE_ARRAY || value_type == LMD_TYPE_ARRAY_NUM ||
                    value_type == LMD_TYPE_RANGE ||
                    value_type == LMD_TYPE_UNDEFINED || value_type == LMD_TYPE_BOOL);
                if (old_is_ptr && new_is_ptr) {
                    // safety: only do in-place update when byte sizes match.
                    // shaped (constructor) objects always use 8-byte slots, but
                    // regular map_put objects use type_info byte sizes which
                    // differ for UNDEFINED(1)/BOOL(1) vs pointer types(8).
                    int old_bsz = type_info[field_type].byte_size;
                    int new_bsz = type_info[value_type].byte_size;
                    if (old_bsz != new_bsz) {
                        // fall through to map_rebuild_for_type_change
                    } else {
                        map_field_decrement_ref(field_ptr, field_type);
                        map_field_store(field_ptr, value, value_type);
                        // Update the ShapeEntry type so GC can properly trace
                        // container pointers stored in formerly-NULL fields, and
                        // so a stored null is not read back as a zero-valued T.
                        // The T→NULL downgrade is gated on this instance owning
                        // the shape — see shape_entry_retag_is_safe.
                        if (shape_entry_retag_is_safe(map_type, value_type)) {
                            entry->type = type_info[value_type].type;
                        }
                        return ItemNull;
                    }
                }
            }

            // Shaped JS objects preallocate fixed 8-byte slots and publish
            // slot_entries for slot-indexed access.  Do not run the generic
            // Lambda map rebuild here: it packs fields by type byte size and
            // breaks the slot*8 layout used by compiled JS accessors.
            if (typemap_entry_uses_fixed_slot(map_type, entry)) {
                map_field_decrement_ref(field_ptr, field_type);
                map_field_store(field_ptr, value, value_type);
                if (shape_entry_retag_is_safe(map_type, value_type)) {
                    entry->type = type_info[value_type].type;
                }
                return ItemNull;
            }

            // type change — rebuild shape with new field type
            log_debug("fn_map_set: type change for '%s' (%d → %d), rebuilding shape",
                      key_cstr, field_type, value_type);
            Container* cont = map_item.container;
            map_rebuild_for_type_change(type_slot, data_slot, cap_slot,
                                        map_type_id, cont, entry,
                                        type_info[value_type].type, value);
            return ItemNull;
        }
        entry = entry->next;
    }
    // A shaped map cannot grow through an unknown member write; return the
    // hard error so callers do not mistake the old silent no-op for success.
    log_error("fn_map_set: field '%s' not found", key_cstr);
    return ItemError;
}
