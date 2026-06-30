#pragma once

// js_runtime_internal.hpp - shared declarations for the split JS runtime.

#include "js_runtime.h"
#include "js_dom.h"
#include "js_dom_events.h"
#include "js_cssom.h"
#include "js_typed_array.h"
#include "js_event_loop.h"
#include "js_error_codes.h"
#include "js_property_attrs.h"
#include "js_props.h"
#include "js_class.h"
#include "js_coerce.h"
#include "js_runtime_state.hpp"
#include "../lambda-data.hpp"
#include "../lambda-decimal.hpp"
#include "../transpiler.hpp"
#include "../module_registry.h"
#include "../../lib/lambda_typed.hpp"
#include "../../lib/log.h"
#include "../../lib/hashmap.h"
#include "../../lib/str.h"
#include "../../lib/utf.h"
#include <cstring>
#include <cmath>
#include "../../lib/mem.h"
#include <cstdio>
#include <cstdlib>
#include <uv.h>
#include <cctype>
#include <string>
#include <unordered_map>
#include <map>
#include <re2/re2.h>
#include <utf8proc.h>
#ifndef _WIN32
#include <execinfo.h>
#else
// Windows stubs for POSIX functions
#include <direct.h>
#include <io.h>
#include <stdlib.h>
static inline int setenv(const char* name, const char* value, int overwrite) {
    if (!overwrite && getenv(name)) return 0;
    return _putenv_s(name, value) == 0 ? 0 : -1;
}
static inline int unsetenv(const char* name) {
    return _putenv_s(name, "") == 0 ? 0 : -1;
}
static inline void* memmem(const void* haystack, size_t hlen, const void* needle, size_t nlen) {
    if (nlen == 0) return (void*)haystack;
    if (nlen > hlen) return NULL;
    const char* h = (const char*)haystack;
    const char* n = (const char*)needle;
    for (size_t i = 0; i <= hlen - nlen; i++) {
        if (memcmp(h + i, n, nlen) == 0) return (void*)(h + i);
    }
    return NULL;
}
#endif



#ifndef JS_RUNTIME_INTERNAL_HPP_DECLS
#define JS_RUNTIME_INTERNAL_HPP_DECLS

// JsFunction layout shared by runtime translation units.
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
    uint16_t flags;   // v20: JS_FUNC_FLAG_* bits
    int16_t formal_length; // ES spec .length: params before first default, excl rest (-1 = use param_count)
    Item* module_vars; // Per-module variable array (NULL for built-in functions)
    Item home_global; // globalThis captured when the function was created
    String* source_text; // v29: original source text for Function.prototype.toString
    bool eval_initializer_context;
    Item* with_env; // captured with-object environment stack, if any
    int with_env_depth;
    String* vm_stack_filename;
    String* vm_stack_source;
    int64_t vm_stack_line_offset;
    int64_t vm_stack_column_offset;
};

#define JS_FUNC_FLAG_GENERATOR 1
#define JS_FUNC_FLAG_ARROW     2
#define JS_FUNC_FLAG_TYPED_ARRAY_METHOD 4
#define JS_FUNC_FLAG_STRICT    8
#define JS_FUNC_FLAG_HAS_BOUND_THIS 16
#define JS_FUNC_FLAG_METHOD    32
#define JS_FUNC_FLAG_ASYNC_GEN 64  // async generator function (sets is_async in js_generator_create)
#define JS_FUNC_FLAG_ASYNC     128 // async (non-generator) function: changes [[Prototype]] to %AsyncFunction%.prototype
#define JS_FUNC_FLAG_DERIVED_CTOR 256
#define JS_FUNC_FLAG_DATA_VIEW_ACCESSOR JS_FUNC_FLAG_METHOD

