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
#endif

// Name pool configuration
#define NAME_POOL_SYMBOL_LIMIT 32  // Max length for symbols in name_pool

enum EnumTypeId {
    LMD_TYPE_RAW_POINTER = 0,
    LMD_TYPE_NULL,

    // scalar types
    LMD_TYPE_BOOL,
    LMD_TYPE_INT,  // int literal, just 32-bit
    LMD_TYPE_INT64,  // int literal, 64-bit
    LMD_TYPE_FLOAT,  // float literal, 64-bit
    LMD_TYPE_DECIMAL,
    LMD_TYPE_NUMBER,  // explicit number, which includes decimal
    LMD_TYPE_DTIME,
    LMD_TYPE_SYMBOL,
    LMD_TYPE_STRING,
    LMD_TYPE_BINARY,

    // container types, LMD_TYPE_CONTAINER
    LMD_TYPE_LIST,
    LMD_TYPE_RANGE,
    LMD_TYPE_ARRAY_INT,
    LMD_TYPE_ARRAY_INT64,
    LMD_TYPE_ARRAY_FLOAT,
    LMD_TYPE_ARRAY,  // array of Items
    LMD_TYPE_MAP,
    LMD_TYPE_ELEMENT,
    LMD_TYPE_TYPE,
    LMD_TYPE_TYPE_UNARY,  // unary type with occurrence operator (?, +, *, [n], etc.)
    LMD_TYPE_TYPE_BINARY, // binary type for union (|), intersection (&), exclude (\) operations
    LMD_TYPE_FUNC,
    LMD_TYPE_PATTERN,  // compiled regex pattern for string matching

    LMD_TYPE_ANY,
    LMD_TYPE_ERROR,

    // JavaScript-specific types (added at end to preserve existing type IDs)
    LMD_TYPE_UNDEFINED,  // JavaScript undefined (distinct from null)

    // Path type for file/URL paths
    LMD_TYPE_PATH,  // segmented path with scheme (file, http, https, sys, etc.)

    LMD_CONTAINER_HEAP_START, // special value for container heap entry start
};
typedef uint8_t TypeId;

// Get human-readable name for a TypeId (for error messages)
static inline const char* get_type_name(TypeId type_id) {
    switch (type_id) {
        case LMD_TYPE_RAW_POINTER: return "raw_pointer";
        case LMD_TYPE_NULL: return "null";
        case LMD_TYPE_BOOL: return "bool";
        case LMD_TYPE_INT: return "int";
        case LMD_TYPE_INT64: return "int64";
        case LMD_TYPE_FLOAT: return "float";
        case LMD_TYPE_DECIMAL: return "decimal";
        case LMD_TYPE_NUMBER: return "number";
        case LMD_TYPE_DTIME: return "datetime";
        case LMD_TYPE_SYMBOL: return "symbol";
        case LMD_TYPE_STRING: return "string";
        case LMD_TYPE_BINARY: return "binary";
        case LMD_TYPE_LIST: return "list";
        case LMD_TYPE_RANGE: return "range";
        case LMD_TYPE_ARRAY_INT: return "array[int]";
        case LMD_TYPE_ARRAY_INT64: return "array[int64]";
        case LMD_TYPE_ARRAY_FLOAT: return "array[float]";
        case LMD_TYPE_ARRAY: return "array";
        case LMD_TYPE_MAP: return "map";
        case LMD_TYPE_ELEMENT: return "element";
        case LMD_TYPE_TYPE: return "type";
        case LMD_TYPE_TYPE_UNARY: return "type_unary";
        case LMD_TYPE_TYPE_BINARY: return "type_binary";
        case LMD_TYPE_FUNC: return "function";
        case LMD_TYPE_PATTERN: return "pattern";
        case LMD_TYPE_ANY: return "any";
        case LMD_TYPE_ERROR: return "error";
        case LMD_TYPE_UNDEFINED: return "undefined";
        case LMD_TYPE_PATH: return "path";
        default: return "unknown";
    }
}

