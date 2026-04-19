#pragma once
// #include <math.h>  // MIR has problem parsing math.h

// Include standard integer types from system
#include <stdint.h>

// Define size_t only when compiled by MIR C compiler (not standard C/C++ compiler)
// MIR doesn't include stddef.h so size_t won't be defined
#if !defined(__cplusplus) && !defined(_SIZE_T) && !defined(_SIZE_T_) && !defined(__SIZE_T__) && !defined(_SYS__TYPES_H_)
typedef uint64_t size_t;
#endif

#if !defined(__cplusplus) && !defined(_STDBOOL_H) && !defined(_STDBOOL_H_) && !defined(__bool_true_false_are_defined)
#define bool uint8_t
#define true 1
#define false 0
#endif

#define null 0

// C math function declarations (for native math optimization in transpiler)
// These are imported from libm at runtime via MIR's import resolver
// Only declare when compiled by C2MIR (not by C++ compiler which includes <cmath>)
#if !defined(__cplusplus)
extern double sin(double x);
extern double cos(double x);
extern double tan(double x);
extern double sqrt(double x);
extern double log(double x);
extern double log10(double x);
extern double exp(double x);
extern double fabs(double x);
extern double floor(double x);
extern double ceil(double x);
extern double round(double x);
// inverse trigonometric
extern double asin(double x);
extern double acos(double x);
extern double atan(double x);
extern double atan2(double y, double x);
// hyperbolic
extern double sinh(double x);
extern double cosh(double x);
extern double tanh(double x);
// inverse hyperbolic
extern double asinh(double x);
extern double acosh(double x);
extern double atanh(double x);
// exponential/logarithmic variants
extern double exp2(double x);
extern double expm1(double x);
extern double log2(double x);
// root
extern double cbrt(double x);
// truncation / misc
extern double trunc(double x);
extern double fmod(double x, double y);
extern double hypot(double y, double x);
extern double log1p(double x);
#endif

// Dry-run mode: when enabled, IO functions return fabricated results
// instead of performing actual network/filesystem operations
#if !defined(__cplusplus)
extern bool g_dry_run;
#endif

// Stack overflow protection (callable from JIT-compiled code)
#ifdef __cplusplus
extern "C"
#endif
void lambda_stack_overflow_error(const char* func_name);

// Name pool configuration
#define NAME_POOL_SYMBOL_LIMIT 32  // Max length for symbols in name_pool

// TCO (Tail Call Optimization) iteration limit
// Guards against infinite loops from tail-recursive functions that never terminate.
// TCO converts tail calls into goto loops, bypassing signal-based stack overflow
// detection, so we use an explicit counter.
#define LAMBDA_TCO_MAX_ITERATIONS 1000000

enum EnumTypeId {
    LMD_TYPE_RAW_POINTER = 0,
    LMD_TYPE_NULL,

    // scalar types
    LMD_TYPE_BOOL,
    // Sized numeric types
    LMD_TYPE_NUM_SIZED,  // inline sized numerics (i8..u32, f16, f32) — packed in Item
    LMD_TYPE_INT,    // int literal, just 32-bit
    LMD_TYPE_INT64,  // int literal, 64-bit
    LMD_TYPE_UINT64, // unsigned 64-bit integer (heap-allocated pointer)
    LMD_TYPE_FLOAT,  // float literal, 64-bit
    LMD_TYPE_DECIMAL,
    LMD_TYPE_NUMBER,  // explicit number, which includes decimal
    LMD_TYPE_DTIME,
    LMD_TYPE_SYMBOL,
    LMD_TYPE_STRING,
    LMD_TYPE_BINARY,

    // Path type for file/URL paths
    LMD_TYPE_PATH,  // segmented path with scheme (file, http, https, sys, etc.)

    // container types, LMD_TYPE_CONTAINER
    LMD_TYPE_RANGE,
    LMD_TYPE_ARRAY_NUM,   // unified numeric array (elem_type selects int/int64/float)
    LMD_TYPE_ARRAY,  // array of Items
    LMD_TYPE_MAP,
    LMD_TYPE_VMAP,  // virtual map with vtable dispatch (hashmap, treemap, etc.)
    LMD_TYPE_ELEMENT,
    LMD_TYPE_OBJECT,  // object = map + type_name + methods (nominally-typed)
    LMD_TYPE_TYPE,
    LMD_TYPE_FUNC,

    LMD_TYPE_ANY,
    LMD_TYPE_ERROR,

    // JavaScript-specific types (added at end to preserve existing type IDs)
    LMD_TYPE_UNDEFINED,  // JavaScript undefined (distinct from null)

    LMD_TYPE_COUNT,  // number of type IDs — must be last before HEAP_START
    LMD_CONTAINER_HEAP_START, // special value for container heap entry start
};
typedef uint8_t TypeId;

// ============================================================================
// Sized numeric sub-types (stored in bits [55:48] of NUM_SIZED Items)
// ============================================================================
enum EnumNumSizedType {
    NUM_INT8 = 0,     // signed 8-bit   [-128, 127]
    NUM_INT16,        // signed 16-bit  [-32768, 32767]
    NUM_INT32,        // signed 32-bit  [-2^31, 2^31-1]
    NUM_UINT8,        // unsigned 8-bit  [0, 255]
    NUM_UINT16,       // unsigned 16-bit [0, 65535]
    NUM_UINT32,       // unsigned 32-bit [0, 2^32-1]
    NUM_FLOAT16,      // IEEE 754 half-precision (16-bit)
    NUM_FLOAT32,      // IEEE 754 single-precision (32-bit)
    NUM_SIZED_COUNT
};
typedef uint8_t NumSizedType;

// ============================================================================
// ArrayNum element sub-types (stored in upper 4 bits of Container.flags byte)
// Lower 4 bits of flags are reserved for Container boolean flags
// (is_content, is_spreadable, is_heap, is_data_migrated)
// ============================================================================
enum EnumArrayNumElemType {
    // Lambda's standard numeric types (8 bytes/element each):
    ELEM_INT   = 0x00,   // 8 bytes  — int56-as-int64 (was ARRAY_INT)
    ELEM_FLOAT = 0x10,   // 8 bytes  — double (was ARRAY_FLOAT)
    ELEM_INT64 = 0x20,   // 8 bytes  — int64 (was ARRAY_INT64)

    // Compact sized integer types:
    ELEM_INT8    = 0x30,  // 1 byte   — maps to NUM_INT8
    ELEM_INT16   = 0x40,  // 2 bytes  — maps to NUM_INT16
    ELEM_INT32   = 0x50,  // 4 bytes  — maps to NUM_INT32
    ELEM_UINT8   = 0x60,  // 1 byte   — maps to NUM_UINT8
    ELEM_UINT16  = 0x70,  // 2 bytes  — maps to NUM_UINT16
    ELEM_UINT32  = 0x80,  // 4 bytes  — maps to NUM_UINT32

    // Compact sized float types:
    ELEM_FLOAT16 = 0x90,  // 2 bytes  — maps to NUM_FLOAT16
    ELEM_FLOAT32 = 0xA0,  // 4 bytes  — maps to NUM_FLOAT32

    // Explicit 64-bit types:
    ELEM_UINT64  = 0xB0,  // 8 bytes
    ELEM_FLOAT64 = 0xC0,  // 8 bytes  — explicit f64 (same storage as ELEM_FLOAT)

    ELEM_NUM_COUNT = 14
};
typedef uint8_t ArrayNumElemType;

// Bytes per element, indexed by (elem_type >> 4)
static const uint8_t ELEM_TYPE_SIZE[16] = {
    8, // 0x00 ELEM_INT     — int64_t
    8, // 0x10 ELEM_FLOAT   — double
    8, // 0x20 ELEM_INT64   — int64_t
    1, // 0x30 ELEM_INT8
    2, // 0x40 ELEM_INT16
    4, // 0x50 ELEM_INT32
    1, // 0x60 ELEM_UINT8
    2, // 0x70 ELEM_UINT16
    4, // 0x80 ELEM_UINT32
    2, // 0x90 ELEM_FLOAT16
    4, // 0xA0 ELEM_FLOAT32
    8, // 0xB0 ELEM_UINT64
    8, // 0xC0 ELEM_FLOAT64
    0, // 0xD0 reserved
    0, // 0xE0 reserved
    0, // 0xF0 reserved
};

