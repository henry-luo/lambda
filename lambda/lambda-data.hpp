#pragma once

#include <string.h>  // moved outside extern "C" block to fix C++ compatibility
#include <mpdecimal.h>

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

// mpdecimal's value layout is embedded by Decimal, while contexts remain
// runtime-private implementation detail.
typedef struct mpd_context_t mpd_context_t;

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
    NameId* property_keys;
    void* consts;
    void* type_list;
    uint32_t var_count;      // live module slots visible to generated code
    uint32_t var_capacity;   // root-range/storage capacity; may exceed var_count in REPL
    uint32_t property_key_count;
    uint32_t module_id;
    bool vars_registered;
} LambdaModuleState;

// RC-J7v2: a compilation unit's shared literal pool. Keyed by unit id rather
// than by the active module state because an `eval`'d unit compiles separately
// while sharing its caller's environment: one state, two index namespaces.
typedef struct LambdaConstPool {
    void** entries;          // mem-owned String* bodies
    uint32_t count;
    uint32_t capacity;
} LambdaConstPool;

// Runtime-facing scalar materializers are needed by native input adapters as
// well as generated code. Keep DateTime construction here because static input
// headers intentionally keep the public lambda.h ABI declarations minimal.
extern "C" Item push_k(DateTime dtval);

#include "runtime/context_capsule.h"

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
    // Every language selects its context-owned module slab here. Generated
    // Lambda and JS MIR receive this Context directly and load the same selector.
    LambdaModuleState* active_module_state;
    // Indexed by sealed module id.  The table and every state are created at
    // module-instantiation boundaries; generated hot paths only load this
    // pointer and use ordinary owner-thread loads/stores in the selected slab.
    LambdaModuleState** module_states;
    uint32_t module_state_capacity;
    // Keep new shell bookkeeping at the tail: generated and native callers
    // depend on the established module-state offsets (D8.1.3v10).
    uint32_t execution_depth;
    // JSCU27: one directory for context-owned subsystem state. Reading a
    // capsule is an indexed load; the lifecycle contract travels with it.
    ContextCapsuleDirectory capsule_directory;
    // RC-J7v2: literal pools indexed by compilation-unit id. Held here rather
    // than on the module slab because units outnumber slabs -- direct `eval`
    // compiles a fresh unit into its caller's slab -- and because every unit's
    // generated code dies with this context, which is when the pools are freed.
    LambdaConstPool** const_pools;
    uint32_t const_pool_capacity;
    uint32_t const_pool_count;
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

enum DecimalKind : uint8_t {
    DECIMAL_FIXED = 0,
    DECIMAL_EXTENDED = 1,
    DECIMAL_BIGINT = 2,
};

struct Decimal {
    DecimalKind storage_kind;
    mpd_t dec_val;  // embedded libmpdec value; its coefficient storage remains libmpdec-owned
};

static inline bool decimal_has_payload(const Decimal* decimal) {
    return decimal && (decimal->dec_val.flags & MPD_STATIC) != 0;
}

static inline mpd_t* decimal_mpd(Decimal* decimal) {
    return decimal_has_payload(decimal) ? &decimal->dec_val : NULL;
}

static inline const mpd_t* decimal_mpd(const Decimal* decimal) {
    return decimal_has_payload(decimal) ? &decimal->dec_val : NULL;
}

// Complex values are immutable GC objects with no outgoing references.  The
// leading tag lets a raw-pointer Item participate in the normal type dispatch.
typedef struct Complex {
    TypeId type_id;
    double real;
    double imag;
} Complex;

// Set by complex_new, the only constructor of Complex values, and never
// cleared. While it reads false no Item can hold a complex, so whole-tree
// complex scans (format_data) are skipped. Accessed with relaxed atomics.
extern bool g_complex_value_created;

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

static_assert(offsetof(TypedItem, type_id) == LAMBDA_GC_OFF_TYPED_ITEM_TYPE_ID &&
              offsetof(TypedItem, item) == LAMBDA_GC_OFF_TYPED_ITEM_VALUE,
              "TypedItem must match the GC ABI");

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
#define JSPD_IS_ACCESSOR      0x08u  // 1 = virtual JsAccessorCell* descriptor
#define JSPD_DELETED          0x10u  // 1 = property logically deleted (tombstone bit;
                                     //     A2-T8 successor to JS_DELETED_SENTINEL_VAL).

// A stored accessor is property metadata, never a callable value. Its GC
// allocation uses GC_TYPE_JS_ACCESSOR; a private ShapeEntry owns the only
// strong edge and publishes it as a virtual field (byte_offset == -1).
#define JS_ACCESSOR_CELL_LAYOUT_MAGIC 0x4A534143u
typedef struct JsAccessorCell {
    uint8_t type_id;   // = LMD_TYPE_UNDEFINED; ShapeEntry owns interpretation
    uint8_t _pad[3];
    uint32_t layout_magic;  // = JS_ACCESSOR_CELL_LAYOUT_MAGIC
    Item getter;       // ItemNull or LMD_TYPE_FUNC
    Item setter;       // ItemNull or LMD_TYPE_FUNC
} JsAccessorCell;

// Compatibility spelling for property-layer APIs. This is an alias only:
// there is one cell allocation and no Item/FUNC-layout carrier.
typedef JsAccessorCell JsAccessorPair;

typedef struct ShapeEntry {
    StrView* name;
    Type* type;  // type of the field
    int64_t byte_offset;  // byte offset of the map field
    // D3.4.3v3: the chain link. A transition-tree node shares its chain with
    // the descendant that extended it in place, so the link may run past a
    // type's `last` into fields that type does not have. Walk a type's fields
    // with FOR_EACH_MAP_FIELD / typemap_next_field, never by this link alone.
    struct ShapeEntry* chain_next;
    Target* ns;  // namespace target (NULL for unqualified fields)
    struct AstNode* default_value;  // default value expression (NULL if none)
    uint32_t name_hash;  // FNV lookup hash; never an identity.
    NameId name_id;  // generated or identity-scope identity; NONE for id-less Input.
    uint8_t key_kind;  // NAME_KEY_STRING, NAME_KEY_SYMBOL, or NAME_KEY_PRIVATE.
    uint8_t flags;  // JSPD_* flags; 0 = JS default (data, writable/enum/config)
    // D3.4.3v3: the entry's position in a transition-tree chain, set when the
    // tree appends it; 0 for every other entry. A hash table shared along a
    // chain holds descendants' entries too, and a type rejects any entry at or
    // past its own length. Sits in padding: ShapeEntry does not grow.
    uint32_t chain_index;
    // Object-method field lowering uses the builder-resolved binding directly.
    // Runtime/Input-created shapes leave this compiler-only edge null.
    struct NameEntry* binding;
    // SCU9 (D3.4.6): the physical descriptor derived from `type` by the one
    // resolver, at the moment the contract is assigned (shape_entry_set_type).
    // Readers, writers, the collector and MIR direct access all consult this
    // record; none re-derives a lane from `type`. Trailing so the GC ABI view
    // (LambdaGcShapeEntryLayout: type, byte_offset, next) is untouched.
    LaneStorageDesc storage;
    // JS accessor descriptors are virtual fields. Their entries are private to
    // one object, carry byte_offset == -1, and never consume Map::data space.
    JsAccessorCell* accessor;
} ShapeEntry;