// 3-state boolean:
typedef enum {
    BOOL_FALSE = 0,
    BOOL_TRUE = 1,
    BOOL_ERROR = 2
} BoolEnum;
typedef uint8_t Bool;

#define  LMD_TYPE_CONTAINER LMD_TYPE_LIST

typedef struct Type {
    TypeId type_id;
    uint8_t is_literal:1;  // is a literal value
    uint8_t is_const:1;  // is a constant expr
} Type;

typedef struct Container Container;
typedef struct Range Range;
typedef struct List List;
typedef struct List Array;
typedef struct ArrayInt ArrayInt;
typedef struct ArrayInt64 ArrayInt64;
typedef struct ArrayFloat ArrayFloat;
typedef struct Map Map;
typedef struct Element Element;
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
    uint32_t len:22;  // string len , up to 4MB;
    uint32_t ref_cnt:10;  // ref_cnt, up to 1024 refs
    char chars[];
} String;
#define STRING_STRUCT_DEFINED
#endif

typedef struct Target Target;  // forward declaration for Symbol.ns

typedef struct Symbol {
    uint32_t len:22;    // symbol name length, up to 4MB
    uint32_t ref_cnt:10;  // ref_cnt, up to 1024 refs
    Target* ns;         // namespace target (NULL for unqualified symbols)
    char chars[];       // symbol name characters
} Symbol;
typedef String Binary;  // Binary is just a String

// Array and List struct defintions needed for for-loop
struct Container {
    TypeId type_id;
    union {
        uint8_t flags;
        struct {
            uint8_t is_content:1;    // whether it is a content list, or value list
            uint8_t is_spreadable:1; // whether this array should be spread when added to collections
            uint8_t is_heap:1;       // whether allocated from runtime heap (vs arena for input docs)
            uint8_t reserved:5;
        };
    };
    uint16_t ref_cnt;  // reference count
};

#ifndef __cplusplus
    struct Range {
        TypeId type_id;
        uint8_t flags;
        uint16_t ref_cnt;  // reference count
        //---------------------
        int64_t start;  // inclusive start
        int64_t end;    // inclusive end
        int64_t length;
    };

    struct List {
        TypeId type_id;
        uint8_t flags;
        uint16_t ref_cnt;  // reference count
        //---------------------
        Item* items;  // pointer to items
        int64_t length;  // number of items
        int64_t extra;   // count of extra items stored at the end of the list
        int64_t capacity;  // allocated capacity
    };

    struct ArrayInt {
        TypeId type_id;
        uint8_t flags;
        uint16_t ref_cnt;  // reference count
        //---------------------
        int64_t* items;  // pointer to int56 values (stored as int64)
        int64_t length;  // number of items
        int64_t extra;   // count of extra items stored at the end of the array
        int64_t capacity;  // allocated capacity
    };

    struct ArrayInt64 {
        TypeId type_id;
        uint8_t flags;
        uint16_t ref_cnt;  // reference count
        //---------------------
        int64_t* items;  // pointer to 64-bit integer items
        int64_t length;  // number of items
        int64_t extra;   // count of extra items stored at the end of the array
        int64_t capacity;  // allocated capacity
    };

    struct ArrayFloat {
        TypeId type_id;
        uint8_t flags;
        uint16_t ref_cnt;  // reference count
        //---------------------
        double* items;  // pointer to items
        int64_t length;  // number of items
        int64_t extra;   // count of extra items stored at the end of the array
        int64_t capacity;  // allocated capacity
    };

    // ArrayList definition for MIR runtime (item_keys return type)
    typedef void *ArrayListValue;
    struct _ArrayList {
        ArrayListValue *data;
        int length;
        int _alloced;
    };

#endif

Range* range();
long range_get(Range *range, int index);

List* list();  // constructs an empty list
Item list_fill(List *list, int cnt, ...);  // fill the list with the items
void list_push(List *list, Item item);
void list_push_spread(List *list, Item item);  // push item, spreading if spreadable array
Item list_end(List *list);

