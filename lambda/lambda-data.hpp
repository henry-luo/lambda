#pragma once

#include <string.h>  // moved outside extern "C" block to fix C++ compatibility

#ifdef __cplusplus
extern "C" {
#endif

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <cstdint>  // C++
#include <inttypes.h>  // for cross-platform integer formatting
#include <math.h>

// Forward declaration for mpdecimal types (full definition in lambda-decimal.cpp)
typedef struct mpd_context_t mpd_context_t;
typedef struct mpd_t mpd_t;

#include "../lib/strbuf.h"
#include "../lib/stringbuf.h"
#include "../lib/hashmap.h"
#include "../lib/mempool.h"
#include "../lib/arena.h"
#include "../lib/arraylist.h"
#include "../lib/strview.h"
#include "../lib/gc/gc_nursery.h"
#include "../lib/datetime.h"
#include "../lib/url.h"

#ifndef max
#define max(a, b) ((a) > (b) ? (a) : (b))
#endif
#ifndef min
#define min(a, b) ((a) < (b) ? (a) : (b))
#endif

// Forward declarations for C++ types
class SchemaValidator;

#include "lambda.hpp"
#undef max
#undef min

#include "name_pool.hpp"
#include "shape_pool.hpp"

// void *memcpy(void *dest, const void *src, size_t n);
// void *memset(void *s, int c, size_t n);
// int memcmp(const void *s1, const void *s2, size_t n);
// size_t strlen(const char *s);
// int strcmp(const char *s1, const char *s2);
// int strncmp(const char *s1, const char *s2, size_t n);
// int strcasecmp(const char *s1, const char *s2);
// int strncasecmp(const char *s1, const char *s2, size_t n);
// char *strchr(const char *s, int c);
// char *strcpy(char *dest, const char *src);
// char *strncpy(char *dest, const char *src, size_t n);
// char *strdup(const char *s);
// char *strstr(const char *target, const char *source);
// char *strrchr(const char *s, int c);
// char *strtok(char *str, const char *delim);

#ifdef __cplusplus
}
#endif


typedef struct Heap Heap;
typedef struct Pack Pack;
typedef struct mpd_context_t mpd_context_t;
typedef struct gc_nursery gc_nursery_t;
struct LambdaError;  // forward declaration

typedef struct EvalContext : Context {
    Heap* heap;
    Pool* ast_pool;
    NamePool* name_pool;        // name_pool for runtime-generated names
    gc_nursery_t* nursery;  // bump-pointer allocator for numeric temporaries (int64, double, DateTime)
    void* type_info;  // meta info for the base types
    Item result; // final exec result
    mpd_context_t* decimal_ctx; // libmpdec context for decimal operations
    SchemaValidator* validator; // Schema validator for document validation

    // Error handling and stack trace support
    ArrayList* debug_info;      // function address → source mapping for stack traces
    const char* current_file;   // current source file (for error reporting)
    LambdaError* last_error;    // most recent runtime error (owned)
} EvalContext;

// Unicode-enhanced comparison functions are declared in utf_string.h
#include "utf_string.h"

typedef struct TypeInfo {
    int byte_size;  // byte size of the type
    char* name;  // name of the type
    Type* type;  // literal type
    Type* lit_type;  // literal type_type
    // char* c_type;  // C type of the type
} TypeInfo;

extern TypeInfo type_info[];

// const_index, type_index - 32-bit, there should not be more than 4G types and consts in a single Lambda runtime
// list item count, map size - 64-bit, to support large data files

typedef struct mpd_t mpd_t;
struct Decimal {
    uint8_t unlimited;   // whether it is an unlimited decimal
    mpd_t* dec_val;  // libmpdec decimal number
};

#pragma pack(push, 1)
// TypedItem for storing data in map with type_id
typedef struct TypedItem {
    TypeId type_id;
    union {
        // inline value types
        bool bool_val;
        int int_val;
        int64_t long_val;
        // float float_val;
        double double_val;
        DateTime datetime_val;
        uint64_t item;

        // pointer types
        void* pointer;
        Decimal* decimal;
        String* string;
        Symbol* symbol;

        // containers
        Container* container;
        Range* range;
        Array* array;
        List* list;
        Map* map;
        Element* element;
        Object* object;
        Type* type;
        Function* function;
        Path* path;
    };
} TypedItem;
#pragma pack(pop)

typedef struct Script Script;

typedef struct TypeConst : Type {
    int const_index;
} TypeConst;

typedef struct TypeFloat : TypeConst {
    double double_val;
} TypeFloat;