extern "C" Item js_get_generator_shared_proto(bool is_async);

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
    JS_BUILTIN_OBJ_DEFINE_GETTER,
    JS_BUILTIN_OBJ_DEFINE_SETTER,
    JS_BUILTIN_OBJ_LOOKUP_GETTER,
    JS_BUILTIN_OBJ_LOOKUP_SETTER,
    JS_BUILTIN_OBJ_PROTO_GETTER,
    JS_BUILTIN_OBJ_PROTO_SETTER,
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
    JS_BUILTIN_ARR_ITEM,
    JS_BUILTIN_ARR_LAST_INDEX_OF,
    JS_BUILTIN_ARR_REDUCE_RIGHT,
    JS_BUILTIN_ARR_FIND_LAST,
    JS_BUILTIN_ARR_FIND_LAST_INDEX,
    JS_BUILTIN_ARR_TO_SORTED,
    JS_BUILTIN_ARR_TO_REVERSED,
    JS_BUILTIN_ARR_TO_SPLICED,
    JS_BUILTIN_ARR_WITH,
    JS_BUILTIN_ARR_TO_LOCALE_STRING,
    // Function.prototype
    JS_BUILTIN_FUNC_CALL,
    JS_BUILTIN_FUNC_APPLY,
    JS_BUILTIN_FUNC_BIND,
    JS_BUILTIN_FUNC_TO_STRING,
    JS_BUILTIN_FUNC_HAS_INSTANCE,
    JS_BUILTIN_FUNC_THROW_TYPE_ERROR,
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
    JS_BUILTIN_STR_IS_WELL_FORMED,
    JS_BUILTIN_STR_TO_WELL_FORMED,
    // HTML wrapper methods (Annex B)
    JS_BUILTIN_STR_ANCHOR,
    JS_BUILTIN_STR_BIG,
    JS_BUILTIN_STR_BLINK,
    JS_BUILTIN_STR_BOLD,
    JS_BUILTIN_STR_FIXED,
    JS_BUILTIN_STR_FONTCOLOR,
    JS_BUILTIN_STR_FONTSIZE,
    JS_BUILTIN_STR_ITALICS,
    JS_BUILTIN_STR_LINK,
    JS_BUILTIN_STR_SMALL,
    JS_BUILTIN_STR_STRIKE,
    JS_BUILTIN_STR_SUB,
    JS_BUILTIN_STR_SUP,
    JS_BUILTIN_STR_SUBSTR,
    JS_BUILTIN_STR_TO_LOCALE_LOWER_CASE,
    JS_BUILTIN_STR_TO_LOCALE_UPPER_CASE,
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
    JS_BUILTIN_OBJECT_GROUP_BY,
    JS_BUILTIN_MAP_GROUP_BY,
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
    // BigInt.prototype methods
    JS_BUILTIN_BIGINT_TO_STRING,
    JS_BUILTIN_BIGINT_VALUE_OF,
    JS_BUILTIN_BIGINT_TO_LOCALE_STRING,
    JS_BUILTIN_BIGINT_AS_INT_N,
    JS_BUILTIN_BIGINT_AS_UINT_N,
    // Symbol.prototype methods
    JS_BUILTIN_SYM_TO_STRING,
    JS_BUILTIN_SYM_VALUE_OF,
    JS_BUILTIN_SYM_TO_PRIMITIVE,
    JS_BUILTIN_SYM_DESCRIPTION_GETTER,
    // Symbol static methods
    JS_BUILTIN_SYMBOL_FOR,
    JS_BUILTIN_SYMBOL_KEY_FOR,
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
    JS_BUILTIN_JSON_RAW_JSON,
    JS_BUILTIN_JSON_IS_RAW_JSON,
    // String iterator
    JS_BUILTIN_STRING_ITER,      // String.prototype[Symbol.iterator]() — creates string iterator
    JS_BUILTIN_STRING_ITER_NEXT, // String iterator .next()
    // Error.prototype.toString (generic)
    JS_BUILTIN_ERR_TO_STRING,
    // Boolean.prototype.toString / valueOf
    JS_BUILTIN_BOOL_TO_STRING,
    JS_BUILTIN_BOOL_VALUE_OF,
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
    JS_BUILTIN_DATE_TO_LOCALE_TIME_STRING,
    JS_BUILTIN_DATE_VALUE_OF,
    JS_BUILTIN_DATE_TO_PRIMITIVE,
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
    JS_BUILTIN_DATE_GET_YEAR,
    JS_BUILTIN_DATE_SET_YEAR,
    // Promise static methods (v45: make Promise static methods visible as properties)
    JS_BUILTIN_PROMISE_RESOLVE,
    JS_BUILTIN_PROMISE_REJECT,
    JS_BUILTIN_PROMISE_ALL,
    JS_BUILTIN_PROMISE_ALL_SETTLED,
    JS_BUILTIN_PROMISE_ANY,
    JS_BUILTIN_PROMISE_RACE,
    JS_BUILTIN_PROMISE_WITH_RESOLVERS,
    // Promise prototype methods
    JS_BUILTIN_PROMISE_PROTO_THEN,
    JS_BUILTIN_PROMISE_PROTO_CATCH,
    JS_BUILTIN_PROMISE_PROTO_FINALLY,
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
    JS_BUILTIN_MAP_SIZE_GETTER,  // get Map.prototype.size
    JS_BUILTIN_SET_SIZE_GETTER,  // get Set.prototype.size
    // WeakMap/WeakSet prototype methods (accept is_weak collections)
    JS_BUILTIN_WEAKMAP_SET,      // WeakMap.prototype.set(key, value)
    JS_BUILTIN_WEAKMAP_GET,      // WeakMap.prototype.get(key)
    JS_BUILTIN_WEAKMAP_HAS,      // WeakMap.prototype.has(key)
    JS_BUILTIN_WEAKMAP_DELETE,   // WeakMap.prototype.delete(key)
    JS_BUILTIN_WEAKSET_ADD,      // WeakSet.prototype.add(value)
    JS_BUILTIN_WEAKSET_HAS,      // WeakSet.prototype.has(value)
    JS_BUILTIN_WEAKSET_DELETE,   // WeakSet.prototype.delete(value)
    JS_BUILTIN_WEAKREF_DEREF,    // WeakRef.prototype.deref()
    JS_BUILTIN_FINALIZATION_REGISTER,   // FinalizationRegistry.prototype.register(target, holdings, token)
    JS_BUILTIN_FINALIZATION_UNREGISTER, // FinalizationRegistry.prototype.unregister(token)
    // RegExp Symbol methods (v83: @@match, @@replace, @@search, @@split)
    JS_BUILTIN_REGEXP_SYMBOL_MATCH,
    JS_BUILTIN_REGEXP_SYMBOL_REPLACE,
    JS_BUILTIN_REGEXP_SYMBOL_SEARCH,
    JS_BUILTIN_REGEXP_SYMBOL_SPLIT,
    JS_BUILTIN_REGEXP_SYMBOL_MATCHALL,   // v90: RegExp.prototype[@@matchAll]
    JS_BUILTIN_REGEXP_MATCHALL_ITER_NEXT, // v90: RegExpStringIterator.next()
    JS_BUILTIN_ARRAYBUFFER_ISVIEW,   // ArrayBuffer.isView(arg)
    // Reflect methods
    JS_BUILTIN_REFLECT_APPLY,
    JS_BUILTIN_REFLECT_CONSTRUCT,
    JS_BUILTIN_REFLECT_DEFINE_PROPERTY,
    JS_BUILTIN_REFLECT_DELETE_PROPERTY,
    JS_BUILTIN_REFLECT_GET,
    JS_BUILTIN_REFLECT_GET_OWN_PROPERTY_DESCRIPTOR,
    JS_BUILTIN_REFLECT_GET_PROTOTYPE_OF,
    JS_BUILTIN_REFLECT_HAS,
    JS_BUILTIN_REFLECT_IS_EXTENSIBLE,
    JS_BUILTIN_REFLECT_OWN_KEYS,
    JS_BUILTIN_REFLECT_PREVENT_EXTENSIONS,
    JS_BUILTIN_REFLECT_SET,
    JS_BUILTIN_REFLECT_SET_PROTOTYPE_OF,
    // Generator prototype methods
    JS_BUILTIN_GENERATOR_NEXT,
    JS_BUILTIN_GENERATOR_RETURN,
    JS_BUILTIN_GENERATOR_THROW,
    JS_BUILTIN_ASYNC_GENERATOR_NEXT,
    JS_BUILTIN_ASYNC_GENERATOR_RETURN,
    JS_BUILTIN_ASYNC_GENERATOR_THROW,
    // Iterator protocol: [Symbol.iterator]() { return this; }
    JS_BUILTIN_ITER_IDENTITY, // returns this_val (for iterators that are their own iterable)
    // Proxy static methods
    JS_BUILTIN_PROXY_REVOCABLE,
    // test262 host object $262
    JS_BUILTIN_262_DETACH_ARRAYBUFFER,
    JS_BUILTIN_262_CREATE_REALM,
    JS_BUILTIN_262_REALM_REGEXP_GET_HASINDICES,
    // TypedArray static methods
    JS_BUILTIN_TYPED_ARRAY_FROM,
    JS_BUILTIN_TYPED_ARRAY_OF,
    JS_BUILTIN_REGEXP_COMPILE,       // RegExp.prototype.compile (Annex B)
    // RegExp prototype accessor getters (v89: flag accessors on prototype)
    JS_BUILTIN_REGEXP_GET_SOURCE,
    JS_BUILTIN_REGEXP_GET_FLAGS,
    JS_BUILTIN_REGEXP_GET_GLOBAL,
    JS_BUILTIN_REGEXP_GET_IGNORECASE,
    JS_BUILTIN_REGEXP_GET_MULTILINE,
    JS_BUILTIN_REGEXP_GET_DOTALL,
    JS_BUILTIN_REGEXP_GET_UNICODE,
    JS_BUILTIN_REGEXP_GET_UNICODE_SETS,  // Js54: ES2024 RegExp.prototype.unicodeSets
    JS_BUILTIN_REGEXP_GET_STICKY,
    JS_BUILTIN_REGEXP_GET_HASINDICES,
    JS_BUILTIN_ARRAYBUFFER_SLICE,    // ArrayBuffer.prototype.slice
    JS_BUILTIN_ARRAYBUFFER_RESIZE,   // ArrayBuffer.prototype.resize
    JS_BUILTIN_ARRAYBUFFER_TRANSFER, // Js54 P8: ArrayBuffer.prototype.transfer
    JS_BUILTIN_ARRAYBUFFER_TRANSFER_TO_FIXED_LENGTH, // Js54 P8: ArrayBuffer.prototype.transferToFixedLength
    JS_BUILTIN_ARRAYBUFFER_GET_BYTE_LENGTH,
    JS_BUILTIN_ARRAYBUFFER_GET_RESIZABLE,
    JS_BUILTIN_ARRAYBUFFER_GET_MAX_BYTE_LENGTH,
    JS_BUILTIN_ARRAYBUFFER_GET_DETACHED, // Js54 P8: ArrayBuffer.prototype.detached (ES2024)
    JS_BUILTIN_SHAREDARRAYBUFFER_SLICE,
    JS_BUILTIN_SHAREDARRAYBUFFER_GROW, // SharedArrayBuffer.prototype.grow
    JS_BUILTIN_SHAREDARRAYBUFFER_GET_BYTE_LENGTH,
    JS_BUILTIN_SHAREDARRAYBUFFER_GET_GROWABLE,
    JS_BUILTIN_SHAREDARRAYBUFFER_GET_MAX_BYTE_LENGTH,
    // Atomics methods
    JS_BUILTIN_ATOMICS_ADD,
    JS_BUILTIN_ATOMICS_AND,
    JS_BUILTIN_ATOMICS_COMPAREEXCHANGE,
    JS_BUILTIN_ATOMICS_EXCHANGE,
    JS_BUILTIN_ATOMICS_ISLOCKFREE,
    JS_BUILTIN_ATOMICS_LOAD,
    JS_BUILTIN_ATOMICS_NOTIFY,
    JS_BUILTIN_ATOMICS_OR,
    JS_BUILTIN_ATOMICS_PAUSE,
    JS_BUILTIN_ATOMICS_STORE,
    JS_BUILTIN_ATOMICS_SUB,
    JS_BUILTIN_ATOMICS_WAIT,
    JS_BUILTIN_ATOMICS_WAIT_ASYNC,
    JS_BUILTIN_ATOMICS_XOR,
    // CSS namespace methods
    JS_BUILTIN_CSS_SUPPORTS,
    JS_BUILTIN_CSS_ESCAPE,
    JS_BUILTIN_MAX
};