// Spreadable array functions for for-expression results
Array* array_spreadable();  // constructs a spreadable empty array
void array_push(Array* arr, Item item);  // push item to array
void array_push_spread(Array* arr, Item item);  // push item, spreading if spreadable array
Item array_end(Array* arr);  // finalize and return array as Item
void frame_end();  // cleanup memory frame after for-expression

typedef void* (*fn_ptr)();

// Function as first-class value
// Supports both direct function references and closures (future)
struct Function {
    uint8_t type_id;
    uint8_t arity;        // number of parameters (0-255)
    uint16_t ref_cnt;     // reference count for memory management
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

// Path metadata structure (optional, allocated on demand)
// Stores file/directory metadata without loading content
struct PathMeta {
    int64_t size;           // file size in bytes (-1 for dirs or unknown)
    DateTime modified;      // last modification time
    uint8_t flags;          // is_dir (bit 0), is_link (bit 1)
    uint8_t mode;           // Unix permissions (compressed to 8 bits: rwx for owner)
};

// Path metadata flags
#define PATH_META_IS_DIR   0x01
#define PATH_META_IS_LINK  0x02

// Path segment type flags (stored in flags field)
// These distinguish between regular segments, wildcards, and dynamic segments
// Note: Named with LPATH_ prefix to avoid conflict with validator's PathSegmentType
typedef enum {
    LPATH_SEG_NORMAL = 0,        // regular segment (literal string)
    LPATH_SEG_WILDCARD = 1,      // single wildcard (*) - match one segment
    LPATH_SEG_WILDCARD_REC = 2,  // recursive wildcard (**) - match zero or more segments
    LPATH_SEG_DYNAMIC = 3,       // dynamic segment (runtime-computed, name is NULL until resolved)
} LPathSegmentType;

// Path flags (bits 0-1 for segment type, bit 7 for metadata loaded)
#define PATH_FLAG_META_LOADED  0x80  // bit 7: metadata has been stat'd and loaded

struct Path {
    TypeId type_id;         // LMD_TYPE_PATH
    uint8_t flags;          // segment type (bits 0-1), metadata loaded (bit 7)
    uint16_t ref_cnt;       // reference count
    const char* name;       // segment name (interned via name_pool), NULL for wildcards
    Path* parent;           // parent segment (NULL for root schemes)
    uint64_t result;        // cached resolved content (0 = not resolved yet)
    PathMeta* meta;         // optional metadata (NULL until stat'd)
};

// Helper macros for path segment type
#define PATH_GET_SEG_TYPE(p)      ((LPathSegmentType)((p)->flags & 0x03))
#define PATH_SET_SEG_TYPE(p, t)   ((p)->flags = ((p)->flags & 0xFC) | ((t) & 0x03))

// Path scheme identifiers (predefined roots)
typedef enum {
    PATH_SCHEME_FILE = 0,   // file://
    PATH_SCHEME_HTTP,       // http://
    PATH_SCHEME_HTTPS,      // https://
    PATH_SCHEME_SYS,        // sys:// (system info)
    PATH_SCHEME_REL,        // . (relative path)
    PATH_SCHEME_PARENT,     // .. (parent directory)
    PATH_SCHEME_COUNT
} PathScheme;

// Path API (defined in path.c)
void path_init(void);                                   // Initialize root scheme paths
Path* path_get_root(PathScheme scheme);                 // Get predefined root path
Path* path_append(Path* parent, const char* segment);   // Append segment to path
Path* path_append_len(Path* parent, const char* segment, size_t len);
const char* path_get_scheme_name(Path* path);           // Get scheme name (file, http, etc.)
PathScheme path_get_scheme(Path* path);                 // Get scheme type (PATH_SCHEME_FILE, etc.)
bool path_is_root(Path* path);                          // Check if path is a root scheme
bool path_is_absolute(Path* path);                      // Check if path is absolute (not . or ..)
int path_depth(Path* path);                             // Get path depth (segment count)
void path_to_string(Path* path, void* out);             // Convert to string (StrBuf*)
void path_to_os_path(Path* path, void* out);            // Convert to OS path (StrBuf*)

// New Path API: path_new, path_extend, path_concat
Path* path_new(Pool* pool, int scheme);                           // Create new path with scheme
Path* path_extend(Pool* pool, Path* base, const char* segment);   // Extend path with segment
Path* path_concat(Pool* pool, Path* base, Path* suffix);          // Concatenate two paths

// Wildcard support for glob patterns
Path* path_wildcard(Pool* pool, Path* base);                      // Add * wildcard segment
Path* path_wildcard_recursive(Pool* pool, Path* base);            // Add ** wildcard segment
bool path_is_wildcard(Path* path);                                // Check if segment is *
bool path_is_wildcard_recursive(Path* path);                      // Check if segment is **
bool path_has_wildcards(Path* path);                              // Check if path has any wildcards

// Path content loading (lazy evaluation support)
Item path_load_content(Path* path);                               // Load path content (file/URL)
int64_t path_get_length(Path* path);                              // Get path content length (triggers load)
Item path_get_item(Path* path, int64_t index);                    // Get item at index (triggers load)

// ============================================================================
// Target: Unified I/O target for input/output operations
// ============================================================================
// Supports both URL strings and Lambda Path objects
// - URLs: file://, http://, https://, sys:// etc. (parsed from string/symbol)
// - Paths: Lambda's cross-platform path type

// Forward declaration for Url (defined in lib/url.h)
typedef struct Url Url;

// Target scheme (derived from URL or Path)
typedef enum {
    TARGET_SCHEME_FILE = 0,     // local file (file:// or relative path)
    TARGET_SCHEME_HTTP,         // HTTP URL
    TARGET_SCHEME_HTTPS,        // HTTPS URL
    TARGET_SCHEME_SYS,          // system info (sys://)
    TARGET_SCHEME_FTP,          // FTP (future)
    TARGET_SCHEME_DATA,         // data: URL (future)
    TARGET_SCHEME_UNKNOWN       // unrecognized scheme
} TargetScheme;

// Target type (how the target was specified)
typedef enum {
    TARGET_TYPE_URL = 0,        // parsed from URL string
    TARGET_TYPE_PATH            // Lambda Path object
} TargetType;

// Target structure - unified I/O target
typedef struct Target {
    TargetScheme scheme;        // scheme type (file, http, https, sys, etc.)
    TargetType type;            // source type (url or path)
    uint64_t url_hash;          // hash of normalized URL for fast equality comparison
    const char* original;       // original input string (for relative path preservation)
    union {
        Url* url;               // parsed URL (when type == TARGET_TYPE_URL)
        Path* path;             // Lambda Path (when type == TARGET_TYPE_PATH)
    };
} Target;

// Target API (defined in target.c)
// item_to_target: Convert Item to Target
// - string/symbol: parse as URL
// - path: use directly
// - other types: return error
// Note: Takes uint64_t to avoid C/C++ Item type mismatch
Target* item_to_target(uint64_t item, Url* cwd);

// target_to_local_path: Convert Target to local OS file path
// - Resolves relative paths against cwd
// - Returns NULL for non-file schemes (http, https, etc.)
// - Caller must free the returned StrBuf
void* target_to_local_path(Target* target, Url* cwd);

// target_to_url_string: Get URL string representation
// - For URL targets: returns href
// - For Path targets: converts to URL string (file:// for local paths)
const char* target_to_url_string(Target* target, void* out_buf);

// target_is_local: Check if target is a local file (not http/https)
bool target_is_local(Target* target);

// target_is_remote: Check if target is a remote URL (http/https)
bool target_is_remote(Target* target);

// target_is_dir: Check if target is a directory (local targets only)
// Returns false for remote URLs or if stat fails
bool target_is_dir(Target* target);

// target_exists: Check if target exists (file or directory, local targets only)
// Returns false for remote URLs (would need HTTP HEAD request)
bool target_exists(Target* target);

// target_free: Free target and its contents
void target_free(Target* target);

// target_equal: Check if two targets refer to the same resource (by URL hash)
bool target_equal(Target* a, Target* b);

// Name - a qualified name with optional namespace
// Pool-managed (no refcount). Used for element tag names and map field names.
typedef struct Name {
    String* name;       // local name (interned via name_pool)
    Target* ns;         // namespace target (NULL for unqualified names)
} Name;

// name_equal: Check if two names are equal (by local name pointer and namespace hash)
static inline bool name_equal(const Name* a, const Name* b) {
    if (a == b) return true;
    if (!a || !b) return false;
    if (a->name != b->name) return false;  // interned strings: pointer equality
    if (a->ns == b->ns) return true;       // same namespace pointer (or both NULL)
    if (!a->ns || !b->ns) return false;    // one has ns, one doesn't
    return target_equal(a->ns, b->ns);
}

// Path resolution for iteration (returns list for dirs, content for files)
Item path_resolve_for_iteration(Path* path);                      // Resolve path for iteration
bool path_ends_with_wildcard(Path* path);                         // Check if leaf is * or **
void path_load_metadata(Path* path);                              // Load metadata via stat()

// System function: exists() - check if file/directory exists
Bool fn_exists(Item path);                                        // Check if path exists

// Create function wrappers for first-class usage
Function* to_fn(fn_ptr ptr);
Function* to_fn_n(fn_ptr ptr, int arity);
Function* to_fn_named(fn_ptr ptr, int arity, const char* name);
Function* to_closure(fn_ptr ptr, int arity, void* env);
Function* to_closure_named(fn_ptr ptr, int arity, void* env, const char* name);

// Memory allocation for closure environments
void* heap_calloc(size_t size, TypeId type_id);
// String creation for name pooling
String* heap_create_name(const char* name);
// String creation for runtime strings
String* heap_strcpy(char* src, int len);

#define INT64_ERROR           INT64_MAX
#define LAMBDA_INT64_MAX    (INT64_MAX - 1)

#define ITEM_UNDEFINED      0
#define ITEM_NULL           ((uint64_t)LMD_TYPE_NULL << 56)
#define ITEM_NULL_SPREADABLE ((uint64_t)LMD_TYPE_NULL << 56 | 1)  // spreadable null (skip when spreading)
#define ITEM_JS_UNDEFINED   ((uint64_t)LMD_TYPE_UNDEFINED << 56)  // JavaScript undefined
#define ITEM_INT            ((uint64_t)LMD_TYPE_INT << 56)
#define ITEM_ERROR          ((uint64_t)LMD_TYPE_ERROR << 56)
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
#define l2it(long_ptr)       ((long_ptr)? ((((uint64_t)LMD_TYPE_INT64)<<56) | (uint64_t)(long_ptr)): null)
#define d2it(double_ptr)     ((double_ptr)? ((((uint64_t)LMD_TYPE_FLOAT)<<56) | (uint64_t)(double_ptr)): null)
#define c2it(decimal_ptr)    ((decimal_ptr)? ((((uint64_t)LMD_TYPE_DECIMAL)<<56) | (uint64_t)(decimal_ptr)): null)
#define s2it(str_ptr)        ((str_ptr)? ((((uint64_t)LMD_TYPE_STRING)<<56) | (uint64_t)(str_ptr)): null)
#define y2it(sym_ptr)        ((sym_ptr)? ((((uint64_t)LMD_TYPE_SYMBOL)<<56) | (uint64_t)(sym_ptr)): null)
#define x2it(bin_ptr)        ((bin_ptr)? ((((uint64_t)LMD_TYPE_BINARY)<<56) | (uint64_t)(bin_ptr)): null)
#define k2it(dtime_ptr)      ((dtime_ptr)? ((((uint64_t)LMD_TYPE_DTIME)<<56) | (uint64_t)(dtime_ptr)): null)

Array* array_fill(Array* arr, int count, ...);
ArrayInt* array_int_fill(ArrayInt* arr, int count, ...);
ArrayInt64* array_int64_fill(ArrayInt64* arr, int count, ...);
ArrayFloat* array_float_fill(ArrayFloat* arr, int count, ...);

typedef struct Map Map;
Map* map_fill(Map* map, ...);

typedef struct Element Element;
Element* elmt_fill(Element *elmt, ...);

typedef struct Url Url;
typedef struct Pool Pool;

// Forward declaration of ArrayList (defined in lib/arraylist.h)
typedef struct _ArrayList ArrayList;

typedef struct Context {
    Pool* pool;
    void** consts;
    void* type_list;  // type definitions list (ArrayList* at runtime, void* for JIT access)
    Url* cwd;  // current working directory
    void* (*context_alloc)(int size, TypeId type_id);
    bool run_main; // whether to run main procedure on start
    bool disable_string_merging; // disable automatic string merging in list_push
    uintptr_t stack_limit; // stack overflow check limit (from lambda_stack_init)
} Context;

#ifndef LAMBDA_STATIC
    Array* array();
    ArrayInt* array_int();
    ArrayInt64* array_int64();
    ArrayFloat* array_float();