// Convert NumSizedType to ArrayNumElemType
static inline ArrayNumElemType num_sized_to_elem_type(NumSizedType nst) {
    switch (nst) {
        case NUM_INT8:    return ELEM_INT8;
        case NUM_INT16:   return ELEM_INT16;
        case NUM_INT32:   return ELEM_INT32;
        case NUM_UINT8:   return ELEM_UINT8;
        case NUM_UINT16:  return ELEM_UINT16;
        case NUM_UINT32:  return ELEM_UINT32;
        case NUM_FLOAT16: return ELEM_FLOAT16;
        case NUM_FLOAT32: return ELEM_FLOAT32;
        default:          return ELEM_INT;
    }
}

// Check if an elem_type uses compact (sub-8-byte) storage
static inline int elem_type_is_compact(ArrayNumElemType et) {
    return ELEM_TYPE_SIZE[et >> 4] < 8;
}

// Sized numeric packing: value in [31:0], sub-type in [55:48], type_id in [63:56]
// Bits [47:32] are unused padding.
#define NUM_SIZED_PACK(num_type, val32) \
    (((uint64_t)LMD_TYPE_NUM_SIZED << 56) | ((uint64_t)(num_type) << 48) | ((uint32_t)(val32)))

// Unpack sub-type and raw 32-bit value from a NUM_SIZED Item
#define NUM_SIZED_SUBTYPE(item)  ((uint8_t)(((uint64_t)(item) >> 48) & 0xFF))
#define NUM_SIZED_RAW32(item)    ((uint32_t)((uint64_t)(item) & 0xFFFFFFFF))

// Convenience packers for each sub-type
#define i8_to_item(v)   NUM_SIZED_PACK(NUM_INT8,   (uint32_t)(uint8_t)(int8_t)(v))
#define i16_to_item(v)  NUM_SIZED_PACK(NUM_INT16,  (uint32_t)(uint16_t)(int16_t)(v))
#define i32_to_item(v)  NUM_SIZED_PACK(NUM_INT32,  (uint32_t)(int32_t)(v))
#define u8_to_item(v)   NUM_SIZED_PACK(NUM_UINT8,  (uint32_t)(uint8_t)(v))
#define u16_to_item(v)  NUM_SIZED_PACK(NUM_UINT16, (uint32_t)(uint16_t)(v))
#define u32_to_item(v)  NUM_SIZED_PACK(NUM_UINT32, (uint32_t)(v))

// uint64 packing macro (heap-allocated pointer, like int64)
#define u64_to_item(uint64_ptr) \
    ((uint64_ptr) ? ((((uint64_t)LMD_TYPE_UINT64) << 56) | (uint64_t)(uint64_ptr)) : ITEM_NULL)

// Get human-readable name for a NumSizedType sub-type
#ifdef __cplusplus
extern "C"
#endif
const char* get_num_sized_type_name(NumSizedType num_type);

// TypeKind enum moved to lambda.hpp (not needed by JIT)

// Get human-readable name for a TypeId (for error messages)
// Implemented in lambda-data.cpp (not inlined — saves ~30 lines from JIT-embedded header)
#ifdef __cplusplus
extern "C"
#endif
const char* get_type_name(TypeId type_id);

// 3-state boolean:
typedef enum {
    BOOL_FALSE = 0,
    BOOL_TRUE = 1,
    BOOL_ERROR = 2
} BoolEnum;
typedef uint8_t Bool;

#define  LMD_TYPE_CONTAINER LMD_TYPE_RANGE

// System function identifiers (moved from lambda-data.hpp for C compatibility)
typedef enum SysFunc {
    SYSFUNC_LEN,
    SYSFUNC_TYPE,
    SYSFUNC_NAME,       // name(item) - get local name of element, function, or type
    SYSFUNC_INT,
    SYSFUNC_INT64,
    SYSFUNC_FLOAT,
    SYSFUNC_DECIMAL,
    SYSFUNC_NUMBER,
    SYSFUNC_STRING,
    //SYSFUNC_CHAR,
    SYSFUNC_SYMBOL,
    SYSFUNC_SYMBOL2,    // symbol(name, url) - 2 args, create namespaced symbol
    SYSFUNC_BINARY,
    SYSFUNC_DATETIME,
    SYSFUNC_DATETIME0,  // datetime() - 0 args, current datetime
    SYSFUNC_DATE,
    SYSFUNC_DATE0,      // date() - 0 args, current date
    SYSFUNC_DATE3,      // date(y,m,d) - 3 args, construct from components
    SYSFUNC_TIME,
    SYSFUNC_TIME0,      // time() - 0 args, current time
    SYSFUNC_TIME3,      // time(h,m,s) - 3 args, construct from components
    SYSFUNC_JUSTNOW,
    SYSFUNC_SET,
    SYSFUNC_SLICE,
    SYSFUNC_ALL,
    SYSFUNC_ANY,
    SYSFUNC_MIN1,
    SYSFUNC_MIN2,
    SYSFUNC_MAX1,
    SYSFUNC_MAX2,
    SYSFUNC_SUM,
    SYSFUNC_AVG,
    SYSFUNC_ABS,
    SYSFUNC_ROUND,
    SYSFUNC_FLOOR,
    SYSFUNC_CEIL,
    SYSFUNC_INPUT1,
    SYSFUNC_INPUT2,
    SYSFUNC_FORMAT1,
    SYSFUNC_FORMAT2,
    SYSFUNC_ERROR,
    SYSFUNC_EXISTS,         // exists(path) - check if file/dir exists
    SYSFUNC_NORMALIZE,
    SYSFUNC_NORMALIZE2,     // normalize(str, form) with 2 args
    // string functions
    SYSFUNC_CONTAINS,
    SYSFUNC_STARTS_WITH,
    SYSFUNC_ENDS_WITH,
    SYSFUNC_INDEX_OF,
    SYSFUNC_LAST_INDEX_OF,
    SYSFUNC_TRIM,
    SYSFUNC_TRIM_START,
    SYSFUNC_TRIM_END,
    SYSFUNC_LOWER,
    SYSFUNC_UPPER,
    SYSFUNC_URL_RESOLVE,
    SYSFUNC_SPLIT,
    SYSFUNC_SPLIT3,         // split(str, sep, keep_delim) with 3 args
    SYSFUNC_JOIN,           // join(strs, sep) for strings
    SYSFUNC_REPLACE,
    SYSFUNC_FIND,           // find(str, pattern) - find all matches
    SYSFUNC_FIND3,          // find(str, pattern, options) - with options
    SYSFUNC_ORD,            // ord(str) - Unicode code point of first character
    SYSFUNC_CHR,            // chr(int) - character from Unicode code point
    // vector functions
    SYSFUNC_PROD,
    SYSFUNC_CUMSUM,
    SYSFUNC_CUMPROD,
    SYSFUNC_ARGMIN,
    SYSFUNC_ARGMAX,
    SYSFUNC_FILL,
    SYSFUNC_DOT,
    SYSFUNC_NORM,
    // statistical functions
    SYSFUNC_MEAN,
    SYSFUNC_MEDIAN,
    SYSFUNC_VARIANCE,
    SYSFUNC_DEVIATION,
    // element-wise math functions
    SYSFUNC_SQRT,
    SYSFUNC_LOG,
    SYSFUNC_LOG10,
    SYSFUNC_EXP,
    SYSFUNC_SIN,
    SYSFUNC_COS,
    SYSFUNC_TAN,
    // inverse trigonometric
    SYSFUNC_ASIN,
    SYSFUNC_ACOS,
    SYSFUNC_ATAN,
    SYSFUNC_ATAN2,
    // hyperbolic
    SYSFUNC_SINH,
    SYSFUNC_COSH,
    SYSFUNC_TANH,
    // inverse hyperbolic
    SYSFUNC_ASINH,
    SYSFUNC_ACOSH,
    SYSFUNC_ATANH,
    // exponential/logarithmic variants
    SYSFUNC_EXP2,
    SYSFUNC_EXPM1,
    SYSFUNC_LOG2,
    // power/root
    SYSFUNC_POW_MATH,
    SYSFUNC_CBRT,
    SYSFUNC_TRUNC,
    SYSFUNC_HYPOT,
    SYSFUNC_LOG1P,
    SYSFUNC_SIGN,
    // random number generation
    SYSFUNC_RANDOM,
    // vector manipulation functions
    SYSFUNC_REVERSE,
    SYSFUNC_SORT,
    SYSFUNC_SORT2,
    SYSFUNC_UNIQUE,
    SYSFUNC_TAKE,
    SYSFUNC_DROP,
    SYSFUNC_ZIP,
    SYSFUNC_RANGE3,
    SYSFUNC_QUANTILE,
    SYSFUNC_REDUCE,         // reduce(collection, fn) - fold/accumulate
    // parse string functions
    SYSFUNC_PARSE1,         // parse(str) - parse string, auto-detect format
    SYSFUNC_PARSE2,         // parse(str, format) - parse string with format
    // variadic parameter access
    SYSFUNC_VARG0,          // varg() - get all variadic args as list
    SYSFUNC_VARG1,          // varg(n) - get nth variadic arg
    // bitwise functions
    SYSFUNC_BAND,
    SYSFUNC_BOR,
    SYSFUNC_BXOR,
    SYSFUNC_BNOT,
    SYSFUNC_SHL,
    SYSFUNC_SHR,
    // procedural functions
    SYSPROC_NOW,
    SYSPROC_TODAY,
    SYSPROC_PRINT,
    SYSPROC_FETCH,
    SYSPROC_OUTPUT2,         // output(source, target) - writes data to target, returns bytes
    SYSPROC_OUTPUT3,         // output(source, target, options) - with options map
    SYSPROC_CMD,
    SYSPROC_CMD1,            // cmd(command) - no args version
    // io module functions (unified I/O - supports local and remote targets)
    SYSPROC_IO_COPY,
    SYSPROC_IO_MOVE,
    SYSPROC_IO_DELETE,
    SYSPROC_IO_MKDIR,
    SYSPROC_IO_TOUCH,
    SYSPROC_IO_SYMLINK,
    SYSPROC_IO_CHMOD,
    SYSPROC_IO_RENAME,
    SYSPROC_IO_FETCH,        // io.fetch(target, options) - fetch data from URL or file
    // io.http module (web server)
    SYSPROC_IO_HTTP_CREATE_SERVER,  // io.http.create_server(config?) - create HTTP server
    SYSPROC_IO_HTTP_LISTEN,         // io.http.listen(server, port) - start listening
    SYSPROC_IO_HTTP_ROUTE,          // io.http.route(server, method, path, handler)
    SYSPROC_IO_HTTP_USE,            // io.http.use(server, middleware) - add middleware
    SYSPROC_IO_HTTP_STATIC,         // io.http.static(server, url_path, dir_path)
    SYSPROC_IO_HTTP_STOP,           // io.http.stop(server) - graceful shutdown
    // vmap functions
    SYSFUNC_VMAP_NEW,        // map() or map([k1,v1,...]) - create VMap
    SYSPROC_VMAP_SET,        // m.set(k, v) - in-place insert on VMap (procedural)
    SYSPROC_CLOCK,           // clock() - high-resolution monotonic time in seconds (float)
    // file-based find/replace (procedural)
    SYSPROC_REPLACE_FILE,    // pn replace(path, pattern, repl) - sed-like file replace
    SYSPROC_REPLACE_FILE4,   // pn replace(path, pattern, repl, options)
    // view/edit template apply
    SYSFUNC_APPLY1,          // apply(target) - apply view templates to target
    SYSFUNC_APPLY2,          // apply(target, options) - apply with options map
    // edit bridge — MarkEditor operations (Phase 4)
    SYSFUNC_EDIT_UNDO,       // undo() - undo last edit commit
    SYSFUNC_EDIT_REDO,       // redo() - redo last undone commit
    SYSFUNC_EDIT_COMMIT,     // commit() - commit current edits as version
    SYSFUNC_EDIT_COMMIT1,    // commit(description) - commit with description
    // reactive UI event dispatch
    SYSPROC_EMIT,            // emit(event_name, data) - dispatch event to parent template handler
} SysFunc;