typedef struct TypeInt64 : TypeConst {
    int64_t int64_val;
} TypeInt64;

typedef struct TypeNumSized : TypeConst {
    NumSizedType num_type;  // which sized numeric sub-type
    uint32_t raw_bits;      // raw 32-bit value (bit pattern)
} TypeNumSized;

typedef struct TypeUint64 : TypeConst {
    uint64_t uint64_val;
} TypeUint64;

typedef struct TypeDateTime : TypeConst {
    DateTime datetime;
} TypeDateTime;

typedef struct TypeDecimal : TypeConst {
    Decimal* decimal;
} TypeDecimal;

typedef struct TypeString : TypeConst {
    String* string;
} TypeString;

typedef TypeString TypeSymbol;

typedef struct TypeArray : Type {
    Type* nested;  // nested item type for the array
    int64_t length;  // no. of items in the array/map
    int type_index;  // index of the type in the type list
} TypeArray;

typedef TypeArray TypeList;

typedef struct ShapeEntry {
    StrView* name;
    Type* type;  // type of the field
    int64_t byte_offset;  // byte offset of the map field
    struct ShapeEntry* next;
    Target* ns;  // namespace target (NULL for unqualified fields)
    struct AstNode* default_value;  // default value expression (NULL if none)
} ShapeEntry;

// A1: Property hash table — inline open-addressing table for O(1) property lookup.
// For objects with ≤32 properties (covers >99% of JS objects), uses a small fixed
// table indexed by FNV-1a hash. Each slot stores a ShapeEntry pointer.
#define TYPEMAP_HASH_CAPACITY 32

typedef struct TypeMap : Type {
    int64_t length;  // no. of items in the map
    int64_t byte_size;  // byte size of the struct that the map is transpiled to
    int type_index;  // index of the type in the type list
    bool has_named_shape;  // shape was merged from a named type annotation (safe for direct stores)
    ShapeEntry* shape;  // first shape entry of the map
    ShapeEntry* last;  // last shape entry of the map
    const char* struct_name;  // C struct name for direct access (NULL if anonymous)
    // A1: Inline property hash table for O(1) lookup
    ShapeEntry* field_index[TYPEMAP_HASH_CAPACITY];  // hash table slots (NULL = empty)
    uint8_t field_count;  // number of fields in hash table (0 = not populated)
    // P1: Slot-indexed array for O(1) shaped property access (used by js_get_slot_fast/js_set_slot_fast).
    // Populated for constructor-shaped objects. slot_entries[i] points to the i-th ShapeEntry.
    ShapeEntry** slot_entries;  // NULL if not populated; else array of slot_count pointers
    int slot_count;             // number of slot_entries (0 = not populated)
} TypeMap;

// A1: FNV-1a hash for property name lookup
static inline uint32_t typemap_fnv1a(const char* key, int len) {
    uint32_t h = 2166136261u;
    for (int i = 0; i < len; i++) {
        h ^= (uint8_t)key[i];
        h *= 16777619u;
    }
    return h;
}

// A1: Insert a ShapeEntry into the TypeMap hash table (open addressing, linear probe).
// Uses last-writer-wins: if a name already exists, the slot is overwritten.
static inline void typemap_hash_insert(TypeMap* tm, ShapeEntry* entry) {
    if (!entry || !entry->name || tm->field_count >= TYPEMAP_HASH_CAPACITY) return;
    uint32_t h = typemap_fnv1a(entry->name->str, (int)entry->name->length);
    uint32_t idx = h & (TYPEMAP_HASH_CAPACITY - 1);
    for (int probe = 0; probe < TYPEMAP_HASH_CAPACITY; probe++) {
        uint32_t slot = (idx + probe) & (TYPEMAP_HASH_CAPACITY - 1);
        if (!tm->field_index[slot]) {
            tm->field_index[slot] = entry;
            tm->field_count++;
            return;
        }
        // last-writer-wins: replace existing entry with same name
        if (tm->field_index[slot]->name &&
            tm->field_index[slot]->name->length == entry->name->length &&
            memcmp(tm->field_index[slot]->name->str, entry->name->str, entry->name->length) == 0) {
            tm->field_index[slot] = entry;
            return;
        }
    }
    // table full — field_count stays below TYPEMAP_HASH_CAPACITY
}