// Both shape walks (map_get_by_name_id_keyed and fn_map_set) confirm a field by
// its NAME BYTES, because a NameId alone is not identity for element/input
// shapes whose spelling can drift from the id they preserve. That byte
// confirmation is correct and stays. What was wrong is HOW it was spelled:
// field names are short identifiers, so the libc memcmp CALL costs more than
// the comparison it performs -- a sampled richards2 spent 43% of its runtime in
// _platform_memcmp reached from these two walks, on names like "id" and "link".
// Compare inline at identifier length and keep memcmp only where its vectorised
// loop actually pays. Reads the same authoritative bytes, so every shape
// resolves exactly as before (D4.6.1v2-D4.6.2v2).
static inline bool shape_field_name_equals(const ShapeEntry* entry,
        const char* chars, size_t len) {
    if (!entry || !entry->name || !entry->name->str || !chars) return false;
    if (entry->name->length != len) return false;
    const char* name = entry->name->str;
    // Identifier-length names never reach memcmp's vectorised regime, so the
    // whole comparison stays inline; measured against a first-byte-reject
    // variant that still called memcmp for the tail, this full inline form was
    // worth a further 10 points on richards and 14 on json (untyped).
    if (len > 32) return memcmp(name, chars, len) == 0;
    for (size_t i = 0; i < len; i++) {
        if (name[i] != chars[i]) return false;
    }
    return true;
}

// A1v2: Property hash table — open-addressing table (FNV-1a hash, linear probe)
// for O(1) property lookup; each slot stores a ShapeEntry pointer. It lives out
// of line, allocated from the owning pool when first populated and sized to the
// shape: a power of two at least twice the field count. The shape chain
// remains authoritative when the table is absent or saturated.
// A1 kept 32 slots inline in every TypeMap: 256 bytes that shared JS shapes
// amortize but every private type pays again -- each element of a parsed
// document has one (320K of them on a 13 MiB HTML page).
#define TYPEMAP_HASH_MIN_CAPACITY 8
#define TYPEMAP_HASH_MAX_CAPACITY 32768

// JS adds an immutable semantic refinement without coupling core shapes to
// the JS runtime's metadata and operation-table definitions.
struct JsClassMeta;

typedef struct TypeMap : Type {
    int64_t length;  // no. of items in the map
    int64_t byte_size;  // byte size of the struct that the map is transpiled to
    int type_index;  // index of the type in the type list
    // A spread (`{*:m}`, `<el *:attrs>`) is ONE nameless ShapeEntry holding a
    // raw Map* link, so `length` counts it once however many fields it covers.
    // Set when such an entry enters the shape; `len()` only pays for the
    // flattening walk when it is on, and reads `length` directly otherwise.
    bool has_spread;
    bool has_named_shape;  // shape was merged from a named type annotation (safe for direct stores)
    // only compiler-built named contracts set this certificate; dynamic/input/JS
    // shapes may have the same bytes but their writers do not enforce the contract.
    bool is_trusted_contract;
    ShapeEntry* shape;  // first shape entry of the map
    ShapeEntry* last;  // last shape entry of the map
    const char* struct_name;  // C struct name for direct access (NULL if anonymous)
    // A1v2: pool-owned property hash table, NULL until first populated. A struct
    // copy shares it, so a copied TypeMap rebuilds its own (typemap_hash_prepare)
    // before inserting.
    ShapeEntry** field_index;  // field_capacity slots (NULL = empty slot)
    uint16_t field_count;  // number of hash slots used (0 = not populated)
    uint16_t field_capacity;  // slots in field_index (0 while it is NULL)
    // Optional fixed-slot index used by ordinary transition shapes.
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
    // P5 (JS): immutable transition roots/targets are shared across instances.
    // A predicted literal blueprint joins this family once it publishes null,
    // so later initialization must detach instead of upgrading its NULL slots.
    bool is_transition_shared_shape;
    struct TypeMapTransition* transitions;
    // Tune6: immutable JS semantic metadata. Null is reserved for foreign or
    // Input TypeMaps; runtime JS families select it before publication and
    // shape transitions preserve it exactly.
    const JsClassMeta* js_meta;
    // Tune12 P1b: true when an array companion map contains numeric own shape
    // entries. Pure named companions can still use direct dense element writes.
    bool has_array_index_shape;
    // Lazily allocated only for shared JS shapes; non-JS/private shapes should
    // not pay for prototype-walk metadata they never use.
    struct JsProtoEntryCache* js_proto_entry_cache;
    // D2.6.6v2 phase 2: the nominal record, or NULL for a structural shape.
    // Authoritative; the container's `is_nominal` header bit only caches it.
    struct TypeNominal* nominal;
} TypeMap;

// D3.4.3v3: a type's fields run from `shape` through `last`. A transition-tree
// node shares its chain with the descendant that extended it in place, so the
// chain may continue past `last`; every walk stops there. A type whose `last`
// is unset owns an unshared chain and walks it to its end.
static inline ShapeEntry* typemap_first_field(const TypeMap* tm) {
    return tm ? tm->shape : NULL;
}

// the step of a walk over a chain segment that ends at `last` -- for a chain
// being assembled before any type holds it
static inline ShapeEntry* shape_chain_next_until(const ShapeEntry* entry,
        const ShapeEntry* last) {
    if (!entry || entry == last) return NULL;
    return entry->chain_next;  // SHAPE_CHAIN_OK: the bounded step itself
}

static inline ShapeEntry* typemap_next_field(const TypeMap* tm, const ShapeEntry* entry) {
    return shape_chain_next_until(entry, tm ? tm->last : NULL);
}

#define FOR_EACH_MAP_FIELD(map_type, field_var) \
    for (ShapeEntry* field_var = typemap_first_field((const TypeMap*)(map_type)); \
         field_var; field_var = typemap_next_field((const TypeMap*)(map_type), field_var))