typedef struct Type {
    TypeId type_id;
    uint8_t kind:4;      // TypeKind: sub-classification (SIMPLE, UNARY, BINARY, PATTERN)
    uint8_t is_literal:1;  // is a literal value
    uint8_t is_const:1;  // is a constant expr
} Type;

typedef struct Container Container;
typedef struct Range Range;
typedef struct List List;
typedef struct List Array;
typedef struct ArrayNum ArrayNum;
typedef ArrayNum ArrayInt;    // compat alias: int56 arrays (elem_type == ELEM_INT)
typedef ArrayNum ArrayInt64;  // compat alias: int64 arrays (elem_type == ELEM_INT64)
typedef ArrayNum ArrayFloat;  // compat alias: float arrays (elem_type == ELEM_FLOAT)
typedef struct Map Map;
typedef struct VMap VMap;
typedef struct Element Element;
typedef struct Object Object;
typedef struct Function Function;
typedef struct Decimal Decimal;
typedef struct TypePattern TypePattern;

/*
* The C verion of Lambda Item and data structures are defined primarily for MIR JIT ciompiler
*/

// only define DateTime if not already defined by lib/datetime.h
#ifndef __cplusplus
typedef uint64_t DateTime;
typedef uint64_t Item;
#else
#include "../lib/datetime.h"
typedef struct Item Item;
#endif

// a fat string with prefixed length and flags
#ifndef STRING_STRUCT_DEFINED
typedef struct String {
    uint32_t len;       // byte length of the string
    uint8_t is_ascii;   // 1 if all bytes < 0x80, 0 otherwise (enables O(1) indexing)
    char chars[];       // UTF-8 string data (null-terminated)
} String;
#define STRING_STRUCT_DEFINED
#endif

typedef struct Target Target;  // forward declaration for Symbol.ns

typedef struct Symbol {
    uint32_t len;       // symbol name length
    Target* ns;         // namespace target (NULL for unqualified symbols)
    char chars[];       // symbol name characters
} Symbol;
typedef String Binary;  // Binary is just a String

// MapKind: discriminates exotic Map sub-types so js_property_get can
// skip 9 cascading sentinel-pointer checks for plain JS objects.
enum MapKind {
    MAP_KIND_PLAIN       = 0,  // regular JS/Lambda object (default, zero-init safe)
    MAP_KIND_TYPED_ARRAY = 1,  // Int8Array, Float64Array, etc.
    MAP_KIND_ARRAYBUFFER = 2,  // ArrayBuffer / SharedArrayBuffer
    MAP_KIND_DATAVIEW    = 3,  // DataView
    MAP_KIND_DOM         = 4,  // DOM nodes, ComputedStyle
    MAP_KIND_CSSOM       = 5,  // Stylesheet, CSSRule, RuleStyleDeclaration
    MAP_KIND_ITERATOR    = 6,  // Synthetic iterator (array, string, typed array)
    MAP_KIND_PROCESS_ENV = 7,  // process.env — coerces all values to strings on set
    MAP_KIND_DOC_PROXY   = 8,  // document proxy — JS document object
    MAP_KIND_PROXY       = 9,  // ES6 Proxy object
};

// Array and List struct defintions needed for for-loop
struct Container {
    TypeId type_id;
    union {
        uint8_t flags;
        struct {
            uint8_t is_content:1;    // whether it is a content list, or value list
            uint8_t is_spreadable:1; // whether this array should be spread when added to collections
            uint8_t is_heap:1;       // whether allocated from runtime heap (vs arena for input docs)
            uint8_t is_data_migrated:1; // data buffer migrated from input pool to runtime pool (for mutated markup containers)
            uint8_t map_kind:4;      // MapKind tag (0 = plain, upper 4 bits of flags byte)
        };
    };
};

// List/Array flags (stored in List.flags / Array.flags field)

#ifndef __cplusplus
    struct Range {
        TypeId type_id;
        uint8_t flags;
        //---------------------
        int64_t start;  // inclusive start
        int64_t end;    // inclusive end
        int64_t length;
    };

    struct List {
        TypeId type_id;
        uint8_t flags;
        //---------------------
        Item* items;  // pointer to items
        int64_t length;  // number of items
        int64_t extra;   // count of extra items stored at the end of the list
        int64_t capacity;  // allocated capacity
    };

    struct ArrayNum {
        TypeId type_id;
        uint8_t elem_type;     // ArrayNumElemType (replaces flags for typed arrays)
        //---------------------
        union {
            int64_t* items;        // for ELEM_INT, ELEM_INT64
            double* float_items;   // for ELEM_FLOAT
            void* data;            // for compact types (ELEM_INT8, ELEM_UINT8, etc.)
        };
        int64_t length;  // number of elements
        int64_t extra;   // count of extra elements stored at end
        int64_t capacity;  // allocated capacity
    };

    // ArrayList definition for MIR runtime (item_keys return type)