// v22 / P8 + Js58.2: Maximum index/capacity gap considered for dense array
// expansion before forcing sparse companion-map storage. Js58.2 restores the
// ES-scale cap and relies on density conversion in js_runtime.cpp to keep
// low-density writes such as `arr[999999] = ...` sparse instead of
// dense-filling almost one million holes.
#define SPARSE_GAP_MAX 1000000

// Forward declarations for Unicode normalization (implemented in utf_string.cpp)
extern "C" char* normalize_utf8proc_nfc(const char* str, int len, int* out_len);
extern "C" char* normalize_utf8proc_nfd(const char* str, int len, int* out_len);
extern "C" char* normalize_utf8proc_nfkc(const char* str, int len, int* out_len);
extern "C" char* normalize_utf8proc_nfkd(const char* str, int len, int* out_len);

extern TypeMap EmptyMap;

extern "C" bool js_func_is_builtin_ctor(Item fn);
extern "C" Item js_lookup_builtin_method(TypeId type, const char* name, int len);
extern "C" Item js_array_get_custom_proto(Item arr);
extern "C" void js_child_process_reset();
extern "C" void js_fs_reset();
extern "C" void js_path_reset();
extern "C" void js_os_reset();
extern "C" void js_url_module_reset();
extern "C" void js_util_reset();