// callers detach shared shapes before changing observable field order. Physical
// offsets and slot_entries stay valid because only the enumeration chain moves.
// A detached type owns its chain, so relinking it cannot reach another type.
static inline void typemap_move_field_to_end(TypeMap* shape, ShapeEntry* field) {
    if (!shape || !field || shape->last == field) return;
    ShapeEntry* previous = NULL;
    ShapeEntry* current = shape->shape;
    while (current && current != field) {
        previous = current;
        current = typemap_next_field(shape, current);
    }
    if (!current) return;
    if (previous) previous->chain_next = field->chain_next;  // SHAPE_CHAIN_OK: unlinks from an owned chain
    else shape->shape = field->chain_next;  // SHAPE_CHAIN_OK: unlinks from an owned chain
    field->chain_next = NULL;
    if (shape->last) shape->last->chain_next = field;
    else shape->shape = field;
    shape->last = field;
}


// The C collector walks these descriptor prefixes without including this
// C++ header. Keep that bridge checked at the defining types.
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Winvalid-offsetof"
static_assert(offsetof(ShapeEntry, type) == LAMBDA_GC_OFF_SHAPE_ENTRY_TYPE &&
              offsetof(ShapeEntry, byte_offset) == LAMBDA_GC_OFF_SHAPE_ENTRY_BYTE_OFFSET &&
              offsetof(ShapeEntry, chain_next) == LAMBDA_GC_OFF_SHAPE_ENTRY_NEXT,
              "ShapeEntry must match the GC ABI");
static_assert(offsetof(TypeMap, byte_size) == LAMBDA_GC_OFF_TYPE_MAP_BYTE_SIZE &&
              offsetof(TypeMap, shape) == LAMBDA_GC_OFF_TYPE_MAP_SHAPE &&
              offsetof(TypeMap, last) == LAMBDA_GC_OFF_TYPE_MAP_LAST,
              "TypeMap must match the GC ABI");
#pragma clang diagnostic pop

typedef struct TypeMapTransition {
    NameId name_id;
    uint8_t key_kind;
    const char* name; // retained only for the explicit id-less Input seam
    uint32_t name_len;
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
        entry = typemap_next_field(tm, entry);
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

// ---------------------------------------------------------------------------
// SCU7/SCU8: the one storage resolver and its projections.
// Defined in lambda/core/lambda-data.cpp, NOT header-inline: these are long
// classifiers, and a `static inline` of that size in a header this widely
// included is emitted out-of-line in every TU that cannot fully inline it --
// which pulled a symbol into test binaries that do not link it and made them
// fail to LOAD (`symbol not found in flat namespace '_ItemError'`, Tune19 §12.9).
// ---------------------------------------------------------------------------

// Pure contract walks promoted from the runtime type-contract rules so the
// core resolver (and the collector, and Input) can use them without linking
// the runtime library.
bool lambda_type_accepts_error(Type* type);
bool lambda_type_accepts_null(Type* type);
// The payload contract under `T?` / `T | null` (nullable=true), or the
// contract itself, unwrapping type parameters and constrained bases. NULL for
// heterogeneous unions and error-admitting `null | T`.
Type* lambda_type_nullable_lane_base(Type* type, bool* nullable);

// THE resolver (D2.6.1, D3.4.6): total over well-formed contracts. A null
// contract logs and yields LANE_STORAGE_INVALID (D1.9: never a guess).
LaneStorageDesc lambda_lane_storage_desc_for(Type* type);

// Persistent native destinations refine the scalar ABI only where an optional
// full-width integer cannot encode null in one raw word (D2.5.2v3).
LaneStorageDesc lambda_persistent_lane_storage_desc_for(Type* type);

// Width projection: the packed slot size of a field holding `type`.
static inline int lambda_lane_storage_size(Type* type) {
    return lambda_persistent_lane_storage_desc_for(type).byte_size;
}

// Decoding-TypeId projection (what map_field_to_item and the collector read).
TypeId type_field_storage_type_id(const Type* type);

// SCU9: assign a field's contract and derive its descriptor once. Every
// ShapeEntry constructor and every retag goes through here.
void shape_entry_set_type(ShapeEntry* entry, Type* type);
// The stored descriptor. A constructor that bypassed shape_entry_set_type is
// repaired on first read (and logged) so a missed site cannot desynchronize
// readers; it is a bug to rely on that.
const LaneStorageDesc* shape_entry_storage(const ShapeEntry* entry);

// Read one shaped field's value. Defined in lambda-data-runtime.cpp; declared
// here rather than re-externed per consumer, which is how the JS adapter, the
// document node table, and the Tier-3 write set had each grown their own copy.
Item _map_read_field(ShapeEntry* field, void* map_data);

// Nullable-native projection: the field's slot is int?/bool?/float?/T? lane.
bool shape_entry_uses_native_lane(const ShapeEntry* field,
        LaneStorageDesc* out);

static inline TypeId shape_entry_storage_type_id(const ShapeEntry* field) {
    return field ? (TypeId)shape_entry_storage(field)->value_domain : LMD_TYPE_NULL;
}

static inline int shape_entry_storage_size(const ShapeEntry* field) {
    return field ? shape_entry_storage(field)->byte_size : 0;
}

static inline bool shape_entry_storage_fits_data(const ShapeEntry* field,
        int64_t data_cap) {
    if (!field || field->byte_offset < 0 || data_cap < 0) return false;
    int storage_size = shape_entry_storage_size(field);
    // Packed maps may end in a one-byte undefined/bool field; requiring a
    // pointer-width tail made that valid final field appear absent after a
    // sibling type change rebuilt the shape.
    return storage_size > 0 && field->byte_offset <= data_cap - storage_size;
}

Item map_field_to_item(void* field_ptr, TypeId type_id);
// Read/write helpers must see ShapeEntry::type: TypeId alone cannot tell
// `int` apart from `int?` once both use an eight-byte packed slot.
Item map_shape_field_to_item(void* map_data, const ShapeEntry* field);
// Static MIR member sites already carry a context-resolved NameId. Keep the
// hot lookup on that identity instead of reconstructing a boxed key string;
// NAME_ID_NONE remains the id-less Input fallback handled by the caller.
Item map_get_by_name_id(Container* owner, TypeMap* map_type, void* map_data,
    NameId name_id, bool* is_found);
bool map_shape_field_store_native_lane(void* field_ptr, const ShapeEntry* field,
    Item value);
Item scalar_storage_read(Item item, bool immortal);

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
    return tm ? tm->field_index : NULL;
}

static inline int typemap_hash_capacity(TypeMap* tm) {
    return tm && tm->field_index ? (int)tm->field_capacity : 0;
}

