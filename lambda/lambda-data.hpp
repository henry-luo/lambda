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

#include "core/name_pool.hpp"
#include "core/shape_pool.hpp"
#include "runtime/ast-core.hpp"

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
struct Runtime;
struct JsRuntimeState;
typedef struct TemplateRegistry TemplateRegistry;
class MarkEditor;
class Input;

// One sealed MIR module is instantiated separately in every EvalContext.  The
// generated code may retain only the module id/slot constants; the mutable
// binding storage is reached through this context-owned slab. JS direct eval
// may grow `vars`; its MIR lowering reloads that pointer from the active
// context for every module-slot access.
typedef struct LambdaModuleState {
    Item* vars;
    uint64_t* var_payloads;
    PropertyKeyRef* property_keys;
    void* consts;
    void* type_list;
    uint32_t var_count;
    uint32_t property_key_count;
    uint32_t module_id;
    bool vars_registered;
} LambdaModuleState;

// Runtime-facing scalar materializers are needed by native input adapters as
// well as generated code. Keep DateTime construction here because static input
// headers intentionally suppress the C2MIR-only lambda.h ABI declarations.
extern "C" Item push_k(DateTime dtval);

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
    // Variadic calls may nest. The active list is execution state of this
    // context, never a thread-wide register shared by unrelated runtimes.
    List* current_vargs;
    MarkEditor* edit_editor;    // active editor for this context's edit session
    Input* edit_editor_input;   // input allocated with the editor's context
    // Runtime owns the long-lived execution context. TLS may borrow this
    // pointer while code runs, but it must never become the owner. Keep this
    // after heap so MIR's hot allocation offsets remain stable.
    Runtime* runtime;
    TemplateRegistry* template_registry; // view/edit registry for this isolate
    void* render_map_state;       // context-owned render reconciliation capsule
    void* template_state_store;   // context-owned view/edit state map
    void* jube_node_session;      // context-owned Jube Node service session
    JsRuntimeState* js_state;  // context-owned JS semantic state capsule
    // Native JS ABI paths select a context-owned module slab here. Generated
    // JS MIR receives this Context directly and can load the same selector.
    LambdaModuleState* active_js_module_state;
    // Indexed by sealed module id.  The table and every state are created at
    // module-instantiation boundaries; generated hot paths only load this
    // pointer and use ordinary owner-thread loads/stores in the selected slab.
    LambdaModuleState** module_states;
    uint32_t module_state_capacity;
} EvalContext;

// Unicode-enhanced comparison functions are declared in utf_string.h
#include "core/utf_string.h"

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

// Complex values are immutable GC objects with no outgoing references.  The
// leading tag lets a raw-pointer Item participate in the normal type dispatch.
typedef struct Complex {
    TypeId type_id;
    double real;
    double imag;
} Complex;