// A1: Lookup a ShapeEntry by name through the hash table.
// Returns the ShapeEntry or NULL if not found.
// A6: Uses pointer comparison first (interned strings via name pool share
// the same char* pointer), falling back to memcmp only on pointer mismatch.
static inline ShapeEntry* typemap_hash_lookup(TypeMap* tm, const char* key, int key_len) {
    if (tm->field_count == 0) return NULL;
    uint32_t h = typemap_fnv1a(key, key_len);
    uint32_t idx = h & (TYPEMAP_HASH_CAPACITY - 1);
    for (int probe = 0; probe < TYPEMAP_HASH_CAPACITY; probe++) {
        uint32_t slot = (idx + probe) & (TYPEMAP_HASH_CAPACITY - 1);
        ShapeEntry* e = tm->field_index[slot];
        if (!e) return NULL;  // empty slot → not found
        if (e->name && e->name->str && (e->name->str == key ||  // A6: interned pointer match (fast)
            (e->name->length == (size_t)key_len &&
             memcmp(e->name->str, key, key_len) == 0))) {
            return e;
        }
    }
    return NULL;
}

typedef struct TypeElmt : TypeMap {
    StrView name;  // local name of the element
    int64_t content_length;  // no. of content items, needed for element type
    Target* ns;  // namespace target (NULL for unqualified elements)
} TypeElmt;

// TypeMethod: entry in the method table of a TypeObject
typedef struct TypeMethod {
    StrView* name;              // method name (interned)
    Function* fn;               // compiled function pointer
    bool is_proc;               // true for pn, false for fn
    struct TypeMethod* next;    // linked list
} TypeMethod;

// Forward declaration for constraint function pointer (full typedef below near TypeConstrained)
typedef uint8_t (*ConstraintFn)(uint64_t value);

// TypeObject: nominally-typed map with methods
// Extends TypeMap — inherits shape (fields), length, byte_size, type_index
typedef struct TypeObject : TypeMap {
    StrView type_name;          // nominal type name ("Point", "Circle")
    struct TypeObject* base;    // parent type for inheritance (NULL if no base)
    TypeMethod* methods;        // linked list of methods (head)
    TypeMethod* methods_last;   // linked list of methods (tail)
    int method_count;           // number of methods
    struct AstNode* constraint; // object-level that(...) constraint AST (NULL if none)
    ConstraintFn constraint_fn; // JIT-compiled constraint checker (NULL if none)
} TypeObject;

typedef enum Operator {
    // unary
    OPERATOR_NOT,
    OPERATOR_NEG,
    OPERATOR_POS,
    OPERATOR_SPREAD, // * spread operator
    OPERATOR_IS_ERROR, // ^expr - error type check shorthand

    // binary
    OPERATOR_ADD,
    OPERATOR_JOIN,
    OPERATOR_SUB,
    OPERATOR_MUL,
    OPERATOR_POW,
    OPERATOR_DIV,
    OPERATOR_IDIV,
    OPERATOR_MOD,

    OPERATOR_AND,
    OPERATOR_OR,

    OPERATOR_EQ,
    OPERATOR_NE,
    OPERATOR_LT,
    OPERATOR_LE,
    OPERATOR_GT,
    OPERATOR_GE,

    OPERATOR_TO,
    OPERATOR_UNION,
    OPERATOR_INTERSECT,
    OPERATOR_EXCLUDE,
    OPERATOR_IS,
    OPERATOR_IS_NAN,  // expr is nan — IEEE NaN check
    OPERATOR_IN,

    // pipe operators
    OPERATOR_PIPE,      // | pipe operator
    OPERATOR_WHERE,     // where filter clause
    OPERATOR_PIPE_FILE,     // |> pipe to file (write)
    OPERATOR_PIPE_APPEND,   // |>> pipe to file (append)

    // occurrence
    OPERATOR_OPTIONAL,  // ?
    OPERATOR_ONE_MORE,  // +
    OPERATOR_ZERO_MORE,  // *
    OPERATOR_REPEAT,    // {n}, {n,}, {n,m} for patterns
} Operator;

// Character class types for pattern matching
typedef enum PatternCharClass {
    PATTERN_DIGIT,      // \d - [0-9]
    PATTERN_WORD,       // \w - [a-zA-Z0-9_]
    PATTERN_SPACE,      // \s - whitespace
    PATTERN_ALPHA,      // \a - [a-zA-Z]
    PATTERN_ANY,        // \. - any character
} PatternCharClass;

// SysFunc enum is now in lambda.h (C-compatible)

typedef struct TypeBinary : Type {
    Type* left;
    Type* right;
    Operator op;  // operator
    int type_index;  // index of the type in the type list
} TypeBinary;