    ArrayInt* array_int_new(int length);
    ArrayInt64* array_int64_new(int length);
    ArrayFloat* array_float_new(int length);

    Map* map(int type_index);
    Element* elmt(int type_index);

    // these getters use runtime num_stack
    Item array_get(Array *array, int index);
    Item array_int_get(ArrayInt *array, int index);
    Item array_int64_get(ArrayInt64* array, int index);
    Item array_float_get(ArrayFloat* array, int index);
    Item list_get(List *list, int index);
    Item map_get(Map* map, Item key);
    Item elmt_get(Element *elmt, Item key);
    Item item_at(Item data, int index);
    Item item_attr(Item data, const char* key);  // get attribute by name
    struct _ArrayList* item_keys(Item data);     // get list of attribute names

    Bool is_truthy(Item item);
    Item v2it(List *list);

    Item push_d(double dval);
    Item push_l(int64_t lval);
    Item push_k(DateTime dtval);
    Item push_c(int64_t cval);

    #define const_d2it(index)    d2it(rt->consts[index])
    #define const_l2it(index)    l2it(rt->consts[index])
    #define const_c2it(index)    c2it(rt->consts[index])
    #define const_s2it(index)    s2it(rt->consts[index])
    #define const_y2it(index)    y2it(rt->consts[index])
    #define const_k2it(index)    k2it(rt->consts[index])
    #define const_x2it(index)    x2it(rt->consts[index])