#ifndef _ARRAYLIST_STRUCT_DEFINED
#define _ARRAYLIST_STRUCT_DEFINED
    typedef void *ArrayListValue;
    struct _ArrayList {
        ArrayListValue *data;
        int length;
        int _alloced;
    };
#endif

    // Map, Object, Element struct definitions for direct field access optimization
    // Layout must match the C++ structs in lambda.hpp exactly
    struct Map {
        TypeId type_id;
        uint8_t flags;
        //---------------------
        void* type;       // TypeMap* — shape/type info
        void* data;       // packed data struct of the map fields
        int data_cap;     // capacity of the data buffer
    };

    struct Object {
        TypeId type_id;
        uint8_t flags;
        //---------------------
        void* type;       // TypeObject* — shape + methods + type_name
        void* data;       // packed field data (same layout as Map)
        int data_cap;     // data buffer capacity
    };

    struct Element {
        TypeId type_id;
        uint8_t flags;
        //---------------------
        Item* items;       // list content items
        int64_t length;    // number of content items
        int64_t extra;     // count of extra items
        int64_t capacity;  // allocated capacity
        //---------------------
        void* type;        // TypeElmt* — attr type/shape
        void* data;        // packed data struct of the attrs
        int data_cap;      // capacity of the data buffer
    };

#endif

Range* range();
long range_get(Range *range, int64_t index);

List* list();  // constructs an empty list
Item list_fill(List *list, int cnt, ...);  // fill the list with the items
void list_push(List *list, Item item);
void list_push_spread(List *list, Item item);  // push item, spreading if spreadable array
Item list_end(List *list);

// Spreadable array functions for for-expression results
Array* array_plain();  // constructs a plain empty array (no frame management)
void array_drop_inplace(Array* arr, int64_t n);  // drop first n items in-place
void array_limit_inplace(Array* arr, int64_t n);  // limit to first n items in-place
Array* array_spreadable();  // constructs a spreadable empty array
void array_push(Array* arr, Item item);  // push item to array
void array_push_spread(Array* arr, Item item);      // push item, spreading if spreadable array
void array_push_spread_all(Array* arr, Item item);  // push item, spreading any array (for pipe exprs in array literals)
Item array_end(Array* arr);  // finalize and return array as Item

// Mark an item as spreadable (for spread operator *expr)
Item item_spread(Item item);

typedef void* (*fn_ptr)();

// Function flags (stored in Function.flags field)
#define FN_FLAG_BOXED_RET     0x01  // bit 0: fn->ptr returns RetItem instead of Item
#define FN_FLAG_HAS_KWARGS    0x02  // bit 1: fn->ptr has an extra Item **kwargs_map param
#define FN_FLAG_IS_GENERATOR  0x04  // bit 2: function is a Python generator (resume fn, frame in closure_env)
#define FN_FLAG_IS_COROUTINE  0x08  // bit 3: function is a Python coroutine (async def)

// Function as first-class value
// Supports both direct function references and closures
struct Function {
    uint8_t type_id;
    uint8_t arity;               // number of parameters (0-255)
    uint8_t closure_field_count;  // number of Item fields in closure_env (0 if not a closure)
    uint8_t flags;               // function flags (FN_FLAG_BOXED_RET, etc.)
    // --- 4 bytes padding --- (offset 4..7)
    void* fn_type;        // fn type definition (TypeFunc*)
    fn_ptr ptr;           // native function pointer
    void* closure_env;    // closure environment (NULL if no captures)
    const char* name;     // function name for stack traces (may be NULL)
};

// Dynamic function invocation for first-class functions
Item fn_call(Function* fn, List* args);
Item fn_call0(Function* fn);
Item fn_call1(Function* fn, Item a);
Item fn_call2(Function* fn, Item a, Item b);
Item fn_call3(Function* fn, Item a, Item b, Item c);

// Forward declaration for Pool (full definition at line ~359)
typedef struct Pool Pool;

// Path: segmented symbol for file/URL paths
// A path is a linked chain of segments from leaf to root
// Example: file.etc.hosts -> Path("hosts") -> Path("etc") -> Path("file") -> ROOT
typedef struct Path Path;
typedef struct PathMeta PathMeta;

// Path/PathMeta full definitions, path enums, macros, and most Path API
// moved to lambda-path.h (not needed by JIT-compiled code).
// Target/Name structs and APIs moved to lambda.hpp.

// Path construction API (called by JIT-generated code)
Path* path_new(Pool* pool, int scheme);                           // Create new path with scheme
Path* path_extend(Pool* pool, Path* base, const char* segment);   // Extend path with segment
Path* path_concat(Pool* pool, Path* base, Path* suffix);          // Concatenate two paths
Path* path_wildcard(Pool* pool, Path* base);                      // Add * wildcard segment
Path* path_wildcard_recursive(Pool* pool, Path* base);            // Add ** wildcard segment

// System function: exists() - check if file/directory exists (called by JIT)
Bool fn_exists(Item path);

// Create function wrappers for first-class usage
Function* to_fn(fn_ptr ptr);
Function* to_fn_n(fn_ptr ptr, int arity);
Function* to_fn_named(fn_ptr ptr, int arity, const char* name);
Function* to_closure(fn_ptr ptr, int arity, void* env);
Function* to_closure_named(fn_ptr ptr, int arity, void* env, const char* name);

// Memory allocation for closure environments
#ifdef __cplusplus
extern "C" {
#endif
void* heap_calloc(size_t size, TypeId type_id);
void* heap_calloc_class(size_t size, TypeId type_id, int cls);  // allocate with pre-computed size class
void* heap_data_calloc(size_t size);  // allocate GC-managed data buffer (for map/object data)
// String creation for name pooling
String* heap_create_name(const char* name);
// String creation for runtime strings
String* heap_strcpy(char* src, int64_t len);
// Symbol creation for runtime symbols
Symbol* heap_create_symbol(const char* symbol, size_t len);
#ifdef __cplusplus
}
#endif

#define INT64_ERROR           INT64_MAX
#define LAMBDA_INT64_MAX    (INT64_MAX - 1)

// DateTime error sentinel — all bits set = clearly invalid
// month=15 (impossible: months are 1-12), every field at maximum
// Used by DateTime-returning functions to signal errors
#define DATETIME_ERROR_VALUE  0xFFFFFFFFFFFFFFFFULL

// Check if a DateTime value is the error sentinel
// Works in both C (uint64_t) and C++ (struct with int64_val)
#ifdef __cplusplus
#define DATETIME_IS_ERROR(dt) ((dt).int64_val == DATETIME_ERROR_VALUE)
#else
#define DATETIME_IS_ERROR(dt) ((dt) == DATETIME_ERROR_VALUE)
#endif

// Create a DateTime error value
#ifdef __cplusplus
#define DATETIME_MAKE_ERROR() (DateTime{.int64_val = DATETIME_ERROR_VALUE})
#else
#define DATETIME_MAKE_ERROR() ((DateTime)DATETIME_ERROR_VALUE)
#endif

#define ITEM_UNDEFINED      0
#define ITEM_NULL           ((uint64_t)LMD_TYPE_NULL << 56)
#define ITEM_NULL_SPREADABLE ((uint64_t)LMD_TYPE_NULL << 56 | 1)  // spreadable null (skip when spreading)
#define ITEM_JS_UNDEFINED   ((uint64_t)LMD_TYPE_UNDEFINED << 56)  // JavaScript undefined
#define ITEM_JS_TDZ         ((uint64_t)LMD_TYPE_UNDEFINED << 56 | 1)  // TDZ sentinel for let/const
#define ITEM_INT            ((uint64_t)LMD_TYPE_INT << 56)
#define ITEM_ERROR          ((uint64_t)LMD_TYPE_ERROR << 56)

// numeric type range check — includes sized types outside INT..NUMBER range
#define IS_NUMERIC_ID(t) (((t) >= LMD_TYPE_INT && (t) <= LMD_TYPE_NUMBER) || \
                          (t) == LMD_TYPE_NUM_SIZED || (t) == LMD_TYPE_UINT64)

#define ITEM_TRUE           ((uint64_t)LMD_TYPE_BOOL << 56) | (uint8_t)1
#define ITEM_FALSE          ((uint64_t)LMD_TYPE_BOOL << 56) | (uint8_t)0

// int56 limits: signed 56-bit integer range
#define INT56_MAX  ((int64_t)0x007FFFFFFFFFFFFF)   // +36,028,797,018,963,967
#define INT56_MIN  ((int64_t)0xFF80000000000000LL) // -36,028,797,018,963,968