// a power of two at least twice the fields, so probes stay short; 0 for none
static inline int typemap_hash_recommended_capacity(int64_t expected_fields) {
    if (expected_fields <= 0) return 0;
    int64_t target = expected_fields * 2;
    if (target < expected_fields) target = TYPEMAP_HASH_MAX_CAPACITY;
    int capacity = TYPEMAP_HASH_MIN_CAPACITY;
    while ((int64_t)capacity < target && capacity < TYPEMAP_HASH_MAX_CAPACITY) {
        capacity <<= 1;
    }
    return capacity;
}

// Where a type's records come from (D4.1.4v4). A transition-tree node is never
// freed on its own, so it takes the Input's arena and goes with the Input
// (D3.4.3v3, D4.2.6); a type a container owns takes a pool.
typedef struct TypeAlloc {
    Pool* pool;
    Arena* arena;   // when set, records come from here rather than `pool`
} TypeAlloc;

static inline TypeAlloc type_alloc_of_pool(Pool* pool) {
    TypeAlloc alloc = {pool, NULL};
    return alloc;
}

static inline void* type_alloc_zeroed(TypeAlloc alloc, size_t size) {
    if (alloc.arena) return arena_calloc(alloc.arena, size);
    return alloc.pool ? pool_calloc(alloc.pool, size) : NULL;
}

// Allocates a fresh empty table for `expected_fields`. Any previous table is
// left to its owner: a struct copy of this TypeMap may still share it.
static inline void typemap_hash_prepare_in(TypeMap* tm, TypeAlloc alloc, int64_t expected_fields) {
    if (!tm) return;
    tm->field_index = NULL;
    tm->field_capacity = 0;
    tm->field_count = 0;

    int capacity = typemap_hash_recommended_capacity(expected_fields);
    if (capacity > 0) {
        ShapeEntry** slots = (ShapeEntry**)type_alloc_zeroed(alloc,
            (size_t)capacity * sizeof(ShapeEntry*));
        if (slots) {
            tm->field_index = slots;
            tm->field_capacity = (uint16_t)capacity;
        }
    }
}

static inline void typemap_hash_prepare(TypeMap* tm, Pool* pool, int64_t expected_fields) {
    typemap_hash_prepare_in(tm, type_alloc_of_pool(pool), expected_fields);
}

static inline bool typemap_shape_name_equals_hash(ShapeEntry* e, const char* key,
        int key_len, uint32_t key_hash) {
    if (!e || !e->name || !e->name->str || !key || key_len < 0) return false;
    // A byte lookup is an explicitly non-canonical boundary.  It must never
    // discover a symbol/private entry merely because its diagnostic bytes match.
    if (e->key_kind != NAME_KEY_STRING) return false;
    uint32_t entry_hash = typemap_shape_entry_name_hash(e);
    if (entry_hash != 0 && key_hash != 0 && entry_hash != key_hash) return false;
    return e->name->length == (size_t)key_len &&
           memcmp(e->name->str, key, (size_t)key_len) == 0;
}

static inline bool typemap_shape_entries_equal(ShapeEntry* left, ShapeEntry* right) {
    if (!left || !right) return false;
    if (left->name_id != NAME_ID_NONE && right->name_id != NAME_ID_NONE) {
        return left->name_id == right->name_id;
    }
    if (left->key_kind != NAME_KEY_STRING || right->key_kind != NAME_KEY_STRING) {
        return false;
    }
    if (!left->name || !right->name) return !left->name && !right->name;
    return typemap_shape_name_equals_hash(left, right->name->str,
        (int)right->name->length,
        typemap_name_hash(right->name->str, (int)right->name->length));
}

// Canonical shape-chain lookup. Keeps last-writer-wins semantics for duplicate
// names and covers entries that were not inserted into the fixed inline hash.
static inline ShapeEntry* typemap_shape_lookup_last_by_hash(TypeMap* tm,
        const char* key, int key_len, uint32_t key_hash) {
    if (!tm) return NULL;
    ShapeEntry* found = NULL;
    FOR_EACH_MAP_FIELD(tm, e) {
        if (typemap_shape_name_equals_hash(e, key, key_len, key_hash)) {
            found = e;
        }
    }
    return found;
}

// Input-owned fields deliberately have no NameId.  A runtime NameId lookup may
// confirm those fields by bytes at the Input boundary, but it must never use
// that seam to select a different runtime-created property with the same
// spelling.
static inline ShapeEntry* typemap_shape_lookup_last_idless_by_hash(TypeMap* tm,
        const char* key, int key_len, uint32_t key_hash) {
    if (!tm) return NULL;
    ShapeEntry* found = NULL;
    FOR_EACH_MAP_FIELD(tm, e) {
        if (e->name_id == NAME_ID_NONE &&
                typemap_shape_name_equals_hash(e, key, key_len, key_hash)) {
            found = e;
        }
    }
    return found;
}

static inline ShapeEntry* typemap_shape_lookup_last(TypeMap* tm, const char* key, int key_len) {
    return typemap_shape_lookup_last_by_hash(tm, key, key_len, typemap_name_hash(key, key_len));
}

// NameId is definitive for runtime-created JS entries. Input-owned entries
// intentionally carry NAME_ID_NONE and remain on the byte-confirmation path.
static inline bool typemap_shape_entry_has_name_id(const ShapeEntry* entry,
        NameId name_id) {
    return entry && name_id != NAME_ID_NONE && entry->name_id == name_id;
}

static inline ShapeEntry* typemap_shape_lookup_last_by_name_id(TypeMap* tm,
        NameId name_id) {
    if (!tm || name_id == NAME_ID_NONE) return NULL;
    ShapeEntry* found = NULL;
    FOR_EACH_MAP_FIELD(tm, entry) {
        if (typemap_shape_entry_has_name_id(entry, name_id)) found = entry;
    }
    return found;
}

// D3.4.3v3: a table shared along a transition-tree chain also holds the
// entries of descendants that extended the chain in place; theirs sit at or
// past this type's length. A shared table never takes a second entry for an
// identity it holds, so hitting one of theirs means the name is not a field of
// this type -- a miss, answered without walking the chain.
static inline bool typemap_hash_entry_is_own(const TypeMap* tm, const ShapeEntry* entry) {
    // position 0 is on every type of its chain, and an entry the tree did not
    // append carries 0, so only a tree node's length is ever consulted
    return entry->chain_index == 0 || (int64_t)entry->chain_index < tm->length;
}