typedef struct TypeUnary : Type {
    Type* operand;
    Operator op;  // operator
    int type_index;  // index of the type in the type list
    int min_count;   // minimum occurrence count (for OPERATOR_REPEAT)
    int max_count;   // maximum occurrence count (-1 for unbounded)
} TypeUnary;

// Constrained type: base_type where (constraint)
// e.g. int where (5 < ~ < 10)
// The constraint_fn is a compiled function that takes the value and returns bool
// Note: ConstraintFn typedef is forward-declared above (near TypeObject)
typedef struct TypeConstrained : Type {
    Type* base;                 // base type (e.g., int, string)
    struct AstNode* constraint; // constraint expression AST (for error messages)
    int type_index;             // index in the type list
    ConstraintFn constraint_fn; // compiled constraint check function
} TypeConstrained;

typedef struct TypeParam : Type {
    struct TypeParam* next;
    bool is_optional;           // whether parameter is optional (? marker or default value)
    struct AstNode* default_value;  // default value expression (NULL if none)
    Type* full_type;            // for complex types (TypeBinary etc), points to full type; NULL for simple types
} TypeParam;

typedef struct TypeFunc : Type {
    TypeParam* param;
    Type* returned;         // return type on success
    Type* error_type;       // error type (NULL if function cannot raise errors)
    int param_count;
    int required_param_count;   // count of required (non-optional) parameters
    int type_index;
    bool is_anonymous;
    bool is_public;
    bool is_proc;
    bool is_variadic;           // function accepts variadic args (...)
    bool can_raise;             // true if function may raise errors (T^ or T^E)
} TypeFunc;

typedef struct TypeSysFunc : Type {
    SysFunc* fn;
} TypeSysFunc;

typedef struct TypeType : Type {
    Type* type;  // full type defintion
} TypeType;

// Forward declaration for RE2
namespace re2 { class RE2; }

// Compiled string/symbol pattern for regex matching
typedef struct TypePattern : Type {
    int pattern_index;      // index in type_list for runtime access
    bool is_symbol;         // true for symbol pattern, false for string pattern
    re2::RE2* re2;          // compiled RE2 regex (owned, anchored ^...$)
    re2::RE2* re2_unanchored; // unanchored regex for find/replace/split (lazy, owned)
    String* source;         // original pattern source (anchored) for error messages
} TypePattern;

struct Pack {
    size_t size;           // Current used size of the pack
    size_t capacity;       // Total capacity of the pack
    size_t committed_size; // Currently committed memory size - non-zero indicates virtual memory mode
    void* data;            // Pointer to the allocated memory
};
Pack* pack_init(size_t initial_size);
void* pack_alloc(Pack* pack, size_t size);
void* pack_calloc(Pack* pack, size_t size);
void pack_free(Pack* pack);

extern Type TYPE_NULL;
extern Type TYPE_UNDEFINED;  // JavaScript undefined
extern Type TYPE_BOOL;
extern Type TYPE_INT;
extern Type TYPE_INT64;
extern Type TYPE_BIGINT;
extern Type TYPE_FLOAT;
extern Type TYPE_DECIMAL;
extern Type TYPE_NUMBER;
extern Type TYPE_STRING;
extern Type TYPE_BINARY;
extern Type TYPE_SYMBOL;
extern Type TYPE_PATH;
extern Type TYPE_NUM_SIZED;
extern Type TYPE_UINT64;
// sub-type Type objects for sized numerics (kind = NumSizedType)
extern Type TYPE_I8;
extern Type TYPE_I16;
extern Type TYPE_I32;
extern Type TYPE_U8;
extern Type TYPE_U16;
extern Type TYPE_U32;
extern Type TYPE_F16;
extern Type TYPE_F32;
extern Type TYPE_DTIME;
extern Type TYPE_DATE;   // sub-type of datetime (precision: DATE_ONLY or YEAR_ONLY)
extern Type TYPE_TIME;   // sub-type of datetime (precision: TIME_ONLY)
extern Type TYPE_LIST;
extern Type TYPE_RANGE;
extern TypeArray TYPE_ARRAY;
extern Type TYPE_MAP;
extern Type TYPE_OBJECT;
extern Type TYPE_ELMT;
extern Type TYPE_TYPE;
extern Type TYPE_FUNC;
extern Type TYPE_ANY;
extern Type TYPE_ERROR;

extern Type CONST_BOOL;
extern Type CONST_INT;
extern Type CONST_FLOAT;
extern Type CONST_STRING;