struct JsBuiltinMethodSpec {
    const char* name;
    int len;
    int builtin_id;
    int param_count;
    const char* display_name;
};

Item js_lookup_builtin_method_spec(const JsBuiltinMethodSpec* specs, const char* name, int len);
void js_install_builtin_method_specs(Item object, const JsBuiltinMethodSpec* specs);
void js_install_builtin_function_specs(Item object, const JsBuiltinMethodSpec* specs, int flags, bool use_cache);
void js_install_builtin_accessor_specs(Item object, const JsBuiltinMethodSpec* specs, int flags);
void js_populate_builtin_prototype_methods(Item prototype, const char* ctor_name, int ctor_len);
void js_populate_dataview_prototype_methods(Item prototype);
Item js_lookup_builtin_prototype_method_for_class(JsClass cls, const char* name, int len);
Item js_get_or_create_builtin(int builtin_id, const char* name, int param_count);
Item js_lookup_constructor_static(const char* ctor_name, int ctor_len, const char* prop_name, int prop_len);

extern const JsBuiltinMethodSpec JS_MATH_METHOD_SPECS[];
extern const JsBuiltinMethodSpec JS_JSON_METHOD_SPECS[];
extern const JsBuiltinMethodSpec JS_REFLECT_METHOD_SPECS[];
extern const JsBuiltinMethodSpec JS_ATOMICS_METHOD_SPECS[];
extern const JsBuiltinMethodSpec JS_CSS_METHOD_SPECS[];
extern const JsBuiltinMethodSpec JS_TYPED_ARRAY_PROTOTYPE_METHOD_SPECS[];
extern const JsBuiltinMethodSpec JS_TYPED_ARRAY_STUB_METHOD_SPECS[];
extern const JsBuiltinMethodSpec JS_TYPED_ARRAY_ACCESSOR_SPECS[];
extern const JsBuiltinMethodSpec JS_DATAVIEW_PROTOTYPE_METHOD_SPECS[];
extern const JsBuiltinMethodSpec JS_DATAVIEW_ACCESSOR_SPECS[];