// nameid is the definitive property identity. Probe the existing hash table
// with the pooled spelling's cached hash so the common JS path never walks the
// authoritative shape chain; the chain remains the correctness fallback when
// a shape has no usable table or the table is saturated.
static inline ShapeEntry* typemap_hash_lookup_by_name_id(TypeMap* tm,
        NameId name_id, uint32_t key_hash) {
    if (!tm || name_id == NAME_ID_NONE) return NULL;
    ShapeEntry** slots = typemap_hash_slots(tm);
    int capacity = typemap_hash_capacity(tm);
    if (!slots || capacity <= 0 || tm->field_count == 0 ||
            tm->field_count >= (uint16_t)capacity || key_hash == 0) {
        return typemap_shape_lookup_last_by_name_id(tm, name_id);
    }
    uint32_t idx = key_hash & ((uint32_t)capacity - 1);
    for (int probe = 0; probe < capacity; probe++) {
        uint32_t slot = (idx + (uint32_t)probe) & ((uint32_t)capacity - 1);
        ShapeEntry* entry = slots[slot];
        if (!entry) return NULL;
        if (entry->name_id == name_id) {
            return typemap_hash_entry_is_own(tm, entry) ? entry : NULL;
        }
    }
    return typemap_shape_lookup_last_by_name_id(tm, name_id);
}

// A1: the slot on `entry`'s probe path that holds an entry with its identity,
// else the first empty one; -1 when there is no table or it is full.
static inline int typemap_hash_probe_slot(TypeMap* tm, ShapeEntry* entry) {
    ShapeEntry** slots = typemap_hash_slots(tm);
    int capacity = typemap_hash_capacity(tm);
    if (!slots || capacity <= 0) return -1;
    uint32_t idx = typemap_shape_entry_key_hash(entry) & ((uint32_t)capacity - 1);
    for (int probe = 0; probe < capacity; probe++) {
        uint32_t slot = (idx + (uint32_t)probe) & ((uint32_t)capacity - 1);
        // last-writer-wins applies to the definitive property identity, not
        // to diagnostics bytes shared by two Symbols or private names.
        if (!slots[slot] || typemap_shape_entries_equal(slots[slot], entry)) return (int)slot;
    }
    return -1;
}

// A1: Insert a ShapeEntry into the TypeMap hash table (open addressing, linear probe).
// Uses last-writer-wins: if a name already exists, the slot is overwritten.
static inline void typemap_hash_insert(TypeMap* tm, ShapeEntry* entry) {
    if (!tm || !entry || !entry->name) return;
    int slot = typemap_hash_probe_slot(tm, entry);
    // table full — callers fall back to the authoritative shape chain.
    if (slot < 0) return;
    ShapeEntry** slots = typemap_hash_slots(tm);
    if (!slots[slot]) {
        if (tm->field_count >= (uint16_t)typemap_hash_capacity(tm)) return;
        tm->field_count++;
    }
    slots[slot] = entry;
}

// D3.4.3v3: whether the table already holds an entry with `entry`'s identity.
// A chain's shared table must not take such an entry: overwriting the slot
// would hide a field from the types that still read it through the table.
static inline bool typemap_hash_holds_equal(TypeMap* tm, ShapeEntry* entry) {
    if (!tm || !entry || !entry->name) return false;
    int slot = typemap_hash_probe_slot(tm, entry);
    return slot >= 0 && typemap_hash_slots(tm)[slot] != NULL;
}

static inline void typemap_hash_build_in(TypeMap* tm, TypeAlloc alloc) {
    if (!tm) return;
    typemap_hash_prepare_in(tm, alloc, tm->length);
    FOR_EACH_MAP_FIELD(tm, e) {
        typemap_hash_insert(tm, e);
    }
}

static inline void typemap_hash_build(TypeMap* tm, Pool* pool) {
    typemap_hash_build_in(tm, type_alloc_of_pool(pool));
}

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
            return typemap_hash_entry_is_own(tm, e) ? e : NULL;
        }
    }
    return NULL;
}

static inline ShapeEntry* typemap_hash_lookup(TypeMap* tm, const char* key, int key_len) {
    return typemap_hash_lookup_by_hash(tm, key, key_len, typemap_name_hash(key, key_len));
}

static inline ShapeEntry* typemap_hash_lookup_idless(TypeMap* tm,
        const char* key, int key_len) {
    return typemap_shape_lookup_last_idless_by_hash(tm, key, key_len,
        typemap_name_hash(key, key_len));
}

typedef struct TypeElmt : TypeMap {
    StrView name;  // local name of the element
    NameId name_id;  // generated element identity; NAME_ID_NONE for custom names.
    // D2.6.6v3: a DECLARED element type's content pattern, matched against the
    // children as a sequence pattern (S11.1.6v3); NULL leaves content
    // unconstrained. Instance and literal types never carry one, so it plays
    // no part in type sharing (D3.4.3v3).
    TypeList* content_list;
    Target* ns;  // namespace target (NULL for unqualified elements)
} TypeElmt;

// TypeMethod: entry in the method table of a TypeObject
typedef struct TypeMethod {
    StrView* name;              // method name (interned)
    fn_ptr compiled_fn;         // non-GC JIT code pointer
    const char* compiled_name;  // JIT-owned name used by bound call wrappers
    struct TypeFunc* fn_type;   // semantic signature retained for dynamic calls
    const struct AstFuncNode* ast_def;  // T0 definition retained beside the JIT entry
    struct Script* ast_module;  // Script that owns ast_def's slab, consts, and type list
    uint8_t arity;              // user-visible arity, excluding self
    bool is_proc;               // true for pn, false for fn
    struct TypeMethod* next;    // linked list
} TypeMethod;

// Forward declaration for constraint function pointer (full typedef below near TypeConstrained)
typedef uint8_t (*ConstraintFn)(uint64_t value);

// TypeObject: nominally-typed map with methods
// Extends TypeMap — inherits shape (fields), length, byte_size, type_index
// D2.6.6v2 phase 2 (S2.1.4): the NOMINAL RECORD. Nominal-ness is a property of
// the type descriptor, not a container kind — a nominal value is an ordinary
// map or element whose shape points here. One record is shared by the declared
// shape AND by every shape reached from it by extension, which is what lets an
// open instance gain a field without ceasing to be an instance of its type
// (S2.1.4 part 3, OB16); `is T` therefore compares this POINTER, never a name.
// The record is sealed for the life of the evaluation (S2.1.4 part 2).
typedef struct TypeNominal {
    StrView type_name;            // "Point", "Circle"
    struct TypeNominal* base;     // parent record, NULL if none
    TypeMethod* methods;          // linked list head
    TypeMethod* methods_last;     // linked list tail
    int method_count;
    struct AstNode* constraint;   // object-level that(...) AST, NULL if none
    ConstraintFn constraint_fn;   // JIT-compiled constraint checker, NULL if none
    TypeId struct_kind;           // the one structural kind this type declares
} TypeNominal;