    #define const_s(index)      ((String*)rt->consts[index])
    #define const_c(index)      ((Decimal*)rt->consts[index])
    #define const_k(index)      (*(DateTime*)rt->consts[index])

    // item unboxing
    int64_t it2l(Item item);
    double it2d(Item item);
    bool it2b(Item item);
    int it2i(Item item);
    String* it2s(Item item);
    const char* fn_to_cstr(Item item);  // convert Item to C string (for path segment names)

    // generic field access function
    Item fn_index(Item item, Item index);
    Item fn_member(Item item, Item key);
    // length function
    int64_t fn_len(Item item);
    Item fn_int(Item a);
    int64_t fn_int64(Item a);

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
    Bool fn_lt(Item a, Item b);
    Bool fn_gt(Item a, Item b);
    Bool fn_le(Item a, Item b);
    Bool fn_ge(Item a, Item b);
    Bool fn_not(Item a);
    Bool fn_is(Item a, Item b);
    Bool fn_in(Item a, Item b);

    // vector arithmetic operations (element-wise)
    Item vec_add(Item a, Item b);
    Item vec_sub(Item a, Item b);
    Item vec_mul(Item a, Item b);
    Item vec_div(Item a, Item b);
    Item vec_mod(Item a, Item b);
    Item vec_pow(Item a, Item b);