Item _map_read_field(ShapeEntry* field, void* map_data);
Item _map_get(TypeMap* map_type, void* map_data, const char *key, bool *is_found);

bool js_runtime_trace_enabled();
void js_strict_throw_property_error(const char* reason, const char* prop_name, int prop_len);
Map* js_resolve_object_prototype();
Item js_map_get_fast(Map* m, const char* key_str, int key_len, bool* out_found = nullptr);
Item js_check_array_sym_iterator();
extern "C" void js_note_array_prototype_push_tamper(Item object, Item key);
void js_regex_cache_reset();
void js_module_cache_reset();
void js_reset_transient_call_state();
void js_reset_heap_bound_runtime_state();
void js_assert_batch_runtime_state_clear(const char* reset_name, bool include_heap_bound);
void js_reset_math_object();
void js_reset_json_object();
void js_reset_console_object();
void js_reset_reflect_object();
void js_reset_atomics_object();
void js_reset_262_object();
void js_reset_proto_key();
void js_func_cache_reset();
extern "C" void js_function_set_prototype(Item fn_item, Item proto);
void js_builtin_cache_reset();
void js_deep_batch_reset();

double js_get_number(Item value);
Item js_make_number(double d);
int32_t js_to_int32(double d);

extern "C" Item js_property_get_str(Item object, const char* key, int key_len);
extern "C" Item js_number_function(Item value);
extern "C" bool js_typed_array_is_out_of_bounds_item(Item ta_item);
extern "C" Item js_object_define_property(Item obj, Item name, Item descriptor);
extern "C" Item js_has_own_property(Item obj, Item key);
extern "C" Item js_object_has_own(Item obj, Item key);
extern "C" Item js_object_prototype_has_own_property(Item this_val, Item key);

