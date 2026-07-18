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
#include "../lib/hash.h"
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
#include "ast-core.hpp"

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
struct LambdaError;  // forward declaration
struct LambdaScheduler;

typedef struct EvalContext : Context {
    Heap* heap;
    Pool* ast_pool;
    NamePool* name_pool;        // name_pool for runtime-generated names
    void* type_info;  // meta info for the base types
    Item result; // final exec result
    mpd_context_t* decimal_ctx; // libmpdec context for decimal operations
    SchemaValidator* validator; // Schema validator for document validation

    // Error handling and stack trace support
    ArrayList* debug_info;      // function address → source mapping for stack traces
    const char* current_file;   // current source file (for error reporting)
    LambdaError* last_error;    // most recent runtime error (owned)
    LambdaScheduler* scheduler; // per-runtime cooperative task scheduler
} EvalContext;

// Unicode-enhanced comparison functions are declared in utf_string.h
#include "utf_string.h"

typedef struct TypeInfo {
    int byte_size;  // byte size of the type
    const char* name;  // name of the type
    Type* type;  // literal type
    Type* lit_type;  // literal type_type
    // char* c_type;  // C type of the type
} TypeInfo;

extern TypeInfo type_info[];

// const_index, type_index - 32-bit, there should not be more than 4G types and consts in a single Lambda runtime
// list item count, map size - 64-bit, to support large data files

typedef struct mpd_t mpd_t;
struct Decimal {
    uint8_t unlimited;   // 0 fixed, 1 extended decimal, DECIMAL_BIGINT integer carrier
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
        Binary* binary;