    // vector system functions
    Item fn_prod(Item a);
    Item fn_cumsum(Item a);
    Item fn_cumprod(Item a);
    Item fn_argmin(Item a);
    Item fn_argmax(Item a);
    Item fn_fill(Item n, Item value);
    Item fn_dot(Item a, Item b);
    Item fn_norm(Item a);
    // statistical functions
    Item fn_mean(Item a);
    Item fn_median(Item a);
    Item fn_variance(Item a);
    Item fn_deviation(Item a);
    // element-wise math functions
    Item fn_sqrt(Item a);
    Item fn_log(Item a);
    Item fn_log10(Item a);
    Item fn_exp(Item a);
    Item fn_sin(Item a);
    Item fn_cos(Item a);
    Item fn_tan(Item a);
    Item fn_sign(Item a);
    // vector manipulation functions
    Item fn_reverse(Item a);
    Item fn_sort1(Item a);
    Item fn_sort2(Item a, Item dir);
    Item fn_unique(Item a);
    Item fn_concat(Item a, Item b);
    Item fn_take(Item a, Item n);
    Item fn_drop(Item a, Item n);
    Item fn_slice(Item a, Item start, Item end);
    Item fn_zip(Item a, Item b);
    Item fn_range3(Item start, Item end, Item step);
    Item fn_quantile(Item a, Item p);

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
    Bool fn_ends_with(Item str, Item suffix);
    int64_t fn_index_of(Item str, Item sub);
    int64_t fn_last_index_of(Item str, Item sub);
    Item fn_trim(Item str);
    Item fn_trim_start(Item str);
    Item fn_trim_end(Item str);
    Item fn_lower(Item str);
    Item fn_upper(Item str);
    Item fn_split(Item str, Item sep);
    Item fn_str_join(Item list, Item sep);
    Item fn_replace(Item str, Item old_str, Item new_str);