extern Type LIT_NULL;
extern Type LIT_BOOL;
extern Type LIT_INT;
extern Type LIT_INT64;
extern Type LIT_FLOAT;
extern Type LIT_DECIMAL;
extern Type LIT_STRING;
extern Type LIT_DTIME;
extern Type LIT_NUM_SIZED;
extern Type LIT_UINT64;
extern Type LIT_TYPE;

extern TypeType LIT_TYPE_NULL;
extern TypeType LIT_TYPE_BOOL;
extern TypeType LIT_TYPE_INT;
extern TypeType LIT_TYPE_INT64;
extern TypeType LIT_TYPE_FLOAT;
extern TypeType LIT_TYPE_DECIMAL;
extern TypeType LIT_TYPE_NUMBER;
extern TypeType LIT_TYPE_STRING;
extern TypeType LIT_TYPE_BINARY;
extern TypeType LIT_TYPE_SYMBOL;
extern TypeType LIT_TYPE_PATH;
extern TypeType LIT_TYPE_DTIME;
extern TypeType LIT_TYPE_DATE;   // sub-type: date-only datetime
extern TypeType LIT_TYPE_TIME;   // sub-type: time-only datetime
extern TypeType LIT_TYPE_LIST;
extern TypeType LIT_TYPE_RANGE;
extern TypeType LIT_TYPE_ARRAY;
extern TypeType LIT_TYPE_MAP;
extern TypeType LIT_TYPE_ELMT;
extern TypeType LIT_TYPE_OBJECT;
extern TypeType LIT_TYPE_FUNC;
extern TypeType LIT_TYPE_TYPE;
extern TypeType LIT_TYPE_ANY;
extern TypeType LIT_TYPE_ERROR;
// sized numeric type references
extern TypeType LIT_TYPE_I8;
extern TypeType LIT_TYPE_I16;
extern TypeType LIT_TYPE_I32;
extern TypeType LIT_TYPE_U8;
extern TypeType LIT_TYPE_U16;
extern TypeType LIT_TYPE_U32;
extern TypeType LIT_TYPE_U64;
extern TypeType LIT_TYPE_F16;
extern TypeType LIT_TYPE_F32;

extern TypeMap EmptyMap;
extern TypeElmt EmptyElmt;
extern const Item ItemNull;
extern const Item ItemError;
extern TypeInfo type_info[];

typedef struct Input {
    void* url;
    void* path;
    Pool* pool;                 // memory pool
    Arena* arena;               // arena allocator
    NamePool* name_pool;        // centralized name management
    ShapePool* shape_pool;      // shape deduplication (NEW)
    ArrayList* type_list;       // list of types
    Item root;
    Input* parent;              // parent Input for hierarchical ownership (nullable)
    char* xml_stylesheet_href;  // href from <?xml-stylesheet?> processing instruction (nullable)
    int doc_count;              // number of YAML documents (0 or 1 = single doc, >1 = multi-doc array)
    bool ui_mode;               // true = allocate DomElement/DomText during parsing (layout/render/view commands)
    // StringBuf* sb;

    // member functions
    static Input* create(Pool* pool, Url* abs_url = nullptr, Input* parent = nullptr);
} Input;

#ifdef __cplusplus
extern "C" {
#endif

// Pool-based allocation (for runtime)
Array* array_pooled(Pool* pool);
void array_append(Array* arr, Item itm, Pool* pool, Arena* arena = nullptr);
Map* map_pooled(Pool* pool);
Element* elmt_pooled(Pool* pool);

// Arena-based allocation (for MarkBuilder)
Array* array_arena(Arena* arena);
Map* map_arena(Arena* arena);
Element* elmt_arena(Arena* arena);
List* list_arena(Arena* arena);

void map_put(Map* mp, String* key, Item value, Input *input);
void elmt_put(Element* elmt, String* key, Item value, Pool* pool);

// Shape finalization - deduplicate map/element shapes using shape pool
void map_finalize_shape(TypeMap* type_map, Input* input);
void elmt_finalize_shape(TypeElmt* type_elmt, Input* input);

// Deep structural equality for Items (Phase 14: no-op elision)
bool item_deep_equal(Item a, Item b);

#ifdef __cplusplus
}
#endif

#ifdef __cplusplus
extern "C++" {
Type* alloc_type(Pool* pool, TypeId type, size_t size);
Type* alloc_type_kind(Pool* pool, uint8_t kind, size_t size);
}
#else
Type* alloc_type(Pool* pool, TypeId type, size_t size);
Type* alloc_type_kind(Pool* pool, uint8_t kind, size_t size);
#endif