// D2.6.6v2 phase 2: an object's shape extends TypeElmt, not TypeMap. A nominal
// type declares ONE structural kind (S2.1.3v2) — map or element — and this shape
// serves both: a nominal map simply leaves the element fields unused, while a
// nominal element needs `name`/`content_list`/`ns` at TypeElmt's own offsets
// so every element code path reads it correctly. Before this, an object's shape
// was a TypeMap and element readers reached the element fields at the wrong
// offset, working only by accident where `type_name` happened to alias `name`.
typedef struct TypeObject : TypeElmt {
    StrView type_name;          // nominal type name ("Point", "Circle"); mirrors TypeElmt::name
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
    PATTERN_ANY_STRING, // ... - any string
} PatternCharClass;

// SysFunc enum is now in lambda.h (C-compatible)

typedef struct TypeBinary : Type {
    Type* left;
    Type* right;
    Operator op;  // operator
    int type_index;  // index of the type in the type list
} TypeBinary;

// Is this Type a `T1 | T2` union? Callers that key representation or ABI
// decisions on `type_id` see LMD_TYPE_TYPE for every structured type, so a
// union in expression position must be treated like ANY (boxed, dynamic) —
// never like its payload. This predicate is the shared spelling of that test
// (15 call sites previously open-coded the kind check).
static inline bool lambda_type_is_union(const Type* type) {
    return type && type->type_id == LMD_TYPE_TYPE &&
        type->kind == TYPE_KIND_BINARY &&
        ((const TypeBinary*)type)->op == OPERATOR_UNION;
}

typedef struct TypeUnary : Type {
    Type* operand;
    Operator op;  // operator
    int type_index;  // index of the type in the type list
    // occurrence bounds for OPERATOR_REPEAT; for OPERATOR_ARRAY, `T[n]` fixes
    // the length (min = max = n) and `T[]` is (0, -1) (S11.1.1v3)
    int min_count;
    int max_count;   // maximum count (-1 for unbounded)
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
    // the declaring module: the predicate reads its names wherever `is` runs
    // it, an importer's included (S10.1.7v2)
    Script* module;
    // T0 frame-plan facts: the window of names the predicate binds itself
    // (BINDING_STORAGE_PREDICATE), reserved afresh by each evaluation
    uint16_t predicate_slots;
    bool predicate_planned;
} TypeConstrained;

// A binder appears only in a function parameter contract.  `bound` is the
// written contract; the call frame replaces `env[slot]` with the narrowest
// admitted runtime type.  A second site for the same spelling shares `slot`.
typedef struct TypeBinder : Type {
    Type* bound;
    Name* name;
    String* parameter_name;
    uint16_t slot;
    int type_index;
} TypeBinder;

// A use of a preceding binder name.  No pointer back to TypeBinder is kept:
// serialized/module-local contracts use the slot plus copied bound only.
typedef struct TypeBoundRef : Type {
    Type* bound;
    uint16_t slot;
    int type_index;
} TypeBoundRef;

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
    TypeBinder* binder;       // non-null for an explicit `T: type` parameter
    struct AstNode* type_expr; // source contract, retained for binder ordering checks
} TypeParam;

// TYPE_KIND_PARAM marks a TypeParam, which every AST_NODE_PARAM binding owns.
// Pass only such a binding's type: a TypeParam keeps its carrier TypeId, and a
// non-literal f16 Type stores NUM_FLOAT16, which equals TYPE_KIND_PARAM, in
// the same `kind` field, so the test cannot classify an arbitrary Type.
static inline TypeParam* lambda_type_param(Type* type) {
    return type && type->kind == TYPE_KIND_PARAM ? (TypeParam*)type : NULL;
}

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
    // S12.1.4v2: declared with `function` — the value is `fn`, but a call is
    // `pn` when a `function`-typed argument is (its polymorphic slots)
    bool is_colour_poly;
    uint16_t binder_count;
    TypeBinder** binders;       // canonical binder for each slot
} TypeFunc;

typedef struct TypeSysFunc : Type {
    SysFunc* fn;
} TypeSysFunc;

typedef struct TypeType : Type {
    Type* type;  // full type defintion
} TypeType;

typedef struct TypeRange : Type {
    Item start;  // inclusive lower bound for range annotations
    Item end;    // inclusive upper bound for range annotations
    bool is_char;
} TypeRange;

// Forward declaration for RE2
namespace re2 { class RE2; }