    Function* to_fn(fn_ptr ptr);
    Function* to_fn_n(fn_ptr ptr, int arity);  // create function with arity info
    Type* base_type(TypeId type_id);
    Type* const_type(int type_index);
    TypePattern* const_pattern(int pattern_index);  // retrieve compiled pattern by index

    // returns the type of the item
    Type* fn_type(Item item);
    TypeId item_type_id(Item item);  // returns the TypeId of an item (for MIR use)

    Item fn_input1(Item url);
    Item fn_input2(Item url, Item options);
    String* fn_format1(Item item);
    String* fn_format2(Item item, Item options);
    Item fn_error(Item message);  // raise a user-defined error
    Symbol* fn_symbol1(Item item);  // convert to symbol
    Item fn_symbol2(Item name, Item url);  // create namespaced symbol

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
    void set_vargs(List* vargs);  // set current variadic args
    Item fn_varg0();              // varg() - get all variadic args as list
    Item fn_varg1(Item index);    // varg(n) - get nth variadic arg

    // procedural functions
    Item pn_print(Item item);
    Item pn_cmd1(Item cmd);
    Item pn_cmd2(Item cmd, Item args);
    Item pn_fetch(Item url, Item options);
    Item pn_output2(Item source, Item target);            // output(data, trg) - writes data to target, returns bytes written
    Item pn_output3(Item source, Item target, Item options);  // output(data, trg, options) - options: map {format, mode, atomic}, symbol/string (format), or null
    Item pn_output_append(Item source, Item target);      // used by |>> pipe operator (append mode)

    // io module functions (procedural)
    Item pn_io_copy(Item src, Item dst);
    Item pn_io_move(Item src, Item dst);
    Item pn_io_delete(Item path);
    Item pn_io_mkdir(Item path);
    Item pn_io_touch(Item path);
    Item pn_io_symlink(Item target, Item link);
    Item pn_io_chmod(Item path, Item mode);
    Item pn_io_rename(Item old_path, Item new_path);
    Item pn_io_fetch1(Item target);
    Item pn_io_fetch2(Item target, Item options);

#endif