inline uint64_t b2it(uint8_t bool_val) {
    return bool_val >= BOOL_ERROR ? ITEM_ERROR : ((((uint64_t)LMD_TYPE_BOOL)<<56) | bool_val);
}
// int56: check range and pack, return ITEM_ERROR on overflow
#ifndef __cplusplus
#define i2it(int_val)        (((int64_t)(int_val) <= INT56_MAX && (int64_t)(int_val) >= INT56_MIN) ? (ITEM_INT | ((uint64_t)(int_val) & 0x00FFFFFFFFFFFFFF)) : ITEM_ERROR)
#else
#define i2it(int_val)        (((int64_t)(int_val) <= INT56_MAX && (int64_t)(int_val) >= INT56_MIN) ? (ITEM_INT | ((uint64_t)(int_val) & 0x00FFFFFFFFFFFFFF)) : ITEM_ERROR)
#endif
#define l2it(long_ptr)       ((long_ptr)? ((((uint64_t)LMD_TYPE_INT64)<<56) | (uint64_t)(long_ptr)): ITEM_NULL)
#define d2it(double_ptr)     ((double_ptr)? ((((uint64_t)LMD_TYPE_FLOAT)<<56) | (uint64_t)(double_ptr)): ITEM_NULL)
#define c2it(decimal_ptr)    ((decimal_ptr)? ((((uint64_t)LMD_TYPE_DECIMAL)<<56) | (uint64_t)(decimal_ptr)): ITEM_NULL)
#define s2it(str_ptr)        ((str_ptr)? ((((uint64_t)LMD_TYPE_STRING)<<56) | (uint64_t)(str_ptr)): ITEM_NULL)
#define y2it(sym_ptr)        ((sym_ptr)? ((((uint64_t)LMD_TYPE_SYMBOL)<<56) | (uint64_t)(sym_ptr)): ITEM_NULL)
#define x2it(bin_ptr)        ((bin_ptr)? ((((uint64_t)LMD_TYPE_BINARY)<<56) | (uint64_t)(bin_ptr)): ITEM_NULL)
#define k2it(dtime_ptr)      ((dtime_ptr)? ((((uint64_t)LMD_TYPE_DTIME)<<56) | (uint64_t)(dtime_ptr)): ITEM_NULL)
#define u2it(uint64_ptr)     ((uint64_ptr)? ((((uint64_t)LMD_TYPE_UINT64)<<56) | (uint64_t)(uint64_ptr)): ITEM_NULL)

// Float16/Float32 packing into NUM_SIZED Items
// float32: store IEEE 754 binary32 bit pattern in low 32 bits
// C2MIR: import from native runtime (C2MIR has issues with float in inline functions)
// Native: use __builtin_memcpy for type-safe bit conversion
#if defined(LAMBDA_C2MIR_RUNTIME)
extern uint32_t f32_to_bits(float f);
extern float bits_to_f32(uint32_t u);
#else
static inline uint32_t f32_to_bits(float f) { uint32_t u; __builtin_memcpy(&u, &f, 4); return u; }
static inline float bits_to_f32(uint32_t u) { float f; __builtin_memcpy(&f, &u, 4); return f; }
#endif
#define f32_to_item(v) NUM_SIZED_PACK(NUM_FLOAT32, f32_to_bits((float)(v)))

// float16: software conversion (IEEE 754 binary16)
static inline uint16_t f32_to_f16_bits(float f) {
    uint32_t b = f32_to_bits(f);
    uint32_t sign = (b >> 16) & 0x8000;
    int32_t  expo = ((b >> 23) & 0xFF) - 127 + 15;
    uint32_t mant = (b >> 13) & 0x03FF;
    if (expo <= 0) return (uint16_t)sign;         // underflow → ±0
    if (expo >= 31) return (uint16_t)(sign | 0x7C00); // overflow → ±inf
    return (uint16_t)(sign | (expo << 10) | mant);
}
static inline float f16_bits_to_f32(uint16_t h) {
    uint32_t sign = ((uint32_t)h & 0x8000) << 16;
    uint32_t expo = (h >> 10) & 0x1F;
    uint32_t mant = h & 0x03FF;
    if (expo == 0) { if (mant == 0) return bits_to_f32(sign); /* denorm → 0 */ }
    if (expo == 31) return bits_to_f32(sign | 0x7F800000 | (mant << 13)); // inf/nan
    uint32_t result = sign | ((expo - 15 + 127) << 23) | (mant << 13);
    return bits_to_f32(result);
}
#define f16_to_item(v) NUM_SIZED_PACK(NUM_FLOAT16, (uint32_t)f32_to_f16_bits((float)(v)))

// Unpack sized numeric value from Item
static inline int8_t   item_to_i8(uint64_t it)  { return (int8_t)(NUM_SIZED_RAW32(it) & 0xFF); }
static inline int16_t  item_to_i16(uint64_t it) { return (int16_t)(NUM_SIZED_RAW32(it) & 0xFFFF); }
static inline int32_t  item_to_i32(uint64_t it) { return (int32_t)NUM_SIZED_RAW32(it); }
static inline uint8_t  item_to_u8(uint64_t it)  { return (uint8_t)(NUM_SIZED_RAW32(it) & 0xFF); }
static inline uint16_t item_to_u16(uint64_t it) { return (uint16_t)(NUM_SIZED_RAW32(it) & 0xFFFF); }
static inline uint32_t item_to_u32(uint64_t it) { return NUM_SIZED_RAW32(it); }
static inline float    item_to_f32(uint64_t it) { return bits_to_f32(NUM_SIZED_RAW32(it)); }
static inline float    item_to_f16(uint64_t it) { return f16_bits_to_f32((uint16_t)(NUM_SIZED_RAW32(it) & 0xFFFF)); }

// ============================================================================
// Forward declaration for structured error handling
// ============================================================================
typedef struct LambdaError LambdaError;

// ============================================================================
// Container unboxing helpers: Item → native container pointer
// These validate the type tag and extract the pointer; return NULL on mismatch.
// In C++ mode, these are defined in lambda.hpp (after full Item struct definition).
// ============================================================================

#ifndef __cplusplus
// Container unboxing: Item → native pointer (simple cast).
// Container Items store direct pointers (no type tag in the high bits),
// so no masking is needed — just cast to the target pointer type.
#define it2map(item)    ((Map*)(uintptr_t)(item))
#define it2list(item)   ((List*)(uintptr_t)(item))
#define it2elmt(item)   ((Element*)(uintptr_t)(item))
#define it2obj(item)    ((Object*)(uintptr_t)(item))
#define it2arr(item)    ((Array*)(uintptr_t)(item))
#define it2range(item)  ((Range*)(uintptr_t)(item))
#define it2path(item)   ((Path*)(uintptr_t)(item))
#define it2p(item)      ((void*)(uintptr_t)(item))

// Container boxing helper: native pointer → Item
static inline Item p2it(void* ptr) {
    if (!ptr) return ITEM_NULL;
    return (Item)(uint64_t)(uintptr_t)ptr;
}

// Convert LambdaError* → Error Item (LMD_TYPE_ERROR-tagged pointer)
static inline Item err2it(LambdaError* err) {
    if (!err) return ITEM_NULL;
    return (Item)(((uint64_t)LMD_TYPE_ERROR << 56) | (uint64_t)(uintptr_t)err);
}

// Convert Error Item → LambdaError* (extract pointer from tagged Item)
static inline LambdaError* it2err(Item item) {
    uint8_t tag = (uint64_t)item >> 56;
    if (tag != LMD_TYPE_ERROR) return null;
    return (LambdaError*)(uintptr_t)((uint64_t)item & 0x00FFFFFFFFFFFFFFULL);
}
#endif // !__cplusplus

// ============================================================================
// Per-type Ret* result structs (2-field: value + err)
// Used by can_raise functions to return typed native values with error info.
// Size: 16 bytes — fits in rax+rdx on x86-64, x0+x1 on ARM64.
// err == NULL means success; err != NULL means error (check err->code).
// ============================================================================

