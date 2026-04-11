/**
 * JavaScript Runtime Functions for Lambda v2
 *
 * Implements JavaScript semantics on top of Lambda's type system.
 * All functions are callable from MIR JIT compiled code.
 */
#include "js_runtime.h"
#include "js_dom.h"
#include "js_cssom.h"
#include "js_typed_array.h"
#include "js_event_loop.h"
#include "../lambda-data.hpp"
#include "../transpiler.hpp"
#include "../module_registry.h"
#include "../../lib/log.h"
#include "../../lib/hashmap.h"
#include "../../lib/str.h"
#include <cstring>
#include <cmath>
#include <cstdlib>
#include <cstdio>
#include <cctype>
#include <string>
#include <re2/re2.h>

// v22: Maximum gap allowed for dense array expansion; beyond this, skip to avoid OOM
#define SPARSE_GAP_MAX 1000000

// Forward declarations for Unicode normalization (implemented in utf_string.cpp)
extern "C" char* normalize_utf8proc_nfc(const char* str, int len, int* out_len);
extern "C" char* normalize_utf8proc_nfd(const char* str, int len, int* out_len);
extern "C" char* normalize_utf8proc_nfkc(const char* str, int len, int* out_len);
extern "C" char* normalize_utf8proc_nfkd(const char* str, int len, int* out_len);

// Global Input context for JS runtime map_put operations.
// Initialized in transpile_js_to_mir() before JIT execution.
Input* js_input = NULL;

// v24: Global strict mode flag. Set by transpiler when "use strict" directive is active.
// Used by js_property_set to throw TypeError instead of silently rejecting writes.
bool js_strict_mode = false;

extern "C" void js_set_strict_mode(int64_t strict) {
    js_strict_mode = (strict != 0);
}

// v24: throw TypeError in strict mode for property write violations
static void js_strict_throw_property_error(const char* reason, const char* prop_name, int prop_len) {
    if (!js_strict_mode) return;
    char msg[512];
    if (prop_name && prop_len > 0) {
        snprintf(msg, sizeof(msg), "Cannot %s property '%.*s' of object", reason, prop_len > 200 ? 200 : prop_len, prop_name);
    } else {
        snprintf(msg, sizeof(msg), "Cannot %s property of object", reason);
    }
    Item err_name = (Item){.item = s2it(heap_create_name("TypeError"))};
    Item err_msg = (Item){.item = s2it(heap_create_name(msg))};
    Item error = js_new_error_with_name(err_name, err_msg);
    js_throw_value(error);
}

// Forward declaration for _map_read_field (defined in lambda-data-runtime.cpp)
Item _map_read_field(ShapeEntry* field, void* map_data);
// Forward declaration for _map_get (used as fallback for nested/spread maps)
Item _map_get(TypeMap* map_type, void* map_data, char *key, bool *is_found);
// Forward declaration for js_map_get_fast (defined later in this file)
static Item js_map_get_fast(Map* m, const char* key_str, int key_len, bool* out_found);

// Global 'this' binding for the current method call
static Item js_current_this = {0};

// new.target: set to the constructor function when called via 'new', undefined otherwise.
// Uses a pending pattern: js_set_new_target sets a pending value that js_call_function
// picks up on entry. Regular calls see new.target as undefined ({0}).
static Item js_new_target = {0};
static Item js_pending_new_target = {0};
static bool js_has_pending_new_target = false;

// Pending arguments for building 'arguments' object inside JIT-compiled functions.
// Set by js_invoke_fn before each call, read by js_build_arguments_object().
static Item* js_pending_call_args = NULL;
static int js_pending_call_argc = 0;

// Module-level variable table for top-level bindings accessible from any function.
// Populated during js_main execution, read by class method closures.
#define JS_MAX_MODULE_VARS 2048
static Item js_module_vars[JS_MAX_MODULE_VARS];
static int js_module_var_count = 0;

// v18m: Original 'this' for Array.prototype.call() on non-array objects.
// When filter/forEach/every/map/etc. are called on a MAP via .call(),
// we convert to array-like internally but callbacks should get the original object.
static Item js_array_method_real_this = {0};

// Helper to make JS undefined value
static inline Item make_js_undefined() {
    return (Item){.item = ((uint64_t)LMD_TYPE_UNDEFINED << 56)};
}

// Sentinel value for deleted properties. Uses a unique int encoding that
// won't collide with real values. Property accessors (Object.keys, in, hasOwnProperty)
// skip entries where the value equals this sentinel.
static inline Item make_js_deleted_sentinel() {
    return (Item){.item = JS_DELETED_SENTINEL_VAL};
}
static inline bool js_is_deleted_sentinel(Item val) {
    return val.item == JS_DELETED_SENTINEL_VAL;
}

// Symbol property key helpers: Symbols are encoded as negative ints (<= -JS_SYMBOL_BASE).
// Convert symbol items to unique string keys for property storage.
static inline bool js_key_is_symbol(Item key) {
    if (get_type_id(key) != LMD_TYPE_INT) return false;
    return it2i(key) <= -(int64_t)JS_SYMBOL_BASE;
}

static inline Item js_symbol_to_key(Item sym) {
    int64_t id = -(it2i(sym) + (int64_t)JS_SYMBOL_BASE);
    char buf[32];
    snprintf(buf, sizeof(buf), "__sym_%lld", (long long)id);
    return (Item){.item = s2it(heap_create_name(buf, strlen(buf)))};    
}

// Convert any key (string or symbol) to a getter key (__get_<key_string>)
extern "C" Item js_make_getter_key(Item key) {
    // convert symbol to string key first
    if (js_key_is_symbol(key)) key = js_symbol_to_key(key);
    // convert numeric keys to string (JS coerces property keys to strings)
    if (get_type_id(key) == LMD_TYPE_INT || get_type_id(key) == LMD_TYPE_FLOAT) {
        char nbuf[64];
        if (get_type_id(key) == LMD_TYPE_INT) snprintf(nbuf, sizeof(nbuf), "%lld", (long long)it2i(key));
        else snprintf(nbuf, sizeof(nbuf), "%g", it2d(key));
        key = (Item){.item = s2it(heap_create_name(nbuf, strlen(nbuf)))};
    }
    if (get_type_id(key) == LMD_TYPE_STRING) {
        String* s = it2s(key);
        char buf[256];
        snprintf(buf, sizeof(buf), "__get_%.*s", (int)s->len, s->chars);
        return (Item){.item = s2it(heap_create_name(buf, strlen(buf)))};
    }
    return key;
}

// Convert any key (string or symbol) to a setter key (__set_<key_string>)
extern "C" Item js_make_setter_key(Item key) {
    if (js_key_is_symbol(key)) key = js_symbol_to_key(key);
    // convert numeric keys to string (JS coerces property keys to strings)
    if (get_type_id(key) == LMD_TYPE_INT || get_type_id(key) == LMD_TYPE_FLOAT) {
        char nbuf[64];
        if (get_type_id(key) == LMD_TYPE_INT) snprintf(nbuf, sizeof(nbuf), "%lld", (long long)it2i(key));
        else snprintf(nbuf, sizeof(nbuf), "%g", it2d(key));
        key = (Item){.item = s2it(heap_create_name(nbuf, strlen(nbuf)))};
    }
    if (get_type_id(key) == LMD_TYPE_STRING) {
        String* s = it2s(key);
        char buf[256];
        snprintf(buf, sizeof(buf), "__set_%.*s", (int)s->len, s->chars);
        return (Item){.item = s2it(heap_create_name(buf, strlen(buf)))};
    }
    return key;
}

extern "C" void js_set_module_var(int index, Item value) {
    if (index >= 0 && index < JS_MAX_MODULE_VARS) {
        js_module_vars[index] = value;
    }
}

extern "C" Item js_get_module_var(int index) {
    if (index >= 0 && index < JS_MAX_MODULE_VARS) {
        return js_module_vars[index];
    }
    return ItemNull;
}

extern "C" void js_reset_module_vars() {
    memset(js_module_vars, 0, sizeof(js_module_vars));
    js_module_var_count = 0;
    // Register module vars as GC root range so class objects and their
    // prototypes are traceable by the garbage collector.
    static bool module_vars_rooted = false;
    if (!module_vars_rooted) {
        heap_register_gc_root_range((uint64_t*)js_module_vars, JS_MAX_MODULE_VARS);
        module_vars_rooted = true;
    }
}

// =============================================================================
// Exception Handling State
// =============================================================================

static bool js_exception_pending = false;
static Item js_exception_value = {0};
static char js_exception_msg_buf[1024] = {0};

// Throw TypeError if value is null or undefined (ES spec RequireObjectCoercible)
extern "C" void js_require_object_coercible(Item value) {
    TypeId type = get_type_id(value);
    if (type == LMD_TYPE_NULL || type == LMD_TYPE_UNDEFINED) {
        const char* type_str = (type == LMD_TYPE_NULL) ? "null" : "undefined";
        char msg[256];
        snprintf(msg, sizeof(msg), "Cannot destructure '%s' as it is %s.", type_str, type_str);
        Item err_name = (Item){.item = s2it(heap_create_name("TypeError"))};
        Item err_msg = (Item){.item = s2it(heap_create_name(msg))};
        Item error = js_new_error_with_name(err_name, err_msg);
        js_throw_value(error);
    }
}

extern "C" void js_throw_value(Item value) {
    js_exception_pending = true;
    js_exception_value = value;
    log_debug("js: throw_value called, exception pending");
    // Capture exception message into static buffer while context is alive
    js_exception_msg_buf[0] = '\0';
    if (get_type_id(value) == LMD_TYPE_MAP) {
        Item name_key = (Item){.item = s2it(heap_create_name("name"))};
        Item msg_key = (Item){.item = s2it(heap_create_name("message"))};
        Item name_val = js_property_get(value, name_key);
        Item msg_val = js_property_get(value, msg_key);
        const char* nstr = "Error"; int nlen = 5;
        if (get_type_id(name_val) == LMD_TYPE_STRING) {
            String* ns = it2s(name_val);
            nstr = ns->chars; nlen = ns->len;
        }
        if (get_type_id(msg_val) == LMD_TYPE_STRING) {
            String* ms = it2s(msg_val);
            snprintf(js_exception_msg_buf, sizeof(js_exception_msg_buf),
                     "%.*s: %.*s", nlen, nstr, ms->len, ms->chars);
        } else {
            snprintf(js_exception_msg_buf, sizeof(js_exception_msg_buf),
                     "%.*s", nlen, nstr);
        }
    } else if (get_type_id(value) == LMD_TYPE_STRING) {
        String* s = it2s(value);
        snprintf(js_exception_msg_buf, sizeof(js_exception_msg_buf),
                 "%.*s", s->len, s->chars);
    }
}

extern "C" const char* js_get_exception_message(void) {
    return js_exception_msg_buf;
}

extern "C" int js_check_exception(void) {
    return js_exception_pending ? 1 : 0;
}

extern "C" Item js_clear_exception(void) {
    js_exception_pending = false;
    Item val = js_exception_value;
    js_exception_value = ItemNull;
    return val;
}

// TDZ check: throw ReferenceError if variable is still in Temporal Dead Zone
extern "C" void js_check_tdz(Item value, const char* name, int name_len) {
    if (value.item == ITEM_JS_TDZ) {
        char buf[256];
        int len = snprintf(buf, sizeof(buf), "Cannot access '%.*s' before initialization", name_len, name);
        Item tn = (Item){.item = s2it(heap_create_name("ReferenceError", 14))};
        Item msg = (Item){.item = s2it(heap_create_name(buf, len))};
        js_throw_value(js_new_error_with_name(tn, msg));
    }
}

// forward declaration for js_batch_reset (defined near js_module_count_v14)
static void js_module_cache_reset();

extern "C" void js_batch_reset() {
    // reset module variable table
    js_reset_module_vars();
    // clear module registry (cached namespace_obj / mir_ctx are invalid after heap reset)
    module_registry_cleanup();
    // clear JS module cache (specifier String* pointers become dangling after heap reset)
    js_module_cache_reset();
    // clear any pending exception from previous script
    js_exception_pending = false;
    js_exception_value = (Item){0};
    // clear current this binding
    js_current_this = (Item){0};
    // clear new.target
    js_new_target = (Item){0};
    js_pending_new_target = (Item){0};
    js_has_pending_new_target = false;
    // clear array method real this
    js_array_method_real_this = (Item){0};
    // clear Input context
    js_input = NULL;
    // reset cached global objects (Math, JSON, console, Reflect) so they're recreated fresh
    // — tests may modify them (delete/overwrite properties)
    extern void js_reset_math_object();
    js_reset_math_object();
    extern void js_reset_json_object();
    js_reset_json_object();
    extern void js_reset_console_object();
    js_reset_console_object();
    extern void js_reset_reflect_object();
    js_reset_reflect_object();
    // reset interned __proto__ key (allocated in old pool)
    extern void js_reset_proto_key();
    js_reset_proto_key();
    // reset function pointer → JsFunction cache (JsFunction* in old pool)
    extern void js_func_cache_reset();
    js_func_cache_reset();
    // reset builtin function cache (defined later in file, called via forward decl)
    extern void js_builtin_cache_reset();
    js_builtin_cache_reset();
    // deep reset: generators, promises, async contexts, pending calls
    extern void js_deep_batch_reset();
    js_deep_batch_reset();
}

// Get current module var count (for checkpointing)
extern "C" int js_get_module_var_count() {
    return js_module_var_count;
}

// Partial batch reset: restore module vars to a checkpoint and clear test state,
// but leave heap and cached builtins intact.  Used by js-test-batch preamble mode
// to avoid re-initializing the harness between tests.
extern "C" void js_batch_reset_to(int checkpoint_var_count) {
    // zero out module vars beyond the checkpoint
    for (int i = checkpoint_var_count; i < JS_MAX_MODULE_VARS; i++) {
        js_module_vars[i] = (Item){0};
    }
    js_module_var_count = checkpoint_var_count;
    // clear module registry (frees strdup/calloc per registered module)
    module_registry_cleanup();
    // clear JS module cache counter
    js_module_cache_reset();
    // clear pending exception
    js_exception_pending = false;
    js_exception_value = (Item){0};
    // clear this/new.target
    js_current_this = (Item){0};
    js_new_target = (Item){0};
    js_pending_new_target = (Item){0};
    js_has_pending_new_target = false;
    js_array_method_real_this = (Item){0};
    // clear Input context (recreated per script by transpile_js_to_mir)
    js_input = NULL;
    // reset cached global objects — tests may modify them
    extern void js_reset_math_object();
    js_reset_math_object();
    extern void js_reset_json_object();
    js_reset_json_object();
    extern void js_reset_console_object();
    js_reset_console_object();
    extern void js_reset_reflect_object();
    js_reset_reflect_object();
    // reset interned __proto__ key
    extern void js_reset_proto_key();
    js_reset_proto_key();
    // reset function pointer → JsFunction cache
    extern void js_func_cache_reset();
    js_func_cache_reset();
    extern void js_builtin_cache_reset();
    js_builtin_cache_reset();
    // deep reset: generators, promises, async contexts, pending calls
    extern void js_deep_batch_reset();
    js_deep_batch_reset();
}

extern "C" Item js_new_error(Item message) {
    return js_new_error_with_stack(message, (Item){.item = ITEM_JS_UNDEFINED});
}

// AggregateError(errors, message): Error subclass with .errors array
extern "C" Item js_new_aggregate_error(Item errors, Item message) {
    Item err_name = (Item){.item = s2it(heap_create_name("AggregateError", 14))};
    Item err = js_new_error_with_name(err_name, message);
    // Convert errors iterable to array — use js_array_from for iterable conversion
    Item errors_arr = js_array_from(errors);
    js_property_set(err, (Item){.item = s2it(heap_create_name("errors", 6))}, errors_arr);
    return err;
}

// v12: Create Error with a compile-time stack trace string
extern "C" Item js_new_error_with_stack(Item message, Item stack_str) {
    Item obj = js_new_object();
    Item name_val = (Item){.item = s2it(heap_create_name("Error"))};
    // Per spec: 'name' is inherited from Error.prototype, NOT set as own property
    // Only set 'message' when it is explicitly provided (not undefined/null)
    Item msg_key = (Item){.item = s2it(heap_create_name("message"))};
    if (message.item != ItemNull.item && message.item != ITEM_JS_UNDEFINED &&
        get_type_id(message) != LMD_TYPE_NULL && get_type_id(message) != LMD_TYPE_UNDEFINED) {
        if (get_type_id(message) != LMD_TYPE_STRING) {
            Item str_msg = js_to_string(message);
            js_property_set(obj, msg_key, str_msg);
        } else {
            js_property_set(obj, msg_key, message);
        }
    }
    // Set stack property from compile-time stack trace
    Item stack_key = (Item){.item = s2it(heap_create_name("stack"))};
    if (stack_str.item != ITEM_JS_UNDEFINED) {
        js_property_set(obj, stack_key, stack_str);
    } else {
        // Default: "Error" + message
        const char* msg_str = "";
        int msg_len = 0;
        if (get_type_id(message) == LMD_TYPE_STRING) {
            String* ms = it2s(message);
            msg_str = ms->chars;
            msg_len = ms->len;
        }
        char buf[512];
        int len;
        if (msg_len > 0) {
            len = snprintf(buf, sizeof(buf), "Error: %.*s", msg_len, msg_str);
        } else {
            len = snprintf(buf, sizeof(buf), "Error");
        }
        js_property_set(obj, stack_key, (Item){.item = s2it(heap_create_name(buf, len))});
    }
    // Set __class_name__ for instanceof support
    Item cn_key = (Item){.item = s2it(heap_create_name("__class_name__", 14))};
    js_property_set(obj, cn_key, name_val);
    // Set __proto__ to Error.prototype so prototype methods (toString) are found
    {
        Item ctor_fn = js_get_constructor(name_val);
        if (ctor_fn.item != ITEM_JS_UNDEFINED && get_type_id(ctor_fn) == LMD_TYPE_FUNC) {
            Item proto_key = (Item){.item = s2it(heap_create_name("prototype", 9))};
            Item proto = js_property_get(ctor_fn, proto_key);
            if (proto.item != ItemNull.item && get_type_id(proto) == LMD_TYPE_MAP) {
                js_set_prototype(obj, proto);
            }
        }
    }
    return obj;
}

// v11: Create a typed Error (TypeError, RangeError, SyntaxError, ReferenceError)
extern "C" Item js_new_error_with_name(Item error_name, Item message) {
    return js_new_error_with_name_stack(error_name, message, (Item){.item = ITEM_JS_UNDEFINED});
}

// v12: Create typed Error with compile-time stack trace
extern "C" Item js_new_error_with_name_stack(Item error_name, Item message, Item stack_str) {
    Item obj = js_new_object();
    // Per spec: 'name' is inherited from the prototype, NOT set as own property
    // Only set 'message' when it is explicitly provided (not undefined/null)
    Item msg_key = (Item){.item = s2it(heap_create_name("message"))};
    if (message.item != ItemNull.item && message.item != ITEM_JS_UNDEFINED &&
        get_type_id(message) != LMD_TYPE_NULL && get_type_id(message) != LMD_TYPE_UNDEFINED) {
        if (get_type_id(message) != LMD_TYPE_STRING) {
            Item str_msg = js_to_string(message);
            js_property_set(obj, msg_key, str_msg);
        } else {
            js_property_set(obj, msg_key, message);
        }
    }
    // Set stack property
    Item stack_key = (Item){.item = s2it(heap_create_name("stack"))};
    if (stack_str.item != ITEM_JS_UNDEFINED) {
        js_property_set(obj, stack_key, stack_str);
    } else {
        const char* name_str = "Error";
        int name_len = 5;
        if (get_type_id(error_name) == LMD_TYPE_STRING) {
            String* ns = it2s(error_name);
            name_str = ns->chars;
            name_len = ns->len;
        }
        const char* msg_str = "";
        int msg_len = 0;
        if (get_type_id(message) == LMD_TYPE_STRING) {
            String* ms = it2s(message);
            msg_str = ms->chars;
            msg_len = ms->len;
        }
        char buf[512];
        int len;
        if (msg_len > 0) {
            len = snprintf(buf, sizeof(buf), "%.*s: %.*s", name_len, name_str, msg_len, msg_str);
        } else {
            len = snprintf(buf, sizeof(buf), "%.*s", name_len, name_str);
        }
        js_property_set(obj, stack_key, (Item){.item = s2it(heap_create_name(buf, len))});
    }
    // Set __class_name__ for instanceof support
    Item cn_key = (Item){.item = s2it(heap_create_name("__class_name__", 14))};
    js_property_set(obj, cn_key, error_name);
    // v18c: Set .constructor for assert.throws / constructor identity checks
    Item ctor_fn = js_get_constructor(error_name);
    if (ctor_fn.item != ITEM_JS_UNDEFINED && get_type_id(ctor_fn) == LMD_TYPE_FUNC) {
        Item ctor_key = (Item){.item = s2it(heap_create_name("constructor"))};
        js_property_set(obj, ctor_key, ctor_fn);
        // Mark constructor as non-enumerable
        Item ne_ctor = (Item){.item = s2it(heap_create_name("__ne_constructor", 16))};
        js_property_set(obj, ne_ctor, (Item){.item = b2it(true)});
        // Set __proto__ to ErrorType.prototype so prototype methods (toString) are found
        Item proto_key = (Item){.item = s2it(heap_create_name("prototype", 9))};
        Item proto = js_property_get(ctor_fn, proto_key);
        if (proto.item != ItemNull.item && get_type_id(proto) == LMD_TYPE_MAP) {
            js_set_prototype(obj, proto);
        }
    }
    // Mark stack as non-enumerable (per ES spec)
    Item ne_stack = (Item){.item = s2it(heap_create_name("__ne_stack", 10))};
    js_property_set(obj, ne_stack, (Item){.item = b2it(true)});
    return obj;
}

// ES2022: Extract cause from options object and set on error
extern "C" Item js_error_set_cause(Item error, Item options) {
    if (get_type_id(options) != LMD_TYPE_MAP) return error;
    Item cause_key = (Item){.item = s2it(heap_create_name("cause"))};
    bool found = false;
    Item cause_val = js_map_get_fast(options.map, "cause", 5, &found);
    if (found) {
        js_property_set(error, cause_key, cause_val);
    }
    return error;
}

extern "C" void js_runtime_set_input(void* input) {
    js_input = (Input*)input;
    // Register static Item variables as GC roots on the CURRENT heap so their
    // referenced objects are not collected.  Must re-register on each new heap.
    heap_register_gc_root(&js_current_this.item);
    heap_register_gc_root(&js_new_target.item);
    heap_register_gc_root(&js_pending_new_target.item);
    heap_register_gc_root(&js_exception_value.item);
}

extern "C" Item js_get_this() {
    // Sloppy mode coercion: when this is null-like, return globalThis.
    // Three null representations: {0} (initial/RAW_POINTER), ITEM_NULL (LMD_TYPE_NULL<<56)
    // Both mean "no explicit this set" and should map to globalThis in sloppy mode.
    // Strict mode bare calls use ITEM_JS_UNDEFINED which is NOT coerced.
    if (js_current_this.item == 0 || js_current_this.item == ITEM_NULL) {
        extern Item js_get_global_this();
        return js_get_global_this();
    }
    return js_current_this;
}

extern "C" void js_set_this(Item this_val) {
    js_current_this = this_val;
}

extern "C" Item js_get_new_target() {
    return js_new_target;
}

extern "C" void js_set_new_target(Item target) {
    // Set as pending — will be picked up by js_call_function on entry
    js_pending_new_target = target;
    js_has_pending_new_target = true;
}

extern "C" void js_set_direct_new_target(Item target) {
    // Directly set new.target (for direct calls that bypass js_call_function)
    js_new_target = target;
}

// Build the 'arguments' array-like object from the pending call args.
// Called at the top of JIT-compiled functions that reference 'arguments'.
extern "C" Item js_build_arguments_object() {
    int argc = js_pending_call_argc;
    Item* args = js_pending_call_args;
    Item arr = js_array_new(0);
    for (int i = 0; i < argc; i++) {
        js_array_push(arr, args ? args[i] : ItemNull);
    }
    // Set .length explicitly (js_array_new + push should handle this, but be safe)
    // Also set callee to undefined (strict mode compatible)
    return arr;
}

extern TypeMap EmptyMap;

// =============================================================================
// Type Conversion Functions
// =============================================================================

extern "C" Item js_to_number(Item value) {
    TypeId type = get_type_id(value);

    switch (type) {
    case LMD_TYPE_NULL:
    case LMD_TYPE_UNDEFINED:
        // null -> 0, undefined -> NaN
        if (type == LMD_TYPE_UNDEFINED) {
            double* nan_ptr = (double*)heap_alloc(sizeof(double), LMD_TYPE_FLOAT);
            *nan_ptr = NAN;
            return (Item){.item = d2it(nan_ptr)};
        }
        return (Item){.item = i2it(0)};

    case LMD_TYPE_BOOL: {
        int val = it2b(value) ? 1 : 0;
        return (Item){.item = i2it(val)};
    }

    case LMD_TYPE_INT:
        // Already a number (int), convert to float for consistency
        return value;

    case LMD_TYPE_FLOAT:
        return value;

    case LMD_TYPE_STRING: {
        String* str = it2s(value);
        if (!str || str->len == 0) {
            return (Item){.item = i2it(0)};  // Empty string -> 0
        }
        // v20: Trim whitespace before parsing (ES spec: WhiteSpace + LineTerminator)
        const char* start = str->chars;
        const char* end = str->chars + str->len;
        while (start < end && (*start == ' ' || *start == '\t' || *start == '\n' ||
               *start == '\r' || *start == '\f' || *start == '\v' ||
               *start == (char)0xA0)) start++;
        while (end > start && (*(end-1) == ' ' || *(end-1) == '\t' || *(end-1) == '\n' ||
               *(end-1) == '\r' || *(end-1) == '\f' || *(end-1) == '\v' ||
               *(end-1) == (char)0xA0)) end--;
        if (start == end) {
            return (Item){.item = i2it(0)};  // Whitespace-only string -> 0
        }
        // Copy trimmed portion to null-terminated buffer for parsing
        int trimmed_len = (int)(end - start);
        char buf[128];
        if (trimmed_len < (int)sizeof(buf)) {
            memcpy(buf, start, trimmed_len);
            buf[trimmed_len] = '\0';
        } else {
            // Fallback for very long strings
            memcpy(buf, start, sizeof(buf) - 1);
            buf[sizeof(buf) - 1] = '\0';
        }
        // v20: Handle binary (0b/0B) and octal (0o/0O) literals
        if (trimmed_len > 2 && buf[0] == '0' && (buf[1] == 'b' || buf[1] == 'B')) {
            char* bp = buf + 2;
            long long val = 0;
            while (*bp == '0' || *bp == '1') { val = val * 2 + (*bp - '0'); bp++; }
            if (*bp != '\0' || bp == buf + 2) {
                double* nan_ptr = (double*)heap_alloc(sizeof(double), LMD_TYPE_FLOAT);
                *nan_ptr = NAN;
                return (Item){.item = d2it(nan_ptr)};
            }
            double* result = (double*)heap_alloc(sizeof(double), LMD_TYPE_FLOAT);
            *result = (double)val;
            return (Item){.item = d2it(result)};
        }
        if (trimmed_len > 2 && buf[0] == '0' && (buf[1] == 'o' || buf[1] == 'O')) {
            char* op = buf + 2;
            long long val = 0;
            while (*op >= '0' && *op <= '7') { val = val * 8 + (*op - '0'); op++; }
            if (*op != '\0' || op == buf + 2) {
                double* nan_ptr = (double*)heap_alloc(sizeof(double), LMD_TYPE_FLOAT);
                *nan_ptr = NAN;
                return (Item){.item = d2it(nan_ptr)};
            }
            double* result = (double*)heap_alloc(sizeof(double), LMD_TYPE_FLOAT);
            *result = (double)val;
            return (Item){.item = d2it(result)};
        }
        // v29: Handle hex (0x/0X) literals explicitly — ES spec does not allow
        // a sign prefix on hex literals. strtod() on some platforms (macOS) accepts
        // "+0x10" which violates the spec. Parse hex ourselves.
        if (trimmed_len > 2 && buf[0] == '0' && (buf[1] == 'x' || buf[1] == 'X')) {
            char* hp = buf + 2;
            long long val = 0;
            bool has_digits = false;
            while ((*hp >= '0' && *hp <= '9') || (*hp >= 'a' && *hp <= 'f') || (*hp >= 'A' && *hp <= 'F')) {
                int d;
                if (*hp >= '0' && *hp <= '9') d = *hp - '0';
                else if (*hp >= 'a' && *hp <= 'f') d = *hp - 'a' + 10;
                else d = *hp - 'A' + 10;
                val = val * 16 + d;
                has_digits = true;
                hp++;
            }
            if (*hp != '\0' || !has_digits) {
                double* nan_ptr = (double*)heap_alloc(sizeof(double), LMD_TYPE_FLOAT);
                *nan_ptr = NAN;
                return (Item){.item = d2it(nan_ptr)};
            }
            double* result = (double*)heap_alloc(sizeof(double), LMD_TYPE_FLOAT);
            *result = (double)val;
            return (Item){.item = d2it(result)};
        }
        // v29: Reject signed hex/octal/binary — strtod might accept "+0x..." on some platforms
        if (trimmed_len > 3 && (buf[0] == '+' || buf[0] == '-') && buf[1] == '0' &&
            (buf[2] == 'x' || buf[2] == 'X' || buf[2] == 'b' || buf[2] == 'B' ||
             buf[2] == 'o' || buf[2] == 'O')) {
            double* nan_ptr = (double*)heap_alloc(sizeof(double), LMD_TYPE_FLOAT);
            *nan_ptr = NAN;
            return (Item){.item = d2it(nan_ptr)};
        }
        char* endptr;
        double num = strtod(buf, &endptr);
        // Check that ALL trimmed characters were consumed
        if (endptr == buf || *endptr != '\0') {
            // Not a valid number — NaN
            double* nan_ptr = (double*)heap_alloc(sizeof(double), LMD_TYPE_FLOAT);
            *nan_ptr = NAN;
            return (Item){.item = d2it(nan_ptr)};
        }
        double* result = (double*)heap_alloc(sizeof(double), LMD_TYPE_FLOAT);
        *result = num;
        return (Item){.item = d2it(result)};
    }

    default:
        // v16: Objects/arrays — check for Symbol.toPrimitive before returning NaN
        if (type == LMD_TYPE_MAP) {
            // Fast path: boxed primitives with __primitiveValue__
            bool pv_found = false;
            Item pv = js_map_get_fast(value.map, "__primitiveValue__", 18, &pv_found);
            if (pv_found && pv.item != ItemNull.item) {
                return js_to_number(pv);
            }
            Item sym_key = (Item){.item = s2it(heap_create_name("__sym_2", 7))};
            Item to_prim = js_property_get(value, sym_key);
            if (to_prim.item != ItemNull.item && get_type_id(to_prim) == LMD_TYPE_FUNC) {
                Item hint = (Item){.item = s2it(heap_create_name("number", 6))};
                Item args[1] = { hint };
                Item result = js_call_function(to_prim, value, args, 1);
                return js_to_number(result);
            }
            // try valueOf()
            bool found = false;
            Item valueOf_fn = js_map_get_fast(value.map, "valueOf", 7, &found);
            if (!found) valueOf_fn = js_prototype_lookup(value, (Item){.item = s2it(heap_create_name("valueOf", 7))});
            if (valueOf_fn.item != ItemNull.item && get_type_id(valueOf_fn) == LMD_TYPE_FUNC) {
                Item result = js_call_function(valueOf_fn, value, NULL, 0);
                TypeId rt = get_type_id(result);
                if (rt != LMD_TYPE_MAP && rt != LMD_TYPE_ARRAY) {
                    return js_to_number(result);
                }
            }
        }
        // Arrays: ToPrimitive → toString → ToNumber (e.g. +[] → +"" → 0, +[1] → +"1" → 1)
        if (type == LMD_TYPE_ARRAY) {
            Item str = js_to_string(value);
            return js_to_number(str);
        }
        // Objects, arrays, etc. -> NaN
        double* nan_ptr = (double*)heap_alloc(sizeof(double), LMD_TYPE_FLOAT);
        *nan_ptr = NAN;
        return (Item){.item = d2it(nan_ptr)};
    }
}

// ES spec §7.1.12.1 Number::toString
// Converts a double to its JavaScript string representation.
// - Uses shortest representation that round-trips
// - No scientific notation for exponents in [-6, 20]
// - Scientific notation uses 'e+' or 'e-' (no leading zeros in exponent)
static void js_double_to_string(double d, char* out, int out_size) {
    // Handle negative numbers
    int neg = 0;
    if (d < 0) { neg = 1; d = -d; }

    // Try increasing precision to find shortest round-trip representation
    char buf[64];
    int best_len = 0;
    for (int prec = 1; prec <= 21; prec++) {
        snprintf(buf, sizeof(buf), "%.*e", prec - 1, d);
        double roundtrip;
        sscanf(buf, "%lf", &roundtrip);
        if (roundtrip == d) {
            best_len = prec;
            break;
        }
    }
    if (best_len == 0) best_len = 17; // fallback: 17 digits always round-trips

    // Format with the minimal precision in scientific notation
    snprintf(buf, sizeof(buf), "%.*e", best_len - 1, d);

    // Parse the scientific notation: digits, decimal point, exponent
    // Format from snprintf: [-]d.dddde[+-]dd
    char digits[32];
    int digit_count = 0;
    int exp_val = 0;

    char* p = buf;
    // Skip sign (we handle separately)
    if (*p == '-') p++;

    // Collect digits (skip decimal point)
    while (*p && *p != 'e' && *p != 'E') {
        if (*p != '.') {
            digits[digit_count++] = *p;
        }
        p++;
    }
    digits[digit_count] = '\0';

    // Remove trailing zeros from digit string
    while (digit_count > 1 && digits[digit_count - 1] == '0') {
        digit_count--;
        digits[digit_count] = '\0';
    }

    // Parse exponent
    if (*p == 'e' || *p == 'E') {
        p++;
        exp_val = atoi(p);
    }

    // n = number of significant digits (k in ES spec)
    int k = digit_count;
    // e = exponent such that value = 0.digits * 10^e  =>  e = exp_val + 1
    int e = exp_val + 1;

    // Now format according to ES spec §7.1.12.1
    char* o = out;
    if (neg) *o++ = '-';

    if (k <= e && e <= 21) {
        // Case: integer-like, e.g. 120, 1000000
        // digits followed by (e-k) zeros
        memcpy(o, digits, k);
        o += k;
        for (int i = 0; i < e - k; i++) *o++ = '0';
        *o = '\0';
    } else if (0 < e && e <= 21) {
        // Case: decimal point within digits, e.g. 1.5, 12.34
        // first e digits, then '.', then remaining digits
        memcpy(o, digits, e);
        o += e;
        *o++ = '.';
        memcpy(o, digits + e, k - e);
        o += (k - e);
        *o = '\0';
    } else if (-6 < e && e <= 0) {
        // Case: 0.00...0digits, e.g. 0.5, 0.001
        *o++ = '0';
        *o++ = '.';
        for (int i = 0; i < -e; i++) *o++ = '0';
        memcpy(o, digits, k);
        o += k;
        *o = '\0';
    } else if (k == 1) {
        // Scientific notation with single digit
        *o++ = digits[0];
        *o++ = 'e';
        if (e - 1 >= 0) *o++ = '+';
        snprintf(o, out_size - (int)(o - out), "%d", e - 1);
    } else {
        // Scientific notation with multiple digits
        *o++ = digits[0];
        *o++ = '.';
        memcpy(o, digits + 1, k - 1);
        o += (k - 1);
        *o++ = 'e';
        if (e - 1 >= 0) *o++ = '+';
        snprintf(o, out_size - (int)(o - out), "%d", e - 1);
    }
}

extern "C" Item js_to_string(Item value) {
    TypeId type = get_type_id(value);

    switch (type) {
    case LMD_TYPE_NULL:
        return (Item){.item = s2it(heap_create_name("null"))};

    case LMD_TYPE_UNDEFINED:
        return (Item){.item = s2it(heap_create_name("undefined"))};

    case LMD_TYPE_BOOL:
        return (Item){.item = s2it(heap_create_name(it2b(value) ? "true" : "false"))};

    case LMD_TYPE_INT: {
        int64_t v = it2i(value);
        // Symbols cannot be implicitly converted to string (ES spec 7.1.12)
        if (v <= -(int64_t)JS_SYMBOL_BASE) {
            js_throw_type_error("Cannot convert a Symbol value to a string");
            return ItemNull;
        }
        char buffer[32];
        snprintf(buffer, sizeof(buffer), "%lld", (long long)v);
        return (Item){.item = s2it(heap_create_name(buffer))};
    }

    case LMD_TYPE_FLOAT: {
        double d = it2d(value);
        if (isnan(d)) {
            return (Item){.item = s2it(heap_create_name("NaN"))};
        } else if (isinf(d)) {
            return (Item){.item = s2it(heap_create_name(d > 0 ? "Infinity" : "-Infinity"))};
        } else if (d == 0.0) {
            // ES spec: Number::toString(-0) and +0 both return "0"
            return (Item){.item = s2it(heap_create_name("0"))};
        } else {
            char buffer[64];
            js_double_to_string(d, buffer, sizeof(buffer));
            return (Item){.item = s2it(heap_create_name(buffer))};
        }
    }

    case LMD_TYPE_STRING:
        return value;

    case LMD_TYPE_ARRAY: {
        // JS: String([1,2,3]) => "1,2,3" (same as Array.prototype.join(","))
        Array* a = value.array;
        if (!a || a->length == 0) {
            return (Item){.item = s2it(heap_create_name(""))};
        }
        StrBuf* sb = strbuf_new();
        for (int i = 0; i < a->length; i++) {
            if (i > 0) strbuf_append_str_n(sb, ",", 1);
            TypeId etype = get_type_id(a->items[i]);
            if (etype != LMD_TYPE_NULL && etype != LMD_TYPE_UNDEFINED && a->items[i].item != JS_DELETED_SENTINEL_VAL) {
                Item elem_str = js_to_string(a->items[i]);
                String* s = it2s(elem_str);
                if (s && s->len > 0) {
                    strbuf_append_str_n(sb, s->chars, (int)s->len);
                }
            }
        }
        String* result = heap_create_name(sb->str, sb->length);
        strbuf_free(sb);
        return (Item){.item = s2it(result)};
    }

    case LMD_TYPE_MAP: {
        // v16: Check for Symbol.toPrimitive first (prototype chain lookup)
        {
            Item sym_key = (Item){.item = s2it(heap_create_name("__sym_2", 7))};
            Item to_prim = js_property_get(value, sym_key);
            if (to_prim.item != ItemNull.item && get_type_id(to_prim) == LMD_TYPE_FUNC) {
                Item hint = (Item){.item = s2it(heap_create_name("string", 6))};
                Item args[1] = { hint };
                Item result = js_call_function(to_prim, value, args, 1);
                if (get_type_id(result) == LMD_TYPE_STRING) return result;
                return js_to_string(result);
            }
        }
        // Check for Date objects (have __class_name__ == "Date")
        bool own_cls = false;
        Item cls_val = js_map_get_fast(value.map, "__class_name__", 14, &own_cls);
        if (own_cls && get_type_id(cls_val) == LMD_TYPE_STRING) {
            String* cls_s = it2s(cls_val);
            if (cls_s && cls_s->len == 4 && strncmp(cls_s->chars, "Date", 4) == 0) {
                // delegate to js_date_method(obj, 17=toString)
                return js_date_method(value, 17);
            }
        }
        // Wrapper objects with __primitiveValue__ (e.g. new Number(42), new String("hi"))
        {
            bool own_pv = false;
            Item pv = js_map_get_fast(value.map, "__primitiveValue__", 18, &own_pv);
            if (own_pv) return js_to_string(pv);
        }
        // Check for regex objects (have __rd hidden property)
        // JS: String(/pattern/flags) => "/pattern/flags"
        {
            bool own_rd = false;
            js_map_get_fast(value.map, "__rd", 4, &own_rd);
            if (own_rd) {
                bool own_src = false, own_flags = false;
                Item src_val = js_map_get_fast(value.map, "source", 6, &own_src);
                Item flags_val = js_map_get_fast(value.map, "flags", 5, &own_flags);
                String* src_s = (own_src && get_type_id(src_val) == LMD_TYPE_STRING) ? it2s(src_val) : NULL;
                String* flags_s = (own_flags && get_type_id(flags_val) == LMD_TYPE_STRING) ? it2s(flags_val) : NULL;
                StrBuf* sb = strbuf_new();
                strbuf_append_str_n(sb, "/", 1);
                if (src_s && src_s->len > 0) strbuf_append_str_n(sb, src_s->chars, (int)src_s->len);
                strbuf_append_str_n(sb, "/", 1);
                if (flags_s && flags_s->len > 0) strbuf_append_str_n(sb, flags_s->chars, (int)flags_s->len);
                String* result = heap_create_name(sb->str, sb->length);
                strbuf_free(sb);
                return (Item){.item = s2it(result)};
            }
        }
        // Check for custom toString() method in own properties or prototype chain
        // (before Error-like check, since user objects may have a "name" property)
        {
            Item ts_key = (Item){.item = s2it(heap_create_name("toString", 8))};
            Item ts_fn = js_prototype_lookup(value, ts_key);
            if (ts_fn.item == ItemNull.item) {
                // also check own
                bool own_ts = false;
                ts_fn = js_map_get_fast(value.map, "toString", 8, &own_ts);
                if (!own_ts) ts_fn = ItemNull;
            }
            if (ts_fn.item != ItemNull.item && get_type_id(ts_fn) == LMD_TYPE_FUNC) {
                Item result = js_call_function(ts_fn, value, NULL, 0);
                if (get_type_id(result) == LMD_TYPE_STRING) return result;
                // toString returned non-string primitive: convert recursively
                TypeId rt = get_type_id(result);
                if (rt != LMD_TYPE_MAP && rt != LMD_TYPE_ARRAY) return js_to_string(result);
            }
        }
        // Check for Error-like objects (have 'name' and 'message' properties)
        // JS: String(new Error("msg")) => "Error: msg"
        bool own_name = false, own_msg = false;
        Item name_val = js_map_get_fast(value.map, "name", 4, &own_name);
        Item msg_val = js_map_get_fast(value.map, "message", 7, &own_msg);
        if (own_name && get_type_id(name_val) == LMD_TYPE_STRING) {
            String* name_s = it2s(name_val);
            String* msg_s = (own_msg && get_type_id(msg_val) == LMD_TYPE_STRING) ? it2s(msg_val) : NULL;
            if (msg_s && msg_s->len > 0) {
                StrBuf* sb = strbuf_new();
                strbuf_append_str_n(sb, name_s->chars, (int)name_s->len);
                strbuf_append_str_n(sb, ": ", 2);
                strbuf_append_str_n(sb, msg_s->chars, (int)msg_s->len);
                String* result = heap_create_name(sb->str, sb->length);
                strbuf_free(sb);
                return (Item){.item = s2it(result)};
            }
            return name_val;
        }
        return (Item){.item = s2it(heap_create_name("[object Object]"))};
    }

    case LMD_TYPE_FUNC: {
        // Access function name via layout-compatible struct (JsFunction defined later)
        struct { TypeId type_id; void* func_ptr; int param_count; Item* env; int env_size;
                 Item prototype; Item bound_this; Item* bound_args; int bound_argc; String* name; } *fn_layout;
        fn_layout = decltype(fn_layout)(value.function);
        if (fn_layout->name && fn_layout->name->len > 0) {
            // NativeFunction syntax allows only a single IdentifierName (no spaces).
            // Bound functions have names like "bound f" — use only "bound" part.
            int name_len = fn_layout->name->len;
            for (int i = 0; i < fn_layout->name->len; i++) {
                if (fn_layout->name->chars[i] == ' ') { name_len = i; break; }
            }
            StrBuf* sb = strbuf_new();
            strbuf_append_str_n(sb, "function ", 9);
            strbuf_append_str_n(sb, fn_layout->name->chars, name_len);
            strbuf_append_str_n(sb, "() { [native code] }", 20);
            String* result = heap_create_name(sb->str, sb->length);
            strbuf_free(sb);
            return (Item){.item = s2it(result)};
        }
        return (Item){.item = s2it(heap_create_name("function () { [native code] }", 29))};
    }
    default:
        return (Item){.item = s2it(heap_create_name("[object Object]"))};
    }
}

extern "C" Item js_to_boolean(Item value) {
    return (Item){.item = b2it(js_is_truthy(value))};
}

extern "C" bool js_is_truthy(Item value) {
    TypeId type = get_type_id(value);

    switch (type) {
    case LMD_TYPE_NULL:
    case LMD_TYPE_UNDEFINED:
        return false;

    case LMD_TYPE_BOOL:
        return it2b(value);

    case LMD_TYPE_INT:
        return it2i(value) != 0;

    case LMD_TYPE_FLOAT: {
        double d = it2d(value);
        return !isnan(d) && d != 0.0;
    }

    case LMD_TYPE_STRING: {
        String* str = it2s(value);
        return str && str->len > 0;
    }

    default:
        // Objects, arrays, functions are all truthy
        return value.item != 0;
    }
}

// js_is_nullish: returns true if value is null or undefined (for ?? operator)
extern "C" int64_t js_is_nullish(Item value) {
    TypeId type = get_type_id(value);
    return (type == LMD_TYPE_NULL || type == LMD_TYPE_UNDEFINED) ? 1 : 0;
}

// =============================================================================
// v23 Performance Facades — compound operations returning raw int64_t
// =============================================================================

// js_typeof_is: returns 1 if typeof(value) matches type_str, 0 otherwise.
// Avoids heap string allocation that js_typeof() performs.
extern "C" int64_t js_typeof_is(Item value, const char* type_str) {
    TypeId type = get_type_id(value);
    switch (type_str[0]) {
    case 'n':
        if (type_str[1] == 'u') {
            // "number"
            if (type == LMD_TYPE_INT || type == LMD_TYPE_FLOAT) {
                return js_key_is_symbol(value) ? 0 : 1;
            }
            return 0;
        }
        return 0;
    case 's':
        if (type_str[1] == 't') return (type == LMD_TYPE_STRING) ? 1 : 0;  // "string"
        if (type_str[1] == 'y') return (type == LMD_TYPE_SYMBOL ||         // "symbol"
            ((type == LMD_TYPE_INT || type == LMD_TYPE_FLOAT) && js_key_is_symbol(value))) ? 1 : 0;
        return 0;
    case 'b': return (type == LMD_TYPE_BOOL) ? 1 : 0;      // "boolean"
    case 'u': return (type == LMD_TYPE_UNDEFINED) ? 1 : 0;  // "undefined"
    case 'o':
        // "object": null, map (non-class), array, element, or other non-function
        if (type == LMD_TYPE_NULL) return 1;
        if (type == LMD_TYPE_MAP) {
            bool own_ip = false;
            js_map_get_fast_ext(value.map, "__instance_proto__", 18, &own_ip);
            return own_ip ? 0 : 1;  // class objects are "function"
        }
        if (type == LMD_TYPE_FUNC || type == LMD_TYPE_UNDEFINED ||
            type == LMD_TYPE_BOOL || type == LMD_TYPE_STRING ||
            type == LMD_TYPE_SYMBOL) return 0;
        if ((type == LMD_TYPE_INT || type == LMD_TYPE_FLOAT) && !js_key_is_symbol(value)) return 0;
        return 1;  // arrays, elements, etc. are "object"
    case 'f':
        // "function"
        if (type == LMD_TYPE_FUNC) return 1;
        if (type == LMD_TYPE_MAP) {
            bool own_ip = false;
            js_map_get_fast_ext(value.map, "__instance_proto__", 18, &own_ip);
            return own_ip ? 1 : 0;
        }
        return 0;
    default: return 0;
    }
}

// v23b: Comparison facades returning raw int64_t 0/1 for direct use in MIR_BF/BT.
// Eliminates box→unbox→branch cycle when comparison is used in if/for/while condition.
// These inline the fast int-vs-int path and fall back to the full boxed comparison.
extern "C" int64_t js_lt_raw(Item left, Item right) {
    TypeId lt = get_type_id(left), rt = get_type_id(right);
    bool l_num = (lt == LMD_TYPE_INT || lt == LMD_TYPE_FLOAT);
    bool r_num = (rt == LMD_TYPE_INT || rt == LMD_TYPE_FLOAT);
    if (l_num && r_num) {
        double l = (lt == LMD_TYPE_INT) ? (double)it2i(left) : it2d(left);
        double r = (rt == LMD_TYPE_INT) ? (double)it2i(right) : it2d(right);
        if (isnan(l) || isnan(r)) return 0;
        return l < r ? 1 : 0;
    }
    return (int64_t)it2b(js_less_than(left, right));
}

extern "C" int64_t js_gt_raw(Item left, Item right) {
    return js_lt_raw(right, left);
}

extern "C" int64_t js_le_raw(Item left, Item right) {
    return js_gt_raw(left, right) ? 0 : 1;
}

extern "C" int64_t js_ge_raw(Item left, Item right) {
    return js_lt_raw(left, right) ? 0 : 1;
}

extern "C" int64_t js_eq_raw(Item left, Item right) {
    return (int64_t)it2b(js_strict_equal(left, right));
}

extern "C" int64_t js_ne_raw(Item left, Item right) {
    return js_eq_raw(left, right) ? 0 : 1;
}

extern "C" int64_t js_loose_eq_raw(Item left, Item right) {
    return (int64_t)it2b(js_equal(left, right));
}

extern "C" int64_t js_loose_ne_raw(Item left, Item right) {
    return js_loose_eq_raw(left, right) ? 0 : 1;
}

// js_property_get_str: property access with C string key (avoids string boxing)
extern "C" Item js_property_get_str(Item object, const char* key, int key_len);

// =============================================================================
// Helper: Get numeric value as double
// =============================================================================

static double js_get_number(Item value) {
    TypeId type = get_type_id(value);

    switch (type) {
    case LMD_TYPE_INT:
        return (double)it2i(value);
    case LMD_TYPE_FLOAT:
        return it2d(value);
    case LMD_TYPE_BOOL:
        return it2b(value) ? 1.0 : 0.0;
    case LMD_TYPE_NULL:
        return 0.0;
    case LMD_TYPE_UNDEFINED:
        return NAN;
    case LMD_TYPE_STRING: {
        String* str = it2s(value);
        if (!str || str->len == 0) return 0.0;
        // trim whitespace
        const char* s = str->chars;
        int len = (int)str->len;
        while (len > 0 && (s[0] == ' ' || s[0] == '\t' || s[0] == '\n' || s[0] == '\r')) { s++; len--; }
        while (len > 0 && (s[len-1] == ' ' || s[len-1] == '\t' || s[len-1] == '\n' || s[len-1] == '\r')) { len--; }
        if (len == 0) return 0.0;
        // handle hex/octal/binary
        if (len > 2 && s[0] == '0') {
            if (s[1] == 'x' || s[1] == 'X') return (double)strtoull(s, NULL, 16);
            if (s[1] == 'o' || s[1] == 'O') return (double)strtoull(s + 2, NULL, 8);
            if (s[1] == 'b' || s[1] == 'B') return (double)strtoull(s + 2, NULL, 2);
        }
        char* endptr;
        double num = strtod(s, &endptr);
        if (endptr == s) return NAN;
        return num;
    }
    case LMD_TYPE_MAP: {
        // ToPrimitive: wrapper objects with __primitiveValue__
        bool own_pv = false;
        Item pv = js_map_get_fast(value.map, "__primitiveValue__", 18, &own_pv);
        if (own_pv) return js_get_number(pv);
        // Check valueOf method
        bool own_vo = false;
        Item vo_fn = js_map_get_fast(value.map, "valueOf", 7, &own_vo);
        if (!own_vo) {
            Item vo_key = (Item){.item = s2it(heap_create_name("valueOf", 7))};
            vo_fn = js_prototype_lookup(value, vo_key);
        }
        if (vo_fn.item != ItemNull.item && get_type_id(vo_fn) == LMD_TYPE_FUNC) {
            Item result = js_call_function(vo_fn, value, NULL, 0);
            if (get_type_id(result) != LMD_TYPE_MAP) return js_get_number(result);
        }
        // ES spec: also try toString after valueOf
        bool own_ts = false;
        Item ts_fn = js_map_get_fast(value.map, "toString", 8, &own_ts);
        if (!own_ts) {
            Item ts_key = (Item){.item = s2it(heap_create_name("toString", 8))};
            ts_fn = js_prototype_lookup(value, ts_key);
        }
        if (ts_fn.item != ItemNull.item && get_type_id(ts_fn) == LMD_TYPE_FUNC) {
            Item result = js_call_function(ts_fn, value, NULL, 0);
            if (get_type_id(result) != LMD_TYPE_MAP) return js_get_number(result);
        }
        return NAN;
    }
    default:
        return NAN;
    }
}

static Item js_make_number(double d) {
    // Check if it can be represented as an integer
    // Guard with isfinite to avoid UB from (int64_t)Infinity/NaN
    // v18p: Preserve -0.0 as float (don't collapse to int 0)
    // Avoid creating ints with magnitude >= JS_SYMBOL_BASE to prevent
    // collision with JS symbol encoding (symbols use negative ints <= -JS_SYMBOL_BASE)
    if (isfinite(d) && d == (double)(int64_t)d && d >= INT56_MIN && d <= INT56_MAX
        && !(d == 0.0 && signbit(d))
        && (int64_t)d > -(int64_t)JS_SYMBOL_BASE) {
        return (Item){.item = i2it((int64_t)d)};
    }
    double* ptr = (double*)heap_alloc(sizeof(double), LMD_TYPE_FLOAT);
    *ptr = d;
    return (Item){.item = d2it(ptr)};
}

// =============================================================================
// Arithmetic Operators
// =============================================================================

extern "C" Item js_add(Item left, Item right) {
    TypeId left_type = get_type_id(left);
    TypeId right_type = get_type_id(right);

    // ToPrimitive for objects before type checking (ES spec §12.8.3)
    // The + operator uses hint "default", which prefers valueOf then toString
    if (left_type == LMD_TYPE_MAP) {
        bool own_pv = false;
        Item pv = js_map_get_fast(left.map, "__primitiveValue__", 18, &own_pv);
        if (own_pv) { left = pv; left_type = get_type_id(left); }
        else {
            // Check Symbol.toPrimitive (prototype chain)
            Item sym_key_l = (Item){.item = s2it(heap_create_name("__sym_2", 7))};
            Item to_prim = js_property_get(left, sym_key_l);
            if (to_prim.item != ItemNull.item && get_type_id(to_prim) == LMD_TYPE_FUNC) {
                Item hint = (Item){.item = s2it(heap_create_name("default", 7))};
                Item args[1] = { hint };
                left = js_call_function(to_prim, left, args, 1);
                left_type = get_type_id(left);
            } else {
                // Try valueOf first (ES spec: OrdinaryToPrimitive with hint "default")
                bool found_vo = false;
                Item vo_fn = js_map_get_fast(left.map, "valueOf", 7, &found_vo);
                if (!found_vo) vo_fn = js_prototype_lookup(left, (Item){.item = s2it(heap_create_name("valueOf", 7))});
                bool left_resolved = false;
                if (vo_fn.item != ItemNull.item && get_type_id(vo_fn) == LMD_TYPE_FUNC) {
                    Item result = js_call_function(vo_fn, left, NULL, 0);
                    TypeId rt = get_type_id(result);
                    if (rt != LMD_TYPE_MAP && rt != LMD_TYPE_ARRAY) {
                        left = result; left_type = rt; left_resolved = true;
                    }
                }
                // Then try toString (ES spec: OrdinaryToPrimitive step 2)
                if (!left_resolved) {
                    bool found_ts = false;
                    Item ts_fn = js_map_get_fast(left.map, "toString", 8, &found_ts);
                    if (!found_ts) ts_fn = js_prototype_lookup(left, (Item){.item = s2it(heap_create_name("toString", 8))});
                    if (ts_fn.item != ItemNull.item && get_type_id(ts_fn) == LMD_TYPE_FUNC) {
                        Item result = js_call_function(ts_fn, left, NULL, 0);
                        TypeId rt = get_type_id(result);
                        if (rt != LMD_TYPE_MAP && rt != LMD_TYPE_ARRAY) {
                            left = result; left_type = rt; left_resolved = true;
                        }
                    }
                }
                if (!left_resolved) {
                    left = js_to_string(left); left_type = LMD_TYPE_STRING;
                }
            }
        }
    }
    if (right_type == LMD_TYPE_MAP) {
        bool own_pv = false;
        Item pv = js_map_get_fast(right.map, "__primitiveValue__", 18, &own_pv);
        if (own_pv) { right = pv; right_type = get_type_id(right); }
        else {
            Item sym_key_r = (Item){.item = s2it(heap_create_name("__sym_2", 7))};
            Item to_prim = js_property_get(right, sym_key_r);
            if (to_prim.item != ItemNull.item && get_type_id(to_prim) == LMD_TYPE_FUNC) {
                Item hint = (Item){.item = s2it(heap_create_name("default", 7))};
                Item args[1] = { hint };
                right = js_call_function(to_prim, right, args, 1);
                right_type = get_type_id(right);
            } else {
                bool found_vo = false;
                Item vo_fn = js_map_get_fast(right.map, "valueOf", 7, &found_vo);
                if (!found_vo) vo_fn = js_prototype_lookup(right, (Item){.item = s2it(heap_create_name("valueOf", 7))});
                bool right_resolved = false;
                if (vo_fn.item != ItemNull.item && get_type_id(vo_fn) == LMD_TYPE_FUNC) {
                    Item result = js_call_function(vo_fn, right, NULL, 0);
                    TypeId rt = get_type_id(result);
                    if (rt != LMD_TYPE_MAP && rt != LMD_TYPE_ARRAY) {
                        right = result; right_type = rt; right_resolved = true;
                    }
                }
                if (!right_resolved) {
                    bool found_ts = false;
                    Item ts_fn = js_map_get_fast(right.map, "toString", 8, &found_ts);
                    if (!found_ts) ts_fn = js_prototype_lookup(right, (Item){.item = s2it(heap_create_name("toString", 8))});
                    if (ts_fn.item != ItemNull.item && get_type_id(ts_fn) == LMD_TYPE_FUNC) {
                        Item result = js_call_function(ts_fn, right, NULL, 0);
                        TypeId rt = get_type_id(result);
                        if (rt != LMD_TYPE_MAP && rt != LMD_TYPE_ARRAY) {
                            right = result; right_type = rt; right_resolved = true;
                        }
                    }
                }
                if (!right_resolved) {
                    right = js_to_string(right); right_type = LMD_TYPE_STRING;
                }
            }
        }
    }
    // Arrays/Functions: ToPrimitive — check valueOf/toString before default
    auto toprimitive_non_map = [](Item& val, TypeId& vtype) {
        // Check for custom valueOf (set via val.valueOf = fn)
        Item vo_fn = js_property_get(val, (Item){.item = s2it(heap_create_name("valueOf", 7))});
        if (vo_fn.item != ItemNull.item && get_type_id(vo_fn) == LMD_TYPE_FUNC) {
            Item result = js_call_function(vo_fn, val, NULL, 0);
            TypeId rt = get_type_id(result);
            if (rt != LMD_TYPE_MAP && rt != LMD_TYPE_ARRAY && rt != LMD_TYPE_FUNC) {
                val = result; vtype = rt; return;
            }
        }
        // Check for custom toString
        Item ts_fn = js_property_get(val, (Item){.item = s2it(heap_create_name("toString", 8))});
        if (ts_fn.item != ItemNull.item && get_type_id(ts_fn) == LMD_TYPE_FUNC) {
            Item result = js_call_function(ts_fn, val, NULL, 0);
            TypeId rt = get_type_id(result);
            if (rt != LMD_TYPE_MAP && rt != LMD_TYPE_ARRAY && rt != LMD_TYPE_FUNC) {
                val = result; vtype = rt; return;
            }
        }
        // Default: convert to string
        val = js_to_string(val); vtype = LMD_TYPE_STRING;
    };
    if (left_type == LMD_TYPE_ARRAY || left_type == LMD_TYPE_FUNC) {
        toprimitive_non_map(left, left_type);
    }
    if (right_type == LMD_TYPE_ARRAY || right_type == LMD_TYPE_FUNC) {
        toprimitive_non_map(right, right_type);
    }

    // String concatenation if either operand is a string
    if (left_type == LMD_TYPE_STRING || right_type == LMD_TYPE_STRING) {
        Item left_str = js_to_string(left);
        Item right_str = js_to_string(right);
        return fn_join(left_str, right_str);
    }

    // Numeric addition — use double arithmetic for JS semantics
    double l = js_get_number(left);
    double r = js_get_number(right);
    return js_make_number(l + r);
}

extern "C" Item js_subtract(Item left, Item right) {
    double l = js_get_number(left);
    double r = js_get_number(right);
    return js_make_number(l - r);
}

extern "C" Item js_multiply(Item left, Item right) {
    // Use double arithmetic for JS semantics (no integer overflow errors)
    double l = js_get_number(left);
    double r = js_get_number(right);
    return js_make_number(l * r);
}

extern "C" Item js_divide(Item left, Item right) {
    // Use double arithmetic for correct JS semantics:
    // x/0 → Infinity, -x/0 → -Infinity, 0/0 → NaN (IEEE 754)
    double l = js_get_number(left);
    double r = js_get_number(right);
    return js_make_number(l / r);
}

extern "C" Item js_modulo(Item left, Item right) {
    // fn_mod does not support float types, so keep custom implementation
    // that handles JS numeric coercion correctly
    double l = js_get_number(left);
    double r = js_get_number(right);
    return js_make_number(fmod(l, r));
}

extern "C" Item js_power(Item left, Item right) {
    double base_d = js_get_number(js_to_number(left));
    double exp_d = js_get_number(js_to_number(right));
    return js_make_number(js_math_pow_d(base_d, exp_d));
}

// =============================================================================
// Comparison Operators
// =============================================================================

extern "C" Item js_equal(Item left, Item right) {
    TypeId left_type = get_type_id(left);
    TypeId right_type = get_type_id(right);

    // Same type: use strict equality
    if (left_type == right_type) {
        return js_strict_equal(left, right);
    }

    // null == undefined
    if ((left_type == LMD_TYPE_NULL && right_type == LMD_TYPE_UNDEFINED) ||
        (left_type == LMD_TYPE_UNDEFINED && right_type == LMD_TYPE_NULL)) {
        return (Item){.item = b2it(true)};
    }

    // Number comparisons
    if ((left_type == LMD_TYPE_INT || left_type == LMD_TYPE_FLOAT) &&
        (right_type == LMD_TYPE_INT || right_type == LMD_TYPE_FLOAT)) {
        double l = js_get_number(left);
        double r = js_get_number(right);
        return (Item){.item = b2it(l == r)};
    }

    // String to number
    if ((left_type == LMD_TYPE_STRING && (right_type == LMD_TYPE_INT || right_type == LMD_TYPE_FLOAT)) ||
        ((left_type == LMD_TYPE_INT || left_type == LMD_TYPE_FLOAT) && right_type == LMD_TYPE_STRING)) {
        double l = js_get_number(left);
        double r = js_get_number(right);
        return (Item){.item = b2it(l == r)};
    }

    // Boolean to number
    if (left_type == LMD_TYPE_BOOL) {
        return js_equal(js_to_number(left), right);
    }
    if (right_type == LMD_TYPE_BOOL) {
        return js_equal(left, js_to_number(right));
    }

    // Object ToPrimitive: if one side is object/map, convert via ToPrimitive then recurse
    if (left_type == LMD_TYPE_MAP && (right_type == LMD_TYPE_INT || right_type == LMD_TYPE_FLOAT || right_type == LMD_TYPE_STRING)) {
        // Try __primitiveValue__ first (wrapper objects)
        bool own_pv = false;
        Item pv = js_map_get_fast(left.map, "__primitiveValue__", 18, &own_pv);
        if (own_pv) return js_equal(pv, right);
        // ToPrimitive: try valueOf, then toString (ES spec §7.1.1)
        bool resolved = false;
        Item prim = ItemNull;
        // valueOf
        bool found_vo = false;
        Item vo_fn = js_map_get_fast(left.map, "valueOf", 7, &found_vo);
        if (!found_vo) vo_fn = js_prototype_lookup(left, (Item){.item = s2it(heap_create_name("valueOf", 7))});
        if (vo_fn.item != ItemNull.item && get_type_id(vo_fn) == LMD_TYPE_FUNC) {
            Item result = js_call_function(vo_fn, left, NULL, 0);
            TypeId rt = get_type_id(result);
            if (rt != LMD_TYPE_MAP && rt != LMD_TYPE_ARRAY) { prim = result; resolved = true; }
        }
        // toString
        if (!resolved) {
            bool found_ts = false;
            Item ts_fn = js_map_get_fast(left.map, "toString", 8, &found_ts);
            if (!found_ts) ts_fn = js_prototype_lookup(left, (Item){.item = s2it(heap_create_name("toString", 8))});
            if (ts_fn.item != ItemNull.item && get_type_id(ts_fn) == LMD_TYPE_FUNC) {
                Item result = js_call_function(ts_fn, left, NULL, 0);
                TypeId rt = get_type_id(result);
                if (rt != LMD_TYPE_MAP && rt != LMD_TYPE_ARRAY) { prim = result; resolved = true; }
            }
        }
        if (resolved) return js_equal(prim, right);
        return (Item){.item = b2it(false)};
    }
    if (right_type == LMD_TYPE_MAP && (left_type == LMD_TYPE_INT || left_type == LMD_TYPE_FLOAT || left_type == LMD_TYPE_STRING)) {
        bool own_pv = false;
        Item pv = js_map_get_fast(right.map, "__primitiveValue__", 18, &own_pv);
        if (own_pv) return js_equal(left, pv);
        bool resolved = false;
        Item prim = ItemNull;
        bool found_vo = false;
        Item vo_fn = js_map_get_fast(right.map, "valueOf", 7, &found_vo);
        if (!found_vo) vo_fn = js_prototype_lookup(right, (Item){.item = s2it(heap_create_name("valueOf", 7))});
        if (vo_fn.item != ItemNull.item && get_type_id(vo_fn) == LMD_TYPE_FUNC) {
            Item result = js_call_function(vo_fn, right, NULL, 0);
            TypeId rt = get_type_id(result);
            if (rt != LMD_TYPE_MAP && rt != LMD_TYPE_ARRAY) { prim = result; resolved = true; }
        }
        if (!resolved) {
            bool found_ts = false;
            Item ts_fn = js_map_get_fast(right.map, "toString", 8, &found_ts);
            if (!found_ts) ts_fn = js_prototype_lookup(right, (Item){.item = s2it(heap_create_name("toString", 8))});
            if (ts_fn.item != ItemNull.item && get_type_id(ts_fn) == LMD_TYPE_FUNC) {
                Item result = js_call_function(ts_fn, right, NULL, 0);
                TypeId rt = get_type_id(result);
                if (rt != LMD_TYPE_MAP && rt != LMD_TYPE_ARRAY) { prim = result; resolved = true; }
            }
        }
        if (resolved) return js_equal(left, prim);
        return (Item){.item = b2it(false)};
    }

    // Array ToPrimitive: convert to string then compare
    if (left_type == LMD_TYPE_ARRAY) {
        return js_equal(js_to_string(left), right);
    }
    if (right_type == LMD_TYPE_ARRAY) {
        return js_equal(left, js_to_string(right));
    }

    return (Item){.item = b2it(false)};
}

extern "C" Item js_not_equal(Item left, Item right) {
    Item eq = js_equal(left, right);
    return (Item){.item = b2it(!it2b(eq))};
}

extern "C" Item js_strict_equal(Item left, Item right) {
    TypeId left_type = get_type_id(left);
    TypeId right_type = get_type_id(right);

    // In JS, all numeric types (int, int64, float) are the same "number" type
    // so int 0 === float 0.0 should be true (strict equality within number)
    bool left_is_num = (left_type == LMD_TYPE_INT || left_type == LMD_TYPE_INT64 || left_type == LMD_TYPE_FLOAT);
    bool right_is_num = (right_type == LMD_TYPE_INT || right_type == LMD_TYPE_INT64 || right_type == LMD_TYPE_FLOAT);
    if (left_is_num && right_is_num) {
        double l = js_get_number(left);
        double r = js_get_number(right);
        if (isnan(l) || isnan(r)) return (Item){.item = b2it(false)};
        return (Item){.item = b2it(l == r)};
    }

    // Different types are never strictly equal
    if (left_type != right_type) {
        return (Item){.item = b2it(false)};
    }

    switch (left_type) {
    case LMD_TYPE_NULL:
    case LMD_TYPE_UNDEFINED:
        return (Item){.item = b2it(true)};

    case LMD_TYPE_BOOL:
        return (Item){.item = b2it(it2b(left) == it2b(right))};

    case LMD_TYPE_INT:
        return (Item){.item = b2it(it2i(left) == it2i(right))};

    case LMD_TYPE_FLOAT: {
        double l = it2d(left);
        double r = it2d(right);
        // NaN !== NaN
        if (isnan(l) || isnan(r)) {
            return (Item){.item = b2it(false)};
        }
        return (Item){.item = b2it(l == r)};
    }

    case LMD_TYPE_STRING: {
        String* l_str = it2s(left);
        String* r_str = it2s(right);
        if (l_str->len != r_str->len) {
            return (Item){.item = b2it(false)};
        }
        return (Item){.item = b2it(memcmp(l_str->chars, r_str->chars, l_str->len) == 0)};
    }

    default:
        // Object identity comparison (also handles native wrapper objects:
        // two wrappers of the same type wrapping the same native pointer are ===)
        if (left.item != right.item && left_type == LMD_TYPE_MAP) {
            Map* l_map = left.map;
            Map* r_map = right.map;
            if (l_map && r_map && l_map->type && l_map->type == r_map->type
                && l_map->data && l_map->data == r_map->data) {
                return (Item){.item = b2it(true)};
            }
        }
        return (Item){.item = b2it(left.item == right.item)};
    }
}

extern "C" Item js_strict_not_equal(Item left, Item right) {
    Item eq = js_strict_equal(left, right);
    return (Item){.item = b2it(!it2b(eq))};
}

extern "C" Item js_less_than(Item left, Item right) {
    TypeId left_type = get_type_id(left);
    TypeId right_type = get_type_id(right);

    // ToPrimitive for objects/arrays (ES spec §7.2.14 Abstract Relational Comparison)
    if (left_type == LMD_TYPE_MAP) {
        bool own_pv = false;
        Item pv = js_map_get_fast(left.map, "__primitiveValue__", 18, &own_pv);
        if (own_pv) { left = pv; }
        else {
            bool resolved = false;
            bool found_vo = false;
            Item vo_fn = js_map_get_fast(left.map, "valueOf", 7, &found_vo);
            if (!found_vo) vo_fn = js_prototype_lookup(left, (Item){.item = s2it(heap_create_name("valueOf", 7))});
            if (vo_fn.item != ItemNull.item && get_type_id(vo_fn) == LMD_TYPE_FUNC) {
                Item result = js_call_function(vo_fn, left, NULL, 0);
                TypeId rt = get_type_id(result);
                if (rt != LMD_TYPE_MAP && rt != LMD_TYPE_ARRAY) { left = result; resolved = true; }
            }
            if (!resolved) {
                bool found_ts = false;
                Item ts_fn = js_map_get_fast(left.map, "toString", 8, &found_ts);
                if (!found_ts) ts_fn = js_prototype_lookup(left, (Item){.item = s2it(heap_create_name("toString", 8))});
                if (ts_fn.item != ItemNull.item && get_type_id(ts_fn) == LMD_TYPE_FUNC) {
                    Item result = js_call_function(ts_fn, left, NULL, 0);
                    TypeId rt = get_type_id(result);
                    if (rt != LMD_TYPE_MAP && rt != LMD_TYPE_ARRAY) { left = result; resolved = true; }
                }
            }
            if (!resolved) left = js_to_string(left);
        }
        left_type = get_type_id(left);
    }
    if (left_type == LMD_TYPE_ARRAY) { left = js_to_string(left); left_type = LMD_TYPE_STRING; }
    if (right_type == LMD_TYPE_MAP) {
        bool own_pv = false;
        Item pv = js_map_get_fast(right.map, "__primitiveValue__", 18, &own_pv);
        if (own_pv) { right = pv; }
        else {
            bool resolved = false;
            bool found_vo = false;
            Item vo_fn = js_map_get_fast(right.map, "valueOf", 7, &found_vo);
            if (!found_vo) vo_fn = js_prototype_lookup(right, (Item){.item = s2it(heap_create_name("valueOf", 7))});
            if (vo_fn.item != ItemNull.item && get_type_id(vo_fn) == LMD_TYPE_FUNC) {
                Item result = js_call_function(vo_fn, right, NULL, 0);
                TypeId rt = get_type_id(result);
                if (rt != LMD_TYPE_MAP && rt != LMD_TYPE_ARRAY) { right = result; resolved = true; }
            }
            if (!resolved) {
                bool found_ts = false;
                Item ts_fn = js_map_get_fast(right.map, "toString", 8, &found_ts);
                if (!found_ts) ts_fn = js_prototype_lookup(right, (Item){.item = s2it(heap_create_name("toString", 8))});
                if (ts_fn.item != ItemNull.item && get_type_id(ts_fn) == LMD_TYPE_FUNC) {
                    Item result = js_call_function(ts_fn, right, NULL, 0);
                    TypeId rt = get_type_id(result);
                    if (rt != LMD_TYPE_MAP && rt != LMD_TYPE_ARRAY) { right = result; resolved = true; }
                }
            }
            if (!resolved) right = js_to_string(right);
        }
        right_type = get_type_id(right);
    }
    if (right_type == LMD_TYPE_ARRAY) { right = js_to_string(right); right_type = LMD_TYPE_STRING; }

    // String comparison
    if (left_type == LMD_TYPE_STRING && right_type == LMD_TYPE_STRING) {
        String* l_str = it2s(left);
        String* r_str = it2s(right);
        int cmp = memcmp(l_str->chars, r_str->chars,
                        l_str->len < r_str->len ? l_str->len : r_str->len);
        if (cmp == 0) {
            return (Item){.item = b2it(l_str->len < r_str->len)};
        }
        return (Item){.item = b2it(cmp < 0)};
    }

    // Numeric comparison
    double l = js_get_number(left);
    double r = js_get_number(right);
    if (isnan(l) || isnan(r)) {
        return (Item){.item = b2it(false)};
    }
    return (Item){.item = b2it(l < r)};
}

extern "C" Item js_less_equal(Item left, Item right) {
    Item gt = js_greater_than(left, right);
    return (Item){.item = b2it(!it2b(gt))};
}

extern "C" Item js_greater_than(Item left, Item right) {
    return js_less_than(right, left);
}

extern "C" Item js_greater_equal(Item left, Item right) {
    Item lt = js_less_than(left, right);
    return (Item){.item = b2it(!it2b(lt))};
}

// =============================================================================
// Logical Operators
// =============================================================================

extern "C" Item js_logical_and(Item left, Item right) {
    // Returns left if falsy, otherwise right
    if (!js_is_truthy(left)) {
        return left;
    }
    return right;
}

extern "C" Item js_logical_or(Item left, Item right) {
    // Returns left if truthy, otherwise right
    if (js_is_truthy(left)) {
        return left;
    }
    return right;
}

extern "C" Item js_logical_not(Item operand) {
    return (Item){.item = b2it(!js_is_truthy(operand))};
}

// =============================================================================
// Bitwise Operators
// =============================================================================

// JavaScript ToInt32: non-finite values → 0, large values wrap modulo 2^32
static inline int32_t js_to_int32(double d) {
    if (!isfinite(d) || d == 0.0) return 0;
    // Modulo 2^32, then interpret as signed
    double d2 = fmod(trunc(d), 4294967296.0);
    if (d2 < 0) d2 += 4294967296.0;
    return (d2 >= 2147483648.0) ? (int32_t)(d2 - 4294967296.0) : (int32_t)d2;
}

// JIT-callable version of ToInt32: takes double, returns int64 for MIR compatibility
extern "C" int64_t js_double_to_int32(double d) {
    return (int64_t)js_to_int32(d);
}

extern "C" Item js_bitwise_and(Item left, Item right) {
    int32_t l = js_to_int32(js_get_number(left));
    int32_t r = js_to_int32(js_get_number(right));
    return (Item){.item = i2it(l & r)};
}

extern "C" Item js_bitwise_or(Item left, Item right) {
    int32_t l = js_to_int32(js_get_number(left));
    int32_t r = js_to_int32(js_get_number(right));
    return (Item){.item = i2it(l | r)};
}

extern "C" Item js_bitwise_xor(Item left, Item right) {
    int32_t l = js_to_int32(js_get_number(left));
    int32_t r = js_to_int32(js_get_number(right));
    return (Item){.item = i2it(l ^ r)};
}

extern "C" Item js_bitwise_not(Item operand) {
    int32_t val = js_to_int32(js_get_number(operand));
    return (Item){.item = i2it(~val)};
}

extern "C" Item js_left_shift(Item left, Item right) {
    int32_t l = js_to_int32(js_get_number(left));
    uint32_t r = (uint32_t)js_to_int32(js_get_number(right)) & 0x1F;
    return (Item){.item = i2it(l << r)};
}

extern "C" Item js_right_shift(Item left, Item right) {
    int32_t l = js_to_int32(js_get_number(left));
    uint32_t r = (uint32_t)js_to_int32(js_get_number(right)) & 0x1F;
    return (Item){.item = i2it(l >> r)};
}

extern "C" Item js_unsigned_right_shift(Item left, Item right) {
    uint32_t l = (uint32_t)js_to_int32(js_get_number(left));
    uint32_t r = (uint32_t)js_to_int32(js_get_number(right)) & 0x1F;
    return (Item){.item = i2it((int64_t)(l >> r))};
}

// =============================================================================
// Unary Operators
// =============================================================================

extern "C" Item js_unary_plus(Item operand) {
    return js_to_number(operand);
}

extern "C" Item js_unary_minus(Item operand) {
    Item num = js_to_number(operand);
    // v18p: Integer 0 negated must produce float -0.0 per IEEE 754 / ECMAScript spec
    if (get_type_id(num) == LMD_TYPE_INT && it2i(num) == 0) {
        return js_make_number(-0.0);
    }
    Item result = fn_neg(num);
    // After negation, check if the result is an int in the symbol collision range.
    // If so, promote to float to avoid being misidentified as a symbol.
    if (get_type_id(result) == LMD_TYPE_INT && it2i(result) <= -(int64_t)JS_SYMBOL_BASE) {
        return js_make_number((double)it2i(result));
    }
    return result;
}

extern "C" Item js_typeof(Item value) {
    TypeId type = get_type_id(value);

    const char* result;
    switch (type) {
    case LMD_TYPE_UNDEFINED:
        result = "undefined";
        break;
    case LMD_TYPE_NULL:
        result = "object";  // typeof null === "object" (JS quirk)
        break;
    case LMD_TYPE_BOOL:
        result = "boolean";
        break;
    case LMD_TYPE_INT:
    case LMD_TYPE_FLOAT:
        result = js_key_is_symbol(value) ? "symbol" : "number";
        break;
    case LMD_TYPE_STRING:
        result = "string";
        break;
    case LMD_TYPE_SYMBOL:
        result = "symbol";
        break;
    case LMD_TYPE_FUNC:
        result = "function";
        break;
    case LMD_TYPE_MAP: {
        // v18h: class objects (MAPs with __instance_proto__) should return "function"
        // Use direct property lookup instead of shape walking for GC safety
        bool own_ip = false;
        js_map_get_fast_ext(value.map, "__instance_proto__", 18, &own_ip);
        if (own_ip) {
            result = "function";
            goto done;
        }
        result = "object";
        break;
    }
    default:
        result = "object";
        break;
    }
done:
    return (Item){.item = s2it(heap_create_name(result))};
}

// =============================================================================
// Object Functions
// =============================================================================

// JsFunction struct: defined here so js_property_get/set can access .prototype
struct JsFunction {
    TypeId type_id;  // Always LMD_TYPE_FUNC
    void* func_ptr;  // Pointer to the compiled function
    int param_count; // Number of parameters (user-visible, not including env)
    Item* env;       // Closure environment (NULL for non-closures)
    int env_size;    // Number of captured variables in env
    Item prototype;  // Constructor prototype (Foo.prototype = {...})
    Item bound_this; // v11: bound 'this' (0 if not a bound function)
    Item* bound_args; // v11: pre-applied arguments (NULL if none)
    int bound_argc;  // v11: number of bound arguments
    String* name;    // Function name (NULL if anonymous)
    int builtin_id;  // >0 for built-in method dispatch (0 = user function)
    Item properties_map; // v18: backing map for arbitrary properties (0 if none)
    uint8_t flags;   // v20: bit 0 = is_generator
    int16_t formal_length; // ES spec .length: params before first default, excl rest (-1 = use param_count)
};

#define JS_FUNC_FLAG_GENERATOR 1
#define JS_FUNC_FLAG_ARROW     2

// Forward declarations for collection types (defined here, used in js_property_get and js_dispatch_builtin)
#define JS_COLLECTION_MAP 0
#define JS_COLLECTION_SET 1

struct JsCollectionOrderNode {
    Item key;
    Item value;
    JsCollectionOrderNode* next;
    JsCollectionOrderNode* prev;
};

struct JsCollectionData {
    HashMap* hmap;
    int type; // JS_COLLECTION_MAP or JS_COLLECTION_SET
    bool is_weak; // true for WeakMap/WeakSet
    JsCollectionOrderNode* order_head;
    JsCollectionOrderNode* order_tail;
};

static JsCollectionData* js_get_collection_data(Item obj);

// Built-in method IDs for prototype method dispatch
enum JsBuiltinId {
    JS_BUILTIN_NONE = 0,
    // Object.prototype
    JS_BUILTIN_OBJ_HAS_OWN_PROPERTY,
    JS_BUILTIN_OBJ_PROPERTY_IS_ENUMERABLE,
    JS_BUILTIN_OBJ_TO_STRING,
    JS_BUILTIN_OBJ_VALUE_OF,
    JS_BUILTIN_OBJ_IS_PROTOTYPE_OF,
    JS_BUILTIN_OBJ_TO_LOCALE_STRING,
    // Array.prototype
    JS_BUILTIN_ARR_PUSH,
    JS_BUILTIN_ARR_POP,
    JS_BUILTIN_ARR_SHIFT,
    JS_BUILTIN_ARR_UNSHIFT,
    JS_BUILTIN_ARR_JOIN,
    JS_BUILTIN_ARR_SLICE,
    JS_BUILTIN_ARR_SPLICE,
    JS_BUILTIN_ARR_INDEX_OF,
    JS_BUILTIN_ARR_INCLUDES,
    JS_BUILTIN_ARR_MAP,
    JS_BUILTIN_ARR_FILTER,
    JS_BUILTIN_ARR_REDUCE,
    JS_BUILTIN_ARR_FOR_EACH,
    JS_BUILTIN_ARR_FIND,
    JS_BUILTIN_ARR_FIND_INDEX,
    JS_BUILTIN_ARR_SOME,
    JS_BUILTIN_ARR_EVERY,
    JS_BUILTIN_ARR_SORT,
    JS_BUILTIN_ARR_REVERSE,
    JS_BUILTIN_ARR_CONCAT,
    JS_BUILTIN_ARR_FLAT,
    JS_BUILTIN_ARR_FLAT_MAP,
    JS_BUILTIN_ARR_FILL,
    JS_BUILTIN_ARR_COPY_WITHIN,
    JS_BUILTIN_ARR_TO_STRING,
    JS_BUILTIN_ARR_KEYS,
    JS_BUILTIN_ARR_VALUES,
    JS_BUILTIN_ARR_ENTRIES,
    JS_BUILTIN_ARR_AT,
    JS_BUILTIN_ARR_LAST_INDEX_OF,
    JS_BUILTIN_ARR_REDUCE_RIGHT,
    JS_BUILTIN_ARR_FIND_LAST,
    JS_BUILTIN_ARR_FIND_LAST_INDEX,
    JS_BUILTIN_ARR_TO_SORTED,
    JS_BUILTIN_ARR_TO_REVERSED,
    JS_BUILTIN_ARR_TO_SPLICED,
    JS_BUILTIN_ARR_WITH,
    // Function.prototype
    JS_BUILTIN_FUNC_CALL,
    JS_BUILTIN_FUNC_APPLY,
    JS_BUILTIN_FUNC_BIND,
    JS_BUILTIN_FUNC_TO_STRING,
    // String.prototype
    JS_BUILTIN_STR_CHAR_AT,
    JS_BUILTIN_STR_CHAR_CODE_AT,
    JS_BUILTIN_STR_INDEX_OF,
    JS_BUILTIN_STR_INCLUDES,
    JS_BUILTIN_STR_SLICE,
    JS_BUILTIN_STR_SUBSTRING,
    JS_BUILTIN_STR_TO_LOWER_CASE,
    JS_BUILTIN_STR_TO_UPPER_CASE,
    JS_BUILTIN_STR_TRIM,
    JS_BUILTIN_STR_SPLIT,
    JS_BUILTIN_STR_REPLACE,
    JS_BUILTIN_STR_MATCH,
    JS_BUILTIN_STR_SEARCH,
    JS_BUILTIN_STR_STARTS_WITH,
    JS_BUILTIN_STR_ENDS_WITH,
    JS_BUILTIN_STR_REPEAT,
    JS_BUILTIN_STR_PAD_START,
    JS_BUILTIN_STR_PAD_END,
    JS_BUILTIN_STR_TO_STRING,
    JS_BUILTIN_STR_VALUE_OF,
    JS_BUILTIN_STR_TRIM_START,
    JS_BUILTIN_STR_TRIM_END,
    JS_BUILTIN_STR_CODE_POINT_AT,
    JS_BUILTIN_STR_NORMALIZE,
    JS_BUILTIN_STR_CONCAT,
    JS_BUILTIN_STR_AT,
    JS_BUILTIN_STR_LAST_INDEX_OF,
    JS_BUILTIN_STR_LOCALE_COMPARE,
    JS_BUILTIN_STR_REPLACE_ALL,
    JS_BUILTIN_STR_MATCH_ALL,
    // Object static methods (v18k: accessible as first-class values)
    JS_BUILTIN_OBJECT_DEFINE_PROPERTY,
    JS_BUILTIN_OBJECT_DEFINE_PROPERTIES,
    JS_BUILTIN_OBJECT_GET_OWN_PROPERTY_DESCRIPTOR,
    JS_BUILTIN_OBJECT_GET_OWN_PROPERTY_DESCRIPTORS,
    JS_BUILTIN_OBJECT_GET_OWN_PROPERTY_NAMES,
    JS_BUILTIN_OBJECT_GET_OWN_PROPERTY_SYMBOLS,
    JS_BUILTIN_OBJECT_KEYS,
    JS_BUILTIN_OBJECT_VALUES,
    JS_BUILTIN_OBJECT_ENTRIES,
    JS_BUILTIN_OBJECT_FROM_ENTRIES,
    JS_BUILTIN_OBJECT_CREATE,
    JS_BUILTIN_OBJECT_ASSIGN,
    JS_BUILTIN_OBJECT_FREEZE,
    JS_BUILTIN_OBJECT_IS_FROZEN,
    JS_BUILTIN_OBJECT_SEAL,
    JS_BUILTIN_OBJECT_IS_SEALED,
    JS_BUILTIN_OBJECT_PREVENT_EXTENSIONS,
    JS_BUILTIN_OBJECT_IS_EXTENSIBLE,
    JS_BUILTIN_OBJECT_IS,
    JS_BUILTIN_OBJECT_GET_PROTOTYPE_OF,
    JS_BUILTIN_OBJECT_SET_PROTOTYPE_OF,
    JS_BUILTIN_OBJECT_HAS_OWN,
    // Array static methods
    JS_BUILTIN_ARRAY_IS_ARRAY,
    JS_BUILTIN_ARRAY_FROM,
    JS_BUILTIN_ARRAY_OF,
    JS_BUILTIN_ARRAY_ITER_NEXT, // Array iterator .next()
    // Number static methods
    JS_BUILTIN_NUMBER_IS_INTEGER,
    JS_BUILTIN_NUMBER_IS_FINITE,
    JS_BUILTIN_NUMBER_IS_NAN,
    JS_BUILTIN_NUMBER_IS_SAFE_INTEGER,
    JS_BUILTIN_NUMBER_PARSE_INT,
    JS_BUILTIN_NUMBER_PARSE_FLOAT,
    // Number.prototype methods (v18o)
    JS_BUILTIN_NUM_TO_STRING,
    JS_BUILTIN_NUM_VALUE_OF,
    JS_BUILTIN_NUM_TO_FIXED,
    JS_BUILTIN_NUM_TO_PRECISION,
    JS_BUILTIN_NUM_TO_EXPONENTIAL,
    // Symbol.prototype methods
    JS_BUILTIN_SYM_TO_STRING,
    // String static methods
    JS_BUILTIN_STRING_RAW,
    JS_BUILTIN_STRING_FROM_CODE_POINT,
    JS_BUILTIN_STRING_FROM_CHAR_CODE,
    // Math methods (first-class function values)
    JS_BUILTIN_MATH_ABS,
    JS_BUILTIN_MATH_FLOOR,
    JS_BUILTIN_MATH_CEIL,
    JS_BUILTIN_MATH_ROUND,
    JS_BUILTIN_MATH_SQRT,
    JS_BUILTIN_MATH_POW,
    JS_BUILTIN_MATH_MIN,
    JS_BUILTIN_MATH_MAX,
    JS_BUILTIN_MATH_LOG,
    JS_BUILTIN_MATH_LOG10,
    JS_BUILTIN_MATH_LOG2,
    JS_BUILTIN_MATH_EXP,
    JS_BUILTIN_MATH_SIN,
    JS_BUILTIN_MATH_COS,
    JS_BUILTIN_MATH_TAN,
    JS_BUILTIN_MATH_SIGN,
    JS_BUILTIN_MATH_TRUNC,
    JS_BUILTIN_MATH_RANDOM,
    JS_BUILTIN_MATH_ASIN,
    JS_BUILTIN_MATH_ACOS,
    JS_BUILTIN_MATH_ATAN,
    JS_BUILTIN_MATH_ATAN2,
    JS_BUILTIN_MATH_CBR,
    JS_BUILTIN_MATH_HYPOT,
    JS_BUILTIN_MATH_CLZ32,
    JS_BUILTIN_MATH_FROUND,
    JS_BUILTIN_MATH_IMUL,
    JS_BUILTIN_MATH_SINH,
    JS_BUILTIN_MATH_COSH,
    JS_BUILTIN_MATH_TANH,
    JS_BUILTIN_MATH_ASINH,
    JS_BUILTIN_MATH_ACOSH,
    JS_BUILTIN_MATH_ATANH,
    JS_BUILTIN_MATH_EXPM1,
    JS_BUILTIN_MATH_LOG1P,
    JS_BUILTIN_JSON_PARSE,
    JS_BUILTIN_JSON_STRINGIFY,
    // String iterator
    JS_BUILTIN_STRING_ITER,      // String.prototype[Symbol.iterator]() — creates string iterator
    JS_BUILTIN_STRING_ITER_NEXT, // String iterator .next()
    // Error.prototype.toString (generic)
    JS_BUILTIN_ERR_TO_STRING,
    // Boolean.prototype.toString
    JS_BUILTIN_BOOL_TO_STRING,
    // Date.prototype methods (v45: make Date methods visible as properties)
    JS_BUILTIN_DATE_GET_TIME,
    JS_BUILTIN_DATE_GET_FULL_YEAR,
    JS_BUILTIN_DATE_GET_MONTH,
    JS_BUILTIN_DATE_GET_DATE,
    JS_BUILTIN_DATE_GET_HOURS,
    JS_BUILTIN_DATE_GET_MINUTES,
    JS_BUILTIN_DATE_GET_SECONDS,
    JS_BUILTIN_DATE_GET_MILLISECONDS,
    JS_BUILTIN_DATE_TO_ISO_STRING,
    JS_BUILTIN_DATE_TO_JSON,
    JS_BUILTIN_DATE_TO_UTC_STRING,
    JS_BUILTIN_DATE_TO_DATE_STRING,
    JS_BUILTIN_DATE_TO_TIME_STRING,
    JS_BUILTIN_DATE_TO_STRING,
    JS_BUILTIN_DATE_TO_LOCALE_DATE_STRING,
    JS_BUILTIN_DATE_VALUE_OF,
    JS_BUILTIN_DATE_GET_DAY,
    JS_BUILTIN_DATE_GET_UTC_FULL_YEAR,
    JS_BUILTIN_DATE_GET_UTC_MONTH,
    JS_BUILTIN_DATE_GET_UTC_DATE,
    JS_BUILTIN_DATE_GET_UTC_HOURS,
    JS_BUILTIN_DATE_GET_UTC_MINUTES,
    JS_BUILTIN_DATE_GET_UTC_SECONDS,
    JS_BUILTIN_DATE_GET_UTC_MILLISECONDS,
    JS_BUILTIN_DATE_GET_UTC_DAY,
    JS_BUILTIN_DATE_GET_TIMEZONE_OFFSET,
    JS_BUILTIN_DATE_SET_TIME,
    JS_BUILTIN_DATE_SET_FULL_YEAR,
    JS_BUILTIN_DATE_SET_MONTH,
    JS_BUILTIN_DATE_SET_DATE,
    JS_BUILTIN_DATE_SET_HOURS,
    JS_BUILTIN_DATE_SET_MINUTES,
    JS_BUILTIN_DATE_SET_SECONDS,
    JS_BUILTIN_DATE_SET_MILLISECONDS,
    JS_BUILTIN_DATE_SET_UTC_FULL_YEAR,
    JS_BUILTIN_DATE_SET_UTC_MONTH,
    JS_BUILTIN_DATE_SET_UTC_DATE,
    JS_BUILTIN_DATE_SET_UTC_HOURS,
    JS_BUILTIN_DATE_SET_UTC_MINUTES,
    JS_BUILTIN_DATE_SET_UTC_SECONDS,
    JS_BUILTIN_DATE_SET_UTC_MILLISECONDS,
    // Promise static methods (v45: make Promise static methods visible as properties)
    JS_BUILTIN_PROMISE_RESOLVE,
    JS_BUILTIN_PROMISE_REJECT,
    JS_BUILTIN_PROMISE_ALL,
    JS_BUILTIN_PROMISE_ALL_SETTLED,
    JS_BUILTIN_PROMISE_ANY,
    JS_BUILTIN_PROMISE_RACE,
    // Date static methods (v45)
    JS_BUILTIN_DATE_NOW,
    JS_BUILTIN_DATE_PARSE,
    JS_BUILTIN_DATE_UTC,
    // RegExp prototype methods (v46)
    JS_BUILTIN_REGEXP_EXEC,
    JS_BUILTIN_REGEXP_TEST,
    JS_BUILTIN_REGEXP_TO_STRING,
    // Set/Map iterator builtins (v55: proper iterator protocol)
    JS_BUILTIN_SET_VALUES,       // Set.prototype.values / Set.prototype[@@iterator]
    JS_BUILTIN_MAP_ENTRIES,      // Map.prototype.entries / Map.prototype[@@iterator]
    JS_BUILTIN_SET_KEYS,         // Set.prototype.keys (alias for values)
    JS_BUILTIN_MAP_KEYS,         // Map.prototype.keys
    JS_BUILTIN_MAP_VALUES,       // Map.prototype.values
    JS_BUILTIN_SET_ENTRIES,      // Set.prototype.entries
    JS_BUILTIN_COLL_ITER_NEXT,   // CollectionIterator.next()
    // Collection prototype methods (v76: expose on prototype for test262 compliance)
    JS_BUILTIN_MAP_SET,          // Map.prototype.set(key, value)
    JS_BUILTIN_MAP_GET,          // Map.prototype.get(key)
    JS_BUILTIN_MAP_HAS,          // Map.prototype.has(key)
    JS_BUILTIN_MAP_DELETE,       // Map.prototype.delete(key)
    JS_BUILTIN_MAP_CLEAR,        // Map.prototype.clear()
    JS_BUILTIN_MAP_FOREACH,      // Map.prototype.forEach(cb, thisArg)
    JS_BUILTIN_SET_ADD,          // Set.prototype.add(value)
    JS_BUILTIN_SET_HAS,          // Set.prototype.has(value)
    JS_BUILTIN_SET_DELETE,       // Set.prototype.delete(value)
    JS_BUILTIN_SET_CLEAR,        // Set.prototype.clear()
    JS_BUILTIN_SET_FOREACH,      // Set.prototype.forEach(cb, thisArg)
    JS_BUILTIN_SET_INTERSECTION, // Set.prototype.intersection(other)
    JS_BUILTIN_SET_UNION,        // Set.prototype.union(other)
    JS_BUILTIN_SET_DIFFERENCE,   // Set.prototype.difference(other)
    JS_BUILTIN_SET_SYM_DIFF,     // Set.prototype.symmetricDifference(other)
    JS_BUILTIN_SET_IS_SUBSET,    // Set.prototype.isSubsetOf(other)
    JS_BUILTIN_SET_IS_SUPERSET,  // Set.prototype.isSupersetOf(other)
    JS_BUILTIN_SET_IS_DISJOINT,  // Set.prototype.isDisjointFrom(other)
    JS_BUILTIN_COLL_SIZE_GETTER, // Map/Set.prototype size getter
    // RegExp Symbol methods (v83: @@match, @@replace, @@search, @@split)
    JS_BUILTIN_REGEXP_SYMBOL_MATCH,
    JS_BUILTIN_REGEXP_SYMBOL_REPLACE,
    JS_BUILTIN_REGEXP_SYMBOL_SEARCH,
    JS_BUILTIN_REGEXP_SYMBOL_SPLIT,
    JS_BUILTIN_MAX
};

extern "C" void* js_function_get_ptr(Item fn_item) {
    if (get_type_id(fn_item) != LMD_TYPE_FUNC) return NULL;
    // Try JsFunction layout first (func_ptr at offset 8)
    JsFunction* jsfn = (JsFunction*)fn_item.function;
    if (jsfn->func_ptr) return jsfn->func_ptr;
    // Fall back to Function layout (ptr at offset 16)
    Function* fn = fn_item.function;
    return (void*)fn->ptr;
}

extern "C" int js_function_get_arity(Item fn_item) {
    if (get_type_id(fn_item) != LMD_TYPE_FUNC) return 0;
    JsFunction* jsfn = (JsFunction*)fn_item.function;
    // If func_ptr (offset 8) is set, it's JsFunction layout
    if (jsfn->func_ptr) return jsfn->param_count;
    // Otherwise it's Function layout — arity at offset 1
    Function* fn = fn_item.function;
    return fn->arity;
}

// P2: Pre-computed size class for sizeof(Map) = 32 bytes → SIZE_CLASSES[1] = 32.
// Skips the class-index lookup in gc_heap_alloc and uses the bump-pointer fast path.
#define JS_MAP_SIZE_CLASS 1

// Create a new JS object as a Lambda Map (empty, using map_put for dynamic keys)
extern "C" Item js_new_object() {
    Map* m = (Map*)heap_calloc_class(sizeof(Map), LMD_TYPE_MAP, JS_MAP_SIZE_CLASS);
    m->type_id = LMD_TYPE_MAP;
    m->type = &EmptyMap;
    return (Item){.map = m};
}

// Create a new object for a constructor call: sets __proto__ from callee.prototype
extern "C" Item js_constructor_create_object(Item callee) {
    Item obj = js_new_object();
    if (get_type_id(callee) == LMD_TYPE_FUNC) {
        JsFunction* fn = (JsFunction*)callee.function;
        // Lazily create prototype if not yet initialized (ensures __proto__ is set
        // even when .prototype hasn't been explicitly accessed before new)
        if (fn->prototype.item == ItemNull.item) {
            Item proto_key = (Item){.item = s2it(heap_create_name("prototype", 9))};
            js_property_get(callee, proto_key); // triggers lazy init
        }
        if (fn->prototype.item != ItemNull.item && get_type_id(fn->prototype) == LMD_TYPE_MAP) {
            js_set_prototype(obj, fn->prototype);
        }
    }
    return obj;
}

// Dynamic class instantiation: new Type() where Type is a runtime variable.
// Handles both function constructors and class objects (MAPs with __ctor__).
extern "C" Item js_new_from_class_object(Item callee, Item* args, int argc) {
    // Set pending new.target (will be picked up by js_call_function)
    js_pending_new_target = callee;
    js_has_pending_new_target = true;

    // Case 1: callee is a function — standard constructor call
    if (get_type_id(callee) == LMD_TYPE_FUNC) {
        JsFunction* fn = (JsFunction*)callee.function;
        // Builtin functions (Math.abs, etc.), arrow functions, generators, and global builtins
        // (parseInt, etc.) are not constructable
        if (fn->builtin_id > 0 || fn->builtin_id == -2 || (fn->flags & (JS_FUNC_FLAG_ARROW | JS_FUNC_FLAG_GENERATOR))) {
            js_pending_new_target = ItemNull;
            js_has_pending_new_target = false;
            char buf[256];
            int len = snprintf(buf, sizeof(buf), "%s is not a constructor", fn->name ? fn->name->chars : "function");
            Item tn = (Item){.item = s2it(heap_create_name("TypeError", 9))};
            Item msg = (Item){.item = s2it(heap_create_name(buf, len))};
            js_throw_value(js_new_error_with_name(tn, msg));
            return ItemNull;
        }

        // Dispatch native built-in constructors by name (for dynamic new ctor(...))
        if (fn->name) {
            const char* n = fn->name->chars;
            int nl = (int)fn->name->len;

            // TypedArray constructors
            int ta_type = -1;
            if      (nl == 9  && strncmp(n, "Int8Array", 9) == 0)           ta_type = JS_TYPED_INT8;
            else if (nl == 10 && strncmp(n, "Uint8Array", 10) == 0)         ta_type = JS_TYPED_UINT8;
            else if (nl == 17 && strncmp(n, "Uint8ClampedArray", 17) == 0)  ta_type = JS_TYPED_UINT8_CLAMPED;
            else if (nl == 10 && strncmp(n, "Int16Array", 10) == 0)         ta_type = JS_TYPED_INT16;
            else if (nl == 11 && strncmp(n, "Uint16Array", 11) == 0)        ta_type = JS_TYPED_UINT16;
            else if (nl == 10 && strncmp(n, "Int32Array", 10) == 0)         ta_type = JS_TYPED_INT32;
            else if (nl == 11 && strncmp(n, "Uint32Array", 11) == 0)        ta_type = JS_TYPED_UINT32;
            else if (nl == 12 && strncmp(n, "Float32Array", 12) == 0)       ta_type = JS_TYPED_FLOAT32;
            else if (nl == 12 && strncmp(n, "Float64Array", 12) == 0)       ta_type = JS_TYPED_FLOAT64;
            if (ta_type >= 0) {
                js_pending_new_target = ItemNull;
                js_has_pending_new_target = false;
                Item arg = (argc > 0 && args) ? args[0] : ItemNull;
                int off = (argc > 1 && args) ? (int)it2i(args[1]) : 0;
                int tlen = (argc > 2 && args) ? (int)it2i(args[2]) : -1;
                return js_typed_array_construct(ta_type, arg, off, tlen, argc);
            }

            // ArrayBuffer
            if (nl == 11 && strncmp(n, "ArrayBuffer", 11) == 0) {
                js_pending_new_target = ItemNull;
                js_has_pending_new_target = false;
                int blen = (argc > 0 && args) ? (int)it2i(args[0]) : 0;
                return js_arraybuffer_new(blen);
            }

            // DataView
            if (nl == 8 && strncmp(n, "DataView", 8) == 0) {
                js_pending_new_target = ItemNull;
                js_has_pending_new_target = false;
                Item buf = (argc > 0 && args) ? args[0] : ItemNull;
                int off = (argc > 1 && args) ? (int)it2i(args[1]) : 0;
                int dvlen = (argc > 2 && args) ? (int)it2i(args[2]) : -1;
                return js_dataview_new(buf, off, dvlen);
            }

            // Map
            if (nl == 3 && strncmp(n, "Map", 3) == 0) {
                js_pending_new_target = ItemNull;
                js_has_pending_new_target = false;
                if (argc > 0 && args) return js_map_collection_new_from(args[0]);
                return js_map_collection_new();
            }

            // Set
            if (nl == 3 && strncmp(n, "Set", 3) == 0) {
                js_pending_new_target = ItemNull;
                js_has_pending_new_target = false;
                if (argc > 0 && args) return js_set_collection_new_from(args[0]);
                return js_set_collection_new();
            }

            // WeakMap
            if (nl == 7 && strncmp(n, "WeakMap", 7) == 0) {
                js_pending_new_target = ItemNull;
                js_has_pending_new_target = false;
                return js_weakmap_new();
            }

            // WeakSet
            if (nl == 7 && strncmp(n, "WeakSet", 7) == 0) {
                js_pending_new_target = ItemNull;
                js_has_pending_new_target = false;
                return js_weakset_new();
            }

            // Promise
            if (nl == 7 && strncmp(n, "Promise", 7) == 0) {
                js_pending_new_target = ItemNull;
                js_has_pending_new_target = false;
                Item executor = (argc > 0 && args) ? args[0] : ItemNull;
                return js_promise_create(executor);
            }

            // RegExp
            if (nl == 6 && strncmp(n, "RegExp", 6) == 0) {
                js_pending_new_target = ItemNull;
                js_has_pending_new_target = false;
                Item pattern = (argc > 0 && args) ? args[0] : (Item){.item = s2it(heap_create_name("", 0))};
                Item flags = (argc > 1 && args) ? args[1] : (Item){.item = s2it(heap_create_name("", 0))};
                return js_regexp_construct(pattern, flags);
            }

            // Date
            if (nl == 4 && strncmp(n, "Date", 4) == 0) {
                js_pending_new_target = ItemNull;
                js_has_pending_new_target = false;
                if (argc == 0) return js_date_new();
                if (argc == 1) return js_date_new_from(args[0]);
                // Multi-arg: pack into an array and call js_date_new_multi
                Item arr = js_array_new(argc);
                for (int i = 0; i < argc; i++) js_array_push(arr, args[i]);
                return js_date_new_multi(arr);
            }

            // Error and subclasses
            if ((nl == 5 && strncmp(n, "Error", 5) == 0) ||
                (nl == 9 && strncmp(n, "TypeError", 9) == 0) ||
                (nl == 10 && strncmp(n, "RangeError", 10) == 0) ||
                (nl == 14 && strncmp(n, "ReferenceError", 14) == 0) ||
                (nl == 11 && strncmp(n, "SyntaxError", 11) == 0) ||
                (nl == 8 && strncmp(n, "URIError", 8) == 0) ||
                (nl == 9 && strncmp(n, "EvalError", 9) == 0)) {
                js_pending_new_target = ItemNull;
                js_has_pending_new_target = false;
                Item tn = (Item){.item = s2it(heap_create_name(n, nl))};
                Item msg = (argc > 0 && args) ? args[0] : make_js_undefined();
                return js_new_error_with_name(tn, msg);
            }
            // AggregateError(errors, message)
            if (nl == 14 && strncmp(n, "AggregateError", 14) == 0) {
                js_pending_new_target = ItemNull;
                js_has_pending_new_target = false;
                Item errors = (argc > 0 && args) ? args[0] : js_array_new(0);
                Item msg = (argc > 1 && args) ? args[1] : make_js_undefined();
                return js_new_aggregate_error(errors, msg);
            }

            // Array
            if (nl == 5 && strncmp(n, "Array", 5) == 0) {
                js_pending_new_target = ItemNull;
                js_has_pending_new_target = false;
                if (argc == 0) return js_array_new(0);
                if (argc == 1) return js_array_new_from_item(args[0]);
                Item arr = js_array_new(argc);
                for (int i = 0; i < argc; i++) js_array_push(arr, args[i]);
                return arr;
            }

            // Object
            if (nl == 6 && strncmp(n, "Object", 6) == 0) {
                js_pending_new_target = ItemNull;
                js_has_pending_new_target = false;
                if (argc > 0 && args) return js_to_object(args[0]);
                return js_new_object();
            }
        }

        Item obj = js_constructor_create_object(callee);
        Item result = js_call_function(callee, obj, args, argc);
        // Per ES spec §9.2.2: if constructor returns an Object, use that instead of this
        TypeId rt = get_type_id(result);
        if (rt == LMD_TYPE_MAP || rt == LMD_TYPE_ARRAY || rt == LMD_TYPE_ELEMENT ||
            rt == LMD_TYPE_FUNC || rt == LMD_TYPE_OBJECT || rt == LMD_TYPE_VMAP) {
            return result;
        }
        return obj;
    }
    // Case 2: callee is a class object (MAP with __ctor__ or __instance_proto__)
    if (get_type_id(callee) == LMD_TYPE_MAP) {
        bool own;
        Item class_name = js_map_get_fast(callee.map, "__class_name__", 14, &own);
        Item ctor = js_map_get_fast(callee.map, "__ctor__", 8, &own);
        Item instance_proto = js_map_get_fast(callee.map, "__instance_proto__", 18, &own);
        // Only treat as constructable if it has class metadata
        if (class_name.item == ItemNull.item && ctor.item == ItemNull.item && instance_proto.item == ItemNull.item) {
            // Plain object — not a constructor
            js_pending_new_target = ItemNull;
            js_has_pending_new_target = false;
            Item tn = (Item){.item = s2it(heap_create_name("TypeError", 9))};
            Item msg = (Item){.item = s2it(heap_create_name("is not a constructor", 20))};
            js_throw_value(js_new_error_with_name(tn, msg));
            return ItemNull;
        }
        Item obj = js_new_object();
        // Copy __class_name__ from the class object
        if (class_name.item != ItemNull.item) {
            Item cn_key = (Item){.item = s2it(heap_create_name("__class_name__", 14))};
            js_property_set(obj, cn_key, class_name);
        }
        // v18c: Set constructor property (instance.constructor === Class)
        Item ctor_key = (Item){.item = s2it(heap_create_name("constructor"))};
        js_property_set(obj, ctor_key, callee);
        // Mark constructor as non-enumerable (per ES spec)
        Item ne_ctor_key = (Item){.item = s2it(heap_create_name("__ne_constructor", 16))};
        js_property_set(obj, ne_ctor_key, (Item){.item = b2it(true)});
        // Set __proto__ so instance methods are accessible via prototype chain
        if (instance_proto.item != ItemNull.item && get_type_id(instance_proto) == LMD_TYPE_MAP) {
            js_set_prototype(obj, instance_proto);
        }
        // Call the constructor (__ctor__ property on the class object)
        if (ctor.item != ItemNull.item && get_type_id(ctor) == LMD_TYPE_FUNC) {
            // Set pending new.target for the constructor call
            js_pending_new_target = callee;
            js_has_pending_new_target = true;
            Item result = js_call_function(ctor, obj, args, argc);
            // Per ES spec §9.2.2: if constructor returns an Object, use that instead
            TypeId rt = get_type_id(result);
            if (rt == LMD_TYPE_MAP || rt == LMD_TYPE_ARRAY || rt == LMD_TYPE_ELEMENT ||
                rt == LMD_TYPE_FUNC || rt == LMD_TYPE_OBJECT || rt == LMD_TYPE_VMAP) {
                return result;
            }
        }
        return obj;
    }
    // Not a function or class object — throw TypeError
    {
        js_pending_new_target = ItemNull;
        js_has_pending_new_target = false;
        Item tn = (Item){.item = s2it(heap_create_name("TypeError", 9))};
        Item msg = (Item){.item = s2it(heap_create_name("is not a constructor", 20))};
        js_throw_value(js_new_error_with_name(tn, msg));
        return ItemNull;
    }
}

// A5: Create a new object with pre-built shape for constructor optimization.
// All property slots are pre-allocated as LMD_TYPE_NULL (8-byte pointer-sized)
// and initialized to null. When the constructor body sets this.prop = val,
// js_property_set will find the existing key via hash table and do a fast
// in-place update instead of extending the shape.
extern "C" Item js_new_object_with_shape(const char** prop_names, const int* prop_lens, int count) {
    if (!js_input || count <= 0) return js_new_object();

    Map* m = (Map*)heap_calloc_class(sizeof(Map), LMD_TYPE_MAP, JS_MAP_SIZE_CLASS);
    m->type_id = LMD_TYPE_MAP;

    // Allocate TypeMap
    TypeMap* tm = (TypeMap*)alloc_type(js_input->pool, LMD_TYPE_MAP, sizeof(TypeMap));
    if (!tm) { m->type = &EmptyMap; return (Item){.map = m}; }

    // Build ShapeEntry chain — each slot is LMD_TYPE_NULL (sizeof(void*) = 8 bytes).
    // This gives correct 8-byte spacing so INT, FLOAT, STRING, MAP, FUNC etc.
    // all fit in-place via the NULL→same-byte-size fast path in fn_map_set.
    ShapeEntry* first = NULL;
    ShapeEntry* prev = NULL;
    for (int i = 0; i < count; i++) {
        ShapeEntry* se = (ShapeEntry*)pool_calloc(js_input->pool, sizeof(ShapeEntry) + sizeof(StrView));
        StrView* nv = (StrView*)((char*)se + sizeof(ShapeEntry));
        String* key_str = heap_create_name(prop_names[i], (size_t)prop_lens[i]);
        nv->str = key_str->chars;
        nv->length = key_str->len;
        se->name = nv;
        se->type = type_info[LMD_TYPE_NULL].type;
        se->byte_offset = i * (int)sizeof(void*);  // 8-byte slots
        se->next = NULL;
        if (prev) prev->next = se;
        else first = se;
        prev = se;
    }
    tm->shape = first;
    tm->last = prev;
    tm->length = count;
    tm->byte_size = count * (int)sizeof(void*);

    // Populate hash table
    ShapeEntry* se = first;
    while (se) {
        typemap_hash_insert(tm, se);
        se = se->next;
    }

    // Allocate data buffer (pre-sized, zero-initialized = null pointers)
    int data_size = count * (int)sizeof(void*);
    int data_cap = data_size < 64 ? 64 : data_size;
    m->data = pool_calloc(js_input->pool, data_cap);
    m->data_cap = data_cap;
    m->type = tm;

    return (Item){.map = m};
}

// A5: Create pre-shaped object and set __proto__ from constructor's prototype
extern "C" Item js_constructor_create_object_shaped(Item callee,
    const char** prop_names, const int* prop_lens, int count) {
    Item obj = js_new_object_with_shape(prop_names, prop_lens, count);
    if (get_type_id(callee) == LMD_TYPE_FUNC) {
        JsFunction* fn = (JsFunction*)callee.function;
        if (fn->prototype.item == ItemNull.item) {
            Item proto_key = (Item){.item = s2it(heap_create_name("prototype", 9))};
            js_property_get(callee, proto_key);
        }
        if (fn->prototype.item != ItemNull.item && get_type_id(fn->prototype) == LMD_TYPE_MAP) {
            js_set_prototype(obj, fn->prototype);
        }
    }
    return obj;
}

// P3/P4: Slot-indexed property access for shaped (constructor-created) objects.
// These bypass the hash-table lookup in js_property_get/set by walking the
// ShapeEntry linked list to the N-th slot (O(slot) ≈ O(2-4) for typical classes).
//
// js_get_shaped_slot: read property at slot index → returns correctly boxed Item
// js_set_shaped_slot: write property at slot index, updates ShapeEntry type

extern "C" Item js_get_shaped_slot(Item object, int64_t slot) {
    if (get_type_id(object) != LMD_TYPE_MAP) return ItemNull;
    Map* m = (Map*)object.map;
    TypeMap* tm = (TypeMap*)m->type;
    if (!tm || !tm->shape) return ItemNull;
    ShapeEntry* entry = tm->shape;
    for (int i = 0; i < (int)slot && entry; i++) entry = entry->next;
    if (!entry) return ItemNull;
    return _map_read_field(entry, m->data);
}

extern "C" void js_set_shaped_slot(Item object, int64_t slot, Item value) {
    if (get_type_id(object) != LMD_TYPE_MAP) return;
    Map* m = (Map*)object.map;
    TypeMap* tm = (TypeMap*)m->type;
    if (!tm || !tm->shape) return;
    ShapeEntry* entry = tm->shape;
    for (int i = 0; i < (int)slot && entry; i++) entry = entry->next;
    if (!entry) return;
    void* field_ptr = (char*)m->data + entry->byte_offset;
    TypeId value_type = get_type_id(value);
    TypeId field_type = entry->type->type_id;
    // Store with correct type-aware unboxing (all shaped slots are 8 bytes).
    switch (value_type) {
    case LMD_TYPE_INT:
        *(int64_t*)field_ptr = value.get_int56();
        break;
    case LMD_TYPE_FLOAT:
        *(double*)field_ptr = value.get_double();
        break;
    case LMD_TYPE_MAP: case LMD_TYPE_ELEMENT: case LMD_TYPE_OBJECT:
    case LMD_TYPE_ARRAY: case LMD_TYPE_ARRAY_NUM:
    case LMD_TYPE_RANGE:
        *(Container**)field_ptr = value.container;
        break;
    case LMD_TYPE_STRING: case LMD_TYPE_SYMBOL: case LMD_TYPE_BINARY: {
        String* s = value.get_string();
        *(String**)field_ptr = s;
        break;
    }
    case LMD_TYPE_FUNC: case LMD_TYPE_DECIMAL: case LMD_TYPE_TYPE:
    case LMD_TYPE_PATH: case LMD_TYPE_VMAP:
        *(void**)field_ptr = (void*)(uintptr_t)(value.item & 0x00FFFFFFFFFFFFFFULL);
        break;
    case LMD_TYPE_BOOL:
        *(bool*)field_ptr = value.bool_val;
        break;
    case LMD_TYPE_NULL:
        *(void**)field_ptr = NULL;
        break;
    default:
        // Unexpected type: skip to prevent slot corruption
        log_debug("js_set_shaped_slot: unhandled type %d at slot %d", (int)value_type, (int)slot);
        return;
    }
    // Update ShapeEntry type in-place (NULL→type is the common constructor init path).
    // All shaped slots are 8 bytes so no reshape is needed.
    if (field_type != value_type && value_type != LMD_TYPE_NULL) {
        entry->type = type_info[value_type].type;
    }
}

// Forward declaration for prototype chain support
extern "C" Item js_prototype_lookup(Item object, Item property);

// P10f: Fast property lookup for JS objects.
// Like _map_get but avoids strncmp+strlen overhead by using pre-computed key_len.
// Still uses last-writer-wins since type changes can create duplicate shape entries.
// Also handles spread/nested map entries (field->name == NULL).
static Item js_map_get_fast(Map* m, const char* key_str, int key_len, bool* out_found = nullptr) {
    TypeMap* map_type = (TypeMap*)m->type;
    if (!map_type || !map_type->shape) { if (out_found) *out_found = false; return ItemNull; }

    // A1: Try hash table first for O(1) lookup (covers >99% of JS objects).
    // The hash table uses last-writer-wins via typemap_hash_insert, so the
    // entry in the table always points to the latest ShapeEntry for that name.
    if (map_type->field_count > 0) {
        ShapeEntry* entry = typemap_hash_lookup(map_type, key_str, key_len);
        if (entry) {
            if (out_found) *out_found = true;
            return _map_read_field(entry, m->data);
        }
        // Not found in hash table — may have overflowed (capacity=32).
        // Walk all named fields linearly to catch overflow entries, and also
        // check unnamed (nested/spread) entries.
        ShapeEntry* field = map_type->shape;
        Item overflow_result = ItemNull;
        bool found = false;
        while (field) {
            if (!field->name) {
                Map* nested_map = *(Map**)((char*)m->data + field->byte_offset);
                if (nested_map && nested_map->type_id == LMD_TYPE_MAP) {
                    bool nested_found;
                    Item nested_result = _map_get((TypeMap*)nested_map->type, nested_map->data, (char*)key_str, &nested_found);
                    if (nested_found) { if (out_found) *out_found = true; return nested_result; }
                }
            } else if (field->name->str == key_str ||  // A6: interned pointer match
                       (field->name->length == (size_t)key_len &&
                        memcmp(field->name->str, key_str, key_len) == 0)) {
                overflow_result = _map_read_field(field, m->data);
                found = true;
            }
            field = field->next;
        }
        if (out_found) *out_found = found;
        return overflow_result;
    }

    // Fallback: linear scan for objects without hash table
    ShapeEntry* field = map_type->shape;
    Item result = ItemNull;
    bool found = false;
    while (field) {
        if (!field->name) {
            Map* nested_map = *(Map**)((char*)m->data + field->byte_offset);
            if (nested_map && nested_map->type_id == LMD_TYPE_MAP) {
                bool nested_found;
                Item nested_result = _map_get((TypeMap*)nested_map->type, nested_map->data, (char*)key_str, &nested_found);
                if (nested_found) {
                    result = nested_result;
                    found = true;
                }
            }
        } else if (field->name->str == key_str ||  // A6: interned pointer match
                   (field->name->length == (size_t)key_len &&
                    memcmp(field->name->str, key_str, key_len) == 0)) {
            result = _map_read_field(field, m->data);
            found = true;
        }
        field = field->next;
    }
    if (out_found) *out_found = found;
    return result;
}

// Non-getter data-only property existence check. Used by js_in (the "in" operator)
// to check if a property exists WITHOUT triggering getters.
Item js_map_get_fast_ext(Map* m, const char* key_str, int key_len, bool* out_found) {
    return js_map_get_fast(m, key_str, key_len, out_found);
}

// P10d: Interned __proto__ key — avoid heap_create_name on every prototype lookup.
// Initialized lazily on first use.
static Item js_proto_key_item = {0};
void js_reset_proto_key() { js_proto_key_item = (Item){0}; }
static Item js_get_proto_key() {
    if (js_proto_key_item.item == 0) {
        js_proto_key_item.item = s2it(heap_create_name("__proto__", 9));
    }
    return js_proto_key_item;
}

// Forward declaration for builtin method lookup (extern — used by js_globals.cpp too)
extern "C" Item js_lookup_builtin_method(TypeId type, const char* name, int len);
extern "C" Item js_collection_method(Item obj, int method_id, Item arg1, Item arg2);
static Item js_get_or_create_builtin(int builtin_id, const char* name, int param_count);
static Item js_lookup_constructor_static(const char* ctor_name, int ctor_len,
                                          const char* prop_name, int prop_len);

extern "C" Item js_property_get(Item object, Item key) {
    // Convert Symbol keys to unique string keys for property lookup
    if (js_key_is_symbol(key)) key = js_symbol_to_key(key);
    TypeId type = get_type_id(object);

    if (type == LMD_TYPE_MAP) {
        Map* m = object.map;
        // Check if this is a typed array
        if (js_is_typed_array(object)) {
            if (get_type_id(key) == LMD_TYPE_STRING) {
                String* str_key = it2s(key);
                if (str_key->len == 6 && strncmp(str_key->chars, "length", 6) == 0) {
                    return (Item){.item = i2it(js_typed_array_length(object))};
                }
                if (str_key->len == 10 && strncmp(str_key->chars, "byteLength", 10) == 0) {
                    JsTypedArray* ta = (JsTypedArray*)object.map->data;
                    return (Item){.item = i2it(ta->byte_length)};
                }
                if (str_key->len == 10 && strncmp(str_key->chars, "byteOffset", 10) == 0) {
                    JsTypedArray* ta = (JsTypedArray*)object.map->data;
                    return (Item){.item = i2it(ta->byte_offset)};
                }
                if (str_key->len == 6 && strncmp(str_key->chars, "buffer", 6) == 0) {
                    JsTypedArray* ta = (JsTypedArray*)object.map->data;
                    if (ta->buffer_item) {
                        // Return the original ArrayBuffer Item to preserve identity (===)
                        return (Item){.item = ta->buffer_item};
                    }
                    if (!ta->buffer) {
                        // Create a backing ArrayBuffer lazily for standalone typed arrays
                        JsArrayBuffer* ab = (JsArrayBuffer*)malloc(sizeof(JsArrayBuffer));
                        ab->data = ta->data;  // share the same data pointer
                        ab->byte_length = ta->byte_length;
                        ta->buffer = ab;
                    }
                    return js_arraybuffer_wrap(ta->buffer);
                }
                if (str_key->len == 17 && strncmp(str_key->chars, "BYTES_PER_ELEMENT", 17) == 0) {
                    JsTypedArray* ta = (JsTypedArray*)object.map->data;
                    int bpe = 4;
                    switch (ta->element_type) {
                    case JS_TYPED_INT8: case JS_TYPED_UINT8: bpe = 1; break;
                    case JS_TYPED_INT16: case JS_TYPED_UINT16: bpe = 2; break;
                    case JS_TYPED_INT32: case JS_TYPED_UINT32: case JS_TYPED_FLOAT32: bpe = 4; break;
                    case JS_TYPED_FLOAT64: bpe = 8; break;
                    }
                    return (Item){.item = i2it(bpe)};
                }
                // Non-numeric string key on typed array: check prototype chain, else undefined
                // Try numeric parse: only forward to element access if the key is a valid integer string
                {
                    String* sk = it2s(key);
                    bool is_numeric = sk->len > 0;
                    for (int ni = 0; ni < sk->len; ni++) {
                        if (sk->chars[ni] < '0' || sk->chars[ni] > '9') { is_numeric = false; break; }
                    }
                    if (is_numeric) {
                        return js_typed_array_get(object, key);
                    }
                }
                // Fall through to prototype chain lookup below for methods/other named props
                // Look up prototype chain for typed array methods
                Item ta_proto = js_get_prototype(object);
                if (ta_proto.item != ITEM_NULL) {
                    Item result = js_property_get(ta_proto, key);
                    if (result.item != ITEM_NULL) return result;
                }
                return (Item){.item = ITEM_NULL};
            }
            // Key is not a string (it's an int or other type) — use element access
            return js_typed_array_get(object, key);
        }
        // Check if this is an ArrayBuffer
        if (js_is_arraybuffer(object)) {
            if (get_type_id(key) == LMD_TYPE_STRING) {
                String* str_key = it2s(key);
                if (str_key->len == 10 && strncmp(str_key->chars, "byteLength", 10) == 0) {
                    return (Item){.item = i2it(js_arraybuffer_byte_length(object))};
                }
            }
            return (Item){.item = ITEM_NULL};
        }
        // Check if this is a DataView
        if (js_is_dataview(object)) {
            if (get_type_id(key) == LMD_TYPE_STRING) {
                String* str_key = it2s(key);
                if (str_key->len == 10 && strncmp(str_key->chars, "byteLength", 10) == 0) {
                    JsDataView* dv = (JsDataView*)object.map->data;
                    return (Item){.item = i2it(dv->byte_length)};
                }
                if (str_key->len == 10 && strncmp(str_key->chars, "byteOffset", 10) == 0) {
                    JsDataView* dv = (JsDataView*)object.map->data;
                    return (Item){.item = i2it(dv->byte_offset)};
                }
                if (str_key->len == 6 && strncmp(str_key->chars, "buffer", 6) == 0) {
                    JsDataView* dv = (JsDataView*)object.map->data;
                    if (dv->buffer) {
                        return js_arraybuffer_wrap(dv->buffer);
                    }
                    return (Item){.item = ITEM_NULL};
                }
            }
            return (Item){.item = ITEM_NULL};
        }
        // Check if this is a document proxy object
        if (js_is_document_proxy(object)) {
            return js_document_proxy_get_property(key);
        }
        // Check if this is a DOM node wrapper (indicated by js_dom_type_marker)
        if (js_is_dom_node(object)) {
            return js_dom_get_property(object, key);
        }
        // Check if this is a computed style wrapper
        if (js_is_computed_style_item(object)) {
            return js_computed_style_get_property(object, key);
        }
        // Check if this is a CSSOM wrapper (stylesheet, rule, declaration)
        if (js_is_stylesheet(object)) {
            return js_cssom_stylesheet_get_property(object, key);
        }
        if (js_is_css_rule(object)) {
            return js_cssom_rule_get_property(object, key);
        }
        if (js_is_rule_style_decl(object)) {
            return js_cssom_rule_decl_get_property(object, key);
        }
        // Regular Lambda map (including JS objects)
        // P10f: Use fast lookup with pre-computed key length (memcmp instead of strncmp+strlen)
        // JS semantics: numeric keys are coerced to strings (obj[17] === obj["17"])
        if (get_type_id(key) == LMD_TYPE_INT || get_type_id(key) == LMD_TYPE_FLOAT) {
            char buf[64];
            if (get_type_id(key) == LMD_TYPE_INT) {
                snprintf(buf, sizeof(buf), "%lld", (long long)it2i(key));
            } else {
                double dv = it2d(key);
                // v24: -0.0 should stringify to "0" per ES spec
                if (dv == 0.0) snprintf(buf, sizeof(buf), "0");
                else snprintf(buf, sizeof(buf), "%g", dv);
            }
            key = (Item){.item = s2it(heap_create_name(buf, strlen(buf)))};
        }
        Item result = ItemNull;
        bool own_found = false;
        if (key._type_id == LMD_TYPE_STRING || key._type_id == LMD_TYPE_SYMBOL) {
            const char* key_str = key.get_chars();
            int key_len = (int)key.get_len();
            result = js_map_get_fast(object.map, key_str, key_len, &own_found);
        } else {
            result = map_get(object.map, key);  // fallback for non-string keys
            own_found = (result.item != ItemNull.item);
        }
        if (own_found) {
            // If property was deleted (sentinel), fall through to getter/prototype
            if (js_is_deleted_sentinel(result)) { /* deleted — check getter first */ }
            else return result;
        }
        // String wrapper indexed character access: new String("abc")[0] → "a"
        if (!own_found && key._type_id == LMD_TYPE_STRING) {
            String* sk = it2s(key);
            if (sk && sk->len > 0 && sk->chars[0] >= '0' && sk->chars[0] <= '9') {
                bool is_num = true;
                for (int ni = 0; ni < sk->len; ni++) {
                    if (sk->chars[ni] < '0' || sk->chars[ni] > '9') { is_num = false; break; }
                }
                if (is_num) {
                    bool cn_found = false;
                    Item cn = js_map_get_fast(m, "__class_name__", 14, &cn_found);
                    if (cn_found && get_type_id(cn) == LMD_TYPE_STRING) {
                        String* cn_str = it2s(cn);
                        if (cn_str && cn_str->len == 6 && strncmp(cn_str->chars, "String", 6) == 0) {
                            bool pv_found = false;
                            Item pv = js_map_get_fast(m, "__primitiveValue__", 18, &pv_found);
                            if (pv_found && get_type_id(pv) == LMD_TYPE_STRING) {
                                String* pv_str = it2s(pv);
                                int idx = atoi(sk->chars);
                                if (pv_str && idx >= 0 && idx < (int)pv_str->len) {
                                    char ch[2] = {pv_str->chars[idx], 0};
                                    return (Item){.item = s2it(heap_create_name(ch, 1))};
                                }
                            }
                        }
                    }
                }
            }
        }
        // Own getter check: check for __get_<propName> on THIS object BEFORE prototype chain.
        // This ensures own accessors take priority over inherited data properties (ES spec §9.1.8).
        // Skip getter check only for __get_/__set_ keys (to prevent infinite recursion).
        if (key._type_id == LMD_TYPE_STRING) {
            String* str_key = it2s(key);
            bool check_getter = (str_key->len < 128 && str_key->len > 0 &&
                !(str_key->len > 6 && (strncmp(str_key->chars, "__get_", 6) == 0 ||
                                        strncmp(str_key->chars, "__set_", 6) == 0)));
            if (check_getter) {
                char getter_key[256];
                snprintf(getter_key, sizeof(getter_key), "__get_%.*s", (int)str_key->len, str_key->chars);
                Item gk = (Item){.item = s2it(heap_create_name(getter_key, strlen(getter_key)))};
                bool gk_found = false;
                Item getter = js_map_get_fast(object.map, getter_key, (int)strlen(getter_key), &gk_found);
                if (gk_found && get_type_id(getter) == LMD_TYPE_FUNC) {
                    // Invoke getter with this = object (0 args)
                    return js_call_function(getter, object, NULL, 0);
                }
            }
        }
        // Prototype chain fallback: if property not found on own object, walk __proto__
        result = js_prototype_lookup(object, key);
        if (result.item != ItemNull.item) return result;
        // Getter on prototype chain: check for __get_<propName> inherited from prototype
        if (key._type_id == LMD_TYPE_STRING) {
            String* str_key = it2s(key);
            bool check_getter = (str_key->len < 128 && str_key->len > 0 &&
                !(str_key->len > 6 && (strncmp(str_key->chars, "__get_", 6) == 0 ||
                                        strncmp(str_key->chars, "__set_", 6) == 0)));
            if (check_getter) {
                char getter_key[256];
                snprintf(getter_key, sizeof(getter_key), "__get_%.*s", (int)str_key->len, str_key->chars);
                Item gk = (Item){.item = s2it(heap_create_name(getter_key, strlen(getter_key)))};
                Item getter = js_prototype_lookup(object, gk);
                if (getter.item != ItemNull.item && get_type_id(getter) == LMD_TYPE_FUNC) {
                    return js_call_function(getter, object, NULL, 0);
                }
            }
        }
        // Property not found — check for built-in methods (Object.prototype)
        // Also check Array/String methods for prototype objects
        if (get_type_id(key) == LMD_TYPE_STRING) {
            String* str_key = it2s(key);
            Item builtin = ItemNull;
            // v18o: Check __class_name__ FIRST for type-specific prototype resolution
            // This ensures Number.prototype.toString returns Number's toString, not Object's.
            {
                bool cn_own = false;
                Item cn = js_map_get_fast(object.map, "__class_name__", 14, &cn_own);
                if (cn_own && get_type_id(cn) == LMD_TYPE_STRING) {
                    String* cn_str = it2s(cn);
                    if (cn_str && cn_str->len == 6 && strncmp(cn_str->chars, "String", 6) == 0) {
                        builtin = js_lookup_builtin_method(LMD_TYPE_STRING, str_key->chars, str_key->len);
                        if (builtin.item != ItemNull.item) return builtin;
                    } else if (cn_str && cn_str->len == 6 && strncmp(cn_str->chars, "Number", 6) == 0) {
                        // Number prototype methods: toString, valueOf, toFixed, toPrecision, toExponential
                        if (str_key->len == 8 && strncmp(str_key->chars, "toString", 8) == 0) {
                            return js_get_or_create_builtin(JS_BUILTIN_NUM_TO_STRING, "toString", 1);
                        }
                        if (str_key->len == 7 && strncmp(str_key->chars, "valueOf", 7) == 0) {
                            return js_get_or_create_builtin(JS_BUILTIN_NUM_VALUE_OF, "valueOf", 0);
                        }
                        if (str_key->len == 7 && strncmp(str_key->chars, "toFixed", 7) == 0) {
                            return js_get_or_create_builtin(JS_BUILTIN_NUM_TO_FIXED, "toFixed", 1);
                        }
                        if (str_key->len == 11 && strncmp(str_key->chars, "toPrecision", 11) == 0) {
                            return js_get_or_create_builtin(JS_BUILTIN_NUM_TO_PRECISION, "toPrecision", 1);
                        }
                        if (str_key->len == 13 && strncmp(str_key->chars, "toExponential", 13) == 0) {
                            return js_get_or_create_builtin(JS_BUILTIN_NUM_TO_EXPONENTIAL, "toExponential", 1);
                        }
                    } else if (cn_str && cn_str->len == 4 && strncmp(cn_str->chars, "Date", 4) == 0) {
                        // v45: Date prototype methods — make Date methods visible as properties
                        struct { const char* name; int len; int id; int pc; } date_methods[] = {
                            {"getTime", 7, JS_BUILTIN_DATE_GET_TIME, 0},
                            {"getFullYear", 11, JS_BUILTIN_DATE_GET_FULL_YEAR, 0},
                            {"getMonth", 8, JS_BUILTIN_DATE_GET_MONTH, 0},
                            {"getDate", 7, JS_BUILTIN_DATE_GET_DATE, 0},
                            {"getHours", 8, JS_BUILTIN_DATE_GET_HOURS, 0},
                            {"getMinutes", 10, JS_BUILTIN_DATE_GET_MINUTES, 0},
                            {"getSeconds", 10, JS_BUILTIN_DATE_GET_SECONDS, 0},
                            {"getMilliseconds", 15, JS_BUILTIN_DATE_GET_MILLISECONDS, 0},
                            {"toISOString", 11, JS_BUILTIN_DATE_TO_ISO_STRING, 0},
                            {"toJSON", 6, JS_BUILTIN_DATE_TO_JSON, 0},
                            {"toUTCString", 11, JS_BUILTIN_DATE_TO_UTC_STRING, 0},
                            {"toDateString", 12, JS_BUILTIN_DATE_TO_DATE_STRING, 0},
                            {"toTimeString", 12, JS_BUILTIN_DATE_TO_TIME_STRING, 0},
                            {"toString", 8, JS_BUILTIN_DATE_TO_STRING, 0},
                            {"toLocaleDateString", 18, JS_BUILTIN_DATE_TO_LOCALE_DATE_STRING, 0},
                            {"valueOf", 7, JS_BUILTIN_DATE_VALUE_OF, 0},
                            {"getDay", 6, JS_BUILTIN_DATE_GET_DAY, 0},
                            {"getUTCFullYear", 14, JS_BUILTIN_DATE_GET_UTC_FULL_YEAR, 0},
                            {"getUTCMonth", 11, JS_BUILTIN_DATE_GET_UTC_MONTH, 0},
                            {"getUTCDate", 10, JS_BUILTIN_DATE_GET_UTC_DATE, 0},
                            {"getUTCHours", 11, JS_BUILTIN_DATE_GET_UTC_HOURS, 0},
                            {"getUTCMinutes", 13, JS_BUILTIN_DATE_GET_UTC_MINUTES, 0},
                            {"getUTCSeconds", 13, JS_BUILTIN_DATE_GET_UTC_SECONDS, 0},
                            {"getUTCMilliseconds", 18, JS_BUILTIN_DATE_GET_UTC_MILLISECONDS, 0},
                            {"getUTCDay", 9, JS_BUILTIN_DATE_GET_UTC_DAY, 0},
                            {"getTimezoneOffset", 17, JS_BUILTIN_DATE_GET_TIMEZONE_OFFSET, 0},
                            {"setTime", 7, JS_BUILTIN_DATE_SET_TIME, 1},
                            {"setFullYear", 11, JS_BUILTIN_DATE_SET_FULL_YEAR, 3},
                            {"setMonth", 8, JS_BUILTIN_DATE_SET_MONTH, 2},
                            {"setDate", 7, JS_BUILTIN_DATE_SET_DATE, 1},
                            {"setHours", 8, JS_BUILTIN_DATE_SET_HOURS, 4},
                            {"setMinutes", 10, JS_BUILTIN_DATE_SET_MINUTES, 3},
                            {"setSeconds", 10, JS_BUILTIN_DATE_SET_SECONDS, 2},
                            {"setMilliseconds", 15, JS_BUILTIN_DATE_SET_MILLISECONDS, 1},
                            {"setUTCFullYear", 14, JS_BUILTIN_DATE_SET_UTC_FULL_YEAR, 3},
                            {"setUTCMonth", 11, JS_BUILTIN_DATE_SET_UTC_MONTH, 2},
                            {"setUTCDate", 10, JS_BUILTIN_DATE_SET_UTC_DATE, 1},
                            {"setUTCHours", 11, JS_BUILTIN_DATE_SET_UTC_HOURS, 4},
                            {"setUTCMinutes", 13, JS_BUILTIN_DATE_SET_UTC_MINUTES, 3},
                            {"setUTCSeconds", 13, JS_BUILTIN_DATE_SET_UTC_SECONDS, 2},
                            {"setUTCMilliseconds", 18, JS_BUILTIN_DATE_SET_UTC_MILLISECONDS, 1},
                            {NULL, 0, 0, 0}
                        };
                        for (int i = 0; date_methods[i].name; i++) {
                            if ((int)str_key->len == date_methods[i].len &&
                                strncmp(str_key->chars, date_methods[i].name, str_key->len) == 0) {
                                return js_get_or_create_builtin(date_methods[i].id, date_methods[i].name, date_methods[i].pc);
                            }
                        }
                    } else if (cn_str && cn_str->len == 6 && strncmp(cn_str->chars, "RegExp", 6) == 0) {
                        // v46: RegExp prototype methods
                        if (str_key->len == 4 && strncmp(str_key->chars, "exec", 4) == 0)
                            return js_get_or_create_builtin(JS_BUILTIN_REGEXP_EXEC, "exec", 1);
                        if (str_key->len == 4 && strncmp(str_key->chars, "test", 4) == 0)
                            return js_get_or_create_builtin(JS_BUILTIN_REGEXP_TEST, "test", 1);
                        if (str_key->len == 8 && strncmp(str_key->chars, "toString", 8) == 0)
                            return js_get_or_create_builtin(JS_BUILTIN_REGEXP_TO_STRING, "toString", 0);
                        // v83: Symbol-keyed methods
                        if (str_key->len == 7 && strncmp(str_key->chars, "__sym_7", 7) == 0)
                            return js_get_or_create_builtin(JS_BUILTIN_REGEXP_SYMBOL_MATCH, "[Symbol.match]", 1);
                        if (str_key->len == 7 && strncmp(str_key->chars, "__sym_8", 7) == 0)
                            return js_get_or_create_builtin(JS_BUILTIN_REGEXP_SYMBOL_REPLACE, "[Symbol.replace]", 2);
                        if (str_key->len == 7 && strncmp(str_key->chars, "__sym_9", 7) == 0)
                            return js_get_or_create_builtin(JS_BUILTIN_REGEXP_SYMBOL_SEARCH, "[Symbol.search]", 1);
                        if (str_key->len == 8 && strncmp(str_key->chars, "__sym_10", 8) == 0)
                            return js_get_or_create_builtin(JS_BUILTIN_REGEXP_SYMBOL_SPLIT, "[Symbol.split]", 2);
                    } else if (cn_str && cn_str->len == 5 && strncmp(cn_str->chars, "Array", 5) == 0) {
                        // Array.prototype methods: resolve via Array builtin table
                        builtin = js_lookup_builtin_method(LMD_TYPE_ARRAY, str_key->chars, (int)str_key->len);
                        if (builtin.item != ItemNull.item) return builtin;
                    } else if (cn_str && cn_str->len == 7 && strncmp(cn_str->chars, "Boolean", 7) == 0) {
                        // Boolean wrapper: toString/valueOf resolve to boolean-specific builtins
                        builtin = js_lookup_builtin_method(LMD_TYPE_BOOL, str_key->chars, (int)str_key->len);
                        if (builtin.item != ItemNull.item) return builtin;
                    }
                }
            }
            // Check Object.prototype methods (applies to all objects)
            builtin = js_lookup_builtin_method(LMD_TYPE_MAP, str_key->chars, str_key->len);
            if (builtin.item != ItemNull.item) return builtin;
            // Default: check Array before String (for Array.prototype and array-like objects)
            builtin = js_lookup_builtin_method(LMD_TYPE_ARRAY, str_key->chars, str_key->len);
            if (builtin.item != ItemNull.item) return builtin;
            builtin = js_lookup_builtin_method(LMD_TYPE_STRING, str_key->chars, str_key->len);
            if (builtin.item != ItemNull.item) return builtin;
            // Also check Function methods (for Function.prototype)
            builtin = js_lookup_builtin_method(LMD_TYPE_FUNC, str_key->chars, str_key->len);
            if (builtin.item != ItemNull.item) return builtin;
            // v18c: .constructor fallback — return appropriate constructor when not found
            if (str_key->len == 11 && strncmp(str_key->chars, "constructor", 11) == 0) {
                // Check if object has __class_name__ to determine its type
                bool cn_own = false;
                Item cn = js_map_get_fast(object.map, "__class_name__", 14, &cn_own);
                if (cn_own && get_type_id(cn) == LMD_TYPE_STRING) {
                    return js_get_constructor(cn);
                }
                // Plain object — return Object constructor
                Item obj_name = (Item){.item = s2it(heap_create_name("Object", 6))};
                return js_get_constructor(obj_name);
            }
            // v55: Collection (Set/Map) method resolution via Symbol.iterator and named methods
            {
                JsCollectionData* cd = js_get_collection_data(object);
                if (cd) {
                    // Symbol.iterator → Set: values, Map: entries
                    if (str_key->len == 7 && strncmp(str_key->chars, "__sym_1", 7) == 0) {
                        if (cd->type == JS_COLLECTION_SET)
                            return js_get_or_create_builtin(JS_BUILTIN_SET_VALUES, "[Symbol.iterator]", 0);
                        else
                            return js_get_or_create_builtin(JS_BUILTIN_MAP_ENTRIES, "[Symbol.iterator]", 0);
                    }
                    // Named iterator methods
                    if (str_key->len == 6 && strncmp(str_key->chars, "values", 6) == 0) {
                        return js_get_or_create_builtin(
                            cd->type == JS_COLLECTION_SET ? JS_BUILTIN_SET_VALUES : JS_BUILTIN_MAP_VALUES,
                            "values", 0);
                    }
                    if (str_key->len == 4 && strncmp(str_key->chars, "keys", 4) == 0) {
                        return js_get_or_create_builtin(
                            cd->type == JS_COLLECTION_SET ? JS_BUILTIN_SET_KEYS : JS_BUILTIN_MAP_KEYS,
                            "keys", 0);
                    }
                    if (str_key->len == 7 && strncmp(str_key->chars, "entries", 7) == 0) {
                        return js_get_or_create_builtin(
                            cd->type == JS_COLLECTION_SET ? JS_BUILTIN_SET_ENTRIES : JS_BUILTIN_MAP_ENTRIES,
                            "entries", 0);
                    }
                }
            }
        }
        return make_js_undefined();
    } else if (type == LMD_TYPE_ELEMENT) {
        return elmt_get(object.element, key);
    } else if (type == LMD_TYPE_ARRAY) {
        // Array index access
        if (get_type_id(key) == LMD_TYPE_STRING) {
            String* str_key = it2s(key);
            // Check for "length" property
            if (str_key->len == 6 && strncmp(str_key->chars, "length", 6) == 0) {
                return (Item){.item = i2it(object.array->length)};
            }
            // Only allow numeric string keys for array index access
            if (str_key->len == 0 || (str_key->chars[0] < '0' || str_key->chars[0] > '9')) {
                // v25: check companion map for custom properties first
                if (object.array->extra != 0) {
                    Map* pm = (Map*)(uintptr_t)object.array->extra;
                    bool pm_found = false;
                    Item pm_val = js_map_get_fast_ext(pm, str_key->chars, (int)str_key->len, &pm_found);
                    if (pm_found && !js_is_deleted_sentinel(pm_val)) return pm_val;
                }
                // v18c: .constructor for arrays → Array constructor
                if (str_key->len == 11 && strncmp(str_key->chars, "constructor", 11) == 0) {
                    Item arr_name = (Item){.item = s2it(heap_create_name("Array", 5))};
                    return js_get_constructor(arr_name);
                }
                // v83: __proto__ for arrays → Array.prototype
                if (str_key->len == 9 && strncmp(str_key->chars, "__proto__", 9) == 0) {
                    return js_get_prototype_of(object);
                }
                // Symbol.iterator → values method (Symbol.iterator has well-known ID=1, key "__sym_1")
                if (str_key->len == 7 && strncmp(str_key->chars, "__sym_1", 7) == 0) {
                    return js_lookup_builtin_method(LMD_TYPE_ARRAY, "values", 6);
                }
                // Check built-in array methods (push, slice, concat, etc.)
                Item builtin = js_lookup_builtin_method(LMD_TYPE_ARRAY, str_key->chars, str_key->len);
                if (builtin.item != ItemNull.item) return builtin;
                // Check Object.prototype methods (toString, hasOwnProperty, etc.)
                builtin = js_lookup_builtin_method(LMD_TYPE_MAP, str_key->chars, str_key->len);
                if (builtin.item != ItemNull.item) return builtin;
                return make_js_undefined();
            }
        }
        // Numeric index access
        int idx = (int)js_get_number(key);
        if (idx >= 0 && idx < object.array->length) {
            // check for accessor (getter) on companion map
            if (object.array->extra != 0) {
                char gk[64];
                snprintf(gk, sizeof(gk), "__get_%d", idx);
                Map* props = (Map*)(uintptr_t)object.array->extra;
                bool gk_found = false;
                Item getter = js_map_get_fast(props, gk, (int)strlen(gk), &gk_found);
                if (gk_found && get_type_id(getter) == LMD_TYPE_FUNC) {
                    return js_call_function(getter, object, NULL, 0);
                }
            }
            // v25: check for deleted sentinel (array hole) — return undefined
            if (object.array->items[idx].item == JS_DELETED_SENTINEL_VAL) {
                return make_js_undefined();
            }
            return object.array->items[idx];
        }
        return make_js_undefined();
    } else if (type == LMD_TYPE_STRING) {
        // String character access: str[index]
        String* str = it2s(object);
        if (get_type_id(key) == LMD_TYPE_STRING) {
            String* str_key = it2s(key);
            if (str_key->len == 6 && strncmp(str_key->chars, "length", 6) == 0) {
                return (Item){.item = i2it(str->len)};
            }
            // v20: .constructor for strings → String constructor
            if (str_key->len == 11 && strncmp(str_key->chars, "constructor", 11) == 0) {
                Item str_name = (Item){.item = s2it(heap_create_name("String", 6))};
                return js_get_constructor(str_name);
            }
            // v83: __proto__ for strings → String.prototype
            if (str_key->len == 9 && strncmp(str_key->chars, "__proto__", 9) == 0) {
                return js_get_prototype_of(object);
            }
            // Symbol.iterator → string iterator factory (Symbol.iterator has well-known ID=1, key "__sym_1")
            if (str_key->len == 7 && strncmp(str_key->chars, "__sym_1", 7) == 0) {
                return js_get_or_create_builtin(JS_BUILTIN_STRING_ITER, "[Symbol.iterator]", 0);
            }
            // v20: Check string builtin methods before falling through to index access
            Item builtin = js_lookup_builtin_method(LMD_TYPE_STRING, str_key->chars, str_key->len);
            if (builtin.item != ItemNull.item) return builtin;
            // Non-numeric, non-method string key: check if it's a valid numeric index
            bool is_numeric = str_key->len > 0;
            for (int i = 0; i < str_key->len; i++) {
                if (str_key->chars[i] < '0' || str_key->chars[i] > '9') { is_numeric = false; break; }
            }
            if (!is_numeric) return make_js_undefined();
        }
        int idx = (int)js_get_number(key);
        if (idx >= 0) {
            // Use item_at for proper UTF-8 codepoint indexing
            return item_at(object, (int64_t)idx);
        }
        return make_js_undefined();
    }

    // Function: reading .prototype, .length, .name, .call, .apply, .bind properties
    if (type == LMD_TYPE_FUNC) {
        JsFunction* fn = (JsFunction*)object.function;
        if (get_type_id(key) == LMD_TYPE_STRING) {
            String* str_key = it2s(key);
            // v23: Check properties_map FIRST for overridden/deleted .name/.length/.prototype
            // This enables Object.defineProperty and delete to work correctly on function
            // virtual properties. properties_map takes priority over struct-based values.
            if (fn->properties_map.item != 0 && get_type_id(fn->properties_map) == LMD_TYPE_MAP) {
                bool pm_found = false;
                Item pm_val = js_map_get_fast_ext(fn->properties_map.map, str_key->chars, (int)str_key->len, &pm_found);
                if (pm_found) {
                    if (js_is_deleted_sentinel(pm_val)) return make_js_undefined();
                    return pm_val;
                }
            }
            // v83: __proto__ for function objects → return Function.prototype
            if (str_key->len == 9 && strncmp(str_key->chars, "__proto__", 9) == 0) {
                return js_get_prototype_of(object);
            }
            // .prototype — lazy initialization (skip for global builtin wrappers like parseInt)
            if (str_key->len == 9 && strncmp(str_key->chars, "prototype", 9) == 0) {
                if (fn->builtin_id == -2) return make_js_undefined(); // global builtins have no prototype
                if (fn->prototype.item == ItemNull.item) {
                    fn->prototype = js_new_object();
                    heap_register_gc_root(&fn->prototype.item);
                    // v18o: Set __class_name__ on built-in constructor prototypes
                    // This enables type-specific builtin method resolution on MAPs.
                    if (fn->name) {
                        const char* nm = fn->name->chars;
                        int nl = (int)fn->name->len;
                        // Set __class_name__ for known constructors
                        bool needs_class_name = 
                            (nl == 5 && strncmp(nm, "Array", 5) == 0) ||
                            (nl == 6 && strncmp(nm, "String", 6) == 0) ||
                            (nl == 6 && strncmp(nm, "Number", 6) == 0) ||
                            (nl == 7 && strncmp(nm, "Boolean", 7) == 0) ||
                            (nl == 6 && strncmp(nm, "Object", 6) == 0) ||
                            (nl == 8 && strncmp(nm, "Function", 8) == 0) ||
                            (nl == 4 && strncmp(nm, "Date", 4) == 0) ||
                            (nl == 6 && strncmp(nm, "RegExp", 6) == 0);
                        if (needs_class_name) {
                            Item cnk = (Item){.item = s2it(heap_create_name("__class_name__", 14))};
                            Item cnv = (Item){.item = s2it(heap_create_name(nm, nl))};
                            js_property_set(fn->prototype, cnk, cnv);
                            // v26: mark as prototype object for builtin method enumeration
                            Item ipk = (Item){.item = s2it(heap_create_name("__is_proto__", 12))};
                            js_property_set(fn->prototype, ipk, (Item){.item = b2it(true)});
                        }
                        // Array.prototype.length = 0 (per spec, Array.prototype is an Array exotic object)
                        if (nl == 5 && strncmp(nm, "Array", 5) == 0) {
                            Item lk = (Item){.item = s2it(heap_create_name("length", 6))};
                            js_property_set(fn->prototype, lk, js_make_number(0));
                        }
                        // Boolean.prototype.[[BooleanData]] = false (ES spec 20.3.4)
                        if (nl == 7 && strncmp(nm, "Boolean", 7) == 0) {
                            Item pvk = (Item){.item = s2it(heap_create_name("__primitiveValue__", 18))};
                            js_property_set(fn->prototype, pvk, (Item){.item = b2it(false)});
                        }
                        // Number.prototype.[[NumberData]] = 0 (ES spec 21.1.4)
                        if (nl == 6 && strncmp(nm, "Number", 6) == 0) {
                            Item pvk = (Item){.item = s2it(heap_create_name("__primitiveValue__", 18))};
                            js_property_set(fn->prototype, pvk, js_make_number(0));
                        }
                        // String.prototype.[[StringData]] = "" (ES spec 22.1.4)
                        if (nl == 6 && strncmp(nm, "String", 6) == 0) {
                            Item pvk = (Item){.item = s2it(heap_create_name("__primitiveValue__", 18))};
                            js_property_set(fn->prototype, pvk, (Item){.item = s2it(heap_create_name("", 0))});
                        }
                        // v18g: For error constructors, set name and message on prototype
                        bool is_error = (nl == 5 && strncmp(nm, "Error", 5) == 0) ||
                            (nl == 9 && strncmp(nm, "TypeError", 9) == 0) ||
                            (nl == 10 && strncmp(nm, "RangeError", 10) == 0) ||
                            (nl == 11 && strncmp(nm, "SyntaxError", 11) == 0) ||
                            (nl == 14 && strncmp(nm, "ReferenceError", 14) == 0) ||
                            (nl == 8 && strncmp(nm, "URIError", 8) == 0) ||
                            (nl == 9 && strncmp(nm, "EvalError", 9) == 0) ||
                            (nl == 14 && strncmp(nm, "AggregateError", 14) == 0);
                        if (is_error) {
                            Item nk = (Item){.item = s2it(heap_create_name("name", 4))};
                            Item nv = (Item){.item = s2it(heap_create_name(nm, nl))};
                            js_property_set(fn->prototype, nk, nv);
                            js_mark_non_enumerable(fn->prototype, nk);
                            Item mk = (Item){.item = s2it(heap_create_name("message", 7))};
                            Item mv = (Item){.item = s2it(heap_create_name("", 0))};
                            js_property_set(fn->prototype, mk, mv);
                            js_mark_non_enumerable(fn->prototype, mk);
                            // Set Error.prototype.toString to generic Error toString builtin
                            Item ts_key = (Item){.item = s2it(heap_create_name("toString", 8))};
                            Item ts_fn = js_get_or_create_builtin(JS_BUILTIN_ERR_TO_STRING, "toString", 0);
                            js_property_set(fn->prototype, ts_key, ts_fn);
                            js_mark_non_enumerable(fn->prototype, ts_key);
                            // For error subclasses, set __proto__ to Error.prototype
                            if (!(nl == 5 && strncmp(nm, "Error", 5) == 0)) {
                                Item err_name = (Item){.item = s2it(heap_create_name("Error", 5))};
                                Item err_ctor = js_get_constructor(err_name);
                                if (err_ctor.item != ItemNull.item) {
                                    Item pk = (Item){.item = s2it(heap_create_name("prototype", 9))};
                                    Item err_proto = js_property_get(err_ctor, pk);
                                    if (err_proto.item != ItemNull.item && get_type_id(err_proto) == LMD_TYPE_MAP) {
                                        js_set_prototype(fn->prototype, err_proto);
                                    }
                                }
                            }
                        }
                        // v41: Set Symbol.toStringTag on Map/Set/WeakMap/WeakSet prototypes
                        bool needs_tostring_tag =
                            (nl == 3 && strncmp(nm, "Map", 3) == 0) ||
                            (nl == 3 && strncmp(nm, "Set", 3) == 0) ||
                            (nl == 7 && strncmp(nm, "WeakMap", 7) == 0) ||
                            (nl == 7 && strncmp(nm, "WeakSet", 7) == 0);
                        if (needs_tostring_tag) {
                            Item tag_key = (Item){.item = s2it(heap_create_name("__sym_4", 7))};
                            Item tag_val = (Item){.item = s2it(heap_create_name(nm, nl))};
                            js_property_set(fn->prototype, tag_key, tag_val);
                        }
                        // v76: Populate Map/Set prototype methods for test262 compliance
                        if (nl == 3 && strncmp(nm, "Map", 3) == 0) {
                            struct { const char* name; int len; int bid; int pc; } methods[] = {
                                {"set", 3, JS_BUILTIN_MAP_SET, 2},
                                {"get", 3, JS_BUILTIN_MAP_GET, 1},
                                {"has", 3, JS_BUILTIN_MAP_HAS, 1},
                                {"delete", 6, JS_BUILTIN_MAP_DELETE, 1},
                                {"clear", 5, JS_BUILTIN_MAP_CLEAR, 0},
                                {"forEach", 7, JS_BUILTIN_MAP_FOREACH, 1},
                                {"keys", 4, JS_BUILTIN_MAP_KEYS, 0},
                                {"values", 6, JS_BUILTIN_MAP_VALUES, 0},
                                {"entries", 7, JS_BUILTIN_MAP_ENTRIES, 0},
                                {NULL, 0, 0, 0}
                            };
                            for (int mi = 0; methods[mi].name; mi++) {
                                Item mk = (Item){.item = s2it(heap_create_name(methods[mi].name, methods[mi].len))};
                                Item mf = js_get_or_create_builtin(methods[mi].bid, methods[mi].name, methods[mi].pc);
                                js_property_set(fn->prototype, mk, mf);
                                js_mark_non_enumerable(fn->prototype, mk);
                            }
                            // Symbol.iterator = entries
                            Item si_key = (Item){.item = s2it(heap_create_name("__sym_1", 7))};
                            Item si_fn = js_get_or_create_builtin(JS_BUILTIN_MAP_ENTRIES, "[Symbol.iterator]", 0);
                            js_property_set(fn->prototype, si_key, si_fn);
                            js_mark_non_enumerable(fn->prototype, si_key);
                        }
                        if (nl == 3 && strncmp(nm, "Set", 3) == 0) {
                            struct { const char* name; int len; int bid; int pc; } methods[] = {
                                {"add", 3, JS_BUILTIN_SET_ADD, 1},
                                {"has", 3, JS_BUILTIN_SET_HAS, 1},
                                {"delete", 6, JS_BUILTIN_SET_DELETE, 1},
                                {"clear", 5, JS_BUILTIN_SET_CLEAR, 0},
                                {"forEach", 7, JS_BUILTIN_SET_FOREACH, 1},
                                {"keys", 4, JS_BUILTIN_SET_KEYS, 0},
                                {"values", 6, JS_BUILTIN_SET_VALUES, 0},
                                {"entries", 7, JS_BUILTIN_SET_ENTRIES, 0},
                                {"intersection", 12, JS_BUILTIN_SET_INTERSECTION, 1},
                                {"union", 5, JS_BUILTIN_SET_UNION, 1},
                                {"difference", 10, JS_BUILTIN_SET_DIFFERENCE, 1},
                                {"symmetricDifference", 19, JS_BUILTIN_SET_SYM_DIFF, 1},
                                {"isSubsetOf", 10, JS_BUILTIN_SET_IS_SUBSET, 1},
                                {"isSupersetOf", 12, JS_BUILTIN_SET_IS_SUPERSET, 1},
                                {"isDisjointFrom", 14, JS_BUILTIN_SET_IS_DISJOINT, 1},
                                {NULL, 0, 0, 0}
                            };
                            for (int mi = 0; methods[mi].name; mi++) {
                                Item mk = (Item){.item = s2it(heap_create_name(methods[mi].name, methods[mi].len))};
                                Item mf = js_get_or_create_builtin(methods[mi].bid, methods[mi].name, methods[mi].pc);
                                js_property_set(fn->prototype, mk, mf);
                                js_mark_non_enumerable(fn->prototype, mk);
                            }
                            // Symbol.iterator = values
                            Item si_key = (Item){.item = s2it(heap_create_name("__sym_1", 7))};
                            Item si_fn = js_get_or_create_builtin(JS_BUILTIN_SET_VALUES, "[Symbol.iterator]", 0);
                            js_property_set(fn->prototype, si_key, si_fn);
                            js_mark_non_enumerable(fn->prototype, si_key);
                        }
                        // v82: Populate Date.prototype methods for test262 compliance
                        if (nl == 4 && strncmp(nm, "Date", 4) == 0) {
                            struct { const char* name; int len; int bid; int pc; } methods[] = {
                                {"getTime", 7, JS_BUILTIN_DATE_GET_TIME, 0},
                                {"getFullYear", 11, JS_BUILTIN_DATE_GET_FULL_YEAR, 0},
                                {"getMonth", 8, JS_BUILTIN_DATE_GET_MONTH, 0},
                                {"getDate", 7, JS_BUILTIN_DATE_GET_DATE, 0},
                                {"getHours", 8, JS_BUILTIN_DATE_GET_HOURS, 0},
                                {"getMinutes", 10, JS_BUILTIN_DATE_GET_MINUTES, 0},
                                {"getSeconds", 10, JS_BUILTIN_DATE_GET_SECONDS, 0},
                                {"getMilliseconds", 15, JS_BUILTIN_DATE_GET_MILLISECONDS, 0},
                                {"getDay", 6, JS_BUILTIN_DATE_GET_DAY, 0},
                                {"getUTCFullYear", 14, JS_BUILTIN_DATE_GET_UTC_FULL_YEAR, 0},
                                {"getUTCMonth", 11, JS_BUILTIN_DATE_GET_UTC_MONTH, 0},
                                {"getUTCDate", 10, JS_BUILTIN_DATE_GET_UTC_DATE, 0},
                                {"getUTCHours", 11, JS_BUILTIN_DATE_GET_UTC_HOURS, 0},
                                {"getUTCMinutes", 13, JS_BUILTIN_DATE_GET_UTC_MINUTES, 0},
                                {"getUTCSeconds", 13, JS_BUILTIN_DATE_GET_UTC_SECONDS, 0},
                                {"getUTCMilliseconds", 18, JS_BUILTIN_DATE_GET_UTC_MILLISECONDS, 0},
                                {"getUTCDay", 9, JS_BUILTIN_DATE_GET_UTC_DAY, 0},
                                {"getTimezoneOffset", 17, JS_BUILTIN_DATE_GET_TIMEZONE_OFFSET, 0},
                                {"setTime", 7, JS_BUILTIN_DATE_SET_TIME, 1},
                                {"setFullYear", 11, JS_BUILTIN_DATE_SET_FULL_YEAR, 3},
                                {"setMonth", 8, JS_BUILTIN_DATE_SET_MONTH, 2},
                                {"setDate", 7, JS_BUILTIN_DATE_SET_DATE, 1},
                                {"setHours", 8, JS_BUILTIN_DATE_SET_HOURS, 4},
                                {"setMinutes", 10, JS_BUILTIN_DATE_SET_MINUTES, 3},
                                {"setSeconds", 10, JS_BUILTIN_DATE_SET_SECONDS, 2},
                                {"setMilliseconds", 15, JS_BUILTIN_DATE_SET_MILLISECONDS, 1},
                                {"setUTCFullYear", 14, JS_BUILTIN_DATE_SET_UTC_FULL_YEAR, 3},
                                {"setUTCMonth", 11, JS_BUILTIN_DATE_SET_UTC_MONTH, 2},
                                {"setUTCDate", 10, JS_BUILTIN_DATE_SET_UTC_DATE, 1},
                                {"setUTCHours", 11, JS_BUILTIN_DATE_SET_UTC_HOURS, 4},
                                {"setUTCMinutes", 13, JS_BUILTIN_DATE_SET_UTC_MINUTES, 3},
                                {"setUTCSeconds", 13, JS_BUILTIN_DATE_SET_UTC_SECONDS, 2},
                                {"setUTCMilliseconds", 18, JS_BUILTIN_DATE_SET_UTC_MILLISECONDS, 1},
                                {"toISOString", 11, JS_BUILTIN_DATE_TO_ISO_STRING, 0},
                                {"toJSON", 6, JS_BUILTIN_DATE_TO_JSON, 1},
                                {"toUTCString", 11, JS_BUILTIN_DATE_TO_UTC_STRING, 0},
                                {"toDateString", 12, JS_BUILTIN_DATE_TO_DATE_STRING, 0},
                                {"toTimeString", 12, JS_BUILTIN_DATE_TO_TIME_STRING, 0},
                                {"toString", 8, JS_BUILTIN_DATE_TO_STRING, 0},
                                {"toLocaleDateString", 18, JS_BUILTIN_DATE_TO_LOCALE_DATE_STRING, 0},
                                {"valueOf", 7, JS_BUILTIN_DATE_VALUE_OF, 0},
                                {NULL, 0, 0, 0}
                            };
                            for (int mi = 0; methods[mi].name; mi++) {
                                Item mk = (Item){.item = s2it(heap_create_name(methods[mi].name, methods[mi].len))};
                                Item mf = js_get_or_create_builtin(methods[mi].bid, methods[mi].name, methods[mi].pc);
                                js_property_set(fn->prototype, mk, mf);
                                js_mark_non_enumerable(fn->prototype, mk);
                            }
                            // Symbol.toPrimitive
                            Item tp_key = (Item){.item = s2it(heap_create_name("__sym_5", 7))};
                            Item tp_fn = js_get_or_create_builtin(JS_BUILTIN_DATE_VALUE_OF, "[Symbol.toPrimitive]", 1);
                            js_property_set(fn->prototype, tp_key, tp_fn);
                            js_mark_non_enumerable(fn->prototype, tp_key);
                        }
                        // v82b: Populate Number.prototype methods
                        if (nl == 6 && strncmp(nm, "Number", 6) == 0) {
                            struct { const char* name; int len; int bid; int pc; } methods[] = {
                                {"toString", 8, JS_BUILTIN_NUM_TO_STRING, 1},
                                {"valueOf", 7, JS_BUILTIN_NUM_VALUE_OF, 0},
                                {"toFixed", 7, JS_BUILTIN_NUM_TO_FIXED, 1},
                                {"toPrecision", 11, JS_BUILTIN_NUM_TO_PRECISION, 1},
                                {"toExponential", 13, JS_BUILTIN_NUM_TO_EXPONENTIAL, 1},
                                {NULL, 0, 0, 0}
                            };
                            for (int mi = 0; methods[mi].name; mi++) {
                                Item mk = (Item){.item = s2it(heap_create_name(methods[mi].name, methods[mi].len))};
                                Item mf = js_get_or_create_builtin(methods[mi].bid, methods[mi].name, methods[mi].pc);
                                js_property_set(fn->prototype, mk, mf);
                                js_mark_non_enumerable(fn->prototype, mk);
                            }
                        }
                        // v82c: Populate RegExp.prototype methods
                        if (nl == 6 && strncmp(nm, "RegExp", 6) == 0) {
                            struct { const char* name; int len; int bid; int pc; } methods[] = {
                                {"exec", 4, JS_BUILTIN_REGEXP_EXEC, 1},
                                {"test", 4, JS_BUILTIN_REGEXP_TEST, 1},
                                {"toString", 8, JS_BUILTIN_REGEXP_TO_STRING, 0},
                                {NULL, 0, 0, 0}
                            };
                            for (int mi = 0; methods[mi].name; mi++) {
                                Item mk = (Item){.item = s2it(heap_create_name(methods[mi].name, methods[mi].len))};
                                Item mf = js_get_or_create_builtin(methods[mi].bid, methods[mi].name, methods[mi].pc);
                                js_property_set(fn->prototype, mk, mf);
                                js_mark_non_enumerable(fn->prototype, mk);
                            }
                            // v83: Symbol-keyed methods (@@match, @@replace, @@search, @@split)
                            struct { const char* sym_key; int sym_len; int bid; const char* display; int pc; } sym_methods[] = {
                                {"__sym_7",  7, JS_BUILTIN_REGEXP_SYMBOL_MATCH,     "[Symbol.match]",    1},
                                {"__sym_8",  7, JS_BUILTIN_REGEXP_SYMBOL_REPLACE,   "[Symbol.replace]",  2},
                                {"__sym_9",  7, JS_BUILTIN_REGEXP_SYMBOL_SEARCH,    "[Symbol.search]",   1},
                                {"__sym_10", 8, JS_BUILTIN_REGEXP_SYMBOL_SPLIT,     "[Symbol.split]",    2},
                                {NULL, 0, 0, NULL, 0}
                            };
                            for (int mi = 0; sym_methods[mi].sym_key; mi++) {
                                Item mk = (Item){.item = s2it(heap_create_name(sym_methods[mi].sym_key, sym_methods[mi].sym_len))};
                                Item mf = js_get_or_create_builtin(sym_methods[mi].bid, sym_methods[mi].display, sym_methods[mi].pc);
                                js_property_set(fn->prototype, mk, mf);
                                js_mark_non_enumerable(fn->prototype, mk);
                            }
                        }
                    }
                }
                // v20: Set constructor property (non-enumerable, writable, configurable)
                // Skip for generator functions — generator prototypes have no constructor
                if (!(fn->flags & JS_FUNC_FLAG_GENERATOR)) {
                    Item ctor_key = (Item){.item = s2it(heap_create_name("constructor", 11))};
                    js_property_set(fn->prototype, ctor_key, object);
                    js_mark_non_enumerable(fn->prototype, ctor_key);
                }
                return fn->prototype;
            }
            // .length — formal parameter count (ES spec: params before first default, excl rest)
            if (str_key->len == 6 && strncmp(str_key->chars, "length", 6) == 0) {
                int len = (fn->formal_length >= 0) ? fn->formal_length : fn->param_count;
                if (len < 0) len = -len - 1; // rest param: -N means N total, length = N-1
                if (fn->bound_args) {
                    len = len - fn->bound_argc;
                    if (len < 0) len = 0;
                }
                return (Item){.item = i2it(len)};
            }
            // .name — function name
            if (str_key->len == 4 && strncmp(str_key->chars, "name", 4) == 0) {
                if (fn->name) {
                    return (Item){.item = s2it(fn->name)};
                }
                return (Item){.item = s2it(heap_create_name("", 0))};
            }
            // .call, .apply, .bind and other Function.prototype methods
            Item builtin = js_lookup_builtin_method(LMD_TYPE_FUNC, str_key->chars, str_key->len);
            if (builtin.item != ItemNull.item) return builtin;
            // v18: check custom properties backing map (e.g. assert._isSameValue)
            if (fn->properties_map.item != 0 && get_type_id(fn->properties_map) == LMD_TYPE_MAP) {
                Item result = map_get(fn->properties_map.map, key);
                if (result.item != ItemNull.item) return result;
            }
            // v18k: check constructor static methods (Object.keys, Array.isArray, Number.isInteger, etc.)
            if (fn->name) {
                Item static_method = js_lookup_constructor_static(
                    fn->name->chars, (int)fn->name->len,
                    str_key->chars, (int)str_key->len);
                if (static_method.item != ItemNull.item) {
                    // Auto-store in properties_map so reflection APIs
                    // (hasOwnProperty, getOwnPropertyDescriptor) can find it
                    js_func_init_property(object, key, static_method);
                    js_mark_non_enumerable(object, key);
                    return static_method;
                }
            }
            // v83: Prototype chain fallback for function objects
            // Look up unknown properties on Function.prototype (e.g. .constructor, .__proto__)
            {
                Item func_ctor = js_get_constructor((Item){.item = s2it(heap_create_name("Function", 8))});
                if (get_type_id(func_ctor) == LMD_TYPE_FUNC) {
                    Item proto_key = (Item){.item = s2it(heap_create_name("prototype", 9))};
                    Item func_proto = js_property_get(func_ctor, proto_key);
                    if (get_type_id(func_proto) == LMD_TYPE_MAP) {
                        Item result = map_get(func_proto.map, key);
                        if (result.item != ItemNull.item) return result;
                        // Also walk Object.prototype chain
                        result = js_prototype_lookup(func_proto, key);
                        if (result.item != ItemNull.item) return result;
                    }
                }
            }
        }
    }

    // Fallback: check for built-in method on the value's type
    if (get_type_id(key) == LMD_TYPE_STRING) {
        String* str_key = it2s(key);

        // Symbol property access: .description, .toString
        if (type == LMD_TYPE_INT) {
            // Check if object is actually a symbol (encoded as negative int)
            int64_t iv = it2i(object);
            if (iv <= -(int64_t)JS_SYMBOL_BASE) {
                if (str_key->len == 11 && strncmp(str_key->chars, "description", 11) == 0) {
                    return js_symbol_get_description(object);
                }
                if (str_key->len == 8 && strncmp(str_key->chars, "toString", 8) == 0) {
                    return js_get_or_create_builtin(JS_BUILTIN_SYM_TO_STRING, "toString", 0);
                }
            }
        }

        // v20: .constructor for number and boolean primitives
        if (str_key->len == 11 && strncmp(str_key->chars, "constructor", 11) == 0) {
            if (type == LMD_TYPE_INT || type == LMD_TYPE_INT64 || type == LMD_TYPE_FLOAT) {
                Item num_name = (Item){.item = s2it(heap_create_name("Number", 6))};
                return js_get_constructor(num_name);
            }
            if (type == LMD_TYPE_BOOL) {
                Item bool_name = (Item){.item = s2it(heap_create_name("Boolean", 7))};
                return js_get_constructor(bool_name);
            }
        }
        // v83: __proto__ for number and boolean primitives
        if (str_key->len == 9 && strncmp(str_key->chars, "__proto__", 9) == 0) {
            if (type == LMD_TYPE_INT || type == LMD_TYPE_INT64 || type == LMD_TYPE_FLOAT ||
                type == LMD_TYPE_BOOL) {
                return js_get_prototype_of(object);
            }
        }
        Item builtin = js_lookup_builtin_method(type, str_key->chars, str_key->len);
        if (builtin.item != ItemNull.item) return builtin;
    }

    return make_js_undefined();
}

extern "C" Item js_property_set(Item object, Item key, Item value) {
    // Convert Symbol keys to unique string keys for property storage
    if (js_key_is_symbol(key)) key = js_symbol_to_key(key);
    TypeId type = get_type_id(object);

    // Array: result[i] = val or arr.length = n
    if (type == LMD_TYPE_ARRAY) {
        // Handle arr.length = newLength (resize array)
        if (get_type_id(key) == LMD_TYPE_STRING) {
            String* str_key = it2s(key);
            if (str_key->len == 6 && strncmp(str_key->chars, "length", 6) == 0) {
                int64_t new_len = (int64_t)js_get_number(value);
                Array* arr = object.array;
                if (new_len >= 0) {
                    if (new_len > arr->length) {
                        // v22: Guard against huge length expansion (> 1M gap)
                        int64_t gap = new_len - arr->length;
                        if (gap > SPARSE_GAP_MAX) {
                            log_debug("js_property_set: array length expansion %lld->%lld (gap %lld) too large, skipping",
                                      (long long)arr->length, (long long)new_len, (long long)gap);
                            return value;
                        }
                        // Extend: ensure capacity and fill with undefined.
                        // Use direct realloc to avoid GC-triggering array_push loops.
                        if (new_len + 4 > arr->capacity) {
                            int64_t new_cap = new_len + 4;
                            Item* new_items = (Item*)malloc(new_cap * sizeof(Item));
                            if (arr->items && arr->length > 0) {
                                memcpy(new_items, arr->items, arr->length * sizeof(Item));
                            }
                            arr->items = new_items;
                            arr->capacity = new_cap;
                        }
                        // Fill new slots with undefined
                        Item undef = make_js_undefined();
                        for (int64_t i = arr->length; i < new_len; i++) {
                            arr->items[i] = undef;
                        }
                        arr->length = new_len;
                    } else if (new_len < arr->length) {
                        // Truncate
                        arr->length = new_len;
                    }
                }
                return value;
            }
        }
        // v25: non-numeric string keys on arrays → store in companion map (arr->extra)
        if (get_type_id(key) == LMD_TYPE_STRING) {
            String* sk = it2s(key);
            if (sk && sk->len > 0) {
                // check if the key is a numeric array index
                double idx_d = js_get_number(key);
                if (idx_d != idx_d) { // NaN → not a valid numeric index
                    // store in companion map
                    Array* arr = object.array;
                    Map* pm;
                    if (arr->extra == 0) {
                        Item obj = js_new_object();
                        arr->extra = (int64_t)(uintptr_t)obj.map;
                    }
                    pm = (Map*)(uintptr_t)arr->extra;
                    Item map_item = (Item){.map = pm};
                    js_property_set(map_item, key, value);
                    return value;
                }
            }
        }
        return js_array_set(object, key, value);
    }

    // Typed array: ta[i] = val
    if (type == LMD_TYPE_MAP && js_is_typed_array(object)) {
        return js_typed_array_set(object, key, value);
    }

    if (type == LMD_TYPE_MAP) {
        Map* m = object.map;
        // JS semantics: numeric keys are coerced to strings (obj[17] === obj["17"])
        if (get_type_id(key) == LMD_TYPE_INT || get_type_id(key) == LMD_TYPE_FLOAT) {
            char buf[64];
            if (get_type_id(key) == LMD_TYPE_INT) {
                snprintf(buf, sizeof(buf), "%lld", (long long)it2i(key));
            } else {
                double dv = it2d(key);
                // v24: -0.0 should stringify to "0" per ES spec
                if (dv == 0.0) snprintf(buf, sizeof(buf), "0");
                else snprintf(buf, sizeof(buf), "%g", dv);
            }
            key = (Item){.item = s2it(heap_create_name(buf, strlen(buf)))};
        }
        // v16: Enforce Object.freeze — frozen objects reject all property writes
        {
            bool frozen_found = false;
            Item frozen_val = js_map_get_fast(m, "__frozen__", 10, &frozen_found);
            if (frozen_found && js_is_truthy(frozen_val)) {
                // skip writes to internal properties (needed for freeze itself)
                if (get_type_id(key) == LMD_TYPE_STRING) {
                    String* sk = it2s(key);
                    if (!(sk && sk->len >= 2 && sk->chars[0] == '_' && sk->chars[1] == '_')) {
                        js_strict_throw_property_error("assign to read only", sk ? sk->chars : NULL, sk ? (int)sk->len : 0);
                        return value; // silently reject write to frozen object
                    }
                }
            }
        }
        // v16: Enforce non-writable properties via __nw_<name> marker
        if (get_type_id(key) == LMD_TYPE_STRING) {
            String* str_key = it2s(key);
            if (str_key && str_key->len > 0 && str_key->len < 200 &&
                !(str_key->len >= 5 && strncmp(str_key->chars, "__nw_", 5) == 0) &&
                !(str_key->len >= 5 && strncmp(str_key->chars, "__nc_", 5) == 0) &&
                !(str_key->len >= 5 && strncmp(str_key->chars, "__ne_", 5) == 0)) {
                char nw_key[256];
                snprintf(nw_key, sizeof(nw_key), "__nw_%.*s", (int)str_key->len, str_key->chars);
                bool nw_found = false;
                Item nw_val = js_map_get_fast(m, nw_key, (int)strlen(nw_key), &nw_found);
                if (nw_found && js_is_truthy(nw_val)) {
                    js_strict_throw_property_error("assign to read only", str_key->chars, (int)str_key->len);
                    return value; // silently reject write to non-writable property
                }
            }
        }
        // Check if this is a document proxy object
        if (js_is_document_proxy(object)) {
            return js_document_proxy_set_property(key, value);
        }
        // Check if this is a DOM node wrapper (indicated by js_dom_type_marker)
        if (js_is_dom_node(object)) {
            return js_dom_set_property(object, key, value);
        }
        // Check if this is a CSSOM rule wrapper (e.g., rule.selectorText = "...")
        if (js_is_css_rule(object)) {
            return js_cssom_rule_set_property(object, key, value);
        }
        // Check if this is a CSSOM rule declaration wrapper (e.g., rule.style.zIndex = "12345")
        if (js_is_rule_style_decl(object)) {
            return js_cssom_rule_decl_set_property(object, key, value);
        }
        // Setter property dispatch: check for __set_<propName> on object or prototype
        // Skip setter check only for __get_/__set_ keys (to prevent infinite recursion)
        if (get_type_id(key) == LMD_TYPE_STRING) {
            String* str_key = it2s(key);
            if (str_key && str_key->len < 64 && str_key->len > 0 &&
                !(str_key->len > 6 && (strncmp(str_key->chars, "__get_", 6) == 0 ||
                                        strncmp(str_key->chars, "__set_", 6) == 0))) {
                char setter_key[256];
                snprintf(setter_key, sizeof(setter_key), "__set_%.*s", (int)str_key->len, str_key->chars);
                Item sk = (Item){.item = s2it(heap_create_name(setter_key, strlen(setter_key)))};
                Item setter = map_get(m, sk);
                if (setter.item == ItemNull.item) {
                    setter = js_prototype_lookup(object, sk);
                }
                if (setter.item != ItemNull.item && get_type_id(setter) == LMD_TYPE_FUNC) {
                    Item args[1] = { value };
                    js_call_function(setter, object, args, 1);
                    return value;
                }
            }
        }
        // JS object / Lambda map: try fn_map_set first (update existing field),
        // fall back to map_put for new keys
        TypeMap* map_type = (TypeMap*)m->type;
        if (map_type && map_type != &EmptyMap && map_type->shape) {
            // search for existing key
            String* str_key = NULL;
            TypeId key_type = get_type_id(key);
            if (key_type == LMD_TYPE_STRING) str_key = it2s(key);
            else if (key_type == LMD_TYPE_SYMBOL) str_key = it2s(key);
            if (str_key) {
                // A1: Use hash table for O(1) existing-key check
                if (map_type->field_count > 0) {
                    ShapeEntry* found = typemap_hash_lookup(map_type, str_key->chars, (int)str_key->len);
                    if (found) {
                        fn_map_set(object, key, value);
                        return value;
                    }
                } else {
                    ShapeEntry* entry = map_type->shape;
                    while (entry) {
                        if (entry->name && (entry->name->str == str_key->chars ||  // A6: interned pointer
                            (entry->name->length == (size_t)str_key->len
                             && strncmp(entry->name->str, str_key->chars, str_key->len) == 0))) {
                            fn_map_set(object, key, value);
                            return value;
                        }
                        entry = entry->next;
                    }
                }
            }
        }
        // key not found or empty map — add new key via map_put
        // v21: Check non-extensible before adding new property
        {
            if (get_type_id(key) == LMD_TYPE_STRING) {
                String* sk = it2s(key);
                // skip internal properties (__ prefix)
                if (!(sk && sk->len >= 2 && sk->chars[0] == '_' && sk->chars[1] == '_')) {
                    bool ne_found = false;
                    Item ne_val = js_map_get_fast(m, "__non_extensible__", 17, &ne_found);
                    if (ne_found && js_is_truthy(ne_val)) {
                        js_strict_throw_property_error("add property", sk->chars, (int)sk->len);
                        return value; // silently reject — object is not extensible
                    }
                    // also check sealed and frozen (they imply non-extensible)
                    bool sl_found = false;
                    Item sl_val = js_map_get_fast(m, "__sealed__", 10, &sl_found);
                    if (sl_found && js_is_truthy(sl_val)) {
                        js_strict_throw_property_error("add property", sk->chars, (int)sk->len);
                        return value;
                    }
                }
            }
        }
        if (js_input) {
            String* str_key = NULL;
            TypeId key_type = get_type_id(key);
            if (key_type == LMD_TYPE_STRING) str_key = it2s(key);
            else if (key_type == LMD_TYPE_SYMBOL) str_key = it2s(key);
            if (str_key) {
                map_put(m, str_key, value, js_input);
            }
        } else {
            log_error("js_property_set: no js_input context for map_put");
        }
        return value;
    }

    // Function: setting .prototype on a function (constructor pattern)
    if (type == LMD_TYPE_FUNC) {
        JsFunction* fn = (JsFunction*)object.function;
        String* str_key = NULL;
        TypeId key_type = get_type_id(key);
        if (key_type == LMD_TYPE_STRING) str_key = it2s(key);
        if (str_key && str_key->len == 9 && strncmp(str_key->chars, "prototype", 9) == 0) {
            fn->prototype = value;
            // Register fn->prototype as a GC root so the prototype map survives GC.
            // JsFunction is pool-allocated (invisible to GC), but fn->prototype points
            // to a GC-managed map. Without this, the prototype gets collected when no
            // live objects reference it via __proto__.
            heap_register_gc_root(&fn->prototype.item);
            return value;
        }
        // v23: .name and .length are non-writable by default on functions.
        // Simple assignment should be silently ignored unless the property was
        // explicitly made writable via Object.defineProperty.
        if (str_key && ((str_key->len == 4 && memcmp(str_key->chars, "name", 4) == 0) ||
                        (str_key->len == 6 && memcmp(str_key->chars, "length", 6) == 0))) {
            if (fn->properties_map.item != 0 && get_type_id(fn->properties_map) == LMD_TYPE_MAP) {
                bool has_key = false;
                js_map_get_fast_ext(fn->properties_map.map, str_key->chars, (int)str_key->len, &has_key);
                if (has_key) {
                    // Property was explicitly set (class init or defineProperty).
                    // Check __nw_ marker: if non-writable, reject.
                    char nw_key[32];
                    snprintf(nw_key, sizeof(nw_key), "__nw_%.*s", (int)str_key->len, str_key->chars);
                    bool nw_found = false;
                    js_map_get_fast_ext(fn->properties_map.map, nw_key, (int)strlen(nw_key), &nw_found);
                    if (nw_found) return value; // non-writable, silently reject
                    // writable, fall through to store
                } else {
                    // Virtual .name/.length — non-writable by default
                    return value;
                }
            } else {
                // No properties_map — virtual .name/.length — non-writable by default
                return value;
            }
        }
        // v18: store arbitrary properties in backing map (e.g. assert._isSameValue = fn)
        if (fn->properties_map.item == 0) {
            fn->properties_map = js_new_object();
            heap_register_gc_root(&fn->properties_map.item);
        }
        if (fn->properties_map.item != 0 && get_type_id(fn->properties_map) == LMD_TYPE_MAP) {
            js_property_set(fn->properties_map, key, value);
        }
    }

    return value;
}

// v23: Force-store a property on a function's properties_map, bypassing writability checks.
// Used by class initialization (transpiler) and Object.defineProperty.
extern "C" void js_func_init_property(Item fn_item, Item key, Item value) {
    if (get_type_id(fn_item) != LMD_TYPE_FUNC) return;
    JsFunction* fn = (JsFunction*)fn_item.function;
    if (fn->properties_map.item == 0) {
        fn->properties_map = js_new_object();
        heap_register_gc_root(&fn->properties_map.item);
    }
    if (fn->properties_map.item != 0 && get_type_id(fn->properties_map) == LMD_TYPE_MAP) {
        js_property_set(fn->properties_map, key, value);
    }
}

extern "C" Item js_property_access(Item object, Item key) {
    // v18: throw TypeError when accessing properties on null or undefined
    TypeId type = get_type_id(object);
    if (type == LMD_TYPE_NULL || type == LMD_TYPE_UNDEFINED) {
        const char* type_str = (type == LMD_TYPE_NULL) ? "null" : "undefined";
        char msg[256];
        if (get_type_id(key) == LMD_TYPE_STRING) {
            String* sk = it2s(key);
            snprintf(msg, sizeof(msg), "Cannot read properties of %s (reading '%.*s')",
                     type_str, sk ? (int)sk->len : 0, sk ? sk->chars : "");
        } else {
            snprintf(msg, sizeof(msg), "Cannot read properties of %s", type_str);
        }
        Item err_name = (Item){.item = s2it(heap_create_name("TypeError"))};
        Item err_msg = (Item){.item = s2it(heap_create_name(msg))};
        Item error = js_new_error_with_name(err_name, err_msg);
        js_throw_value(error);
        return make_js_undefined();
    }
    return js_property_get(object, key);
}

// v23: Property access with raw C-string key — avoids heap string allocation.
// Used by transpiler when property name is a compile-time constant.
extern "C" Item js_property_get_str(Item object, const char* key, int key_len) {
    // null/undefined checks
    TypeId type = get_type_id(object);
    if (type == LMD_TYPE_NULL || type == LMD_TYPE_UNDEFINED) {
        const char* type_str = (type == LMD_TYPE_NULL) ? "null" : "undefined";
        char msg[256];
        snprintf(msg, sizeof(msg), "Cannot read properties of %s (reading '%.*s')",
                 type_str, key_len, key);
        Item err_name = (Item){.item = s2it(heap_create_name("TypeError"))};
        Item err_msg = (Item){.item = s2it(heap_create_name(msg))};
        Item error = js_new_error_with_name(err_name, err_msg);
        js_throw_value(error);
        return make_js_undefined();
    }
    // create string key and delegate to js_property_get
    Item str_key = (Item){.item = s2it(heap_create_name(key, key_len))};
    return js_property_get(object, str_key);
}

// Convert a UTF-16 unit index to the corresponding byte offset in a UTF-8 string.
// Returns the byte offset of the code unit at utf16_idx, or str_len if out of range.
static int js_utf16_idx_to_byte(const char* chars, int str_len, int64_t utf16_idx) {
    int pos = 0;
    int64_t cu = 0;
    while (pos < str_len && cu < utf16_idx) {
        unsigned char b = (unsigned char)chars[pos];
        int bytes;
        uint32_t cp;
        if (b < 0x80)            { cp = b;        bytes = 1; }
        else if ((b & 0xE0) == 0xC0) { cp = b & 0x1F; bytes = 2; }
        else if ((b & 0xF0) == 0xE0) { cp = b & 0x0F; bytes = 3; }
        else if ((b & 0xF8) == 0xF0) { cp = b & 0x07; bytes = 4; }
        else { cp = b; bytes = 1; }
        for (int i = 1; i < bytes && pos + i < str_len; i++)
            cp = (cp << 6) | ((unsigned char)chars[pos + i] & 0x3F);
        cu += (cp >= 0x10000) ? 2 : 1;
        pos += bytes;
    }
    return pos;
}

// JS-aware substring: indices are UTF-16 code unit indices (not codepoints).
// Handles negative indices like JS substring (clamp to 0) or slice (count from end).
// use_slice_semantics: true = slice (negative counts from end), false = substring (clamp to 0).
static Item js_str_substring_utf16(Item str_item, int64_t start, int64_t end) {
    String* s = it2s(str_item);
    if (!s) return (Item){.item = s2it(heap_create_name("", 0))};
    if (s->is_ascii) {
        // ASCII: UTF-16 idx == byte idx == codepoint idx
        int64_t len = (int64_t)s->len;
        if (start < 0) start = 0;
        if (end < 0) end = 0;
        if (start > len) start = len;
        if (end > len) end = len;
        if (start >= end) return (Item){.item = s2it(heap_create_name("", 0))};
        int64_t rlen = end - start;
        String* result = (String*)heap_alloc(sizeof(String) + (int)rlen + 1, LMD_TYPE_STRING);
        result->len = (int)rlen;
        result->is_ascii = 1;
        memcpy(result->chars, s->chars + start, (int)rlen);
        result->chars[rlen] = '\0';
        return (Item){.item = s2it(result)};
    }
    // Non-ASCII: convert UTF-16 unit indices to byte offsets
    int byte_start = js_utf16_idx_to_byte(s->chars, (int)s->len, start);
    int byte_end   = js_utf16_idx_to_byte(s->chars, (int)s->len, end);
    if (byte_start >= byte_end) return (Item){.item = s2it(heap_create_name("", 0))};
    int rlen = byte_end - byte_start;
    String* result = (String*)heap_alloc(sizeof(String) + rlen + 1, LMD_TYPE_STRING);
    result->len = rlen;
    result->is_ascii = 0;
    memcpy(result->chars, s->chars + byte_start, rlen);
    result->chars[rlen] = '\0';
    return (Item){.item = s2it(result)};
}

// Compute UTF-16 unit count of a non-ASCII UTF-8 string.
static int64_t js_utf16_len(const char* chars, int str_len, bool is_ascii) {
    if (is_ascii) return (int64_t)str_len;
    int64_t units = 0;
    int pos = 0;
    while (pos < str_len) {
        unsigned char b = (unsigned char)chars[pos];
        int bytes;
        uint32_t cp;
        if (b < 0x80)            { cp = b;        bytes = 1; }
        else if ((b & 0xE0) == 0xC0) { cp = b & 0x1F; bytes = 2; }
        else if ((b & 0xF0) == 0xE0) { cp = b & 0x0F; bytes = 3; }
        else if ((b & 0xF8) == 0xF0) { cp = b & 0x07; bytes = 4; }
        else { cp = b; bytes = 1; }
        for (int i = 1; i < bytes && pos + i < str_len; i++)
            cp = (cp << 6) | ((unsigned char)chars[pos + i] & 0x3F);
        units += (cp >= 0x10000) ? 2 : 1;
        pos += bytes;
    }
    return units;
}

// Get the length of any JS value. Handles typed arrays specially (Map-based),
// and delegates to fn_len for all standard Lambda types (array, list, string, etc.)
extern "C" int64_t js_get_length(Item object) {
    if (get_type_id(object) == LMD_TYPE_MAP && js_is_typed_array(object)) {
        return (int64_t)js_typed_array_length(object);
    }
    // Function .length = formal parameter count (unless overridden/deleted)
    if (get_type_id(object) == LMD_TYPE_FUNC) {
        JsFunction* fn = (JsFunction*)object.function;
        // v23: Check properties_map first for overridden/deleted .length
        if (fn->properties_map.item != 0 && get_type_id(fn->properties_map) == LMD_TYPE_MAP) {
            bool pm_found = false;
            Item pm_val = js_map_get_fast_ext(fn->properties_map.map, "length", 6, &pm_found);
            if (pm_found) {
                if (js_is_deleted_sentinel(pm_val)) return 0;
                return (int64_t)js_get_number(pm_val);
            }
        }
        int len = (fn->formal_length >= 0) ? fn->formal_length : fn->param_count;
        if (len < 0) len = -len - 1; // rest param: -N means N total, length = N-1
        if (fn->bound_args) {
            len = len - fn->bound_argc;
            if (len < 0) len = 0;
        }
        return (int64_t)len;
    }
    // JS string .length = UTF-16 code unit count (not codepoint count).
    if (get_type_id(object) == LMD_TYPE_STRING) {
        String* s = it2s(object);
        if (!s) return 0;
        return js_utf16_len(s->chars, (int)s->len, (bool)s->is_ascii);
    }
    // For Map objects (JS class instances), check for:
    // 1. Own "length" property
    // 2. Getter (__get_length) on own or prototype chain
    // 3. Prototype "length" property
    // before falling back to fn_len
    if (get_type_id(object) == LMD_TYPE_MAP) {
        Map* m = object.map;
        // Check own "length" property
        bool own_found = false;
        Item result = js_map_get_fast(m, "length", 6, &own_found);
        if (own_found && !js_is_deleted_sentinel(result)) {
            return (int64_t)js_get_number(result);
        }
        // Check getter __get_length on own object
        Item getter = js_map_get_fast(m, "__get_length", 12);
        if (getter.item == ItemNull.item) {
            // Check prototype chain for getter
            Item gk = (Item){.item = s2it(heap_create_name("__get_length", 12))};
            getter = js_prototype_lookup(object, gk);
        }
        if (getter.item != ItemNull.item && get_type_id(getter) == LMD_TYPE_FUNC) {
            Item val = js_call_function(getter, object, NULL, 0);
            return (int64_t)js_get_number(val);
        }
        // Check prototype chain for regular "length" property
        Item len_key = (Item){.item = s2it(heap_create_name("length", 6))};
        Item proto_result = js_prototype_lookup(object, len_key);
        if (proto_result.item != ItemNull.item && !js_is_deleted_sentinel(proto_result)) {
            return (int64_t)js_get_number(proto_result);
        }
    }
    return fn_len(object);
}

// Like js_get_length but returns the raw Item for .length property.
// Used by transpiler for .length access where the result might be non-numeric
// (e.g., {length: {toString: fn}} patterns in Array.prototype.forEach.call).
extern "C" Item js_get_length_item(Item object) {
    if (get_type_id(object) == LMD_TYPE_MAP && js_is_typed_array(object)) {
        return js_make_number((double)js_typed_array_length(object));
    }
    if (get_type_id(object) == LMD_TYPE_FUNC) {
        return js_make_number((double)js_get_length(object));
    }
    if (get_type_id(object) == LMD_TYPE_STRING) {
        String* s = it2s(object);
        if (!s) return js_make_number(0);
        return js_make_number((double)js_utf16_len(s->chars, (int)s->len, (bool)s->is_ascii));
    }
    if (get_type_id(object) == LMD_TYPE_MAP) {
        Map* m = object.map;
        // Check own "length" property - return raw value
        bool own_found = false;
        Item result = js_map_get_fast(m, "length", 6, &own_found);
        if (own_found && !js_is_deleted_sentinel(result)) {
            return result;  // return raw Item, not converted to number
        }
        // Check getter __get_length
        Item getter = js_map_get_fast(m, "__get_length", 12);
        if (getter.item == ItemNull.item) {
            Item gk = (Item){.item = s2it(heap_create_name("__get_length", 12))};
            getter = js_prototype_lookup(object, gk);
        }
        if (getter.item != ItemNull.item && get_type_id(getter) == LMD_TYPE_FUNC) {
            return js_call_function(getter, object, NULL, 0);
        }
        // Check prototype chain
        Item len_key = (Item){.item = s2it(heap_create_name("length", 6))};
        Item proto_result = js_prototype_lookup(object, len_key);
        if (proto_result.item != ItemNull.item && !js_is_deleted_sentinel(proto_result)) {
            return proto_result;
        }
    }
    if (get_type_id(object) == LMD_TYPE_ARRAY) {
        return js_make_number((double)fn_len(object));
    }
    // For unknown types, try general property access
    {
        Item key = (Item){.item = s2it(heap_create_name("length", 6))};
        return js_property_get(object, key);
    }
}

// =============================================================================
// Array Functions
// =============================================================================

// Direct array item push that bypasses Lambda's array_set compound-scalar embedding.
// Lambda's array_set uses arr->extra to count floats/int64s stored at buffer end,
// but JS uses arr->extra for companion property maps. Direct store avoids this conflict.
extern "C" void js_array_push_item_direct(Array* arr, Item value) {
    if (arr->length + 2 > arr->capacity) {
        expand_list((List*)arr);
    }
    arr->items[arr->length] = value;
    arr->length++;
}

// ES2024 §21.3.2.26 Math.pow — spec-mandated overrides of C pow()
extern "C" double js_math_pow_d(double base, double exp) {
    // ES spec: if exponent is +0 or -0, result is 1 (even if base is NaN)
    if (exp == 0.0) return 1.0;
    // ES spec: if exponent is NaN, result is NaN
    // (C pow(1, NaN) returns 1.0 per IEEE 754, but ES spec returns NaN)
    if (isnan(exp)) return NAN;
    // ES spec: if base is NaN, result is NaN (C handles this, but be explicit)
    if (isnan(base)) return NAN;
    // ES spec: if base is 1 or -1 and exponent is ±Infinity, result is NaN
    // (C pow returns 1.0 for pow(1, inf) and pow(-1, inf))
    if ((base == 1.0 || base == -1.0) && isinf(exp)) return NAN;
    return pow(base, exp);
}

// Boxed version: takes Items, returns Item
extern "C" Item js_math_pow(Item base_item, Item exp_item) {
    double base = js_get_number(js_to_number(base_item));
    double exp = js_get_number(js_to_number(exp_item));
    return js_make_number(js_math_pow_d(base, exp));
}

extern "C" Item js_array_new(int length) {
    Array* arr = array();
    // Pre-allocate the array with space for all elements
    if (length > 0) {
        // Allocate items array directly
        arr->capacity = length + 4;
        arr->items = (Item*)malloc(arr->capacity * sizeof(Item));
        // Set length to the target size
        arr->length = length;
        // Initialize all slots to holes (deleted sentinel) per ES spec.
        // new Array(n) creates a sparse array — slots are holes, not undefined.
        // js_array_get_int returns undefined when reading a hole.
        // forEach/map/filter etc. skip holes (deleted sentinels).
        Item hole = (Item){.item = JS_DELETED_SENTINEL_VAL};
        for (int i = 0; i < length; i++) {
            arr->items[i] = hole;
        }
    }
    return (Item){.array = arr};
}

// Return a hole sentinel value for array elisions
extern "C" Item js_array_hole() {
    return (Item){.item = JS_DELETED_SENTINEL_VAL};
}

// v18q: Create arguments array (stub — kept for sys_func_registry compatibility)
extern "C" Item js_create_arguments() {
    return js_array_new(0);
}

// new Array(arg) — JS spec: if arg is a valid non-negative integer, create sparse
// array of that length. Otherwise, if arg is numeric but not a valid length, throw
// RangeError. Otherwise, create a single-element array [arg].
// This matches the JS behavior where new Array(undefined) => [undefined] (length 1).
extern "C" Item js_array_new_from_item(Item arg) {
    TypeId type = get_type_id(arg);
    // Integer argument: create sparse array or throw RangeError
    if (type == LMD_TYPE_INT) {
        int64_t len = it2i(arg);
        if (len >= 0 && len <= 0xFFFFFFFF) {
            return js_array_new((int)len);
        }
        // Negative or > 2^32-1 → RangeError
        return js_throw_range_error("Invalid array length");
    }
    // Float argument: check if it's a non-negative integer value
    if (type == LMD_TYPE_FLOAT) {
        double d = it2d(arg);
        if (d >= 0.0 && d <= 4294967295.0 && d == (double)(uint32_t)d) {
            return js_array_new((int)(uint32_t)d);
        }
        // NaN, Infinity, negative, non-integer, or > 2^32-1 → RangeError
        return js_throw_range_error("Invalid array length");
    }
    // Any other value (null, undefined, string, object):
    // create a single-element array [arg]
    Item result = js_array_new(0);
    js_array_push_item_direct(result.array, arg);
    return result;
}

extern "C" Item js_array_get(Item array, Item index) {
    if (get_type_id(array) != LMD_TYPE_ARRAY) {
        return make_js_undefined();
    }

    int idx = (int)js_get_number(index);
    Array* arr = array.array;

    if (idx >= 0 && idx < arr->length) {
        // check for accessor (getter) on companion map
        if (arr->extra != 0) {
            char gk[64];
            snprintf(gk, sizeof(gk), "__get_%d", idx);
            Map* props = (Map*)(uintptr_t)arr->extra;
            bool gk_found = false;
            Item getter = js_map_get_fast(props, gk, (int)strlen(gk), &gk_found);
            if (gk_found && get_type_id(getter) == LMD_TYPE_FUNC) {
                return js_call_function(getter, array, NULL, 0);
            }
        }
        // return undefined for holes (deleted sentinel)
        if (arr->items[idx].item == JS_DELETED_SENTINEL_VAL)
            return make_js_undefined();
        return arr->items[idx];
    }

    return make_js_undefined();
}

// P10e: Fast array access with native int index (no js_get_number overhead)
extern "C" Item js_array_get_int(Item array, int64_t index) {
    if (get_type_id(array) == LMD_TYPE_ARRAY) {
        Array* arr = array.array;
        if (index >= 0 && index < arr->length) {
            // check for accessor (getter) on companion map
            if (arr->extra != 0) {
                char gk[64];
                snprintf(gk, sizeof(gk), "__get_%lld", (long long)index);
                Map* props = (Map*)(uintptr_t)arr->extra;
                bool gk_found = false;
                Item getter = js_map_get_fast(props, gk, (int)strlen(gk), &gk_found);
                if (gk_found && get_type_id(getter) == LMD_TYPE_FUNC) {
                    return js_call_function(getter, array, NULL, 0);
                }
            }
            // v25: check for deleted sentinel (array hole) — return undefined
            if (arr->items[index].item == JS_DELETED_SENTINEL_VAL) {
                return make_js_undefined();
            }
            return arr->items[index];
        }
        return make_js_undefined();
    }
    // fast path for typed arrays: avoid going through js_property_access
    if (js_is_typed_array(array)) {
        return js_typed_array_get(array, (Item){.item = i2it((int)index)});
    }
    // fall back to general property access for strings, maps, etc.
    return js_property_access(array, (Item){.item = i2it((int)index)});
}

// P10e: Fast array set with native int index
extern "C" Item js_array_set_int(Item array, int64_t index, Item value) {
    if (get_type_id(array) != LMD_TYPE_ARRAY) {
        // fast path for typed arrays
        if (js_is_typed_array(array)) {
            return js_typed_array_set(array, (Item){.item = i2it((int)index)}, value);
        }
        return js_property_set(array, (Item){.item = i2it((int)index)}, value);
    }
    Array* arr = array.array;
    // v27: check __nw_ (non-writable) marker from companion map before writing
    if (arr->extra != 0 && index >= 0 && index < arr->length) {
        Map* pm = (Map*)(uintptr_t)arr->extra;
        char nw_buf[32];
        snprintf(nw_buf, sizeof(nw_buf), "__nw_%lld", (long long)index);
        bool nw_found = false;
        js_map_get_fast_ext(pm, nw_buf, (int)strlen(nw_buf), &nw_found);
        if (nw_found) {
            return value; // silently fail for non-writable properties (sloppy mode)
        }
    }
    if (index >= 0 && index < arr->length) {
        arr->items[index] = value;
    } else if (index >= 0) {
        int64_t gap = index - arr->length;
        if (gap > SPARSE_GAP_MAX) {
            // v22: Sparse index — gap too large, skip dense expansion to prevent OOM
            log_debug("js_array_set_int: sparse index %lld (gap %lld), skipping dense expansion",
                      (long long)index, (long long)gap);
            return value;
        }
        // Expand array: fill gaps with undefined, then set the value
        Item undef = make_js_undefined();
        while (arr->length < index) {
            js_array_push_item_direct(arr, undef);
        }
        if (index == arr->length) {
            js_array_push_item_direct(arr, value);
        } else {
            arr->items[index] = value;
        }
    }
    return value;
}

extern "C" Item js_array_set(Item array, Item index, Item value) {
    if (get_type_id(array) != LMD_TYPE_ARRAY) {
        return value;
    }

    // v23: validate that the key is a valid numeric array index (not "foo", "__nw_0" etc.)
    // js_get_number on non-numeric strings returns NaN; (int64_t)NaN is UB in C/C++
    double idx_d = js_get_number(index);
    if (idx_d != idx_d) { // NaN check — non-numeric key
        return value; // silently ignore non-numeric keys on arrays
    }
    int64_t idx = (int64_t)idx_d;
    Array* arr = array.array;

    // v27: check __nw_ (non-writable) marker from companion map before writing
    if (arr->extra != 0 && idx >= 0 && idx < arr->length) {
        Map* pm = (Map*)(uintptr_t)arr->extra;
        char nw_buf[32];
        snprintf(nw_buf, sizeof(nw_buf), "__nw_%lld", (long long)idx);
        bool nw_found = false;
        js_map_get_fast_ext(pm, nw_buf, (int)strlen(nw_buf), &nw_found);
        if (nw_found) {
            return value; // silently fail for non-writable properties (sloppy mode)
        }
    }

    if (idx >= 0 && idx < arr->length) {
        arr->items[idx] = value;
    } else if (idx >= 0) {
        int64_t gap = idx - arr->length;
        if (gap > SPARSE_GAP_MAX) {
            // v22: Sparse index — gap too large, skip dense expansion to prevent OOM
            log_debug("js_array_set: sparse index %lld (gap %lld), skipping dense expansion",
                      (long long)idx, (long long)gap);
            return value;
        }
        // Expand array: fill gaps with undefined, then set the value
        Item undef = make_js_undefined();
        while (arr->length < idx) {
            js_array_push_item_direct(arr, undef);
        }
        if (idx == arr->length) {
            js_array_push_item_direct(arr, value);
        } else {
            arr->items[idx] = value;
        }
    }

    return value;
}

extern "C" int64_t js_array_length(Item array) {
    if (get_type_id(array) != LMD_TYPE_ARRAY) {
        return 0;
    }
    return (int64_t)array.array->length;
}

extern "C" Item js_array_push(Item array, Item value) {
    if (get_type_id(array) != LMD_TYPE_ARRAY) {
        return (Item){.item = i2it(0)};
    }

    Array* arr = array.array;
    js_array_push_item_direct(arr, value);
    return (Item){.item = i2it(arr->length)};
}

// =============================================================================
// Tagged Template Literals
// =============================================================================

extern "C" Item js_build_template_object(Item* cooked, Item* raw, int count) {
    // build a JS object (map) that acts like an array with .raw property
    // { "0": cooked[0], "1": cooked[1], ..., length: count, raw: [raw[0], raw[1], ...] }
    Item obj = js_new_object();
    char buf[24];
    for (int i = 0; i < count; i++) {
        snprintf(buf, sizeof(buf), "%d", i);
        Item key = (Item){.item = s2it(heap_create_name(buf))};
        js_property_set(obj, key, cooked[i]);
    }
    // set length
    Item len_key = (Item){.item = s2it(heap_create_name("length"))};
    js_property_set(obj, len_key, (Item){.item = i2it(count)});
    // build raw array
    Item raw_arr = js_new_object();
    for (int i = 0; i < count; i++) {
        snprintf(buf, sizeof(buf), "%d", i);
        Item key = (Item){.item = s2it(heap_create_name(buf))};
        js_property_set(raw_arr, key, raw[i]);
    }
    Item raw_len_key = (Item){.item = s2it(heap_create_name("length"))};
    js_property_set(raw_arr, raw_len_key, (Item){.item = i2it(count)});
    // freeze raw
    js_object_freeze(raw_arr);
    // set .raw on obj
    Item raw_key = (Item){.item = s2it(heap_create_name("raw"))};
    js_property_set(obj, raw_key, raw_arr);
    // freeze obj
    js_object_freeze(obj);
    return obj;
}

// =============================================================================
// Console Functions
// =============================================================================

extern "C" void js_console_log(Item value) {
    Item str = js_to_string(value);
    if (get_type_id(str) == LMD_TYPE_STRING) {
        String* s = it2s(str);
        printf("%.*s\n", (int)s->len, s->chars);
    }
}

// Per ES spec §9.2.2: if constructor returns an Object, use that instead of `this`
extern "C" Item js_new_check_constructor_return(Item obj, Item result) {
    TypeId rt = get_type_id(result);
    if (rt == LMD_TYPE_MAP || rt == LMD_TYPE_ARRAY || rt == LMD_TYPE_ELEMENT ||
        rt == LMD_TYPE_FUNC || rt == LMD_TYPE_OBJECT || rt == LMD_TYPE_VMAP) {
        return result;
    }
    return obj;
}

// =============================================================================
// Function Functions
// =============================================================================

// (JsFunction struct defined above, before js_property_get/set)

// Cache: func_ptr → JsFunction*  (ensures same MIR function → same wrapper → same .prototype)
static const int JS_FUNC_CACHE_SIZE = 512;
static void* js_func_cache_keys[512];
static JsFunction* js_func_cache_vals[512];
static int js_func_cache_count = 0;

static JsFunction* js_func_cache_lookup(void* func_ptr) {
    for (int i = 0; i < js_func_cache_count; i++) {
        if (js_func_cache_keys[i] == func_ptr) return js_func_cache_vals[i];
    }
    return NULL;
}

static void js_func_cache_insert(void* func_ptr, JsFunction* fn) {
    if (js_func_cache_count < JS_FUNC_CACHE_SIZE) {
        js_func_cache_keys[js_func_cache_count] = func_ptr;
        js_func_cache_vals[js_func_cache_count] = fn;
        js_func_cache_count++;
    }
}

void js_func_cache_reset() {
    js_func_cache_count = 0;
}

// Built-in method function cache — keyed by builtin_id
static Item js_builtin_cache[JS_BUILTIN_MAX];
static bool js_builtin_cache_init = false;

void js_builtin_cache_reset() {
    for (int i = 0; i < JS_BUILTIN_MAX; i++) js_builtin_cache[i] = ItemNull;
}

static Item js_get_or_create_builtin(int builtin_id, const char* name, int param_count) {
    if (!js_builtin_cache_init) {
        for (int i = 0; i < JS_BUILTIN_MAX; i++) js_builtin_cache[i] = ItemNull;
        js_builtin_cache_init = true;
    }
    if (js_builtin_cache[builtin_id].item != ItemNull.item) {
        return js_builtin_cache[builtin_id];
    }
    JsFunction* fn = (JsFunction*)pool_calloc(js_input->pool, sizeof(JsFunction));
    fn->type_id = LMD_TYPE_FUNC;
    fn->func_ptr = NULL;  // not needed, dispatch uses builtin_id
    fn->param_count = param_count;
    fn->formal_length = -1; // -1 = use param_count for .length
    fn->builtin_id = builtin_id;
    fn->name = heap_create_name(name, strlen(name));
    fn->prototype = ItemNull;
    // NOTE: bound_this left as 0 (from pool_calloc). Do NOT set to ItemNull
    // because ItemNull.item is non-zero (0x100000000000000) and the bound
    // function check uses `fn->bound_this.item` as a boolean test.
    Item result = {.function = (Function*)fn};
    js_builtin_cache[builtin_id] = result;
    return result;
}

// v18k: Lookup static methods on constructor functions (Object.keys, Array.isArray, etc.)
// Returns ItemNull if not a known constructor or not a known static method.
static Item js_lookup_constructor_static(const char* ctor_name, int ctor_len,
                                          const char* prop_name, int prop_len) {
    // Object static methods
    if (ctor_len == 6 && strncmp(ctor_name, "Object", 6) == 0) {
        struct { const char* name; int len; int id; int pc; } methods[] = {
            {"defineProperty", 14, JS_BUILTIN_OBJECT_DEFINE_PROPERTY, 3},
            {"defineProperties", 16, JS_BUILTIN_OBJECT_DEFINE_PROPERTIES, 2},
            {"getOwnPropertyDescriptor", 24, JS_BUILTIN_OBJECT_GET_OWN_PROPERTY_DESCRIPTOR, 2},
            {"getOwnPropertyDescriptors", 25, JS_BUILTIN_OBJECT_GET_OWN_PROPERTY_DESCRIPTORS, 1},
            {"getOwnPropertyNames", 19, JS_BUILTIN_OBJECT_GET_OWN_PROPERTY_NAMES, 1},
            {"getOwnPropertySymbols", 21, JS_BUILTIN_OBJECT_GET_OWN_PROPERTY_SYMBOLS, 1},
            {"keys", 4, JS_BUILTIN_OBJECT_KEYS, 1},
            {"values", 6, JS_BUILTIN_OBJECT_VALUES, 1},
            {"entries", 7, JS_BUILTIN_OBJECT_ENTRIES, 1},
            {"fromEntries", 11, JS_BUILTIN_OBJECT_FROM_ENTRIES, 1},
            {"create", 6, JS_BUILTIN_OBJECT_CREATE, 2},
            {"assign", 6, JS_BUILTIN_OBJECT_ASSIGN, 2},
            {"freeze", 6, JS_BUILTIN_OBJECT_FREEZE, 1},
            {"isFrozen", 8, JS_BUILTIN_OBJECT_IS_FROZEN, 1},
            {"seal", 4, JS_BUILTIN_OBJECT_SEAL, 1},
            {"isSealed", 8, JS_BUILTIN_OBJECT_IS_SEALED, 1},
            {"preventExtensions", 17, JS_BUILTIN_OBJECT_PREVENT_EXTENSIONS, 1},
            {"isExtensible", 12, JS_BUILTIN_OBJECT_IS_EXTENSIBLE, 1},
            {"is", 2, JS_BUILTIN_OBJECT_IS, 2},
            {"getPrototypeOf", 14, JS_BUILTIN_OBJECT_GET_PROTOTYPE_OF, 1},
            {"setPrototypeOf", 14, JS_BUILTIN_OBJECT_SET_PROTOTYPE_OF, 2},
            {"hasOwn", 6, JS_BUILTIN_OBJECT_HAS_OWN, 2},
            {NULL, 0, 0, 0}
        };
        for (int i = 0; methods[i].name; i++) {
            if (prop_len == methods[i].len && strncmp(prop_name, methods[i].name, prop_len) == 0) {
                return js_get_or_create_builtin(methods[i].id, methods[i].name, methods[i].pc);
            }
        }
    }
    // Array static methods
    if (ctor_len == 5 && strncmp(ctor_name, "Array", 5) == 0) {
        if (prop_len == 7 && strncmp(prop_name, "isArray", 7) == 0)
            return js_get_or_create_builtin(JS_BUILTIN_ARRAY_IS_ARRAY, "isArray", 1);
        if (prop_len == 4 && strncmp(prop_name, "from", 4) == 0)
            return js_get_or_create_builtin(JS_BUILTIN_ARRAY_FROM, "from", 1);
        if (prop_len == 2 && strncmp(prop_name, "of", 2) == 0)
            return js_get_or_create_builtin(JS_BUILTIN_ARRAY_OF, "of", 0);
    }
    // String static methods
    if (ctor_len == 6 && strncmp(ctor_name, "String", 6) == 0) {
        if (prop_len == 3 && strncmp(prop_name, "raw", 3) == 0)
            return js_get_or_create_builtin(JS_BUILTIN_STRING_RAW, "raw", 1);
        if (prop_len == 13 && strncmp(prop_name, "fromCodePoint", 13) == 0)
            return js_get_or_create_builtin(JS_BUILTIN_STRING_FROM_CODE_POINT, "fromCodePoint", 1);
        if (prop_len == 12 && strncmp(prop_name, "fromCharCode", 12) == 0)
            return js_get_or_create_builtin(JS_BUILTIN_STRING_FROM_CHAR_CODE, "fromCharCode", 1);
    }
    // Number static methods
    if (ctor_len == 6 && strncmp(ctor_name, "Number", 6) == 0) {
        if (prop_len == 9 && strncmp(prop_name, "isInteger", 9) == 0)
            return js_get_or_create_builtin(JS_BUILTIN_NUMBER_IS_INTEGER, "isInteger", 1);
        if (prop_len == 8 && strncmp(prop_name, "isFinite", 8) == 0)
            return js_get_or_create_builtin(JS_BUILTIN_NUMBER_IS_FINITE, "isFinite", 1);
        if (prop_len == 5 && strncmp(prop_name, "isNaN", 5) == 0)
            return js_get_or_create_builtin(JS_BUILTIN_NUMBER_IS_NAN, "isNaN", 1);
        if (prop_len == 13 && strncmp(prop_name, "isSafeInteger", 13) == 0)
            return js_get_or_create_builtin(JS_BUILTIN_NUMBER_IS_SAFE_INTEGER, "isSafeInteger", 1);
        if (prop_len == 8 && strncmp(prop_name, "parseInt", 8) == 0)
            return js_get_or_create_builtin(JS_BUILTIN_NUMBER_PARSE_INT, "parseInt", 2);
        if (prop_len == 10 && strncmp(prop_name, "parseFloat", 10) == 0)
            return js_get_or_create_builtin(JS_BUILTIN_NUMBER_PARSE_FLOAT, "parseFloat", 1);
    }
    // Promise static methods
    if (ctor_len == 7 && strncmp(ctor_name, "Promise", 7) == 0) {
        struct { const char* name; int len; int id; int pc; } methods[] = {
            {"resolve", 7, JS_BUILTIN_PROMISE_RESOLVE, 1},
            {"reject", 6, JS_BUILTIN_PROMISE_REJECT, 1},
            {"all", 3, JS_BUILTIN_PROMISE_ALL, 1},
            {"allSettled", 10, JS_BUILTIN_PROMISE_ALL_SETTLED, 1},
            {"any", 3, JS_BUILTIN_PROMISE_ANY, 1},
            {"race", 4, JS_BUILTIN_PROMISE_RACE, 1},
            {NULL, 0, 0, 0}
        };
        for (int i = 0; methods[i].name; i++) {
            if (prop_len == methods[i].len && strncmp(prop_name, methods[i].name, prop_len) == 0) {
                return js_get_or_create_builtin(methods[i].id, methods[i].name, methods[i].pc);
            }
        }
    }
    // Date static methods
    if (ctor_len == 4 && strncmp(ctor_name, "Date", 4) == 0) {
        if (prop_len == 3 && strncmp(prop_name, "now", 3) == 0)
            return js_get_or_create_builtin(JS_BUILTIN_DATE_NOW, "now", 0);
        if (prop_len == 5 && strncmp(prop_name, "parse", 5) == 0)
            return js_get_or_create_builtin(JS_BUILTIN_DATE_PARSE, "parse", 1);
        if (prop_len == 3 && strncmp(prop_name, "UTC", 3) == 0)
            return js_get_or_create_builtin(JS_BUILTIN_DATE_UTC, "UTC", 7);
    }
    // Handle .prototype on any constructor — delegate to the constructor's property access
    if (prop_len == 9 && strncmp(prop_name, "prototype", 9) == 0) {
        Item ctor_name_item = (Item){.item = s2it(heap_create_name(ctor_name, ctor_len))};
        Item ctor = js_get_constructor(ctor_name_item);
        if (get_type_id(ctor) == LMD_TYPE_FUNC) {
            Item proto_key = (Item){.item = s2it(heap_create_name("prototype", 9))};
            return js_property_get(ctor, proto_key);
        }
    }
    return ItemNull;
}

// Externally callable wrapper for js_lookup_constructor_static (from transpiler)
extern "C" Item js_constructor_static_property(Item ctor_name, Item prop_name) {
    String* cn = it2s(ctor_name);
    String* pn = it2s(prop_name);
    if (!cn || !pn) return ItemNull;
    return js_lookup_constructor_static(cn->chars, (int)cn->len, pn->chars, (int)pn->len);
}

// Populate all known static methods on a constructor function as own properties.
// This makes them visible to hasOwnProperty, getOwnPropertyDescriptor, getOwnPropertyNames.
extern "C" void js_populate_constructor_statics(Item ctor_item, const char* ctor_name, int ctor_len) {
    // Method tables: name, length pairs per constructor
    struct method_entry { const char* name; int len; };
    static const method_entry object_methods[] = {
        {"keys",4}, {"values",6}, {"entries",7}, {"fromEntries",11}, {"create",6}, {"assign",6},
        {"freeze",6}, {"isFrozen",8}, {"seal",4}, {"isSealed",8}, {"preventExtensions",17},
        {"isExtensible",12}, {"is",2}, {"getPrototypeOf",14}, {"setPrototypeOf",14},
        {"defineProperty",14}, {"defineProperties",16}, {"getOwnPropertyDescriptor",24},
        {"getOwnPropertyDescriptors",25}, {"getOwnPropertyNames",19},
        {"getOwnPropertySymbols",21}, {"hasOwn",6}, {NULL,0}
    };
    static const method_entry array_methods[] = {
        {"isArray",7}, {"from",4}, {"of",2}, {NULL,0}
    };
    static const method_entry string_methods[] = {
        {"fromCharCode",12}, {"fromCodePoint",13}, {"raw",3}, {NULL,0}
    };
    static const method_entry date_methods[] = {
        {"now",3}, {"parse",5}, {"UTC",3}, {NULL,0}
    };
    static const method_entry promise_methods[] = {
        {"resolve",7}, {"reject",6}, {"all",3}, {"allSettled",10}, {"any",3}, {"race",4}, {NULL,0}
    };
    static const method_entry number_methods[] = {
        {"isFinite",8}, {"isNaN",5}, {"isInteger",9}, {"isSafeInteger",13},
        {"parseInt",8}, {"parseFloat",10}, {NULL,0}
    };

    const method_entry* table = NULL;
    if (ctor_len == 6 && strncmp(ctor_name, "Object", 6) == 0) table = object_methods;
    else if (ctor_len == 5 && strncmp(ctor_name, "Array", 5) == 0) table = array_methods;
    else if (ctor_len == 6 && strncmp(ctor_name, "String", 6) == 0) table = string_methods;
    else if (ctor_len == 4 && strncmp(ctor_name, "Date", 4) == 0) table = date_methods;
    else if (ctor_len == 7 && strncmp(ctor_name, "Promise", 7) == 0) table = promise_methods;
    else if (ctor_len == 6 && strncmp(ctor_name, "Number", 6) == 0) table = number_methods;
    if (!table) return;

    for (int i = 0; table[i].name; i++) {
        Item method = js_lookup_constructor_static(ctor_name, ctor_len, table[i].name, table[i].len);
        if (method.item != ItemNull.item) {
            Item key = (Item){.item = s2it(heap_create_name(table[i].name, table[i].len))};
            js_func_init_property(ctor_item, key, method);
            js_mark_non_enumerable(ctor_item, key);
        }
    }
}

// Lookup built-in method by name for a given receiver type
extern "C" Item js_lookup_builtin_method(TypeId type, const char* name, int len) {
    // Object.prototype methods (available on all objects and arrays)
    if (len == 14 && strncmp(name, "hasOwnProperty", 14) == 0)
        return js_get_or_create_builtin(JS_BUILTIN_OBJ_HAS_OWN_PROPERTY, "hasOwnProperty", 1);
    if (len == 20 && strncmp(name, "propertyIsEnumerable", 20) == 0)
        return js_get_or_create_builtin(JS_BUILTIN_OBJ_PROPERTY_IS_ENUMERABLE, "propertyIsEnumerable", 1);
    if (len == 8 && strncmp(name, "toString", 8) == 0 && type != LMD_TYPE_FUNC && type != LMD_TYPE_BOOL && type != LMD_TYPE_ARRAY && type != LMD_TYPE_STRING)
        return js_get_or_create_builtin(JS_BUILTIN_OBJ_TO_STRING, "toString", 0);
    // Boolean.prototype.toString → returns "true"/"false"
    if (len == 8 && strncmp(name, "toString", 8) == 0 && type == LMD_TYPE_BOOL)
        return js_get_or_create_builtin(JS_BUILTIN_BOOL_TO_STRING, "toString", 0);
    if (len == 7 && strncmp(name, "valueOf", 7) == 0 && type != LMD_TYPE_FUNC)
        return js_get_or_create_builtin(JS_BUILTIN_OBJ_VALUE_OF, "valueOf", 0);
    if (len == 13 && strncmp(name, "isPrototypeOf", 13) == 0)
        return js_get_or_create_builtin(JS_BUILTIN_OBJ_IS_PROTOTYPE_OF, "isPrototypeOf", 1);
    if (len == 14 && strncmp(name, "toLocaleString", 14) == 0)
        return js_get_or_create_builtin(JS_BUILTIN_OBJ_TO_LOCALE_STRING, "toLocaleString", 0);

    // Function.prototype methods
    if (type == LMD_TYPE_FUNC) {
        if (len == 4 && strncmp(name, "call", 4) == 0)
            return js_get_or_create_builtin(JS_BUILTIN_FUNC_CALL, "call", 1);
        if (len == 5 && strncmp(name, "apply", 5) == 0)
            return js_get_or_create_builtin(JS_BUILTIN_FUNC_APPLY, "apply", 2);
        if (len == 4 && strncmp(name, "bind", 4) == 0)
            return js_get_or_create_builtin(JS_BUILTIN_FUNC_BIND, "bind", 1);
        if (len == 8 && strncmp(name, "toString", 8) == 0)
            return js_get_or_create_builtin(JS_BUILTIN_FUNC_TO_STRING, "toString", 0);
    }

    // Array.prototype methods
    if (type == LMD_TYPE_ARRAY) {
        struct { const char* name; int len; int id; int pc; } arr_methods[] = {
            {"push", 4, JS_BUILTIN_ARR_PUSH, 1},
            {"pop", 3, JS_BUILTIN_ARR_POP, 0},
            {"shift", 5, JS_BUILTIN_ARR_SHIFT, 0},
            {"unshift", 7, JS_BUILTIN_ARR_UNSHIFT, 1},
            {"join", 4, JS_BUILTIN_ARR_JOIN, 1},
            {"slice", 5, JS_BUILTIN_ARR_SLICE, 2},
            {"splice", 6, JS_BUILTIN_ARR_SPLICE, 2},
            {"indexOf", 7, JS_BUILTIN_ARR_INDEX_OF, 1},
            {"lastIndexOf", 11, JS_BUILTIN_ARR_LAST_INDEX_OF, 1},
            {"includes", 8, JS_BUILTIN_ARR_INCLUDES, 1},
            {"map", 3, JS_BUILTIN_ARR_MAP, 1},
            {"filter", 6, JS_BUILTIN_ARR_FILTER, 1},
            {"reduce", 6, JS_BUILTIN_ARR_REDUCE, 1},
            {"forEach", 7, JS_BUILTIN_ARR_FOR_EACH, 1},
            {"find", 4, JS_BUILTIN_ARR_FIND, 1},
            {"findIndex", 9, JS_BUILTIN_ARR_FIND_INDEX, 1},
            {"some", 4, JS_BUILTIN_ARR_SOME, 1},
            {"every", 5, JS_BUILTIN_ARR_EVERY, 1},
            {"sort", 4, JS_BUILTIN_ARR_SORT, 1},
            {"reverse", 7, JS_BUILTIN_ARR_REVERSE, 0},
            {"concat", 6, JS_BUILTIN_ARR_CONCAT, 1},
            {"flat", 4, JS_BUILTIN_ARR_FLAT, 0},
            {"flatMap", 7, JS_BUILTIN_ARR_FLAT_MAP, 1},
            {"fill", 4, JS_BUILTIN_ARR_FILL, 1},
            {"copyWithin", 10, JS_BUILTIN_ARR_COPY_WITHIN, 2},
            {"toString", 8, JS_BUILTIN_ARR_TO_STRING, 0},
            {"keys", 4, JS_BUILTIN_ARR_KEYS, 0},
            {"values", 6, JS_BUILTIN_ARR_VALUES, 0},
            {"entries", 7, JS_BUILTIN_ARR_ENTRIES, 0},
            {"at", 2, JS_BUILTIN_ARR_AT, 1},
            {"reduceRight", 11, JS_BUILTIN_ARR_REDUCE_RIGHT, 1},
            {"findLast", 8, JS_BUILTIN_ARR_FIND_LAST, 1},
            {"findLastIndex", 13, JS_BUILTIN_ARR_FIND_LAST_INDEX, 1},
            {"toSorted", 8, JS_BUILTIN_ARR_TO_SORTED, 1},
            {"toReversed", 10, JS_BUILTIN_ARR_TO_REVERSED, 0},
            {"toSpliced", 9, JS_BUILTIN_ARR_TO_SPLICED, 2},
            {"with", 4, JS_BUILTIN_ARR_WITH, 2},
            {NULL, 0, 0, 0}
        };
        for (int i = 0; arr_methods[i].name; i++) {
            if (len == arr_methods[i].len && strncmp(name, arr_methods[i].name, len) == 0) {
                return js_get_or_create_builtin(arr_methods[i].id, arr_methods[i].name, arr_methods[i].pc);
            }
        }
    }

    // String.prototype methods
    if (type == LMD_TYPE_STRING) {
        struct { const char* name; int len; int id; int pc; } str_methods[] = {
            {"charAt", 6, JS_BUILTIN_STR_CHAR_AT, 1},
            {"charCodeAt", 10, JS_BUILTIN_STR_CHAR_CODE_AT, 1},
            {"indexOf", 7, JS_BUILTIN_STR_INDEX_OF, 1},
            {"lastIndexOf", 11, JS_BUILTIN_STR_LAST_INDEX_OF, 1},
            {"includes", 8, JS_BUILTIN_STR_INCLUDES, 1},
            {"slice", 5, JS_BUILTIN_STR_SLICE, 2},
            {"substring", 9, JS_BUILTIN_STR_SUBSTRING, 2},
            {"toLowerCase", 11, JS_BUILTIN_STR_TO_LOWER_CASE, 0},
            {"toUpperCase", 11, JS_BUILTIN_STR_TO_UPPER_CASE, 0},
            {"trim", 4, JS_BUILTIN_STR_TRIM, 0},
            {"trimStart", 9, JS_BUILTIN_STR_TRIM_START, 0},
            {"trimEnd", 7, JS_BUILTIN_STR_TRIM_END, 0},
            {"split", 5, JS_BUILTIN_STR_SPLIT, 1},
            {"replace", 7, JS_BUILTIN_STR_REPLACE, 2},
            {"replaceAll", 10, JS_BUILTIN_STR_REPLACE_ALL, 2},
            {"match", 5, JS_BUILTIN_STR_MATCH, 1},
            {"matchAll", 8, JS_BUILTIN_STR_MATCH_ALL, 1},
            {"search", 6, JS_BUILTIN_STR_SEARCH, 1},
            {"startsWith", 10, JS_BUILTIN_STR_STARTS_WITH, 1},
            {"endsWith", 8, JS_BUILTIN_STR_ENDS_WITH, 1},
            {"repeat", 6, JS_BUILTIN_STR_REPEAT, 1},
            {"padStart", 8, JS_BUILTIN_STR_PAD_START, 2},
            {"padEnd", 6, JS_BUILTIN_STR_PAD_END, 2},
            {"toString", 8, JS_BUILTIN_STR_TO_STRING, 0},
            {"valueOf", 7, JS_BUILTIN_STR_VALUE_OF, 0},
            {"codePointAt", 11, JS_BUILTIN_STR_CODE_POINT_AT, 1},
            {"normalize", 9, JS_BUILTIN_STR_NORMALIZE, 0},
            {"concat", 6, JS_BUILTIN_STR_CONCAT, 1},
            {"at", 2, JS_BUILTIN_STR_AT, 1},
            {"localeCompare", 13, JS_BUILTIN_STR_LOCALE_COMPARE, 1},
            {"trimLeft", 8, JS_BUILTIN_STR_TRIM_START, 0},
            {"trimRight", 9, JS_BUILTIN_STR_TRIM_END, 0},
            {NULL, 0, 0, 0}
        };
        for (int i = 0; str_methods[i].name; i++) {
            if (len == str_methods[i].len && strncmp(name, str_methods[i].name, len) == 0) {
                return js_get_or_create_builtin(str_methods[i].id, str_methods[i].name, str_methods[i].pc);
            }
        }
    }

    // Number.prototype methods
    if (type == LMD_TYPE_INT || type == LMD_TYPE_FLOAT) {
        if (len == 8 && strncmp(name, "toString", 8) == 0)
            return js_get_or_create_builtin(JS_BUILTIN_NUM_TO_STRING, "toString", 1);
        if (len == 7 && strncmp(name, "valueOf", 7) == 0)
            return js_get_or_create_builtin(JS_BUILTIN_NUM_VALUE_OF, "valueOf", 0);
        if (len == 7 && strncmp(name, "toFixed", 7) == 0)
            return js_get_or_create_builtin(JS_BUILTIN_NUM_TO_FIXED, "toFixed", 1);
        if (len == 11 && strncmp(name, "toPrecision", 11) == 0)
            return js_get_or_create_builtin(JS_BUILTIN_NUM_TO_PRECISION, "toPrecision", 1);
        if (len == 13 && strncmp(name, "toExponential", 13) == 0)
            return js_get_or_create_builtin(JS_BUILTIN_NUM_TO_EXPONENTIAL, "toExponential", 1);
    }

    return ItemNull;
}

// v26: Return all builtin method names for a prototype type as a Lambda array.
// Used by getOwnPropertyNames to enumerate builtin methods on prototype objects.
extern "C" void js_append_builtin_method_names(TypeId type, Item result) {
    // Object.prototype methods (available on all objects)
    static const char* obj_methods[] = {
        "hasOwnProperty", "propertyIsEnumerable", "toString", "valueOf",
        "isPrototypeOf", "constructor", NULL
    };
    // Function.prototype methods
    static const char* func_methods[] = {
        "call", "apply", "bind", "toString", "constructor", NULL
    };
    // Array.prototype methods
    static const char* arr_methods[] = {
        "push", "pop", "shift", "unshift", "join", "slice", "splice",
        "indexOf", "lastIndexOf", "includes", "map", "filter", "reduce",
        "forEach", "find", "findIndex", "some", "every", "sort", "reverse",
        "concat", "flat", "flatMap", "fill", "copyWithin", "toString",
        "keys", "values", "entries", "at", "reduceRight",
        "findLast", "findLastIndex", "toSorted", "toReversed", "toSpliced",
        "with", "toLocaleString", "constructor", NULL
    };
    // String.prototype methods
    static const char* str_methods[] = {
        "charAt", "charCodeAt", "indexOf", "lastIndexOf", "includes",
        "slice", "substring", "toLowerCase", "toUpperCase", "trim",
        "trimStart", "trimEnd", "split", "replace", "replaceAll",
        "match", "matchAll", "search", "startsWith", "endsWith",
        "repeat", "padStart", "padEnd", "toString", "valueOf",
        "codePointAt", "normalize", "concat", "at", "localeCompare",
        "trimLeft", "trimRight", "constructor", NULL
    };
    // Number.prototype methods
    static const char* num_methods[] = {
        "toString", "valueOf", "toFixed", "toPrecision", "toExponential",
        "constructor", NULL
    };

    const char** names = NULL;
    if (type == LMD_TYPE_ARRAY) names = arr_methods;
    else if (type == LMD_TYPE_STRING) names = str_methods;
    else if (type == LMD_TYPE_FUNC) names = func_methods;
    else if (type == LMD_TYPE_INT || type == LMD_TYPE_FLOAT) names = num_methods;
    else names = obj_methods;

    for (int i = 0; names[i]; i++) {
        Item key = (Item){.item = s2it(heap_create_name(names[i], strlen(names[i])))};
        js_array_push(result, key);
    }
}

extern "C" Item js_new_function(void* func_ptr, int param_count) {
    if (!func_ptr) {
        log_error("js_new_function: null func_ptr! param_count=%d", param_count);
        return ItemNull;
    }
    // Return cached wrapper if the same MIR function was already wrapped.
    // This ensures Foo.prototype = {...} and (new Foo()) share the same JsFunction*.
    JsFunction* cached = js_func_cache_lookup(func_ptr);
    if (cached) return (Item){.function = (Function*)cached};

    // Pool-allocate: JS functions are module-lifetime objects that must not be
    // GC-collected (they live in pool-allocated env arrays unreachable from GC roots).
    JsFunction* fn = (JsFunction*)pool_calloc(js_input->pool, sizeof(JsFunction));
    fn->type_id = LMD_TYPE_FUNC;
    fn->func_ptr = func_ptr;
    fn->param_count = param_count;
    fn->formal_length = -1; // -1 = use param_count for .length
    fn->env = NULL;
    fn->env_size = 0;
    fn->prototype = ItemNull;
    js_func_cache_insert(func_ptr, fn);
    return (Item){.function = (Function*)fn};
}

// Create a closure (function with captured environment)
extern "C" Item js_new_closure(void* func_ptr, int param_count, Item* env, int env_size) {
    // Pool-allocate: closures stored in env arrays are unreachable from GC roots
    // (env is pool-allocated, stack scan can't trace through pool to find them).
    JsFunction* fn = (JsFunction*)pool_calloc(js_input->pool, sizeof(JsFunction));
    fn->type_id = LMD_TYPE_FUNC;
    fn->func_ptr = func_ptr;
    fn->param_count = param_count;
    fn->formal_length = -1; // -1 = use param_count for .length
    fn->env = env;
    fn->env_size = env_size;
    fn->prototype = ItemNull;
    return (Item){.function = (Function*)fn};
}

// Set the ES spec formal .length for a function (params before first default, excl rest)
extern "C" void js_set_formal_length(Item fn_item, int length) {
    if (get_type_id(fn_item) != LMD_TYPE_FUNC) return;
    JsFunction* fn = (JsFunction*)fn_item.function;
    fn->formal_length = (int16_t)length;
}

// Allocate closure environment (array of Item on the pool)
// Register as GC root range so the collector can trace objects held in env slots.
extern "C" Item* js_alloc_env(int count) {
    Item* env = (Item*)pool_calloc(js_input->pool, count * sizeof(Item));
    heap_register_gc_root_range((uint64_t*)env, count);
    return env;
}

// v20: Mark a function as a generator (generator prototype has no constructor)
extern "C" void js_mark_generator_func(Item fn_item) {
    if (get_type_id(fn_item) != LMD_TYPE_FUNC) return;
    JsFunction* fn = (JsFunction*)fn_item.function;
    fn->flags |= JS_FUNC_FLAG_GENERATOR;
}

// Mark a function as an arrow function (non-constructable)
extern "C" void js_mark_arrow_func(Item fn_item) {
    if (get_type_id(fn_item) != LMD_TYPE_FUNC) return;
    JsFunction* fn = (JsFunction*)fn_item.function;
    fn->flags |= JS_FUNC_FLAG_ARROW;
}

// Set the name of a JsFunction (called from transpiler after js_new_function/js_new_closure)
extern "C" void js_set_function_name(Item fn_item, Item name_item) {
    if (get_type_id(fn_item) != LMD_TYPE_FUNC) return;
    if (get_type_id(name_item) != LMD_TYPE_STRING) return;
    JsFunction* fn = (JsFunction*)fn_item.function;
    if (fn->func_ptr) { // is JsFunction layout
        fn->name = it2s(name_item);
    }
}

static Item js_dispatch_builtin(int builtin_id, Item this_val, Item* args, int arg_count);

// Invoke a JsFunction with args, handling env if it's a closure
static Item js_invoke_fn(JsFunction* fn, Item* args, int arg_count) {
    // Builtin functions have no func_ptr — dispatch by builtin_id
    if (fn->builtin_id > 0) {
        return js_dispatch_builtin(fn->builtin_id, js_current_this, args, arg_count);
    }

    // v48: Global builtin wrapper functions (parseInt, parseFloat, etc.)
    if (fn->builtin_id == -2 && fn->name) {
        const char* n = fn->name->chars;
        int nl = (int)fn->name->len;
        Item a0 = arg_count > 0 ? args[0] : ItemNull;
        Item a1 = arg_count > 1 ? args[1] : ItemNull;
        if (nl == 8 && strncmp(n, "parseInt", 8) == 0)
            return js_parseInt(a0, a1);
        if (nl == 10 && strncmp(n, "parseFloat", 10) == 0)
            return js_parseFloat(a0);
        if (nl == 5 && strncmp(n, "isNaN", 5) == 0)
            return js_isNaN(a0);
        if (nl == 8 && strncmp(n, "isFinite", 8) == 0)
            return js_isFinite(a0);
        if (nl == 9 && strncmp(n, "encodeURI", 9) == 0)
            return js_encodeURI(a0);
        if (nl == 9 && strncmp(n, "decodeURI", 9) == 0)
            return js_decodeURI(a0);
        if (nl == 18 && strncmp(n, "encodeURIComponent", 18) == 0)
            return js_encodeURIComponent(a0);
        if (nl == 18 && strncmp(n, "decodeURIComponent", 18) == 0)
            return js_decodeURIComponent(a0);
        return ItemNull;
    }

    // Store pending args so js_build_arguments_object() can access them.
    // Called at function entry before any nested calls, so no save/restore needed.
    js_pending_call_args = args;
    js_pending_call_argc = arg_count;

    typedef Item (*P0)();
    typedef Item (*P1)(Item);
    typedef Item (*P2)(Item, Item);
    typedef Item (*P3)(Item, Item, Item);
    typedef Item (*P4)(Item, Item, Item, Item);
    typedef Item (*P5)(Item, Item, Item, Item, Item);
    typedef Item (*P6)(Item, Item, Item, Item, Item, Item);
    typedef Item (*P7)(Item, Item, Item, Item, Item, Item, Item);
    typedef Item (*P8)(Item, Item, Item, Item, Item, Item, Item, Item);
    typedef Item (*P9)(Item, Item, Item, Item, Item, Item, Item, Item, Item);
    typedef Item (*P10)(Item, Item, Item, Item, Item, Item, Item, Item, Item, Item);
    typedef Item (*P11)(Item, Item, Item, Item, Item, Item, Item, Item, Item, Item, Item);
    typedef Item (*P12)(Item, Item, Item, Item, Item, Item, Item, Item, Item, Item, Item, Item);
    typedef Item (*P13)(Item, Item, Item, Item, Item, Item, Item, Item, Item, Item, Item, Item, Item);
    typedef Item (*P14)(Item, Item, Item, Item, Item, Item, Item, Item, Item, Item, Item, Item, Item, Item);
    typedef Item (*P15)(Item, Item, Item, Item, Item, Item, Item, Item, Item, Item, Item, Item, Item, Item, Item);
    typedef Item (*P16)(Item, Item, Item, Item, Item, Item, Item, Item, Item, Item, Item, Item, Item, Item, Item, Item);

    // Pad missing arguments with undefined to match declared param count
    Item padded_args[16];
    Item undef = make_js_undefined();
    int effective_count = arg_count;
    Item* effective_args = args;

    // Rest params: negative param_count signals last param is ...rest
    // Collect excess args into a JS array for the rest parameter
    bool has_rest = (fn->param_count < 0);
    int real_param_count = has_rest ? -fn->param_count : fn->param_count;

    if (has_rest) {
        int regular_count = real_param_count - 1;  // params before rest
        effective_count = real_param_count;
        // Copy regular args, then build rest array from remaining
        for (int i = 0; i < regular_count && i < 16; i++) {
            padded_args[i] = (i < arg_count && args) ? args[i] : undef;
        }
        // Build rest array from args[regular_count..arg_count-1]
        int rest_len = (arg_count > regular_count) ? (arg_count - regular_count) : 0;
        Item rest_arr = js_array_new(0);
        for (int i = 0; i < rest_len; i++) {
            js_array_push(rest_arr, args[regular_count + i]);
        }
        padded_args[regular_count] = rest_arr;
        effective_args = padded_args;
    } else if (arg_count < fn->param_count) {
        effective_count = fn->param_count;
        if (effective_count > 16) effective_count = 16;
        for (int i = 0; i < effective_count; i++) {
            padded_args[i] = (i < arg_count && args) ? args[i] : undef;
        }
        effective_args = padded_args;
    }

    if (fn->env) {
        // Closure: prepend env pointer as first argument
        Item env_item;
        env_item.item = (uint64_t)fn->env;
        switch (effective_count) {
            case 0: return ((P1)fn->func_ptr)(env_item);
            case 1: return ((P2)fn->func_ptr)(env_item, effective_args[0]);
            case 2: return ((P3)fn->func_ptr)(env_item, effective_args[0], effective_args[1]);
            case 3: return ((P4)fn->func_ptr)(env_item, effective_args[0], effective_args[1], effective_args[2]);
            case 4: return ((P5)fn->func_ptr)(env_item, effective_args[0], effective_args[1], effective_args[2], effective_args[3]);
            case 5: return ((P6)fn->func_ptr)(env_item, effective_args[0], effective_args[1], effective_args[2], effective_args[3], effective_args[4]);
            case 6: return ((P7)fn->func_ptr)(env_item, effective_args[0], effective_args[1], effective_args[2], effective_args[3], effective_args[4], effective_args[5]);
            case 7: return ((P8)fn->func_ptr)(env_item, effective_args[0], effective_args[1], effective_args[2], effective_args[3], effective_args[4], effective_args[5], effective_args[6]);
            case 8: return ((P9)fn->func_ptr)(env_item, effective_args[0], effective_args[1], effective_args[2], effective_args[3], effective_args[4], effective_args[5], effective_args[6], effective_args[7]);
            case 9: return ((P10)fn->func_ptr)(env_item, effective_args[0], effective_args[1], effective_args[2], effective_args[3], effective_args[4], effective_args[5], effective_args[6], effective_args[7], effective_args[8]);
            case 10: return ((P11)fn->func_ptr)(env_item, effective_args[0], effective_args[1], effective_args[2], effective_args[3], effective_args[4], effective_args[5], effective_args[6], effective_args[7], effective_args[8], effective_args[9]);
            case 11: return ((P12)fn->func_ptr)(env_item, effective_args[0], effective_args[1], effective_args[2], effective_args[3], effective_args[4], effective_args[5], effective_args[6], effective_args[7], effective_args[8], effective_args[9], effective_args[10]);
            case 12: return ((P13)fn->func_ptr)(env_item, effective_args[0], effective_args[1], effective_args[2], effective_args[3], effective_args[4], effective_args[5], effective_args[6], effective_args[7], effective_args[8], effective_args[9], effective_args[10], effective_args[11]);
            case 13: return ((P14)fn->func_ptr)(env_item, effective_args[0], effective_args[1], effective_args[2], effective_args[3], effective_args[4], effective_args[5], effective_args[6], effective_args[7], effective_args[8], effective_args[9], effective_args[10], effective_args[11], effective_args[12]);
            case 14: return ((P15)fn->func_ptr)(env_item, effective_args[0], effective_args[1], effective_args[2], effective_args[3], effective_args[4], effective_args[5], effective_args[6], effective_args[7], effective_args[8], effective_args[9], effective_args[10], effective_args[11], effective_args[12], effective_args[13]);
            case 15: return ((P16)fn->func_ptr)(env_item, effective_args[0], effective_args[1], effective_args[2], effective_args[3], effective_args[4], effective_args[5], effective_args[6], effective_args[7], effective_args[8], effective_args[9], effective_args[10], effective_args[11], effective_args[12], effective_args[13], effective_args[14]);
            default:
                log_error("js_invoke_fn: too many args for closure (%d)", effective_count);
                return ItemNull;
        }
    } else {
        switch (effective_count) {
            case 0: return ((P0)fn->func_ptr)();
            case 1: return ((P1)fn->func_ptr)(effective_args[0]);
            case 2: return ((P2)fn->func_ptr)(effective_args[0], effective_args[1]);
            case 3: return ((P3)fn->func_ptr)(effective_args[0], effective_args[1], effective_args[2]);
            case 4: return ((P4)fn->func_ptr)(effective_args[0], effective_args[1], effective_args[2], effective_args[3]);
            case 5: return ((P5)fn->func_ptr)(effective_args[0], effective_args[1], effective_args[2], effective_args[3], effective_args[4]);
            case 6: return ((P6)fn->func_ptr)(effective_args[0], effective_args[1], effective_args[2], effective_args[3], effective_args[4], effective_args[5]);
            case 7: return ((P7)fn->func_ptr)(effective_args[0], effective_args[1], effective_args[2], effective_args[3], effective_args[4], effective_args[5], effective_args[6]);
            case 8: return ((P8)fn->func_ptr)(effective_args[0], effective_args[1], effective_args[2], effective_args[3], effective_args[4], effective_args[5], effective_args[6], effective_args[7]);
            case 9: return ((P9)fn->func_ptr)(effective_args[0], effective_args[1], effective_args[2], effective_args[3], effective_args[4], effective_args[5], effective_args[6], effective_args[7], effective_args[8]);
            case 10: return ((P10)fn->func_ptr)(effective_args[0], effective_args[1], effective_args[2], effective_args[3], effective_args[4], effective_args[5], effective_args[6], effective_args[7], effective_args[8], effective_args[9]);
            case 11: return ((P11)fn->func_ptr)(effective_args[0], effective_args[1], effective_args[2], effective_args[3], effective_args[4], effective_args[5], effective_args[6], effective_args[7], effective_args[8], effective_args[9], effective_args[10]);
            case 12: return ((P12)fn->func_ptr)(effective_args[0], effective_args[1], effective_args[2], effective_args[3], effective_args[4], effective_args[5], effective_args[6], effective_args[7], effective_args[8], effective_args[9], effective_args[10], effective_args[11]);
            case 13: return ((P13)fn->func_ptr)(effective_args[0], effective_args[1], effective_args[2], effective_args[3], effective_args[4], effective_args[5], effective_args[6], effective_args[7], effective_args[8], effective_args[9], effective_args[10], effective_args[11], effective_args[12]);
            case 14: return ((P14)fn->func_ptr)(effective_args[0], effective_args[1], effective_args[2], effective_args[3], effective_args[4], effective_args[5], effective_args[6], effective_args[7], effective_args[8], effective_args[9], effective_args[10], effective_args[11], effective_args[12], effective_args[13]);
            case 15: return ((P15)fn->func_ptr)(effective_args[0], effective_args[1], effective_args[2], effective_args[3], effective_args[4], effective_args[5], effective_args[6], effective_args[7], effective_args[8], effective_args[9], effective_args[10], effective_args[11], effective_args[12], effective_args[13], effective_args[14]);
            default:
                log_error("js_invoke_fn: too many args (%d)", effective_count);
                return ItemNull;
        }
    }
}

// Call a JavaScript function stored as an Item
static int js_call_count = 0;

// Debug: check callee before calling, print site info if null
extern "C" Item js_debug_check_callee(Item callee, int64_t site_id) {
    if (get_type_id(callee) != LMD_TYPE_FUNC) {
        log_debug("js_debug_check_callee: non-function callee at site_id=%lld (type=%d, call_count=%d)",
            (long long)site_id, get_type_id(callee), js_call_count);
    }
    return ItemNull;
}

// Forward declarations for builtin dispatch
extern "C" Item js_string_method(Item str, Item method_name, Item* args, int argc);
extern "C" Item js_string_raw(Item* args, int argc);
// v83: Forward declarations for RegExp Symbol methods
static Item js_regexp_symbol_match(Item this_val, Item arg0);
static Item js_regexp_symbol_replace(Item this_val, Item str, Item replacement);
static Item js_regexp_symbol_search(Item this_val, Item arg0);
static Item js_regexp_symbol_split(Item this_val, Item str, Item limit);
static Item js_string_replace_impl(Item str, Item* args, int argc, bool is_replace_all);
// v18k: Forward declarations for Object/Array/Number static methods (js_globals.cpp)
extern "C" Item js_object_define_property(Item obj, Item name, Item descriptor);
extern "C" Item js_object_define_properties(Item obj, Item props);
extern "C" Item js_object_get_own_property_descriptor(Item obj, Item name);
extern "C" Item js_object_get_own_property_descriptors(Item obj);
extern "C" Item js_object_get_own_property_names(Item object);
extern "C" Item js_object_get_own_property_symbols(Item object);
extern "C" Item js_object_keys(Item object);
extern "C" Item js_object_values(Item object);
extern "C" Item js_object_entries(Item object);
extern "C" Item js_object_from_entries(Item iterable);
extern "C" Item js_object_create(Item proto);
extern "C" Item js_object_assign(Item target, Item* sources, int count);
extern "C" Item js_object_freeze(Item obj);
extern "C" Item js_object_is_frozen(Item obj);
extern "C" Item js_object_seal(Item obj);
extern "C" Item js_object_is_sealed(Item obj);
extern "C" Item js_object_prevent_extensions(Item obj);
extern "C" Item js_object_is_extensible(Item obj);
extern "C" Item js_object_is(Item left, Item right);
extern "C" Item js_get_prototype_of(Item object);
extern "C" Item js_has_own_property(Item obj, Item key);
extern "C" Item js_array_is_array(Item value);
extern "C" Item js_array_from(Item iterable);
extern "C" Item js_number_is_integer(Item value);
extern "C" Item js_number_is_finite(Item value);
extern "C" Item js_number_is_nan(Item value);
extern "C" Item js_number_is_safe_integer(Item value);
extern "C" Item js_parseInt(Item str_item, Item radix_item);
extern "C" Item js_parseFloat(Item str_item);

// Convert an array-like object (MAP with length + numeric indices) to an Array
static Item js_array_like_to_array(Item obj) {
    // Get length property
    Item length_key = (Item){.item = s2it(heap_create_name("length", 6))};
    Item len_val = js_property_get(obj, length_key);
    int len = (int)js_get_number(len_val);
    if (len < 0) len = 0;
    if (len > 100000) len = 100000; // safety cap
    Item result = js_array_new(len);
    Array* arr = result.array;
    for (int i = 0; i < len; i++) {
        char buf[16];
        int blen = snprintf(buf, sizeof(buf), "%d", i);
        Item idx_key = (Item){.item = s2it(heap_create_name(buf, blen))};
        Item val = js_property_get(obj, idx_key);
        if (i < arr->capacity) {
            arr->items[i] = val;
            if (i >= arr->length) arr->length = i + 1;
        } else {
            js_array_push_item_direct(arr, val);
        }
    }
    return result;
}

// Forward declarations for JSON functions (defined in js_globals.cpp)
extern "C" Item js_json_parse(Item str_item);
extern "C" Item js_json_parse_full(Item str_item, Item reviver);
extern "C" Item js_json_stringify(Item value);
extern "C" Item js_json_stringify_full(Item value, Item replacer, Item space);

// Dispatch a built-in method call by builtin_id
static Item js_dispatch_builtin(int builtin_id, Item this_val, Item* args, int arg_count) {
    Item undef = make_js_undefined();
    Item arg0 = arg_count > 0 && args ? args[0] : undef;
    Item arg1 = arg_count > 1 && args ? args[1] : undef;
    Item arg2 = arg_count > 2 && args ? args[2] : undef;

    switch (builtin_id) {
    // Object.prototype methods
    case JS_BUILTIN_OBJ_HAS_OWN_PROPERTY:
        return js_has_own_property(this_val, arg0);
    case JS_BUILTIN_OBJ_PROPERTY_IS_ENUMERABLE: {
        // Check if the property exists and is enumerable
        // v27: Handle arrays — check __ne_ marker from companion map
        if (get_type_id(this_val) == LMD_TYPE_ARRAY) {
            if (!it2b(js_has_own_property(this_val, arg0))) return (Item){.item = ITEM_FALSE};
            Item k = js_to_string(arg0);
            if (get_type_id(k) == LMD_TYPE_STRING) {
                String* ks = it2s(k);
                // "length" is always non-enumerable for arrays
                if (ks && ks->len == 6 && strncmp(ks->chars, "length", 6) == 0)
                    return (Item){.item = ITEM_FALSE};
                if (ks && this_val.array->extra != 0) {
                    Map* pm = (Map*)(uintptr_t)this_val.array->extra;
                    char ne_buf[256];
                    snprintf(ne_buf, sizeof(ne_buf), "__ne_%.*s", (int)ks->len, ks->chars);
                    bool ne_found = false;
                    js_map_get_fast_ext(pm, ne_buf, (int)strlen(ne_buf), &ne_found);
                    if (ne_found) return (Item){.item = ITEM_FALSE};
                }
            }
            return (Item){.item = ITEM_TRUE};
        }
        // v20: Virtual builtin methods are non-enumerable — check map shape directly
        if (get_type_id(this_val) == LMD_TYPE_MAP) {
            Item k = js_to_string(arg0);
            if (get_type_id(k) == LMD_TYPE_STRING) {
                String* ks = it2s(k);
                Map* m = this_val.map;
                if (m && m->type) {
                    // Check if property exists in actual map shape
                    bool found_in_shape = false;
                    TypeMap* tm = (TypeMap*)m->type;
                    ShapeEntry* e = tm->shape;
                    while (e) {
                        if (e->name && e->name->length == (size_t)ks->len &&
                            strncmp(e->name->str, ks->chars, ks->len) == 0) {
                            Item val = _map_read_field(e, m->data);
                            if (val.item != JS_DELETED_SENTINEL_VAL)
                                found_in_shape = true;
                            break;
                        }
                        e = e->next;
                    }
                    // Also check for accessor properties (__get_<name> or __set_<name>)
                    if (!found_in_shape && ks->len > 0 && ks->len < 200) {
                        char get_key[256];
                        snprintf(get_key, sizeof(get_key), "__get_%.*s", (int)ks->len, ks->chars);
                        bool get_found = false;
                        js_map_get_fast_ext(m, get_key, (int)strlen(get_key), &get_found);
                        if (get_found) found_in_shape = true;
                        if (!found_in_shape) {
                            char set_key[256];
                            snprintf(set_key, sizeof(set_key), "__set_%.*s", (int)ks->len, ks->chars);
                            bool set_found = false;
                            js_map_get_fast_ext(m, set_key, (int)strlen(set_key), &set_found);
                            if (set_found) found_in_shape = true;
                        }
                    }
                    if (!found_in_shape) return (Item){.item = ITEM_FALSE};
                    // Check __ne_ marker
                    char ne_buf[256];
                    snprintf(ne_buf, sizeof(ne_buf), "__ne_%.*s", (int)ks->len, ks->chars);
                    bool ne_found = false;
                    Item ne_val = js_map_get_fast_ext(m, ne_buf, (int)strlen(ne_buf), &ne_found);
                    if (ne_found && js_is_truthy(ne_val)) return (Item){.item = ITEM_FALSE};
                    return (Item){.item = ITEM_TRUE};
                }
            }
        }
        // Function objects: name, length, prototype, arguments, caller are non-enumerable
        if (get_type_id(this_val) == LMD_TYPE_FUNC) {
            Item k = js_to_string(arg0);
            if (get_type_id(k) == LMD_TYPE_STRING) {
                String* ks = it2s(k);
                if (ks && ((ks->len == 4 && strncmp(ks->chars, "name", 4) == 0) ||
                           (ks->len == 6 && strncmp(ks->chars, "length", 6) == 0) ||
                           (ks->len == 9 && strncmp(ks->chars, "prototype", 9) == 0) ||
                           (ks->len == 9 && strncmp(ks->chars, "arguments", 9) == 0) ||
                           (ks->len == 6 && strncmp(ks->chars, "caller", 6) == 0))) {
                    return (Item){.item = ITEM_FALSE};
                }
                // Check __ne_ marker in properties_map for custom properties
                JsFunction* fn = (JsFunction*)this_val.function;
                if (fn->properties_map.item != 0 && get_type_id(fn->properties_map) == LMD_TYPE_MAP) {
                    // First check if property exists in properties_map
                    bool has_key = false;
                    js_map_get_fast_ext(fn->properties_map.map, ks->chars, (int)ks->len, &has_key);
                    if (has_key) {
                        char ne_buf[256];
                        snprintf(ne_buf, sizeof(ne_buf), "__ne_%.*s", (int)ks->len, ks->chars);
                        bool ne_found = false;
                        Item ne_val = js_map_get_fast_ext(fn->properties_map.map, ne_buf, (int)strlen(ne_buf), &ne_found);
                        if (ne_found && js_is_truthy(ne_val)) return (Item){.item = ITEM_FALSE};
                        return (Item){.item = ITEM_TRUE};
                    }
                }
            }
        }
        Item has = js_has_own_property(this_val, arg0);
        if (!it2b(has)) return (Item){.item = ITEM_FALSE};
        return (Item){.item = ITEM_TRUE};
    }
    case JS_BUILTIN_OBJ_TO_STRING: {
        // ES spec §20.1.3.6: Object.prototype.toString returns "[object <tag>]"
        if (get_type_id(this_val) == LMD_TYPE_MAP) {
            Map* m = this_val.map;
            if (m) {
                // Check Symbol.toStringTag first (highest priority per spec)
                bool tag_found = false;
                Item tag = js_map_get_fast(m, "__sym_4", 7, &tag_found);
                if (!tag_found) {
                    Item tag_key = (Item){.item = s2it(heap_create_name("__sym_4", 7))};
                    tag = js_prototype_lookup(this_val, tag_key);
                    tag_found = (tag.item != ItemNull.item);
                }
                if (tag_found && get_type_id(tag) == LMD_TYPE_STRING) {
                    String* ts = it2s(tag);
                    if (ts) {
                        char buf[256];
                        int blen = snprintf(buf, sizeof(buf), "[object %.*s]", (int)ts->len, ts->chars);
                        return (Item){.item = s2it(heap_create_name(buf, blen))};
                    }
                }
                // Use __class_name__ for the tag when it matches a built-in type
                // ES spec §20.1.3.6: only certain built-in classes define a [[Class]] tag
                bool own_cn = false;
                Item cn = js_map_get_fast(m, "__class_name__", 14, &own_cn);
                if (own_cn && get_type_id(cn) == LMD_TYPE_STRING) {
                    String* cn_str = it2s(cn);
                    if (cn_str && cn_str->len > 0) {
                        const char* c = cn_str->chars;
                        int cl = (int)cn_str->len;
                        // Map class names to ES spec builtinTag
                        const char* tag = NULL;
                        int tag_len = 0;
                        // Error and subclasses → "Error"
                        if ((cl >= 5 && strncmp(c + cl - 5, "Error", 5) == 0) ||
                            (cl == 14 && strncmp(c, "AggregateError", 14) == 0)) {
                            tag = "Error"; tag_len = 5;
                        }
                        // Date → "Date" (only for actual Date instances with __date_ms__)
                        else if (cl == 4 && strncmp(c, "Date", 4) == 0) {
                            bool has_ms = false;
                            js_map_get_fast(m, "__date_ms__", 11, &has_ms);
                            if (has_ms) { tag = "Date"; tag_len = 4; }
                        }
                        // RegExp → "RegExp" (only for actual RegExp instances with __rd)
                        else if (cl == 6 && strncmp(c, "RegExp", 6) == 0) {
                            bool has_rd = false;
                            js_map_get_fast(m, "__rd", 4, &has_rd);
                            if (has_rd) { tag = "RegExp"; tag_len = 6; }
                        }
                        // String/Number/Boolean wrappers
                        else if (cl == 6 && strncmp(c, "String", 6) == 0) {
                            tag = "String"; tag_len = 6;
                        }
                        else if (cl == 6 && strncmp(c, "Number", 6) == 0) {
                            tag = "Number"; tag_len = 6;
                        }
                        else if (cl == 7 && strncmp(c, "Boolean", 7) == 0) {
                            tag = "Boolean"; tag_len = 7;
                        }
                        if (tag) {
                            char buf[256];
                            int blen = snprintf(buf, sizeof(buf), "[object %.*s]", tag_len, tag);
                            return (Item){.item = s2it(heap_create_name(buf, blen))};
                        }
                    }
                }
            }
        }
        TypeId tt = get_type_id(this_val);
        if (this_val.item == ITEM_NULL || tt == LMD_TYPE_NULL)
            return (Item){.item = s2it(heap_create_name("[object Null]", 13))};
        if (this_val.item == ITEM_JS_UNDEFINED || tt == LMD_TYPE_UNDEFINED)
            return (Item){.item = s2it(heap_create_name("[object Undefined]", 18))};
        if (tt == LMD_TYPE_ARRAY) {
            // v41: Check for custom Symbol.toStringTag on array (via companion map)
            Array* arr = this_val.array;
            if (arr && arr->extra) {
                bool tag_found = false;
                Item tag = js_map_get_fast((Map*)arr->extra, "__sym_4", 7, &tag_found);
                if (tag_found && get_type_id(tag) == LMD_TYPE_STRING) {
                    String* ts = it2s(tag);
                    if (ts) {
                        char buf[256];
                        int blen = snprintf(buf, sizeof(buf), "[object %.*s]", (int)ts->len, ts->chars);
                        return (Item){.item = s2it(heap_create_name(buf, blen))};
                    }
                }
            }
            return (Item){.item = s2it(heap_create_name("[object Array]", 14))};
        }
        if (tt == LMD_TYPE_FUNC) {
            // v41: Check for custom Symbol.toStringTag on function
            JsFunction* fn = (JsFunction*)this_val.function;
            if (fn->properties_map.item != 0 && get_type_id(fn->properties_map) == LMD_TYPE_MAP) {
                bool tag_found = false;
                Item tag = js_map_get_fast(fn->properties_map.map, "__sym_4", 7, &tag_found);
                if (tag_found && get_type_id(tag) == LMD_TYPE_STRING) {
                    String* ts = it2s(tag);
                    if (ts) {
                        char buf[256];
                        int blen = snprintf(buf, sizeof(buf), "[object %.*s]", (int)ts->len, ts->chars);
                        return (Item){.item = s2it(heap_create_name(buf, blen))};
                    }
                }
            }
            // v41: Generator functions → [object GeneratorFunction]
            if (fn->flags & JS_FUNC_FLAG_GENERATOR)
                return (Item){.item = s2it(heap_create_name("[object GeneratorFunction]", 26))};
            return (Item){.item = s2it(heap_create_name("[object Function]", 17))};
        }        if (tt == LMD_TYPE_BOOL)
            return (Item){.item = s2it(heap_create_name("[object Boolean]", 16))};
        if (tt == LMD_TYPE_INT || tt == LMD_TYPE_FLOAT)
            return (Item){.item = s2it(heap_create_name("[object Number]", 15))};
        if (tt == LMD_TYPE_STRING)
            return (Item){.item = s2it(heap_create_name("[object String]", 15))};
        if (tt == LMD_TYPE_MAP) {
            Map* m = this_val.map;
            if (m) {
                // Check for custom Symbol.toStringTag FIRST (well-known symbol ID=4, stored as __sym_4)
                // ES spec: @@toStringTag takes priority when explicitly set
                // Check own property first, then prototype chain
                bool tag_found = false;
                Item tag = js_map_get_fast(m, "__sym_4", 7, &tag_found);
                if (!tag_found) {
                    // Check prototype chain for @@toStringTag
                    Item tag_key = (Item){.item = s2it(heap_create_name("__sym_4", 7))};
                    tag = js_prototype_lookup(this_val, tag_key);
                    if (tag.item != ITEM_NULL && get_type_id(tag) == LMD_TYPE_STRING)
                        tag_found = true;
                }
                if (tag_found && get_type_id(tag) == LMD_TYPE_STRING) {
                    String* ts = it2s(tag);
                    if (ts) {
                        char buf[256];
                        int blen = snprintf(buf, sizeof(buf), "[object %.*s]", (int)ts->len, ts->chars);
                        return (Item){.item = s2it(heap_create_name(buf, blen))};
                    }
                }
                // Check for specific object types via __rd (RegExp)
                bool found = false;
                js_map_get_fast(m, "__rd", 4, &found);
                if (found) return (Item){.item = s2it(heap_create_name("[object RegExp]", 15))};
                found = false;
                Item cn = js_map_get_fast(m, "__class_name__", 14, &found);
                if (found && get_type_id(cn) == LMD_TYPE_STRING) {
                    String* cns = it2s(cn);
                    if (cns) {
                        if (cns->len == 4 && memcmp(cns->chars, "Date", 4) == 0)
                            return (Item){.item = s2it(heap_create_name("[object Date]", 13))};
                        if (cns->len == 5 && memcmp(cns->chars, "Error", 5) == 0)
                            return (Item){.item = s2it(heap_create_name("[object Error]", 14))};
                        if (cns->len == 3 && memcmp(cns->chars, "Map", 3) == 0)
                            return (Item){.item = s2it(heap_create_name("[object Map]", 12))};
                        if (cns->len == 3 && memcmp(cns->chars, "Set", 3) == 0)
                            return (Item){.item = s2it(heap_create_name("[object Set]", 12))};
                        // Check for error subtypes
                        if (cns->len >= 5 && memcmp(cns->chars + cns->len - 5, "Error", 5) == 0)
                            return (Item){.item = s2it(heap_create_name("[object Error]", 14))};
                    }
                }
                // Check for Math object
                found = false;
                js_map_get_fast(m, "__is_math__", 11, &found);
                if (found) return (Item){.item = s2it(heap_create_name("[object Math]", 13))};

                // Check for ArrayBuffer / DataView / TypedArray
                extern bool js_is_arraybuffer(Item val);
                extern bool js_is_dataview(Item val);
                if (js_is_arraybuffer(this_val))
                    return (Item){.item = s2it(heap_create_name("[object ArrayBuffer]", 20))};
                if (js_is_dataview(this_val))
                    return (Item){.item = s2it(heap_create_name("[object DataView]", 17))};
                const char* ta_name = js_typed_array_type_name(this_val);
                if (ta_name) {
                    char buf[64];
                    int blen = snprintf(buf, sizeof(buf), "[object %s]", ta_name);
                    return (Item){.item = s2it(heap_create_name(buf, blen))};
                }

                // Check for Promise
                found = false;
                js_map_get_fast(m, "__promise_idx", 13, &found);
                if (found) return (Item){.item = s2it(heap_create_name("[object Promise]", 16))};
            }
            return (Item){.item = s2it(heap_create_name("[object Object]", 15))};
        }
        return (Item){.item = s2it(heap_create_name("[object Object]", 15))};
    }
    case JS_BUILTIN_OBJ_VALUE_OF: {
        // Wrapper objects return their __primitiveValue__
        if (get_type_id(this_val) == LMD_TYPE_MAP) {
            bool own_pv = false;
            Item pv = js_map_get_fast(this_val.map, "__primitiveValue__", 18, &own_pv);
            if (own_pv) return pv;
        }
        return this_val;
    }
    case JS_BUILTIN_OBJ_IS_PROTOTYPE_OF: {
        // v18g: Check if this_val is in the prototype chain of arg0
        // this_val.isPrototypeOf(obj) → walks obj's prototype chain looking for this_val
        if (arg_count < 1) return (Item){.item = ITEM_FALSE};
        Item target = args[0];
        // Walk the prototype chain of target
        for (int depth = 0; depth < 32; depth++) {
            Item proto = js_get_prototype_of(target);
            if (proto.item == ItemNull.item || get_type_id(proto) == LMD_TYPE_NULL) break;
            if (proto.item == this_val.item) return (Item){.item = ITEM_TRUE};
            // For next iteration, we need to get proto's proto
            // But js_get_prototype_of on a prototype object may not work the same way
            // Use raw __proto__ from here
            target = proto;
        }
        return (Item){.item = ITEM_FALSE};
    }
    case JS_BUILTIN_OBJ_TO_LOCALE_STRING: {
        // Object.prototype.toLocaleString() — per spec: Invoke(this, "toString")
        // Step 1: Throw TypeError if this is null or undefined
        TypeId tid = get_type_id(this_val);
        if (tid == LMD_TYPE_NULL || tid == LMD_TYPE_UNDEFINED ||
            this_val.item == ITEM_NULL || this_val.item == ITEM_JS_UNDEFINED) {
            Item tn = (Item){.item = s2it(heap_create_name("TypeError"))};
            Item msg = (Item){.item = s2it(heap_create_name("Cannot convert undefined or null to object"))};
            js_throw_value(js_new_error_with_name(tn, msg));
            return ItemNull;
        }
        if (tid == LMD_TYPE_MAP || tid == LMD_TYPE_ELEMENT) {
            Item ts_key = (Item){.item = s2it(heap_create_name("toString", 8))};
            Item ts_fn = js_property_get(this_val, ts_key);
            if (get_type_id(ts_fn) == LMD_TYPE_FUNC) {
                return js_call_function(ts_fn, this_val, nullptr, 0);
            }
        }
        return js_to_string(this_val);
    }

    // Array.prototype methods - delegate to js_map_method which handles arrays
    case JS_BUILTIN_ARR_PUSH:
    case JS_BUILTIN_ARR_POP:
    case JS_BUILTIN_ARR_SHIFT:
    case JS_BUILTIN_ARR_UNSHIFT:
    case JS_BUILTIN_ARR_JOIN:
    case JS_BUILTIN_ARR_SLICE:
    case JS_BUILTIN_ARR_SPLICE:
    case JS_BUILTIN_ARR_INDEX_OF:
    case JS_BUILTIN_ARR_INCLUDES:
    case JS_BUILTIN_ARR_MAP:
    case JS_BUILTIN_ARR_FILTER:
    case JS_BUILTIN_ARR_REDUCE:
    case JS_BUILTIN_ARR_FOR_EACH:
    case JS_BUILTIN_ARR_FIND:
    case JS_BUILTIN_ARR_FIND_INDEX:
    case JS_BUILTIN_ARR_SOME:
    case JS_BUILTIN_ARR_EVERY:
    case JS_BUILTIN_ARR_SORT:
    case JS_BUILTIN_ARR_REVERSE:
    case JS_BUILTIN_ARR_CONCAT:
    case JS_BUILTIN_ARR_FLAT:
    case JS_BUILTIN_ARR_FLAT_MAP:
    case JS_BUILTIN_ARR_FILL:
    case JS_BUILTIN_ARR_COPY_WITHIN:
    case JS_BUILTIN_ARR_TO_STRING:
    case JS_BUILTIN_ARR_KEYS:
    case JS_BUILTIN_ARR_VALUES:
    case JS_BUILTIN_ARR_ENTRIES:
    case JS_BUILTIN_ARR_AT:
    case JS_BUILTIN_ARR_LAST_INDEX_OF:
    case JS_BUILTIN_ARR_REDUCE_RIGHT:
    case JS_BUILTIN_ARR_FIND_LAST:
    case JS_BUILTIN_ARR_FIND_LAST_INDEX:
    case JS_BUILTIN_ARR_TO_SORTED:
    case JS_BUILTIN_ARR_TO_REVERSED:
    case JS_BUILTIN_ARR_TO_SPLICED:
    case JS_BUILTIN_ARR_WITH: {
        // Map builtin_id to method name and delegate to js_map_method
        static const char* arr_method_names[] = {
            [JS_BUILTIN_ARR_PUSH - JS_BUILTIN_ARR_PUSH] = "push",
            [JS_BUILTIN_ARR_POP - JS_BUILTIN_ARR_PUSH] = "pop",
            [JS_BUILTIN_ARR_SHIFT - JS_BUILTIN_ARR_PUSH] = "shift",
            [JS_BUILTIN_ARR_UNSHIFT - JS_BUILTIN_ARR_PUSH] = "unshift",
            [JS_BUILTIN_ARR_JOIN - JS_BUILTIN_ARR_PUSH] = "join",
            [JS_BUILTIN_ARR_SLICE - JS_BUILTIN_ARR_PUSH] = "slice",
            [JS_BUILTIN_ARR_SPLICE - JS_BUILTIN_ARR_PUSH] = "splice",
            [JS_BUILTIN_ARR_INDEX_OF - JS_BUILTIN_ARR_PUSH] = "indexOf",
            [JS_BUILTIN_ARR_INCLUDES - JS_BUILTIN_ARR_PUSH] = "includes",
            [JS_BUILTIN_ARR_MAP - JS_BUILTIN_ARR_PUSH] = "map",
            [JS_BUILTIN_ARR_FILTER - JS_BUILTIN_ARR_PUSH] = "filter",
            [JS_BUILTIN_ARR_REDUCE - JS_BUILTIN_ARR_PUSH] = "reduce",
            [JS_BUILTIN_ARR_FOR_EACH - JS_BUILTIN_ARR_PUSH] = "forEach",
            [JS_BUILTIN_ARR_FIND - JS_BUILTIN_ARR_PUSH] = "find",
            [JS_BUILTIN_ARR_FIND_INDEX - JS_BUILTIN_ARR_PUSH] = "findIndex",
            [JS_BUILTIN_ARR_SOME - JS_BUILTIN_ARR_PUSH] = "some",
            [JS_BUILTIN_ARR_EVERY - JS_BUILTIN_ARR_PUSH] = "every",
            [JS_BUILTIN_ARR_SORT - JS_BUILTIN_ARR_PUSH] = "sort",
            [JS_BUILTIN_ARR_REVERSE - JS_BUILTIN_ARR_PUSH] = "reverse",
            [JS_BUILTIN_ARR_CONCAT - JS_BUILTIN_ARR_PUSH] = "concat",
            [JS_BUILTIN_ARR_FLAT - JS_BUILTIN_ARR_PUSH] = "flat",
            [JS_BUILTIN_ARR_FLAT_MAP - JS_BUILTIN_ARR_PUSH] = "flatMap",
            [JS_BUILTIN_ARR_FILL - JS_BUILTIN_ARR_PUSH] = "fill",
            [JS_BUILTIN_ARR_COPY_WITHIN - JS_BUILTIN_ARR_PUSH] = "copyWithin",
            [JS_BUILTIN_ARR_TO_STRING - JS_BUILTIN_ARR_PUSH] = "toString",
            [JS_BUILTIN_ARR_KEYS - JS_BUILTIN_ARR_PUSH] = "keys",
            [JS_BUILTIN_ARR_VALUES - JS_BUILTIN_ARR_PUSH] = "values",
            [JS_BUILTIN_ARR_ENTRIES - JS_BUILTIN_ARR_PUSH] = "entries",
            [JS_BUILTIN_ARR_AT - JS_BUILTIN_ARR_PUSH] = "at",
            [JS_BUILTIN_ARR_LAST_INDEX_OF - JS_BUILTIN_ARR_PUSH] = "lastIndexOf",
            [JS_BUILTIN_ARR_REDUCE_RIGHT - JS_BUILTIN_ARR_PUSH] = "reduceRight",
            [JS_BUILTIN_ARR_FIND_LAST - JS_BUILTIN_ARR_PUSH] = "findLast",
            [JS_BUILTIN_ARR_FIND_LAST_INDEX - JS_BUILTIN_ARR_PUSH] = "findLastIndex",
            [JS_BUILTIN_ARR_TO_SORTED - JS_BUILTIN_ARR_PUSH] = "toSorted",
            [JS_BUILTIN_ARR_TO_REVERSED - JS_BUILTIN_ARR_PUSH] = "toReversed",
            [JS_BUILTIN_ARR_TO_SPLICED - JS_BUILTIN_ARR_PUSH] = "toSpliced",
            [JS_BUILTIN_ARR_WITH - JS_BUILTIN_ARR_PUSH] = "with",
        };
        int idx = builtin_id - JS_BUILTIN_ARR_PUSH;
        const char* name = arr_method_names[idx];
        Item method_name = {.item = s2it(heap_create_name(name, strlen(name)))};
        // v18c: Throw TypeError if this is null/undefined (ES spec §22.1.3)
        TypeId this_type = get_type_id(this_val);
        if (this_val.item == ITEM_JS_UNDEFINED || this_val.item == ITEM_NULL ||
            this_type == LMD_TYPE_NULL) {
            Item type_name = (Item){.item = s2it(heap_create_name("TypeError"))};
            char msg[128];
            snprintf(msg, sizeof(msg), "Cannot read properties of %s",
                this_val.item == ITEM_JS_UNDEFINED ? "undefined" : "null");
            Item msg_item = (Item){.item = s2it(heap_create_name(msg, strlen(msg)))};
            Item error = js_new_error_with_name(type_name, msg_item);
            js_throw_value(error);
            return ItemNull;
        }
        // Route to js_array_method for actual arrays, js_map_method for maps/typed arrays.
        // Do NOT call js_map_method for plain MAPs — it would recurse through
        // the property access fallback which finds the builtin again.
        if (this_type == LMD_TYPE_ARRAY) {
            js_array_method_real_this = this_val;
            Item result = js_array_method(this_val, method_name, args, arg_count);
            js_array_method_real_this = (Item){0};
            return result;
        }
        // ES spec §23.1.3.30: Array.prototype.toString
        // 1. Let array be ToObject(this)  2. Let func = Get(array, "join")
        // 3. If IsCallable(func), return Call(func, array)
        // 4. Otherwise, return Object.prototype.toString(array)
        if (builtin_id == JS_BUILTIN_ARR_TO_STRING) {
            Item wrapped = js_to_object(this_val);
            // Check for own "join" property or explicit prototype chain join
            // (don't use js_property_get which has generic builtin fallback for all MAPs)
            Item join_fn = ItemNull;
            if (get_type_id(wrapped) == LMD_TYPE_MAP) {
                bool own_join = false;
                join_fn = js_map_get_fast(wrapped.map, "join", 4, &own_join);
                if (!own_join || js_is_deleted_sentinel(join_fn)) {
                    Item join_key = (Item){.item = s2it(heap_create_name("join", 4))};
                    join_fn = js_prototype_lookup(wrapped, join_key);
                }
            }
            if (join_fn.item != ItemNull.item && get_type_id(join_fn) == LMD_TYPE_FUNC) {
                return js_call_function(join_fn, wrapped, args, arg_count);
            }
            // No join method — fall back to Object.prototype.toString
            return js_dispatch_builtin(JS_BUILTIN_OBJ_TO_STRING, wrapped, args, arg_count);
        }
        // For MAP objects (typed arrays, data views), delegate to js_map_method
        // which handles them. For plain Maps, convert to array-like and delegate.
        if (this_type == LMD_TYPE_MAP) {
            if (js_is_typed_array(this_val) || js_is_dataview(this_val)) {
                return js_map_method(this_val, method_name, args, arg_count);
            }
            // Plain Map: treat as array-like object (has length + numeric indices)
            js_array_method_real_this = this_val;
            Item temp_arr = js_array_like_to_array(this_val);
            Item result = js_array_method(temp_arr, method_name, args, arg_count);
            js_array_method_real_this = (Item){0};
            return result;
        }
        // String, Number, Boolean, etc. — ToObject then convert to array-like
        // Strings: wrap to String object but use raw string for array-like conversion
        // (wrapper objects don't support indexed character access yet)
        {
            Item wrapped = js_to_object(this_val);
            js_array_method_real_this = wrapped;
            // For strings, use the raw primitive for array-like conversion
            // since js_property_get on strings supports indexed char access
            Item source = (this_type == LMD_TYPE_STRING) ? this_val : wrapped;
            Item temp_arr = js_array_like_to_array(source);
            Item result = js_array_method(temp_arr, method_name, args, arg_count);
            js_array_method_real_this = (Item){0};
            return result;
        }
    }

    // Function.prototype methods
    case JS_BUILTIN_FUNC_CALL: {
        // fn.call(thisArg, ...args) → js_call_function(fn=this_val, thisArg=arg0, rest)
        Item* rest_args = arg_count > 1 ? args + 1 : NULL;
        int rest_count = arg_count > 1 ? arg_count - 1 : 0;
        return js_call_function(this_val, arg0, rest_args, rest_count);
    }
    case JS_BUILTIN_FUNC_APPLY:
        return js_apply_function(this_val, arg0, arg1);
    case JS_BUILTIN_FUNC_BIND:
        return js_func_bind(this_val, arg0, arg_count > 1 ? args + 1 : NULL, arg_count > 1 ? arg_count - 1 : 0);
    case JS_BUILTIN_FUNC_TO_STRING: {
        if (get_type_id(this_val) == LMD_TYPE_FUNC) {
            JsFunction* fn = (JsFunction*)this_val.function;
            StrBuf* sb = strbuf_new();
            if (fn->flags & JS_FUNC_FLAG_GENERATOR)
                strbuf_append_str_n(sb, "function* ", 10);
            else
                strbuf_append_str_n(sb, "function ", 9);
            if (fn->name && fn->name->len > 0) {
                // NativeFunction syntax allows only a single IdentifierName (no spaces).
                // Bound functions have names like "bound f" — use only first word.
                int name_len = fn->name->len;
                for (int i = 0; i < fn->name->len; i++) {
                    if (fn->name->chars[i] == ' ') { name_len = i; break; }
                }
                strbuf_append_str_n(sb, fn->name->chars, name_len);
            }
            strbuf_append_str_n(sb, "() { [native code] }", 20);
            String* result = heap_create_name(sb->str, sb->length);
            strbuf_free(sb);
            return (Item){.item = s2it(result)};
        }
        return (Item){.item = s2it(heap_create_name("function () { [native code] }", 29))};
    }

    // String.prototype methods - delegate to js_string_method
    case JS_BUILTIN_STR_CHAR_AT:
    case JS_BUILTIN_STR_CHAR_CODE_AT:
    case JS_BUILTIN_STR_INDEX_OF:
    case JS_BUILTIN_STR_INCLUDES:
    case JS_BUILTIN_STR_SLICE:
    case JS_BUILTIN_STR_SUBSTRING:
    case JS_BUILTIN_STR_TO_LOWER_CASE:
    case JS_BUILTIN_STR_TO_UPPER_CASE:
    case JS_BUILTIN_STR_TRIM:
    case JS_BUILTIN_STR_SPLIT:
    case JS_BUILTIN_STR_REPLACE:
    case JS_BUILTIN_STR_MATCH:
    case JS_BUILTIN_STR_SEARCH:
    case JS_BUILTIN_STR_STARTS_WITH:
    case JS_BUILTIN_STR_ENDS_WITH:
    case JS_BUILTIN_STR_REPEAT:
    case JS_BUILTIN_STR_PAD_START:
    case JS_BUILTIN_STR_PAD_END:
    case JS_BUILTIN_STR_TO_STRING:
    case JS_BUILTIN_STR_VALUE_OF:
    case JS_BUILTIN_STR_TRIM_START:
    case JS_BUILTIN_STR_TRIM_END:
    case JS_BUILTIN_STR_CODE_POINT_AT:
    case JS_BUILTIN_STR_NORMALIZE:
    case JS_BUILTIN_STR_CONCAT:
    case JS_BUILTIN_STR_AT:
    case JS_BUILTIN_STR_LAST_INDEX_OF:
    case JS_BUILTIN_STR_LOCALE_COMPARE:
    case JS_BUILTIN_STR_REPLACE_ALL:
    case JS_BUILTIN_STR_MATCH_ALL: {
        static const char* str_method_names[] = {
            [JS_BUILTIN_STR_CHAR_AT - JS_BUILTIN_STR_CHAR_AT] = "charAt",
            [JS_BUILTIN_STR_CHAR_CODE_AT - JS_BUILTIN_STR_CHAR_AT] = "charCodeAt",
            [JS_BUILTIN_STR_INDEX_OF - JS_BUILTIN_STR_CHAR_AT] = "indexOf",
            [JS_BUILTIN_STR_INCLUDES - JS_BUILTIN_STR_CHAR_AT] = "includes",
            [JS_BUILTIN_STR_SLICE - JS_BUILTIN_STR_CHAR_AT] = "slice",
            [JS_BUILTIN_STR_SUBSTRING - JS_BUILTIN_STR_CHAR_AT] = "substring",
            [JS_BUILTIN_STR_TO_LOWER_CASE - JS_BUILTIN_STR_CHAR_AT] = "toLowerCase",
            [JS_BUILTIN_STR_TO_UPPER_CASE - JS_BUILTIN_STR_CHAR_AT] = "toUpperCase",
            [JS_BUILTIN_STR_TRIM - JS_BUILTIN_STR_CHAR_AT] = "trim",
            [JS_BUILTIN_STR_SPLIT - JS_BUILTIN_STR_CHAR_AT] = "split",
            [JS_BUILTIN_STR_REPLACE - JS_BUILTIN_STR_CHAR_AT] = "replace",
            [JS_BUILTIN_STR_MATCH - JS_BUILTIN_STR_CHAR_AT] = "match",
            [JS_BUILTIN_STR_SEARCH - JS_BUILTIN_STR_CHAR_AT] = "search",
            [JS_BUILTIN_STR_STARTS_WITH - JS_BUILTIN_STR_CHAR_AT] = "startsWith",
            [JS_BUILTIN_STR_ENDS_WITH - JS_BUILTIN_STR_CHAR_AT] = "endsWith",
            [JS_BUILTIN_STR_REPEAT - JS_BUILTIN_STR_CHAR_AT] = "repeat",
            [JS_BUILTIN_STR_PAD_START - JS_BUILTIN_STR_CHAR_AT] = "padStart",
            [JS_BUILTIN_STR_PAD_END - JS_BUILTIN_STR_CHAR_AT] = "padEnd",
            [JS_BUILTIN_STR_TO_STRING - JS_BUILTIN_STR_CHAR_AT] = "toString",
            [JS_BUILTIN_STR_VALUE_OF - JS_BUILTIN_STR_CHAR_AT] = "valueOf",
            [JS_BUILTIN_STR_TRIM_START - JS_BUILTIN_STR_CHAR_AT] = "trimStart",
            [JS_BUILTIN_STR_TRIM_END - JS_BUILTIN_STR_CHAR_AT] = "trimEnd",
            [JS_BUILTIN_STR_CODE_POINT_AT - JS_BUILTIN_STR_CHAR_AT] = "codePointAt",
            [JS_BUILTIN_STR_NORMALIZE - JS_BUILTIN_STR_CHAR_AT] = "normalize",
            [JS_BUILTIN_STR_CONCAT - JS_BUILTIN_STR_CHAR_AT] = "concat",
            [JS_BUILTIN_STR_AT - JS_BUILTIN_STR_CHAR_AT] = "at",
            [JS_BUILTIN_STR_LAST_INDEX_OF - JS_BUILTIN_STR_CHAR_AT] = "lastIndexOf",
            [JS_BUILTIN_STR_LOCALE_COMPARE - JS_BUILTIN_STR_CHAR_AT] = "localeCompare",
            [JS_BUILTIN_STR_REPLACE_ALL - JS_BUILTIN_STR_CHAR_AT] = "replaceAll",
            [JS_BUILTIN_STR_MATCH_ALL - JS_BUILTIN_STR_CHAR_AT] = "matchAll",
        };
        int idx = builtin_id - JS_BUILTIN_STR_CHAR_AT;
        const char* name = str_method_names[idx];
        Item method_name = {.item = s2it(heap_create_name(name, strlen(name)))};
        // v18c: Throw TypeError if this is null/undefined (ES spec §21.1.3)
        {
            TypeId st = get_type_id(this_val);
            if (this_val.item == ITEM_JS_UNDEFINED || this_val.item == ITEM_NULL ||
                st == LMD_TYPE_NULL) {
                Item type_name = (Item){.item = s2it(heap_create_name("TypeError"))};
                Item msg = (Item){.item = s2it(heap_create_name("Cannot read properties of null or undefined"))};
                Item error = js_new_error_with_name(type_name, msg);
                js_throw_value(error);
                return ItemNull;
            }
        }
        return js_string_method(this_val, method_name, args, arg_count);
    }

    // v18k: Object static methods — accessible as first-class function values
    // v18l: ES5 requires TypeError for non-object arguments on most Object static methods
    case JS_BUILTIN_OBJECT_DEFINE_PROPERTY:
    case JS_BUILTIN_OBJECT_DEFINE_PROPERTIES:
    case JS_BUILTIN_OBJECT_GET_OWN_PROPERTY_DESCRIPTOR:
    case JS_BUILTIN_OBJECT_GET_OWN_PROPERTY_DESCRIPTORS:
    case JS_BUILTIN_OBJECT_GET_OWN_PROPERTY_NAMES:
    case JS_BUILTIN_OBJECT_GET_OWN_PROPERTY_SYMBOLS:
    case JS_BUILTIN_OBJECT_KEYS:
    case JS_BUILTIN_OBJECT_VALUES:
    case JS_BUILTIN_OBJECT_ENTRIES:
    case JS_BUILTIN_OBJECT_FREEZE:
    case JS_BUILTIN_OBJECT_IS_FROZEN:
    case JS_BUILTIN_OBJECT_SEAL:
    case JS_BUILTIN_OBJECT_IS_SEALED:
    case JS_BUILTIN_OBJECT_PREVENT_EXTENSIONS:
    case JS_BUILTIN_OBJECT_IS_EXTENSIBLE:
    case JS_BUILTIN_OBJECT_GET_PROTOTYPE_OF:
    case JS_BUILTIN_OBJECT_SET_PROTOTYPE_OF: {
        // ES6+ §19.1.2.*: Handle primitives per spec
        TypeId arg0_type = get_type_id(arg0);
        if (arg0_type != LMD_TYPE_MAP && arg0_type != LMD_TYPE_ARRAY &&
            arg0_type != LMD_TYPE_FUNC && arg0_type != LMD_TYPE_ELEMENT) {
            // ES6: freeze/seal/preventExtensions on non-objects return the argument
            if (builtin_id == JS_BUILTIN_OBJECT_FREEZE ||
                builtin_id == JS_BUILTIN_OBJECT_SEAL ||
                builtin_id == JS_BUILTIN_OBJECT_PREVENT_EXTENSIONS) {
                return arg0;
            }
            // ES6: isFrozen/isSealed on non-objects return true
            if (builtin_id == JS_BUILTIN_OBJECT_IS_FROZEN ||
                builtin_id == JS_BUILTIN_OBJECT_IS_SEALED) {
                return (Item){.item = b2it(true)};
            }
            // ES6: isExtensible on non-objects returns false
            if (builtin_id == JS_BUILTIN_OBJECT_IS_EXTENSIBLE) {
                return (Item){.item = b2it(false)};
            }
            // ES6: defineProperty/defineProperties still throw TypeError on non-objects
            if (builtin_id == JS_BUILTIN_OBJECT_DEFINE_PROPERTY ||
                builtin_id == JS_BUILTIN_OBJECT_DEFINE_PROPERTIES) {
                Item type_name = (Item){.item = s2it(heap_create_name("TypeError"))};
                Item msg = (Item){.item = s2it(heap_create_name("Object.defineProperty called on non-object"))};
                Item error = js_new_error_with_name(type_name, msg);
                js_throw_value(error);
                return ItemNull;
            }
            // ES6: setPrototypeOf on non-objects returns the argument (after type check for proto)
            if (builtin_id == JS_BUILTIN_OBJECT_SET_PROTOTYPE_OF) {
                return arg0;
            }
            // ES6: For keys/values/entries/getOwnPropertyNames/getPrototypeOf etc.,
            // null/undefined still throw TypeError; other primitives get ToObject
            if (arg0.item == ITEM_NULL || arg0.item == ITEM_JS_UNDEFINED || arg0.item == 0) {
                Item type_name = (Item){.item = s2it(heap_create_name("TypeError"))};
                Item msg = (Item){.item = s2it(heap_create_name("Cannot convert undefined or null to object"))};
                Item error = js_new_error_with_name(type_name, msg);
                js_throw_value(error);
                return ItemNull;
            }
            // ES6: ToObject for primitives — convert string/number/boolean to wrapper
            // For string: keys are character indices "0".."n-1", plus "length"
            if (arg0_type == LMD_TYPE_STRING) {
                String* str = it2s(arg0);
                int slen = str ? (int)str->len : 0;
                if (builtin_id == JS_BUILTIN_OBJECT_KEYS) {
                    Item arr = js_array_new(slen);
                    for (int i = 0; i < slen; i++) {
                        char buf[16];
                        int blen = snprintf(buf, sizeof(buf), "%d", i);
                        js_array_push(arr, (Item){.item = s2it(heap_create_name(buf, blen))});
                    }
                    return arr;
                }
                if (builtin_id == JS_BUILTIN_OBJECT_GET_OWN_PROPERTY_NAMES) {
                    Item arr = js_array_new(slen + 1);
                    for (int i = 0; i < slen; i++) {
                        char buf[16];
                        int blen = snprintf(buf, sizeof(buf), "%d", i);
                        js_array_push(arr, (Item){.item = s2it(heap_create_name(buf, blen))});
                    }
                    js_array_push(arr, (Item){.item = s2it(heap_create_name("length", 6))});
                    return arr;
                }
                if (builtin_id == JS_BUILTIN_OBJECT_VALUES) {
                    Item arr = js_array_new(slen);
                    for (int i = 0; i < slen; i++) {
                        js_array_push(arr, (Item){.item = s2it(heap_create_name(str->chars + i, 1))});
                    }
                    return arr;
                }
                if (builtin_id == JS_BUILTIN_OBJECT_ENTRIES) {
                    Item arr = js_array_new(slen);
                    for (int i = 0; i < slen; i++) {
                        Item entry = js_array_new(2);
                        char buf[16];
                        int blen = snprintf(buf, sizeof(buf), "%d", i);
                        js_array_push(entry, (Item){.item = s2it(heap_create_name(buf, blen))});
                        js_array_push(entry, (Item){.item = s2it(heap_create_name(str->chars + i, 1))});
                        js_array_push(arr, entry);
                    }
                    return arr;
                }
                if (builtin_id == JS_BUILTIN_OBJECT_GET_PROTOTYPE_OF) {
                    Item ctor = js_get_constructor((Item){.item = s2it(heap_create_name("String", 6))});
                    return js_property_get(ctor, (Item){.item = s2it(heap_create_name("prototype", 9))});
                }
            }
            // For number/boolean/other primitives: keys/values/entries return []
            if (builtin_id == JS_BUILTIN_OBJECT_KEYS ||
                builtin_id == JS_BUILTIN_OBJECT_VALUES ||
                builtin_id == JS_BUILTIN_OBJECT_ENTRIES ||
                builtin_id == JS_BUILTIN_OBJECT_GET_OWN_PROPERTY_NAMES ||
                builtin_id == JS_BUILTIN_OBJECT_GET_OWN_PROPERTY_SYMBOLS) {
                return js_array_new(0);
            }
            if (builtin_id == JS_BUILTIN_OBJECT_GET_OWN_PROPERTY_DESCRIPTOR ||
                builtin_id == JS_BUILTIN_OBJECT_GET_OWN_PROPERTY_DESCRIPTORS) {
                return (builtin_id == JS_BUILTIN_OBJECT_GET_OWN_PROPERTY_DESCRIPTORS) ? js_new_object() : make_js_undefined();
            }
            if (builtin_id == JS_BUILTIN_OBJECT_GET_PROTOTYPE_OF) {
                // Return the appropriate prototype for the primitive type
                if (arg0_type == LMD_TYPE_INT || arg0_type == LMD_TYPE_FLOAT) {
                    Item ctor = js_get_constructor((Item){.item = s2it(heap_create_name("Number", 6))});
                    return js_property_get(ctor, (Item){.item = s2it(heap_create_name("prototype", 9))});
                }
                if (arg0_type == LMD_TYPE_BOOL) {
                    Item ctor = js_get_constructor((Item){.item = s2it(heap_create_name("Boolean", 7))});
                    return js_property_get(ctor, (Item){.item = s2it(heap_create_name("prototype", 9))});
                }
                if (arg0_type == LMD_TYPE_STRING || arg0_type == LMD_TYPE_SYMBOL) {
                    const char* cn = arg0_type == LMD_TYPE_STRING ? "String" : "Symbol";
                    Item ctor = js_get_constructor((Item){.item = s2it(heap_create_name(cn, strlen(cn)))});
                    return js_property_get(ctor, (Item){.item = s2it(heap_create_name("prototype", 9))});
                }
                return ItemNull;
            }
            // Fallback: throw TypeError for anything else
            Item type_name = (Item){.item = s2it(heap_create_name("TypeError"))};
            Item msg = (Item){.item = s2it(heap_create_name("Object.* called on non-object"))};
            Item error = js_new_error_with_name(type_name, msg);
            js_throw_value(error);
            return ItemNull;
        }
        switch (builtin_id) {
        case JS_BUILTIN_OBJECT_DEFINE_PROPERTY:
            return js_object_define_property(arg0, arg1, arg2);
        case JS_BUILTIN_OBJECT_DEFINE_PROPERTIES:
            return js_object_define_properties(arg0, arg1);
        case JS_BUILTIN_OBJECT_GET_OWN_PROPERTY_DESCRIPTOR:
            return js_object_get_own_property_descriptor(arg0, arg1);
        case JS_BUILTIN_OBJECT_GET_OWN_PROPERTY_DESCRIPTORS:
            return js_object_get_own_property_descriptors(arg0);
        case JS_BUILTIN_OBJECT_GET_OWN_PROPERTY_NAMES:
            return js_object_get_own_property_names(arg0);
        case JS_BUILTIN_OBJECT_GET_OWN_PROPERTY_SYMBOLS:
            return js_object_get_own_property_symbols(arg0);
        case JS_BUILTIN_OBJECT_KEYS:
            return js_object_keys(arg0);
        case JS_BUILTIN_OBJECT_VALUES:
            return js_object_values(arg0);
        case JS_BUILTIN_OBJECT_ENTRIES:
            return js_object_entries(arg0);
        case JS_BUILTIN_OBJECT_FREEZE:
            return js_object_freeze(arg0);
        case JS_BUILTIN_OBJECT_IS_FROZEN:
            return js_object_is_frozen(arg0);
        case JS_BUILTIN_OBJECT_SEAL:
            return js_object_seal(arg0);
        case JS_BUILTIN_OBJECT_IS_SEALED:
            return js_object_is_sealed(arg0);
        case JS_BUILTIN_OBJECT_PREVENT_EXTENSIONS:
            return js_object_prevent_extensions(arg0);
        case JS_BUILTIN_OBJECT_IS_EXTENSIBLE:
            return js_object_is_extensible(arg0);
        case JS_BUILTIN_OBJECT_GET_PROTOTYPE_OF:
            return js_get_prototype_of(arg0);
        case JS_BUILTIN_OBJECT_SET_PROTOTYPE_OF:
            js_set_prototype(arg0, arg1);
            return arg0;
        default: return ItemNull;
        }
    }
    case JS_BUILTIN_OBJECT_FROM_ENTRIES:
        return js_object_from_entries(arg0);
    case JS_BUILTIN_OBJECT_CREATE: {
        Item obj = js_object_create(arg0);
        if (arg_count >= 2 && arg1.item != ITEM_JS_UNDEFINED && arg1.item != ITEM_NULL) {
            js_object_define_properties(obj, arg1);
        }
        return obj;
    }
    case JS_BUILTIN_OBJECT_ASSIGN:
        if (arg_count <= 1) return js_object_assign(arg0, NULL, 0);
        return js_object_assign(arg0, args + 1, arg_count - 1);
    case JS_BUILTIN_OBJECT_IS:
        return js_object_is(arg0, arg1);
    case JS_BUILTIN_OBJECT_HAS_OWN:
        return js_has_own_property(arg0, arg1);
    // Array static methods
    case JS_BUILTIN_ARRAY_IS_ARRAY:
        return js_array_is_array(arg0);
    case JS_BUILTIN_ARRAY_FROM:
        return js_array_from(arg0);
    case JS_BUILTIN_ARRAY_OF: {
        // Array.of(...args) — creates array from arguments
        Item result = js_array_new(arg_count);
        for (int i = 0; i < arg_count; i++) {
            result.array->items[i] = args[i];
        }
        return result;
    }
    case JS_BUILTIN_ARRAY_ITER_NEXT: {
        // Array iterator .next() — this_val is the iterator object with __array__, __index__, __kind__
        String* arr_key = heap_create_name("__array__", 9);
        String* idx_key = heap_create_name("__index__", 9);
        String* kind_key = heap_create_name("__kind__", 8);
        Item arr_item = js_property_get(this_val, (Item){.item = s2it(arr_key)});
        Item idx_item = js_property_get(this_val, (Item){.item = s2it(idx_key)});
        Item kind_item = js_property_get(this_val, (Item){.item = s2it(kind_key)});
        int idx = (int)it2i(idx_item);
        int kind = (int)it2i(kind_item); // 0=keys, 1=values, 2=entries
        if (get_type_id(arr_item) != LMD_TYPE_ARRAY || idx >= arr_item.array->length) {
            // done — return {value: undefined, done: true}
            Item result = js_new_object();
            js_property_set(result, (Item){.item = s2it(heap_create_name("value", 5))}, make_js_undefined());
            js_property_set(result, (Item){.item = s2it(heap_create_name("done", 4))}, (Item){.item = b2it(true)});
            return result;
        }
        // advance index
        js_property_set(this_val, (Item){.item = s2it(idx_key)}, (Item){.item = i2it(idx + 1)});
        Item val;
        if (kind == 0) {
            val = (Item){.item = i2it(idx)};
        } else if (kind == 1) {
            val = arr_item.array->items[idx];
            if (val.item == JS_DELETED_SENTINEL_VAL) val = make_js_undefined();
        } else {
            // entries: [index, value]
            Item pair = js_array_new(2);
            Item elem = arr_item.array->items[idx];
            if (elem.item == JS_DELETED_SENTINEL_VAL) elem = make_js_undefined();
            pair.array->items[0] = (Item){.item = i2it(idx)};
            pair.array->items[1] = elem;
            val = pair;
        }
        // return {value: val, done: false}
        Item result = js_new_object();
        js_property_set(result, (Item){.item = s2it(heap_create_name("value", 5))}, val);
        js_property_set(result, (Item){.item = s2it(heap_create_name("done", 4))}, (Item){.item = b2it(false)});
        return result;
    }
    // String iterator
    case JS_BUILTIN_STRING_ITER: {
        // String.prototype[Symbol.iterator]() — creates a string iterator object
        // this_val is the string
        Item str_item = js_to_string(this_val);
        Item iter = js_new_object();
        js_property_set(iter, (Item){.item = s2it(heap_create_name("__string__", 10))}, str_item);
        js_property_set(iter, (Item){.item = s2it(heap_create_name("__index__", 9))}, (Item){.item = i2it(0)});
        Item next_fn = js_get_or_create_builtin(JS_BUILTIN_STRING_ITER_NEXT, "next", 0);
        js_property_set(iter, (Item){.item = s2it(heap_create_name("next", 4))}, next_fn);
        // Set Symbol.toStringTag for [object String Iterator]
        js_property_set(iter, (Item){.item = s2it(heap_create_name("__sym_4", 7))},
                         (Item){.item = s2it(heap_create_name("String Iterator", 15))});
        return iter;
    }
    case JS_BUILTIN_STRING_ITER_NEXT: {
        // String iterator .next() — iterates by Unicode codepoints
        Item str_item = js_property_get(this_val, (Item){.item = s2it(heap_create_name("__string__", 10))});
        Item idx_item = js_property_get(this_val, (Item){.item = s2it(heap_create_name("__index__", 9))});
        String* s = it2s(str_item);
        int idx = (int)it2i(idx_item);
        if (!s || idx >= (int)s->len) {
            // done
            Item result = js_new_object();
            js_property_set(result, (Item){.item = s2it(heap_create_name("value", 5))}, make_js_undefined());
            js_property_set(result, (Item){.item = s2it(heap_create_name("done", 4))}, (Item){.item = b2it(true)});
            return result;
        }
        // Decode one UTF-8 codepoint
        const unsigned char* p = (const unsigned char*)s->chars + idx;
        int remaining = (int)s->len - idx;
        int cp_len = 1;
        if ((*p & 0x80) == 0) cp_len = 1;
        else if ((*p & 0xE0) == 0xC0 && remaining >= 2) cp_len = 2;
        else if ((*p & 0xF0) == 0xE0 && remaining >= 3) cp_len = 3;
        else if ((*p & 0xF8) == 0xF0 && remaining >= 4) cp_len = 4;
        // Advance index
        js_property_set(this_val, (Item){.item = s2it(heap_create_name("__index__", 9))}, (Item){.item = i2it(idx + cp_len)});
        // Create single-codepoint string
        Item val = (Item){.item = s2it(heap_create_name(s->chars + idx, cp_len))};
        Item result = js_new_object();
        js_property_set(result, (Item){.item = s2it(heap_create_name("value", 5))}, val);
        js_property_set(result, (Item){.item = s2it(heap_create_name("done", 4))}, (Item){.item = b2it(false)});
        return result;
    }
    // Error.prototype.toString (generic)
    case JS_BUILTIN_ERR_TO_STRING: {
        // ES spec: Error.prototype.toString:
        // 1. Let O be the this value; if it's not an object, throw TypeError
        // 2. Let name = Get(O, "name"); if undefined, use "Error"
        // 3. Let msg = Get(O, "message"); if undefined, use ""
        // 4. If name is "", return msg
        // 5. If msg is "", return name
        // 6. Return name + ": " + msg
        Item name_key = (Item){.item = s2it(heap_create_name("name", 4))};
        Item msg_key = (Item){.item = s2it(heap_create_name("message", 7))};
        Item name_val = js_property_get(this_val, name_key);
        Item msg_val = js_property_get(this_val, msg_key);
        // Default name to "Error" if undefined
        const char* name_str = "Error";
        int name_len = 5;
        if (get_type_id(name_val) != LMD_TYPE_UNDEFINED && name_val.item != ITEM_JS_UNDEFINED) {
            Item name_s = js_to_string(name_val);
            String* ns = it2s(name_s);
            if (ns) { name_str = ns->chars; name_len = (int)ns->len; }
        }
        // Default message to "" if undefined
        const char* msg_str = "";
        int msg_len = 0;
        if (get_type_id(msg_val) != LMD_TYPE_UNDEFINED && msg_val.item != ITEM_JS_UNDEFINED) {
            Item msg_s = js_to_string(msg_val);
            String* ms = it2s(msg_s);
            if (ms) { msg_str = ms->chars; msg_len = (int)ms->len; }
        }
        if (name_len == 0) return (Item){.item = s2it(heap_create_name(msg_str, msg_len))};
        if (msg_len == 0) return (Item){.item = s2it(heap_create_name(name_str, name_len))};
        // name + ": " + msg
        int total = name_len + 2 + msg_len;
        char buf[1024];
        char* p = (total + 1 <= (int)sizeof(buf)) ? buf : (char*)malloc(total + 1);
        memcpy(p, name_str, name_len);
        p[name_len] = ':';
        p[name_len + 1] = ' ';
        memcpy(p + name_len + 2, msg_str, msg_len);
        Item result = (Item){.item = s2it(heap_create_name(p, total))};
        if (p != buf) free(p);
        return result;
    }
    case JS_BUILTIN_BOOL_TO_STRING: {
        // Boolean.prototype.toString: return "true" or "false"
        return js_to_string(this_val);
    }
    // Number static methods
    case JS_BUILTIN_NUMBER_IS_INTEGER:
        return js_number_is_integer(arg0);
    case JS_BUILTIN_NUMBER_IS_FINITE:
        return js_number_is_finite(arg0);
    case JS_BUILTIN_NUMBER_IS_NAN:
        return js_number_is_nan(arg0);
    case JS_BUILTIN_NUMBER_IS_SAFE_INTEGER:
        return js_number_is_safe_integer(arg0);
    case JS_BUILTIN_NUMBER_PARSE_INT:
        return js_parseInt(arg0, arg1);
    case JS_BUILTIN_NUMBER_PARSE_FLOAT:
        return js_parseFloat(arg0);

    // v18o: Number.prototype methods
    case JS_BUILTIN_NUM_TO_STRING:
    case JS_BUILTIN_NUM_VALUE_OF:
    case JS_BUILTIN_NUM_TO_FIXED:
    case JS_BUILTIN_NUM_TO_PRECISION:
    case JS_BUILTIN_NUM_TO_EXPONENTIAL: {
        // Extract number value from this_val (could be primitive or wrapper)
        Item num_val = this_val;
        TypeId tv_type = get_type_id(this_val);
        if (tv_type == LMD_TYPE_MAP) {
            // Number wrapper: extract __primitiveValue__
            bool pv_own = false;
            Item pv = js_map_get_fast(this_val.map, "__primitiveValue__", 18, &pv_own);
            if (pv_own) {
                num_val = pv;
            } else {
                Item tn = (Item){.item = s2it(heap_create_name("TypeError"))};
                Item msg = (Item){.item = s2it(heap_create_name("Number.prototype method requires that 'this' be a Number"))};
                js_throw_value(js_new_error_with_name(tn, msg));
                return ItemNull;
            }
        } else if (tv_type != LMD_TYPE_INT && tv_type != LMD_TYPE_FLOAT && tv_type != LMD_TYPE_INT64) {
            Item tn = (Item){.item = s2it(heap_create_name("TypeError"))};
            Item msg = (Item){.item = s2it(heap_create_name("Number.prototype method requires that 'this' be a Number"))};
            js_throw_value(js_new_error_with_name(tn, msg));
            return ItemNull;
        }
        if (builtin_id == JS_BUILTIN_NUM_VALUE_OF) {
            return num_val;
        }
        // Delegate to js_number_method which handles toString, toFixed, etc.
        static const char* num_method_names[] = {
            [JS_BUILTIN_NUM_TO_STRING - JS_BUILTIN_NUM_TO_STRING] = "toString",
            [JS_BUILTIN_NUM_VALUE_OF - JS_BUILTIN_NUM_TO_STRING] = "valueOf",
            [JS_BUILTIN_NUM_TO_FIXED - JS_BUILTIN_NUM_TO_STRING] = "toFixed",
            [JS_BUILTIN_NUM_TO_PRECISION - JS_BUILTIN_NUM_TO_STRING] = "toPrecision",
            [JS_BUILTIN_NUM_TO_EXPONENTIAL - JS_BUILTIN_NUM_TO_STRING] = "toExponential",
        };
        int nidx = builtin_id - JS_BUILTIN_NUM_TO_STRING;
        const char* mname = num_method_names[nidx];
        Item method_name = {.item = s2it(heap_create_name(mname, strlen(mname)))};
        return js_number_method(num_val, method_name, args, arg_count);
    }

    // Symbol.prototype.toString
    case JS_BUILTIN_SYM_TO_STRING:
        return js_symbol_to_string(this_val);

    // String.raw — tagged template / direct call
    case JS_BUILTIN_STRING_RAW:
        return js_string_raw(args, arg_count);
    case JS_BUILTIN_STRING_FROM_CODE_POINT: {
        if (arg_count == 1) return js_string_fromCodePoint(arg0);
        // multiple args — build array and pass
        Item arr = js_array_new(arg_count);
        for (int i = 0; i < arg_count; i++) arr.array->items[i] = args[i];
        return js_string_fromCodePoint_array(arr);
    }
    case JS_BUILTIN_STRING_FROM_CHAR_CODE: {
        if (arg_count == 1) return js_string_fromCodePoint(arg0); // fromCharCode is same for BMP
        Item arr = js_array_new(arg_count);
        for (int i = 0; i < arg_count; i++) arr.array->items[i] = args[i];
        return js_string_fromCodePoint_array(arr);
    }

    // Math methods called as first-class function values
    case JS_BUILTIN_MATH_ABS:
    case JS_BUILTIN_MATH_FLOOR:
    case JS_BUILTIN_MATH_CEIL:
    case JS_BUILTIN_MATH_ROUND:
    case JS_BUILTIN_MATH_SQRT:
    case JS_BUILTIN_MATH_POW:
    case JS_BUILTIN_MATH_MIN:
    case JS_BUILTIN_MATH_MAX:
    case JS_BUILTIN_MATH_LOG:
    case JS_BUILTIN_MATH_LOG10:
    case JS_BUILTIN_MATH_LOG2:
    case JS_BUILTIN_MATH_EXP:
    case JS_BUILTIN_MATH_SIN:
    case JS_BUILTIN_MATH_COS:
    case JS_BUILTIN_MATH_TAN:
    case JS_BUILTIN_MATH_SIGN:
    case JS_BUILTIN_MATH_TRUNC:
    case JS_BUILTIN_MATH_RANDOM:
    case JS_BUILTIN_MATH_ASIN:
    case JS_BUILTIN_MATH_ACOS:
    case JS_BUILTIN_MATH_ATAN:
    case JS_BUILTIN_MATH_ATAN2:
    case JS_BUILTIN_MATH_CBR:
    case JS_BUILTIN_MATH_HYPOT:
    case JS_BUILTIN_MATH_CLZ32:
    case JS_BUILTIN_MATH_FROUND:
    case JS_BUILTIN_MATH_IMUL:
    case JS_BUILTIN_MATH_SINH:
    case JS_BUILTIN_MATH_COSH:
    case JS_BUILTIN_MATH_TANH:
    case JS_BUILTIN_MATH_ASINH:
    case JS_BUILTIN_MATH_ACOSH:
    case JS_BUILTIN_MATH_ATANH:
    case JS_BUILTIN_MATH_EXPM1:
    case JS_BUILTIN_MATH_LOG1P: {
        static const char* math_names[] = {
            "abs", "floor", "ceil", "round", "sqrt", "pow", "min", "max",
            "log", "log10", "log2", "exp", "sin", "cos", "tan", "sign",
            "trunc", "random", "asin", "acos", "atan", "atan2", "cbrt",
            "hypot", "clz32", "fround", "imul", "sinh", "cosh", "tanh",
            "asinh", "acosh", "atanh", "expm1", "log1p"
        };
        int idx = builtin_id - JS_BUILTIN_MATH_ABS;
        const char* mname = math_names[idx];
        Item method_name = {.item = s2it(heap_create_name(mname, strlen(mname)))};
        return js_math_method(method_name, args, arg_count);
    }

    case JS_BUILTIN_JSON_PARSE: {
        Item arg0 = (arg_count > 0) ? args[0] : undef;
        if (arg_count > 1) {
            return js_json_parse_full(arg0, args[1]);
        }
        return js_json_parse(arg0);
    }
    case JS_BUILTIN_JSON_STRINGIFY: {
        Item arg0 = (arg_count > 0) ? args[0] : undef;
        if (arg_count > 1) {
            Item replacer = args[1];
            Item space = (arg_count > 2) ? args[2] : ItemNull;
            return js_json_stringify_full(arg0, replacer, space);
        }
        return js_json_stringify(arg0);
    }

    // v45: Date.prototype methods — dispatch to js_date_method / js_date_setter
    case JS_BUILTIN_DATE_GET_TIME:
        return js_date_method(this_val, 0);
    case JS_BUILTIN_DATE_GET_FULL_YEAR:
        return js_date_method(this_val, 1);
    case JS_BUILTIN_DATE_GET_MONTH:
        return js_date_method(this_val, 2);
    case JS_BUILTIN_DATE_GET_DATE:
        return js_date_method(this_val, 3);
    case JS_BUILTIN_DATE_GET_HOURS:
        return js_date_method(this_val, 4);
    case JS_BUILTIN_DATE_GET_MINUTES:
        return js_date_method(this_val, 5);
    case JS_BUILTIN_DATE_GET_SECONDS:
        return js_date_method(this_val, 6);
    case JS_BUILTIN_DATE_GET_MILLISECONDS:
        return js_date_method(this_val, 7);
    case JS_BUILTIN_DATE_TO_ISO_STRING:
        return js_date_method(this_val, 8);
    case JS_BUILTIN_DATE_TO_LOCALE_DATE_STRING:
        return js_date_method(this_val, 9);
    case JS_BUILTIN_DATE_GET_UTC_FULL_YEAR:
        return js_date_method(this_val, 10);
    case JS_BUILTIN_DATE_GET_UTC_MONTH:
        return js_date_method(this_val, 11);
    case JS_BUILTIN_DATE_GET_UTC_DATE:
        return js_date_method(this_val, 12);
    case JS_BUILTIN_DATE_GET_UTC_HOURS:
        return js_date_method(this_val, 13);
    case JS_BUILTIN_DATE_GET_UTC_MINUTES:
        return js_date_method(this_val, 14);
    case JS_BUILTIN_DATE_GET_UTC_SECONDS:
        return js_date_method(this_val, 15);
    case JS_BUILTIN_DATE_GET_UTC_MILLISECONDS:
        return js_date_method(this_val, 16);
    case JS_BUILTIN_DATE_TO_STRING:
        return js_date_method(this_val, 17);
    case JS_BUILTIN_DATE_GET_DAY:
        return js_date_setter(this_val, 40, undef, undef, undef, undef);
    case JS_BUILTIN_DATE_GET_UTC_DAY:
        return js_date_setter(this_val, 41, undef, undef, undef, undef);
    case JS_BUILTIN_DATE_GET_TIMEZONE_OFFSET:
        return js_date_setter(this_val, 42, undef, undef, undef, undef);
    case JS_BUILTIN_DATE_VALUE_OF:
        return js_date_setter(this_val, 43, undef, undef, undef, undef);
    case JS_BUILTIN_DATE_TO_JSON:
        return js_date_setter(this_val, 44, undef, undef, undef, undef);
    case JS_BUILTIN_DATE_TO_UTC_STRING:
        return js_date_setter(this_val, 45, undef, undef, undef, undef);
    case JS_BUILTIN_DATE_TO_DATE_STRING:
        return js_date_setter(this_val, 46, undef, undef, undef, undef);
    case JS_BUILTIN_DATE_TO_TIME_STRING:
        return js_date_setter(this_val, 47, undef, undef, undef, undef);
    case JS_BUILTIN_DATE_SET_TIME:
        return js_date_setter(this_val, 20, arg0, undef, undef, undef);
    case JS_BUILTIN_DATE_SET_FULL_YEAR: {
        Item a1 = (arg_count > 1) ? args[1] : undef;
        Item a2 = (arg_count > 2) ? args[2] : undef;
        return js_date_setter(this_val, 21, arg0, a1, a2, undef);
    }
    case JS_BUILTIN_DATE_SET_MONTH: {
        Item a1 = (arg_count > 1) ? args[1] : undef;
        return js_date_setter(this_val, 22, arg0, a1, undef, undef);
    }
    case JS_BUILTIN_DATE_SET_DATE:
        return js_date_setter(this_val, 23, arg0, undef, undef, undef);
    case JS_BUILTIN_DATE_SET_HOURS: {
        Item a1 = (arg_count > 1) ? args[1] : undef;
        Item a2 = (arg_count > 2) ? args[2] : undef;
        Item a3 = (arg_count > 3) ? args[3] : undef;
        return js_date_setter(this_val, 24, arg0, a1, a2, a3);
    }
    case JS_BUILTIN_DATE_SET_MINUTES: {
        Item a1 = (arg_count > 1) ? args[1] : undef;
        Item a2 = (arg_count > 2) ? args[2] : undef;
        return js_date_setter(this_val, 25, arg0, a1, a2, undef);
    }
    case JS_BUILTIN_DATE_SET_SECONDS: {
        Item a1 = (arg_count > 1) ? args[1] : undef;
        return js_date_setter(this_val, 26, arg0, a1, undef, undef);
    }
    case JS_BUILTIN_DATE_SET_MILLISECONDS:
        return js_date_setter(this_val, 27, arg0, undef, undef, undef);
    case JS_BUILTIN_DATE_SET_UTC_FULL_YEAR: {
        Item a1 = (arg_count > 1) ? args[1] : undef;
        Item a2 = (arg_count > 2) ? args[2] : undef;
        return js_date_setter(this_val, 30, arg0, a1, a2, undef);
    }
    case JS_BUILTIN_DATE_SET_UTC_MONTH: {
        Item a1 = (arg_count > 1) ? args[1] : undef;
        return js_date_setter(this_val, 31, arg0, a1, undef, undef);
    }
    case JS_BUILTIN_DATE_SET_UTC_DATE:
        return js_date_setter(this_val, 32, arg0, undef, undef, undef);
    case JS_BUILTIN_DATE_SET_UTC_HOURS: {
        Item a1 = (arg_count > 1) ? args[1] : undef;
        Item a2 = (arg_count > 2) ? args[2] : undef;
        Item a3 = (arg_count > 3) ? args[3] : undef;
        return js_date_setter(this_val, 33, arg0, a1, a2, a3);
    }
    case JS_BUILTIN_DATE_SET_UTC_MINUTES: {
        Item a1 = (arg_count > 1) ? args[1] : undef;
        Item a2 = (arg_count > 2) ? args[2] : undef;
        return js_date_setter(this_val, 34, arg0, a1, a2, undef);
    }
    case JS_BUILTIN_DATE_SET_UTC_SECONDS: {
        Item a1 = (arg_count > 1) ? args[1] : undef;
        return js_date_setter(this_val, 35, arg0, a1, undef, undef);
    }
    case JS_BUILTIN_DATE_SET_UTC_MILLISECONDS:
        return js_date_setter(this_val, 36, arg0, undef, undef, undef);

    // v45: Promise static methods
    case JS_BUILTIN_PROMISE_RESOLVE:
        return js_promise_resolve(arg0);
    case JS_BUILTIN_PROMISE_REJECT:
        return js_promise_reject(arg0);
    case JS_BUILTIN_PROMISE_ALL:
        return js_promise_all(arg0);
    case JS_BUILTIN_PROMISE_ALL_SETTLED:
        return js_promise_all_settled(arg0);
    case JS_BUILTIN_PROMISE_ANY:
        return js_promise_any(arg0);
    case JS_BUILTIN_PROMISE_RACE:
        return js_promise_race(arg0);

    // v45: Date static methods
    case JS_BUILTIN_DATE_NOW:
        return js_date_now();
    case JS_BUILTIN_DATE_PARSE:
        return js_date_parse(arg0);
    case JS_BUILTIN_DATE_UTC: {
        // Date.UTC takes up to 7 args — pack into array
        Item arr = js_array_new(arg_count);
        for (int i = 0; i < arg_count; i++) js_array_push(arr, args[i]);
        return js_date_utc(arr);
    }

    // v46: RegExp prototype methods
    case JS_BUILTIN_REGEXP_EXEC:
        return js_regex_exec(this_val, arg0);
    case JS_BUILTIN_REGEXP_TEST:
        return js_regex_test(this_val, arg0);
    case JS_BUILTIN_REGEXP_TO_STRING: {
        // /source/flags format
        Item src = js_property_get(this_val, (Item){.item = s2it(heap_create_name("source", 6))});
        Item flg = js_property_get(this_val, (Item){.item = s2it(heap_create_name("flags", 5))});
        const char* src_str = (get_type_id(src) == LMD_TYPE_STRING) ? it2s(src)->chars : "";
        int src_len = (get_type_id(src) == LMD_TYPE_STRING) ? (int)it2s(src)->len : 0;
        const char* flg_str = (get_type_id(flg) == LMD_TYPE_STRING) ? it2s(flg)->chars : "";
        int flg_len = (get_type_id(flg) == LMD_TYPE_STRING) ? (int)it2s(flg)->len : 0;
        char* buf = (char*)pool_calloc(js_input->pool, src_len + flg_len + 3);
        buf[0] = '/';
        memcpy(buf + 1, src_str, src_len);
        buf[1 + src_len] = '/';
        memcpy(buf + 2 + src_len, flg_str, flg_len);
        buf[2 + src_len + flg_len] = '\0';
        return (Item){.item = s2it(heap_create_name(buf, 2 + src_len + flg_len))};
    }

    // v83: RegExp Symbol methods
    case JS_BUILTIN_REGEXP_SYMBOL_MATCH:
        return js_regexp_symbol_match(this_val, arg0);
    case JS_BUILTIN_REGEXP_SYMBOL_REPLACE:
        return js_regexp_symbol_replace(this_val, arg0, (arg_count >= 2) ? args[1] : make_js_undefined());
    case JS_BUILTIN_REGEXP_SYMBOL_SEARCH:
        return js_regexp_symbol_search(this_val, arg0);
    case JS_BUILTIN_REGEXP_SYMBOL_SPLIT:
        return js_regexp_symbol_split(this_val, arg0, (arg_count >= 2) ? args[1] : make_js_undefined());

    // v55: Set/Map keys/values/entries — return arrays for backward compat
    case JS_BUILTIN_SET_VALUES:   // Set.prototype.values() / Set.prototype[@@iterator]()
    case JS_BUILTIN_SET_KEYS:     // Set.prototype.keys() — same as values for Set
    case JS_BUILTIN_SET_ENTRIES:  // Set.prototype.entries()
    case JS_BUILTIN_MAP_ENTRIES:  // Map.prototype.entries() / Map.prototype[@@iterator]()
    case JS_BUILTIN_MAP_KEYS:    // Map.prototype.keys()
    case JS_BUILTIN_MAP_VALUES:  // Map.prototype.values()
    {
        JsCollectionData* cd = js_get_collection_data(this_val);
        if (!cd) {
            Item tn = (Item){.item = s2it(heap_create_name("TypeError", 9))};
            Item msg = (Item){.item = s2it(heap_create_name("Method called on incompatible receiver"))};
            js_throw_value(js_new_error_with_name(tn, msg));
            return ItemNull;
        }
        // determine iteration mode: 0=values, 1=keys, 2=entries
        int mode = 0;
        if (builtin_id == JS_BUILTIN_SET_ENTRIES || builtin_id == JS_BUILTIN_MAP_ENTRIES) mode = 2;
        else if (builtin_id == JS_BUILTIN_MAP_KEYS) mode = 1;
        else if (builtin_id == JS_BUILTIN_MAP_VALUES) mode = 0;
        else if (builtin_id == JS_BUILTIN_SET_VALUES || builtin_id == JS_BUILTIN_SET_KEYS) mode = 0;
        // build array from insertion-order linked list
        Item arr = js_array_new(0);
        for (JsCollectionOrderNode* node = cd->order_head; node; node = node->next) {
            if (mode == 2) {
                // entries: [key, value] for Map; [value, value] for Set
                Item pair = js_array_new(0);
                if (cd->type == JS_COLLECTION_SET) {
                    js_array_push(pair, node->key);
                    js_array_push(pair, node->key);
                } else {
                    js_array_push(pair, node->key);
                    js_array_push(pair, node->value);
                }
                js_array_push(arr, pair);
            } else if (mode == 1) {
                js_array_push(arr, node->key);
            } else {
                js_array_push(arr, (cd->type == JS_COLLECTION_SET) ? node->key : node->value);
            }
        }
        return arr;
    }
    case JS_BUILTIN_COLL_ITER_NEXT: {
        // Collection iterator .next()
        if (get_type_id(this_val) != LMD_TYPE_MAP) {
            log_error("COLL_ITER_NEXT: this_val is not a MAP (type=%d)", get_type_id(this_val));
            Item result = js_new_object();
            js_property_set(result, (Item){.item = s2it(heap_create_name("value", 5))}, make_js_undefined());
            js_property_set(result, (Item){.item = s2it(heap_create_name("done", 4))}, (Item){.item = b2it(true)});
            return result;
        }
        // Use js_map_get_fast for internal properties to avoid js_property_get fallback chains
        bool found_np = false, found_mode = false, found_ct = false;
        Item node_item = js_map_get_fast(this_val.map, "__node_ptr__", 12, &found_np);
        Item mode_item = js_map_get_fast(this_val.map, "__iter_mode__", 13, &found_mode);
        Item ctype_item = js_map_get_fast(this_val.map, "__coll_type__", 13, &found_ct);
        JsCollectionOrderNode* node = (JsCollectionOrderNode*)(uintptr_t)it2i(node_item);
        int mode = (int)it2i(mode_item);
        int coll_type = (int)it2i(ctype_item);
        Item result = js_new_object();
        if (!node) {
            // iterator exhausted
            js_property_set(result, (Item){.item = s2it(heap_create_name("value", 5))}, make_js_undefined());
            js_property_set(result, (Item){.item = s2it(heap_create_name("done", 4))}, (Item){.item = b2it(true)});
            return result;
        }
        // get value based on mode
        Item val;
        if (mode == 2) {
            // entries: [key, value] for Map; [value, value] for Set
            Item pair = js_array_new(2);
            if (coll_type == JS_COLLECTION_SET) {
                js_array_set_int(pair, 0, node->key);
                js_array_set_int(pair, 1, node->key);
            } else {
                js_array_set_int(pair, 0, node->key);
                js_array_set_int(pair, 1, node->value);
            }
            val = pair;
        } else if (mode == 1) {
            val = node->key; // keys
        } else {
            // values: for Set, key IS the value; for Map, node->value
            val = (coll_type == JS_COLLECTION_SET) ? node->key : node->value;
        }
        js_property_set(result, (Item){.item = s2it(heap_create_name("value", 5))}, val);
        js_property_set(result, (Item){.item = s2it(heap_create_name("done", 4))}, (Item){.item = b2it(false)});
        // advance to next node
        js_property_set(this_val, (Item){.item = s2it(heap_create_name("__node_ptr__", 12))},
                        (Item){.item = i2it((int64_t)(uintptr_t)node->next)});
        return result;
    }

    // v76: Collection prototype methods (exposed on Map/Set.prototype)
    // Map methods — throw TypeError if this is not a Map with [[MapData]]
    case JS_BUILTIN_MAP_SET:
    case JS_BUILTIN_MAP_GET:
    case JS_BUILTIN_MAP_HAS:
    case JS_BUILTIN_MAP_DELETE:
    case JS_BUILTIN_MAP_CLEAR:
    case JS_BUILTIN_MAP_FOREACH: {
        JsCollectionData* cd = js_get_collection_data(this_val);
        if (!cd || cd->type != JS_COLLECTION_MAP || cd->is_weak) {
            Item tn = (Item){.item = s2it(heap_create_name("TypeError", 9))};
            Item msg = (Item){.item = s2it(heap_create_name("Method Map.prototype.* called on incompatible receiver"))};
            js_throw_value(js_new_error_with_name(tn, msg));
            return ItemNull;
        }
        int mid = 0;
        switch (builtin_id) {
            case JS_BUILTIN_MAP_SET: mid = 0; break;
            case JS_BUILTIN_MAP_GET: mid = 1; break;
            case JS_BUILTIN_MAP_HAS: mid = 2; break;
            case JS_BUILTIN_MAP_DELETE: mid = 3; break;
            case JS_BUILTIN_MAP_CLEAR: mid = 4; break;
            case JS_BUILTIN_MAP_FOREACH: mid = 5; break;
        }
        Item a1 = (mid == 4) ? ItemNull : arg0;
        Item a2 = (mid == 0 || mid == 5) ? arg1 : ItemNull;
        return js_collection_method(this_val, mid, a1, a2);
    }
    // Set methods — throw TypeError if this is not a Set with [[SetData]]
    case JS_BUILTIN_SET_ADD:
    case JS_BUILTIN_SET_HAS:
    case JS_BUILTIN_SET_DELETE:
    case JS_BUILTIN_SET_CLEAR:
    case JS_BUILTIN_SET_FOREACH: {
        JsCollectionData* cd = js_get_collection_data(this_val);
        if (!cd || cd->type != JS_COLLECTION_SET || cd->is_weak) {
            Item tn = (Item){.item = s2it(heap_create_name("TypeError", 9))};
            Item msg = (Item){.item = s2it(heap_create_name("Method Set.prototype.* called on incompatible receiver"))};
            js_throw_value(js_new_error_with_name(tn, msg));
            return ItemNull;
        }
        int mid = 0;
        switch (builtin_id) {
            case JS_BUILTIN_SET_ADD: mid = 0; break;
            case JS_BUILTIN_SET_HAS: mid = 2; break;
            case JS_BUILTIN_SET_DELETE: mid = 3; break;
            case JS_BUILTIN_SET_CLEAR: mid = 4; break;
            case JS_BUILTIN_SET_FOREACH: mid = 5; break;
        }
        Item a1 = (mid == 4) ? ItemNull : arg0;
        Item a2 = (mid == 5) ? arg1 : ItemNull;
        return js_collection_method(this_val, mid, a1, a2);
    }
    case JS_BUILTIN_COLL_SIZE_GETTER: {
        JsCollectionData* cd = js_get_collection_data(this_val);
        if (!cd) return undef;
        return (Item){.item = i2it((int64_t)hashmap_count(cd->hmap))};
    }
    case JS_BUILTIN_SET_INTERSECTION:
    case JS_BUILTIN_SET_UNION:
    case JS_BUILTIN_SET_DIFFERENCE:
    case JS_BUILTIN_SET_SYM_DIFF:
    case JS_BUILTIN_SET_IS_SUBSET:
    case JS_BUILTIN_SET_IS_SUPERSET:
    case JS_BUILTIN_SET_IS_DISJOINT: {
        // Delegate to js_map_method which has the Set operation logic
        const char* method_name_str = NULL;
        switch (builtin_id) {
            case JS_BUILTIN_SET_INTERSECTION: method_name_str = "intersection"; break;
            case JS_BUILTIN_SET_UNION: method_name_str = "union"; break;
            case JS_BUILTIN_SET_DIFFERENCE: method_name_str = "difference"; break;
            case JS_BUILTIN_SET_SYM_DIFF: method_name_str = "symmetricDifference"; break;
            case JS_BUILTIN_SET_IS_SUBSET: method_name_str = "isSubsetOf"; break;
            case JS_BUILTIN_SET_IS_SUPERSET: method_name_str = "isSupersetOf"; break;
            case JS_BUILTIN_SET_IS_DISJOINT: method_name_str = "isDisjointFrom"; break;
        }
        Item mn = (Item){.item = s2it(heap_create_name(method_name_str, strlen(method_name_str)))};
        return js_map_method(this_val, mn, &arg0, arg_count);
    }

    default:
        log_error("js_dispatch_builtin: unknown builtin_id=%d", builtin_id);
        return undef;
    }
}

extern "C" Item js_call_function(Item func_item, Item this_val, Item* args, int arg_count) {
    js_call_count++;

    if (get_type_id(func_item) != LMD_TYPE_FUNC) {
        // v18: throw TypeError for calling non-callable values
        log_debug("js_call_function[%d]: not a function (type=%d, item=0x%llx, argc=%d, this_type=%d)",
            js_call_count, get_type_id(func_item), (unsigned long long)func_item.item, arg_count,
            get_type_id(this_val));
        Item type_name = (Item){.item = s2it(heap_create_name("TypeError"))};
        Item msg = (Item){.item = s2it(heap_create_name("is not a function"))};
        Item error = js_new_error_with_name(type_name, msg);
        js_throw_value(error);
        return ItemNull;
    }

    JsFunction* fn = (JsFunction*)func_item.function;
    if (!fn || (!fn->func_ptr && fn->builtin_id == 0)) {
        log_error("js_call_function: null function pointer");
        return ItemNull;
    }

    // Built-in method dispatch (prototype methods like Array.prototype.push)
    if (fn->builtin_id > 0) {
        // Handle bound builtins
        if (fn->bound_args || fn->bound_this.item) {
            Item effective_this = fn->bound_this.item ? fn->bound_this : this_val;
            int total_argc = fn->bound_argc + arg_count;
            Item* merged_args = (Item*)alloca(total_argc * sizeof(Item));
            for (int i = 0; i < fn->bound_argc; i++) {
                merged_args[i] = fn->bound_args[i];
            }
            for (int i = 0; i < arg_count; i++) {
                merged_args[fn->bound_argc + i] = args ? args[i] : ItemNull;
            }
            return js_dispatch_builtin(fn->builtin_id, effective_this, merged_args, total_argc);
        }
        return js_dispatch_builtin(fn->builtin_id, this_val, args, arg_count);
    }

    // v11: handle bound functions — use bound this and prepend bound args
    if (fn->bound_args || fn->bound_this.item) {
        Item effective_this = fn->bound_this.item ? fn->bound_this : this_val;
        int total_argc = fn->bound_argc + arg_count;
        Item* merged_args = (Item*)alloca(total_argc * sizeof(Item));
        for (int i = 0; i < fn->bound_argc; i++) {
            merged_args[i] = fn->bound_args[i];
        }
        for (int i = 0; i < arg_count; i++) {
            merged_args[fn->bound_argc + i] = args ? args[i] : ItemNull;
        }
        Item prev_this = js_current_this;
        Item prev_nt = js_new_target;
        js_current_this = effective_this;
        // Check for pending new.target (set by 'new' expression before this call)
        if (js_has_pending_new_target) {
            js_new_target = js_pending_new_target;
            js_has_pending_new_target = false;
        } else {
            js_new_target = make_js_undefined(); // regular call: new.target is undefined
        }
        Item result = js_invoke_fn(fn, merged_args, total_argc);
        js_current_this = prev_this;
        js_new_target = prev_nt;
        return result;
    }

    // Bind 'this' for the duration of this call
    Item prev_this = js_current_this;
    Item prev_nt = js_new_target;
    js_current_this = this_val;
    // Check for pending new.target (set by 'new' expression before this call)
    if (js_has_pending_new_target) {
        js_new_target = js_pending_new_target;
        js_has_pending_new_target = false;
    } else {
        js_new_target = make_js_undefined(); // regular call: new.target is undefined
    }
    Item result = js_invoke_fn(fn, args, arg_count);
    js_current_this = prev_this;
    js_new_target = prev_nt;
    return result;
}

// Function.prototype.apply(thisArg, argsArray)
extern "C" Item js_apply_function(Item func_item, Item this_val, Item args_array) {
    if (get_type_id(func_item) != LMD_TYPE_FUNC) {
        // Support objects with .apply method (e.g. Sizzle's push polyfill)
        if (get_type_id(func_item) == LMD_TYPE_MAP) {
            bool found = false;
            Item apply_fn = js_map_get_fast(func_item.map, "apply", 5, &found);
            if (found && get_type_id(apply_fn) == LMD_TYPE_FUNC) {
                Item args[2] = { this_val, args_array };
                return js_call_function(apply_fn, func_item, args, 2);
            }
        }
        log_error("js_apply_function: not a function (type=%d)", get_type_id(func_item));
        Item type_name = (Item){.item = s2it(heap_create_name("TypeError"))};
        Item msg = (Item){.item = s2it(heap_create_name("apply called on non-function"))};
        Item error = js_new_error_with_name(type_name, msg);
        js_throw_value(error);
        return ItemNull;
    }
    // Extract args from array
    int argc = 0;
    Item* args = NULL;
    if (get_type_id(args_array) == LMD_TYPE_ARRAY) {
        argc = (int)args_array.array->length;
        if (argc > 0) {
            args = (Item*)alloca(argc * sizeof(Item));
            for (int i = 0; i < argc; i++) {
                Item idx = {.item = i2it(i)};
                args[i] = js_array_get(args_array, idx);
            }
        }
    } else if (get_type_id(args_array) == LMD_TYPE_MAP) {
        // v20: Array-like objects with .length property
        Item len_key = (Item){.item = s2it(heap_create_name("length", 6))};
        Item len_val = js_property_get(args_array, len_key);
        TypeId lt = get_type_id(len_val);
        if (lt == LMD_TYPE_INT || lt == LMD_TYPE_FLOAT) {
            argc = (lt == LMD_TYPE_INT) ? (int)it2i(len_val) : (int)it2d(len_val);
            if (argc > 0) {
                args = (Item*)alloca(argc * sizeof(Item));
                for (int i = 0; i < argc; i++) {
                    char idx_buf[16];
                    snprintf(idx_buf, sizeof(idx_buf), "%d", i);
                    Item idx_key = (Item){.item = s2it(heap_create_name(idx_buf, strlen(idx_buf)))};
                    args[i] = js_property_get(args_array, idx_key);
                }
            }
        }
    }
    return js_call_function(func_item, this_val, args, argc);
}

// v11: Function.prototype.bind(thisArg, ...args)
extern "C" Item js_bind_function(Item func_item, Item bound_this, Item* bound_args, int bound_argc) {
    if (get_type_id(func_item) != LMD_TYPE_FUNC) {
        log_error("js_bind_function: not a function (type=%d)", get_type_id(func_item));
        return ItemNull;
    }
    JsFunction* orig = (JsFunction*)func_item.function;
    JsFunction* bound = (JsFunction*)pool_calloc(js_input->pool, sizeof(JsFunction));
    bound->type_id = LMD_TYPE_FUNC;
    bound->func_ptr = orig->func_ptr;
    bound->param_count = orig->param_count;
    bound->formal_length = orig->formal_length; // preserve formal_length from original
    bound->env = orig->env;
    bound->env_size = orig->env_size;
    bound->prototype = ItemNull;
    bound->builtin_id = orig->builtin_id;
    bound->bound_this = bound_this;
    // Set name to "bound <original_name>" per ES spec
    if (orig->name && orig->name->len > 0) {
        int new_len = 6 + orig->name->len; // "bound " + name
        char* buf = (char*)pool_calloc(js_input->pool, new_len + 1);
        memcpy(buf, "bound ", 6);
        memcpy(buf + 6, orig->name->chars, orig->name->len);
        bound->name = heap_create_name(buf, new_len);
    } else {
        bound->name = heap_create_name("bound ", 6);
    }
    if (bound_argc > 0 && bound_args) {
        bound->bound_args = (Item*)pool_calloc(js_input->pool, bound_argc * sizeof(Item));
        for (int i = 0; i < bound_argc; i++) {
            bound->bound_args[i] = bound_args[i];
        }
        bound->bound_argc = bound_argc;
    }
    return (Item){.function = (Function*)bound};
}

// js_func_bind: safe version that checks type first.
// If func_item is a function → js_bind_function.
// If func_item is a map/object → call its own .bind() method with the args.
extern "C" Item js_func_bind(Item func_item, Item bound_this, Item* bound_args, int bound_argc) {
    if (get_type_id(func_item) == LMD_TYPE_FUNC) {
        return js_bind_function(func_item, bound_this, bound_args, bound_argc);
    }
    // ES spec: If IsCallable(Target) is false, throw a TypeError.
    // A non-function object (even if it has Function.prototype) is not callable.
    Item tn = (Item){.item = s2it(heap_create_name("TypeError", 9))};
    Item msg = (Item){.item = s2it(heap_create_name("Bind must be called on a function"))};
    js_throw_value(js_new_error_with_name(tn, msg));
    return make_js_undefined();
}

// =============================================================================
// v11: Regex support — /pattern/flags as Map objects with compiled RE2
// =============================================================================

// hidden property key for storing regex data pointer
static const char* JS_REGEX_DATA_KEY = "__rd";

struct JsRegexData {
    re2::RE2* re2;            // compiled regex
    bool global;              // 'g' flag
    bool ignore_case;         // 'i' flag
    bool multiline;           // 'm' flag
    bool sticky;              // 'y' flag (v46)
};

extern "C" Item js_create_regex(const char* pattern, int pattern_len, const char* flags, int flags_len) {
    // Check for 'v' flag (Unicode Sets mode) — needs preprocessing for set operations
    bool has_v_flag = false;
    for (int i = 0; i < flags_len; i++) {
        if (flags[i] == 'v') has_v_flag = true;
    }

    // If v flag is present, preprocess set subtraction [A--B] and intersection [A&&B]
    // by transforming them into RE2-compatible character classes.
    std::string v_processed;
    const char* effective_pattern = pattern;
    int effective_pattern_len = pattern_len;
    if (has_v_flag) {
        v_processed.reserve(pattern_len + 128);
        int i = 0;
        while (i < pattern_len) {
            // handle escape sequences
            if (pattern[i] == '\\' && i + 1 < pattern_len) {
                v_processed += pattern[i];
                v_processed += pattern[i + 1];
                i += 2;
                continue;
            }
            // detect [ to look for set operations
            if (pattern[i] == '[') {
                // scan for set subtraction [X--[Y]] or intersection [X&&[Y]]
                // find the matching ] accounting for nesting and escapes
                int start = i;
                bool has_set_op = false;
                int op_pos = -1;
                char op_type = 0; // '-' for subtraction, '&' for intersection
                int depth = 0;
                for (int j = i; j < pattern_len; j++) {
                    if (pattern[j] == '\\' && j + 1 < pattern_len) {
                        j++; // skip escaped char
                        continue;
                    }
                    if (pattern[j] == '[') depth++;
                    else if (pattern[j] == ']') {
                        depth--;
                        if (depth == 0) break;
                    }
                    // check for -- or && at depth==1 (outermost class)
                    if (depth == 1 && j + 1 < pattern_len) {
                        if (pattern[j] == '-' && pattern[j+1] == '-') {
                            has_set_op = true;
                            op_pos = j;
                            op_type = '-';
                        } else if (pattern[j] == '&' && pattern[j+1] == '&') {
                            has_set_op = true;
                            op_pos = j;
                            op_type = '&';
                        }
                    }
                }

                if (has_set_op && op_type == '-') {
                    // Set subtraction: [A--[B]] → transform
                    // Extract A part (from [ to --)
                    int a_start = start + 1;
                    int a_end = op_pos;
                    // Extract B part (from --[ to matching ])
                    int b_bracket_start = op_pos + 2; // skip --
                    // find the end of [B] part
                    if (b_bracket_start < pattern_len && pattern[b_bracket_start] == '[') {
                        int b_start = b_bracket_start + 1;
                        int b_depth = 1;
                        int b_end = b_start;
                        for (int j = b_start; j < pattern_len && b_depth > 0; j++) {
                            if (pattern[j] == '\\' && j + 1 < pattern_len) { j++; continue; }
                            if (pattern[j] == '[') b_depth++;
                            else if (pattern[j] == ']') { b_depth--; if (b_depth == 0) { b_end = j; break; } }
                        }
                        // find the outer ]
                        int outer_end = b_end + 1;
                        if (outer_end < pattern_len && pattern[outer_end] == ']') outer_end++;
                        else { outer_end = b_end + 1; }

                        // Check what A is:
                        std::string a_content(pattern + a_start, a_end - a_start);
                        std::string b_content(pattern + b_start, b_end - b_start);

                        // if A contains \S → build [^\s B] (negate whitespace union B)
                        if (a_content.find("\\S") != std::string::npos) {
                            // \S--[B] → [^(whitespace_chars)(B_chars)]
                            v_processed += "[^\\p{Z}\\t\\n\\r\\f\\x0b\\x{FEFF}";
                            v_processed += b_content;
                            v_processed += "]";
                        }
                        // if A contains \w → build [^\W B]
                        else if (a_content.find("\\w") != std::string::npos) {
                            v_processed += "[^\\W";
                            v_processed += b_content;
                            v_processed += "]";
                        }
                        // generic: A minus B → (?:(?![B])A) using lookahead
                        else {
                            // use negative lookahead: (?:(?![B])[A])
                            v_processed += "(?:(?![";
                            v_processed += b_content;
                            v_processed += "])[";
                            v_processed += a_content;
                            v_processed += "])";
                        }
                        i = outer_end;
                        continue;
                    }
                }
                else if (has_set_op && op_type == '&') {
                    // Set intersection: [A&&[B]] → (?:(?=[B])[A])
                    int a_start = start + 1;
                    int a_end = op_pos;
                    int b_bracket_start = op_pos + 2;
                    if (b_bracket_start < pattern_len && pattern[b_bracket_start] == '[') {
                        int b_start = b_bracket_start + 1;
                        int b_depth = 1;
                        int b_end = b_start;
                        for (int j = b_start; j < pattern_len && b_depth > 0; j++) {
                            if (pattern[j] == '\\' && j + 1 < pattern_len) { j++; continue; }
                            if (pattern[j] == '[') b_depth++;
                            else if (pattern[j] == ']') { b_depth--; if (b_depth == 0) { b_end = j; break; } }
                        }
                        int outer_end = b_end + 1;
                        if (outer_end < pattern_len && pattern[outer_end] == ']') outer_end++;
                        else { outer_end = b_end + 1; }

                        std::string a_content(pattern + a_start, a_end - a_start);
                        std::string b_content(pattern + b_start, b_end - b_start);

                        // intersection: char must match BOTH A and B → (?:(?=[B])[A])
                        v_processed += "(?:(?=[";
                        v_processed += b_content;
                        v_processed += "])[";
                        v_processed += a_content;
                        v_processed += "])";
                        i = outer_end;
                        continue;
                    }
                }
            }
            v_processed += pattern[i];
            i++;
        }
        effective_pattern = v_processed.c_str();
        effective_pattern_len = (int)v_processed.size();
    }

    // Preprocess pattern: expand JS \s / \S to the full Unicode whitespace class,
    // since RE2's \s only matches ASCII whitespace but JS \s is Unicode-aware.
    // JS \s = [ \t\n\r\f\v\u00A0\u1680\u2000-\u200A\u2028\u2029\u202F\u205F\u3000\uFEFF]
    // RE2 equivalent: [\p{Z}\t\n\r\f\x0b\x{FEFF}]  (\p{Z} covers Zs+Zl+Zp categories)
    // We also need to expand \S (non-whitespace).
    static const char* S_EXPAND = "[\\p{Z}\\t\\n\\r\\f\\x0b\\x{FEFF}]";
    static const char* S_EXPAND_INNER = "\\p{Z}\\t\\n\\r\\f\\x0b\\x{FEFF}";  // inside existing []
    static const char* NOT_S_EXPAND = "[^\\p{Z}\\t\\n\\r\\f\\x0b\\x{FEFF}]";
    std::string processed_pattern;
    processed_pattern.reserve(effective_pattern_len + 64);
    int bracket_depth = 0;
    for (int i = 0; i < effective_pattern_len; i++) {
        if (effective_pattern[i] == '\\' && i + 1 < effective_pattern_len) {
            char next = effective_pattern[i + 1];
            if (next == 's') {
                if (bracket_depth > 0) {
                    processed_pattern += S_EXPAND_INNER; // inline within existing []
                } else {
                    processed_pattern += S_EXPAND;
                }
                i++;
                continue;
            }
            if (next == 'S') {
                if (bracket_depth > 0) {
                    // can't negate inside existing class; keep as \S (best effort)
                    processed_pattern += "\\S";
                } else {
                    processed_pattern += NOT_S_EXPAND;
                }
                i++;
                continue;
            }
            // consume the 2-char escape sequence as-is
            // but convert JS \uXXXX to RE2 \x{XXXX}
            if (next == 'u') {
                // \u{XXXXX} form
                if (i + 2 < effective_pattern_len && effective_pattern[i + 2] == '{') {
                    int j = i + 3;
                    while (j < effective_pattern_len && effective_pattern[j] != '}') j++;
                    if (j < effective_pattern_len) {
                        processed_pattern += "\\x{";
                        processed_pattern.append(effective_pattern + i + 3, j - (i + 3));
                        processed_pattern += '}';
                        i = j;
                        continue;
                    }
                }
                // \uXXXX form (exactly 4 hex digits)
                if (i + 5 < effective_pattern_len) {
                    bool all_hex = true;
                    for (int j = i + 2; j < i + 6; j++) {
                        char c = effective_pattern[j];
                        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F'))) {
                            all_hex = false; break;
                        }
                    }
                    if (all_hex) {
                        processed_pattern += "\\x{";
                        processed_pattern.append(effective_pattern + i + 2, 4);
                        processed_pattern += '}';
                        i += 5; // skip \uXXXX (6 chars, loop will i++)
                        continue;
                    }
                }
            }
            processed_pattern += effective_pattern[i];
            processed_pattern += effective_pattern[i + 1];
            i++;
            continue;
        }
        if (effective_pattern[i] == '[') {
            bracket_depth++;
        } else if (effective_pattern[i] == ']') {
            if (bracket_depth > 0) bracket_depth--;
        }
        processed_pattern += effective_pattern[i];
    }

    // Post-process: fix patterns RE2 cannot handle
    // 1. Replace empty character class [] with a never-matching pattern
    {
        size_t pos = 0;
        while ((pos = processed_pattern.find("[]", pos)) != std::string::npos) {
            // check it's not preceded by backslash (escaped bracket)
            if (pos > 0 && processed_pattern[pos - 1] == '\\') { pos++; continue; }
            // replace [] with (?!.)  — wait, RE2 doesn't support lookahead
            // replace [] with \x{FFFE} (BOM internal — never in real text)
            processed_pattern.replace(pos, 2, "\\x{FFFE}");
            pos += 9;
        }
    }
    // 2. Strip lookahead assertions (?=...) and (?!...) since RE2 doesn't support them
    //    (?=X) → remove entirely (zero-width assertion; dropping it is approximate but safe)
    //    (?!X) → remove entirely (drops the negative constraint)
    {
        size_t pos = 0;
        while ((pos = processed_pattern.find("(?=", pos)) != std::string::npos) {
            if (pos > 0 && processed_pattern[pos - 1] == '\\') { pos++; continue; }
            // find matching closing paren and remove entire (?=...) group
            int depth = 1;
            size_t end = pos + 3;
            while (end < processed_pattern.size() && depth > 0) {
                if (processed_pattern[end] == '\\' && end + 1 < processed_pattern.size()) {
                    end += 2; continue;
                }
                if (processed_pattern[end] == '(') depth++;
                else if (processed_pattern[end] == ')') depth--;
                end++;
            }
            processed_pattern.erase(pos, end - pos);
        }
        pos = 0;
        while ((pos = processed_pattern.find("(?!", pos)) != std::string::npos) {
            if (pos > 0 && processed_pattern[pos - 1] == '\\') { pos++; continue; }
            // find matching closing paren
            int depth = 1;
            size_t end = pos + 3;
            while (end < processed_pattern.size() && depth > 0) {
                if (processed_pattern[end] == '\\' && end + 1 < processed_pattern.size()) {
                    end += 2; continue;
                }
                if (processed_pattern[end] == '(') depth++;
                else if (processed_pattern[end] == ')') depth--;
                end++;
            }
            // remove the entire (?!...) group
            processed_pattern.erase(pos, end - pos);
        }
    }
    // 3. Map unsupported Unicode property names to RE2-compatible equivalents
    {
        // \p{Ideographic} → \p{Han} (covers CJK Unified Ideographs)
        size_t pos = 0;
        while ((pos = processed_pattern.find("\\p{Ideographic}", pos)) != std::string::npos) {
            processed_pattern.replace(pos, 15, "\\p{Han}");
            pos += 7;
        }
        // \P{Ideographic} → \P{Han}
        pos = 0;
        while ((pos = processed_pattern.find("\\P{Ideographic}", pos)) != std::string::npos) {
            processed_pattern.replace(pos, 15, "\\P{Han}");
            pos += 7;
        }
    }

    // build RE2 options from flags
    re2::RE2::Options opts;
    opts.set_log_errors(false);
    opts.set_one_line(true);  // v18: JS default is non-multiline (^ and $ match string boundaries only)
    bool global = false;
    bool multiline = false;
    bool sticky = false;
    for (int i = 0; i < flags_len; i++) {
        if (flags[i] == 'i') opts.set_case_sensitive(false);
        else if (flags[i] == 'm') { opts.set_one_line(false); multiline = true; }
        else if (flags[i] == 'g') global = true;
        else if (flags[i] == 's') opts.set_dot_nl(true);
        else if (flags[i] == 'y') sticky = true;
    }
    // RE2: set_one_line(false) doesn't reliably enable multiline ^ and $,
    // so prepend (?m) inline flag when multiline mode is requested
    if (multiline) {
        processed_pattern = "(?m)" + processed_pattern;
    }
    // compile RE2 pattern (use preprocessed version)
    re2::RE2* re2 = new re2::RE2(processed_pattern, opts);
    if (!re2->ok()) {
        // fall back to original pattern if preprocessing caused errors
        delete re2;
        re2 = new re2::RE2(re2::StringPiece(pattern, pattern_len), opts);
        if (!re2->ok()) {
            log_error("js regex compile error: /%.*s/%.*s: %s",
                pattern_len, pattern, flags_len, flags, re2->error().c_str());
            delete re2;
            return ItemNull;
        }
    }
    // store regex data in a pool-allocated struct
    JsRegexData* rd = (JsRegexData*)pool_calloc(js_input->pool, sizeof(JsRegexData));
    rd->re2 = re2;
    rd->global = global;
    rd->ignore_case = !opts.case_sensitive();
    rd->multiline = multiline;
    rd->sticky = sticky;
    // create a Map object and set properties
    Item regex_obj = js_new_object();
    // store regex data pointer as int in hidden property
    Item rd_key = (Item){.item = s2it(heap_create_name(JS_REGEX_DATA_KEY))};
    Item rd_val = (Item){.item = i2it((int64_t)(uintptr_t)rd)};
    js_property_set(regex_obj, rd_key, rd_val);
    // create null-terminated copies for heap_create_name
    char* src_buf = (char*)pool_calloc(js_input->pool, pattern_len + 1);
    memcpy(src_buf, pattern, pattern_len);
    src_buf[pattern_len] = '\0';
    char* flg_buf = (char*)pool_calloc(js_input->pool, flags_len + 1);
    memcpy(flg_buf, flags, flags_len);
    flg_buf[flags_len] = '\0';
    // set visible properties
    Item source_key = (Item){.item = s2it(heap_create_name("source"))};
    Item source_val = (Item){.item = s2it(heap_create_name(src_buf))};
    js_property_set(regex_obj, source_key, source_val);
    Item flags_key = (Item){.item = s2it(heap_create_name("flags"))};
    Item flags_val = (Item){.item = s2it(heap_create_name(flg_buf))};
    js_property_set(regex_obj, flags_key, flags_val);
    Item global_key = (Item){.item = s2it(heap_create_name("global"))};
    Item global_val = (Item){.item = b2it(global ? BOOL_TRUE : BOOL_FALSE)};
    js_property_set(regex_obj, global_key, global_val);
    // v18: expose all standard RegExp flag properties
    bool ignore_case = !opts.case_sensitive();
    bool dot_all = opts.dot_nl();
    bool has_sticky = false, has_unicode = false;
    for (int i = 0; i < flags_len; i++) {
        if (flags[i] == 'y') has_sticky = true;
        if (flags[i] == 'u' || flags[i] == 'v') has_unicode = true;
    }
    Item ic_key = (Item){.item = s2it(heap_create_name("ignoreCase"))};
    js_property_set(regex_obj, ic_key, (Item){.item = b2it(ignore_case ? BOOL_TRUE : BOOL_FALSE)});
    Item ml_key = (Item){.item = s2it(heap_create_name("multiline"))};
    js_property_set(regex_obj, ml_key, (Item){.item = b2it(multiline ? BOOL_TRUE : BOOL_FALSE)});
    Item da_key = (Item){.item = s2it(heap_create_name("dotAll"))};
    js_property_set(regex_obj, da_key, (Item){.item = b2it(dot_all ? BOOL_TRUE : BOOL_FALSE)});
    Item uni_key = (Item){.item = s2it(heap_create_name("unicode"))};
    js_property_set(regex_obj, uni_key, (Item){.item = b2it(has_unicode ? BOOL_TRUE : BOOL_FALSE)});
    Item sticky_key = (Item){.item = s2it(heap_create_name("sticky"))};
    js_property_set(regex_obj, sticky_key, (Item){.item = b2it(has_sticky ? BOOL_TRUE : BOOL_FALSE)});
    Item li_key = (Item){.item = s2it(heap_create_name("lastIndex"))};
    js_property_set(regex_obj, li_key, (Item){.item = i2it(0)});
    // v46: set __class_name__ for RegExp prototype method resolution
    Item cn_key = (Item){.item = s2it(heap_create_name("__class_name__", 14))};
    Item cn_val = (Item){.item = s2it(heap_create_name("RegExp", 6))};
    js_property_set(regex_obj, cn_key, cn_val);
    return regex_obj;
}

// new RegExp(pattern, flags) — construct regex from string arguments at runtime
extern "C" Item js_regexp_construct(Item pattern_item, Item flags_item) {
    const char* pattern = "";
    int pattern_len = 0;
    const char* flags = "";
    int flags_len = 0;
    if (get_type_id(pattern_item) == LMD_TYPE_STRING) {
        String* ps = it2s(pattern_item);
        if (ps) { pattern = ps->chars; pattern_len = (int)ps->len; }
    }
    if (get_type_id(flags_item) == LMD_TYPE_STRING) {
        String* fs = it2s(flags_item);
        if (fs) { flags = fs->chars; flags_len = (int)fs->len; }
    }
    return js_create_regex(pattern, pattern_len, flags, flags_len);
}

static JsRegexData* js_get_regex_data(Item obj) {
    if (get_type_id(obj) != LMD_TYPE_MAP) return NULL;
    Item rd_key = (Item){.item = s2it(heap_create_name(JS_REGEX_DATA_KEY))};
    Item rd_item = js_property_get(obj, rd_key);
    TypeId tid = get_type_id(rd_item);
    if (tid != LMD_TYPE_INT && tid != LMD_TYPE_INT64) return NULL;
    int64_t ptr_val = it2i(rd_item);
    if (ptr_val == 0) return NULL;
    return (JsRegexData*)(uintptr_t)ptr_val;
}

extern "C" Item js_regex_test(Item regex, Item str) {
    JsRegexData* rd = js_get_regex_data(regex);
    if (!rd) return (Item){.item = b2it(BOOL_FALSE)};
    // v46: convert argument to string
    TypeId tid = get_type_id(str);
    if (tid != LMD_TYPE_STRING) {
        str = js_to_string(str);
        tid = get_type_id(str);
        if (tid != LMD_TYPE_STRING) return (Item){.item = b2it(BOOL_FALSE)};
    }
    const char* chars = str.get_chars();
    int len = str.get_len();
    bool matched = re2::RE2::PartialMatch(re2::StringPiece(chars, len), *rd->re2);
    return (Item){.item = b2it(matched ? BOOL_TRUE : BOOL_FALSE)};
}

extern "C" Item js_regex_exec(Item regex, Item str) {
    JsRegexData* rd = js_get_regex_data(regex);
    if (!rd) return ItemNull;
    // v46: convert argument to string (handles new String(), numbers, etc.)
    TypeId tid = get_type_id(str);
    if (tid != LMD_TYPE_STRING) {
        str = js_to_string(str);
        tid = get_type_id(str);
        if (tid != LMD_TYPE_STRING) return ItemNull;
    }
    const char* chars = str.get_chars();
    int len = str.get_len();

    // for global/sticky regexes, read lastIndex to start search from there
    int start_pos = 0;
    Item li_key = (Item){.item = s2it(heap_create_name("lastIndex", 9))};
    bool uses_last_index = rd->global || rd->sticky;
    if (uses_last_index) {
        Item li_val = js_property_get(regex, li_key);
        TypeId li_tid = get_type_id(li_val);
        if (li_tid == LMD_TYPE_INT || li_tid == LMD_TYPE_INT64) {
            start_pos = (int)it2i(li_val);
        } else if (li_tid == LMD_TYPE_FLOAT) {
            start_pos = (int)li_val.get_double();
        }
        if (start_pos < 0 || start_pos > len) {
            js_property_set(regex, li_key, (Item){.item = i2it(0)});
            return ItemNull;
        }
    }

    // perform match with captures
    int num_groups = rd->re2->NumberOfCapturingGroups() + 1; // +1 for full match
    if (num_groups > 16) num_groups = 16;
    re2::StringPiece matches[16];
    // sticky: must match at exactly start_pos (ANCHOR_START from start_pos)
    re2::RE2::Anchor anchor = rd->sticky ? re2::RE2::ANCHOR_START : re2::RE2::UNANCHORED;
    bool matched = rd->re2->Match(re2::StringPiece(chars, len), start_pos, len,
        anchor, matches, num_groups);
    if (!matched) {
        if (uses_last_index) {
            js_property_set(regex, li_key, (Item){.item = i2it(0)});
        }
        return ItemNull;
    }

    // update lastIndex for global/sticky regexes
    if (uses_last_index) {
        int match_end = (int)(matches[0].data() - chars) + (int)matches[0].size();
        // advance at least 1 to avoid infinite loop on zero-length matches
        if (match_end == start_pos) match_end++;
        js_property_set(regex, li_key, (Item){.item = i2it(match_end)});
    }

    // build result as an Array with .index, .input, .groups properties (per ES spec)
    Item result = js_array_new(num_groups);
    for (int i = 0; i < num_groups; i++) {
        if (matches[i].data()) {
            int mlen = (int)matches[i].size();
            Item s = (Item){.item = s2it(heap_strcpy((char*)matches[i].data(), mlen))};
            js_array_set_int(result, i, s);
        } else {
            // unmatched group → undefined
            js_array_set_int(result, i, make_js_undefined());
        }
    }
    // Set named properties (index, input, groups) via companion map
    int match_index = (int)(matches[0].data() - chars);
    Item index_key = (Item){.item = s2it(heap_create_name("index", 5))};
    js_property_set(result, index_key, (Item){.item = i2it(match_index)});
    // v46: add input property (the original string passed to exec)
    Item input_key = (Item){.item = s2it(heap_create_name("input", 5))};
    js_property_set(result, input_key, str);
    // groups property (undefined for non-named-group regexes)
    Item groups_key = (Item){.item = s2it(heap_create_name("groups", 6))};
    js_property_set(result, groups_key, make_js_undefined());
    return result;
}

// =============================================================================
// v83: RegExp Symbol methods (@@match, @@replace, @@search, @@split)
// =============================================================================

// RegExp.prototype[@@match](string)
static Item js_regexp_symbol_match(Item this_val, Item arg0) {
    // ES spec: Type(this) must be Object
    if (get_type_id(this_val) != LMD_TYPE_MAP && get_type_id(this_val) != LMD_TYPE_ARRAY) {
        Item tn = (Item){.item = s2it(heap_create_name("TypeError", 9))};
        Item msg = (Item){.item = s2it(heap_create_name("RegExp.prototype[@@match] called on incompatible receiver"))};
        js_throw_value(js_new_error_with_name(tn, msg));
        return ItemNull;
    }
    JsRegexData* rd = js_get_regex_data(this_val);
    if (!rd) return ItemNull;
    Item str = (get_type_id(arg0) == LMD_TYPE_STRING) ? arg0 : js_to_string(arg0);
    if (!rd->global) {
        // non-global: delegate to RegExp.prototype.exec
        return js_regex_exec(this_val, str);
    }
    // global: collect all matches
    Item li_key = (Item){.item = s2it(heap_create_name("lastIndex", 9))};
    js_property_set(this_val, li_key, (Item){.item = i2it(0)});
    Item results = js_array_new(0);
    int match_count = 0;
    for (int safety = 0; safety < 1000000; safety++) {
        Item match = js_regex_exec(this_val, str);
        if (match.item == ItemNull.item) break;
        // get first element (the full match string)
        Item match_str = js_array_get_int(match, 0);
        js_array_push(results, match_str);
        match_count++;
        // if zero-length match, advance lastIndex to avoid infinite loop
        if (get_type_id(match_str) == LMD_TYPE_STRING && it2s(match_str)->len == 0) {
            Item li = js_property_get(this_val, li_key);
            int64_t idx = (get_type_id(li) == LMD_TYPE_INT) ? it2i(li) : 0;
            js_property_set(this_val, li_key, (Item){.item = i2it(idx + 1)});
        }
    }
    return match_count == 0 ? ItemNull : results;
}

// RegExp.prototype[@@replace](string, replacement)
static Item js_regexp_symbol_replace(Item this_val, Item str, Item replacement) {
    // ES spec: Type(this) must be Object
    if (get_type_id(this_val) != LMD_TYPE_MAP && get_type_id(this_val) != LMD_TYPE_ARRAY) {
        Item tn = (Item){.item = s2it(heap_create_name("TypeError", 9))};
        Item msg = (Item){.item = s2it(heap_create_name("RegExp.prototype[@@replace] called on incompatible receiver"))};
        js_throw_value(js_new_error_with_name(tn, msg));
        return ItemNull;
    }
    Item args[2] = {this_val, replacement};
    if (get_type_id(str) != LMD_TYPE_STRING) str = js_to_string(str);
    return js_string_replace_impl(str, args, 2, false);
}

// RegExp.prototype[@@search](string)
static Item js_regexp_symbol_search(Item this_val, Item arg0) {
    // ES spec: Type(this) must be Object
    if (get_type_id(this_val) != LMD_TYPE_MAP && get_type_id(this_val) != LMD_TYPE_ARRAY) {
        Item tn = (Item){.item = s2it(heap_create_name("TypeError", 9))};
        Item msg = (Item){.item = s2it(heap_create_name("RegExp.prototype[@@search] called on incompatible receiver"))};
        js_throw_value(js_new_error_with_name(tn, msg));
        return (Item){.item = i2it(-1)};
    }
    JsRegexData* rd = js_get_regex_data(this_val);
    if (!rd) return (Item){.item = i2it(-1)};
    Item str = (get_type_id(arg0) == LMD_TYPE_STRING) ? arg0 : js_to_string(arg0);
    const char* chars = str.get_chars();
    int len = str.get_len();
    re2::StringPiece match;
    if (rd->re2->Match(re2::StringPiece(chars, len), 0, len, re2::RE2::UNANCHORED, &match, 1)) {
        return (Item){.item = i2it((int)(match.data() - chars))};
    }
    return (Item){.item = i2it(-1)};
}

// RegExp.prototype[@@split](string, limit)
static Item js_regexp_symbol_split(Item this_val, Item str, Item limit) {
    // ES spec: Type(this) must be Object
    if (get_type_id(this_val) != LMD_TYPE_MAP && get_type_id(this_val) != LMD_TYPE_ARRAY) {
        Item tn = (Item){.item = s2it(heap_create_name("TypeError", 9))};
        Item msg = (Item){.item = s2it(heap_create_name("RegExp.prototype[@@split] called on incompatible receiver"))};
        js_throw_value(js_new_error_with_name(tn, msg));
        return js_array_new(0);
    }
    JsRegexData* rd = js_get_regex_data(this_val);
    if (!rd) return js_array_new(0);
    if (get_type_id(str) != LMD_TYPE_STRING) str = js_to_string(str);
    const char* chars = str.get_chars();
    int len = str.get_len();

    // handle limit
    int max_parts = 0x7FFFFFFF;
    if (get_type_id(limit) == LMD_TYPE_INT || get_type_id(limit) == LMD_TYPE_INT64) {
        int64_t lv = it2i(limit);
        if (lv < 0) lv = 0;
        max_parts = (int)lv;
    } else if (get_type_id(limit) == LMD_TYPE_FLOAT) {
        double d = limit.get_double();
        if (d >= 0 && d < 0x7FFFFFFF) max_parts = (int)d;
    }
    if (max_parts == 0) return js_array_new(0);

    Item result = js_array_new(0);
    int num_groups = rd->re2->NumberOfCapturingGroups() + 1;
    if (num_groups > 16) num_groups = 16;
    re2::StringPiece matches[16];
    int pos = 0;
    int part_count = 0;

    while (pos <= len && part_count < max_parts) {
        bool found = rd->re2->Match(re2::StringPiece(chars, len), pos, len, re2::RE2::UNANCHORED, matches, num_groups);
        if (!found || (int)(matches[0].data() - chars) >= len) break;

        int match_start = (int)(matches[0].data() - chars);
        int match_end = match_start + (int)matches[0].size();

        // avoid infinite loop on zero-length match at same position
        if (match_end == pos && match_start == pos) {
            // push one char and advance
            if (pos < len) {
                Item seg = (Item){.item = s2it(heap_strcpy((char*)(chars + pos), 1))};
                js_array_push(result, seg);
                part_count++;
            }
            pos++;
            continue;
        }

        // push segment before match
        int seg_len = match_start - pos;
        Item seg = (seg_len > 0) ? (Item){.item = s2it(heap_strcpy((char*)(chars + pos), seg_len))}
                                 : (Item){.item = s2it(heap_create_name("", 0))};
        js_array_push(result, seg);
        part_count++;
        if (part_count >= max_parts) break;

        // push capturing groups
        for (int g = 1; g < num_groups && part_count < max_parts; g++) {
            if (matches[g].data()) {
                Item gs = (Item){.item = s2it(heap_strcpy((char*)matches[g].data(), (int)matches[g].size()))};
                js_array_push(result, gs);
            } else {
                js_array_push(result, make_js_undefined());
            }
            part_count++;
        }

        pos = match_end;
    }

    // push trailing segment
    if (part_count < max_parts) {
        int seg_len = len - pos;
        Item seg = (seg_len > 0) ? (Item){.item = s2it(heap_strcpy((char*)(chars + pos), seg_len))}
                                 : (Item){.item = s2it(heap_create_name("", 0))};
        js_array_push(result, seg);
    }

    return result;
}

// =============================================================================
// v11: Map/Set collections — backed by HashMap from lib/hashmap.h
// =============================================================================

static const char* JS_COLLECTION_DATA_KEY = "__cd";

struct JsCollectionEntry {
    Item key;
    Item value;
};

// Insertion-order node and data structs moved to forward declaration section above

static uint64_t js_collection_hash(const void *item, uint64_t seed0, uint64_t seed1) {
    const JsCollectionEntry* e = (const JsCollectionEntry*)item;
    Item k = e->key;
    TypeId tid = get_type_id(k);
    if (tid == LMD_TYPE_STRING) {
        String* s = it2s(k);
        return hashmap_sip(s->chars, s->len, seed0, seed1);
    }
    // Numeric types: hash as double for SameValueZero consistency
    if (tid == LMD_TYPE_INT || tid == LMD_TYPE_INT64 || tid == LMD_TYPE_FLOAT) {
        double d;
        if (tid == LMD_TYPE_FLOAT) d = k.get_double();
        else d = (double)it2i(k);
        // SameValueZero: -0 and +0 are the same → normalize to +0
        if (d == 0.0) d = 0.0;
        // SameValueZero: all NaN values are equal → use canonical NaN bits
        if (d != d) { uint64_t nan_marker = 0x7FF8000000000001ULL; return hashmap_sip(&nan_marker, sizeof(nan_marker), seed0, seed1); }
        return hashmap_sip(&d, sizeof(d), seed0, seed1);
    }
    // fallback: hash the raw item bits
    uint64_t bits = k.item;
    return hashmap_sip(&bits, sizeof(bits), seed0, seed1);
}

static int js_collection_compare(const void *a, const void *b, void *udata) {
    const JsCollectionEntry* ea = (const JsCollectionEntry*)a;
    const JsCollectionEntry* eb = (const JsCollectionEntry*)b;
    Item ka = ea->key, kb = eb->key;
    TypeId ta = get_type_id(ka), tb = get_type_id(kb);
    // SameValueZero: handle numeric type coercion (-0 == +0, NaN == NaN)
    // Normalize numeric types for comparison
    bool a_num = (ta == LMD_TYPE_INT || ta == LMD_TYPE_INT64 || ta == LMD_TYPE_FLOAT);
    bool b_num = (tb == LMD_TYPE_INT || tb == LMD_TYPE_INT64 || tb == LMD_TYPE_FLOAT);
    if (a_num && b_num) {
        double da, db;
        if (ta == LMD_TYPE_FLOAT) da = ka.get_double();
        else da = (double)it2i(ka);
        if (tb == LMD_TYPE_FLOAT) db = kb.get_double();
        else db = (double)it2i(kb);
        // SameValueZero: NaN == NaN
        if (da != da && db != db) return 0;
        // SameValueZero: -0 == +0
        return (da == db) ? 0 : 1;
    }
    if (ta != tb) return 1;
    if (ta == LMD_TYPE_STRING) {
        String* sa = it2s(ka);
        String* sb = it2s(kb);
        if (sa->len != sb->len) return 1;
        return memcmp(sa->chars, sb->chars, sa->len);
    }
    // reference equality for objects
    return (ka.item == kb.item) ? 0 : 1;
}

static JsCollectionData* js_get_collection_data(Item obj) {
    if (get_type_id(obj) != LMD_TYPE_MAP) return NULL;
    // Use js_map_get_fast (direct map lookup) instead of js_property_get to avoid
    // infinite recursion: js_property_get -> collection check -> js_get_collection_data -> js_property_get ...
    bool found = false;
    Item cd_item = js_map_get_fast(obj.map, "__cd", 4, &found);
    if (!found) return NULL;
    TypeId tid = get_type_id(cd_item);
    // Property not found (undefined) or null → no collection data
    if (tid == LMD_TYPE_UNDEFINED || tid == LMD_TYPE_NULL) return NULL;
    if (tid != LMD_TYPE_INT && tid != LMD_TYPE_INT64) {
        log_error("js_get_collection_data: cd_item tid=%d (not INT/INT64), cd_item.item=0x%llx", (int)tid, (unsigned long long)cd_item.item);
        return NULL;
    }
    int64_t ptr_val = it2i(cd_item);
    if (ptr_val == 0) return NULL;
    // Guard against corrupt/small pointer values that would cause SIGSEGV
    if (ptr_val < 4096) {
        log_error("js_get_collection_data: suspicious pointer value %lld, rejecting", (long long)ptr_val);
        return NULL;
    }
    return (JsCollectionData*)(uintptr_t)ptr_val;
}

static Item js_collection_create(int type) {
    HashMap* hm = hashmap_new(sizeof(JsCollectionEntry), 16, 0, 0,
        js_collection_hash, js_collection_compare, NULL, NULL);
    JsCollectionData* cd = (JsCollectionData*)pool_calloc(js_input->pool, sizeof(JsCollectionData));
    cd->hmap = hm;
    cd->type = type;
    cd->order_head = NULL;
    cd->order_tail = NULL;
    Item obj = js_new_object();
    Item cd_key = (Item){.item = s2it(heap_create_name(JS_COLLECTION_DATA_KEY))};
    Item cd_val = (Item){.item = i2it((int64_t)(uintptr_t)cd)};
    js_property_set(obj, cd_key, cd_val);
    // set initial size property
    Item size_key = (Item){.item = s2it(heap_create_name("size"))};
    Item size_val = (Item){.item = i2it(0)};
    js_property_set(obj, size_key, size_val);
    return obj;
}

// v41: Link a collection instance to its constructor's prototype
static void js_collection_link_prototype(Item obj, const char* ctor_name, int ctor_len) {
    Item ctor_name_item = (Item){.item = s2it(heap_create_name(ctor_name, ctor_len))};
    Item ctor = js_get_constructor(ctor_name_item);
    if (ctor.item != ITEM_NULL && get_type_id(ctor) == LMD_TYPE_FUNC) {
        Item proto_key = (Item){.item = s2it(heap_create_name("prototype", 9))};
        Item ctor_proto = js_property_get(ctor, proto_key);
        if (ctor_proto.item != ITEM_NULL && get_type_id(ctor_proto) == LMD_TYPE_MAP)
            js_set_prototype(obj, ctor_proto);
    }
}

// Helper: add or update an entry in the insertion-order list
static void js_collection_order_upsert(JsCollectionData* cd, Item key, Item value) {
    // Check if key already exists in the order list
    JsCollectionOrderNode* node = cd->order_head;
    while (node) {
        JsCollectionEntry ea = {.key = node->key};
        JsCollectionEntry eb = {.key = key};
        if (js_collection_compare(&ea, &eb, NULL) == 0) {
            // Key exists — update value in-place (preserves insertion order)
            node->value = value;
            return;
        }
        node = node->next;
    }
    // New entry — append to tail
    JsCollectionOrderNode* n = (JsCollectionOrderNode*)pool_calloc(js_input->pool, sizeof(JsCollectionOrderNode));
    n->key = key;
    n->value = value;
    n->next = NULL;
    n->prev = cd->order_tail;
    if (cd->order_tail) cd->order_tail->next = n;
    else cd->order_head = n;
    cd->order_tail = n;
}

// Helper: remove an entry from the insertion-order list
static void js_collection_order_remove(JsCollectionData* cd, Item key) {
    JsCollectionOrderNode* node = cd->order_head;
    while (node) {
        JsCollectionEntry ea = {.key = node->key};
        JsCollectionEntry eb = {.key = key};
        if (js_collection_compare(&ea, &eb, NULL) == 0) {
            if (node->prev) node->prev->next = node->next;
            else cd->order_head = node->next;
            if (node->next) node->next->prev = node->prev;
            else cd->order_tail = node->prev;
            return;
        }
        node = node->next;
    }
}

static void js_collection_update_size(Item obj, JsCollectionData* cd) {
    Item size_key = (Item){.item = s2it(heap_create_name("size"))};
    Item size_val = (Item){.item = i2it((int64_t)hashmap_count(cd->hmap))};
    js_property_set(obj, size_key, size_val);
}

extern "C" Item js_map_collection_new(void) {
    Item obj = js_collection_create(JS_COLLECTION_MAP);
    js_collection_link_prototype(obj, "Map", 3);
    return obj;
}

extern "C" Item js_set_collection_new(void) {
    Item obj = js_collection_create(JS_COLLECTION_SET);
    js_collection_link_prototype(obj, "Set", 3);
    return obj;
}

extern "C" Item js_iterable_to_array(Item iterable);

extern "C" Item js_set_collection_new_from(Item iterable) {
    Item set = js_collection_create(JS_COLLECTION_SET);
    js_collection_link_prototype(set, "Set", 3);
    TypeId tid = get_type_id(iterable);
    if (tid == LMD_TYPE_ARRAY) {
        Array* a = iterable.array;
        for (int i = 0; i < a->length; i++) {
            js_collection_method(set, 0, a->items[i], ItemNull);
        }
    } else if (tid == LMD_TYPE_MAP) {
        // iterable is another Set or Map — iterate its values via the order list
        JsCollectionData* cd = js_get_collection_data(iterable);
        if (cd) {
            for (JsCollectionOrderNode* node = cd->order_head; node; node = node->next) {
                js_collection_method(set, 0, node->key, ItemNull);
            }
        }
    } else if (tid == LMD_TYPE_STRING) {
        // Iterate string characters
        String* s = it2s(iterable);
        if (s && s->len > 0) {
            size_t pos = 0;
            while (pos < s->len) {
                size_t clen = str_utf8_char_len((unsigned char)s->chars[pos]);
                if (pos + clen > s->len) break;
                Item ch = (Item){.item = s2it(heap_create_name(s->chars + pos, clen))};
                js_collection_method(set, 0, ch, ItemNull);
                pos += clen;
            }
        }
    } else {
        // Try iterator protocol for other iterables
        Item arr = js_iterable_to_array(iterable);
        if (get_type_id(arr) == LMD_TYPE_ARRAY) {
            Array* a = arr.array;
            for (int i = 0; i < a->length; i++) {
                js_collection_method(set, 0, a->items[i], ItemNull);
            }
        }
    }
    return set;
}

extern "C" Item js_map_collection_new_from(Item iterable) {
    Item map = js_collection_create(JS_COLLECTION_MAP);
    js_collection_link_prototype(map, "Map", 3);
    TypeId tid = get_type_id(iterable);
    if (tid == LMD_TYPE_ARRAY) {
        Array* a = iterable.array;
        for (int i = 0; i < a->length; i++) {
            Item entry = a->items[i];
            if (get_type_id(entry) == LMD_TYPE_ARRAY && entry.array->length >= 2) {
                js_collection_method(map, 0, entry.array->items[0], entry.array->items[1]);
            }
        }
    } else if (tid == LMD_TYPE_MAP) {
        // copy from another Map
        JsCollectionData* cd = js_get_collection_data(iterable);
        if (cd && cd->type == JS_COLLECTION_MAP) {
            for (JsCollectionOrderNode* node = cd->order_head; node; node = node->next) {
                js_collection_method(map, 0, node->key, node->value);
            }
        }
    } else {
        // Try iterator protocol for other iterables
        Item arr = js_iterable_to_array(iterable);
        if (get_type_id(arr) == LMD_TYPE_ARRAY) {
            Array* a = arr.array;
            for (int i = 0; i < a->length; i++) {
                Item entry = a->items[i];
                if (get_type_id(entry) == LMD_TYPE_ARRAY && entry.array->length >= 2) {
                    js_collection_method(map, 0, entry.array->items[0], entry.array->items[1]);
                }
            }
        }
    }
    return map;
}

// Map/Set method dispatch
// method_id: 0=set/add, 1=get, 2=has, 3=delete, 4=clear,
//   5=forEach, 6=keys, 7=values, 8=entries, 9=size(getter)
extern "C" Item js_collection_method(Item obj, int method_id, Item arg1, Item arg2) {
    JsCollectionData* cd = js_get_collection_data(obj);
    if (!cd) return ItemNull;

    switch (method_id) {
        case 0: { // set(key, value) for Map, add(value) for Set
            JsCollectionEntry entry;
            if (cd->type == JS_COLLECTION_SET) {
                entry.key = arg1;
                entry.value = (Item){.item = b2it(BOOL_TRUE)};
            } else {
                entry.key = arg1;
                entry.value = arg2;
            }
            hashmap_set(cd->hmap, &entry);
            // Maintain insertion-order list
            js_collection_order_upsert(cd, entry.key, entry.value);
            js_collection_update_size(obj, cd);
            return obj; // return collection for chaining
        }
        case 1: { // get(key) — Map only
            JsCollectionEntry probe = {.key = arg1};
            const JsCollectionEntry* found = (const JsCollectionEntry*)hashmap_get(cd->hmap, &probe);
            if (found) return found->value;
            return make_js_undefined();
        }
        case 2: { // has(key)
            JsCollectionEntry probe = {.key = arg1};
            const JsCollectionEntry* found = (const JsCollectionEntry*)hashmap_get(cd->hmap, &probe);
            return (Item){.item = b2it(found ? BOOL_TRUE : BOOL_FALSE)};
        }
        case 3: { // delete(key)
            JsCollectionEntry probe = {.key = arg1};
            const JsCollectionEntry* found = (const JsCollectionEntry*)hashmap_delete(cd->hmap, &probe);
            js_collection_order_remove(cd, arg1);
            js_collection_update_size(obj, cd);
            return (Item){.item = b2it(found ? BOOL_TRUE : BOOL_FALSE)};
        }
        case 4: { // clear()
            hashmap_clear(cd->hmap, false);
            cd->order_head = NULL;
            cd->order_tail = NULL;
            js_collection_update_size(obj, cd);
            return make_js_undefined();
        }
        case 5: { // forEach(callback[, thisArg]) — insertion order
            // thisArg: use undefined if not provided (ItemNull means not passed)
            Item this_arg = (arg2.item && arg2.item != ItemNull.item) ? arg2 : make_js_undefined();
            JsCollectionOrderNode* node = cd->order_head;
            while (node) {
                if (cd->type == JS_COLLECTION_SET) {
                    // callback(value, value, set)
                    Item args[3] = {node->key, node->key, obj};
                    js_call_function(arg1, this_arg, args, 3);
                } else {
                    // callback(value, key, map)
                    Item args[3] = {node->value, node->key, obj};
                    js_call_function(arg1, this_arg, args, 3);
                }
                node = node->next;
            }
            return make_js_undefined();
        }
        case 6: { // keys() — returns iterator
            int bid = (cd->type == JS_COLLECTION_SET) ? JS_BUILTIN_SET_KEYS : JS_BUILTIN_MAP_KEYS;
            return js_dispatch_builtin(bid, obj, NULL, 0);
        }
        case 7: { // values() — returns iterator
            int bid = (cd->type == JS_COLLECTION_SET) ? JS_BUILTIN_SET_VALUES : JS_BUILTIN_MAP_VALUES;
            return js_dispatch_builtin(bid, obj, NULL, 0);
        }
        case 8: { // entries() — returns iterator
            int bid = (cd->type == JS_COLLECTION_SET) ? JS_BUILTIN_SET_ENTRIES : JS_BUILTIN_MAP_ENTRIES;
            return js_dispatch_builtin(bid, obj, NULL, 0);
        }
        default: return ItemNull;
    }
}

// Forward declarations for v14 promise support
struct JsPromise;
static JsPromise* js_get_promise(Item promise_obj);
extern "C" Item js_promise_then(Item promise, Item on_fulfilled, Item on_rejected);
extern "C" Item js_promise_catch(Item promise, Item on_rejected);
extern "C" Item js_promise_finally(Item promise, Item on_finally);
extern "C" bool js_is_generator(Item obj);
extern "C" Item js_generator_next(Item generator, Item input);
extern "C" Item js_generator_return(Item generator, Item value);
extern "C" Item js_generator_throw(Item generator, Item error);

// Map method dispatcher: handles collection methods, falls back to property access
extern "C" Item js_map_method(Item obj, Item method_name, Item* args, int argc) {
    // Document proxy methods (getElementById, querySelector, etc.)
    if (js_is_document_proxy(obj)) {
        return js_document_proxy_method(method_name, args, argc);
    }
    // CSSOM wrapper methods
    if (js_is_stylesheet(obj)) {
        return js_cssom_stylesheet_method(obj, method_name, args, argc);
    }
    if (js_is_rule_style_decl(obj)) {
        return js_cssom_rule_decl_method(obj, method_name, args, argc);
    }
    // Computed style getPropertyValue
    if (js_is_computed_style_item(obj)) {
        String* method = it2s(method_name);
        if (method && method->len == 16 && strncmp(method->chars, "getPropertyValue", 16) == 0) {
            if (argc < 1) return (Item){.item = s2it(heap_create_name(""))};
            return js_computed_style_get_property(obj, args[0]);
        }
    }
    // DataView methods
    if (js_is_dataview(obj)) {
        return js_dataview_method(obj, method_name, args, argc);
    }
    // TypedArray methods
    if (js_is_typed_array(obj)) {
        String* method = it2s(method_name);
        if (method) {
            if (method->len == 4 && strncmp(method->chars, "fill", 4) == 0) {
                Item value = argc > 0 ? args[0] : ItemNull;
                int start = argc > 1 ? (int)it2i(args[1]) : 0;
                int end = argc > 2 ? (int)it2i(args[2]) : INT_MAX;
                return js_typed_array_fill(obj, value, start, end);
            }
            if (method->len == 3 && strncmp(method->chars, "set", 3) == 0) {
                Item source = argc > 0 ? args[0] : ItemNull;
                int offset = argc > 1 ? (int)it2i(args[1]) : 0;
                return js_typed_array_set_from(obj, source, offset);
            }
            if (method->len == 8 && strncmp(method->chars, "subarray", 8) == 0) {
                int start = argc > 0 ? (int)it2i(args[0]) : 0;
                JsTypedArray* ta = (JsTypedArray*)obj.map->data;
                int end = argc > 1 ? (int)it2i(args[1]) : ta->length;
                return js_typed_array_subarray(obj, start, end);
            }
            if (method->len == 5 && strncmp(method->chars, "slice", 5) == 0) {
                int start = argc > 0 ? (int)it2i(args[0]) : 0;
                JsTypedArray* ta = (JsTypedArray*)obj.map->data;
                int end = argc > 1 ? (int)it2i(args[1]) : ta->length;
                return js_typed_array_slice(obj, start, end);
            }
            if (method->len == 3 && strncmp(method->chars, "map", 3) == 0) {
                if (argc < 1) return obj;
                Item callback = args[0];
                JsTypedArray* ta = (JsTypedArray*)obj.map->data;
                int len = ta->length;
                Item result = js_typed_array_new((int)ta->element_type, len);
                for (int i = 0; i < len; i++) {
                    Item elem = js_typed_array_get(obj, (Item){.item = i2it(i)});
                    Item idx_item = {.item = i2it(i)};
                    Item fn_args[3] = {elem, idx_item, obj};
                    Item mapped = js_call_function(callback, make_js_undefined(), fn_args, 3);
                    js_typed_array_set(result, (Item){.item = i2it(i)}, mapped);
                }
                return result;
            }
            if (method->len == 7 && strncmp(method->chars, "indexOf", 7) == 0) {
                if (argc < 1) return (Item){.item = i2it(-1)};
                JsTypedArray* ta = (JsTypedArray*)obj.map->data;
                int len = ta->length;
                int from = argc > 1 ? (int)it2i(args[1]) : 0;
                if (from < 0) { from += len; if (from < 0) from = 0; }
                for (int i = from; i < len; i++) {
                    Item elem = js_typed_array_get(obj, (Item){.item = i2it(i)});
                    if (it2b(js_strict_equal(elem, args[0]))) return (Item){.item = i2it(i)};
                }
                return (Item){.item = i2it(-1)};
            }
            if (method->len == 8 && strncmp(method->chars, "includes", 8) == 0) {
                if (argc < 1) return (Item){.item = ITEM_FALSE};
                JsTypedArray* ta = (JsTypedArray*)obj.map->data;
                int len = ta->length;
                int from = argc > 1 ? (int)it2i(args[1]) : 0;
                if (from < 0) { from += len; if (from < 0) from = 0; }
                for (int i = from; i < len; i++) {
                    Item elem = js_typed_array_get(obj, (Item){.item = i2it(i)});
                    if (it2b(js_strict_equal(elem, args[0]))) return (Item){.item = ITEM_TRUE};
                }
                return (Item){.item = ITEM_FALSE};
            }
            if (method->len == 7 && strncmp(method->chars, "forEach", 7) == 0) {
                if (argc < 1) return ItemNull;
                Item callback = args[0];
                JsTypedArray* ta = (JsTypedArray*)obj.map->data;
                int len = ta->length;
                for (int i = 0; i < len; i++) {
                    Item elem = js_typed_array_get(obj, (Item){.item = i2it(i)});
                    Item idx_item = {.item = i2it(i)};
                    Item fn_args[3] = {elem, idx_item, obj};
                    js_call_function(callback, make_js_undefined(), fn_args, 3);
                }
                return make_js_undefined();
            }
            if (method->len == 6 && strncmp(method->chars, "reduce", 6) == 0) {
                if (argc < 1) return ItemNull;
                Item callback = args[0];
                JsTypedArray* ta = (JsTypedArray*)obj.map->data;
                int len = ta->length;
                int start_idx = 0;
                Item acc;
                if (argc > 1) {
                    acc = args[1];
                } else if (len > 0) {
                    acc = js_typed_array_get(obj, (Item){.item = i2it(0)});
                    start_idx = 1;
                } else {
                    return ItemNull;
                }
                for (int i = start_idx; i < len; i++) {
                    Item elem = js_typed_array_get(obj, (Item){.item = i2it(i)});
                    Item idx_item = {.item = i2it(i)};
                    Item fn_args[4] = {acc, elem, idx_item, obj};
                    acc = js_call_function(callback, make_js_undefined(), fn_args, 4);
                }
                return acc;
            }
            if (method->len == 4 && strncmp(method->chars, "find", 4) == 0) {
                if (argc < 1) return make_js_undefined();
                Item callback = args[0];
                JsTypedArray* ta = (JsTypedArray*)obj.map->data;
                int len = ta->length;
                for (int i = 0; i < len; i++) {
                    Item elem = js_typed_array_get(obj, (Item){.item = i2it(i)});
                    Item idx_item = {.item = i2it(i)};
                    Item fn_args[3] = {elem, idx_item, obj};
                    Item result = js_call_function(callback, make_js_undefined(), fn_args, 3);
                    if (js_is_truthy(result)) return elem;
                }
                return make_js_undefined();
            }
            if (method->len == 9 && strncmp(method->chars, "findIndex", 9) == 0) {
                if (argc < 1) return (Item){.item = i2it(-1)};
                Item callback = args[0];
                JsTypedArray* ta = (JsTypedArray*)obj.map->data;
                int len = ta->length;
                for (int i = 0; i < len; i++) {
                    Item elem = js_typed_array_get(obj, (Item){.item = i2it(i)});
                    Item idx_item = {.item = i2it(i)};
                    Item fn_args[3] = {elem, idx_item, obj};
                    Item result = js_call_function(callback, make_js_undefined(), fn_args, 3);
                    if (js_is_truthy(result)) return (Item){.item = i2it(i)};
                }
                return (Item){.item = i2it(-1)};
            }
            if (method->len == 5 && strncmp(method->chars, "every", 5) == 0) {
                if (argc < 1) return (Item){.item = ITEM_TRUE};
                Item callback = args[0];
                JsTypedArray* ta = (JsTypedArray*)obj.map->data;
                int len = ta->length;
                for (int i = 0; i < len; i++) {
                    Item elem = js_typed_array_get(obj, (Item){.item = i2it(i)});
                    Item idx_item = {.item = i2it(i)};
                    Item fn_args[3] = {elem, idx_item, obj};
                    Item result = js_call_function(callback, make_js_undefined(), fn_args, 3);
                    if (!js_is_truthy(result)) return (Item){.item = ITEM_FALSE};
                }
                return (Item){.item = ITEM_TRUE};
            }
            if (method->len == 4 && strncmp(method->chars, "some", 4) == 0) {
                if (argc < 1) return (Item){.item = ITEM_FALSE};
                Item callback = args[0];
                JsTypedArray* ta = (JsTypedArray*)obj.map->data;
                int len = ta->length;
                for (int i = 0; i < len; i++) {
                    Item elem = js_typed_array_get(obj, (Item){.item = i2it(i)});
                    Item idx_item = {.item = i2it(i)};
                    Item fn_args[3] = {elem, idx_item, obj};
                    Item result = js_call_function(callback, make_js_undefined(), fn_args, 3);
                    if (js_is_truthy(result)) return (Item){.item = ITEM_TRUE};
                }
                return (Item){.item = ITEM_FALSE};
            }
            if (method->len == 4 && strncmp(method->chars, "join", 4) == 0) {
                JsTypedArray* ta = (JsTypedArray*)obj.map->data;
                int len = ta->length;
                const char* sep = ",";
                int sep_len = 1;
                if (argc > 0 && get_type_id(args[0]) == LMD_TYPE_STRING) {
                    String* s = it2s(args[0]);
                    sep = s->chars;
                    sep_len = s->len;
                }
                StrBuf* sb = strbuf_new();
                for (int i = 0; i < len; i++) {
                    if (i > 0) strbuf_append_str_n(sb, sep, sep_len);
                    Item elem = js_typed_array_get(obj, (Item){.item = i2it(i)});
                    char buf[64];
                    int n;
                    if (get_type_id(elem) == LMD_TYPE_INT) {
                        n = snprintf(buf, sizeof(buf), "%lld", (long long)it2i(elem));
                    } else if (get_type_id(elem) == LMD_TYPE_FLOAT) {
                        js_double_to_string(*(double*)it2p(elem), buf, sizeof(buf));
                        n = (int)strlen(buf);
                    } else {
                        n = snprintf(buf, sizeof(buf), "0");
                    }
                    strbuf_append_str_n(sb, buf, n);
                }
                String* result = heap_create_name(sb->str, sb->length);
                strbuf_free(sb);
                return (Item){.item = s2it(result)};
            }
            if (method->len == 6 && strncmp(method->chars, "filter", 6) == 0) {
                if (argc < 1) return js_typed_array_new((int)((JsTypedArray*)obj.map->data)->element_type, 0);
                Item callback = args[0];
                JsTypedArray* ta = (JsTypedArray*)obj.map->data;
                int len = ta->length;
                // collect matching elements first
                Item* temp = (Item*)malloc(len * sizeof(Item));
                int count = 0;
                for (int i = 0; i < len; i++) {
                    Item elem = js_typed_array_get(obj, (Item){.item = i2it(i)});
                    Item idx_item = {.item = i2it(i)};
                    Item fn_args[3] = {elem, idx_item, obj};
                    Item result = js_call_function(callback, make_js_undefined(), fn_args, 3);
                    if (js_is_truthy(result)) temp[count++] = elem;
                }
                Item result = js_typed_array_new((int)ta->element_type, count);
                for (int i = 0; i < count; i++) {
                    js_typed_array_set(result, (Item){.item = i2it(i)}, temp[i]);
                }
                free(temp);
                return result;
            }
            if (method->len == 7 && strncmp(method->chars, "reverse", 7) == 0) {
                JsTypedArray* ta = (JsTypedArray*)obj.map->data;
                int len = ta->length;
                for (int i = 0; i < len / 2; i++) {
                    Item a = js_typed_array_get(obj, (Item){.item = i2it(i)});
                    Item b = js_typed_array_get(obj, (Item){.item = i2it(len - 1 - i)});
                    js_typed_array_set(obj, (Item){.item = i2it(i)}, b);
                    js_typed_array_set(obj, (Item){.item = i2it(len - 1 - i)}, a);
                }
                return obj;
            }
            if (method->len == 10 && strncmp(method->chars, "copyWithin", 10) == 0) {
                // copyWithin(target, start, end?)
                JsTypedArray* ta = (JsTypedArray*)obj.map->data;
                int len = ta->length;
                int target = argc > 0 ? (int)it2i(args[0]) : 0;
                int start = argc > 1 ? (int)it2i(args[1]) : 0;
                int end = argc > 2 ? (int)it2i(args[2]) : len;
                if (target < 0) target += len;
                if (start < 0) start += len;
                if (end < 0) end += len;
                int count = end - start;
                if (count <= 0) return obj;
                int elem_size = 1;
                switch (ta->element_type) {
                case JS_TYPED_INT8: case JS_TYPED_UINT8: elem_size = 1; break;
                case JS_TYPED_INT16: case JS_TYPED_UINT16: elem_size = 2; break;
                case JS_TYPED_INT32: case JS_TYPED_UINT32: case JS_TYPED_FLOAT32: elem_size = 4; break;
                case JS_TYPED_FLOAT64: elem_size = 8; break;
                }
                memmove((uint8_t*)ta->data + target * elem_size,
                        (uint8_t*)ta->data + start * elem_size,
                        count * elem_size);
                return obj;
            }
            if (method->len == 2 && strncmp(method->chars, "at", 2) == 0) {
                JsTypedArray* ta = (JsTypedArray*)obj.map->data;
                int idx = argc > 0 ? (int)js_get_number(args[0]) : 0;
                if (idx < 0) idx += ta->length;
                if (idx < 0 || idx >= ta->length) return make_js_undefined();
                return js_typed_array_get(obj, (Item){.item = i2it(idx)});
            }
            if (method->len == 4 && strncmp(method->chars, "sort", 4) == 0) {
                JsTypedArray* ta = (JsTypedArray*)obj.map->data;
                int len = ta->length;
                if (len <= 1) return obj;
                // simple insertion sort
                for (int i = 1; i < len; i++) {
                    Item key = js_typed_array_get(obj, (Item){.item = i2it(i)});
                    double key_val = js_get_number(key);
                    int j = i - 1;
                    while (j >= 0) {
                        Item jval = js_typed_array_get(obj, (Item){.item = i2it(j)});
                        double jdbl = js_get_number(jval);
                        if (argc > 0) {
                            Item cmp_args[2] = {jval, key};
                            Item cmp = js_call_function(args[0], make_js_undefined(), cmp_args, 2);
                            if (js_get_number(cmp) <= 0) break;
                        } else {
                            if (jdbl <= key_val) break;
                        }
                        js_typed_array_set(obj, (Item){.item = i2it(j + 1)}, jval);
                        j--;
                    }
                    js_typed_array_set(obj, (Item){.item = i2it(j + 1)}, key);
                }
                return obj;
            }
        }
    }
    // ArrayBuffer methods
    if (js_is_arraybuffer(obj)) {
        String* method = it2s(method_name);
        if (method) {
            if (method->len == 5 && strncmp(method->chars, "slice", 5) == 0) {
                int begin = argc > 0 ? (int)it2i(args[0]) : 0;
                int end = argc > 1 ? (int)it2i(args[1]) : js_arraybuffer_byte_length(obj);
                return js_arraybuffer_slice(obj, begin, end);
            }
        }
    }
    // Check if this is a Map/Set collection
    JsCollectionData* cd = js_get_collection_data(obj);
    if (cd) {
        String* method = it2s(method_name);
        if (method) {
            int method_id = -1;
            if (cd->type == JS_COLLECTION_MAP) {
                if (method->len == 3 && strncmp(method->chars, "set", 3) == 0) method_id = 0;
                else if (method->len == 3 && strncmp(method->chars, "get", 3) == 0) method_id = 1;
                else if (method->len == 3 && strncmp(method->chars, "has", 3) == 0) method_id = 2;
                else if (method->len == 6 && strncmp(method->chars, "delete", 6) == 0) method_id = 3;
                else if (method->len == 5 && strncmp(method->chars, "clear", 5) == 0) method_id = 4;
                else if (method->len == 7 && strncmp(method->chars, "forEach", 7) == 0) method_id = 5;
                else if (method->len == 4 && strncmp(method->chars, "keys", 4) == 0) method_id = 6;
                else if (method->len == 6 && strncmp(method->chars, "values", 6) == 0) method_id = 7;
                else if (method->len == 7 && strncmp(method->chars, "entries", 7) == 0) method_id = 8;
            } else {
                if (method->len == 3 && strncmp(method->chars, "add", 3) == 0) method_id = 0;
                else if (method->len == 3 && strncmp(method->chars, "has", 3) == 0) method_id = 2;
                else if (method->len == 6 && strncmp(method->chars, "delete", 6) == 0) method_id = 3;
                else if (method->len == 5 && strncmp(method->chars, "clear", 5) == 0) method_id = 4;
                else if (method->len == 7 && strncmp(method->chars, "forEach", 7) == 0) method_id = 5;
                else if (method->len == 4 && strncmp(method->chars, "keys", 4) == 0) method_id = 6;
                else if (method->len == 6 && strncmp(method->chars, "values", 6) == 0) method_id = 7;
                else if (method->len == 7 && strncmp(method->chars, "entries", 7) == 0) method_id = 8;
                // ES2025 Set methods
                // Helper: check if a Set-like "other" contains key
                // If other is a Set collection, use fast hashmap lookup
                // Otherwise, call other.has(key) method
                auto set_like_has = [](Item other, JsCollectionData* other_cd, Item key) -> bool {
                    if (other_cd) {
                        JsCollectionEntry probe = {.key = key};
                        return hashmap_get(other_cd->hmap, &probe) != NULL;
                    }
                    // Set-like: call other.has(key)
                    if (get_type_id(other) == LMD_TYPE_MAP) {
                        String* has_name = heap_create_name("has", 3);
                        Item has_fn = js_property_get(other, (Item){.item = s2it(has_name)});
                        if (get_type_id(has_fn) == LMD_TYPE_FUNC) {
                            Item fn_args[1] = {key};
                            Item result = js_call_function(has_fn, other, fn_args, 1);
                            return js_is_truthy(result);
                        }
                    }
                    return false;
                };
                // Helper: iterate Set-like "other" keys
                auto set_like_keys = [](Item other, JsCollectionData* other_cd) -> Item {
                    if (other_cd) {
                        // drain to array
                        Item arr = js_array_new(0);
                        for (JsCollectionOrderNode* node = other_cd->order_head; node; node = node->next)
                            js_array_push_item_direct(arr.array, node->key);
                        return arr;
                    }
                    // Set-like: call other.keys() and drain to array
                    if (get_type_id(other) == LMD_TYPE_MAP) {
                        String* keys_name = heap_create_name("keys", 4);
                        Item keys_fn = js_property_get(other, (Item){.item = s2it(keys_name)});
                        if (get_type_id(keys_fn) == LMD_TYPE_FUNC) {
                            Item iter = js_call_function(keys_fn, other, NULL, 0);
                            return js_iterable_to_array(iter);
                        }
                    }
                    return js_array_new(0);
                };

                if (cd->type == JS_COLLECTION_SET && method->len == 12 && strncmp(method->chars, "intersection", 12) == 0) {
                    // Set.prototype.intersection(other)
                    Item other = argc > 0 ? args[0] : ItemNull;
                    JsCollectionData* other_cd = js_get_collection_data(other);
                    Item result = js_collection_create(JS_COLLECTION_SET);
                    js_collection_link_prototype(result, "Set", 3);
                    for (JsCollectionOrderNode* node = cd->order_head; node; node = node->next) {
                        if (set_like_has(other, other_cd, node->key))
                            js_collection_method(result, 0, node->key, ItemNull);
                    }
                    return result;
                }
                else if (cd->type == JS_COLLECTION_SET && method->len == 5 && strncmp(method->chars, "union", 5) == 0) {
                    // Set.prototype.union(other)
                    Item other = argc > 0 ? args[0] : ItemNull;
                    JsCollectionData* other_cd = js_get_collection_data(other);
                    Item result = js_collection_create(JS_COLLECTION_SET);
                    js_collection_link_prototype(result, "Set", 3);
                    for (JsCollectionOrderNode* node = cd->order_head; node; node = node->next)
                        js_collection_method(result, 0, node->key, ItemNull);
                    Item other_keys = set_like_keys(other, other_cd);
                    int64_t ok_len = js_array_length(other_keys);
                    for (int64_t i = 0; i < ok_len; i++) {
                        Item k = js_array_get(other_keys, (Item){.item = i2it(i)});
                        js_collection_method(result, 0, k, ItemNull);
                    }
                    return result;
                }
                else if (cd->type == JS_COLLECTION_SET && method->len == 10 && strncmp(method->chars, "difference", 10) == 0) {
                    // Set.prototype.difference(other)
                    Item other = argc > 0 ? args[0] : ItemNull;
                    JsCollectionData* other_cd = js_get_collection_data(other);
                    Item result = js_collection_create(JS_COLLECTION_SET);
                    js_collection_link_prototype(result, "Set", 3);
                    for (JsCollectionOrderNode* node = cd->order_head; node; node = node->next) {
                        if (!set_like_has(other, other_cd, node->key))
                            js_collection_method(result, 0, node->key, ItemNull);
                    }
                    return result;
                }
                else if (cd->type == JS_COLLECTION_SET && method->len == 19 && strncmp(method->chars, "symmetricDifference", 19) == 0) {
                    // Set.prototype.symmetricDifference(other)
                    Item other = argc > 0 ? args[0] : ItemNull;
                    JsCollectionData* other_cd = js_get_collection_data(other);
                    Item result = js_collection_create(JS_COLLECTION_SET);
                    js_collection_link_prototype(result, "Set", 3);
                    for (JsCollectionOrderNode* node = cd->order_head; node; node = node->next) {
                        if (!set_like_has(other, other_cd, node->key))
                            js_collection_method(result, 0, node->key, ItemNull);
                    }
                    Item other_keys = set_like_keys(other, other_cd);
                    int64_t ok_len = js_array_length(other_keys);
                    for (int64_t i = 0; i < ok_len; i++) {
                        Item k = js_array_get(other_keys, (Item){.item = i2it(i)});
                        JsCollectionEntry probe = {.key = k};
                        if (!hashmap_get(cd->hmap, &probe))
                            js_collection_method(result, 0, k, ItemNull);
                    }
                    return result;
                }
                else if (cd->type == JS_COLLECTION_SET && method->len == 10 && strncmp(method->chars, "isSubsetOf", 10) == 0) {
                    // Set.prototype.isSubsetOf(other)
                    Item other = argc > 0 ? args[0] : ItemNull;
                    JsCollectionData* other_cd = js_get_collection_data(other);
                    for (JsCollectionOrderNode* node = cd->order_head; node; node = node->next) {
                        if (!set_like_has(other, other_cd, node->key)) return (Item){.item = ITEM_FALSE};
                    }
                    return (Item){.item = ITEM_TRUE};
                }
                else if (cd->type == JS_COLLECTION_SET && method->len == 12 && strncmp(method->chars, "isSupersetOf", 12) == 0) {
                    // Set.prototype.isSupersetOf(other)
                    Item other = argc > 0 ? args[0] : ItemNull;
                    JsCollectionData* other_cd = js_get_collection_data(other);
                    Item other_keys = set_like_keys(other, other_cd);
                    int64_t ok_len = js_array_length(other_keys);
                    for (int64_t i = 0; i < ok_len; i++) {
                        Item k = js_array_get(other_keys, (Item){.item = i2it(i)});
                        JsCollectionEntry probe = {.key = k};
                        if (!hashmap_get(cd->hmap, &probe)) return (Item){.item = ITEM_FALSE};
                    }
                    return (Item){.item = ITEM_TRUE};
                }
                else if (cd->type == JS_COLLECTION_SET && method->len == 14 && strncmp(method->chars, "isDisjointFrom", 14) == 0) {
                    // Set.prototype.isDisjointFrom(other)
                    Item other = argc > 0 ? args[0] : ItemNull;
                    JsCollectionData* other_cd = js_get_collection_data(other);
                    for (JsCollectionOrderNode* node = cd->order_head; node; node = node->next) {
                        if (set_like_has(other, other_cd, node->key)) return (Item){.item = ITEM_FALSE};
                    }
                    return (Item){.item = ITEM_TRUE};
                }
            }
            if (method_id >= 0) {
                Item arg1 = argc > 0 ? args[0] : ItemNull;
                Item arg2 = argc > 1 ? args[1] : ItemNull;
                return js_collection_method(obj, method_id, arg1, arg2);
            }
        }
    }
    // v14: Promise instance methods (.then, .catch, .finally)
    JsPromise* promise = js_get_promise(obj);
    if (promise) {
        String* method = it2s(method_name);
        if (method) {
            if (method->len == 4 && strncmp(method->chars, "then", 4) == 0) {
                Item on_fulfilled = argc > 0 ? args[0] : ItemNull;
                Item on_rejected = argc > 1 ? args[1] : ItemNull;
                return js_promise_then(obj, on_fulfilled, on_rejected);
            }
            if (method->len == 5 && strncmp(method->chars, "catch", 5) == 0) {
                Item on_rejected = argc > 0 ? args[0] : ItemNull;
                return js_promise_catch(obj, on_rejected);
            }
            if (method->len == 7 && strncmp(method->chars, "finally", 7) == 0) {
                Item on_finally = argc > 0 ? args[0] : ItemNull;
                return js_promise_finally(obj, on_finally);
            }
        }
    }

    // v15: Generator instance methods (.next, .return, .throw)
    if (js_is_generator(obj)) {
        String* method = it2s(method_name);
        if (method) {
            if (method->len == 4 && strncmp(method->chars, "next", 4) == 0) {
                Item input = argc > 0 ? args[0] : make_js_undefined();
                return js_generator_next(obj, input);
            }
            if (method->len == 6 && strncmp(method->chars, "return", 6) == 0) {
                Item value = argc > 0 ? args[0] : make_js_undefined();
                return js_generator_return(obj, value);
            }
            if (method->len == 5 && strncmp(method->chars, "throw", 5) == 0) {
                Item error = argc > 0 ? args[0] : make_js_undefined();
                return js_generator_throw(obj, error);
            }
        }
    }

    // v15: Regex instance methods (.exec, .test)
    {
        JsRegexData* rd = js_get_regex_data(obj);
        if (rd) {
            String* method = it2s(method_name);
            if (method) {
                if (method->len == 4 && strncmp(method->chars, "exec", 4) == 0) {
                    Item str = argc > 0 ? args[0] : ItemNull;
                    return js_regex_exec(obj, str);
                }
                if (method->len == 4 && strncmp(method->chars, "test", 4) == 0) {
                    Item str = argc > 0 ? args[0] : ItemNull;
                    return js_regex_test(obj, str);
                }
            }
        }
    }

    // TextEncoder/TextDecoder instance methods (.encode, .decode)
    {
        String* method = it2s(method_name);
        if (method) {
            if (method->len == 6 && strncmp(method->chars, "encode", 6) == 0) {
                Item str_arg = argc > 0 ? args[0] : ItemNull;
                return js_text_encoder_encode(obj, str_arg);
            }
            if (method->len == 6 && strncmp(method->chars, "decode", 6) == 0) {
                Item input_arg = argc > 0 ? args[0] : ItemNull;
                return js_text_decoder_decode(obj, input_arg);
            }
        }
    }

    // Built-in object method fallbacks (before property access)
    {
        String* method = it2s(method_name);
        if (method) {
            if (method->len == 8 && strncmp(method->chars, "toString", 8) == 0) {
                // v20: Check user-defined toString (own + prototype) BEFORE builtins
                // This prevents Object.prototype.toString from intercepting Error objects
                {
                    bool own_ts = false;
                    Item ts_fn = js_map_get_fast(obj.map, "toString", 8, &own_ts);
                    if (own_ts && get_type_id(ts_fn) == LMD_TYPE_FUNC) {
                        return js_call_function(ts_fn, obj, args, argc);
                    }
                }
                {
                    Item ts_key = (Item){.item = s2it(heap_create_name("toString", 8))};
                    Item ts_fn = js_prototype_lookup(obj, ts_key);
                    if (ts_fn.item != ItemNull.item && get_type_id(ts_fn) == LMD_TYPE_FUNC) {
                        return js_call_function(ts_fn, obj, args, argc);
                    }
                }
                // v20: Error.prototype.toString — "name: message" format
                {
                    bool cn_own = false;
                    Item cn = js_map_get_fast(obj.map, "__class_name__", 14, &cn_own);
                    if (cn_own && get_type_id(cn) == LMD_TYPE_STRING) {
                        String* cn_str = it2s(cn);
                        if (cn_str && cn_str->len >= 5 &&
                            strncmp(cn_str->chars + cn_str->len - 5, "Error", 5) == 0) {
                            return js_to_string(obj);
                        }
                    }
                }
                // v18l: Wrapper objects (Number, String, Boolean) — delegate to primitive methods
                bool own_cn = false;
                Item cn = js_map_get_fast(obj.map, "__class_name__", 14, &own_cn);
                if (own_cn && get_type_id(cn) == LMD_TYPE_STRING) {
                    String* cn_str = it2s(cn);
                    bool own_pv = false;
                    Item pv = js_map_get_fast(obj.map, "__primitiveValue__", 18, &own_pv);
                    if (own_pv) {
                        if (cn_str->len == 6 && strncmp(cn_str->chars, "Number", 6) == 0) {
                            return js_number_method(pv, method_name, args, argc);
                        }
                        if (cn_str->len == 6 && strncmp(cn_str->chars, "String", 6) == 0) {
                            return js_to_string(pv);
                        }
                        if (cn_str->len == 7 && strncmp(cn_str->chars, "Boolean", 7) == 0) {
                            return js_to_string(pv);
                        }
                    }
                }
                // Built-in fallback: use js_to_string (handles plain objects)
                return js_to_string(obj);
            }
            if (method->len == 14 && strncmp(method->chars, "hasOwnProperty", 14) == 0) {
                if (argc > 0) {
                    String* prop = it2s(js_to_string(args[0]));
                    if (prop) {
                        // v18: handle function objects separately
                        if (get_type_id(obj) == LMD_TYPE_FUNC) {
                            JsFunction* fn = (JsFunction*)obj.function;
                            // built-in own properties: name, length (always own)
                            if ((prop->len == 4 && strncmp(prop->chars, "name", 4) == 0) ||
                                (prop->len == 6 && strncmp(prop->chars, "length", 6) == 0)) {
                                return (Item){.item = ITEM_TRUE};
                            }
                            // prototype is own only for non-global-builtin wrappers
                            if (prop->len == 9 && strncmp(prop->chars, "prototype", 9) == 0) {
                                if (fn->builtin_id != -2) return (Item){.item = ITEM_TRUE};
                                return (Item){.item = ITEM_FALSE};
                            }
                            // check custom properties backing map
                            if (fn->properties_map.item != 0) {
                                bool own = false;
                                js_map_get_fast(fn->properties_map.map, prop->chars, prop->len, &own);
                                if (own) return (Item){.item = ITEM_TRUE};
                            }
                            return (Item){.item = ITEM_FALSE};
                        }
                        // handle arrays: check numeric indices and "length"
                        if (get_type_id(obj) == LMD_TYPE_ARRAY) {
                            Array* arr = obj.array;
                            if (prop->len == 6 && strncmp(prop->chars, "length", 6) == 0)
                                return (Item){.item = ITEM_TRUE};
                            // try numeric index
                            char* end = NULL;
                            long idx = strtol(prop->chars, &end, 10);
                            if (end == prop->chars + prop->len && idx >= 0 && idx < arr->length) {
                                // check if it's a hole
                                if (arr->items[idx].item == JS_DELETED_SENTINEL_VAL)
                                    return (Item){.item = ITEM_FALSE};
                                return (Item){.item = ITEM_TRUE};
                            }
                            // check companion map (extra properties on array)
                            if (arr->extra) {
                                bool own = false;
                                js_map_get_fast((Map*)arr->extra, prop->chars, prop->len, &own);
                                if (own) return (Item){.item = ITEM_TRUE};
                            }
                            return (Item){.item = ITEM_FALSE};
                        }
                        bool own = false;
                        js_map_get_fast(obj.map, prop->chars, prop->len, &own);
                        return own ? (Item){.item = ITEM_TRUE} : (Item){.item = ITEM_FALSE};
                    }
                }
                return (Item){.item = ITEM_FALSE};
            }
        }
    }

    // Fallback: property access + call
    Item fn = js_property_access(obj, method_name);
    return js_call_function(fn, obj, args, argc);
}

// =============================================================================
// Helper: convert a JS number arg (typically float from push_d) to an int Item
// Lambda fn_substring requires LMD_TYPE_INT, but JS literals arrive as floats.
// =============================================================================
static Item js_arg_to_int(Item arg) {
    TypeId tid = get_type_id(arg);
    if (tid == LMD_TYPE_INT || tid == LMD_TYPE_INT64) return arg;
    if (tid == LMD_TYPE_FLOAT) {
        double d = arg.get_double();
        return (Item){.item = i2it((int64_t)d)};
    }
    // fallback: try js_get_number
    double d = js_get_number(arg);
    return (Item){.item = i2it((int64_t)d)};
}

// =============================================================================
// JS Unicode whitespace helper (ES spec WhiteSpace + LineTerminator)
// =============================================================================

// decode one UTF-8 codepoint starting at s[i], return codepoint and advance i
static uint32_t js_decode_utf8(const char* s, size_t len, size_t* i) {
    unsigned char c = (unsigned char)s[*i];
    uint32_t cp;
    int extra;
    if (c < 0x80)      { cp = c; extra = 0; }
    else if (c < 0xC0) { (*i)++; return 0xFFFD; }
    else if (c < 0xE0) { cp = c & 0x1F; extra = 1; }
    else if (c < 0xF0) { cp = c & 0x0F; extra = 2; }
    else               { cp = c & 0x07; extra = 3; }
    for (int j = 0; j < extra; j++) {
        if (*i + 1 + j >= len) { *i += 1; return 0xFFFD; }
        cp = (cp << 6) | ((unsigned char)s[*i + 1 + j] & 0x3F);
    }
    *i += 1 + extra;
    return cp;
}

// check if codepoint is ES spec whitespace (WhiteSpace + LineTerminator)
static bool js_is_whitespace_cp(uint32_t cp) {
    switch (cp) {
    case 0x0009: case 0x000A: case 0x000B: case 0x000C: case 0x000D: case 0x0020:
    case 0x00A0: case 0x1680:
    case 0x2000: case 0x2001: case 0x2002: case 0x2003: case 0x2004:
    case 0x2005: case 0x2006: case 0x2007: case 0x2008: case 0x2009: case 0x200A:
    case 0x2028: case 0x2029: case 0x202F: case 0x205F: case 0x3000: case 0xFEFF:
        return true;
    default:
        return false;
    }
}

// return byte length of UTF-8 char at position
static int js_utf8_char_len(unsigned char c) {
    if (c < 0x80) return 1;
    if (c < 0xC0) return 1;
    if (c < 0xE0) return 2;
    if (c < 0xF0) return 3;
    return 4;
}

// check if byte position starts a whitespace codepoint
static bool js_is_whitespace_at(const char* s, size_t len, size_t pos) {
    size_t p = pos;
    uint32_t cp = js_decode_utf8(s, len, &p);
    return js_is_whitespace_cp(cp);
}

// =============================================================================
// String replace/replaceAll implementation with regex + callback support
// =============================================================================

// v46: Process $-substitution patterns in replacement strings per ES spec
// $$ → $, $& → matched, $` → before match, $' → after match, $N → capture group N
static void js_apply_replacement(StrBuf* buf, const char* repl, int repl_len,
    const char* src, int src_len, int match_start, int match_len,
    re2::StringPiece* groups, int ngroups)
{
    for (int i = 0; i < repl_len; i++) {
        if (repl[i] == '$' && i + 1 < repl_len) {
            char next = repl[i + 1];
            if (next == '$') {
                strbuf_append_char(buf, '$');
                i++;
            } else if (next == '&') {
                strbuf_append_str_n(buf, src + match_start, match_len);
                i++;
            } else if (next == '`') {
                strbuf_append_str_n(buf, src, match_start);
                i++;
            } else if (next == '\'') {
                int after = match_start + match_len;
                if (after < src_len)
                    strbuf_append_str_n(buf, src + after, src_len - after);
                i++;
            } else if (next >= '1' && next <= '9') {
                // $N or $NN capture group reference
                int group_idx = next - '0';
                // Check for two-digit group number ($10-$99)
                if (i + 2 < repl_len && repl[i + 2] >= '0' && repl[i + 2] <= '9') {
                    int two_digit = group_idx * 10 + (repl[i + 2] - '0');
                    if (two_digit < ngroups && two_digit > 0) {
                        group_idx = two_digit;
                        i++;
                    }
                }
                if (groups && group_idx < ngroups && groups[group_idx].data()) {
                    strbuf_append_str_n(buf, groups[group_idx].data(), (int)groups[group_idx].size());
                }
                i++;
            } else {
                strbuf_append_char(buf, '$');
            }
        } else {
            strbuf_append_char(buf, repl[i]);
        }
    }
}

static Item js_string_replace_impl(Item str, Item* args, int argc, bool is_replace_all) {
    String* s = it2s(str);
    if (!s || s->len == 0) return str;
    bool replacement_is_func = (get_type_id(args[1]) == LMD_TYPE_FUNC);
    JsRegexData* rd = js_get_regex_data(args[0]);

    if (rd) {
        // regex-based replace
        re2::StringPiece input(s->chars, s->len);
        int ngroups = rd->re2->NumberOfCapturingGroups() + 1;
        if (ngroups > 16) ngroups = 16;
        re2::StringPiece matches[16];
        StrBuf* buf = strbuf_new();
        int pos = 0;
        bool found_match = false;
        while (pos <= (int)s->len) {
            bool matched = rd->re2->Match(input, pos, (int)s->len,
                re2::RE2::UNANCHORED, matches, ngroups);
            if (!matched) break;
            found_match = true;
            int match_start = (int)(matches[0].data() - s->chars);
            int match_len = (int)matches[0].size();
            // append text before match
            if (match_start > pos)
                strbuf_append_str_n(buf, s->chars + pos, match_start - pos);
            if (replacement_is_func) {
                // call function(match, g1, g2, ..., offset, originalString)
                int fn_argc = ngroups + 2;
                Item* fn_args = (Item*)alloca(fn_argc * sizeof(Item));
                for (int i = 0; i < ngroups; i++) {
                    if (matches[i].data()) {
                        fn_args[i] = (Item){.item = s2it(heap_strcpy(
                            (char*)matches[i].data(), (int)matches[i].size()))};
                    } else {
                        fn_args[i] = ItemNull;
                    }
                }
                fn_args[ngroups] = (Item){.item = i2it(match_start)};
                fn_args[ngroups + 1] = str;
                Item result = js_call_function(args[1], ItemNull, fn_args, fn_argc);
                Item result_str = js_to_string(result);
                String* rs = it2s(result_str);
                if (rs) strbuf_append_str_n(buf, rs->chars, rs->len);
            } else {
                // string replacement with $-substitution patterns
                Item repl_str = js_to_string(args[1]);
                String* rs = it2s(repl_str);
                if (rs) {
                    js_apply_replacement(buf, rs->chars, (int)rs->len,
                        s->chars, (int)s->len, match_start, match_len,
                        matches, ngroups);
                }
            }
            pos = match_start + match_len;
            if (match_len == 0) pos++; // avoid infinite loop on zero-length match
            if (!is_replace_all && !(rd->global)) break;
        }
        if (found_match) {
            // append remaining text
            if (pos < (int)s->len)
                strbuf_append_str_n(buf, s->chars + pos, (int)s->len - pos);
            String* result = heap_strcpy(buf->str, buf->length);
            strbuf_free(buf);
            return (Item){.item = s2it(result)};
        }
        strbuf_free(buf);
        return str;
    }

    // string-based replace
    String* search = it2s(js_to_string(args[0]));
    if (!search || search->len == 0) {
        if (!replacement_is_func) {
            // empty search: JS .replace("", repl) inserts at position 0
            String* repl = it2s(js_to_string(args[1]));
            if (!repl || repl->len == 0) return str;
            StrBuf* buf = strbuf_new();
            strbuf_append_str_n(buf, repl->chars, repl->len);
            strbuf_append_str_n(buf, s->chars, s->len);
            String* result_str = heap_strcpy(buf->str, buf->length);
            strbuf_free(buf);
            return (Item){.item = s2it(result_str)};
        }
        return str;
    }
    if (!replacement_is_func) {
        if (is_replace_all) {
            // linear scan replaceAll: find each occurrence and build result
            String* repl = it2s(js_to_string(args[1]));
            if (!repl) return str;
            StrBuf* buf = strbuf_new();
            int pos = 0;
            bool found_any = false;
            while (pos <= (int)s->len - (int)search->len) {
                const char* found = (const char*)memmem(s->chars + pos, s->len - pos, search->chars, search->len);
                if (!found) break;
                found_any = true;
                int match_start = (int)(found - s->chars);
                if (match_start > pos)
                    strbuf_append_str_n(buf, s->chars + pos, match_start - pos);
                js_apply_replacement(buf, repl->chars, (int)repl->len,
                    s->chars, (int)s->len, match_start, (int)search->len, NULL, 0);
                pos = match_start + (int)search->len;
            }
            if (found_any) {
                if (pos < (int)s->len)
                    strbuf_append_str_n(buf, s->chars + pos, (int)s->len - pos);
                String* result_str = heap_strcpy(buf->str, buf->length);
                strbuf_free(buf);
                return (Item){.item = s2it(result_str)};
            }
            strbuf_free(buf);
            return str;
        }
        // JS .replace() with string pattern: replace FIRST occurrence only
        {
            String* repl = it2s(js_to_string(args[1]));
            const char* found = (const char*)memmem(s->chars, s->len, search->chars, search->len);
            if (!found) return str;
            int match_start = (int)(found - s->chars);
            int match_len = (int)search->len;
            StrBuf* buf2 = strbuf_new();
            if (match_start > 0) strbuf_append_str_n(buf2, s->chars, match_start);
            if (repl && repl->len > 0) {
                // v46: process $-substitution patterns
                js_apply_replacement(buf2, repl->chars, (int)repl->len,
                    s->chars, (int)s->len, match_start, match_len, NULL, 0);
            }
            int after = match_start + match_len;
            if (after < (int)s->len) strbuf_append_str_n(buf2, s->chars + after, (int)s->len - after);
            String* result_str = heap_strcpy(buf2->str, buf2->length);
            strbuf_free(buf2);
            return (Item){.item = s2it(result_str)};
        }
    }
    // string search with function callback
    StrBuf* buf = strbuf_new();
    int pos = 0;
    bool found_any = false;
    while (pos <= (int)s->len - (int)search->len) {
        const char* found = (const char*)memmem(s->chars + pos, s->len - pos, search->chars, search->len);
        if (!found) break;
        found_any = true;
        int match_start = (int)(found - s->chars);
        if (match_start > pos)
            strbuf_append_str_n(buf, s->chars + pos, match_start - pos);
        // call function(match, offset, originalString)
        Item fn_args[3];
        fn_args[0] = args[0]; // the matched string == search string
        fn_args[1] = (Item){.item = i2it(match_start)};
        fn_args[2] = str;
        Item result = js_call_function(args[1], ItemNull, fn_args, 3);
        Item result_str = js_to_string(result);
        String* rs = it2s(result_str);
        if (rs) strbuf_append_str_n(buf, rs->chars, rs->len);
        pos = match_start + (int)search->len;
        if (!is_replace_all) break;
    }
    if (found_any) {
        if (pos < (int)s->len)
            strbuf_append_str_n(buf, s->chars + pos, (int)s->len - pos);
        String* result = heap_strcpy(buf->str, buf->length);
        strbuf_free(buf);
        return (Item){.item = s2it(result)};
    }
    strbuf_free(buf);
    return str;
}

// =============================================================================
// String Method Dispatcher
// =============================================================================

extern "C" Item js_string_method(Item str, Item method_name, Item* args, int argc) {
    if (get_type_id(str) != LMD_TYPE_STRING || get_type_id(method_name) != LMD_TYPE_STRING) {
        // v18n: coerce this to string if not already (for .call() with non-string this)
        if (get_type_id(str) != LMD_TYPE_STRING) {
            str = js_to_string(str);
            if (get_type_id(str) != LMD_TYPE_STRING) return ItemNull;
        }
        if (get_type_id(method_name) != LMD_TYPE_STRING) return ItemNull;
    }
    String* method = it2s(method_name);
    if (!method) return ItemNull;

    // match method name and delegate to Lambda fn_* functions
    if (method->len == 7 && strncmp(method->chars, "indexOf", 7) == 0) {
        Item search_val = js_to_string((argc >= 1) ? args[0] : make_js_undefined());
        if (argc < 2) return (Item){.item = i2it(fn_index_of(str, search_val))};
        // indexOf with start position
        int start_pos = (int)js_get_number(args[1]);
        String* s = it2s(str);
        String* sub = it2s(search_val);
        if (!s || !sub) return (Item){.item = i2it(-1)};
        if (start_pos < 0) start_pos = 0;
        // ES spec: empty search string returns min(start_pos, string_length)
        if (sub->len == 0) {
            size_t str_char_len = s->is_ascii ? s->len : str_utf8_count(s->chars, s->len);
            int64_t result = (start_pos <= (int)str_char_len) ? start_pos : (int64_t)str_char_len;
            return (Item){.item = i2it(result)};
        }
        size_t byte_start;
        if (s->is_ascii) {
            byte_start = (size_t)start_pos;
        } else {
            byte_start = str_utf8_char_to_byte(s->chars, s->len, (size_t)start_pos);
        }
        if (byte_start >= s->len) return (Item){.item = i2it(-1)};
        if (s->len - byte_start < sub->len) return (Item){.item = i2it(-1)};
        for (size_t i = byte_start; i <= s->len - sub->len; i++) {
            if (memcmp(s->chars + i, sub->chars, sub->len) == 0) {
                int64_t char_index = s->is_ascii ? (int64_t)i : (int64_t)str_utf8_count(s->chars, i);
                return (Item){.item = i2it(char_index)};
            }
        }
        return (Item){.item = i2it(-1)};
    }
    if (method->len == 11 && strncmp(method->chars, "lastIndexOf", 11) == 0) {
        Item search_val = js_to_string((argc >= 1) ? args[0] : make_js_undefined());
        if (argc < 2) return (Item){.item = i2it(fn_last_index_of(str, search_val))};
        // lastIndexOf with start position - search backwards from position
        int end_pos = (int)js_get_number(args[1]);
        String* s = it2s(str);
        String* sub = it2s(search_val);
        if (!s || !sub) return (Item){.item = i2it(-1)};
        if (end_pos < 0) return (Item){.item = i2it(-1)};
        size_t byte_end;
        if (s->is_ascii) {
            byte_end = (size_t)end_pos;
        } else {
            byte_end = str_utf8_char_to_byte(s->chars, s->len, (size_t)end_pos);
        }
        if (byte_end >= s->len) byte_end = s->len - 1;
        if (sub->len == 0) {
            int64_t char_pos = s->is_ascii ? (int64_t)byte_end : (int64_t)str_utf8_count(s->chars, byte_end);
            return (Item){.item = i2it(char_pos < end_pos ? char_pos : end_pos)};
        }
        size_t max_start = byte_end + sub->len <= s->len ? byte_end : s->len - sub->len;
        for (size_t i = max_start + 1; i > 0; i--) {
            size_t pos = i - 1;
            if (memcmp(s->chars + pos, sub->chars, sub->len) == 0) {
                int64_t char_index = s->is_ascii ? (int64_t)pos : (int64_t)str_utf8_count(s->chars, pos);
                return (Item){.item = i2it(char_index)};
            }
        }
        return (Item){.item = i2it(-1)};
    }
    if (method->len == 8 && strncmp(method->chars, "includes", 8) == 0) {
        if (argc >= 1 && get_type_id(args[0]) == LMD_TYPE_MAP) {
            bool cn_found = false;
            Item cn = js_map_get_fast_ext(args[0].map, "__class_name__", 14, &cn_found);
            if (cn_found && get_type_id(cn) == LMD_TYPE_STRING) {
                String* cs = it2s(cn);
                if (cs && cs->len == 6 && strncmp(cs->chars, "RegExp", 6) == 0)
                    return js_throw_type_error("First argument to String.prototype.includes must not be a regular expression");
            }
        }
        if (argc < 1) return (Item){.item = b2it(false)};
        String* s = it2s(str);
        String* search_str = it2s(js_to_string(args[0]));
        if (!s || !search_str) return (Item){.item = b2it(false)};
        if (search_str->len == 0) return (Item){.item = b2it(true)};
        int pos = 0;
        if (argc >= 2) {
            double dpos = js_get_number(args[1]);
            if (isnan(dpos)) dpos = 0;
            pos = (int)dpos;
            if (pos < 0) pos = 0;
        }
        size_t byte_start;
        if (s->is_ascii) {
            byte_start = (size_t)pos;
        } else {
            byte_start = str_utf8_char_to_byte(s->chars, s->len, (size_t)pos);
        }
        if (byte_start > s->len) return (Item){.item = b2it(false)};
        if (search_str->len == 0) return (Item){.item = b2it(true)};
        if (s->len - byte_start < search_str->len) return (Item){.item = b2it(false)};
        for (size_t i = byte_start; i <= s->len - search_str->len; i++) {
            if (memcmp(s->chars + i, search_str->chars, search_str->len) == 0) {
                return (Item){.item = b2it(true)};
            }
        }
        return (Item){.item = b2it(false)};
    }
    if (method->len == 10 && strncmp(method->chars, "startsWith", 10) == 0) {
        if (argc >= 1 && get_type_id(args[0]) == LMD_TYPE_MAP) {
            bool cn_found = false;
            Item cn = js_map_get_fast_ext(args[0].map, "__class_name__", 14, &cn_found);
            if (cn_found && get_type_id(cn) == LMD_TYPE_STRING) {
                String* cs = it2s(cn);
                if (cs && cs->len == 6 && strncmp(cs->chars, "RegExp", 6) == 0)
                    return js_throw_type_error("First argument to String.prototype.startsWith must not be a regular expression");
            }
        }
        if (argc < 1) return (Item){.item = b2it(false)};
        String* s = it2s(str);
        String* search_str = it2s(js_to_string(args[0]));
        if (!s || !search_str) return (Item){.item = b2it(false)};
        if (search_str->len == 0) return (Item){.item = b2it(true)};
        int pos = 0;
        if (argc >= 2 && get_type_id(args[1]) != LMD_TYPE_UNDEFINED) {
            double dpos = js_get_number(args[1]);
            if (isnan(dpos)) dpos = 0;
            pos = (int)dpos;
            if (pos < 0) pos = 0;
        }
        size_t byte_start;
        if (s->is_ascii) {
            byte_start = (size_t)pos;
        } else {
            byte_start = str_utf8_char_to_byte(s->chars, s->len, (size_t)pos);
        }
        if (byte_start > s->len) return (Item){.item = b2it(false)};
        if (search_str->len == 0) return (Item){.item = b2it(true)};
        if (s->len - byte_start < search_str->len) return (Item){.item = b2it(false)};
        return (Item){.item = b2it(memcmp(s->chars + byte_start, search_str->chars, search_str->len) == 0)};
    }
    if (method->len == 8 && strncmp(method->chars, "endsWith", 8) == 0) {
        if (argc >= 1 && get_type_id(args[0]) == LMD_TYPE_MAP) {
            bool cn_found = false;
            Item cn = js_map_get_fast_ext(args[0].map, "__class_name__", 14, &cn_found);
            if (cn_found && get_type_id(cn) == LMD_TYPE_STRING) {
                String* cs = it2s(cn);
                if (cs && cs->len == 6 && strncmp(cs->chars, "RegExp", 6) == 0)
                    return js_throw_type_error("First argument to String.prototype.endsWith must not be a regular expression");
            }
        }
        if (argc < 1) return (Item){.item = b2it(false)};
        String* s = it2s(str);
        String* search_str = it2s(js_to_string(args[0]));
        if (!s || !search_str) return (Item){.item = b2it(false)};
        size_t str_char_len = s->is_ascii ? s->len : str_utf8_count(s->chars, s->len);
        size_t end_pos = str_char_len;
        if (argc >= 2 && get_type_id(args[1]) != LMD_TYPE_UNDEFINED) {
            double dpos = js_get_number(args[1]);
            if (isnan(dpos)) dpos = 0;
            int ipos = (int)dpos;
            if (ipos < 0) ipos = 0;
            if ((size_t)ipos < end_pos) end_pos = (size_t)ipos;
        }
        if (search_str->len == 0) return (Item){.item = b2it(true)};
        size_t search_char_len = search_str->is_ascii ? search_str->len : str_utf8_count(search_str->chars, search_str->len);
        if (search_char_len > end_pos) return (Item){.item = b2it(false)};
        size_t start_char = end_pos - search_char_len;
        size_t byte_start, byte_end;
        if (s->is_ascii) {
            byte_start = start_char;
            byte_end = end_pos;
        } else {
            byte_start = str_utf8_char_to_byte(s->chars, s->len, start_char);
            byte_end = str_utf8_char_to_byte(s->chars, s->len, end_pos);
        }
        size_t byte_len = byte_end - byte_start;
        if (byte_len != search_str->len) return (Item){.item = b2it(false)};
        return (Item){.item = b2it(memcmp(s->chars + byte_start, search_str->chars, search_str->len) == 0)};
    }
    if (method->len == 4 && strncmp(method->chars, "trim", 4) == 0) {
        String* s = it2s(str);
        if (!s || s->len == 0) return str;
        size_t start = 0, end = s->len;
        while (start < end && js_is_whitespace_at(s->chars, s->len, start))
            start += js_utf8_char_len((unsigned char)s->chars[start]);
        while (end > start) {
            // find start of previous char
            size_t prev = end - 1;
            while (prev > start && ((unsigned char)s->chars[prev] & 0xC0) == 0x80) prev--;
            if (!js_is_whitespace_at(s->chars, s->len, prev)) break;
            end = prev;
        }
        if (start >= end) return (Item){.item = s2it(heap_create_name("", 0))};
        return (Item){.item = s2it(heap_create_name(s->chars + start, end - start))};
    }
    if ((method->len == 9 && strncmp(method->chars, "trimStart", 9) == 0) ||
        (method->len == 8 && strncmp(method->chars, "trimLeft", 8) == 0)) {
        String* s = it2s(str);
        if (!s || s->len == 0) return str;
        size_t start = 0;
        while (start < s->len && js_is_whitespace_at(s->chars, s->len, start))
            start += js_utf8_char_len((unsigned char)s->chars[start]);
        if (start == 0) return str;
        if (start >= s->len) return (Item){.item = s2it(heap_create_name("", 0))};
        return (Item){.item = s2it(heap_create_name(s->chars + start, s->len - start))};
    }
    if ((method->len == 7 && strncmp(method->chars, "trimEnd", 7) == 0) ||
        (method->len == 9 && strncmp(method->chars, "trimRight", 9) == 0)) {
        String* s = it2s(str);
        if (!s || s->len == 0) return str;
        size_t end = s->len;
        while (end > 0) {
            size_t prev = end - 1;
            while (prev > 0 && ((unsigned char)s->chars[prev] & 0xC0) == 0x80) prev--;
            if (!js_is_whitespace_at(s->chars, s->len, prev)) break;
            end = prev;
        }
        if (end == s->len) return str;
        if (end == 0) return (Item){.item = s2it(heap_create_name("", 0))};
        return (Item){.item = s2it(heap_create_name(s->chars, end))};
    }
    if (method->len == 11 && strncmp(method->chars, "toLowerCase", 11) == 0) {
        return fn_lower(str);
    }
    if (method->len == 11 && strncmp(method->chars, "toUpperCase", 11) == 0) {
        return fn_upper(str);
    }
    // String.prototype.normalize(form?) — Unicode normalization (NFC/NFD/NFKC/NFKD)
    if (method->len == 9 && strncmp(method->chars, "normalize", 9) == 0) {
        String* s = it2s(str);
        if (!s || s->len == 0) return str;
        // Determine requested form (default: NFC)
        const char* form = "NFC";
        if (argc > 0 && get_type_id(args[0]) == LMD_TYPE_STRING) {
            String* f = it2s(args[0]);
            if (f && f->len > 0) form = f->chars;
        }
        char* normalized = NULL;
        int norm_len = 0;
        if (strncmp(form, "NFD", 3) == 0 && form[3] == '\0')
            normalized = normalize_utf8proc_nfd(s->chars, s->len, &norm_len);
        else if (strncmp(form, "NFKC", 4) == 0 && form[4] == '\0')
            normalized = normalize_utf8proc_nfkc(s->chars, s->len, &norm_len);
        else if (strncmp(form, "NFKD", 4) == 0 && form[4] == '\0')
            normalized = normalize_utf8proc_nfkd(s->chars, s->len, &norm_len);
        else // NFC (default)
            normalized = normalize_utf8proc_nfc(s->chars, s->len, &norm_len);
        if (!normalized) return str;
        String* result = heap_strcpy(normalized, norm_len);
        free(normalized);
        return (Item){.item = s2it(result)};
    }
    if (method->len == 5 && strncmp(method->chars, "split", 5) == 0) {
        if (argc > 0) {
            Item sep = args[0];
            // check if separator is a regex (Map with __rd property)
            JsRegexData* rd = js_get_regex_data(sep);
            if (rd) {
                // regex split
                String* s = it2s(str);
                if (!s || s->len == 0) {
                    Item result = js_array_new(0);
                    js_array_push(result, (Item){.item = s2it(heap_create_name(""))});
                    return result;
                }
                Item result = js_array_new(0);
                re2::StringPiece input(s->chars, s->len);
                int num_groups = rd->re2->NumberOfCapturingGroups();
                int total_groups = num_groups + 1;
                if (total_groups > 16) total_groups = 16;
                re2::StringPiece match[16];
                int pos = 0;
                while (pos <= (int)s->len) {
                    bool found = rd->re2->Match(input, pos, (int)s->len,
                        re2::RE2::UNANCHORED, match, total_groups);
                    if (!found || match[0].size() == 0) break;
                    int match_start = (int)(match[0].data() - s->chars);
                    int match_end = match_start + (int)match[0].size();
                    // add substring before match
                    String* part = heap_strcpy(s->chars + pos, match_start - pos);
                    js_array_push(result, (Item){.item = s2it(part)});
                    // add capturing groups
                    for (int g = 1; g < total_groups; g++) {
                        if (match[g].data()) {
                            String* gstr = heap_strcpy((char*)match[g].data(), (int)match[g].size());
                            js_array_push(result, (Item){.item = s2it(gstr)});
                        } else {
                            js_array_push(result, make_js_undefined());
                        }
                    }
                    pos = match_end;
                }
                // add remainder
                String* tail = heap_strcpy(s->chars + pos, (int)s->len - pos);
                js_array_push(result, (Item){.item = s2it(tail)});
                // v20: apply limit parameter
                if (argc >= 2 && get_type_id(args[1]) != LMD_TYPE_UNDEFINED) {
                    int limit = it2i(js_to_number(args[1]));
                    if (limit >= 0 && result.array && result.array->length > limit)
                        result.array->length = limit;
                }
                return result;
            }
        }
        // v18n: non-regex path — coerce separator to string
        // undefined separator: return single-element array with whole string
        Item sep = argc > 0 ? args[0] : make_js_undefined();
        if (sep.item == ITEM_JS_UNDEFINED || get_type_id(sep) == LMD_TYPE_UNDEFINED) {
            Item result = js_array_new(1);
            js_array_set(result, (Item){.item = i2it(0)}, str);
            return result;
        }
        // coerce non-string separators (null, number, boolean) to string
        if (get_type_id(sep) != LMD_TYPE_STRING) {
            sep = js_to_string(sep);
        }
        // v20: check for limit parameter
        int split_limit = -1;
        if (argc >= 2 && get_type_id(args[1]) != LMD_TYPE_UNDEFINED) {
            split_limit = it2i(js_to_number(args[1]));
        }
        Item result = fn_split(str, sep);
        // Clear is_content flag to prevent array flattening in JS context
        if (get_type_id(result) == LMD_TYPE_ARRAY && result.array) {
            result.array->is_content = 0;
            // apply limit
            if (split_limit >= 0 && result.array->length > split_limit)
                result.array->length = split_limit;
        }
        return result;
    }
    if (method->len == 9 && strncmp(method->chars, "substring", 9) == 0) {
        if (argc < 1) return str;
        String* s = it2s(str);
        int64_t slen = s ? js_utf16_len(s->chars, (int)s->len, (bool)s->is_ascii) : 0;
        int64_t start = (int64_t)js_get_number(args[0]);
        int64_t end = argc > 1 ? (int64_t)js_get_number(args[1]) : slen;
        // JS substring: negative indices clamp to 0; swap if start > end
        if (start < 0) start = 0;
        if (end < 0) end = 0;
        if (start > slen) start = slen;
        if (end > slen) end = slen;
        if (start > end) { int64_t tmp = start; start = end; end = tmp; }
        return js_str_substring_utf16(str, start, end);
    }
    if (method->len == 6 && strncmp(method->chars, "substr", 6) == 0) {
        // substr(start, length) — legacy method, uses UTF-16 unit indices
        if (argc < 1) return str;
        String* s = it2s(str);
        int64_t slen = s ? js_utf16_len(s->chars, (int)s->len, (bool)s->is_ascii) : 0;
        int64_t start = (int64_t)js_get_number(args[0]);
        if (start < 0) { start = slen + start; if (start < 0) start = 0; }
        int64_t length;
        if (argc > 1) {
            length = (int64_t)js_get_number(args[1]);
            if (length < 0) length = 0;
        } else {
            length = slen - start;
        }
        if (start >= slen || length <= 0) return (Item){.item = s2it(heap_create_name(""))};
        int64_t end = start + length;
        if (end > slen) end = slen;
        return js_str_substring_utf16(str, start, end);
    }
    if (method->len == 5 && strncmp(method->chars, "slice", 5) == 0) {
        if (argc < 1) return str;
        String* s = it2s(str);
        int64_t slen = s ? js_utf16_len(s->chars, (int)s->len, (bool)s->is_ascii) : 0;
        int64_t start = (int64_t)js_get_number(args[0]);
        int64_t end = argc > 1 ? (int64_t)js_get_number(args[1]) : slen;
        // slice: negative indices count from end
        if (start < 0) { start = slen + start; if (start < 0) start = 0; }
        if (end < 0) { end = slen + end; if (end < 0) end = 0; }
        if (start > slen) start = slen;
        if (end > slen) end = slen;
        if (start >= end) return (Item){.item = s2it(heap_create_name(""))};
        return js_str_substring_utf16(str, start, end);
    }
    if (method->len == 7 && strncmp(method->chars, "replace", 7) == 0) {
        if (argc < 2) return str;
        return js_string_replace_impl(str, args, argc, false);
    }
    if (method->len == 6 && strncmp(method->chars, "charAt", 6) == 0) {
        String* s = it2s(str);
        if (!s || s->len == 0) return (Item){.item = s2it(heap_create_name(""))};
        int64_t idx = (argc >= 1) ? (int64_t)js_get_number(args[0]) : 0;
        if (idx < 0) return (Item){.item = s2it(heap_create_name(""))};
        // charAt uses UTF-16 unit index; extract the code unit as a 1-char string
        // (for BMP chars this is the char itself; for non-BMP this returns a surrogate half-char)
        return js_str_substring_utf16(str, idx, idx + 1);
    }
    if (method->len == 10 && strncmp(method->chars, "charCodeAt", 10) == 0) {
        String* s = it2s(str);
        if (!s || s->len == 0) return js_make_number(NAN);
        int target_idx = (argc >= 1) ? (int)js_get_number(args[0]) : 0;
        if (target_idx < 0) return js_make_number(NAN);
        // walk UTF-8 bytes, counting UTF-16 code units
        int utf16_idx = 0;
        int byte_pos = 0;
        while (byte_pos < (int)s->len) {
            unsigned char b = (unsigned char)s->chars[byte_pos];
            uint32_t cp = 0;
            int bytes = 1;
            if (b < 0x80) { cp = b; bytes = 1; }
            else if ((b & 0xE0) == 0xC0) { cp = b & 0x1F; bytes = 2; }
            else if ((b & 0xF0) == 0xE0) { cp = b & 0x0F; bytes = 3; }
            else if ((b & 0xF8) == 0xF0) { cp = b & 0x07; bytes = 4; }
            for (int i = 1; i < bytes && byte_pos + i < (int)s->len; i++)
                cp = (cp << 6) | ((unsigned char)s->chars[byte_pos + i] & 0x3F);
            if (cp >= 0x10000) {
                // surrogate pair: 2 UTF-16 code units
                if (utf16_idx == target_idx) {
                    uint32_t hi = 0xD800 + ((cp - 0x10000) >> 10);
                    return (Item){.item = i2it((int64_t)hi)};
                }
                if (utf16_idx + 1 == target_idx) {
                    uint32_t lo = 0xDC00 + ((cp - 0x10000) & 0x3FF);
                    return (Item){.item = i2it((int64_t)lo)};
                }
                utf16_idx += 2;
            } else {
                if (utf16_idx == target_idx)
                    return (Item){.item = i2it((int64_t)cp)};
                utf16_idx++;
            }
            byte_pos += bytes;
        }
        return js_make_number(NAN);
    }
    if (method->len == 11 && strncmp(method->chars, "codePointAt", 11) == 0) {
        if (argc < 1) return ItemNull;
        String* s = it2s(str);
        if (!s || s->len == 0) return ItemNull;
        int target_idx = (int)js_get_number(args[0]);
        if (target_idx < 0) return ItemNull;
        // walk UTF-8, counting UTF-16 code units to find the right position
        int utf16_idx = 0;
        int byte_pos = 0;
        while (byte_pos < (int)s->len) {
            unsigned char b = (unsigned char)s->chars[byte_pos];
            uint32_t cp = 0;
            int bytes = 1;
            if (b < 0x80) { cp = b; bytes = 1; }
            else if ((b & 0xE0) == 0xC0) { cp = b & 0x1F; bytes = 2; }
            else if ((b & 0xF0) == 0xE0) { cp = b & 0x0F; bytes = 3; }
            else if ((b & 0xF8) == 0xF0) { cp = b & 0x07; bytes = 4; }
            for (int i = 1; i < bytes && byte_pos + i < (int)s->len; i++)
                cp = (cp << 6) | ((unsigned char)s->chars[byte_pos + i] & 0x3F);
            if (utf16_idx == target_idx)
                return (Item){.item = i2it((int64_t)cp)};
            utf16_idx += (cp >= 0x10000) ? 2 : 1;
            byte_pos += bytes;
        }
        return ItemNull;
    }
    if (method->len == 6 && strncmp(method->chars, "concat", 6) == 0) {
        Item result = str;
        for (int i = 0; i < argc; i++) {
            Item arg_str = js_to_string(args[i]);
            result = fn_join(result, arg_str);
        }
        return result;
    }
    if (method->len == 6 && strncmp(method->chars, "repeat", 6) == 0) {
        if (argc < 1) return (Item){.item = s2it(heap_create_name(""))};
        double n = js_get_number(args[0]);
        // spec: ToIntegerOrInfinity first — NaN becomes 0, then range check
        if (n != n) n = 0;  // NaN → 0 (covers undefined, null, NaN)
        n = trunc(n);        // truncate toward zero (ToIntegerOrInfinity)
        if (n < 0 || n == (1.0/0.0)) {  // negative or +Infinity
            return js_throw_range_error("Invalid count value");
        }
        int count = (int)n;
        String* s = it2s(str);
        if (count == 0 || !s || s->len == 0) return (Item){.item = s2it(heap_create_name(""))};
        // build repeated string
        StrBuf* buf = strbuf_new();
        for (int i = 0; i < count; i++) {
            strbuf_append_str_n(buf, s->chars, s->len);
        }
        String* result = heap_strcpy(buf->str, buf->length);
        strbuf_free(buf);
        return (Item){.item = s2it(result)};
    }
    // replaceAll — redirect to unified replace handler
    if (method->len == 10 && strncmp(method->chars, "replaceAll", 10) == 0) {
        if (argc < 2) return str;
        return js_string_replace_impl(str, args, argc, true);
    }
    // padStart(targetLength, padString?)
    if (method->len == 8 && strncmp(method->chars, "padStart", 8) == 0) {
        if (argc < 1) return str;
        String* s = it2s(str);
        if (!s) return str;
        int target = (int)js_get_number(args[0]);
        if ((int)s->len >= target) return str;
        String* pad = NULL;
        if (argc > 1 && get_type_id(args[1]) != LMD_TYPE_UNDEFINED)
            pad = it2s(js_to_string(args[1]));
        const char* pad_chars = pad ? pad->chars : " ";
        int pad_len = pad ? (int)pad->len : 1;
        if (pad_len == 0) return str;
        int needed = target - (int)s->len;
        StrBuf* buf = strbuf_new();
        for (int i = 0; i < needed; i++) {
            strbuf_append_char(buf, pad_chars[i % pad_len]);
        }
        strbuf_append_str_n(buf, s->chars, s->len);
        String* result = heap_strcpy(buf->str, buf->length);
        strbuf_free(buf);
        return (Item){.item = s2it(result)};
    }
    // padEnd(targetLength, padString?)
    if (method->len == 6 && strncmp(method->chars, "padEnd", 6) == 0) {
        if (argc < 1) return str;
        String* s = it2s(str);
        if (!s) return str;
        int target = (int)js_get_number(args[0]);
        if ((int)s->len >= target) return str;
        String* pad = NULL;
        if (argc > 1 && get_type_id(args[1]) != LMD_TYPE_UNDEFINED)
            pad = it2s(js_to_string(args[1]));
        const char* pad_chars = pad ? pad->chars : " ";
        int pad_len = pad ? (int)pad->len : 1;
        if (pad_len == 0) return str;
        int needed = target - (int)s->len;
        StrBuf* buf = strbuf_new();
        strbuf_append_str_n(buf, s->chars, s->len);
        for (int i = 0; i < needed; i++) {
            strbuf_append_char(buf, pad_chars[i % pad_len]);
        }
        String* result = heap_strcpy(buf->str, buf->length);
        strbuf_free(buf);
        return (Item){.item = s2it(result)};
    }
    // at(index) — supports negative indexing
    if (method->len == 2 && strncmp(method->chars, "at", 2) == 0) {
        String* s = it2s(str);
        if (!s || s->len == 0) return make_js_undefined();
        int idx = (argc < 1) ? 0 : (int)js_get_number(args[0]);
        if (idx < 0) idx = (int)s->len + idx;
        if (idx < 0 || idx >= (int)s->len) return make_js_undefined();
        String* ch = heap_strcpy(&s->chars[idx], 1);
        return (Item){.item = s2it(ch)};
    }
    // search(pattern) — return index of first match
    if (method->len == 6 && strncmp(method->chars, "search", 6) == 0) {
        // No args or undefined → search for empty regex, which matches at 0
        if (argc < 1 || args[0].item == ITEM_JS_UNDEFINED) return (Item){.item = i2it(0)};
        // If arg is a regex object, use regex match to find index
        if (get_type_id(args[0]) == LMD_TYPE_MAP) {
            JsRegexData* rd = js_get_regex_data(args[0]);
            if (rd) {
                String* s = it2s(str);
                if (!s) return (Item){.item = i2it(-1)};
                re2::StringPiece match;
                if (rd->re2->Match(re2::StringPiece(s->chars, s->len), 0, (int)s->len,
                                   RE2::UNANCHORED, &match, 1)) {
                    return (Item){.item = i2it((int)(match.data() - s->chars))};
                }
                return (Item){.item = i2it(-1)};
            }
        }
        // delegate to indexOf for string patterns
        return (Item){.item = i2it(fn_index_of(str, args[0]))};
    }
    // toString
    if (method->len == 8 && strncmp(method->chars, "toString", 8) == 0) {
        return str;
    }

    // match(regex) — delegates to regex exec
    if (method->len == 5 && strncmp(method->chars, "match", 5) == 0) {
        // No args or undefined → match empty regex, returns [""] with index 0
        if (argc < 1 || args[0].item == ITEM_JS_UNDEFINED) {
            Item result = js_array_new(1);
            Item empty_str = (Item){.item = s2it(heap_create_name("", 0))};
            js_array_set_int(result, 0, empty_str);
            // Set index = 0 and input = str
            Item idx_key = (Item){.item = s2it(heap_create_name("index", 5))};
            Item idx_val = (Item){.item = i2it(0)};
            js_property_set(result, idx_key, idx_val);
            String* s = it2s(str);
            if (s) {
                Item input_key = (Item){.item = s2it(heap_create_name("input", 5))};
                js_property_set(result, input_key, str);
            }
            return result;
        }
        // If arg is a regex object, use js_regex_exec
        if (get_type_id(args[0]) == LMD_TYPE_MAP) {
            JsRegexData* rd = js_get_regex_data(args[0]);
            if (rd) {
                if (!rd->global) {
                    return js_regex_exec(args[0], str);
                }
                // Global: collect all matches into array
                String* s = it2s(str);
                if (!s) return ItemNull;
                Item result = js_array_new(0);
                int offset = 0;
                int num_groups = rd->re2->NumberOfCapturingGroups() + 1;
                if (num_groups > 16) num_groups = 16;
                re2::StringPiece matches[16];
                while (offset < (int)s->len) {
                    bool matched = rd->re2->Match(
                        re2::StringPiece(s->chars, s->len), offset, (int)s->len,
                        re2::RE2::UNANCHORED, matches, num_groups);
                    if (!matched) break;
                    int mlen = (int)matches[0].size();
                    Item ms = (Item){.item = s2it(heap_strcpy((char*)matches[0].data(), mlen))};
                    js_array_push_item_direct(result.array, ms);
                    int advance = (int)(matches[0].data() - s->chars) + mlen;
                    if (advance <= offset) advance = offset + 1;
                    offset = advance;
                }
                return result;
            }
        }
        // If arg is a string, treat as literal pattern
        return (Item){.item = i2it(fn_index_of(str, args[0]))};
    }
    // matchAll(regex) — returns an iterator of all match results
    if (method->len == 8 && strncmp(method->chars, "matchAll", 8) == 0) {
        Item matches_arr = js_array_new(0);
        if (argc >= 1 && get_type_id(args[0]) == LMD_TYPE_MAP) {
            JsRegexData* rd = js_get_regex_data(args[0]);
            if (rd) {
                String* s = it2s(str);
                if (s) {
                    int offset = 0;
                    int num_groups = rd->re2->NumberOfCapturingGroups() + 1;
                    if (num_groups > 16) num_groups = 16;
                    re2::StringPiece matches[16];
                    while (offset < (int)s->len) {
                        bool matched = rd->re2->Match(
                            re2::StringPiece(s->chars, s->len), offset, (int)s->len,
                            re2::RE2::UNANCHORED, matches, num_groups);
                        if (!matched) break;
                        Item match_obj = js_new_object();
                        for (int i = 0; i < num_groups; i++) {
                            if (matches[i].data()) {
                                Item ms = (Item){.item = s2it(heap_strcpy((char*)matches[i].data(), (int)matches[i].size()))};
                                char buf[24];
                                snprintf(buf, sizeof(buf), "%d", i);
                                Item key = (Item){.item = s2it(heap_create_name(buf))};
                                js_property_set(match_obj, key, ms);
                            }
                        }
                        int match_index = (int)(matches[0].data() - s->chars);
                        Item index_key = (Item){.item = s2it(heap_create_name("index", 5))};
                        js_property_set(match_obj, index_key, (Item){.item = i2it(match_index)});
                        Item input_key = (Item){.item = s2it(heap_create_name("input", 5))};
                        js_property_set(match_obj, input_key, str);
                        Item groups_key = (Item){.item = s2it(heap_create_name("groups", 6))};
                        js_property_set(match_obj, groups_key, (Item){.item = ITEM_JS_UNDEFINED});
                        Item length_key = (Item){.item = s2it(heap_create_name("length", 6))};
                        js_property_set(match_obj, length_key, (Item){.item = i2it(num_groups)});
                        js_array_push_item_direct(matches_arr.array, match_obj);
                        int advance = match_index + (int)matches[0].size();
                        if (advance <= offset) advance = offset + 1;
                        offset = advance;
                    }
                }
            }
        }
        return matches_arr;
    }
    if (method->len == 7 && strncmp(method->chars, "valueOf", 7) == 0) {
        return str;
    }
    if (method->len == 8 && strncmp(method->chars, "toString", 8) == 0) {
        return str;
    }
    if (method->len == 14 && strncmp(method->chars, "toLocaleString", 14) == 0) {
        return str;
    }

    log_debug("js_string_method: unknown method '%.*s'", (int)method->len, method->chars);
    return ItemNull;
}

// =============================================================================
// Array Method Dispatcher
// =============================================================================

// Helper: throw TypeError for non-callable callback in array methods
static Item js_throw_not_callable(const char* method_name) {
    Item type_name = (Item){.item = s2it(heap_create_name("TypeError"))};
    char msg[128];
    snprintf(msg, sizeof(msg), "%s is not a function", method_name ? method_name : "callback");
    Item msg_item = (Item){.item = s2it(heap_create_name(msg, strlen(msg)))};
    Item error = js_new_error_with_name(type_name, msg_item);
    js_throw_value(error);
    return ItemNull;
}

// v20: Helper: throw RangeError
extern "C" Item js_throw_range_error(const char* message) {
    Item type_name = (Item){.item = s2it(heap_create_name("RangeError"))};
    Item msg_item = (Item){.item = s2it(heap_create_name(message, strlen(message)))};
    Item error = js_new_error_with_name(type_name, msg_item);
    js_throw_value(error);
    return ItemNull;
}

extern "C" Item js_throw_type_error(const char* message) {
    Item type_name = (Item){.item = s2it(heap_create_name("TypeError"))};
    Item msg_item = (Item){.item = s2it(heap_create_name(message, strlen(message)))};
    Item error = js_new_error_with_name(type_name, msg_item);
    js_throw_value(error);
    return ItemNull;
}

// v20: Helper: throw SyntaxError (for early errors detected during transpilation)
extern "C" void js_throw_syntax_error(Item message) {
    Item type_name = (Item){.item = s2it(heap_create_name("SyntaxError"))};
    Item error = js_new_error_with_name(type_name, message);
    js_throw_value(error);
}

// helper: read array element, checking for accessor properties (getters via defineProperty)
static inline Item js_array_element(Item arr_item, int idx) {
    Array* arr = arr_item.array;
    if (arr->extra != 0) {
        char gk[64];
        snprintf(gk, sizeof(gk), "__get_%d", idx);
        Map* props = (Map*)(uintptr_t)arr->extra;
        bool gk_found = false;
        Item getter = js_map_get_fast(props, gk, (int)strlen(gk), &gk_found);
        if (gk_found && get_type_id(getter) == LMD_TYPE_FUNC) {
            return js_call_function(getter, arr_item, NULL, 0);
        }
    }
    // v25: check for deleted sentinel (array hole) — return undefined
    if (arr->items[idx].item == JS_DELETED_SENTINEL_VAL) {
        return make_js_undefined();
    }
    return arr->items[idx];
}

extern "C" Item js_array_method(Item arr, Item method_name, Item* args, int argc) {
    if (get_type_id(method_name) != LMD_TYPE_STRING) return ItemNull;
    String* method = it2s(method_name);
    if (!method) return ItemNull;
    TypeId arr_type = get_type_id(arr);
    // v18m: use original this for callback's third arg (supports .call() on non-arrays)
    Item cb_this = js_array_method_real_this.item ? js_array_method_real_this : arr;

    // push - mutating
    if (method->len == 4 && strncmp(method->chars, "push", 4) == 0) {
        if (arr_type != LMD_TYPE_ARRAY) return (Item){.item = i2it(0)};
        for (int i = 0; i < argc; i++) {
            js_array_push_item_direct(arr.array, args[i]);
        }
        return (Item){.item = i2it(arr.array->length)};
    }
    // pop - mutating
    if (method->len == 3 && strncmp(method->chars, "pop", 3) == 0) {
        if (arr_type != LMD_TYPE_ARRAY || arr.array->length == 0) return make_js_undefined();
        Item last = arr.array->items[arr.array->length - 1];
        arr.array->length--;
        return last;
    }
    // length property (handled as method for convenience)
    if (method->len == 6 && strncmp(method->chars, "length", 6) == 0) {
        return (Item){.item = i2it(fn_len(arr))};
    }
    // indexOf
    if (method->len == 7 && strncmp(method->chars, "indexOf", 7) == 0) {
        if (arr_type != LMD_TYPE_ARRAY) return (Item){.item = i2it(-1)};
        Item search_val = (argc >= 1) ? args[0] : make_js_undefined();
        Array* a = arr.array;
        int start = (argc >= 2) ? (int)js_get_number(args[1]) : 0;
        // v24: ES spec - negative fromIndex means length + fromIndex
        if (start < 0) { start = a->length + start; if (start < 0) start = 0; }
        for (int i = start; i < a->length; i++) {
            // v25: skip holes (deleted elements) per ES spec
            if (a->items[i].item == JS_DELETED_SENTINEL_VAL) continue;
            if (it2b(js_strict_equal(js_array_element(arr, i), search_val))) return (Item){.item = i2it(i)};
        }
        return (Item){.item = i2it(-1)};
    }
    // item(index) - NodeList/HTMLCollection style index access
    if (method->len == 4 && strncmp(method->chars, "item", 4) == 0) {
        if (argc < 1 || arr_type != LMD_TYPE_ARRAY) return ItemNull;
        int idx = (int)js_get_number(args[0]);
        Array* a = arr.array;
        if (idx >= 0 && idx < a->length) return a->items[idx];
        return ItemNull;
    }
    // includes
    if (method->len == 8 && strncmp(method->chars, "includes", 8) == 0) {
        if (arr_type != LMD_TYPE_ARRAY) return (Item){.item = b2it(false)};
        Item search_val = (argc >= 1) ? args[0] : make_js_undefined();
        Array* a = arr.array;
        // v24: Support fromIndex argument (ES spec)
        int from = (argc >= 2) ? (int)js_get_number(args[1]) : 0;
        if (from < 0) { from = a->length + from; if (from < 0) from = 0; }
        for (int i = from; i < a->length; i++) {
            // includes uses SameValueZero (NaN === NaN, +0 === -0)
            Item elem = js_array_element(arr, i);
            if (it2b(js_strict_equal(elem, search_val))) return (Item){.item = b2it(true)};
            // NaN === NaN for includes
            if (get_type_id(elem) == LMD_TYPE_FLOAT && get_type_id(search_val) == LMD_TYPE_FLOAT) {
                double d1 = it2d(elem), d2 = it2d(search_val);
                if (d1 != d1 && d2 != d2) return (Item){.item = b2it(true)};
            }
        }
        return (Item){.item = b2it(false)};
    }
    // join - converts all elements to strings and joins them
    if (method->len == 4 && strncmp(method->chars, "join", 4) == 0) {
        Item sep = argc > 0 ? args[0] : (Item){.item = s2it(heap_create_name(","))};
        String* sep_str = it2s(sep);
        const char* sep_chars = sep_str ? sep_str->chars : ",";
        size_t sep_len = sep_str ? sep_str->len : 1;

        Array* a = arr.array;
        StrBuf* buf = strbuf_new();
        for (int i = 0; i < a->length; i++) {
            if (i > 0 && sep_len > 0) {
                strbuf_append_str_n(buf, sep_chars, sep_len);
            }
            // v25: skip deleted elements (holes) — treated as empty like undefined/null  
            if (a->items[i].item == JS_DELETED_SENTINEL_VAL) continue;
            TypeId etype = get_type_id(a->items[i]);
            if (etype == LMD_TYPE_NULL || etype == LMD_TYPE_UNDEFINED) continue;
            Item elem_str = js_to_string(a->items[i]);
            String* s = it2s(elem_str);
            if (s && s->len > 0) {
                strbuf_append_str_n(buf, s->chars, s->len);
            }
        }
        String* result = heap_strcpy(buf->str, buf->length);
        strbuf_free(buf);
        return (Item){.item = s2it(result)};
    }
    // reverse - in-place reversal (JS spec: mutates and returns same array)
    if (method->len == 7 && strncmp(method->chars, "reverse", 7) == 0) {
        if (arr_type != LMD_TYPE_ARRAY) return arr;
        Array* a = arr.array;
        for (int i = 0, j = a->length - 1; i < j; i++, j--) {
            Item tmp = a->items[i];
            a->items[i] = a->items[j];
            a->items[j] = tmp;
        }
        return arr;
    }
    // slice - returns new Array with elements from start to end
    if (method->len == 5 && strncmp(method->chars, "slice", 5) == 0) {
        if (arr_type != LMD_TYPE_ARRAY) return arr;
        Array* src = arr.array;
        int start = argc > 0 ? (int)js_get_number(args[0]) : 0;
        int end = argc > 1 ? (int)js_get_number(args[1]) : src->length;
        if (start < 0) start = src->length + start;
        if (end < 0) end = src->length + end;
        if (start < 0) start = 0;
        if (end > src->length) end = src->length;
        if (start >= end) return js_array_new(0);
        int count = end - start;
        Item result = js_array_new(count);
        Array* dst = result.array;
        for (int i = 0; i < count; i++) {
            dst->items[i] = src->items[start + i];
        }
        dst->length = count;
        return result;
    }
    // concat - returns new array that is the concatenation
    if (method->len == 6 && strncmp(method->chars, "concat", 6) == 0) {
        if (arr_type != LMD_TYPE_ARRAY) return arr;
        Array* src = arr.array;

        // helper lambda to check if an item should be spread in concat
        // Arrays: spreadable by default unless Symbol.isConcatSpreadable === false
        // Others: not spreadable unless Symbol.isConcatSpreadable === true
        auto is_concat_spreadable = [](Item item) -> bool {
            TypeId t = get_type_id(item);
            bool is_array = (t == LMD_TYPE_ARRAY);
            // Check Symbol.isConcatSpreadable (stored as __sym_10 or similar)
            // Symbol.isConcatSpreadable has well-known ID in JS_SYMBOL_BASE+10 area
            // Look for it in companion map or as a property
            if (t == LMD_TYPE_ARRAY && item.array->extra != 0) {
                Map* props = (Map*)(uintptr_t)item.array->extra;
                bool found = false;
                Item val = js_map_get_fast_ext(props, "__sym_12", 8, &found);
                if (found) return js_is_truthy(val);
            } else if (t == LMD_TYPE_MAP) {
                bool found = false;
                Item val = js_map_get_fast_ext(item.map, "__sym_12", 8, &found);
                if (found) return js_is_truthy(val);
            }
            return is_array;
        };

        // calculate total length
        int total = src->length;
        for (int i = 0; i < argc; i++) {
            if (is_concat_spreadable(args[i])) {
                if (get_type_id(args[i]) == LMD_TYPE_ARRAY)
                    total += args[i].array->length;
                else if (get_type_id(args[i]) == LMD_TYPE_MAP) {
                    Item len_key = (Item){.item = s2it(heap_create_name("length", 6))};
                    Item len_val = js_property_get(args[i], len_key);
                    total += (int)it2i(len_val);
                } else
                    total += 1;
            } else {
                total++;
            }
        }
        Item result = js_array_new(total);
        Array* dst = result.array;
        int pos = 0;
        for (int i = 0; i < src->length; i++) {
            dst->items[pos++] = src->items[i];
        }
        for (int i = 0; i < argc; i++) {
            if (is_concat_spreadable(args[i])) {
                if (get_type_id(args[i]) == LMD_TYPE_ARRAY) {
                    Array* other = args[i].array;
                    for (int j = 0; j < other->length; j++) {
                        dst->items[pos++] = other->items[j];
                    }
                } else if (get_type_id(args[i]) == LMD_TYPE_MAP) {
                    // Spreadable object: iterate by numeric indices using 'length' property
                    Item len_key = (Item){.item = s2it(heap_create_name("length", 6))};
                    Item len_val = js_property_get(args[i], len_key);
                    int64_t len = it2i(len_val);
                    for (int64_t j = 0; j < len; j++) {
                        char buf[24];
                        snprintf(buf, sizeof(buf), "%lld", (long long)j);
                        Item idx_key = (Item){.item = s2it(heap_create_name(buf))};
                        Item elem = js_property_get(args[i], idx_key);
                        if (pos < total) dst->items[pos++] = elem;
                        else { js_array_push_item_direct(dst, elem); pos++; }
                    }
                } else {
                    dst->items[pos++] = args[i];
                }
            } else {
                dst->items[pos++] = args[i];
            }
        }
        dst->length = pos;
        return result;
    }
    // map - uses callback as first arg (must be a JsFunction)
    if (method->len == 3 && strncmp(method->chars, "map", 3) == 0) {
        if (arr_type != LMD_TYPE_ARRAY) return arr;
        if (argc < 1 || get_type_id(args[0]) != LMD_TYPE_FUNC) return js_throw_not_callable("callback");
        Item callback = args[0];
        Array* src = arr.array;
        Item result = js_array_new(src->length);
        Array* dst = result.array;
        JsFunction* fn = (JsFunction*)callback.function;
        // v20: thisArg support
        Item prev_this = js_current_this;
        if (argc >= 2 && get_type_id(args[1]) != LMD_TYPE_UNDEFINED) js_current_this = args[1];
        int len = src->length;  // spec: capture length before loop
        for (int i = 0; i < len; i++) {
            // v25: skip holes (deleted elements) per ES spec — preserve holes in result
            if (i >= src->length || src->items[i].item == JS_DELETED_SENTINEL_VAL) {
                dst->items[i] = (Item){.item = JS_DELETED_SENTINEL_VAL};
                continue;
            }
            Item cb_args[3] = { js_array_element(arr, i), (Item){.item = i2it(i)}, cb_this };
            Item mapped = js_invoke_fn(fn, cb_args, fn->param_count >= 3 ? 3 : (fn->param_count >= 2 ? 2 : 1));
            // directly assign to preserve arrays (avoid list_push flattening)
            dst->items[i] = mapped;
        }
        js_current_this = prev_this;
        return result;
    }
    // filter
    if (method->len == 6 && strncmp(method->chars, "filter", 6) == 0) {
        if (arr_type != LMD_TYPE_ARRAY) return arr;
        if (argc < 1 || get_type_id(args[0]) != LMD_TYPE_FUNC) return js_throw_not_callable("callback");
        Item callback = args[0];
        Array* src = arr.array;
        Item result = js_array_new(0);
        Array* dst = result.array;
        JsFunction* fn = (JsFunction*)callback.function;
        // v20: thisArg support
        Item prev_this = js_current_this;
        if (argc >= 2 && get_type_id(args[1]) != LMD_TYPE_UNDEFINED) js_current_this = args[1];
        int len = src->length;  // spec: capture length before loop
        for (int i = 0; i < len; i++) {
            // v25: skip holes (deleted elements) per ES spec
            if (i >= src->length || src->items[i].item == JS_DELETED_SENTINEL_VAL) continue;
            Item elem = js_array_element(arr, i);
            Item cb_args[3] = { elem, (Item){.item = i2it(i)}, cb_this };
            Item pred = js_invoke_fn(fn, cb_args, fn->param_count >= 3 ? 3 : (fn->param_count >= 2 ? 2 : 1));
            if (js_is_truthy(pred)) {
                js_array_push_item_direct(dst, elem);
            }
        }
        js_current_this = prev_this;
        return result;
    }
    // reduce
    if (method->len == 6 && strncmp(method->chars, "reduce", 6) == 0) {
        if (arr_type != LMD_TYPE_ARRAY) return ItemNull;
        if (argc < 1 || get_type_id(args[0]) != LMD_TYPE_FUNC) return js_throw_not_callable("callback");
        Item callback = args[0];
        Array* src = arr.array;
        JsFunction* fn = (JsFunction*)callback.function;
        Item accumulator;
        int start_idx;
        if (argc >= 2) {
            accumulator = args[1];
            start_idx = 0;
        } else {
            if (src->length == 0) {
                Item type_name = (Item){.item = s2it(heap_create_name("TypeError"))};
                Item msg_item = (Item){.item = s2it(heap_create_name("Reduce of empty array with no initial value"))};
                Item error = js_new_error_with_name(type_name, msg_item);
                js_throw_value(error);
                return ItemNull;
            }
            accumulator = js_array_element(arr, 0);
            start_idx = 1;
        }
        int len = src->length;  // spec: capture length before loop
        for (int i = start_idx; i < len; i++) {
            // v25: skip holes (deleted elements) per ES spec
            if (i >= src->length || src->items[i].item == JS_DELETED_SENTINEL_VAL) continue;
            Item cb_args[4] = { accumulator, js_array_element(arr, i), (Item){.item = i2it(i)}, cb_this };
            accumulator = js_invoke_fn(fn, cb_args, fn->param_count >= 4 ? 4 : (fn->param_count >= 3 ? 3 : 2));
        }
        return accumulator;
    }
    // forEach
    if (method->len == 7 && strncmp(method->chars, "forEach", 7) == 0) {
        if (arr_type != LMD_TYPE_ARRAY) return ItemNull;
        if (argc < 1 || get_type_id(args[0]) != LMD_TYPE_FUNC) return js_throw_not_callable("callback");
        Item callback = args[0];
        Array* src = arr.array;
        JsFunction* fn = (JsFunction*)callback.function;
        // v20: thisArg support
        Item prev_this = js_current_this;
        if (argc >= 2 && get_type_id(args[1]) != LMD_TYPE_UNDEFINED) js_current_this = args[1];
        int len = src->length;  // spec: capture length before loop
        for (int i = 0; i < len; i++) {
            // v25: skip holes (deleted elements) per ES spec
            if (i >= src->length || src->items[i].item == JS_DELETED_SENTINEL_VAL) continue;
            Item elem = js_array_element(arr, i);
            Item cb_args[3] = { elem, (Item){.item = i2it(i)}, cb_this };
            js_invoke_fn(fn, cb_args, fn->param_count >= 3 ? 3 : (fn->param_count >= 2 ? 2 : 1));
        }
        js_current_this = prev_this;
        return ItemNull;
    }
    // find
    if (method->len == 4 && strncmp(method->chars, "find", 4) == 0) {
        if (arr_type != LMD_TYPE_ARRAY) return make_js_undefined();
        if (argc < 1 || get_type_id(args[0]) != LMD_TYPE_FUNC) return js_throw_not_callable("callback");
        Item callback = args[0];
        Array* src = arr.array;
        int len = src->length;  // spec: capture len before loop
        JsFunction* fn = (JsFunction*)callback.function;
        // v20: thisArg support
        Item prev_this = js_current_this;
        if (argc >= 2 && get_type_id(args[1]) != LMD_TYPE_UNDEFINED) js_current_this = args[1];
        for (int i = 0; i < len; i++) {
            Item elem = js_array_element(arr, i);
            Item cb_args[3] = { elem, (Item){.item = i2it(i)}, cb_this };
            Item pred = js_invoke_fn(fn, cb_args, fn->param_count >= 3 ? 3 : (fn->param_count >= 2 ? 2 : 1));
            if (js_is_truthy(pred)) { js_current_this = prev_this; return elem; }
        }
        js_current_this = prev_this;
        return make_js_undefined();
    }
    // findIndex
    if (method->len == 9 && strncmp(method->chars, "findIndex", 9) == 0) {
        if (arr_type != LMD_TYPE_ARRAY) return (Item){.item = i2it(-1)};
        if (argc < 1 || get_type_id(args[0]) != LMD_TYPE_FUNC) return js_throw_not_callable("callback");
        Item callback = args[0];
        Array* src = arr.array;
        int len = src->length;  // spec: capture len before loop
        JsFunction* fn = (JsFunction*)callback.function;
        // v20: thisArg support
        Item prev_this = js_current_this;
        if (argc >= 2 && get_type_id(args[1]) != LMD_TYPE_UNDEFINED) js_current_this = args[1];
        for (int i = 0; i < len; i++) {
            Item cb_args[3] = { js_array_element(arr, i), (Item){.item = i2it(i)}, cb_this };
            Item pred = js_invoke_fn(fn, cb_args, fn->param_count >= 3 ? 3 : (fn->param_count >= 2 ? 2 : 1));
            if (js_is_truthy(pred)) { js_current_this = prev_this; return (Item){.item = i2it(i)}; }
        }
        js_current_this = prev_this;
        return (Item){.item = i2it(-1)};
    }
    // findLast
    if (method->len == 8 && strncmp(method->chars, "findLast", 8) == 0) {
        if (arr_type != LMD_TYPE_ARRAY) return make_js_undefined();
        if (argc < 1 || get_type_id(args[0]) != LMD_TYPE_FUNC) return js_throw_not_callable("callback");
        Item callback = args[0];
        Array* src = arr.array;
        JsFunction* fn = (JsFunction*)callback.function;
        Item prev_this = js_current_this;
        if (argc >= 2 && get_type_id(args[1]) != LMD_TYPE_UNDEFINED) js_current_this = args[1];
        for (int i = src->length - 1; i >= 0; i--) {
            Item cb_args[3] = { src->items[i], (Item){.item = i2it(i)}, cb_this };
            Item pred = js_invoke_fn(fn, cb_args, fn->param_count >= 3 ? 3 : (fn->param_count >= 2 ? 2 : 1));
            if (js_is_truthy(pred)) { js_current_this = prev_this; return src->items[i]; }
        }
        js_current_this = prev_this;
        return make_js_undefined();
    }
    // findLastIndex
    if (method->len == 13 && strncmp(method->chars, "findLastIndex", 13) == 0) {
        if (arr_type != LMD_TYPE_ARRAY) return (Item){.item = i2it(-1)};
        if (argc < 1 || get_type_id(args[0]) != LMD_TYPE_FUNC) return js_throw_not_callable("callback");
        Item callback = args[0];
        Array* src = arr.array;
        JsFunction* fn = (JsFunction*)callback.function;
        Item prev_this = js_current_this;
        if (argc >= 2 && get_type_id(args[1]) != LMD_TYPE_UNDEFINED) js_current_this = args[1];
        for (int i = src->length - 1; i >= 0; i--) {
            Item cb_args[3] = { src->items[i], (Item){.item = i2it(i)}, cb_this };
            Item pred = js_invoke_fn(fn, cb_args, fn->param_count >= 3 ? 3 : (fn->param_count >= 2 ? 2 : 1));
            if (js_is_truthy(pred)) { js_current_this = prev_this; return (Item){.item = i2it(i)}; }
        }
        js_current_this = prev_this;
        return (Item){.item = i2it(-1)};
    }
    // some
    if (method->len == 4 && strncmp(method->chars, "some", 4) == 0) {
        if (arr_type != LMD_TYPE_ARRAY) return (Item){.item = b2it(false)};
        if (argc < 1 || get_type_id(args[0]) != LMD_TYPE_FUNC) return js_throw_not_callable("callback");
        Item callback = args[0];
        Array* src = arr.array;
        JsFunction* fn = (JsFunction*)callback.function;
        // v20: thisArg support
        Item prev_this = js_current_this;
        if (argc >= 2 && get_type_id(args[1]) != LMD_TYPE_UNDEFINED) js_current_this = args[1];
        int len = src->length;  // spec: capture length before loop
        for (int i = 0; i < len; i++) {
            // v25: skip holes (deleted elements) per ES spec
            if (i >= src->length || src->items[i].item == JS_DELETED_SENTINEL_VAL) continue;
            Item cb_args[3] = { js_array_element(arr, i), (Item){.item = i2it(i)}, cb_this };
            Item pred = js_invoke_fn(fn, cb_args, fn->param_count >= 3 ? 3 : (fn->param_count >= 2 ? 2 : 1));
            if (js_is_truthy(pred)) { js_current_this = prev_this; return (Item){.item = b2it(true)}; }
        }
        js_current_this = prev_this;
        return (Item){.item = b2it(false)};
    }
    // every
    if (method->len == 5 && strncmp(method->chars, "every", 5) == 0) {
        if (arr_type != LMD_TYPE_ARRAY) return (Item){.item = b2it(true)};
        if (argc < 1 || get_type_id(args[0]) != LMD_TYPE_FUNC) return js_throw_not_callable("callback");
        Item callback = args[0];
        Array* src = arr.array;
        JsFunction* fn = (JsFunction*)callback.function;
        // v20: thisArg support
        Item prev_this = js_current_this;
        if (argc >= 2 && get_type_id(args[1]) != LMD_TYPE_UNDEFINED) js_current_this = args[1];
        int len = src->length;  // spec: capture length before loop
        for (int i = 0; i < len; i++) {
            // v25: skip holes (deleted elements) per ES spec
            if (i >= src->length || src->items[i].item == JS_DELETED_SENTINEL_VAL) continue;
            Item cb_args[3] = { js_array_element(arr, i), (Item){.item = i2it(i)}, cb_this };
            Item pred = js_invoke_fn(fn, cb_args, fn->param_count >= 3 ? 3 : (fn->param_count >= 2 ? 2 : 1));
            if (!js_is_truthy(pred)) { js_current_this = prev_this; return (Item){.item = b2it(false)}; }
        }
        js_current_this = prev_this;
        return (Item){.item = b2it(true)};
    }
    // sort
    if (method->len == 4 && strncmp(method->chars, "sort", 4) == 0) {
        if (arr_type != LMD_TYPE_ARRAY) return arr;
        Array* src = arr.array;
        // Per spec: if comparefn is defined and not undefined, it must be callable
        if (argc >= 1 && args[0].item != ITEM_JS_UNDEFINED && get_type_id(args[0]) != LMD_TYPE_FUNC) {
            return js_throw_not_callable("comparefn");
        }
        // ES spec: partition — move holes and undefineds to end, sort only defined non-hole values
        int len = src->length;
        int num_defined = 0;
        int num_undefined = 0;
        for (int i = 0; i < len; i++) {
            if (js_is_deleted_sentinel(src->items[i])) {
                // hole — skip
            } else if (src->items[i].item == ITEM_JS_UNDEFINED) {
                num_undefined++;
            } else {
                // compact defined values to the front
                src->items[num_defined++] = src->items[i];
            }
        }
        // sort only the defined portion [0..num_defined)
        if (argc >= 1 && get_type_id(args[0]) == LMD_TYPE_FUNC) {
            // sort with comparator callback
            JsFunction* cmp_fn = (JsFunction*)args[0].function;
            // insertion sort using comparator
            for (int i = 1; i < num_defined; i++) {
                Item key_item = src->items[i];
                int j = i - 1;
                while (j >= 0) {
                    Item cmp_args[2] = { src->items[j], key_item };
                    Item cmp_result = js_invoke_fn(cmp_fn, cmp_args, 2);
                    double cval = js_get_number(cmp_result);
                    if (cval <= 0) break;
                    src->items[j + 1] = src->items[j];
                    j--;
                }
                src->items[j + 1] = key_item;
            }
        } else {
            // default sort: lexicographic string comparison (JS spec)
            for (int i = 1; i < num_defined; i++) {
                Item key_item = src->items[i];
                Item key_str = js_to_string(key_item);
                String* ks = it2s(key_str);
                int j = i - 1;
                while (j >= 0) {
                    Item j_str = js_to_string(src->items[j]);
                    String* js_s = it2s(j_str);
                    // compare strings lexicographically
                    int cmp = 0;
                    if (js_s && ks) {
                        int min_len = js_s->len < ks->len ? js_s->len : ks->len;
                        cmp = strncmp(js_s->chars, ks->chars, min_len);
                        if (cmp == 0) cmp = (int)js_s->len - (int)ks->len;
                    }
                    if (cmp <= 0) break;
                    src->items[j + 1] = src->items[j];
                    j--;
                }
                src->items[j + 1] = key_item;
            }
        }
        // fill undefineds after defined values, then holes after that
        int pos = num_defined;
        for (int i = 0; i < num_undefined; i++) {
            src->items[pos++] = (Item){.item = ITEM_JS_UNDEFINED};
        }
        Item hole = (Item){.item = JS_DELETED_SENTINEL_VAL};
        while (pos < len) {
            src->items[pos++] = hole;
        }
        return arr;
    }
    // flat
    if (method->len == 4 && strncmp(method->chars, "flat", 4) == 0) {
        if (arr_type != LMD_TYPE_ARRAY) return arr;
        // depth defaults to 1, can be Infinity
        int depth = 1;
        if (argc > 0) {
            double d = js_get_number(args[0]);
            if (d != d) { depth = 0; } // NaN → 0
            else if (d >= 2147483647.0) { depth = 2147483647; } // Infinity
            else { depth = (int)d; }
        }
        if (depth <= 0) return arr;  // depth 0 → return copy? no, return as-is per spec
        // recursive flatten helper using a lambda-style approach
        // We'll use iterative BFS-style flatten up to depth
        Item result = js_array_new(0);
        Array* dst = result.array;
        // stack-based approach: push (array, index, remaining_depth)
        struct FlatFrame { Array* src; int idx; int rem_depth; };
        FlatFrame stack[64]; // support up to 64 nesting levels
        int sp = 0;
        stack[0] = {arr.array, 0, depth};
        while (sp >= 0) {
            FlatFrame& f = stack[sp];
            if (f.idx >= f.src->length) { sp--; continue; }
            Item elem = f.src->items[f.idx++];
            if (f.rem_depth > 0 && get_type_id(elem) == LMD_TYPE_ARRAY) {
                // push inner array onto stack
                if (sp + 1 < 64) {
                    stack[sp + 1] = {elem.array, 0, f.rem_depth - 1};
                    sp++;
                } else {
                    // max nesting exceeded, just push as-is
                    js_array_push_item_direct(dst, elem);
                }
            } else {
                js_array_push_item_direct(dst, elem);
            }
        }
        return result;
    }
    // fill(value, start?, end?) — fill array elements in range with value
    if (method->len == 4 && strncmp(method->chars, "fill", 4) == 0) {
        if (argc < 1) return arr;
        if (argc == 1) return js_array_fill(arr, args[0]);
        // fill with start and/or end index
        if (arr_type != LMD_TYPE_ARRAY) return arr;
        Array* a = arr.array;
        int len = a->length;
        int start = argc > 1 ? (int)js_get_number(args[1]) : 0;
        int end   = argc > 2 ? (int)js_get_number(args[2]) : len;
        if (start < 0) start = len + start;
        if (start < 0) start = 0;
        if (start > len) start = len;
        if (end < 0) end = len + end;
        if (end < 0) end = 0;
        if (end > len) end = len;
        Item val = args[0];
        for (int i = start; i < end; i++) {
            a->items[i] = val;
        }
        return arr;
    }
    // copyWithin(target, start, end?) — copy elements within the array
    if (method->len == 10 && strncmp(method->chars, "copyWithin", 10) == 0) {
        if (arr_type != LMD_TYPE_ARRAY || argc < 2) return arr;
        Array* a = arr.array;
        int len = a->length;
        // Use double to handle -Infinity/Infinity/NaN safely before clamping to int
        double d_target = js_get_number(args[0]);
        double d_start = js_get_number(args[1]);
        double d_end = argc > 2 ? js_get_number(args[2]) : (double)len;
        if (d_target != d_target) d_target = 0; // NaN → 0
        if (d_start != d_start) d_start = 0;
        if (d_end != d_end) d_end = 0;
        int target = (d_target < 0) ? (int)fmax(len + d_target, 0) : (int)fmin(d_target, len);
        int start = (d_start < 0) ? (int)fmax(len + d_start, 0) : (int)fmin(d_start, len);
        int end = (d_end < 0) ? (int)fmax(len + d_end, 0) : (int)fmin(d_end, len);
        int count = end - start;
        if (count <= 0) return arr;
        if (target + count > len) count = len - target;
        if (count <= 0) return arr;
        // Use memmove for overlapping regions
        memmove(&a->items[target], &a->items[start], count * sizeof(Item));
        return arr;
    }
    // splice(start, deleteCount, ...items) — mutating
    if (method->len == 6 && strncmp(method->chars, "splice", 6) == 0) {
        if (arr_type != LMD_TYPE_ARRAY) return js_array_new(0);
        Array* a = arr.array;
        int start = argc > 0 ? (int)js_get_number(args[0]) : 0;
        if (start < 0) start = a->length + start;
        if (start < 0) start = 0;
        if (start > a->length) start = a->length;
        int delete_count = argc > 1 ? (int)js_get_number(args[1]) : (a->length - start);
        if (delete_count < 0) delete_count = 0;
        if (start + delete_count > a->length) delete_count = a->length - start;
        int insert_count = argc > 2 ? argc - 2 : 0;

        // save deleted elements
        Item deleted = js_array_new(0);
        Array* del_arr = deleted.array;
        for (int i = 0; i < delete_count; i++) {
            js_array_push_item_direct(del_arr, a->items[start + i]);
        }

        // shift elements
        int shift = insert_count - delete_count;
        int old_len = a->length;
        int new_len = old_len + shift;
        if (shift > 0) {
            // grow: ensure capacity, then memmove
            if (new_len + 4 > a->capacity) {
                int new_cap = new_len + 4;
                Item* new_items = (Item*)malloc(new_cap * sizeof(Item));
                if (a->items && a->length > 0) {
                    memcpy(new_items, a->items, a->length * sizeof(Item));
                }
                a->items = new_items;
                a->capacity = new_cap;
            }
            // move elements after delete region to their new positions
            int elements_to_move = old_len - start - delete_count;
            if (elements_to_move > 0) {
                memmove(&a->items[start + insert_count], &a->items[start + delete_count],
                        elements_to_move * sizeof(Item));
            }
            a->length = new_len;
        } else if (shift < 0) {
            // shrink: memmove left, then adjust length
            int elements_to_move = old_len - start - delete_count;
            if (elements_to_move > 0) {
                memmove(&a->items[start + insert_count], &a->items[start + delete_count],
                        elements_to_move * sizeof(Item));
            }
            a->length = new_len;
        }

        // insert new items
        for (int i = 0; i < insert_count; i++) {
            a->items[start + i] = args[2 + i];
        }
        return deleted;
    }
    // toSorted() — ES2023: returns new sorted copy without mutating original
    if (method->len == 8 && strncmp(method->chars, "toSorted", 8) == 0) {
        if (arr_type != LMD_TYPE_ARRAY) return arr;
        Array* src = arr.array;
        Item copy = js_array_new(src->length);
        Array* dst = copy.array;
        for (int i = 0; i < src->length; i++) dst->items[i] = src->items[i];
        dst->length = src->length;
        // reuse sort logic on the copy
        Item sort_name = (Item){.item = s2it(heap_create_name("sort", 4))};
        Item sort_args[1] = { argc >= 1 ? args[0] : make_js_undefined() };
        return js_array_method(copy, sort_name, sort_args, argc >= 1 ? 1 : 0);
    }
    // toReversed() — ES2023: returns new reversed copy without mutating original
    if (method->len == 10 && strncmp(method->chars, "toReversed", 10) == 0) {
        if (arr_type != LMD_TYPE_ARRAY) return arr;
        Array* src = arr.array;
        int n = src->length;
        Item copy = js_array_new(n);
        Array* dst = copy.array;
        for (int i = 0; i < n; i++) dst->items[i] = src->items[n - 1 - i];
        dst->length = n;
        return copy;
    }
    // toSpliced(start, deleteCount, ...items) — ES2023: returns new array with splice applied
    if (method->len == 9 && strncmp(method->chars, "toSpliced", 9) == 0) {
        if (arr_type != LMD_TYPE_ARRAY) return arr;
        Array* src = arr.array;
        int start = argc > 0 ? (int)js_get_number(args[0]) : 0;
        if (start < 0) start = src->length + start;
        if (start < 0) start = 0;
        if (start > src->length) start = src->length;
        int delete_count = argc > 1 ? (int)js_get_number(args[1]) : (src->length - start);
        if (delete_count < 0) delete_count = 0;
        if (start + delete_count > src->length) delete_count = src->length - start;
        int insert_count = argc > 2 ? argc - 2 : 0;
        int new_len = src->length - delete_count + insert_count;
        Item result = js_array_new(new_len);
        Array* dst = result.array;
        int di = 0;
        for (int i = 0; i < start; i++) dst->items[di++] = src->items[i];
        for (int i = 0; i < insert_count; i++) dst->items[di++] = args[2 + i];
        for (int i = start + delete_count; i < src->length; i++) dst->items[di++] = src->items[i];
        dst->length = di;
        return result;
    }
    // with(index, value) — ES2023: returns copy with one element replaced
    if (method->len == 4 && strncmp(method->chars, "with", 4) == 0) {
        if (arr_type != LMD_TYPE_ARRAY || argc < 2) return arr;
        Array* src = arr.array;
        int idx = (int)js_get_number(args[0]);
        if (idx < 0) idx = src->length + idx;
        if (idx < 0 || idx >= src->length) return js_throw_range_error("Invalid index");
        Item result = js_array_new(src->length);
        Array* dst = result.array;
        for (int i = 0; i < src->length; i++) dst->items[i] = src->items[i];
        dst->length = src->length;
        dst->items[idx] = args[1];
        return result;
    }
    // shift() — remove and return first element
    if (method->len == 5 && strncmp(method->chars, "shift", 5) == 0) {
        if (arr_type != LMD_TYPE_ARRAY) return make_js_undefined();
        Array* a = arr.array;
        if (a->length == 0) return make_js_undefined();
        Item first = a->items[0];
        memmove(&a->items[0], &a->items[1], (a->length - 1) * sizeof(Item));
        a->length--;
        return first;
    }
    // unshift(...items) — prepend items, return new length
    if (method->len == 7 && strncmp(method->chars, "unshift", 7) == 0) {
        if (arr_type != LMD_TYPE_ARRAY || argc < 1) return (Item){.item = i2it(arr_type == LMD_TYPE_ARRAY ? arr.array->length : 0)};
        Array* a = arr.array;
        int old_len = a->length;
        int new_len = old_len + argc;
        // ensure capacity
        if (new_len + 4 > a->capacity) {
            int new_cap = new_len + 4;
            Item* new_items = (Item*)malloc(new_cap * sizeof(Item));
            if (a->items && a->length > 0) {
                memcpy(new_items, a->items, a->length * sizeof(Item));
            }
            a->items = new_items;
            a->capacity = new_cap;
        }
        // shift existing elements right
        memmove(&a->items[argc], &a->items[0], old_len * sizeof(Item));
        // insert new items at front
        for (int i = 0; i < argc; i++) {
            a->items[i] = args[i];
        }
        a->length = new_len;
        return (Item){.item = i2it(a->length)};
    }
    // lastIndexOf
    if (method->len == 11 && strncmp(method->chars, "lastIndexOf", 11) == 0) {
        if (arr_type != LMD_TYPE_ARRAY) return (Item){.item = i2it(-1)};
        Item search_val = (argc >= 1) ? args[0] : make_js_undefined();
        Array* a = arr.array;
        int from = argc > 1 ? (int)js_get_number(args[1]) : a->length - 1;
        if (from < 0) from = a->length + from;
        if (from >= a->length) from = a->length - 1;
        for (int i = from; i >= 0; i--) {
            if (it2b(js_strict_equal(js_array_element(arr, i), search_val))) return (Item){.item = i2it(i)};
        }
        return (Item){.item = i2it(-1)};
    }
    // flatMap
    if (method->len == 7 && strncmp(method->chars, "flatMap", 7) == 0) {
        if (argc < 1 || arr_type != LMD_TYPE_ARRAY) return arr;
        Item callback = args[0];
        if (get_type_id(callback) != LMD_TYPE_FUNC) return arr;
        Array* src = arr.array;
        Item result = js_array_new(0);
        Array* dst = result.array;
        JsFunction* fn = (JsFunction*)callback.function;
        for (int i = 0; i < src->length; i++) {
            Item cb_args[3] = { js_array_element(arr, i), (Item){.item = i2it(i)}, cb_this };
            Item mapped = js_invoke_fn(fn, cb_args, fn->param_count >= 3 ? 3 : (fn->param_count >= 2 ? 2 : 1));
            // flatten one level
            if (get_type_id(mapped) == LMD_TYPE_ARRAY) {
                Array* inner = mapped.array;
                for (int j = 0; j < inner->length; j++) {
                    js_array_push_item_direct(dst, inner->items[j]);
                }
            } else {
                js_array_push_item_direct(dst, mapped);
            }
        }
        return result;
    }
    // reduceRight
    if (method->len == 11 && strncmp(method->chars, "reduceRight", 11) == 0) {
        if (arr_type != LMD_TYPE_ARRAY) return ItemNull;
        if (argc < 1 || get_type_id(args[0]) != LMD_TYPE_FUNC) return js_throw_not_callable("callback");
        Item callback = args[0];
        Array* src = arr.array;
        JsFunction* fn = (JsFunction*)callback.function;
        Item accumulator;
        int start_idx;
        if (argc >= 2) {
            accumulator = args[1];
            start_idx = src->length - 1;
        } else {
            if (src->length == 0) {
                Item type_name = (Item){.item = s2it(heap_create_name("TypeError"))};
                Item msg_item = (Item){.item = s2it(heap_create_name("Reduce of empty array with no initial value"))};
                Item error = js_new_error_with_name(type_name, msg_item);
                js_throw_value(error);
                return ItemNull;
            }
            accumulator = js_array_element(arr, src->length - 1);
            start_idx = src->length - 2;
        }
        for (int i = start_idx; i >= 0; i--) {
            Item cb_args[4] = { accumulator, js_array_element(arr, i), (Item){.item = i2it(i)}, cb_this };
            accumulator = js_invoke_fn(fn, cb_args, fn->param_count >= 4 ? 4 : (fn->param_count >= 3 ? 3 : 2));
        }
        return accumulator;
    }
    // at(index) — supports negative indexing
    if (method->len == 2 && strncmp(method->chars, "at", 2) == 0) {
        if (arr_type != LMD_TYPE_ARRAY) return make_js_undefined();
        Array* a = arr.array;
        int idx = (argc < 1) ? 0 : (int)js_get_number(args[0]);
        if (idx < 0) idx = a->length + idx;
        if (idx < 0 || idx >= a->length) return make_js_undefined();
        Item val = a->items[idx];
        if (val.item == JS_DELETED_SENTINEL_VAL) return make_js_undefined();
        return val;
    }
    // toString — join elements with comma
    if (method->len == 8 && strncmp(method->chars, "toString", 8) == 0) {
        if (arr_type != LMD_TYPE_ARRAY) return js_to_string(arr);
        Array* a = arr.array;
        StrBuf* buf = strbuf_new();
        for (int i = 0; i < a->length; i++) {
            if (i > 0) strbuf_append_str_n(buf, ",", 1);
            // v25: skip deleted elements (holes) — treated as empty
            if (a->items[i].item == JS_DELETED_SENTINEL_VAL) continue;
            TypeId etype = get_type_id(a->items[i]);
            if (etype == LMD_TYPE_NULL || etype == LMD_TYPE_UNDEFINED) continue;
            Item elem_str = js_to_string(a->items[i]);
            String* s = it2s(elem_str);
            if (s && s->len > 0) strbuf_append_str_n(buf, s->chars, s->len);
        }
        String* result = heap_strcpy(buf->str, buf->length);
        strbuf_free(buf);
        return (Item){.item = s2it(result)};
    }

    // keys() — returns array iterator (kind=0: keys)
    if (method->len == 4 && strncmp(method->chars, "keys", 4) == 0) {
        if (arr_type != LMD_TYPE_ARRAY) return make_js_undefined();
        Item iter = js_new_object();
        js_property_set(iter, (Item){.item = s2it(heap_create_name("__array__", 9))}, arr);
        js_property_set(iter, (Item){.item = s2it(heap_create_name("__index__", 9))}, (Item){.item = i2it(0)});
        js_property_set(iter, (Item){.item = s2it(heap_create_name("__kind__", 8))}, (Item){.item = i2it(0)});
        Item next_fn = js_get_or_create_builtin(JS_BUILTIN_ARRAY_ITER_NEXT, "next", 0);
        js_property_set(iter, (Item){.item = s2it(heap_create_name("next", 4))}, next_fn);
        return iter;
    }
    // values() — returns array iterator (kind=1: values)
    if (method->len == 6 && strncmp(method->chars, "values", 6) == 0) {
        if (arr_type != LMD_TYPE_ARRAY) return make_js_undefined();
        Item iter = js_new_object();
        js_property_set(iter, (Item){.item = s2it(heap_create_name("__array__", 9))}, arr);
        js_property_set(iter, (Item){.item = s2it(heap_create_name("__index__", 9))}, (Item){.item = i2it(0)});
        js_property_set(iter, (Item){.item = s2it(heap_create_name("__kind__", 8))}, (Item){.item = i2it(1)});
        Item next_fn = js_get_or_create_builtin(JS_BUILTIN_ARRAY_ITER_NEXT, "next", 0);
        js_property_set(iter, (Item){.item = s2it(heap_create_name("next", 4))}, next_fn);
        return iter;
    }
    // entries() — returns array iterator (kind=2: entries [index, value])
    if (method->len == 7 && strncmp(method->chars, "entries", 7) == 0) {
        if (arr_type != LMD_TYPE_ARRAY) return make_js_undefined();
        Item iter = js_new_object();
        js_property_set(iter, (Item){.item = s2it(heap_create_name("__array__", 9))}, arr);
        js_property_set(iter, (Item){.item = s2it(heap_create_name("__index__", 9))}, (Item){.item = i2it(0)});
        js_property_set(iter, (Item){.item = s2it(heap_create_name("__kind__", 8))}, (Item){.item = i2it(2)});
        Item next_fn = js_get_or_create_builtin(JS_BUILTIN_ARRAY_ITER_NEXT, "next", 0);
        js_property_set(iter, (Item){.item = s2it(heap_create_name("next", 4))}, next_fn);
        return iter;
    }

    // Fallback: check companion map for custom properties, then prototype chain
    {
        // Array.prototype.toLocaleString: join elements using element.toLocaleString()
        if (method->len == 14 && strncmp(method->chars, "toLocaleString", 14) == 0) {
            Array* a = arr.array;
            if (!a || a->length == 0) return (Item){.item = s2it(heap_create_name("", 0))};
            StrBuf* sb = strbuf_new();
            for (int i = 0; i < a->length; i++) {
                if (i > 0) strbuf_append_str_n(sb, ",", 1);
                Item elem = a->items[i];
                TypeId et = get_type_id(elem);
                if (et != LMD_TYPE_NULL && et != LMD_TYPE_UNDEFINED) {
                    Item elem_str = js_dispatch_builtin(JS_BUILTIN_OBJ_TO_LOCALE_STRING, elem, NULL, 0);
                    if (get_type_id(elem_str) == LMD_TYPE_STRING) {
                        String* s = it2s(elem_str);
                        if (s && s->len > 0) strbuf_append_str_n(sb, s->chars, (int)s->len);
                    }
                }
            }
            String* result = heap_create_name(sb->str, sb->length);
            strbuf_free(sb);
            return (Item){.item = s2it(result)};
        }
        Item fn = js_property_access(arr, method_name);
        if (get_type_id(fn) == LMD_TYPE_FUNC) {
            return js_call_function(fn, arr, args, argc);
        }
    }
    log_debug("js_array_method: unknown method '%.*s'", (int)method->len, method->chars);
    return ItemNull;
}

// Generic method call with expanded args array (used for obj.method(...spread) calls)
extern "C" Item js_method_call_apply(Item obj, Item method_name, Item args_array) {
    int argc = 0;
    Item* args = NULL;
    if (get_type_id(args_array) == LMD_TYPE_ARRAY && args_array.array->length > 0) {
        argc = (int)args_array.array->length;
        args = (Item*)alloca(argc * sizeof(Item));
        for (int i = 0; i < argc; i++) {
            Item idx = {.item = i2it(i)};
            args[i] = js_array_get(args_array, idx);
        }
    }
    TypeId obj_type = get_type_id(obj);
    if (obj_type == LMD_TYPE_STRING) {
        return js_string_method(obj, method_name, args, argc);
    }
    if (obj_type == LMD_TYPE_ARRAY) {
        return js_array_method(obj, method_name, args, argc);
    }
    if (obj_type == LMD_TYPE_INT || obj_type == LMD_TYPE_FLOAT) {
        return js_number_method(obj, method_name, args, argc);
    }
    return js_map_method(obj, method_name, args, argc);
}

// =============================================================================
// Math Object Methods
// =============================================================================

// backing store for user-defined Math properties (e.g. Math.sumPrecise polyfill)
static Item js_math_object = {.item = ITEM_NULL};

void js_reset_math_object() {
    js_math_object = (Item){.item = ITEM_NULL};
}

static Item js_get_math_object() {
    if (js_math_object.item == ITEM_NULL) {
        js_math_object = js_object_create(ItemNull);
        heap_register_gc_root(&js_math_object.item);
        // Mark as Math for Object.prototype.toString
        Item mk = (Item){.item = s2it(heap_create_name("__is_math__", 11))};
        js_property_set(js_math_object, mk, (Item){.item = b2it(true)});
        // v18n: Add Symbol.toStringTag for Math (stored as __sym_4 to match Object.prototype.toString)
        Item tag_k = (Item){.item = s2it(heap_create_name("__sym_4", 7))};
        js_property_set(js_math_object, tag_k, (Item){.item = s2it(heap_create_name("Math", 4))});
        // Populate Math constants (non-writable, non-enumerable, non-configurable per spec)
        struct { const char* name; double val; } mc[] = {
            {"PI", M_PI}, {"E", M_E}, {"LN2", M_LN2}, {"LN10", M_LN10},
            {"LOG2E", M_LOG2E}, {"LOG10E", M_LOG10E}, {"SQRT2", M_SQRT2}, {"SQRT1_2", M_SQRT1_2},
        };
        for (int i = 0; i < (int)(sizeof(mc) / sizeof(mc[0])); i++) {
            int nlen = (int)strlen(mc[i].name);
            Item key = (Item){.item = s2it(heap_create_name(mc[i].name, nlen))};
            js_property_set(js_math_object, key, js_make_number(mc[i].val));
            // Mark non-enumerable, non-writable, non-configurable
            char buf[64];
            snprintf(buf, sizeof(buf), "__ne_%s", mc[i].name);
            js_property_set(js_math_object, (Item){.item = s2it(heap_create_name(buf, strlen(buf)))}, (Item){.item = b2it(true)});
            snprintf(buf, sizeof(buf), "__nw_%s", mc[i].name);
            js_property_set(js_math_object, (Item){.item = s2it(heap_create_name(buf, strlen(buf)))}, (Item){.item = b2it(true)});
            snprintf(buf, sizeof(buf), "__nc_%s", mc[i].name);
            js_property_set(js_math_object, (Item){.item = s2it(heap_create_name(buf, strlen(buf)))}, (Item){.item = b2it(true)});
        }
        // Populate Math methods as function properties for dynamic access (Math[m])
        struct { const char* name; int id; int pc; } mm[] = {
            {"abs", JS_BUILTIN_MATH_ABS, 1}, {"floor", JS_BUILTIN_MATH_FLOOR, 1},
            {"ceil", JS_BUILTIN_MATH_CEIL, 1}, {"round", JS_BUILTIN_MATH_ROUND, 1},
            {"sqrt", JS_BUILTIN_MATH_SQRT, 1}, {"pow", JS_BUILTIN_MATH_POW, 2},
            {"min", JS_BUILTIN_MATH_MIN, 2}, {"max", JS_BUILTIN_MATH_MAX, 2},
            {"log", JS_BUILTIN_MATH_LOG, 1}, {"log10", JS_BUILTIN_MATH_LOG10, 1},
            {"log2", JS_BUILTIN_MATH_LOG2, 1}, {"exp", JS_BUILTIN_MATH_EXP, 1},
            {"sin", JS_BUILTIN_MATH_SIN, 1}, {"cos", JS_BUILTIN_MATH_COS, 1},
            {"tan", JS_BUILTIN_MATH_TAN, 1}, {"sign", JS_BUILTIN_MATH_SIGN, 1},
            {"trunc", JS_BUILTIN_MATH_TRUNC, 1}, {"random", JS_BUILTIN_MATH_RANDOM, 0},
            {"asin", JS_BUILTIN_MATH_ASIN, 1}, {"acos", JS_BUILTIN_MATH_ACOS, 1},
            {"atan", JS_BUILTIN_MATH_ATAN, 1}, {"atan2", JS_BUILTIN_MATH_ATAN2, 2},
            {"cbrt", JS_BUILTIN_MATH_CBR, 1}, {"hypot", JS_BUILTIN_MATH_HYPOT, 2},
            {"clz32", JS_BUILTIN_MATH_CLZ32, 1}, {"fround", JS_BUILTIN_MATH_FROUND, 1},
            {"imul", JS_BUILTIN_MATH_IMUL, 2}, {"sinh", JS_BUILTIN_MATH_SINH, 1},
            {"cosh", JS_BUILTIN_MATH_COSH, 1}, {"tanh", JS_BUILTIN_MATH_TANH, 1},
            {"asinh", JS_BUILTIN_MATH_ASINH, 1}, {"acosh", JS_BUILTIN_MATH_ACOSH, 1},
            {"atanh", JS_BUILTIN_MATH_ATANH, 1}, {"expm1", JS_BUILTIN_MATH_EXPM1, 1},
            {"log1p", JS_BUILTIN_MATH_LOG1P, 1},
        };
        for (int i = 0; i < (int)(sizeof(mm) / sizeof(mm[0])); i++) {
            Item key = (Item){.item = s2it(heap_create_name(mm[i].name, strlen(mm[i].name)))};
            Item fn = js_get_or_create_builtin(mm[i].id, mm[i].name, mm[i].pc);
            js_property_set(js_math_object, key, fn);
            js_mark_non_enumerable(js_math_object, key);
        }
    }
    return js_math_object;
}

// v18n: externally callable for transpiler to resolve bare `Math` identifier
extern "C" Item js_get_math_object_value() {
    return js_get_math_object();
}

// v18n: JSON and console as global objects for bare identifier resolution
static Item js_json_object = {.item = ITEM_NULL};
void js_reset_json_object() { js_json_object = (Item){.item = ITEM_NULL}; }

extern "C" Item js_get_json_object_value() {
    if (js_json_object.item == ITEM_NULL) {
        js_json_object = js_object_create(ItemNull);
        heap_register_gc_root(&js_json_object.item);
        Item tag_k = (Item){.item = s2it(heap_create_name("__sym_4", 7))};
        js_property_set(js_json_object, tag_k, (Item){.item = s2it(heap_create_name("JSON", 4))});
        // v28: add parse and stringify as callable function properties
        struct { const char* name; int id; int pc; } jm[] = {
            {"parse", JS_BUILTIN_JSON_PARSE, 2},
            {"stringify", JS_BUILTIN_JSON_STRINGIFY, 3},
        };
        for (int i = 0; i < (int)(sizeof(jm) / sizeof(jm[0])); i++) {
            Item key = (Item){.item = s2it(heap_create_name(jm[i].name, strlen(jm[i].name)))};
            Item fn = js_get_or_create_builtin(jm[i].id, jm[i].name, jm[i].pc);
            js_property_set(js_json_object, key, fn);
            js_mark_non_enumerable(js_json_object, key);
        }
    }
    return js_json_object;
}

static Item js_console_object = {.item = ITEM_NULL};
void js_reset_console_object() { js_console_object = (Item){.item = ITEM_NULL}; }

extern "C" Item js_get_console_object_value() {
    if (js_console_object.item == ITEM_NULL) {
        js_console_object = js_object_create(ItemNull);
        heap_register_gc_root(&js_console_object.item);
    }
    return js_console_object;
}

// v25: Reflect global object for bare identifier resolution
static Item js_reflect_object = {.item = ITEM_NULL};
void js_reset_reflect_object() { js_reflect_object = (Item){.item = ITEM_NULL}; }

extern "C" Item js_get_reflect_object_value() {
    if (js_reflect_object.item == ITEM_NULL) {
        js_reflect_object = js_object_create(ItemNull);
        heap_register_gc_root(&js_reflect_object.item);
        Item tag_k = (Item){.item = s2it(heap_create_name("__sym_4", 7))};
        js_property_set(js_reflect_object, tag_k, (Item){.item = s2it(heap_create_name("Reflect", 7))});
    }
    return js_reflect_object;
}

extern "C" Item js_math_set_property(Item key, Item value) {
    Item math_obj = js_get_math_object();
    return js_property_set(math_obj, key, value);
}

extern "C" Item js_math_method(Item method_name, Item* args, int argc) {
    if (get_type_id(method_name) != LMD_TYPE_STRING) return ItemNull;
    String* method = it2s(method_name);
    if (!method) return ItemNull;

    // check the real MAP object first — user writes/deletes override builtins
    if (js_math_object.item != ITEM_NULL) {
        bool found = false;
        Item val = js_map_get_fast(js_math_object.map, method->chars, (int)method->len, &found);
        if (found) {
            // property was deleted — method no longer exists
            if (js_is_deleted_sentinel(val)) return ItemNull;
            // if user overwrote with a non-builtin value, call it or return null
            if (get_type_id(val) == LMD_TYPE_FUNC) {
                JsFunction* fn = (JsFunction*)val.function;
                if (fn && fn->builtin_id == 0) {
                    // user-defined function override — call it
                    return js_call_function(val, make_js_undefined(), args, argc);
                }
                // original builtin — fall through to fast inline dispatch
            } else {
                // overwritten with non-function (e.g. during isWritable test)
                return ItemNull;
            }
        }
    }

    // Math.abs
    if (method->len == 3 && strncmp(method->chars, "abs", 3) == 0) {
        if (argc < 1) return ItemNull;
        return fn_abs(js_to_number(args[0]));
    }
    // Math.floor — JS semantics: preserves -0, NaN, Infinity
    if (method->len == 5 && strncmp(method->chars, "floor", 5) == 0) {
        if (argc < 1) return js_make_number(NAN);
        double d = js_get_number(js_to_number(args[0]));
        if (d != d || !isfinite(d) || d == 0.0) return js_make_number(d);
        double r = floor(d);
        if (r == (int64_t)r && r >= -9007199254740991.0 && r <= 9007199254740991.0) {
            return (Item){.item = i2it((int64_t)r)};
        }
        return js_make_number(r);
    }
    // Math.ceil — JS semantics: preserves -0, NaN, +-Infinity
    if (method->len == 4 && strncmp(method->chars, "ceil", 4) == 0) {
        if (argc < 1) return js_make_number(NAN);
        double d = js_get_number(js_to_number(args[0]));
        if (d != d || !isfinite(d) || d == 0.0) return js_make_number(d);
        double r = ceil(d);
        // ceil of negative value between -1 and 0 exclusive should be -0
        if (r == 0.0 && d < 0.0) return js_make_number(-0.0);
        if (r == (int64_t)r && r >= -9007199254740991.0 && r <= 9007199254740991.0) {
            return (Item){.item = i2it((int64_t)r)};
        }
        return js_make_number(r);
    }
    // Math.round (JS semantics: round half to +Infinity)
    if (method->len == 5 && strncmp(method->chars, "round", 5) == 0) {
        if (argc < 1) return js_make_number(NAN);
        Item num = js_to_number(args[0]);
        TypeId t = get_type_id(num);
        if (t == LMD_TYPE_INT || t == LMD_TYPE_INT64) return num;
        if (t == LMD_TYPE_FLOAT) {
            double v = num.get_double();
            if (v != v || !isfinite(v) || v == 0.0) return js_make_number(v);
            double r = floor(v + 0.5);
            // round(-0.5) → -0 in JS (floor(-0.5 + 0.5) = floor(0.0) = 0, but should be -0)
            if (r == 0.0 && v < 0.0) return js_make_number(-0.0);
            return push_d(r);
        }
        return js_make_number(NAN);
    }
    // Math.sqrt
    if (method->len == 4 && strncmp(method->chars, "sqrt", 4) == 0) {
        if (argc < 1) return ItemNull;
        return fn_math_sqrt(js_to_number(args[0]));
    }
    // Math.pow — ES2024 §21.3.2.26 with spec-mandated overrides of C pow()
    if (method->len == 3 && strncmp(method->chars, "pow", 3) == 0) {
        if (argc < 2) return ItemNull;
        double base = js_get_number(js_to_number(args[0]));
        double exp = js_get_number(js_to_number(args[1]));
        return js_make_number(js_math_pow_d(base, exp));
    }
    // Math.min
    if (method->len == 3 && strncmp(method->chars, "min", 3) == 0) {
        if (argc < 1) return js_make_number(INFINITY);
        Item result = js_to_number(args[0]);
        for (int i = 1; i < argc; i++) {
            result = fn_min2(result, js_to_number(args[i]));
        }
        return result;
    }
    // Math.max
    if (method->len == 3 && strncmp(method->chars, "max", 3) == 0) {
        if (argc < 1) return js_make_number(-INFINITY);
        Item result = js_to_number(args[0]);
        for (int i = 1; i < argc; i++) {
            result = fn_max2(result, js_to_number(args[i]));
        }
        return result;
    }
    // Math.log
    if (method->len == 3 && strncmp(method->chars, "log", 3) == 0) {
        if (argc < 1) return ItemNull;
        return fn_math_log(js_to_number(args[0]));
    }
    // Math.log10
    if (method->len == 5 && strncmp(method->chars, "log10", 5) == 0) {
        if (argc < 1) return ItemNull;
        return fn_math_log10(js_to_number(args[0]));
    }
    // Math.exp
    if (method->len == 3 && strncmp(method->chars, "exp", 3) == 0) {
        if (argc < 1) return ItemNull;
        return fn_math_exp(js_to_number(args[0]));
    }
    // Math.sin
    if (method->len == 3 && strncmp(method->chars, "sin", 3) == 0) {
        if (argc < 1) return ItemNull;
        return fn_math_sin(js_to_number(args[0]));
    }
    // Math.cos
    if (method->len == 3 && strncmp(method->chars, "cos", 3) == 0) {
        if (argc < 1) return ItemNull;
        return fn_math_cos(js_to_number(args[0]));
    }
    // Math.tan
    if (method->len == 3 && strncmp(method->chars, "tan", 3) == 0) {
        if (argc < 1) return ItemNull;
        return fn_math_tan(js_to_number(args[0]));
    }
    // Math.sign — JS semantics: sign(-0) === -0, sign(NaN) === NaN
    if (method->len == 4 && strncmp(method->chars, "sign", 4) == 0) {
        if (argc < 1) return js_make_number(NAN);
        double d = js_get_number(js_to_number(args[0]));
        if (d != d) return js_make_number(NAN);
        if (d == 0.0) return js_make_number(d);  // preserves -0
        return (Item){.item = i2it(d > 0 ? 1 : -1)};
    }
    // Math.trunc
    if (method->len == 5 && strncmp(method->chars, "trunc", 5) == 0) {
        if (argc < 1) return js_make_number(NAN);
        double d = js_get_number(js_to_number(args[0]));
        if (d != d) return js_make_number(NAN);        // NaN
        if (d == 0.0 || !isfinite(d)) return js_make_number(d); // +-0, +-Infinity
        return js_make_number(trunc(d));
    }
    // Math.random
    if (method->len == 6 && strncmp(method->chars, "random", 6) == 0) {
        double r = (double)rand() / (double)RAND_MAX;
        return js_make_number(r);
    }
    // Math.asin
    if (method->len == 4 && strncmp(method->chars, "asin", 4) == 0) {
        if (argc < 1) return ItemNull;
        double d = js_get_number(js_to_number(args[0]));
        return js_make_number(asin(d));
    }
    // Math.acos
    if (method->len == 4 && strncmp(method->chars, "acos", 4) == 0) {
        if (argc < 1) return ItemNull;
        double d = js_get_number(js_to_number(args[0]));
        return js_make_number(acos(d));
    }
    // Math.atan
    if (method->len == 4 && strncmp(method->chars, "atan", 4) == 0) {
        if (argc < 1) return ItemNull;
        double d = js_get_number(js_to_number(args[0]));
        return js_make_number(atan(d));
    }
    // Math.atan2
    if (method->len == 5 && strncmp(method->chars, "atan2", 5) == 0) {
        if (argc < 2) return ItemNull;
        double y = js_get_number(js_to_number(args[0]));
        double x = js_get_number(js_to_number(args[1]));
        return js_make_number(atan2(y, x));
    }
    // Math.log2
    if (method->len == 4 && strncmp(method->chars, "log2", 4) == 0) {
        if (argc < 1) return ItemNull;
        double d = js_get_number(js_to_number(args[0]));
        return js_make_number(log2(d));
    }
    // Math.cbrt
    if (method->len == 4 && strncmp(method->chars, "cbrt", 4) == 0) {
        if (argc < 1) return ItemNull;
        double d = js_get_number(js_to_number(args[0]));
        return js_make_number(cbrt(d));
    }
    // Math.hypot
    if (method->len == 5 && strncmp(method->chars, "hypot", 5) == 0) {
        if (argc < 1) return js_make_number(0.0);
        double sum = 0.0;
        for (int i = 0; i < argc; i++) {
            double d = js_get_number(js_to_number(args[i]));
            sum += d * d;
        }
        return js_make_number(sqrt(sum));
    }
    // Math.clz32
    if (method->len == 5 && strncmp(method->chars, "clz32", 5) == 0) {
        if (argc < 1) return js_make_number(32.0);
        uint32_t n = (uint32_t)js_to_int32(js_get_number(args[0]));
        if (n == 0) return js_make_number(32.0);
        return js_make_number((double)__builtin_clz(n));
    }
    // Math.imul
    if (method->len == 4 && strncmp(method->chars, "imul", 4) == 0) {
        if (argc < 2) return js_make_number(0.0);
        int32_t a = js_to_int32(js_get_number(args[0]));
        int32_t b = js_to_int32(js_get_number(args[1]));
        return js_make_number((double)((int32_t)((uint32_t)a * (uint32_t)b)));
    }
    // Math.fround
    if (method->len == 6 && strncmp(method->chars, "fround", 6) == 0) {
        if (argc < 1) return ItemNull;
        float f = (float)js_get_number(js_to_number(args[0]));
        return js_make_number((double)f);
    }
    // Math.sinh
    if (method->len == 4 && strncmp(method->chars, "sinh", 4) == 0) {
        if (argc < 1) return ItemNull;
        return js_make_number(sinh(js_get_number(js_to_number(args[0]))));
    }
    // Math.cosh
    if (method->len == 4 && strncmp(method->chars, "cosh", 4) == 0) {
        if (argc < 1) return ItemNull;
        return js_make_number(cosh(js_get_number(js_to_number(args[0]))));
    }
    // Math.tanh
    if (method->len == 4 && strncmp(method->chars, "tanh", 4) == 0) {
        if (argc < 1) return ItemNull;
        return js_make_number(tanh(js_get_number(js_to_number(args[0]))));
    }
    // Math.asinh
    if (method->len == 5 && strncmp(method->chars, "asinh", 5) == 0) {
        if (argc < 1) return ItemNull;
        return js_make_number(asinh(js_get_number(js_to_number(args[0]))));
    }
    // Math.acosh
    if (method->len == 5 && strncmp(method->chars, "acosh", 5) == 0) {
        if (argc < 1) return ItemNull;
        return js_make_number(acosh(js_get_number(js_to_number(args[0]))));
    }
    // Math.atanh
    if (method->len == 5 && strncmp(method->chars, "atanh", 5) == 0) {
        if (argc < 1) return ItemNull;
        return js_make_number(atanh(js_get_number(js_to_number(args[0]))));
    }
    // Math.expm1
    if (method->len == 5 && strncmp(method->chars, "expm1", 5) == 0) {
        if (argc < 1) return ItemNull;
        return js_make_number(expm1(js_get_number(js_to_number(args[0]))));
    }
    // Math.log1p
    if (method->len == 5 && strncmp(method->chars, "log1p", 5) == 0) {
        if (argc < 1) return ItemNull;
        return js_make_number(log1p(js_get_number(js_to_number(args[0]))));
    }

    log_debug("js_math_method: unknown method '%.*s'", (int)method->len, method->chars);
    // fallback: resolve via prototype chain (Object.prototype methods like hasOwnProperty)
    if (js_math_object.item != ITEM_NULL) {
        Item fn = js_property_access(js_math_object, method_name);
        if (fn.item != ITEM_NULL && get_type_id(fn) == LMD_TYPE_FUNC) {
            return js_call_function(fn, js_math_object, args, argc);
        }
    }
    return ItemNull;
}

// Math method dispatch where args are in a JS array (for spread calls: Math.max(...arr))
extern "C" Item js_math_apply(Item method_name, Item args_array) {
    if (get_type_id(args_array) != LMD_TYPE_ARRAY) {
        // Fall back to 0-arg call
        return js_math_method(method_name, NULL, 0);
    }
    int argc = (int)args_array.array->length;
    if (argc == 0) {
        return js_math_method(method_name, NULL, 0);
    }
    Item* args = (Item*)alloca(argc * sizeof(Item));
    for (int i = 0; i < argc; i++) {
        Item idx = {.item = i2it(i)};
        args[i] = js_array_get(args_array, idx);
    }
    return js_math_method(method_name, args, argc);
}

// Math constants as properties
extern "C" Item js_math_property(Item prop_name) {
    if (get_type_id(prop_name) != LMD_TYPE_STRING) return ItemNull;
    String* prop = it2s(prop_name);
    if (!prop) return ItemNull;

    // check the real MAP object first — user writes/deletes override builtins
    if (js_math_object.item != ITEM_NULL) {
        Map* m = js_math_object.map;
        bool found = false;
        Item val = js_map_get_fast(m, prop->chars, (int)prop->len, &found);
        if (found) {
            // property was deleted — return undefined (not the builtin fallback)
            if (js_is_deleted_sentinel(val)) return make_js_undefined();
            return val;
        }
    }

    // builtin fallback for constants not stored in MAP
    if (prop->len == 2 && strncmp(prop->chars, "PI", 2) == 0) {
        return js_make_number(M_PI);
    }
    if (prop->len == 1 && prop->chars[0] == 'E') {
        return js_make_number(M_E);
    }
    if (prop->len == 3 && strncmp(prop->chars, "LN2", 3) == 0) {
        return js_make_number(M_LN2);
    }
    if (prop->len == 4 && strncmp(prop->chars, "LN10", 4) == 0) {
        return js_make_number(M_LN10);
    }
    if (prop->len == 5 && strncmp(prop->chars, "LOG2E", 5) == 0) {
        return js_make_number(M_LOG2E);
    }
    if (prop->len == 6 && strncmp(prop->chars, "LOG10E", 6) == 0) {
        return js_make_number(M_LOG10E);
    }
    if (prop->len == 5 && strncmp(prop->chars, "SQRT2", 5) == 0) {
        return js_make_number(M_SQRT2);
    }
    if (prop->len == 7 && strncmp(prop->chars, "SQRT1_2", 7) == 0) {
        return js_make_number(M_SQRT1_2);
    }

    // Math methods as first-class function values
    struct MathMethodEntry { const char* name; int len; int id; int pc; };
    static const MathMethodEntry math_methods[] = {
        {"abs", 3, JS_BUILTIN_MATH_ABS, 1},
        {"floor", 5, JS_BUILTIN_MATH_FLOOR, 1},
        {"ceil", 4, JS_BUILTIN_MATH_CEIL, 1},
        {"round", 5, JS_BUILTIN_MATH_ROUND, 1},
        {"sqrt", 4, JS_BUILTIN_MATH_SQRT, 1},
        {"pow", 3, JS_BUILTIN_MATH_POW, 2},
        {"min", 3, JS_BUILTIN_MATH_MIN, 2},
        {"max", 3, JS_BUILTIN_MATH_MAX, 2},
        {"log", 3, JS_BUILTIN_MATH_LOG, 1},
        {"log10", 5, JS_BUILTIN_MATH_LOG10, 1},
        {"log2", 4, JS_BUILTIN_MATH_LOG2, 1},
        {"exp", 3, JS_BUILTIN_MATH_EXP, 1},
        {"sin", 3, JS_BUILTIN_MATH_SIN, 1},
        {"cos", 3, JS_BUILTIN_MATH_COS, 1},
        {"tan", 3, JS_BUILTIN_MATH_TAN, 1},
        {"sign", 4, JS_BUILTIN_MATH_SIGN, 1},
        {"trunc", 5, JS_BUILTIN_MATH_TRUNC, 1},
        {"random", 6, JS_BUILTIN_MATH_RANDOM, 0},
        {"asin", 4, JS_BUILTIN_MATH_ASIN, 1},
        {"acos", 4, JS_BUILTIN_MATH_ACOS, 1},
        {"atan", 4, JS_BUILTIN_MATH_ATAN, 1},
        {"atan2", 5, JS_BUILTIN_MATH_ATAN2, 2},
        {"cbrt", 4, JS_BUILTIN_MATH_CBR, 1},
        {"hypot", 5, JS_BUILTIN_MATH_HYPOT, 2},
        {"clz32", 5, JS_BUILTIN_MATH_CLZ32, 1},
        {"fround", 6, JS_BUILTIN_MATH_FROUND, 1},
        {"imul", 4, JS_BUILTIN_MATH_IMUL, 2},
        {"sinh", 4, JS_BUILTIN_MATH_SINH, 1},
        {"cosh", 4, JS_BUILTIN_MATH_COSH, 1},
        {"tanh", 4, JS_BUILTIN_MATH_TANH, 1},
        {"asinh", 5, JS_BUILTIN_MATH_ASINH, 1},
        {"acosh", 5, JS_BUILTIN_MATH_ACOSH, 1},
        {"atanh", 5, JS_BUILTIN_MATH_ATANH, 1},
        {"expm1", 5, JS_BUILTIN_MATH_EXPM1, 1},
        {"log1p", 5, JS_BUILTIN_MATH_LOG1P, 1},
    };
    for (int i = 0; i < (int)(sizeof(math_methods) / sizeof(math_methods[0])); i++) {
        if (prop->len == math_methods[i].len && strncmp(prop->chars, math_methods[i].name, prop->len) == 0) {
            return js_get_or_create_builtin(math_methods[i].id, math_methods[i].name, math_methods[i].pc);
        }
    }

    // fallback: resolve via prototype chain (Object.prototype properties)
    if (js_math_object.item != ITEM_NULL) {
        Item result = js_property_access(js_math_object, prop_name);
        if (result.item != ITEM_NULL) return result;
    }
    return ItemNull;
}

// array slice from index to end — used for rest destructuring
extern "C" Item js_array_slice_from(Item arr, Item start_item) {
    TypeId tid = get_type_id(arr);
    if (tid != LMD_TYPE_ARRAY) return js_array_new(0);
    Array* src = arr.array;
    int start = (int)js_get_number(start_item);
    if (start < 0) start = src->length + start;
    if (start < 0) start = 0;
    if (start >= src->length) return js_array_new(0);
    Item result = js_array_new(0);
    Array* dst = result.array;
    for (int i = start; i < src->length; i++) {
        js_array_push_item_direct(dst, src->items[i]);
    }
    return result;
}

// =============================================================================
// Prototype chain support
// =============================================================================

static const char PROTO_KEY[] = "__proto__";
static const int PROTO_KEY_LEN = 9;

// Set the prototype of an object (stores as __proto__ property on Map)
extern "C" void js_set_prototype(Item object, Item prototype) {
    if (get_type_id(object) != LMD_TYPE_MAP) return;
    if (get_type_id(prototype) != LMD_TYPE_MAP && prototype.item != ItemNull.item) return;
    // v16: Prevent circular prototype chains (ES spec §9.1.2)
    if (get_type_id(prototype) == LMD_TYPE_MAP) {
        Item p = prototype;
        int depth = 0;
        while (p.item != ItemNull.item && get_type_id(p) == LMD_TYPE_MAP && depth < 32) {
            if (p.map == object.map) {
                log_error("js_set_prototype: circular prototype chain detected, rejecting");
                return;
            }
            p = js_get_prototype(p);
            depth++;
        }
    }
    // P10d: use interned __proto__ key
    Item key = js_get_proto_key();
    js_property_set(object, key, prototype);
}

// helper: set __proto__ on a wrapper object to the constructor's prototype
static void js_wrapper_set_proto(Item obj, const char* ctor_name, int ctor_len) {
    Item cn = (Item){.item = s2it(heap_create_name(ctor_name, ctor_len))};
    Item ctor = js_get_constructor(cn);
    if (get_type_id(ctor) == LMD_TYPE_FUNC) {
        Item pk = (Item){.item = s2it(heap_create_name("prototype", 9))};
        Item proto = js_property_get(ctor, pk);
        if (get_type_id(proto) == LMD_TYPE_MAP) {
            js_set_prototype(obj, proto);
        }
    }
}

// Create a Number wrapper object: new Number(42) → {__class_name__: "Number", __primitiveValue__: 42}
extern "C" Item js_new_number_wrapper(Item arg) {
    Item obj = js_new_object();
    Item cn_key = (Item){.item = s2it(heap_create_name("__class_name__", 14))};
    Item cn_val = (Item){.item = s2it(heap_create_name("Number", 6))};
    js_property_set(obj, cn_key, cn_val);
    Item pv_key = (Item){.item = s2it(heap_create_name("__primitiveValue__", 18))};
    js_property_set(obj, pv_key, js_to_number(arg));
    js_wrapper_set_proto(obj, "Number", 6);
    return obj;
}

// Create a Boolean wrapper object
extern "C" Item js_new_boolean_wrapper(Item arg) {
    Item obj = js_new_object();
    Item cn_key = (Item){.item = s2it(heap_create_name("__class_name__", 14))};
    Item cn_val = (Item){.item = s2it(heap_create_name("Boolean", 7))};
    js_property_set(obj, cn_key, cn_val);
    Item pv_key = (Item){.item = s2it(heap_create_name("__primitiveValue__", 18))};
    js_property_set(obj, pv_key, js_to_boolean(arg));
    js_wrapper_set_proto(obj, "Boolean", 7);
    return obj;
}

// Create a String wrapper object
extern "C" Item js_new_string_wrapper(Item arg) {
    Item obj = js_new_object();
    Item cn_key = (Item){.item = s2it(heap_create_name("__class_name__", 14))};
    Item cn_val = (Item){.item = s2it(heap_create_name("String", 6))};
    js_property_set(obj, cn_key, cn_val);
    Item pv_key = (Item){.item = s2it(heap_create_name("__primitiveValue__", 18))};
    Item str_val = js_to_string(arg);
    js_property_set(obj, pv_key, str_val);
    // Also set length property
    String* s = it2s(str_val);
    int len = s ? (int)s->len : 0;
    Item len_key = (Item){.item = s2it(heap_create_name("length", 6))};
    Item len_val = (Item){.item = i2it(len)};
    js_property_set(obj, len_key, len_val);
    js_wrapper_set_proto(obj, "String", 6);
    return obj;
}

// ToObject conversion: wraps primitives into their corresponding wrapper objects
// ES spec 7.1.13 ToObject
extern "C" Item js_to_object(Item value) {
    TypeId type = get_type_id(value);
    if (type == LMD_TYPE_MAP || type == LMD_TYPE_ARRAY || type == LMD_TYPE_FUNC) return value;
    if (type == LMD_TYPE_BOOL) return js_new_boolean_wrapper(value);
    if (type == LMD_TYPE_INT || type == LMD_TYPE_INT64 || type == LMD_TYPE_FLOAT) return js_new_number_wrapper(value);
    if (type == LMD_TYPE_STRING) return js_new_string_wrapper(value);
    // null and undefined throw TypeError in spec, but return empty object here for safety
    return js_new_object();
}

// Mark a property as non-enumerable by setting __ne_<name> marker
extern "C" void js_mark_non_enumerable(Item object, Item name) {
    TypeId tid = get_type_id(object);
    if (tid != LMD_TYPE_MAP && tid != LMD_TYPE_FUNC) return;
    if (get_type_id(name) != LMD_TYPE_STRING) return;
    String* str = it2s(name);
    char ne_key[256];
    snprintf(ne_key, sizeof(ne_key), "__ne_%.*s", (int)str->len, str->chars);
    Item nk = (Item){.item = s2it(heap_create_name(ne_key, strlen(ne_key)))};
    js_property_set(object, nk, (Item){.item = b2it(true)});
}

// Mark a property as non-writable by setting __nw_<name> marker
extern "C" void js_mark_non_writable(Item object, Item name) {
    TypeId tid = get_type_id(object);
    if (tid != LMD_TYPE_MAP && tid != LMD_TYPE_FUNC) return;
    if (get_type_id(name) != LMD_TYPE_STRING) return;
    String* str = it2s(name);
    char nw_key[256];
    snprintf(nw_key, sizeof(nw_key), "__nw_%.*s", (int)str->len, str->chars);
    Item nk = (Item){.item = s2it(heap_create_name(nw_key, strlen(nw_key)))};
    js_property_set(object, nk, (Item){.item = b2it(true)});
}

// Mark all user-visible properties on an object as non-enumerable (used for class prototypes)
extern "C" void js_mark_all_non_enumerable(Item object) {
    if (get_type_id(object) != LMD_TYPE_MAP) return;
    Map* m = object.map;
    if (!m || !m->type) return;
    TypeMap* tm = (TypeMap*)m->type;
    ShapeEntry* entry = tm->shape;
    while (entry) {
        if (!entry->name) { entry = entry->next; continue; }
        const char* name = entry->name->str;
        int name_len = (int)entry->name->length;
        // skip internal properties  
        if (name_len >= 2 && name[0] == '_' && name[1] == '_') {
            entry = entry->next;
            continue;
        }
        char ne_key[256];
        snprintf(ne_key, sizeof(ne_key), "__ne_%.*s", name_len, name);
        Item nk = (Item){.item = s2it(heap_create_name(ne_key, strlen(ne_key)))};
        js_property_set(object, nk, (Item){.item = b2it(true)});
        entry = entry->next;
    }
}

// v12: Link a proto marker to the base constructor's .prototype object
// so that inherited properties (e.g. Error.stack) are accessible through __proto__ chain.
// proto_marker is the __proto__ node created for instanceof resolution.
// base_ctor is the runtime value of the base constructor function.
extern "C" void js_link_base_prototype(Item proto_marker, Item base_ctor) {
    if (get_type_id(proto_marker) != LMD_TYPE_MAP) return;
    // Look up base_ctor.prototype
    Item proto_key = (Item){.item = s2it(heap_create_name("prototype", 9))};
    Item base_proto = js_property_get(base_ctor, proto_key);
    if (base_proto.item != ItemNull.item && get_type_id(base_proto) == LMD_TYPE_MAP) {
        js_set_prototype(proto_marker, base_proto);
    }
}

// Get the prototype of an object (read __proto__ property)
// P10d: uses interned key + first-match lookup (no heap allocation per call)
extern "C" Item js_get_prototype(Item object) {
    if (get_type_id(object) != LMD_TYPE_MAP) return ItemNull;
    Map* m = object.map;
    // P10f+P10d: direct first-match lookup with interned key
    return js_map_get_fast(m, PROTO_KEY, PROTO_KEY_LEN);
}

// Walk the prototype chain to find a property
// P10f: uses first-match lookup on each prototype level
extern "C" Item js_prototype_lookup(Item object, Item property) {
    // first check own properties (skip — caller already checked)
    // walk up the chain via __proto__
    Item proto = js_get_prototype(object);
    int depth = 0;
    // P10f: extract key string once for first-match lookup
    const char* key_str = NULL;
    int key_len = 0;
    if (get_type_id(property) == LMD_TYPE_STRING) {
        String* s = it2s(property);
        key_str = s->chars;
        key_len = (int)s->len;
    }
    while (proto.item != ItemNull.item && get_type_id(proto) == LMD_TYPE_MAP && depth < 32) {
        Item result;
        if (key_str) {
            result = js_map_get_fast(proto.map, key_str, key_len);
        } else {
            result = map_get(proto.map, property);
        }
        if (result.item != ItemNull.item && !js_is_deleted_sentinel(result)) return result;
        proto = js_get_prototype(proto);
        depth++;
    }
    return ItemNull;
}

// =============================================================================
// v12: Object rest destructuring
// =============================================================================

extern "C" Item js_object_rest(Item src, Item* exclude_keys, int exclude_count) {
    if (get_type_id(src) != LMD_TYPE_MAP) return js_new_object();
    Map* m = it2map(src);
    TypeMap* tm = (TypeMap*)m->type;
    if (!tm) return js_new_object();
    ShapeEntry* e = tm->shape;
    Item rest = js_new_object();
    while (e) {
        if (e->name) {
            bool excluded = false;
            for (int i = 0; i < exclude_count; i++) {
                String* ek = it2s(exclude_keys[i]);
                if (ek && e->name->length == ek->len && memcmp(e->name->str, ek->chars, ek->len) == 0) {
                    excluded = true;
                    break;
                }
            }
            if (!excluded) {
                Item val = _map_read_field(e, m->data);
                if (!js_is_deleted_sentinel(val)) {
                    String* key_str = heap_create_name(e->name->str, e->name->length);
                    js_property_set(rest, (Item){.item = s2it(key_str)}, val);
                }
            }
        }
        e = e->next;
    }
    return rest;
}

// =============================================================================
// v15: Generator Runtime
// =============================================================================

// Generator state: the MIR-compiled state machine function takes
// (Item* env, Item input, int64_t state) and returns a 2-element array
// [value, next_state] where next_state == -1 means done.
struct JsGenerator {
    TypeId type_id;           // LMD_TYPE_MAP (treated as object)
    void*  state_fn;          // compiled state machine function pointer
    Item*  env;               // captured closure variables (params + locals)
    int    env_size;
    int64_t state;            // current state index (0=initial, -1=done)
    bool   done;
    Item   delegate;          // active yield* delegate iterator (ItemNull when none)
    int64_t delegate_resume;  // state to resume after delegate is exhausted
};

#define JS_MAX_GENERATORS 4096
static JsGenerator js_generators[JS_MAX_GENERATORS];
static int js_generator_count = 0;

// Helper: create {value, done} iterator result object
static Item js_make_iter_result(Item value, bool done) {
    Item result = js_new_object();
    String* val_key = heap_create_name("value", 5);
    String* done_key = heap_create_name("done", 4);
    js_property_set(result, (Item){.item = s2it(val_key)}, value);
    js_property_set(result, (Item){.item = s2it(done_key)}, (Item){.item = b2it(done)});
    return result;
}

// v15: Create a 2-element array [value, next_state] for state machine returns
extern "C" Item js_gen_yield_result(Item value, int64_t next_state) {
    Item arr = js_array_new(2);
    arr.array->items[0] = value;
    arr.array->items[1] = (Item){.item = i2it(next_state)};
    return arr;
}

// yield* delegation: create 3-element array [iterable, resume_state, 1(flag)]
extern "C" Item js_gen_yield_delegate_result(Item iterable, int64_t resume_state) {
    Item arr = js_array_new(3);
    arr.array->items[0] = iterable;
    arr.array->items[1] = (Item){.item = i2it(resume_state)};
    arr.array->items[2] = (Item){.item = i2it(1)};  // delegation flag
    return arr;
}

extern "C" Item js_generator_create(void* func_ptr, Item* env, int env_size) {
    // Try to recycle a completed generator slot first
    int idx = -1;
    for (int i = 0; i < js_generator_count; i++) {
        if (js_generators[i].done) {
            idx = i;
            break;
        }
    }
    if (idx < 0) {
        if (js_generator_count >= JS_MAX_GENERATORS) {
            log_error("generator: exceeded max generators (%d)", JS_MAX_GENERATORS);
            return ItemNull;
        }
        idx = js_generator_count++;
    }
    JsGenerator* gen = &js_generators[idx];
    gen->type_id = LMD_TYPE_MAP;
    gen->state_fn = func_ptr;
    gen->env = env;
    gen->env_size = env_size;
    gen->state = 0;
    gen->done = false;
    gen->delegate = ItemNull;
    gen->delegate_resume = -1;

    // Create a map object that references this generator via a hidden index
    Item obj = js_new_object();
    String* gen_key = heap_create_name("__gen_idx", 9);
    js_property_set(obj, (Item){.item = s2it(gen_key)}, (Item){.item = i2it(idx)});
    // v41: Set Symbol.toStringTag = "Generator" for Object.prototype.toString
    Item tag_key = (Item){.item = s2it(heap_create_name("__sym_4", 7))};
    Item tag_val = (Item){.item = s2it(heap_create_name("Generator", 9))};
    js_property_set(obj, tag_key, tag_val);

    return obj;
}

static JsGenerator* js_get_generator(Item gen_obj) {
    if (get_type_id(gen_obj) != LMD_TYPE_MAP) return NULL;
    String* gen_key = heap_create_name("__gen_idx", 9);
    Item idx_item = js_property_get(gen_obj, (Item){.item = s2it(gen_key)});
    if (get_type_id(idx_item) != LMD_TYPE_INT) return NULL;
    int64_t idx = it2i(idx_item);
    if (idx < 0 || idx >= js_generator_count) return NULL;
    return &js_generators[idx];
}

extern "C" Item js_generator_next(Item generator, Item input) {
    JsGenerator* gen = js_get_generator(generator);
    if (!gen) {
        log_error("generator_next: invalid generator object");
        return js_make_iter_result(make_js_undefined(), true);
    }

    if (gen->done) {
        return js_make_iter_result(make_js_undefined(), true);
    }

    // If we have an active delegate (from yield*), drain it first
    if (get_type_id(gen->delegate) != LMD_TYPE_NULL) {
        Item del_result = js_generator_next(gen->delegate, input);
        // Check if delegate is done
        if (get_type_id(del_result) == LMD_TYPE_MAP) {
            String* done_key = heap_create_name("done", 4);
            Item done_val = js_property_get(del_result, (Item){.item = s2it(done_key)});
            if (get_type_id(done_val) == LMD_TYPE_BOOL && it2b(done_val)) {
                // Delegate exhausted — clear it and resume our state machine
                gen->delegate = ItemNull;
                gen->state = gen->delegate_resume;
                gen->delegate_resume = -1;
                // Fall through to call state machine at resumed state
            } else {
                // Delegate still producing — return its result
                return del_result;
            }
        }
    }

    // Call the state machine: fn(env, input, state) -> [value, next_state]
    // The state machine returns {value, next_state} as a 2-element array
    // If next_state == -1, the generator is done
    // If next_state == -3, this is yield* delegation: value is the iterable
    typedef Item (*GenFn)(Item*, Item, int64_t);

    Item result = ((GenFn)gen->state_fn)(gen->env, input, gen->state);

    if (get_type_id(result) == LMD_TYPE_ARRAY) {
        Array* arr = result.array;
        Item value = (arr->length > 0) ? arr->items[0] : ItemNull;
        int64_t next_state = -1;
        if (arr->length > 1 && get_type_id(arr->items[1]) == LMD_TYPE_INT) {
            next_state = it2i(arr->items[1]);
        }

        // Check for yield* delegation marker (3-element array with flag)
        if (arr->length > 2 && get_type_id(arr->items[2]) == LMD_TYPE_INT && it2i(arr->items[2]) == 1) {
            // value is the iterable to delegate to, next_state is the resume state
            gen->delegate = value;
            gen->delegate_resume = next_state;
            // Immediately start draining the delegate
            return js_generator_next(generator, make_js_undefined());
        }

        if (next_state < 0) {
            gen->done = true;
            gen->state = -1;
            return js_make_iter_result(value, true);
        } else {
            gen->state = next_state;
            return js_make_iter_result(value, false);
        }
    }

    // Fallback: function returned a plain value (final return)
    gen->done = true;
    gen->state = -1;
    return js_make_iter_result(result, true);
}

extern "C" Item js_generator_return(Item generator, Item value) {
    JsGenerator* gen = js_get_generator(generator);
    if (gen) {
        gen->done = true;
        gen->state = -1;
    }
    return js_make_iter_result(value, true);
}

extern "C" Item js_generator_throw(Item generator, Item error) {
    JsGenerator* gen = js_get_generator(generator);
    if (gen) {
        gen->done = true;
        gen->state = -1;
    }
    // Throw the error via the JS exception mechanism
    js_throw_value(error);
    return js_make_iter_result(make_js_undefined(), true);
}

// v15: Check if an object is a generator (has __gen_idx property)
extern "C" bool js_is_generator(Item obj) {
    if (get_type_id(obj) != LMD_TYPE_MAP) return false;
    String* gen_key = heap_create_name("__gen_idx", 9);
    Item idx_item = js_property_get(obj, (Item){.item = s2it(gen_key)});
    return get_type_id(idx_item) == LMD_TYPE_INT;
}

// =============================================================================
// v29: Lazy iteration protocol for for-of
// GetIterator(iterable) → returns the iterator object.
// For arrays: returns a synthetic array iterator (lightweight wrapper).
// For generators: returns the generator itself.
// For objects with [Symbol.iterator]: calls it and returns the result.
// =============================================================================

// Get the iterator for an iterable (GetIterator, ES spec §7.4.1)
extern "C" Item js_get_iterator(Item iterable) {
    TypeId tid = get_type_id(iterable);

    // Arrays: wrap in array iterator
    if (tid == LMD_TYPE_ARRAY) {
        Item iter = js_object_create(ItemNull);
        js_property_set(iter, (Item){.item = s2it(heap_create_name("__arr__", 7))}, iterable);
        js_property_set(iter, (Item){.item = s2it(heap_create_name("__idx__", 7))}, (Item){.item = i2it(0)});
        return iter;
    }

    // Strings: wrap in string iterator
    if (tid == LMD_TYPE_STRING) {
        Item iter = js_object_create(ItemNull);
        js_property_set(iter, (Item){.item = s2it(heap_create_name("__str__", 7))}, iterable);
        js_property_set(iter, (Item){.item = s2it(heap_create_name("__idx__", 7))}, (Item){.item = i2it(0)});
        return iter;
    }

    // Generators: return as-is (they implement iterator protocol natively)
    if (js_is_generator(iterable)) {
        return iterable;
    }

    // Typed arrays: wrap in typed array iterator
    if (js_is_typed_array(iterable)) {
        Item iter = js_object_create(ItemNull);
        js_property_set(iter, (Item){.item = s2it(heap_create_name("__tarr__", 8))}, iterable);
        js_property_set(iter, (Item){.item = s2it(heap_create_name("__idx__", 7))}, (Item){.item = i2it(0)});
        return iter;
    }

    // Map/Set collections
    if (tid == LMD_TYPE_MAP) {
        JsCollectionData* cd = js_get_collection_data(iterable);
        if (cd) {
            // Drain to array and wrap in array iterator (collections are small enough)
            Item arr = js_iterable_to_array(iterable);
            Item iter = js_object_create(ItemNull);
            js_property_set(iter, (Item){.item = s2it(heap_create_name("__arr__", 7))}, arr);
            js_property_set(iter, (Item){.item = s2it(heap_create_name("__idx__", 7))}, (Item){.item = i2it(0)});
            return iter;
        }

        // Check for [Symbol.iterator]()
        Item iter_factory_key = (Item){.item = s2it(heap_create_name("__sym_1", 7))};
        Item iter_factory = js_property_get(iterable, iter_factory_key);
        if (get_type_id(iter_factory) == LMD_TYPE_FUNC) {
            Item iterator = js_call_function(iter_factory, iterable, NULL, 0);
            if (js_check_exception()) return ItemNull;
            return iterator;
        }

        // Check for .next() method (already an iterator)
        String* next_key = heap_create_name("next", 4);
        Item next_fn = js_property_get(iterable, (Item){.item = s2it(next_key)});
        if (get_type_id(next_fn) == LMD_TYPE_FUNC) {
            return iterable;
        }
    }
    if (tid == LMD_TYPE_ELEMENT) {
        // Check for [Symbol.iterator]()
        Item iter_factory_key = (Item){.item = s2it(heap_create_name("__sym_1", 7))};
        Item iter_factory = js_property_get(iterable, iter_factory_key);
        if (get_type_id(iter_factory) == LMD_TYPE_FUNC) {
            Item iterator = js_call_function(iter_factory, iterable, NULL, 0);
            if (js_check_exception()) return ItemNull;
            return iterator;
        }
    }

    // Fallback: return as-is
    return iterable;
}

// IteratorStep: call iterator.next(), return result {done, value}
// Returns JS_ITER_DONE_SENTINEL on completion (done=true) or the value on success.
// The sentinel is a unique bit pattern (type tag 0x7F) that cannot collide with
// any valid JS value including null, undefined, false, 0, or empty string.
extern "C" Item js_iterator_step(Item iterator) {
    // Synthetic array iterator
    bool has_arr = false;
    Item arr_val = js_map_get_fast(iterator.map, "__arr__", 7, &has_arr);
    if (has_arr) {
        bool has_idx = false;
        Item idx_val = js_map_get_fast(iterator.map, "__idx__", 7, &has_idx);
        int idx = has_idx ? (int)it2i(idx_val) : 0;
        int len = (get_type_id(arr_val) == LMD_TYPE_ARRAY) ? arr_val.array->length : 0;
        if (idx >= len) return (Item){.item = JS_ITER_DONE_SENTINEL};  // done
        Item elem = js_property_access(arr_val, (Item){.item = i2it(idx)});
        js_property_set(iterator, (Item){.item = s2it(heap_create_name("__idx__", 7))}, (Item){.item = i2it(idx + 1)});
        return elem;
    }

    // Synthetic string iterator
    bool has_str = false;
    Item str_val = js_map_get_fast(iterator.map, "__str__", 7, &has_str);
    if (has_str && get_type_id(str_val) == LMD_TYPE_STRING) {
        bool has_idx = false;
        Item idx_val = js_map_get_fast(iterator.map, "__idx__", 7, &has_idx);
        int idx = has_idx ? (int)it2i(idx_val) : 0;
        String* str = it2s(str_val);
        if (idx >= (int)str->len) return (Item){.item = JS_ITER_DONE_SENTINEL};  // done
        String* ch = heap_create_name(str->chars + idx, 1);
        js_property_set(iterator, (Item){.item = s2it(heap_create_name("__idx__", 7))}, (Item){.item = i2it(idx + 1)});
        return (Item){.item = s2it(ch)};
    }

    // Synthetic typed array iterator
    bool has_tarr = false;
    Item tarr_val = js_map_get_fast(iterator.map, "__tarr__", 8, &has_tarr);
    if (has_tarr) {
        bool has_idx = false;
        Item idx_val = js_map_get_fast(iterator.map, "__idx__", 7, &has_idx);
        int idx = has_idx ? (int)it2i(idx_val) : 0;
        int len = js_typed_array_length(tarr_val);
        if (idx >= len) return (Item){.item = JS_ITER_DONE_SENTINEL};  // done
        Item elem = js_typed_array_get(tarr_val, (Item){.item = i2it(idx)});
        js_property_set(iterator, (Item){.item = s2it(heap_create_name("__idx__", 7))}, (Item){.item = i2it(idx + 1)});
        return elem;
    }

    // Generator: call js_generator_next
    if (js_is_generator(iterator)) {
        Item result = js_generator_next(iterator, make_js_undefined());
        if (js_check_exception()) return (Item){.item = JS_ITER_DONE_SENTINEL};
        String* done_key = heap_create_name("done", 4);
        Item done_item = js_property_get(result, (Item){.item = s2it(done_key)});
        if (get_type_id(done_item) == LMD_TYPE_BOOL && it2b(done_item)) return (Item){.item = JS_ITER_DONE_SENTINEL};
        String* val_key = heap_create_name("value", 5);
        return js_property_get(result, (Item){.item = s2it(val_key)});
    }

    // Generic iterator: call .next()
    if (get_type_id(iterator) == LMD_TYPE_MAP || get_type_id(iterator) == LMD_TYPE_ELEMENT) {
        String* next_key = heap_create_name("next", 4);
        Item next_fn = js_property_get(iterator, (Item){.item = s2it(next_key)});
        if (get_type_id(next_fn) == LMD_TYPE_FUNC) {
            Item result = js_call_function(next_fn, iterator, NULL, 0);
            if (js_check_exception()) return (Item){.item = JS_ITER_DONE_SENTINEL};
            String* done_key = heap_create_name("done", 4);
            Item done_item = js_property_get(result, (Item){.item = s2it(done_key)});
            if (get_type_id(done_item) == LMD_TYPE_BOOL && it2b(done_item)) return (Item){.item = JS_ITER_DONE_SENTINEL};
            String* val_key = heap_create_name("value", 5);
            return js_property_get(result, (Item){.item = s2it(val_key)});
        }
    }

    return (Item){.item = JS_ITER_DONE_SENTINEL};  // done
}

// IteratorClose: call iterator.return() if it exists (ES spec §7.4.6)
extern "C" Item js_iterator_close(Item iterator) {
    // Generators: call js_generator_return
    if (js_is_generator(iterator)) {
        // Send a return signal to the generator
        js_generator_return(iterator, make_js_undefined());
        return make_js_undefined();
    }

    // Generic iterator: call .return() if available
    TypeId tid = get_type_id(iterator);
    if (tid == LMD_TYPE_MAP || tid == LMD_TYPE_ELEMENT) {
        String* return_key = heap_create_name("return", 6);
        Item return_fn = js_property_get(iterator, (Item){.item = s2it(return_key)});
        if (get_type_id(return_fn) == LMD_TYPE_FUNC) {
            js_call_function(return_fn, iterator, NULL, 0);
        }
    }
    return make_js_undefined();
}

// v15: Convert an iterable to an array. If it's a generator, drain it.
// If it's already an array, return it as-is.
extern "C" Item js_iterable_to_array(Item iterable) {
    if (get_type_id(iterable) == LMD_TYPE_ARRAY) return iterable;

    // Typed arrays (Uint8Array, Int32Array, etc.): convert to plain array of boxed numbers
    if (js_is_typed_array(iterable)) {
        int len = js_typed_array_length(iterable);
        Item arr = js_array_new(0);
        for (int i = 0; i < len; i++) {
            Item elem = js_typed_array_get(iterable, (Item){.item = i2it(i)});
            js_array_push_item_direct(arr.array, elem);
        }
        return arr;
    }

    // Check if it's a generator object
    if (js_is_generator(iterable)) {
        Item arr = js_array_new(0);
        for (int safety = 0; safety < 100000; safety++) {
            Item result = js_generator_next(iterable, make_js_undefined());
            // Extract .done and .value from result
            String* done_key = heap_create_name("done", 4);
            Item done_item = js_property_get(result, (Item){.item = s2it(done_key)});
            if (get_type_id(done_item) == LMD_TYPE_BOOL && it2b(done_item)) break;
            String* val_key = heap_create_name("value", 5);
            Item value = js_property_get(result, (Item){.item = s2it(val_key)});
            js_array_push_item_direct(arr.array, value);
        }
        return arr;
    }

    // Check if it's a Map/Set collection — convert to array directly
    if (get_type_id(iterable) == LMD_TYPE_MAP) {
        JsCollectionData* cd = js_get_collection_data(iterable);
        if (cd) {
            size_t count = hashmap_count(cd->hmap);
            Item arr = js_array_new((int)count);
            JsCollectionOrderNode* node = cd->order_head;
            int idx = 0;
            if (cd->type == JS_COLLECTION_MAP) {
                // Map: array of [key, value] pairs
                while (node) {
                    Item pair = js_array_new(2);
                    js_array_set_int(pair, 0, node->key);
                    js_array_set_int(pair, 1, node->value);
                    js_array_set_int(arr, idx, pair);
                    idx++;
                    node = node->next;
                }
            } else {
                // Set: array of values
                while (node) {
                    js_array_set_int(arr, idx, node->key);
                    idx++;
                    node = node->next;
                }
            }
            return arr;
        }
    }

    // String: split into individual characters
    if (get_type_id(iterable) == LMD_TYPE_STRING) {
        String* str = it2s(iterable);
        if (str) {
            Item arr = js_array_new(0);
            for (int i = 0; i < (int)str->len; i++) {
                String* ch = heap_create_name(str->chars + i, 1);
                Item ch_item = (Item){.item = s2it(ch)};
                js_array_push_item_direct(arr.array, ch_item);
            }
            return arr;
        }
    }

    // Check for iterator protocol: object with .next() method
    if (get_type_id(iterable) == LMD_TYPE_MAP) {
        // Already handled above - this is for other objects with next()
    }
    TypeId tid = get_type_id(iterable);
    if (tid == LMD_TYPE_MAP || tid == LMD_TYPE_ELEMENT) {
        // Check for [Symbol.iterator]() which returns an iterator (JS iterable protocol)
        // Symbol.iterator has well-known ID=1, stored as property key "__sym_1"
        Item iter_factory_key = (Item){.item = s2it(heap_create_name("__sym_1", 7))};
        Item iter_factory = js_property_get(iterable, iter_factory_key);
        if (get_type_id(iter_factory) == LMD_TYPE_FUNC) {
            Item iterator = js_call_function(iter_factory, iterable, NULL, 0);
            // if [Symbol.iterator]() returned an array directly, use it as-is
            if (get_type_id(iterator) == LMD_TYPE_ARRAY) return iterator;
            // drain the iterator (generator or object with next())
            if (js_is_generator(iterator)) {
                Item arr = js_array_new(0);
                for (int safety = 0; safety < 100000; safety++) {
                    Item result = js_generator_next(iterator, make_js_undefined());
                    String* done_key = heap_create_name("done", 4);
                    Item done_item = js_property_get(result, (Item){.item = s2it(done_key)});
                    if (get_type_id(done_item) == LMD_TYPE_BOOL && it2b(done_item)) break;
                    String* val_key = heap_create_name("value", 5);
                    Item value = js_property_get(result, (Item){.item = s2it(val_key)});
                    js_array_push_item_direct(arr.array, value);
                }
                return arr;
            }
            if (get_type_id(iterator) == LMD_TYPE_MAP || get_type_id(iterator) == LMD_TYPE_ELEMENT) {
                // plain iterator: call .next() in a loop
                String* next_key2 = heap_create_name("next", 4);
                Item next_fn2 = js_property_get(iterator, (Item){.item = s2it(next_key2)});
                if (get_type_id(next_fn2) == LMD_TYPE_FUNC) {
                    Item arr = js_array_new(0);
                    for (int safety = 0; safety < 100000; safety++) {
                        Item result = js_call_function(next_fn2, iterator, NULL, 0);
                        String* done_key = heap_create_name("done", 4);
                        Item done_item = js_property_get(result, (Item){.item = s2it(done_key)});
                        if (get_type_id(done_item) == LMD_TYPE_BOOL && it2b(done_item)) break;
                        String* val_key = heap_create_name("value", 5);
                        Item value = js_property_get(result, (Item){.item = s2it(val_key)});
                        js_array_push_item_direct(arr.array, value);
                    }
                    return arr;
                }
            }
        }
        // Check if object has a next() method (iterator protocol — already is an iterator)
        String* next_key = heap_create_name("next", 4);
        Item next_fn = js_property_get(iterable, (Item){.item = s2it(next_key)});
        if (get_type_id(next_fn) == LMD_TYPE_FUNC) {
            Item arr = js_array_new(0);
            for (int safety = 0; safety < 100000; safety++) {
                Item result = js_call_function(next_fn, iterable, NULL, 0);
                String* done_key = heap_create_name("done", 4);
                Item done_item = js_property_get(result, (Item){.item = s2it(done_key)});
                if (get_type_id(done_item) == LMD_TYPE_BOOL && it2b(done_item)) break;
                String* val_key = heap_create_name("value", 5);
                Item value = js_property_get(result, (Item){.item = s2it(val_key)});
                js_array_push_item_direct(arr.array, value);
            }
            return arr;
        }
    }

    // Fallback: return as-is (might be a map treated as iterable)
    return iterable;
}

// =============================================================================
// v14: Promise Runtime
// =============================================================================

enum JsPromiseState {
    JS_PROMISE_PENDING,
    JS_PROMISE_FULFILLED,
    JS_PROMISE_REJECTED,
};

struct JsPromise {
    TypeId type_id;                // LMD_TYPE_MAP
    JsPromiseState state;
    Item result;                   // fulfilled value or rejection reason
    Item on_fulfilled[8];          // then() callbacks (max chain depth)
    Item on_rejected[8];
    Item next_promise[8];          // chained promise for each then() handler
    bool is_finally[8];            // true if handler[i] is a finally handler
    int  then_count;
};

#define JS_MAX_PROMISES 1024
static JsPromise js_promises[JS_MAX_PROMISES];
static int js_promise_count = 0;

static JsPromise* js_alloc_promise() {
    if (js_promise_count >= JS_MAX_PROMISES) {
        log_error("promise: exceeded max promises (%d)", JS_MAX_PROMISES);
        return NULL;
    }
    if (js_promise_count >= JS_MAX_PROMISES - 16 && (js_promise_count % 16 == 0)) {
        log_info("promise: approaching capacity limit (%d/%d)", js_promise_count, JS_MAX_PROMISES);
    }
    JsPromise* p = &js_promises[js_promise_count++];
    p->type_id = LMD_TYPE_MAP;
    p->state = JS_PROMISE_PENDING;
    p->result = ItemNull;
    p->then_count = 0;
    memset(p->on_fulfilled, 0, sizeof(p->on_fulfilled));
    memset(p->on_rejected, 0, sizeof(p->on_rejected));
    memset(p->next_promise, 0, sizeof(p->next_promise));
    memset(p->is_finally, 0, sizeof(p->is_finally));
    return p;
}

// Wrapper functions for Promise.prototype.then/catch/finally
// First arg is the bound promise (via js_bind_function), remaining are user args
static Item js_promise_then_bound(Item promise, Item on_fulfilled, Item on_rejected) {
    return js_promise_then(promise, on_fulfilled, on_rejected);
}
static Item js_promise_catch_bound(Item promise, Item on_rejected) {
    return js_promise_catch(promise, on_rejected);
}
static Item js_promise_finally_bound(Item promise, Item on_finally) {
    return js_promise_finally(promise, on_finally);
}

static Item js_promise_to_item(JsPromise* p) {
    // Create a map object that holds a reference to the promise by index
    int idx = (int)(p - js_promises);
    Item obj = js_new_object();
    String* key = heap_create_name("__promise_idx", 13);
    js_property_set(obj, (Item){.item = s2it(key)}, (Item){.item = i2it(idx)});
    // Set __class_name__ for instanceof Promise support
    String* cn_key = heap_create_name("__class_name__", 14);
    js_property_set(obj, (Item){.item = s2it(cn_key)}, (Item){.item = s2it(heap_create_name("Promise", 7))});
    // Add then, catch, finally as bound method properties
    // bind prepends obj as first arg so wrapper receives (promise, ...userArgs)
    Item then_fn = js_new_function((void*)js_promise_then_bound, 3);
    Item then_bound = js_bind_function(then_fn, ItemNull, &obj, 1);
    js_property_set(obj, (Item){.item = s2it(heap_create_name("then", 4))}, then_bound);
    Item catch_fn = js_new_function((void*)js_promise_catch_bound, 2);
    Item catch_bound = js_bind_function(catch_fn, ItemNull, &obj, 1);
    js_property_set(obj, (Item){.item = s2it(heap_create_name("catch", 5))}, catch_bound);
    Item finally_fn = js_new_function((void*)js_promise_finally_bound, 2);
    Item finally_bound = js_bind_function(finally_fn, ItemNull, &obj, 1);
    js_property_set(obj, (Item){.item = s2it(heap_create_name("finally", 7))}, finally_bound);
    return obj;
}

static JsPromise* js_get_promise(Item promise_obj) {
    if (get_type_id(promise_obj) != LMD_TYPE_MAP) return NULL;
    String* key = heap_create_name("__promise_idx", 13);
    Item idx_item = js_property_get(promise_obj, (Item){.item = s2it(key)});
    if (get_type_id(idx_item) != LMD_TYPE_INT) return NULL;
    int64_t idx = it2i(idx_item);
    if (idx < 0 || idx >= js_promise_count) return NULL;
    return &js_promises[idx];
}

// Forward declaration — js_promise_settle is called recursively from microtask runner
static void js_promise_settle(JsPromise* p, JsPromiseState state, Item result);

// Forward declarations for promise microtask helpers
static Item js_promise_microtask_resolve(Item next_promise_item, Item value);
static Item js_promise_microtask_reject(Item next_promise_item, Item reason);

// Microtask runner for promise then() handlers.
// Called with 3 bound args: handler, result, next_promise_item.
// Calls handler(result), then settles next_promise with the return value.
static Item js_promise_microtask_run(Item handler, Item result, Item next_promise_item) {
    Item args[1] = {result};
    Item handler_result = js_call_function(handler, ItemNull, args, 1);

    JsPromise* next = js_get_promise(next_promise_item);
    if (next) {
        JsPromise* returned_p = js_get_promise(handler_result);
        if (returned_p) {
            // handler returned a promise — chain resolution
            if (returned_p->state != JS_PROMISE_PENDING) {
                js_promise_settle(next, returned_p->state, returned_p->result);
            } else {
                // pending: register then handler to settle next when returned resolves
                Item next_item = next_promise_item;
                if (returned_p->then_count < 8) {
                    Item resolve_fn = js_new_function((void*)js_promise_microtask_resolve, 2);
                    Item reject_fn = js_new_function((void*)js_promise_microtask_reject, 2);
                    returned_p->on_fulfilled[returned_p->then_count] = js_bind_function(resolve_fn, ItemNull, &next_item, 1);
                    returned_p->on_rejected[returned_p->then_count] = js_bind_function(reject_fn, ItemNull, &next_item, 1);
                    returned_p->next_promise[returned_p->then_count] = ItemNull; // these are direct settlers, no further chain
                    returned_p->then_count++;
                }
            }
        } else {
            js_promise_settle(next, JS_PROMISE_FULFILLED, handler_result);
        }
    }
    return ItemNull;
}

// Microtask runner for finally() handlers.
// Called with 4 bound args: handler, next_promise_item, state_item, original_result.
static Item js_promise_finally_microtask_run(Item handler, Item next_promise_item, Item state_item, Item original_result) {
    js_call_function(handler, ItemNull, NULL, 0);
    JsPromise* next = js_get_promise(next_promise_item);
    if (next) {
        JsPromiseState orig_state = (JsPromiseState)it2i(state_item);
        js_promise_settle(next, orig_state, original_result);
    }
    return ItemNull;
}

// Bound resolve/reject settle functions for promise chaining.
// When a then() handler returns a pending promise, these are registered as then
// handlers on that returned promise. When it settles, they settle the next promise.
static Item js_promise_microtask_resolve(Item next_promise_item, Item value) {
    JsPromise* next = js_get_promise(next_promise_item);
    if (next) js_promise_settle(next, JS_PROMISE_FULFILLED, value);
    return ItemNull;
}

static Item js_promise_microtask_reject(Item next_promise_item, Item reason) {
    JsPromise* next = js_get_promise(next_promise_item);
    if (next) js_promise_settle(next, JS_PROMISE_REJECTED, reason);
    return ItemNull;
}

// Enqueue a promise handler as a microtask with proper chaining.
// Creates a bound thunk: js_promise_microtask_run(handler, result, next_promise_item)
static void js_promise_enqueue_handler(Item handler, Item result, Item next_promise_item) {
    Item runner_fn = js_new_function((void*)js_promise_microtask_run, 3);
    Item bound_args[3] = {handler, result, next_promise_item};
    Item thunk = js_bind_function(runner_fn, ItemNull, bound_args, 3);
    js_microtask_enqueue(thunk);
}

// Enqueue a finally handler as a microtask.
static void js_promise_enqueue_finally(Item handler, Item next_promise_item, JsPromiseState state, Item result) {
    Item runner_fn = js_new_function((void*)js_promise_finally_microtask_run, 4);
    Item bound_args[4] = {handler, next_promise_item, (Item){.item = i2it((int64_t)state)}, result};
    Item thunk = js_bind_function(runner_fn, ItemNull, bound_args, 4);
    js_microtask_enqueue(thunk);
}

static void js_promise_settle(JsPromise* p, JsPromiseState state, Item result) {
    if (p->state != JS_PROMISE_PENDING) return; // already settled

    p->state = state;
    p->result = result;

    // Schedule handlers as microtasks (per ECMAScript spec)
    for (int i = 0; i < p->then_count; i++) {
        Item handler = (state == JS_PROMISE_FULFILLED) ? p->on_fulfilled[i] : p->on_rejected[i];
        Item next_item = p->next_promise[i];

        if (p->is_finally[i]) {
            // finally handler: called with 0 args, passes through original result
            if (get_type_id(handler) == LMD_TYPE_FUNC) {
                js_promise_enqueue_finally(handler, next_item, state, result);
            } else {
                JsPromise* next = js_get_promise(next_item);
                if (next) js_promise_settle(next, state, result);
            }
        } else if (get_type_id(handler) == LMD_TYPE_FUNC) {
            if (get_type_id(next_item) == LMD_TYPE_MAP) {
                // has a chained next promise — enqueue with chaining
                js_promise_enqueue_handler(handler, result, next_item);
            } else {
                // direct handler (e.g. from chaining resolution), no next promise
                Item runner_fn = js_new_function((void*)js_promise_microtask_run, 3);
                Item bound_args[3] = {handler, result, ItemNull};
                Item thunk = js_bind_function(runner_fn, ItemNull, bound_args, 3);
                js_microtask_enqueue(thunk);
            }
        } else {
            // no handler for this state — propagate to next promise directly
            JsPromise* next = js_get_promise(next_item);
            if (next) {
                js_promise_settle(next, state, result);
            }
        }
    }
}

// Resolve/reject callbacks for promise executors.
// Each callback is bound (via js_bind_function) to its promise index,
// so these work even when called asynchronously (e.g. in setTimeout).
static Item js_resolve_callback(Item promise_idx_item, Item value) {
    int64_t idx = it2i(promise_idx_item);
    if (idx >= 0 && idx < js_promise_count) {
        JsPromise* p = &js_promises[idx];
        // if value is a promise, chain resolution
        JsPromise* thenable = js_get_promise(value);
        if (thenable) {
            if (thenable->state != JS_PROMISE_PENDING) {
                js_promise_settle(p, thenable->state, thenable->result);
            } else {
                // register then on thenable to forward settlement
                Item p_item = js_promise_to_item(p);
                if (thenable->then_count < 8) {
                    Item resolve_fn = js_new_function((void*)js_promise_microtask_resolve, 2);
                    Item reject_fn = js_new_function((void*)js_promise_microtask_reject, 2);
                    thenable->on_fulfilled[thenable->then_count] = js_bind_function(resolve_fn, ItemNull, &p_item, 1);
                    thenable->on_rejected[thenable->then_count] = js_bind_function(reject_fn, ItemNull, &p_item, 1);
                    thenable->next_promise[thenable->then_count] = ItemNull;
                    thenable->then_count++;
                }
            }
        } else {
            js_promise_settle(p, JS_PROMISE_FULFILLED, value);
        }
    }
    return ItemNull;
}

static Item js_reject_callback(Item promise_idx_item, Item reason) {
    int64_t idx = it2i(promise_idx_item);
    if (idx >= 0 && idx < js_promise_count) {
        js_promise_settle(&js_promises[idx], JS_PROMISE_REJECTED, reason);
    }
    return ItemNull;
}

extern "C" Item js_promise_create(Item executor) {
    JsPromise* p = js_alloc_promise();
    if (!p) return ItemNull;

    if (get_type_id(executor) == LMD_TYPE_FUNC) {
        // Create resolve/reject functions bound to this promise's index
        int idx = (int)(p - js_promises);
        Item idx_item = (Item){.item = i2it(idx)};
        Item resolve_base = js_new_function((void*)js_resolve_callback, 2);
        Item reject_base = js_new_function((void*)js_reject_callback, 2);
        Item resolve_fn = js_bind_function(resolve_base, ItemNull, &idx_item, 1);
        Item reject_fn = js_bind_function(reject_base, ItemNull, &idx_item, 1);

        Item args[2] = {resolve_fn, reject_fn};
        js_call_function(executor, ItemNull, args, 2);
    }

    return js_promise_to_item(p);
}

extern "C" Item js_promise_resolve(Item value) {
    // If value is already a promise, return it
    JsPromise* existing = js_get_promise(value);
    if (existing) return value;

    JsPromise* p = js_alloc_promise();
    if (!p) return ItemNull;
    js_promise_settle(p, JS_PROMISE_FULFILLED, value);
    return js_promise_to_item(p);
}

extern "C" Item js_promise_reject(Item reason) {
    JsPromise* p = js_alloc_promise();
    if (!p) return ItemNull;
    js_promise_settle(p, JS_PROMISE_REJECTED, reason);
    return js_promise_to_item(p);
}

extern "C" Item js_promise_with_resolvers(void) {
    JsPromise* p = js_alloc_promise();
    if (!p) return ItemNull;
    int idx = (int)(p - js_promises);
    Item idx_item = (Item){.item = i2it(idx)};
    Item resolve_base = js_new_function((void*)js_resolve_callback, 2);
    Item reject_base = js_new_function((void*)js_reject_callback, 2);
    Item resolve_fn = js_bind_function(resolve_base, ItemNull, &idx_item, 1);
    Item reject_fn = js_bind_function(reject_base, ItemNull, &idx_item, 1);
    Item promise = js_promise_to_item(p);
    // Build { promise, resolve, reject } object
    Item result = js_new_object();
    Item k_promise = (Item){.item = s2it(heap_create_name("promise"))};
    Item k_resolve = (Item){.item = s2it(heap_create_name("resolve"))};
    Item k_reject = (Item){.item = s2it(heap_create_name("reject"))};
    js_property_set(result, k_promise, promise);
    js_property_set(result, k_resolve, resolve_fn);
    js_property_set(result, k_reject, reject_fn);
    return result;
}

// Phase 5: Synchronous await — unwraps resolved promises, throws on rejected
extern "C" Item js_await_sync(Item value) {
    // If not a promise, return value as-is (like awaiting a non-thenable)
    JsPromise* p = js_get_promise(value);
    if (!p) return value;

    if (p->state == JS_PROMISE_FULFILLED) {
        return p->result;
    }
    if (p->state == JS_PROMISE_REJECTED) {
        // Rejected promise: throw the rejection reason
        js_throw_value(p->result);
        return ItemNull;
    }
    // Pending promise — synchronous fast-path cannot handle this
    log_debug("js: await_sync: promise still pending (no async state machine yet)");
    return make_js_undefined();
}

// ============================================================
// Phase 6: Async/Await Full State Machine Runtime
// ============================================================

// Async context: tracks a running async function's state machine
struct JsAsyncContext {
    void* state_fn;      // pointer to async_sm_<name> state machine function
    Item* env;           // env array (captures + params + locals)
    int env_size;
    int state;           // current resume state
    int promise_idx;     // index into js_promises[] for the async function's result promise
};

#define JS_MAX_ASYNC_CONTEXTS 256
static JsAsyncContext js_async_contexts[JS_MAX_ASYNC_CONTEXTS];
static int js_async_context_count = 0;

// Cached resolved value from js_async_must_suspend (fast-path)
static Item js_async_resolved_value;

// Check if an awaited value requires suspension (pending promise)
// Returns: 1 = pending (must suspend), 0 = resolved/rejected/non-promise
// For resolved/non-promise: caches result in js_async_resolved_value
// For rejected: calls js_throw_value (exception mechanism handles it)
extern "C" int64_t js_async_must_suspend(Item value) {
    JsPromise* p = js_get_promise(value);
    if (!p) {
        js_async_resolved_value = value;
        return 0;
    }
    if (p->state == JS_PROMISE_FULFILLED) {
        js_async_resolved_value = p->result;
        return 0;
    }
    if (p->state == JS_PROMISE_REJECTED) {
        js_throw_value(p->result);
        js_async_resolved_value = ItemNull;
        return 0;
    }
    // Pending — must suspend
    return 1;
}

// Get the cached resolved value after js_async_must_suspend returned 0
extern "C" Item js_async_get_resolved(void) {
    return js_async_resolved_value;
}

// Forward declarations for async callbacks
static Item js_async_resume_handler(Item ctx_idx_item, Item resolved_value);
static Item js_async_reject_handler(Item ctx_idx_item, Item reason);

// Core async state machine driver: calls the state machine and handles results
static void js_async_drive(int ctx_idx, Item input, int64_t state) {
    JsAsyncContext* ctx = &js_async_contexts[ctx_idx];
    typedef Item (*AsyncSmFn)(Item*, Item, int64_t);
    Item result = ((AsyncSmFn)ctx->state_fn)(ctx->env, input, state);

    // Parse result: [value, next_state]
    if (get_type_id(result) != LMD_TYPE_ARRAY) {
        js_promise_settle(&js_promises[ctx->promise_idx], JS_PROMISE_REJECTED, result);
        return;
    }
    Array* arr = result.array;
    if (arr->length < 2) {
        js_promise_settle(&js_promises[ctx->promise_idx], JS_PROMISE_REJECTED, ItemNull);
        return;
    }
    Item value = arr->items[0];
    int64_t next_state = it2i(arr->items[1]);

    if (next_state == -1) {
        // Done — fulfill the async function's promise
        js_promise_settle(&js_promises[ctx->promise_idx], JS_PROMISE_FULFILLED, value);
    } else if (next_state == -2) {
        // Rejected — reject the async function's promise
        js_promise_settle(&js_promises[ctx->promise_idx], JS_PROMISE_REJECTED, value);
    } else {
        // Suspended on pending promise — register resume/reject callbacks
        ctx->state = next_state;

        // Create bound resume callback: js_async_resume_handler(ctx_idx, resolved_val)
        Item resume_fn = js_new_function((void*)js_async_resume_handler, 2);
        Item idx_item = (Item){.item = i2it(ctx_idx)};
        Item bound_resume = js_bind_function(resume_fn, ItemNull, &idx_item, 1);

        // Create bound reject callback: js_async_reject_handler(ctx_idx, reason)
        Item reject_fn = js_new_function((void*)js_async_reject_handler, 2);
        Item bound_reject = js_bind_function(reject_fn, ItemNull, &idx_item, 1);

        // Register on the pending promise
        js_promise_then(value, bound_resume, bound_reject);
    }
}

// Callback when an awaited promise resolves — resumes the async state machine
static Item js_async_resume_handler(Item ctx_idx_item, Item resolved_value) {
    int ctx_idx = (int)it2i(ctx_idx_item);
    if (ctx_idx < 0 || ctx_idx >= js_async_context_count) return ItemNull;
    JsAsyncContext* ctx = &js_async_contexts[ctx_idx];
    js_async_drive(ctx_idx, resolved_value, ctx->state);
    return ItemNull;
}

// Callback when an awaited promise rejects — re-enter state machine with exception set
static Item js_async_reject_handler(Item ctx_idx_item, Item reason) {
    int ctx_idx = (int)it2i(ctx_idx_item);
    if (ctx_idx < 0 || ctx_idx >= js_async_context_count) return ItemNull;
    JsAsyncContext* ctx = &js_async_contexts[ctx_idx];
    // Set exception so the state machine's try/catch can handle it
    js_throw_value(reason);
    // Re-enter the state machine — it will check the exception and jump to catch
    js_async_drive(ctx_idx, ItemNull, ctx->state);
    return ItemNull;
}

// Create an async context: allocates promise, returns context index
extern "C" Item js_async_context_create(void* fn_ptr, Item* env, int64_t env_size) {
    if (js_async_context_count >= JS_MAX_ASYNC_CONTEXTS) {
        log_error("js: async context limit reached (%d)", JS_MAX_ASYNC_CONTEXTS);
        return (Item){.item = i2it(-1)};
    }
    int idx = js_async_context_count++;
    JsAsyncContext* ctx = &js_async_contexts[idx];
    ctx->state_fn = fn_ptr;
    ctx->env = env;
    ctx->env_size = (int)env_size;
    ctx->state = 0;

    // Create a pending promise for this async function's result
    JsPromise* p = js_alloc_promise();
    ctx->promise_idx = (int)(p - js_promises);

    return (Item){.item = i2it(idx)};
}

// Start execution of an async state machine (initial call at state 0)
extern "C" Item js_async_start(Item ctx_idx_item) {
    int ctx_idx = (int)it2i(ctx_idx_item);
    if (ctx_idx < 0 || ctx_idx >= js_async_context_count) return ItemNull;
    js_async_drive(ctx_idx, make_js_undefined(), 0);
    return ItemNull;
}

// Get the result promise for an async context
extern "C" Item js_async_get_promise(Item ctx_idx_item) {
    int ctx_idx = (int)it2i(ctx_idx_item);
    if (ctx_idx < 0 || ctx_idx >= js_async_context_count) return ItemNull;
    JsAsyncContext* ctx = &js_async_contexts[ctx_idx];
    return js_promise_to_item(&js_promises[ctx->promise_idx]);
}

extern "C" Item js_promise_then(Item promise, Item on_fulfilled, Item on_rejected) {
    JsPromise* p = js_get_promise(promise);
    if (!p) return ItemNull;

    // Create a new promise for the then chain
    JsPromise* next = js_alloc_promise();
    if (!next) return ItemNull;
    Item next_item = js_promise_to_item(next);

    if (p->state == JS_PROMISE_PENDING) {
        // Register callbacks with chained next promise
        if (p->then_count < 8) {
            p->on_fulfilled[p->then_count] = on_fulfilled;
            p->on_rejected[p->then_count] = on_rejected;
            p->next_promise[p->then_count] = next_item;
            p->then_count++;
        }
    } else if (p->state == JS_PROMISE_FULFILLED) {
        if (get_type_id(on_fulfilled) == LMD_TYPE_FUNC) {
            // per spec: schedule as microtask even if already resolved
            js_promise_enqueue_handler(on_fulfilled, p->result, next_item);
        } else {
            // no handler — pass through value
            js_promise_settle(next, JS_PROMISE_FULFILLED, p->result);
        }
    } else {
        if (get_type_id(on_rejected) == LMD_TYPE_FUNC) {
            js_promise_enqueue_handler(on_rejected, p->result, next_item);
        } else {
            js_promise_settle(next, JS_PROMISE_REJECTED, p->result);
        }
    }

    return next_item;
}

extern "C" Item js_promise_catch(Item promise, Item on_rejected) {
    Item undef = (Item){.item = ((uint64_t)LMD_TYPE_UNDEFINED << 56)};
    return js_promise_then(promise, undef, on_rejected);
}

extern "C" Item js_promise_finally(Item promise, Item on_finally) {
    JsPromise* p = js_get_promise(promise);
    if (!p) return ItemNull;

    JsPromise* next = js_alloc_promise();
    if (!next) return ItemNull;
    Item next_item = js_promise_to_item(next);

    if (p->state != JS_PROMISE_PENDING) {
        if (get_type_id(on_finally) == LMD_TYPE_FUNC) {
            // per spec: schedule as microtask even if already settled
            js_promise_enqueue_finally(on_finally, next_item, p->state, p->result);
        } else {
            js_promise_settle(next, p->state, p->result);
        }
    } else {
        // pending: store both fulfilled/rejected handlers as the same finally callback
        if (p->then_count < 8) {
            p->on_fulfilled[p->then_count] = on_finally;
            p->on_rejected[p->then_count] = on_finally;
            p->next_promise[p->then_count] = next_item;
            p->is_finally[p->then_count] = true;
            p->then_count++;
        }
    }

    return next_item;
}

// =============================================================================
// Promise Combinator Helpers (spec-compliant microtask scheduling)
// =============================================================================

// Helper for Promise.all: individual element fulfillment
// Bound args: counter_obj, index_item, result_item; Call arg: value
static Item js_all_resolve_element(Item counter_obj, Item index_item, Item result_item, Item value) {
    String* k_remaining = heap_create_name("remaining", 9);
    String* k_results = heap_create_name("results", 7);

    Item results = js_property_get(counter_obj, (Item){.item = s2it(k_results)});
    int idx = (int)it2i(index_item);
    if (get_type_id(results) == LMD_TYPE_ARRAY && idx < results.array->length) {
        results.array->items[idx] = value;
    }

    int remaining = (int)it2i(js_property_get(counter_obj, (Item){.item = s2it(k_remaining)})) - 1;
    js_property_set(counter_obj, (Item){.item = s2it(k_remaining)}, (Item){.item = i2it(remaining)});

    if (remaining == 0) {
        JsPromise* result = js_get_promise(result_item);
        if (result) js_promise_settle(result, JS_PROMISE_FULFILLED, results);
    }
    return ItemNull;
}

// Helper for Promise.any: individual element rejection
// Bound args: counter_obj, index_item, result_item; Call arg: reason
static Item js_any_reject_element(Item counter_obj, Item index_item, Item result_item, Item reason) {
    String* k_remaining = heap_create_name("remaining", 9);
    String* k_errors = heap_create_name("errors", 6);

    Item errors = js_property_get(counter_obj, (Item){.item = s2it(k_errors)});
    int idx = (int)it2i(index_item);
    if (get_type_id(errors) == LMD_TYPE_ARRAY && idx < errors.array->length) {
        errors.array->items[idx] = reason;
    }

    int remaining = (int)it2i(js_property_get(counter_obj, (Item){.item = s2it(k_remaining)})) - 1;
    js_property_set(counter_obj, (Item){.item = s2it(k_remaining)}, (Item){.item = i2it(remaining)});

    if (remaining == 0) {
        Item msg = (Item){.item = s2it(heap_create_name("All promises were rejected", 26))};
        Item err = js_new_error(msg);
        js_property_set(err, (Item){.item = s2it(k_errors)}, errors);
        // Mark as AggregateError for instanceof and constructor.name checks
        js_property_set(err, (Item){.item = s2it(heap_create_name("__class_name__", 14))},
            (Item){.item = s2it(heap_create_name("AggregateError", 14))});
        js_property_set(err, (Item){.item = s2it(heap_create_name("name", 4))},
            (Item){.item = s2it(heap_create_name("AggregateError", 14))});
        JsPromise* result = js_get_promise(result_item);
        if (result) js_promise_settle(result, JS_PROMISE_REJECTED, err);
    }
    return ItemNull;
}

// Helper for Promise.allSettled: individual element fulfillment
static Item js_settled_fulfill_element(Item counter_obj, Item index_item, Item result_item, Item value) {
    String* k_remaining = heap_create_name("remaining", 9);
    String* k_results = heap_create_name("results", 7);

    Item entry = js_new_object();
    js_property_set(entry, (Item){.item = s2it(heap_create_name("status", 6))},
        (Item){.item = s2it(heap_create_name("fulfilled", 9))});
    js_property_set(entry, (Item){.item = s2it(heap_create_name("value", 5))}, value);

    Item results = js_property_get(counter_obj, (Item){.item = s2it(k_results)});
    int idx = (int)it2i(index_item);
    if (get_type_id(results) == LMD_TYPE_ARRAY && idx < results.array->length) {
        results.array->items[idx] = entry;
    }

    int remaining = (int)it2i(js_property_get(counter_obj, (Item){.item = s2it(k_remaining)})) - 1;
    js_property_set(counter_obj, (Item){.item = s2it(k_remaining)}, (Item){.item = i2it(remaining)});

    if (remaining == 0) {
        JsPromise* result = js_get_promise(result_item);
        if (result) js_promise_settle(result, JS_PROMISE_FULFILLED, results);
    }
    return ItemNull;
}

// Helper for Promise.allSettled: individual element rejection
static Item js_settled_reject_element(Item counter_obj, Item index_item, Item result_item, Item reason) {
    String* k_remaining = heap_create_name("remaining", 9);
    String* k_results = heap_create_name("results", 7);

    Item entry = js_new_object();
    js_property_set(entry, (Item){.item = s2it(heap_create_name("status", 6))},
        (Item){.item = s2it(heap_create_name("rejected", 8))});
    js_property_set(entry, (Item){.item = s2it(heap_create_name("reason", 6))}, reason);

    Item results = js_property_get(counter_obj, (Item){.item = s2it(k_results)});
    int idx = (int)it2i(index_item);
    if (get_type_id(results) == LMD_TYPE_ARRAY && idx < results.array->length) {
        results.array->items[idx] = entry;
    }

    int remaining = (int)it2i(js_property_get(counter_obj, (Item){.item = s2it(k_remaining)})) - 1;
    js_property_set(counter_obj, (Item){.item = s2it(k_remaining)}, (Item){.item = i2it(remaining)});

    if (remaining == 0) {
        JsPromise* result = js_get_promise(result_item);
        if (result) js_promise_settle(result, JS_PROMISE_FULFILLED, results);
    }
    return ItemNull;
}

// =============================================================================
// Promise Combinators (spec-compliant: all settlements go through microtask queue)
// =============================================================================

extern "C" Item js_promise_all(Item iterable) {
    if (get_type_id(iterable) != LMD_TYPE_ARRAY) return js_promise_resolve(iterable);

    Array* arr = iterable.array;
    int count = arr->length;

    if (count == 0) {
        return js_promise_resolve(js_array_new(0));
    }

    JsPromise* result = js_alloc_promise();
    if (!result) return ItemNull;
    Item result_item = js_promise_to_item(result);

    // shared counter { remaining: N, results: Array(N) }
    Item counter = js_new_object();
    js_property_set(counter, (Item){.item = s2it(heap_create_name("remaining", 9))}, (Item){.item = i2it(count)});
    Item results_arr = js_array_new(count);
    results_arr.array->length = count;
    js_property_set(counter, (Item){.item = s2it(heap_create_name("results", 7))}, results_arr);

    // any single rejection rejects the result immediately
    int idx = (int)(result - js_promises);
    Item idx_item = (Item){.item = i2it(idx)};
    Item reject_base = js_new_function((void*)js_reject_callback, 2);
    Item reject_fn = js_bind_function(reject_base, ItemNull, &idx_item, 1);

    for (int i = 0; i < count; i++) {
        Item elem = arr->items[i];
        JsPromise* p = js_get_promise(elem);
        if (!p) elem = js_promise_resolve(elem);

        Item resolve_handler = js_new_function((void*)js_all_resolve_element, 4);
        Item bound_args[3] = {counter, (Item){.item = i2it(i)}, result_item};
        Item resolve_fn = js_bind_function(resolve_handler, ItemNull, bound_args, 3);

        js_promise_then(elem, resolve_fn, reject_fn);
    }

    return result_item;
}

extern "C" Item js_promise_race(Item iterable) {
    if (get_type_id(iterable) != LMD_TYPE_ARRAY) return js_promise_resolve(iterable);

    Array* arr = iterable.array;

    // per spec: empty array → never-settling promise
    JsPromise* result = js_alloc_promise();
    if (!result) return ItemNull;
    Item result_item = js_promise_to_item(result);

    if (arr->length == 0) return result_item;

    int idx = (int)(result - js_promises);
    Item idx_item = (Item){.item = i2it(idx)};
    Item resolve_base = js_new_function((void*)js_resolve_callback, 2);
    Item reject_base = js_new_function((void*)js_reject_callback, 2);
    Item resolve_fn = js_bind_function(resolve_base, ItemNull, &idx_item, 1);
    Item reject_fn = js_bind_function(reject_base, ItemNull, &idx_item, 1);

    for (int i = 0; i < arr->length; i++) {
        Item elem = arr->items[i];
        JsPromise* p = js_get_promise(elem);
        if (!p) elem = js_promise_resolve(elem);
        js_promise_then(elem, resolve_fn, reject_fn);
    }

    return result_item;
}

extern "C" Item js_promise_any(Item iterable) {
    if (get_type_id(iterable) != LMD_TYPE_ARRAY) return js_promise_resolve(iterable);

    Array* arr = iterable.array;
    int count = arr->length;

    if (count == 0) {
        Item msg = (Item){.item = s2it(heap_create_name("All promises were rejected", 26))};
        return js_promise_reject(js_new_error(msg));
    }

    JsPromise* result_p = js_alloc_promise();
    if (!result_p) return ItemNull;
    Item result_item = js_promise_to_item(result_p);

    // first fulfillment wins (reuse resolve_callback bound to result)
    int idx = (int)(result_p - js_promises);
    Item idx_item = (Item){.item = i2it(idx)};
    Item resolve_base = js_new_function((void*)js_resolve_callback, 2);
    Item resolve_fn = js_bind_function(resolve_base, ItemNull, &idx_item, 1);

    // shared counter for rejection tracking
    Item counter = js_new_object();
    js_property_set(counter, (Item){.item = s2it(heap_create_name("remaining", 9))}, (Item){.item = i2it(count)});
    Item errors_arr = js_array_new(count);
    errors_arr.array->length = count;
    js_property_set(counter, (Item){.item = s2it(heap_create_name("errors", 6))}, errors_arr);

    for (int i = 0; i < count; i++) {
        Item elem = arr->items[i];
        JsPromise* p = js_get_promise(elem);
        if (!p) elem = js_promise_resolve(elem);

        Item reject_handler = js_new_function((void*)js_any_reject_element, 4);
        Item bound_args[3] = {counter, (Item){.item = i2it(i)}, result_item};
        Item reject_fn = js_bind_function(reject_handler, ItemNull, bound_args, 3);

        js_promise_then(elem, resolve_fn, reject_fn);
    }

    return result_item;
}

extern "C" Item js_promise_all_settled(Item iterable) {
    if (get_type_id(iterable) != LMD_TYPE_ARRAY) return js_promise_resolve(iterable);

    Array* arr = iterable.array;
    int count = arr->length;

    if (count == 0) {
        return js_promise_resolve(js_array_new(0));
    }

    JsPromise* result = js_alloc_promise();
    if (!result) return ItemNull;
    Item result_item = js_promise_to_item(result);

    Item counter = js_new_object();
    js_property_set(counter, (Item){.item = s2it(heap_create_name("remaining", 9))}, (Item){.item = i2it(count)});
    Item results_arr = js_array_new(count);
    results_arr.array->length = count;
    js_property_set(counter, (Item){.item = s2it(heap_create_name("results", 7))}, results_arr);

    for (int i = 0; i < count; i++) {
        Item elem = arr->items[i];
        JsPromise* p = js_get_promise(elem);
        if (!p) elem = js_promise_resolve(elem);

        Item bound_args[3] = {counter, (Item){.item = i2it(i)}, result_item};

        Item fulfill_handler = js_new_function((void*)js_settled_fulfill_element, 4);
        Item fulfill_fn = js_bind_function(fulfill_handler, ItemNull, bound_args, 3);

        Item reject_handler = js_new_function((void*)js_settled_reject_element, 4);
        Item reject_fn = js_bind_function(reject_handler, ItemNull, bound_args, 3);

        js_promise_then(elem, fulfill_fn, reject_fn);
    }

    return result_item;
}

// =============================================================================
// v14: ES Module Runtime
// =============================================================================

#define JS_MAX_MODULES 64

struct JsModule {
    String* specifier;
    Item    namespace_obj;
};

static JsModule js_modules[JS_MAX_MODULES];
static int js_module_count_v14 = 0;

// called by js_batch_reset() to clear module cache between batch scripts
static void js_module_cache_reset() {
    js_module_count_v14 = 0;
}

extern "C" void js_module_register(Item specifier, Item namespace_obj) {
    if (js_module_count_v14 >= JS_MAX_MODULES) {
        log_error("module: exceeded max modules (%d)", JS_MAX_MODULES);
        return;
    }
    if (js_module_count_v14 >= JS_MAX_MODULES - 4) {
        log_info("module: approaching capacity limit (%d/%d)", js_module_count_v14, JS_MAX_MODULES);
    }
    if (get_type_id(specifier) != LMD_TYPE_STRING) return;

    String* spec = it2s(specifier);
    // Check for existing registration
    for (int i = 0; i < js_module_count_v14; i++) {
        if (js_modules[i].specifier->len == spec->len &&
            memcmp(js_modules[i].specifier->chars, spec->chars, spec->len) == 0) {
            js_modules[i].namespace_obj = namespace_obj;
            return;
        }
    }

    JsModule* m = &js_modules[js_module_count_v14++];
    m->specifier = spec;
    m->namespace_obj = namespace_obj;
}

extern "C" Item js_module_get(Item specifier) {
    if (get_type_id(specifier) != LMD_TYPE_STRING) return ItemNull;
    String* spec = it2s(specifier);

    // check for built-in modules (bare specifier or with .js suffix from resolver)
    if ((spec->len == 2 && memcmp(spec->chars, "fs", 2) == 0) ||
        (spec->len == 5 && memcmp(spec->chars, "fs.js", 5) == 0) ||
        (spec->len == 7 && memcmp(spec->chars, "node:fs", 7) == 0)) {
        extern Item js_get_fs_namespace(void);
        return js_get_fs_namespace();
    }
    if ((spec->len == 13 && memcmp(spec->chars, "child_process", 13) == 0) ||
        (spec->len == 16 && memcmp(spec->chars, "child_process.js", 16) == 0) ||
        (spec->len == 18 && memcmp(spec->chars, "node:child_process", 18) == 0)) {
        extern Item js_get_child_process_namespace(void);
        return js_get_child_process_namespace();
    }

    for (int i = 0; i < js_module_count_v14; i++) {
        if (js_modules[i].specifier->len == spec->len &&
            memcmp(js_modules[i].specifier->chars, spec->chars, spec->len) == 0) {
            return js_modules[i].namespace_obj;
        }
    }
    return ItemNull;
}

extern "C" Item js_module_namespace_create(Item exports_map) {
    // Module namespace is just a frozen map object
    return exports_map;
}

// =============================================================================
// TextEncoder / TextDecoder (UTF-8 only)
// =============================================================================

extern "C" Item js_text_encoder_new(void) {
    Item obj = js_new_object();
    Item k = (Item){.item = s2it(heap_create_name("encoding"))};
    Item v = (Item){.item = s2it(heap_create_name("utf-8"))};
    js_property_set(obj, k, v);
    // Mark as TextEncoder for method dispatch
    Item type_key = (Item){.item = s2it(heap_create_name("__class_name__"))};
    Item type_val = (Item){.item = s2it(heap_create_name("TextEncoder"))};
    js_property_set(obj, type_key, type_val);
    return obj;
}

extern "C" Item js_text_encoder_encode(Item encoder, Item str) {
    (void)encoder;
    if (get_type_id(str) != LMD_TYPE_STRING) return js_array_new(0);
    String* s = it2s(str);
    if (!s || s->len == 0) return js_array_new(0);
    // Create array of byte values
    Item result = js_array_new(0);
    for (int i = 0; i < (int)s->len; i++) {
        js_array_push_item_direct(result.array, (Item){.item = i2it((unsigned char)s->chars[i])});
    }
    return result;
}

extern "C" Item js_text_decoder_new(Item encoding_item) {
    Item obj = js_new_object();
    // Normalize encoding name
    const char* enc = "utf-8";
    char enc_buf[32] = "utf-8";
    if (get_type_id(encoding_item) == LMD_TYPE_STRING) {
        String* s = it2s(encoding_item);
        if (s && s->len > 0 && s->len < 32) {
            // lowercase copy
            for (int i = 0; i < (int)s->len; i++)
                enc_buf[i] = (char)tolower((unsigned char)s->chars[i]);
            enc_buf[s->len] = '\0';
            enc = enc_buf;
        }
    }
    Item k = (Item){.item = s2it(heap_create_name("encoding"))};
    Item v = (Item){.item = s2it(heap_create_name(enc))};
    js_property_set(obj, k, v);
    // Mark as TextDecoder for method dispatch
    Item type_key = (Item){.item = s2it(heap_create_name("__class_name__"))};
    Item type_val = (Item){.item = s2it(heap_create_name("TextDecoder"))};
    js_property_set(obj, type_key, type_val);
    return obj;
}

// Helper: encode a single Unicode codepoint to UTF-8, return bytes written
static int js_cp_to_utf8(char* buf, uint32_t cp) {
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
    } else if (cp <= 0x10FFFF) {
        buf[0] = (char)(0xF0 | (cp >> 18));
        buf[1] = (char)(0x80 | ((cp >> 12) & 0x3F));
        buf[2] = (char)(0x80 | ((cp >> 6) & 0x3F));
        buf[3] = (char)(0x80 | (cp & 0x3F));
        return 4;
    } else {
        // replacement character
        buf[0] = (char)0xEF; buf[1] = (char)0xBF; buf[2] = (char)0xBD;
        return 3;
    }
}

extern "C" Item js_text_decoder_decode(Item decoder, Item input) {
    // Get encoding from decoder object
    const char* encoding = "utf-8";
    char enc_buf[32] = "utf-8";
    if (get_type_id(decoder) == LMD_TYPE_MAP) {
        Item enc_key = (Item){.item = s2it(heap_create_name("encoding"))};
        Item enc_val = js_property_get(decoder, enc_key);
        if (get_type_id(enc_val) == LMD_TYPE_STRING) {
            String* s = it2s(enc_val);
            if (s && s->len > 0 && s->len < 32) {
                memcpy(enc_buf, s->chars, s->len);
                enc_buf[s->len] = '\0';
                encoding = enc_buf;
            }
        }
    }

    // Extract raw bytes from input
    uint8_t* bytes = NULL;
    int byte_len = 0;
    uint8_t* heap_buf = NULL;
    TypeId tid = get_type_id(input);
    if (tid == LMD_TYPE_MAP && js_is_typed_array(input)) {
        JsTypedArray* ta = (JsTypedArray*)input.map->data;
        bytes = ta ? (uint8_t*)ta->data : NULL;
        byte_len = ta ? ta->length : 0;
    } else if (tid == LMD_TYPE_ARRAY) {
        Array* arr = input.array;
        if (arr && arr->length > 0) {
            heap_buf = (uint8_t*)malloc(arr->length);
            for (int i = 0; i < arr->length; i++)
                heap_buf[i] = (uint8_t)js_get_number(arr->items[i]);
            bytes = heap_buf;
            byte_len = arr->length;
        }
    } else if (tid == LMD_TYPE_STRING) {
        // Pass-through if already a string
        if (heap_buf) free(heap_buf);
        return input;
    }

    if (!bytes || byte_len == 0) {
        if (heap_buf) free(heap_buf);
        return (Item){.item = s2it(heap_strcpy("", 0))};
    }

    Item result;
    bool is_utf16be = (strncmp(encoding, "utf-16be", 8) == 0);
    bool is_utf16le = (strncmp(encoding, "utf-16le", 8) == 0) ||
                     (strncmp(encoding, "utf-16", 6) == 0 && !is_utf16be);

    if (is_utf16be || is_utf16le) {
        // UTF-16 decode: each code unit is 2 bytes
        int start = 0;
        // Check/strip BOM
        if (byte_len >= 2) {
            uint16_t bom = is_utf16be ?
                ((uint16_t)bytes[0] << 8 | bytes[1]) :
                ((uint16_t)bytes[1] << 8 | bytes[0]);
            if (bom == 0xFEFF) start = 2; // strip BOM
        }
        // Max output: each 2-byte unit → up to 4 UTF-8 bytes
        int max_out = ((byte_len - start) / 2) * 4 + 4;
        char* out = (char*)malloc(max_out + 1);
        int pos = 0;
        int i = start;
        while (i + 1 < byte_len) {
            uint16_t cu = is_utf16be ?
                ((uint16_t)bytes[i] << 8 | bytes[i+1]) :
                ((uint16_t)bytes[i+1] << 8 | bytes[i]);
            i += 2;
            uint32_t cp;
            if (cu >= 0xD800 && cu <= 0xDBFF && i + 1 < byte_len) {
                // High surrogate — read low surrogate
                uint16_t low = is_utf16be ?
                    ((uint16_t)bytes[i] << 8 | bytes[i+1]) :
                    ((uint16_t)bytes[i+1] << 8 | bytes[i]);
                if (low >= 0xDC00 && low <= 0xDFFF) {
                    cp = 0x10000 + ((uint32_t)(cu - 0xD800) << 10) + (low - 0xDC00);
                    i += 2;
                } else {
                    cp = 0xFFFD; // replacement
                }
            } else if (cu >= 0xDC00 && cu <= 0xDFFF) {
                cp = 0xFFFD; // lone low surrogate
            } else {
                cp = cu;
            }
            // Skip BOM codepoint U+FEFF in output
            if (cp == 0xFEFF && pos == 0) continue;
            pos += js_cp_to_utf8(out + pos, cp);
        }
        out[pos] = '\0';
        result = (Item){.item = s2it(heap_strcpy(out, pos))};
        free(out);
    } else {
        // UTF-8 (default): strip BOM if present
        int start = 0;
        if (byte_len >= 3 && bytes[0] == 0xEF && bytes[1] == 0xBB && bytes[2] == 0xBF)
            start = 3; // strip UTF-8 BOM
        result = (Item){.item = s2it(heap_strcpy((char*)bytes + start, byte_len - start))};
    }

    if (heap_buf) free(heap_buf);
    return result;
}

// =============================================================================
// WeakMap / WeakSet stubs (aliased to Map/Set for PDF.js compat)
// =============================================================================

extern "C" Item js_weakmap_new(void) {
    Item obj = js_map_collection_new();
    // Override prototype to WeakMap.prototype (js_map_collection_new sets Map.prototype)
    js_collection_link_prototype(obj, "WeakMap", 7);
    // Mark as weak collection
    JsCollectionData* cd = js_get_collection_data(obj);
    if (cd) cd->is_weak = true;
    return obj;
}

extern "C" Item js_weakset_new(void) {
    Item obj = js_set_collection_new();
    // Override prototype to WeakSet.prototype (js_set_collection_new sets Set.prototype)
    js_collection_link_prototype(obj, "WeakSet", 7);
    // Mark as weak collection
    JsCollectionData* cd = js_get_collection_data(obj);
    if (cd) cd->is_weak = true;
    return obj;
}

// Public collection type checks (for instanceof)
extern "C" bool js_is_map_instance(Item obj) {
    JsCollectionData* cd = js_get_collection_data(obj);
    return cd && cd->type == JS_COLLECTION_MAP;
}

extern "C" bool js_is_set_instance(Item obj) {
    JsCollectionData* cd = js_get_collection_data(obj);
    return cd && cd->type == JS_COLLECTION_SET;
}

// =============================================================================
// Deep batch reset: clear ALL stale state that references pool-allocated data.
// Called by js_batch_reset() to prevent dangling pointers after pool destruction.
// =============================================================================
void js_deep_batch_reset() {
    // generators, promises, async contexts — contain Items from old pool
    memset(js_generators, 0, sizeof(js_generators));
    js_generator_count = 0;
    memset(js_promises, 0, sizeof(js_promises));
    js_promise_count = 0;
    memset(js_async_contexts, 0, sizeof(js_async_contexts));
    js_async_context_count = 0;
    js_async_resolved_value = (Item){0};
    // pending call args
    js_pending_call_args = NULL;
    js_pending_call_argc = 0;
    // call counter
    js_call_count = 0;
}