#pragma pack(push, 1)
// TypedItem for storing data in map with type_id
typedef struct TypedItem {
    TypeId type_id;
    union {
        // inline value types
        bool bool_val;
        int int_val;
        int64_t long_val;
        uint64_t uint64_val;
        // float float_val;
        double double_val;
        uint64_t item;

        // pointer types
        void* pointer;
        Decimal* decimal;
        String* string;
        Symbol* symbol;
        Binary* binary;
        // Runtime datetimes are GC objects; static Mark data may instead point
        // into its Input arena. Keep the owner-backed object pointer intact.
        DateTime* datetime_ptr;

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

typedef struct TypeComplex : TypeConst {
    double real;
    double imag;
} TypeComplex;

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
#define JS_ACCESSOR_PAIR_LAYOUT_MAGIC 0x4A534150u
typedef struct JsAccessorPair {
    uint8_t type_id;   // = LMD_TYPE_FUNC (matches Function layout for tag compat)
    uint8_t _pad[3];
    uint32_t layout_magic;  // = JS_ACCESSOR_PAIR_LAYOUT_MAGIC for the GC tracer
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
    uint32_t name_hash;  // FNV lookup hash; never an identity.
    NameId predefined_id;  // generated global identity, NAME_ID_NONE for dynamic names.
    PropertyKeyRef key_ref;  // canonical runtime key; Input-owned shapes keep this NULL.
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
    // Lazily allocated only for shared JS shapes; non-JS/private shapes should
    // not pay for prototype-walk metadata they never use.
    struct JsProtoEntryCache* js_proto_entry_cache;
} TypeMap;

typedef struct TypeMapTransition {
    const char* name;
    uint32_t name_len;
    uint32_t name_hash;  // hash routes transition lookup; spelling remains authoritative.
    TypeId value_type;
    uint8_t flags;
    TypeMap* target;
    struct TypeMapTransition* next;
} TypeMapTransition;

// A shape flagged shared is reachable from more than one instance, so per-instance
// structural or tag mutation must clone it first.
static inline bool typemap_is_shared_shape(const TypeMap* tm) {
    return tm && (tm->is_shared_constructor_shape || tm->is_transition_shared_shape);
}

// slot_entries also accelerates ordinary transition shapes. A slot-indexed
// write is valid only for the leading constructor prefix whose storage really
// is laid out as contiguous pointer-width slots.
static inline int typemap_fixed_slot_prefix_count(const TypeMap* tm) {
    if (!tm || !tm->slot_entries || tm->slot_count <= 0 ||
            tm->byte_size < (int64_t)tm->slot_count * (int64_t)sizeof(void*)) {
        return 0;
    }
    ShapeEntry* entry = tm->shape;
    for (int i = 0; i < tm->slot_count; i++) {
        if (!entry || tm->slot_entries[i] != entry ||
                entry->byte_offset != (int64_t)i * (int64_t)sizeof(void*)) {
            return 0;
        }
        entry = entry->next;
    }
    return tm->slot_count;
}

static inline bool typemap_entry_uses_fixed_slot(const TypeMap* tm,
        const ShapeEntry* entry) {
    int fixed_count = typemap_fixed_slot_prefix_count(tm);
    for (int i = 0; i < fixed_count; i++) {
        if (tm->slot_entries[i] == entry) return true;
    }
    return false;
}

// Retag safety for in-place shaped-slot writes. Upgrading a slot's tag is always
// required so GC traces the pointer that was just stored, and leaving a stale tag
// on a T->NULL write makes the null word read back as a zero-valued T (`false`,
// `0`). The downgrade is only safe once the writing instance owns the shape: on a
// shape still flagged shared, retagging to NULL would make GC skip tracing live
// container pointers held by sibling instances.
static inline bool shape_entry_retag_is_safe(const TypeMap* tm, TypeId value_type) {
    if (value_type != LMD_TYPE_NULL) return true;
    return tm && !typemap_is_shared_shape(tm);
}

static inline void* map_field_ptr(void* map_data, const ShapeEntry* field) {
    return (uint8_t*)map_data + field->byte_offset;
}

static inline TypeId type_field_storage_type_id(const Type* type);

static inline TypeId shape_entry_storage_type_id(const ShapeEntry* field) {
    return field ? type_field_storage_type_id(field->type) : LMD_TYPE_NULL;
}

static inline bool shape_entry_storage_fits_data(const ShapeEntry* field,
        int64_t data_cap) {
    if (!field || field->byte_offset < 0 || data_cap < 0) return false;
    TypeId storage_type = shape_entry_storage_type_id(field);
    int storage_size = type_info[storage_type].byte_size;
    // Packed maps may end in a one-byte undefined/bool field; requiring a
    // pointer-width tail made that valid final field appear absent after a
    // sibling type change rebuilt the shape.
    return storage_size > 0 && field->byte_offset <= data_cap - storage_size;
}

Item map_field_to_item(void* field_ptr, TypeId type_id);
Item scalar_storage_read(Item item, bool immortal);

static inline Item map_shape_field_to_item(void* map_data, const ShapeEntry* field) {
    return map_field_to_item(map_field_ptr(map_data, field),
        shape_entry_storage_type_id(field));
}

static inline Map* map_shape_field_to_map(void* map_data, const ShapeEntry* field) {
    return map_data && field ? *(Map**)map_field_ptr(map_data, field) : nullptr;
}

// A1: FNV-1a 32-bit hash for property name lookup.
// Thin alias over lib/hash.h so the algorithm choice lives in one place.
static inline uint32_t typemap_fnv1a(const char* key, int len) {
    return hash_fnv1a_32(key, (size_t)len);
}

static inline uint32_t typemap_name_hash(const char* key, int len) {
    if (!key || len < 0) return 0;
    uint32_t id = typemap_fnv1a(key, len);
    return id ? id : 1;
}

static inline uint32_t typemap_shape_entry_name_hash(ShapeEntry* entry) {
    if (!entry || !entry->name || !entry->name->str) return 0;
    if (entry->name_hash == 0) {
        entry->name_hash = typemap_name_hash(entry->name->str, (int)entry->name->length);
    }
    return entry->name_hash;
}

// Shape hashes route probes only.  SYMBOL and PRIVATE records deliberately
// carry a unique hash, because equal diagnostic spellings are not equal keys.
static inline uint32_t typemap_shape_entry_key_hash(ShapeEntry* entry) {
    if (!entry) return 0;
    if (entry->key_ref) {
        return property_key_hash(entry->key_ref);
    }
    return typemap_shape_entry_name_hash(entry);
}

static inline bool typemap_ptr_is_plausible(void* p) {
    uintptr_t addr = (uintptr_t)p;
    // Map metadata can be corrupted into tagged/scalar debris; TypeMap
    // pointers are aligned heap allocations, never low-page or odd addresses.
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

static inline bool typemap_shape_name_equals_hash(ShapeEntry* e, const char* key,
        int key_len, uint32_t key_hash) {
    if (!e || !e->name || !e->name->str || !key || key_len < 0) return false;
    // A byte lookup is an explicitly non-canonical boundary.  It must never
    // discover a symbol/private entry merely because its diagnostic bytes match.
    if (e->key_ref && property_key_requires_identity(e->key_ref)) return false;
    uint32_t entry_hash = typemap_shape_entry_name_hash(e);
    if (entry_hash != 0 && key_hash != 0 && entry_hash != key_hash) return false;
    if (e->name->str == key && e->name->length == (size_t)key_len) return true;
    return e->name->length == (size_t)key_len &&
           memcmp(e->name->str, key, (size_t)key_len) == 0;
}

static inline bool typemap_shape_key_equals(ShapeEntry* entry, PropertyKeyRef key) {
    if (!entry || !key) return false;
    if (entry->key_ref) {
        // Runtime shapes must not fall back to spelling equality: an ordinary
        // key was canonicalized before shape publication just like SYMBOL/PRIVATE.
        return property_key_equal(entry->key_ref, key);
    }
    if (property_key_requires_identity(key)) return false;
    return entry->name && typemap_shape_name_equals_hash(entry, key->chars,
        (int)key->len, typemap_name_hash(key->chars, (int)key->len));
}

static inline bool typemap_shape_entries_equal(ShapeEntry* left, ShapeEntry* right) {
    if (!left || !right) return false;
    if (left->key_ref && right->key_ref) {
        return property_key_equal(left->key_ref, right->key_ref);
    }
    if (left->key_ref || right->key_ref) {
        PropertyKeyRef runtime_key = left->key_ref ? left->key_ref : right->key_ref;
        ShapeEntry* input_entry = left->key_ref ? right : left;
        if (property_key_requires_identity(runtime_key)) return false;
        // Input shapes intentionally have no key_ref. An ordinary runtime key
        // must merge with that byte-owned field instead of creating a stale slot.
        return input_entry->name && typemap_shape_name_equals_hash(input_entry,
            runtime_key->chars, (int)runtime_key->len,
            property_key_hash(runtime_key));
    }
    if (!left->name || !right->name) return left->name == right->name;
    return typemap_shape_name_equals_hash(left, right->name->str,
        (int)right->name->length,
        typemap_name_hash(right->name->str, (int)right->name->length));
}

static inline bool typemap_shape_name_equals(ShapeEntry* e, const char* key, int key_len) {
    return typemap_shape_name_equals_hash(e, key, key_len, typemap_name_hash(key, key_len));
}

// Canonical shape-chain lookup. Keeps last-writer-wins semantics for duplicate
// names and covers entries that were not inserted into the fixed inline hash.
static inline ShapeEntry* typemap_shape_lookup_last_by_hash(TypeMap* tm,
        const char* key, int key_len, uint32_t key_hash) {
    if (!tm) return NULL;
    ShapeEntry* found = NULL;
    for (ShapeEntry* e = tm->shape; e; e = e->next) {
        if (typemap_shape_name_equals_hash(e, key, key_len, key_hash)) {
            found = e;
        }
    }
    return found;
}

static inline ShapeEntry* typemap_shape_lookup_last(TypeMap* tm, const char* key, int key_len) {
    return typemap_shape_lookup_last_by_hash(tm, key, key_len, typemap_name_hash(key, key_len));
}

static inline ShapeEntry* typemap_shape_lookup_key_last(TypeMap* tm, PropertyKeyRef key) {
    if (!tm || !key) return NULL;
    ShapeEntry* found = NULL;
    for (ShapeEntry* entry = tm->shape; entry; entry = entry->next) {
        if (typemap_shape_key_equals(entry, key)) found = entry;
    }
    return found;
}

// A1: Insert a ShapeEntry into the TypeMap hash table (open addressing, linear probe).
// Uses last-writer-wins: if a name already exists, the slot is overwritten.
static inline void typemap_hash_insert(TypeMap* tm, ShapeEntry* entry) {
    if (!tm || !entry || !entry->name) return;
    ShapeEntry** slots = typemap_hash_slots(tm);
    int capacity = typemap_hash_capacity(tm);
    if (!slots || capacity <= 0) return;
    uint32_t h = typemap_shape_entry_key_hash(entry);
    uint32_t idx = h & ((uint32_t)capacity - 1);
    for (int probe = 0; probe < capacity; probe++) {
        uint32_t slot = (idx + (uint32_t)probe) & ((uint32_t)capacity - 1);
        if (!slots[slot]) {
            if (tm->field_count >= (uint16_t)capacity) return;
            slots[slot] = entry;
            tm->field_count++;
            return;
        }
        // last-writer-wins applies to the definitive property identity, not
        // to diagnostics bytes shared by two Symbols or private names.
        if (typemap_shape_entries_equal(slots[slot], entry)) {
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
static inline ShapeEntry* typemap_hash_lookup_by_hash(TypeMap* tm, const char* key,
        int key_len, uint32_t key_hash) {
    if (!tm || !key || key_len < 0) return NULL;
    if (key_hash == 0) key_hash = typemap_name_hash(key, key_len);
    int capacity = typemap_hash_capacity(tm);
    ShapeEntry** slots = typemap_hash_slots(tm);
    if (!slots || capacity <= 0 || tm->field_count == 0 || tm->field_count >= (uint16_t)capacity) {
        return typemap_shape_lookup_last_by_hash(tm, key, key_len, key_hash);
    }
    uint32_t idx = key_hash & ((uint32_t)capacity - 1);
    for (int probe = 0; probe < capacity; probe++) {
        uint32_t slot = (idx + (uint32_t)probe) & ((uint32_t)capacity - 1);
        ShapeEntry* e = slots[slot];
        if (!e) return NULL;  // empty slot → not found
        if (typemap_shape_name_equals_hash(e, key, key_len, key_hash)) {
            return e;
        }
    }
    return NULL;
}

static inline ShapeEntry* typemap_hash_lookup(TypeMap* tm, const char* key, int key_len) {
    return typemap_hash_lookup_by_hash(tm, key, key_len, typemap_name_hash(key, key_len));
}

static inline ShapeEntry* typemap_hash_lookup_key(TypeMap* tm, PropertyKeyRef key) {
    if (!tm || !key) return NULL;
    uint32_t key_hash = property_key_hash(key);
    int capacity = typemap_hash_capacity(tm);
    ShapeEntry** slots = typemap_hash_slots(tm);
    if (!slots || capacity <= 0 || tm->field_count == 0 ||
            tm->field_count >= (uint16_t)capacity) {
        return typemap_shape_lookup_key_last(tm, key);
    }
    uint32_t index = key_hash & ((uint32_t)capacity - 1);
    for (int probe = 0; probe < capacity; probe++) {
        ShapeEntry* entry = slots[(index + (uint32_t)probe) & ((uint32_t)capacity - 1)];
        if (!entry) return NULL;
        if (typemap_shape_key_equals(entry, key)) return entry;
    }
    return NULL;
}

typedef struct TypeElmt : TypeMap {
    StrView name;  // local name of the element
    NameId name_id;  // generated element identity; NAME_ID_NONE for custom names.
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
    // Signature contracts retain source semantics independently from the compact
    // Type prefix used by native carrier selection. In particular, implicit
    // parameters use TYPE_ANY_NO_ERROR while explicit `any` uses TYPE_ANY.
    Type* contract_type;
    bool has_explicit_contract;
} TypeParam;

typedef struct TypeFunc : Type {
    TypeParam* param;
    Type* returned;         // established success type used by the current call ABI
    Type* inferred_return;  // precise body success type retained independently of that ABI
    Type* return_contract;  // declared or implicit success contract
    Type* error_type;       // error type (NULL if function cannot raise errors)
    int param_count;
    int required_param_count;   // count of required (non-optional) parameters
    int type_index;
    bool is_anonymous;
    bool is_public;
    bool is_proc;
    bool is_variadic;           // function accepts variadic args (...)
    bool can_raise;             // true if function may raise errors (T^ or T^E)
    bool may_return_error;      // true if an Item-valued call may contain an ordinary error
    bool has_explicit_return_contract;
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
extern Type TYPE_COMPLEX;
extern Type TYPE_DECIMAL;
extern Type TYPE_INTEGER;
// Runtime integer values use the decimal carrier but retain a distinct Type*
// so static promotion cannot erase integer into ordinary decimal.
extern Type TYPE_INTEGER_VALUE;
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
// Internal contract tops carry exclusions by pointer identity only. They do
// not add a value-level Item tag and must not be compacted into TypeParam's
// carrier prefix.
extern Type TYPE_ANY_NO_ERROR;
extern Type TYPE_ANY_NO_NULL;
extern Type TYPE_ANY_NO_ERROR_OR_NULL;

// These three values use LMD_TYPE_TYPE as a compact semantic category, not a
// TypeType payload. Callers must test this before reading extended Type fields.
static inline bool type_is_global_meta_type(const Type* type) {
    return type == &TYPE_TYPE || type == &TYPE_INTEGER || type == &TYPE_NUMBER;
}

static inline bool type_is_any_without_error(const Type* type) {
    return type == &TYPE_ANY_NO_ERROR || type == &TYPE_ANY_NO_ERROR_OR_NULL;
}

static inline bool type_is_any_without_null(const Type* type) {
    return type == &TYPE_ANY_NO_NULL || type == &TYPE_ANY_NO_ERROR_OR_NULL;
}

static inline bool type_is_any_without_error_or_null(const Type* type) {
    return type == &TYPE_ANY_NO_ERROR_OR_NULL;
}

static inline const char* type_contract_display_name(const Type* type) {
    if (type == &TYPE_ANY_NO_ERROR) return "any \\ error";
    if (type == &TYPE_ANY_NO_NULL) return "any \\ null";
    if (type == &TYPE_ANY_NO_ERROR_OR_NULL) return "any \\ {error, null}";
    // Abstract numeric contracts share LMD_TYPE_TYPE with the `type` value,
    // so contract diagnostics must preserve their canonical pointer identity.
    if (type == &TYPE_INTEGER) return "integer";
    if (type == &TYPE_NUMBER) return "number";
    return type ? get_type_name(type->type_id) : "unknown";
}

// TypeBinary/TypeUnary/TypeConstrained use LMD_TYPE_TYPE as an internal
// carrier, not as a value representation. A shaped field with one of those
// contracts must retain its boxed Item; dispatching it through the `type`
// pointer lane corrupts unions such as string | error.
static inline TypeId type_field_storage_type_id(const Type* type) {
    if (!type) return LMD_TYPE_NULL;
    if (type == &TYPE_INTEGER || type == &TYPE_NUMBER) return LMD_TYPE_NULL;
    if (type->type_id == LMD_TYPE_TYPE && type->kind != TYPE_KIND_SIMPLE) {
        return LMD_TYPE_NULL;
    }
    return type->type_id;
}

static inline bool shape_entry_uses_raw_item_storage(const ShapeEntry* field) {
    return field && field->type && field->type->type_id != LMD_TYPE_NULL &&
        shape_entry_storage_type_id(field) == LMD_TYPE_NULL;
}

extern Type CONST_BOOL;
extern Type CONST_INT;
extern Type CONST_FLOAT;
extern Type CONST_STRING;

extern Type LIT_NULL;
extern Type LIT_BOOL;
extern Type LIT_INT;
extern Type LIT_INT64;
extern Type LIT_FLOAT;
extern Type LIT_COMPLEX;
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
extern TypeType LIT_TYPE_COMPLEX;
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
// Input construction never borrows the active runtime heap. These explicit
// append entry points keep parser-owned list growth in its Pool/Arena owner.
void list_push_io(List* list, Item item);
void list_push_pooled(List* list, Item item, Pool* pool);
#ifdef LAMBDA_IO_STATIC_VALUES
// C2MIR's frozen declaration remains runtime-owned.  Static input sources
// select their explicit Pool/Arena provider at compile time instead.
#define list_push list_push_io
#endif
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