typedef struct RetBool   { bool         value; LambdaError* err; } RetBool;
typedef struct RetInt56  { int64_t      value; LambdaError* err; } RetInt56;   // Lambda int (56-bit inline)
typedef struct RetInt64  { int64_t      value; LambdaError* err; } RetInt64;   // int64 (heap-allocated)
typedef struct RetFloat  { double       value; LambdaError* err; } RetFloat;
typedef struct RetString { String*      value; LambdaError* err; } RetString;
typedef struct RetSymbol { Symbol*      value; LambdaError* err; } RetSymbol;
typedef struct RetMap    { Map*         value; LambdaError* err; } RetMap;
typedef struct RetList   { List*        value; LambdaError* err; } RetList;
typedef struct RetElmt   { Element*     value; LambdaError* err; } RetElmt;
typedef struct RetObj    { Object*      value; LambdaError* err; } RetObj;
typedef struct RetArray  { Array*       value; LambdaError* err; } RetArray;
typedef struct RetRange  { Range*       value; LambdaError* err; } RetRange;
typedef struct RetPath   { Path*        value; LambdaError* err; } RetPath;
#ifndef __cplusplus
typedef struct RetItem   { Item         value; LambdaError* err; } RetItem;
#endif
#ifdef __cplusplus
struct RetItem;  // full definition in lambda.hpp
#endif

// ============================================================================
// Ret* constructor helpers
// Naming: r + type_abbreviation + _ok / _err
// ============================================================================

// RetItem (boxed, universal — used by _b trampolines)
// In C++ mode, these are defined in lambda.hpp (after full Item struct definition).
#ifndef __cplusplus
static inline RetItem ri_ok(Item value) {
    RetItem r; r.value = value; r.err = null; return r;
}
static inline RetItem ri_err(LambdaError* error) {
    RetItem r; r.value = ITEM_ERROR; r.err = error; return r;
}
#endif

// RetBool
static inline RetBool rb_ok(bool value) {
    RetBool r; r.value = value; r.err = null; return r;
}
static inline RetBool rb_err(LambdaError* error) {
    RetBool r; r.value = false; r.err = error; return r;
}

// RetInt56 (Lambda int)
static inline RetInt56 ri56_ok(int64_t value) {
    RetInt56 r; r.value = value; r.err = null; return r;
}
static inline RetInt56 ri56_err(LambdaError* error) {
    RetInt56 r; r.value = 0; r.err = error; return r;
}

// RetInt64
static inline RetInt64 ri64_ok(int64_t value) {
    RetInt64 r; r.value = value; r.err = null; return r;
}
static inline RetInt64 ri64_err(LambdaError* error) {
    RetInt64 r; r.value = 0; r.err = error; return r;
}

// RetFloat
static inline RetFloat rf_ok(double value) {
    RetFloat r; r.value = value; r.err = null; return r;
}
static inline RetFloat rf_err(LambdaError* error) {
    RetFloat r; r.value = 0.0; r.err = error; return r;
}

// RetString
static inline RetString rs_ok(String* value) {
    RetString r; r.value = value; r.err = null; return r;
}
static inline RetString rs_err(LambdaError* error) {
    RetString r; r.value = null; r.err = error; return r;
}

// RetSymbol
static inline RetSymbol rsy_ok(Symbol* value) {
    RetSymbol r; r.value = value; r.err = null; return r;
}
static inline RetSymbol rsy_err(LambdaError* error) {
    RetSymbol r; r.value = null; r.err = error; return r;
}

// RetMap
static inline RetMap rm_ok(Map* value) {
    RetMap r; r.value = value; r.err = null; return r;
}
static inline RetMap rm_err(LambdaError* error) {
    RetMap r; r.value = null; r.err = error; return r;
}

// RetList
static inline RetList rl_ok(List* value) {
    RetList r; r.value = value; r.err = null; return r;
}
static inline RetList rl_err(LambdaError* error) {
    RetList r; r.value = null; r.err = error; return r;
}

// RetElmt
static inline RetElmt re_ok(Element* value) {
    RetElmt r; r.value = value; r.err = null; return r;
}
static inline RetElmt re_err(LambdaError* error) {
    RetElmt r; r.value = null; r.err = error; return r;
}

// RetObj
static inline RetObj ro_ok(Object* value) {
    RetObj r; r.value = value; r.err = null; return r;
}
static inline RetObj ro_err(LambdaError* error) {
    RetObj r; r.value = null; r.err = error; return r;
}

// RetArray
static inline RetArray ra_ok(Array* value) {
    RetArray r; r.value = value; r.err = null; return r;
}
static inline RetArray ra_err(LambdaError* error) {
    RetArray r; r.value = null; r.err = error; return r;
}

// RetRange
static inline RetRange rr_ok(Range* value) {
    RetRange r; r.value = value; r.err = null; return r;
}
static inline RetRange rr_err(LambdaError* error) {
    RetRange r; r.value = null; r.err = error; return r;
}

// RetPath
static inline RetPath rp_ok(Path* value) {
    RetPath r; r.value = value; r.err = null; return r;
}
static inline RetPath rp_err(LambdaError* error) {
    RetPath r; r.value = null; r.err = error; return r;
}

// ============================================================================
// Compatibility shims for incremental migration
// In C++ mode, these are defined in lambda.hpp (after full Item struct definition).
// ============================================================================
#ifndef __cplusplus

// Wrap a legacy Item-returning function result into RetItem
// Note: Error Items (from fn_error()) don't carry a real LambdaError* pointer.
// They're just tagged values (LMD_TYPE_ERROR in high 8 bits, pointer=0).
// So we store the error Item in .value and use .err as a boolean sentinel.
static inline RetItem item_to_ri(Item item) {
    RetItem r;
    r.value = item;
    r.err = ((uint64_t)item >> 56 == LMD_TYPE_ERROR) ? (LambdaError*)1 : null;
    return r;
}

// Extract Item from RetItem (for legacy callers expecting plain Item)
// .value always holds the actual Item — whether error or normal value.
static inline Item ri_to_item(RetItem ri) {
    return ri.value;
}

#endif // !__cplusplus

Array* array_fill(Array* arr, int count, ...);
ArrayNum* array_int_fill(ArrayNum* arr, int count, ...);
ArrayNum* array_int64_fill(ArrayNum* arr, int count, ...);
ArrayNum* array_float_fill(ArrayNum* arr, int count, ...);

typedef struct Map Map;
Map* map_fill(Map* map, ...);

typedef struct Element Element;
Element* elmt_fill(Element *elmt, ...);

typedef struct Url Url;
typedef struct Pool Pool;
typedef struct Arena Arena;

// Forward declaration of ArrayList (defined in lib/arraylist.h)
typedef struct _ArrayList ArrayList;

typedef struct Context {
    Pool* pool;
    Arena* arena;  // arena allocator (for input parsing path; also result arena in ui_mode)
    void** consts;
    void* type_list;  // type definitions list (ArrayList* at runtime, void* for JIT access)
    Url* cwd;  // current working directory
    void* (*context_alloc)(int size, TypeId type_id);
    bool run_main; // whether to run main procedure on start
    bool disable_string_merging; // disable automatic string merging in list_push
    uintptr_t stack_limit; // stack overflow check limit (from lambda_stack_init)
    bool ui_mode; // allocate fat DomElement/DomText on arena for unified DOM tree
} Context;