        // containers
        Container* container;
        Range* range;
        Array* array;
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

static inline NumSizedType type_num_sized_kind(const Type* type) {
    if (!type || type->type_id != LMD_TYPE_NUM_SIZED) return NUM_INT8;
    if (type->is_literal || type->is_const) {
        return ((const TypeNumSized*)type)->num_type;
    }
    return (NumSizedType)type->kind;
}

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

typedef struct TypeBinaryConst : TypeConst {
    Binary* binary;
} TypeBinaryConst;

typedef struct TypeArray : Type {
    Type* nested;  // nested item type for the array
    int64_t length;  // no. of items in the array/map
    int type_index;  // index of the type in the type list
    Item* item_patterns;  // exact per-slot pattern values for tuple-style [T, v]
    uint8_t* item_is_type_pattern;  // slot uses fn_is instead of fn_eq
} TypeArray;

typedef TypeArray TypeList;

// JS property descriptor attribute flags carried inline on ShapeEntry.
// Inverse-bit encoding: 0 = JS default (writable/enumerable/configurable, data property).
// This way pool_calloc'd entries auto-default to JS-conformant attrs without explicit init.
#define JSPD_NON_WRITABLE     0x01u  // 1 = property is read-only
#define JSPD_NON_ENUMERABLE   0x02u  // 1 = property hidden from for-in / Object.keys
#define JSPD_NON_CONFIGURABLE 0x04u  // 1 = property cannot be deleted/redefined
#define JSPD_IS_ACCESSOR      0x08u  // 1 = slot holds JsAccessorPair*, not data value
#define JSPD_DELETED          0x10u  // 1 = property logically deleted (tombstone bit;
                                     //     A2-T8 successor to JS_DELETED_SENTINEL_VAL).

// First-class accessor pair stored in the map data slot when ShapeEntry::flags has
// JSPD_IS_ACCESSOR set. Replaces the legacy `__get_X`/`__set_X` magic-key scheme.
//
// Layout starts with type_id = LMD_TYPE_FUNC so that `Item.type_id()` returns
// LMD_TYPE_FUNC for slot values pointing here (Option 2 storage scheme). This is
// safe ONLY because consumers consult `ShapeEntry::flags & JSPD_IS_ACCESSOR` BEFORE
// invoking any function operation. Any code path that calls `.function->ptr` on
// an Item from a property slot without first checking IS_ACCESSOR will misbehave.
typedef struct JsAccessorPair {
    uint8_t type_id;   // = LMD_TYPE_FUNC (matches Function layout for tag compat)
    uint8_t _pad[7];   // align getter to 8-byte boundary
    Item getter;       // ItemNull or LMD_TYPE_FUNC
    Item setter;       // ItemNull or LMD_TYPE_FUNC
} JsAccessorPair;

typedef struct ShapeEntry {
    StrView* name;
    Type* type;  // type of the field
    int64_t byte_offset;  // byte offset of the map field
    struct ShapeEntry* next;
    Target* ns;  // namespace target (NULL for unqualified fields)
    struct AstNode* default_value;  // default value expression (NULL if none)
    uint32_t name_id;  // Tune12 P3: stable non-zero name fingerprint, 0 = unset
    uint8_t flags;  // JSPD_* flags; 0 = JS default (data, writable/enum/config)
} ShapeEntry;

// A1: Property hash table — inline open-addressing table for O(1) property lookup.
// For objects with ≤32 hash-indexed properties (covers >99% of JS objects), uses
// a small fixed table indexed by FNV-1a hash. Each slot stores a ShapeEntry
// pointer. The shape chain remains authoritative when the table is not populated
// or saturates.
#define TYPEMAP_HASH_CAPACITY 32
#define TYPEMAP_HASH_DYNAMIC_MAX_CAPACITY 32768

typedef struct TypeMap : Type {
    int64_t length;  // no. of items in the map
    int64_t byte_size;  // byte size of the struct that the map is transpiled to
    int type_index;  // index of the type in the type list
    bool has_named_shape;  // shape was merged from a named type annotation (safe for direct stores)
    ShapeEntry* shape;  // first shape entry of the map
    ShapeEntry* last;  // last shape entry of the map
    const char* struct_name;  // C struct name for direct access (NULL if anonymous)
    // A1: property hash table for O(1) lookup. Small maps use the inline table;
    // larger maps may attach a pool-owned dynamic table.
    ShapeEntry* field_index[TYPEMAP_HASH_CAPACITY];  // hash table slots (NULL = empty)
    ShapeEntry** field_index_dynamic;  // NULL = use inline field_index
    uint16_t field_count;  // number of hash slots used (0 = not populated)
    uint16_t field_capacity;  // 0 = inline capacity, otherwise dynamic slot count
    // P1: Slot-indexed array for O(1) shaped property access (used by js_get_slot_fast/js_set_slot_fast).
    // Populated for constructor-shaped objects. slot_entries[i] points to the i-th ShapeEntry.
    ShapeEntry** slot_entries;  // NULL if not populated; else array of slot_count pointers
    int slot_count;             // number of slot_entries (0 = not populated)
    // A2-T1 (JS): true once this TypeMap has been cloned for a single Map's
    // private use (e.g. by an attribute mutation like defineProperty
    // non-writable). Subsequent attribute mutations on the same Map skip
    // re-cloning. The original blueprint TypeMap (referenced by call-site
    // shape caches) keeps is_private_clone=false and stays immutable.
    bool is_private_clone;
    // P4 (JS): true when this TypeMap is a canonical constructor shape shared
    // by multiple instances from one `new` callsite. Structural mutations and
    // incompatible established-slot retags must clone before mutating entries.
    bool is_shared_constructor_shape;
    // P5 (JS): parent->child transition targets are also shared across
    // instances. They are not constructor roots, but must obey the same detach
    // rules before descriptor or incompatible type mutation.
    bool is_transition_shared_shape;
    struct TypeMapTransition* transitions;
    // A3-T1 (JS): typed class identity (JsClass enum, declared in js/js_class.h).
    // Zero-init = JS_CLASS_NONE so existing TypeMaps stay opaque to the new
    // dispatch path. Stamped via `js_class_set_for_map` (which clones the
    // TypeMap first to avoid cross-instance contamination via the per-callsite
    // shape cache). Read via `js_class_get(Item)`.
    uint8_t js_class;
    // Tune12 P1b: true when an array companion map contains numeric own shape
    // entries. Pure named companions can still use direct dense element writes.
    bool has_array_index_shape;
} TypeMap;

typedef struct TypeMapTransition {
    const char* name;
    uint32_t name_len;
    uint32_t name_id;
    TypeId value_type;
    uint8_t flags;
    TypeMap* target;
    struct TypeMapTransition* next;
} TypeMapTransition;

static inline void* map_field_ptr(void* map_data, const ShapeEntry* field) {
    return (uint8_t*)map_data + field->byte_offset;
}

Item map_field_to_item(void* field_ptr, TypeId type_id);
Item scalar_storage_read(Item item, bool immortal);

static inline Item map_shape_field_to_item(void* map_data, const ShapeEntry* field) {
    return map_field_to_item(map_field_ptr(map_data, field), field->type->type_id);
}

static inline Map* map_shape_field_to_map(void* map_data, const ShapeEntry* field) {
    return map_data && field ? *(Map**)map_field_ptr(map_data, field) : nullptr;
}

// A1: FNV-1a 32-bit hash for property name lookup.
// Thin alias over lib/hash.h so the algorithm choice lives in one place.
static inline uint32_t typemap_fnv1a(const char* key, int len) {
    return hash_fnv1a_32(key, (size_t)len);
}

static inline uint32_t typemap_name_id(const char* key, int len) {
    if (!key || len < 0) return 0;
    uint32_t id = typemap_fnv1a(key, len);
    return id ? id : 1;
}

static inline uint32_t typemap_shape_entry_name_id(ShapeEntry* entry) {
    if (!entry || !entry->name || !entry->name->str) return 0;
    if (entry->name_id == 0) {
        entry->name_id = typemap_name_id(entry->name->str, (int)entry->name->length);
    }
    return entry->name_id;
}

static inline bool typemap_ptr_is_plausible(void* p) {
    uintptr_t addr = (uintptr_t)p;
    // stale inline caches can retain tagged/scalar debris; TypeMap pointers
    // are aligned heap allocations, never low-page or odd addresses.
    return p && addr >= 0x10000ULL &&
        (addr & (sizeof(void*) - 1)) == 0 &&
        addr <= 0x0000FFFFFFFFFFFFULL;
}

static inline ShapeEntry** typemap_hash_slots(TypeMap* tm) {
    if (!tm) return NULL;
    return (tm->field_index_dynamic && tm->field_capacity > 0)
        ? tm->field_index_dynamic
        : tm->field_index;
}

static inline int typemap_hash_capacity(TypeMap* tm) {
    if (!tm) return 0;
    return (tm->field_index_dynamic && tm->field_capacity > 0)
        ? (int)tm->field_capacity
        : TYPEMAP_HASH_CAPACITY;
}

static inline int typemap_hash_recommended_capacity(int64_t expected_fields) {
    if (expected_fields <= TYPEMAP_HASH_CAPACITY) return TYPEMAP_HASH_CAPACITY;
    int64_t target = expected_fields * 2;
    if (target < expected_fields) target = TYPEMAP_HASH_DYNAMIC_MAX_CAPACITY;
    int capacity = TYPEMAP_HASH_CAPACITY;
    while ((int64_t)capacity < target && capacity < TYPEMAP_HASH_DYNAMIC_MAX_CAPACITY) {
        capacity <<= 1;
    }
    return capacity;
}

static inline void typemap_hash_clear(TypeMap* tm) {
    if (!tm) return;
    memset(tm->field_index, 0, sizeof(tm->field_index));
    if (tm->field_index_dynamic && tm->field_capacity > 0) {
        memset(tm->field_index_dynamic, 0, (size_t)tm->field_capacity * sizeof(ShapeEntry*));
    }
    tm->field_count = 0;
}

static inline void typemap_hash_prepare(TypeMap* tm, Pool* pool, int64_t expected_fields) {
    if (!tm) return;
    tm->field_index_dynamic = NULL;
    tm->field_capacity = 0;
    memset(tm->field_index, 0, sizeof(tm->field_index));
    tm->field_count = 0;

    int capacity = typemap_hash_recommended_capacity(expected_fields);
    if (capacity > TYPEMAP_HASH_CAPACITY && pool) {
        ShapeEntry** dynamic_slots = (ShapeEntry**)pool_calloc(pool, (size_t)capacity * sizeof(ShapeEntry*));
        if (dynamic_slots) {
            tm->field_index_dynamic = dynamic_slots;
            tm->field_capacity = (uint16_t)capacity;
        }
    }
}

static inline bool typemap_shape_name_equals_id(ShapeEntry* e, const char* key,
        int key_len, uint32_t key_id) {
    if (!e || !e->name || !e->name->str || !key || key_len < 0) return false;
    uint32_t entry_id = typemap_shape_entry_name_id(e);
    if (entry_id != 0 && key_id != 0 && entry_id != key_id) return false;
    if (e->name->str == key && e->name->length == (size_t)key_len) return true;
    return e->name->length == (size_t)key_len &&
           memcmp(e->name->str, key, (size_t)key_len) == 0;
}

static inline bool typemap_shape_name_equals(ShapeEntry* e, const char* key, int key_len) {
    return typemap_shape_name_equals_id(e, key, key_len, typemap_name_id(key, key_len));
}

// Canonical shape-chain lookup. Keeps last-writer-wins semantics for duplicate
// names and covers entries that were not inserted into the fixed inline hash.
static inline ShapeEntry* typemap_shape_lookup_last_by_id(TypeMap* tm,
        const char* key, int key_len, uint32_t key_id) {
    if (!tm) return NULL;
    ShapeEntry* found = NULL;
    for (ShapeEntry* e = tm->shape; e; e = e->next) {
        if (typemap_shape_name_equals_id(e, key, key_len, key_id)) {
            found = e;
        }
    }
    return found;
}

static inline ShapeEntry* typemap_shape_lookup_last(TypeMap* tm, const char* key, int key_len) {
    return typemap_shape_lookup_last_by_id(tm, key, key_len, typemap_name_id(key, key_len));
}

// A1: Insert a ShapeEntry into the TypeMap hash table (open addressing, linear probe).
// Uses last-writer-wins: if a name already exists, the slot is overwritten.
static inline void typemap_hash_insert(TypeMap* tm, ShapeEntry* entry) {
    if (!tm || !entry || !entry->name) return;
    ShapeEntry** slots = typemap_hash_slots(tm);
    int capacity = typemap_hash_capacity(tm);
    if (!slots || capacity <= 0) return;
    uint32_t h = typemap_shape_entry_name_id(entry);
    uint32_t idx = h & ((uint32_t)capacity - 1);
    for (int probe = 0; probe < capacity; probe++) {
        uint32_t slot = (idx + (uint32_t)probe) & ((uint32_t)capacity - 1);
        if (!slots[slot]) {
            if (tm->field_count >= (uint16_t)capacity) return;
            slots[slot] = entry;
            tm->field_count++;
            return;
        }
        // last-writer-wins: replace existing entry with same name
        if (typemap_shape_name_equals(slots[slot],
                                      entry->name->str, (int)entry->name->length)) {
            slots[slot] = entry;
            return;
        }
    }
    // table full — callers fall back to the authoritative shape chain.
}

static inline void typemap_hash_build(TypeMap* tm, Pool* pool) {
    if (!tm) return;
    typemap_hash_prepare(tm, pool, tm->length);
    for (ShapeEntry* e = tm->shape; e; e = e->next) {
        typemap_hash_insert(tm, e);
    }
}

static inline ShapeEntry* typemap_first_field(TypeMap* tm) {
    return tm ? tm->shape : NULL;
}

static inline ShapeEntry* typemap_next_field(ShapeEntry* entry) {
    return entry ? entry->next : NULL;
}

#define FOR_EACH_MAP_FIELD(map_type, field_var) \
    for (ShapeEntry* field_var = typemap_first_field((TypeMap*)(map_type)); \
         field_var; field_var = typemap_next_field(field_var))

static inline void typemap_hash_insert_owned(TypeMap* tm, ShapeEntry* entry, Pool* pool) {
    if (!tm || !entry) return;
    int current_capacity = typemap_hash_capacity(tm);
    int wanted_capacity = typemap_hash_recommended_capacity(tm->length);
    if (pool && wanted_capacity > current_capacity) {
        typemap_hash_build(tm, pool);
        return;
    }
    typemap_hash_insert(tm, entry);
}

// A1: Lookup a ShapeEntry by name through the hash table.
// Returns the ShapeEntry or NULL if not found.
// A6: Uses pointer comparison first (interned strings via name pool share
// the same char* pointer), falling back to memcmp only on pointer mismatch.
static inline ShapeEntry* typemap_hash_lookup_by_id(TypeMap* tm, const char* key,
        int key_len, uint32_t key_id) {
    if (!tm || !key || key_len < 0) return NULL;
    if (key_id == 0) key_id = typemap_name_id(key, key_len);
    int capacity = typemap_hash_capacity(tm);
    ShapeEntry** slots = typemap_hash_slots(tm);
    if (!slots || capacity <= 0 || tm->field_count == 0 || tm->field_count >= (uint16_t)capacity) {
        return typemap_shape_lookup_last_by_id(tm, key, key_len, key_id);
    }
    uint32_t idx = key_id & ((uint32_t)capacity - 1);
    for (int probe = 0; probe < capacity; probe++) {
        uint32_t slot = (idx + (uint32_t)probe) & ((uint32_t)capacity - 1);
        ShapeEntry* e = slots[slot];
        if (!e) return NULL;  // empty slot → not found
        if (typemap_shape_name_equals_id(e, key, key_len, key_id)) {
            return e;
        }
    }
    return NULL;
}

static inline ShapeEntry* typemap_hash_lookup(TypeMap* tm, const char* key, int key_len) {
    return typemap_hash_lookup_by_id(tm, key, key_len, typemap_name_id(key, key_len));
}

typedef struct TypeElmt : TypeMap {
    StrView name;  // local name of the element
    int64_t content_length;  // no. of content items, needed for element type
    Target* ns;  // namespace target (NULL for unqualified elements)
} TypeElmt;

// TypeMethod: entry in the method table of a TypeObject
typedef struct TypeMethod {
    StrView* name;              // method name (interned)
    fn_ptr compiled_fn;         // non-GC JIT code pointer
    const char* compiled_name;  // JIT-owned name used by bound call wrappers
    uint8_t arity;              // user-visible arity, excluding self
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
    bool is_var_param;          // whether this is an inout `var` parameter
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
extern Type TYPE_FLOAT;
extern Type TYPE_FLOAT64;
extern Type TYPE_DECIMAL;
extern Type TYPE_INTEGER;
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
extern Type TYPE_F64;
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
extern TypeType LIT_TYPE_FLOAT64;
extern TypeType LIT_TYPE_DECIMAL;
extern TypeType LIT_TYPE_INTEGER;
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
extern TypeType LIT_TYPE_F64;

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
    void* mem_ctx;              // per-document MemContext sub-context (nullable; memory attribution)
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
// bulk append for callers that have already proven every key is unique and
// absent from the target map. Values are JS `undefined` slots.
bool map_put_undefined_unique_absent_bulk(Map* mp, String** keys, int count,
    Input* input, uint8_t shape_flags);
void elmt_put(Element* elmt, String* key, Item value, Pool* pool);

// Shape finalization - deduplicate map/element shapes using shape pool
void map_finalize_shape(TypeMap* type_map, Input* input);
void elmt_finalize_shape(TypeElmt* type_elmt, Input* input);

// Borrowed scalar read: boxed int64/float/uint64 Items point into ArrayNum storage.
// Use only while the source ArrayNum is alive and not being mutated.
Item array_num_read_borrowed_item(ArrayNum* array, int64_t offset);
Item array_num_read_item(ArrayNum* array, int64_t offset);
double array_num_read_double(ArrayNum* arr, int64_t offset);

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