// Compiled string/symbol pattern for regex matching
typedef struct TypePattern : Type {
    int pattern_index;      // index in type_list for runtime access
    bool is_symbol;         // true for symbol pattern, false for string pattern
    re2::RE2* re2;          // compiled RE2 regex (owned, anchored ^...$)
    re2::RE2* re2_unanchored; // unanchored regex for find/replace/split (lazy, owned)
    String* source;         // canonical Lambda pattern source for diagnostics
    String* regex_source;   // compiled anchored regex source used by partial matching
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
// The name of a type that shares another kind's TypeId (S2.1.1v4): the
// numeric unions `integer` and `number`, and the array subkind `list`. NULL
// for every other type; callers fall back to the TypeId's name.
const char* type_alias_name(Type* type);
// Internal contract tops carry exclusions by pointer identity only. They do
// not add a value-level Item tag and must not be compacted into TypeParam's
// carrier prefix.
extern Type TYPE_ANY_NO_ERROR;
extern Type TYPE_ANY_NO_NULL;
extern Type TYPE_ANY_NO_ERROR_OR_NULL;

// D3.1.1v4: a range type `X to Y` is a type-kind node under the shared
// LMD_TYPE_TYPE tag. It must never wear LMD_TYPE_RANGE, the tag of a range
// VALUE, or every tag-driven consumer reads "an int from 1 to 5" as "a range"
// (LR03-18, LR03-14).
static inline bool lambda_type_is_range(const Type* type) {
    return type && type->type_id == LMD_TYPE_TYPE && type->kind == TYPE_KIND_RANGE;
}

// The domain a range type draws its members from (S11.1.3): `int` for an
// integer range, `string` for a character range. It serves static carrier
// checks only. A member keeps its own representation (`3.0` is a member of
// `1 to 5`), so the domain never selects a native lane.
static inline Type* lambda_range_type_domain(const Type* type) {
    if (!lambda_type_is_range(type)) return NULL;
    return ((const TypeRange*)type)->is_char ? &TYPE_STRING : &TYPE_INT;
}

// S11.1.5: `function` is the compact TYPE_FUNC singleton — the signature-less
// union of `fn` and `pn`. Every other LMD_TYPE_FUNC type is a full TypeFunc,
// so a caller that needs a signature must ask here rather than cast on the id.
static inline TypeFunc* lambda_type_func_signature(Type* type) {
    return type && type->type_id == LMD_TYPE_FUNC && type != &TYPE_FUNC
        ? (TypeFunc*)type : NULL;
}

// true only for a `pn` signature; `function` leaves the colour open
static inline bool lambda_type_func_is_proc(Type* type) {
    TypeFunc* signature = lambda_type_func_signature(type);
    return signature && signature->is_proc;
}

// S12.1.4v2(1): a polymorphic slot of a `function` declaration is a parameter
// whose contract is exactly `function`; `fn (...)`/`pn (...)` slots keep
// their fixed colour and never vote.
// S12.1.4v2(3): the run-time half of an `fn`-context call's colour check.
// Bit 31 asks for the callee's own colour (a dynamic callee); bits 0..15 name
// the argument positions whose colour could not be resolved statically, and
// are verified only where they land in the callee's polymorphic slots.
#define LAMBDA_COLOUR_GUARD_CALLEE (1u << 31)
#define LAMBDA_COLOUR_GUARD_ARGS 0xFFFFu

static inline bool lambda_type_param_is_colour_poly(const TypeParam* param) {
    if (!param) return false;
    const Type* contract = param->contract_type ? param->contract_type : param->full_type;
    return contract == &TYPE_FUNC;
}

// D2.6.6v2: the generic map/element/object descriptors are compact `Type`
// singletons.  Only a concrete descriptor can be read as its extended
// TypeMap shape; keeping the discriminator here prevents each language front
// end from duplicating that ABI boundary.
static inline bool lambda_type_is_concrete_attr_shape(const Type* type) {
    if (!type || (type->type_id != LMD_TYPE_MAP &&
            type->type_id != LMD_TYPE_ELEMENT)) return false;
    return type != &TYPE_MAP && type != &TYPE_ELMT && type != &TYPE_OBJECT;
}

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

// Numeric contract names a TypeId cannot tell apart. The abstract contracts
// share LMD_TYPE_TYPE with the `type` value, so they keep their canonical
// pointer identity; every sized type shares LMD_TYPE_NUM_SIZED, whose
// diagnostics read "num_sized" instead of `u8` (LR03-11).
static inline const char* type_numeric_contract_name(const Type* type) {
    if (type == &TYPE_INTEGER) return "integer";
    if (type == &TYPE_NUMBER) return "number";
    if (type && type->type_id == LMD_TYPE_NUM_SIZED) {
        return get_num_sized_type_name(type_num_sized_kind(type));
    }
    return NULL;
}

static inline const char* type_contract_display_name(const Type* type) {
    if (type == &TYPE_ANY_NO_ERROR) return "any \\ error";
    if (type == &TYPE_ANY_NO_NULL) return "any \\ null";
    if (type == &TYPE_ANY_NO_ERROR_OR_NULL) return "any \\ {error, null}";
    if (const char* numeric = type_numeric_contract_name(type)) return numeric;
    // a function contract names its colour; bare `function` falls through
    if (TypeFunc* signature = lambda_type_func_signature((Type*)type)) {
        return signature->is_proc ? "pn" : "fn";
    }
    return type ? get_type_name(type->type_id) : "unknown";
}

// TypeBinary/TypeUnary/TypeConstrained use LMD_TYPE_TYPE as an internal
// carrier, not as a value representation. A shaped field with one of those
// contracts must retain its boxed Item; dispatching it through the `type`
// pointer lane corrupts unions such as string | error.
static inline Type* type_field_unwrap_simple_decl(Type* type) {
    while (type && type->type_id == LMD_TYPE_TYPE &&
            !type_is_global_meta_type(type) && type->kind == TYPE_KIND_SIMPLE) {
        Type* inner = ((TypeType*)type)->type;
        if (!inner) break;
        type = inner;
    }
    return type;
}

// A refinement alias nests one constrained base in another (`type Small = Pos
// that ~ < 10`), and a chain this deep can only be a cycle.
#define LAMBDA_CONSTRAINT_CHAIN_MAX 32

static inline bool lambda_type_is_constrained(const Type* type) {
    return type && type->type_id == LMD_TYPE_TYPE &&
        type->kind == TYPE_KIND_CONSTRAINED;
}

// The base a constrained type finally admits, past every `that` layer and
// SIMPLE wrapper; NULL for a cyclic chain.
static inline Type* lambda_constrained_type_base(Type* type) {
    for (int depth = 0; depth <= LAMBDA_CONSTRAINT_CHAIN_MAX; depth++) {
        type = type_field_unwrap_simple_decl(type);
        if (!lambda_type_is_constrained(type)) return type;
        type = ((TypeConstrained*)type)->base;
    }
    return NULL;
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
// PTH30: `reference` is the type ALIAS `symbol | path` (URI = URN | URL), not a
// nominal supertype — a supertype would force every symbol operation
// (indexing, slicing, `\symbol(…)` islands) to rule on paths, whereas the alias
// dissolves at each use site into the two evaluation contracts S2.4.3v3 keeps
// distinct.
extern TypeBinary TYPE_REFERENCE;
extern TypeType LIT_TYPE_REFERENCE;
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

// D2.6.6v2 phase 2 (S2.1.1v3): the value's NOMINAL RECORD, or NULL when it is
// structural. `A is object` is exactly "this returns non-NULL", and it is
// orthogonal to the structural kind — a nominal map answers both `is map` and
// `is object`. The record is reached through the shape, which is authoritative;
// the container's header bit only caches the same answer.
static inline struct TypeNominal* lambda_value_nominal(TypeId type_id,
        const void* container) {
    struct TypeMap* shape = lambda_attr_shape(type_id, container);
    return shape ? shape->nominal : nullptr;
}

// The nominal record of a TYPE descriptor, or NULL if the type is structural.
// Accepts the object tag too so it reads the same before and after the
// representation flip retires it.
static inline struct TypeNominal* type_nominal_record(const Type* t) {
    // The base flag is the discriminator: only a real shape sets it, so this is
    // safe to ask of a bare singleton Type as well.
    if (!t || !t->is_nominal) return nullptr;
    return ((const TypeMap*)t)->nominal;
}

// S11.3.1v2: `A is T` for a nominal T — the record chain is walked and compared
// by POINTER, never by type name, so two modules' `Point`s stay distinct and
// every shape extended from one declaration still answers yes (OB16, OB19).
static inline bool lambda_nominal_derives_from(const struct TypeNominal* actual,
        const struct TypeNominal* wanted) {
    for (const struct TypeNominal* walk = actual; walk; walk = walk->base) {
        if (walk == wanted) return true;
    }
    return false;
}

extern TypeMap EmptyMap;
// D2.6.6v2: an array now has its own attribute face, so a JS array's companion
// property map is held there — one tagged Item in an 8-byte buffer — instead of
// in a reserved tail slot inside the elements buffer. This shape marks that
// buffer. The companion stays a real object (a sparse array's companion is a
// SparseArrayMap with its own fields), so what moved inline is the POINTER.
extern TypeMap ArrayPropsShape;
extern TypeElmt EmptyElmt;
extern const Item ItemNull;
extern const Item ItemError;
extern const Item ItemEmptyString;
extern TypeInfo type_info[];

typedef struct Input {
    void* url;
    void* path;
    Pool* pool;                 // memory pool
    Arena* arena;               // arena allocator
    NamePool* name_pool;        // centralized name management
    TypeMap* shape_transition_root;
    int shape_transition_shapes;      // graph size, bounded by MAX_SHAPE_GRAPH
    // D3.4.3v3: one empty root per element tag and namespace, a pool-owned
    // open-addressing table keyed by the pooled tag-name pointer; element
    // nodes (roots included) count against their own budget
    struct TypeElmt** element_roots;
    int element_root_cap;
    int element_root_count;
    int element_transition_shapes;    // bounded by MAX_ELEMENT_SHAPE_GRAPH
    ArrayList* type_list;       // list of types
    Item root;
    Input* parent;              // parent Input for hierarchical ownership (nullable)
    char* xml_stylesheet_href;  // href from <?xml-stylesheet?> processing instruction (nullable)
    int doc_count;              // number of YAML documents (0 or 1 = single doc, >1 = multi-doc array)
    bool ui_mode;               // true = allocate DomElement/DomText during parsing (layout/render/view commands)
    bool source_positions;      // parse({sourcepos: true}): markup blocks carry the source lines they span
    void* mem_ctx;              // per-document MemContext sub-context (nullable; memory attribution)
    // A parser may legitimately produce ItemNull, so failures need a separate
    // status bit instead of being inferred from the parsed root value.
    bool parse_failed;
    char* parse_error_message;   // formatted parser diagnostic, owned by pool (nullable)
    // StringBuf* sb;

    // member functions
    static Input* create(Pool* pool, Url* abs_url = nullptr, Input* parent = nullptr);
    // A schema-backed input retains this explicit NamePool parent separately
    // from document-tree ownership. Missing fields remain in the Input-local
    // id-less child, while schema hits resolve through the existing hierarchy.
    static Input* create_with_name_parent(Pool* pool, Url* abs_url = nullptr,
                                           Input* parent = nullptr,
                                           NamePool* name_parent = nullptr);
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
// Static input sources select their explicit Pool/Arena provider at compile time.
#define list_push list_push_io
#endif
Map* map_pooled(Pool* pool);
Element* elmt_pooled(Pool* pool);

// Arena-based allocation (for MarkBuilder)
Array* array_arena(Arena* arena);
Map* map_arena(Arena* arena);
Element* elmt_arena(Arena* arena);
List* list_arena(Arena* arena);

typedef bool (*MapDataGrowFn)(Map** map_slot, int byte_cap, int64_t copy_bytes,
    String** keys, int key_count, Item* values, int value_count, void* context);
void map_put_with_data_growth(Map* mp, String* key, Item value, Input* input,
    MapDataGrowFn grow, void* grow_context);
void map_put(Map* mp, String* key, Item value, Input *input);
void map_put_heap(Map* mp, String* key, Item value, Input* input);
// bulk append for callers that have already proven every key is unique and
// absent from the target map. Values are JS `undefined` slots.
bool map_put_undefined_unique_absent_bulk_with_data_growth(Map* mp,
    String** keys, int count, Input* input, uint8_t shape_flags,
    MapDataGrowFn grow, void* grow_context);
bool map_put_undefined_unique_absent_bulk(Map* mp, String** keys, int count,
    Input* input, uint8_t shape_flags);
bool map_put_undefined_unique_absent_bulk_heap(Map* mp, String** keys, int count,
    Input* input, uint8_t shape_flags);
void elmt_put(Element* elmt, String* key, Item value, Pool* pool);

// D3.4.3v3: element types share through the Input's transition tree. The root
// for a tag and namespace (NULL: keep a private type), and the attribute add
// that follows or mints an edge from an element's tree type.
TypeElmt* elmt_tree_root(Input* input, String* tag_name, Target* ns);
void elmt_put_tree(Element* elmt, String* key, Item value, Input* input);

// A field an editor rebuild adds to a tree type: replay an existing field's
// identity (`like`), or add one under `key`, at `type_id`.
typedef struct TypeTreeStep {
    const ShapeEntry* like;
    String* key;
    TypeId type_id;
} TypeTreeStep;

// D3.4.3v3: a rebuilt type comes from the tree too. The root a container's
// rebuild starts from (NULL: the container stays private), and the node
// reached by adding `steps` from `start` (NULL: the tree declined).
TypeMap* type_tree_root_like(Input* input, Map* container);
TypeMap* type_tree_follow(Input* input, TypeMap* start, const TypeTreeStep* steps, int count);

// Borrowed scalar read: boxed int64/float/uint64 Items point into ArrayNum storage.
// Use only while the source ArrayNum is alive and not being mutated.
Item array_num_read_borrowed_item(ArrayNum* array, int64_t offset);
Item array_num_read_item(ArrayNum* array, int64_t offset);
double array_num_read_double(ArrayNum* arr, int64_t offset);

// Strict structural equality for non-observable no-op elision.
bool item_deep_equal(Item a, Item b);

#ifdef __cplusplus
}
#endif

#ifdef __cplusplus
extern "C++" {
Type* alloc_type(Pool* pool, TypeId type, size_t size);
Type* alloc_type_in(TypeAlloc alloc, TypeId type, size_t size);
Type* alloc_type_kind(Pool* pool, uint8_t kind, size_t size);
}
#else
Type* alloc_type(Pool* pool, TypeId type, size_t size);
Type* alloc_type_in(TypeAlloc alloc, TypeId type, size_t size);
Type* alloc_type_kind(Pool* pool, uint8_t kind, size_t size);
#endif

// T28-4: has any type binder or bound reference ever been allocated in this
// process? Every such node is created through alloc_type_kind, so while this
// is false no contract can reach one, and a walk that asks "does this contract
// use a binder?" is answered without walking. Monotonic: it never resets.
#ifdef __cplusplus
extern "C" {
#endif
bool lambda_binder_types_exist(void);
#ifdef __cplusplus
}
#endif