#ifndef LAMBDA_STATIC
#ifdef __cplusplus
extern "C" {
#endif
    Array* array();
    ArrayNum* array_int();
    ArrayNum* array_int64();
    ArrayNum* array_float();

    ArrayNum* array_num_new(ArrayNumElemType elem_type, int64_t length);
    ArrayNum* array_int_new(int64_t length);
    ArrayNum* array_int64_new(int64_t length);
    ArrayNum* array_float_new(int64_t length);

    void array_float_set(ArrayNum *arr, int64_t index, double value);
    void array_int_set(ArrayNum *arr, int64_t index, int64_t value);
    void array_num_set_item(ArrayNum *arr, int64_t index, Item value);

    Map* map(int64_t type_index);
    Map* map_with_data(int64_t type_index);
    Map* map_with_tl(int64_t type_index, void* type_list_ptr);
    Element* elmt(int64_t type_index);
    Element* elmt_with_tl(int64_t type_index, void* type_list_ptr);
    Object* object(int64_t type_index);
    Object* object_with_data(int64_t type_index);
    Object* object_with_tl(int64_t type_index, void* type_list_ptr);
    Object* object_fill(Object* obj, ...);

    // these getters use runtime num_stack
    Item array_get(Array *array, int64_t index);
    Item array_num_get(ArrayNum *array, int64_t index);
    Item array_int_get(ArrayNum *array, int64_t index);
    Item array_int64_get(ArrayNum* array, int64_t index);
    Item array_float_get(ArrayNum* array, int64_t index);
    // fast-path getters: return native types, skip boxing
    int64_t array_int_get_raw(ArrayNum *array, int64_t index);
    int64_t array_int64_get_raw(ArrayNum *array, int64_t index);
    double array_float_get_value(ArrayNum *arr, int64_t index);
    Item list_get(List *list, int64_t index);
    Item map_get(Map* map, Item key);
    Item elmt_get(Element *elmt, Item key);
    Item object_get(Object* obj, Item key);
    void object_type_set_method(int64_t type_index, const char* method_name,
                                fn_ptr func_ptr, int64_t arity, int64_t is_proc);
    void object_type_set_constraint(int64_t type_index, fn_ptr constraint_func);
    Item item_at(Item data, int64_t index);
    Item item_attr(Item data, const char* key);  // get attribute by name
    struct _ArrayList* item_keys(Item data);     // get list of attribute names

    // Unified for-loop iteration helpers (key_filter: 0=ALL, 1=INT, 2=SYMBOL)
    int64_t iter_len(Item data, void* keys_ptr, int key_filter);
    Item iter_key_at(Item data, void* keys_ptr, int64_t idx, int key_filter);
    Item iter_val_at(Item data, void* keys_ptr, int64_t idx, int key_filter);

    Bool is_truthy(Item item);
    Item v2it(List *list);

    Item push_d(double dval);
    Item push_l(int64_t lval);
    Item push_l_safe(int64_t val);  // safe boxing: detects already-boxed INT64 Items
    Item push_d_safe(double val);   // safe boxing: detects already-boxed FLOAT Items
    Item push_k(DateTime dtval);
    Item push_k_safe(DateTime val); // safe boxing: detects already-boxed DTIME Items
    Item push_c(int64_t cval);

    // Const pool pointer — modules override this single macro to redirect to module-local consts
    #define _const_pool  rt->consts

    #define const_d2it(index)    d2it(_const_pool[index])
    #define const_l2it(index)    l2it(_const_pool[index])
    #define const_c2it(index)    c2it(_const_pool[index])
    #define const_s2it(index)    s2it(_const_pool[index])
    #define const_y2it(index)    y2it(_const_pool[index])
    #define const_k2it(index)    k2it(_const_pool[index])
    #define const_x2it(index)    x2it(_const_pool[index])

    #define const_s(index)      ((String*)_const_pool[index])
    #define const_c(index)      ((Decimal*)_const_pool[index])
    #define const_k(index)      (*(DateTime*)_const_pool[index])

    // item unboxing
    int64_t it2l(Item item);
    double it2d(Item item);
    bool it2b(Item item);
    int64_t it2i(Item item);
    String* it2s(Item item);
    const char* fn_to_cstr(Item item);  // convert Item to C string (for path segment names)

    // MIR JIT workaround: opaque store functions prevent SSA optimizer from
    // reordering swap-pattern assignments inside while loops.
    // Since these are external functions, MIR can't inline or reorder them.
    void _store_i64(int64_t* dst, int64_t val);
    void _store_f64(double* dst, double val);

    // Safe unbox to int64_t for bitwise operation arguments.
    // Handles both tagged Items (type tag in high byte) and raw int64_t values
    // (from other bitwise ops or literals, with high byte == 0).
    int64_t _barg(Item v);

    // generic field access function
    Item fn_index(Item item, Item index);
    Item fn_member(Item item, Item key);
    // length function
    int64_t fn_len(Item item);
    Item fn_int(Item a);
    int64_t fn_int64(Item a);
    Item fn_float(Item a);
    Item fn_decimal(Item a);
    Item fn_binary(Item a);

    Item fn_add(Item a, Item b);
    Item fn_mul(Item a, Item b);
    Item fn_sub(Item a, Item b);
    Item fn_div(Item a, Item b);
    Item fn_idiv(Item a, Item b);
    Item fn_pow(Item a, Item b);
    Item fn_mod(Item a, Item b);
    Item fn_abs(Item a);
    Item fn_round(Item a);
    Item fn_floor(Item a);
    Item fn_ceil(Item a);
    Item fn_min1(Item a);
    Item fn_min2(Item a, Item b);
    Item fn_max1(Item a);
    Item fn_max2(Item a, Item b);
    Item fn_sum(Item a);
    Item fn_avg(Item a);
    Item fn_pos(Item a);
    Item fn_neg(Item a);

    // truthy idioms
    Item fn_and(Item a, Item b);
    Item fn_or(Item a, Item b);
    Item op_and(Bool a, Bool b);
    Item op_or(Bool a, Bool b);

    Bool fn_eq(Item a, Item b);
    Bool fn_ne(Item a, Item b);
    Bool fn_str_eq_ptr(String* a, String* b);
    Bool fn_sym_eq_ptr(Symbol* a, Symbol* b);
    Bool fn_lt(Item a, Item b);
    Bool fn_gt(Item a, Item b);
    Bool fn_le(Item a, Item b);
    Bool fn_ge(Item a, Item b);
    Bool fn_not(Item a);
    Bool fn_is(Item a, Item b);
    Bool fn_is_nan(Item a);  // IEEE NaN check: expr is nan
    Bool fn_in(Item a, Item b);

    // query operations: search data for items matching a type
    Item fn_query(Item data, Item type_val, int direct);

    // vector arithmetic operations (element-wise)
    Item vec_add(Item a, Item b);
    Item vec_sub(Item a, Item b);
    Item vec_mul(Item a, Item b);
    Item vec_div(Item a, Item b);
    Item vec_mod(Item a, Item b);
    Item vec_pow(Item a, Item b);

    // vector system functions (math module)
    Item fn_math_prod(Item a);
    Item fn_math_cumsum(Item a);
    Item fn_math_cumprod(Item a);
    Item fn_argmin(Item a);
    Item fn_argmax(Item a);
    Item fn_fill(Item n, Item value);
    Item fn_math_dot(Item a, Item b);
    Item fn_math_norm(Item a);
    // statistical functions (math module)
    Item fn_math_mean(Item a);
    Item fn_math_median(Item a);
    Item fn_math_variance(Item a);
    Item fn_math_deviation(Item a);
    // element-wise math functions (math module)
    Item fn_math_sqrt(Item a);
    Item fn_math_log(Item a);
    Item fn_math_log10(Item a);
    Item fn_math_exp(Item a);
    Item fn_math_sin(Item a);
    Item fn_math_cos(Item a);
    Item fn_math_tan(Item a);
    // inverse trigonometric
    Item fn_math_asin(Item a);
    Item fn_math_acos(Item a);
    Item fn_math_atan(Item a);
    Item fn_math_atan2(Item a, Item b);
    // hyperbolic
    Item fn_math_sinh(Item a);
    Item fn_math_cosh(Item a);
    Item fn_math_tanh(Item a);
    // inverse hyperbolic
    Item fn_math_asinh(Item a);
    Item fn_math_acosh(Item a);
    Item fn_math_atanh(Item a);
    // exponential/logarithmic variants
    Item fn_math_exp2(Item a);
    Item fn_math_expm1(Item a);
    Item fn_math_log2(Item a);
    // power/root
    Item fn_math_pow(Item a, Item b);
    Item fn_math_cbrt(Item a);
    Item fn_trunc(Item a);
    Item fn_math_hypot(Item a, Item b);
    Item fn_math_log1p(Item a);
    Item fn_sign(Item a);
    // random number generation (pure functional, SplitMix64)
    Item fn_math_random(Item seed);

    // ============================================================================
    // UNBOXED SYSTEM FUNCTIONS (fn_*_u)
    // Native C implementations that bypass Item boxing overhead.
    // Called directly when types are known at compile time.
    // ============================================================================

    // Math functions (double → double)
    double fn_pow_u(double base, double exponent);
    double fn_min2_u(double a, double b);
    double fn_max2_u(double a, double b);

    // Integer operations
    int64_t fn_abs_i(int64_t x);
    double fn_abs_f(double x);
    int64_t fn_neg_i(int64_t x);
    double fn_neg_f(double x);
    int64_t fn_mod_i(int64_t a, int64_t b);    // handles div-by-zero (returns INT64_ERROR)
    int64_t fn_idiv_i(int64_t a, int64_t b);   // handles div-by-zero (returns INT64_ERROR)

    // Collection length — type-specialized native variants
    int64_t fn_len_l(List* list);       // list length
    int64_t fn_len_a(Array* arr);       // array length
    int64_t fn_len_s(String* str);      // string length (UTF-8 aware)
    int64_t fn_len_e(Element* elmt);    // element children count

    // Boolean operations
    Bool fn_not_u(Bool x);

    // Sign function
    int64_t fn_sign_i(int64_t x);
    int64_t fn_sign_f(double x);

    // JS Math.round (rounds to +Infinity for ties)
    double js_math_round(double x);

    // JS-semantic Math functions (boxed Item → boxed Item, handle NaN/-0/Infinity)
    Item js_math_trunc(Item x);
    Item js_math_sign(Item x);
    Item js_math_floor(Item x);
    Item js_math_ceil(Item x);
    double js_math_ceil_d(double d);
    Item js_math_round_item(Item x);

    // String.raw tagged template literal
    Item js_string_raw(Item* args, int argc);

    // Constructor static property lookup (String.raw, etc.)
    Item js_constructor_static_property(Item ctor_name, Item prop_name);

    // Mark arrow functions as non-constructable
    void js_mark_arrow_func(Item fn_item);

    // Rounding functions (int versions return identity)
    int64_t fn_floor_i(int64_t x);
    int64_t fn_ceil_i(int64_t x);
    int64_t fn_round_i(int64_t x);

    // vector manipulation functions
    Item fn_reverse(Item a);
    Item fn_sort1(Item a);
    Item fn_sort2(Item a, Item dir);
    void fn_sort_by_keys(Item values, Item keys, int64_t descending);
    Item fn_unique(Item a);
    Item fn_take(Item a, Item n);
    Item fn_drop(Item a, Item n);
    Item fn_slice(Item a, Item start, Item end);
    Item fn_zip(Item a, Item b);
    Item fn_range3(Item start, Item end, Item step);
    Item fn_math_quantile(Item a, Item p);
    Item fn_reduce(Item collection, Item func);

    Range* fn_to(Item a, Item b);

    // pipe operations
    typedef Item (*PipeMapFn)(Item item, Item index);
    Item fn_pipe_map(Item collection, PipeMapFn transform);
    Item fn_pipe_where(Item collection, PipeMapFn predicate);
    Item fn_pipe_call(Item collection, Item func);

    String* fn_string(Item item);
    String *fn_strcat(String *left, String *right);
    Item fn_normalize(Item str, Item type);
    Item fn_normalize1(Item str);           // normalize with default NFC
    Item fn_substring(Item str, Item start, Item end);
    Bool fn_contains(Item str, Item substr);
    Item fn_join(Item a, Item b);
    // string functions
    Bool fn_starts_with(Item str, Item prefix);
    Bool fn_starts_with_str(String* str, String* prefix);   // native String* variant
    Bool fn_ends_with(Item str, Item suffix);
    Bool fn_ends_with_str(String* str, String* suffix);     // native String* variant
    int64_t fn_index_of(Item str, Item sub);
    int64_t fn_last_index_of(Item str, Item sub);
    Item fn_trim(Item str);
    Item fn_trim_start(Item str);
    Item fn_trim_end(Item str);
    Item fn_lower(Item str);
    Item fn_upper(Item str);
    Item fn_url_resolve(Item base, Item relative);
    Item fn_split(Item str, Item sep);
    Item fn_split3(Item str, Item sep, Item keep_delim);
    Item fn_split2(Item str, Item sep);  // overloaded alias for fn_split
    int64_t fn_ord(Item str);           // ord(str) - Unicode code point of first character
    int64_t fn_ord_str(String* str);    // native String* variant
    Item fn_chr(Item codepoint);        // chr(int) - 1-char string from Unicode code point
    Item fn_join2(Item list, Item sep);
    Item fn_replace(Item str, Item old_str, Item new_str);
    Item fn_replace3(Item str, Item old_str, Item new_str);  // overloaded alias for fn_replace
    Item fn_find2(Item source, Item pattern);
    Item fn_find3(Item source, Item pattern, Item options);

    Type* base_type(TypeId type_id);
    Type* const_type(int64_t type_index);
    Type* const_type_with_tl(int64_t type_index, void* type_list_ptr);
    TypePattern* const_pattern(int64_t pattern_index);  // retrieve compiled pattern by index
    TypePattern* const_pattern_with_tl(int64_t pattern_index, void* type_list_ptr);

    // returns the type of the item
    Type* fn_type(Item item);
    TypeId item_type_id(Item item);  // returns the TypeId of an item (for MIR use)

    // returns the name of an element, function, or type as a symbol
    Symbol* fn_name(Item item);

    RetItem fn_input1(Item url);
    RetItem fn_input2(Item url, Item options);
    RetItem fn_parse1(Item str);
    RetItem fn_parse2(Item str, Item options);
    String* fn_format1(Item item);
    String* fn_format2(Item item, Item options);
    Item fn_error(Item message);  // raise a user-defined error
    Symbol* fn_symbol1(Item item);  // convert to symbol
    Item fn_symbol2(Item name, Item url);  // create namespaced symbol

    // view/edit template apply
    Item fn_apply1(Item target);
    Item fn_apply2(Item target, Item options);

    Item fn_typeset_latex(Item input_file, Item output_file, Item options);

    // datetime constructors
    DateTime fn_datetime0();                       // datetime() - current datetime
    DateTime fn_datetime1(Item arg);               // datetime(str) - parse from string
    DateTime fn_date0();                           // date() - current date
    DateTime fn_date1(Item arg);                   // date(dt) - extract date portion
    DateTime fn_date3(Item y, Item m, Item d);     // date(y,m,d) - construct from components
    DateTime fn_time0();                           // time() - current time
    DateTime fn_time1(Item arg);                   // time(dt) - extract time portion
    DateTime fn_time3(Item h, Item m, Item s);     // time(h,m,s) - construct from components
    DateTime fn_justnow();                         // justnow() - current ms timestamp

    // variadic parameter access
    List* set_vargs(List* vargs);     // set current variadic args, returns previous
    void restore_vargs(List* prev);   // restore previous variadic args
    Item fn_varg0();                  // varg() - get all variadic args as list
    Item fn_varg1(Item index);        // varg(n) - get nth variadic arg

    // procedural functions
    Item pn_print(Item item);
    double pn_clock();        // clock() - high-resolution monotonic time in seconds
    RetItem pn_cmd1(Item cmd);
    RetItem pn_cmd2(Item cmd, Item args);
    RetItem pn_fetch(Item url, Item options);
    RetItem pn_output2(Item source, Item target);            // output(data, trg) - writes data to target, returns bytes written
    RetItem pn_output3(Item source, Item target, Item options);  // output(data, trg, options) - options: map {format, mode, atomic}, symbol/string (format), or null
    RetItem pn_output_append(Item source, Item target);      // used by |>> pipe operator (append mode)

    // io module functions (procedural)
    RetItem pn_io_copy(Item src, Item dst);
    RetItem pn_io_move(Item src, Item dst);
    RetItem pn_io_delete(Item path);
    RetItem pn_io_mkdir(Item path);
    RetItem pn_io_touch(Item path);
    RetItem pn_io_symlink(Item target, Item link);
    RetItem pn_io_chmod(Item path, Item mode);
    RetItem pn_io_rename(Item old_path, Item new_path);
    RetItem pn_io_fetch1(Item target);
    RetItem pn_io_fetch2(Item target, Item options);

    // bitwise functions (integer operations)
    int64_t fn_band(int64_t a, int64_t b);
    int64_t fn_bor(int64_t a, int64_t b);
    int64_t fn_bxor(int64_t a, int64_t b);
    int64_t fn_bnot(int64_t a);
    int64_t fn_shl(int64_t a, int64_t b);
    int64_t fn_shr(int64_t a, int64_t b);

    // compound assignment support (procedural only)
    void fn_array_set(Array* arr, int64_t index, Item value);
    void fn_map_set(Item map, Item key, Item value);

    // runtime type coercion for typed array annotations (int[], float[], etc.)
    // converts generic Array/List to typed array, or validates existing typed array
    // returns pointer to the coerced typed array, or NULL if elements are incompatible
    void* ensure_typed_array(Item item, TypeId element_type_id);
    void* ensure_sized_array(Item item, int64_t elem_type);

    // VMap system functions
    Item vmap_new();
    Item vmap_from_array(Item array_item);
    void vmap_set(Item vmap_item, Item key, Item value);

    // reactive UI: emit event to parent template handler
    Item pn_emit(Item event_name, Item event_data);
    // called from Radiant side — dispatches emitted event up the DOM ancestry
    Item dispatch_emit(Item event_name, Item event_data);

#ifdef __cplusplus
} // extern "C"
#endif

#endif