void js_double_to_string(double d, char* out, int out_size);
bool js_ta_key_canonical_numeric(Item key, double* numeric_index, bool* is_negative_zero);
bool js_ta_numeric_index_valid(Item object, double numeric_index, bool is_negative_zero, int* out_index);
bool js_ta_proto_chain_set(Item object, Item key, Item value);
bool js_array_ta_proto_numeric_set(Item array, Item key, bool* no_op);

static inline bool js_is_symbol(Item v) {
    if (get_type_id(v) == LMD_TYPE_SYMBOL) return true;
    if (get_type_id(v) != LMD_TYPE_INT) return false;
    return it2i(v) <= -(int64_t)JS_SYMBOL_BASE;
}

static inline bool js_is_bigint(Item v) {
    return get_type_id(v) == LMD_TYPE_DECIMAL;
}

static inline bool js_check_bigint_arithmetic(Item left, Item right) {
    bool lbig = js_is_bigint(left);
    bool rbig = js_is_bigint(right);
    if (lbig != rbig) {
        js_throw_type_error("Cannot mix BigInt and other types, use explicit conversions");
        return true;
    }
    return false;
}

static inline Item js_make_bigint(int64_t val) {
    return bigint_from_int64(val);
}

// Helper to make JS undefined value
static inline Item make_js_undefined() {
    return (Item){.item = ((uint64_t)LMD_TYPE_UNDEFINED << 56)};
}

// Sentinel value for deleted properties.
static inline Item make_js_deleted_sentinel() {
    return lam::hole_sentinel_item();
}

static inline bool js_is_deleted_sentinel(Item val) {
    return lam::is_hole_sentinel(val);
}

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

namespace {
class ScopedProxyReceiver {
public:
    explicit ScopedProxyReceiver(Item recv) : prev_(js_proxy_receiver) {
        if (!js_proxy_receiver.item) js_proxy_receiver = recv;
    }
    ~ScopedProxyReceiver() { js_proxy_receiver = prev_; }
    ScopedProxyReceiver(const ScopedProxyReceiver&) = delete;
    ScopedProxyReceiver& operator=(const ScopedProxyReceiver&) = delete;
private:
    Item prev_;
};
} // namespace

#endif
