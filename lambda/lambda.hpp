
#pragma once
#include "lambda.h"
#include "lambda-path.h"

// ============================================================================
// Definitions moved from lambda.h to keep the JIT-embedded header slim.
// These are only needed by the C++ runtime, not by JIT-compiled code.
// ============================================================================

// TypeKind: sub-classification for LMD_TYPE_TYPE variants
// Type structs (TypeUnary, TypeBinary, TypePattern) all share type_id = LMD_TYPE_TYPE
// but are distinguished by their kind field.
enum TypeKind {
    TYPE_KIND_SIMPLE = 0,   // base Type (e.g., int, string, map, etc.)
    TYPE_KIND_UNARY,        // TypeUnary: occurrence operators (?, +, *, [n])
    TYPE_KIND_BINARY,       // TypeBinary: union, intersection, exclude
    TYPE_KIND_PATTERN,      // TypePattern: compiled regex pattern
    TYPE_KIND_CONSTRAINED,  // TypeConstrained: type with where constraint
};

// ============================================================================
// Target: Unified I/O target for input/output operations
// ============================================================================

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

// Target API (defined in target.cpp)
Target* item_to_target(uint64_t item, Url* cwd);
void* target_to_local_path(Target* target, Url* cwd);
const char* target_to_url_string(Target* target, void* out_buf);
bool target_is_local(Target* target);
bool target_is_remote(Target* target);
bool target_is_dir(Target* target);
bool target_exists(Target* target);
void target_free(Target* target);
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

typedef struct ConstItem ConstItem;

typedef struct Item {
    union {
        // packed values with type_id tagging
        struct {
            int64_t int_val: 56;  // signed for proper sign extension
            uint64_t _type_id: 8;
        };
        struct {
            uint64_t bool_val: 8;
            uint64_t _56: 56;
        };
        // uses the high byte to tag the item/pointer, defined for little-endian
        struct {
            uint64_t int64_ptr: 56;  // tagged pointer to long value
            uint64_t _8_l: 8;
        };
        struct {
            uint64_t double_ptr: 56;  // tagged pointer to double value
            uint64_t _8_d: 8;
        };
        struct {
            uint64_t decimal_ptr: 56;  // tagged pointer to decimal value
            uint64_t _8_c: 8;
        };
        struct {
            uint64_t string_ptr: 56;  // tagged pointer to String
            uint64_t _8_s: 8;
        };
        struct {
            uint64_t symbol_ptr: 56;  // tagged pointer to Symbol
            uint64_t _8_y: 8;
        };
        struct {
            uint64_t datetime_ptr: 56;  // tagged pointer to Datetime
            uint64_t _8_k: 8;
        };
        struct {
            uint64_t binary_ptr: 56;  // tagged pointer to Binary
            uint64_t _8_x: 8;
        };
        // sized numeric: value in [31:0], sub-type in [55:48], type_id in [63:56]
        struct {
            uint32_t num_value;         // [31:0]  — raw 32-bit value
            uint16_t _num_pad;          // [47:32] — unused padding
            uint8_t  num_type;          // [55:48] — NumSizedType
            uint8_t  _type_id_num;      // [63:56] — LMD_TYPE_NUM_SIZED
        };
        struct {
            uint64_t uint64_ptr: 56;    // tagged pointer to uint64 value
            uint64_t _8_u: 8;
        };
        // raw 64-bit value
        uint64_t item;

        // direct pointers to the container types
        Container* container;
        Range* range;
        List* list;
        Array* array;
        ArrayNum* array_num;
        ArrayNum* array_int;      // compat alias (elem_type == ELEM_INT)
        ArrayNum* array_int64;    // compat alias (elem_type == ELEM_INT64)
        ArrayNum* array_float;    // compat alias (elem_type == ELEM_FLOAT)
        Map* map;
        VMap* vmap;
        Element* element;
        Object* object;
        Type* type;
        Function* function;
        Path* path;
    };

    inline TypeId type_id() {
        if (this->_type_id) {
            return this->_type_id;
        }
        if (this->item) {
            return *((TypeId*)this->item);
        }
        return LMD_TYPE_NULL; // fallback for null item
    }

    inline ConstItem to_const() const;

    // get raw value out of an Item
    inline double get_double() const{ return *(double*)this->double_ptr; }
    inline int64_t get_int64() const { return *(int64_t*)this->int64_ptr; }
    inline uint64_t get_uint64() const { return *(uint64_t*)this->uint64_ptr; }
    inline DateTime get_datetime() const { return *(DateTime*)this->datetime_ptr; }
    inline Decimal* get_decimal() const { return (Decimal*)this->decimal_ptr; }
    inline String* get_string() const { return (String*)this->string_ptr; }
    inline Symbol* get_symbol() const { return (Symbol*)this->symbol_ptr; }
    inline String* get_binary() const{ return (String*)this->binary_ptr; }

    // sized numeric getters (NUM_SIZED)
    inline NumSizedType get_num_type() const { return this->num_type; }
    inline int8_t   get_i8()  const { return (int8_t)(num_value & 0xFF); }
    inline int16_t  get_i16() const { return (int16_t)(num_value & 0xFFFF); }
    inline int32_t  get_i32() const { return (int32_t)num_value; }
    inline uint8_t  get_u8()  const { return (uint8_t)(num_value & 0xFF); }
    inline uint16_t get_u16() const { return (uint16_t)(num_value & 0xFFFF); }
    inline uint32_t get_u32() const { return num_value; }
    inline float    get_f32() const { return bits_to_f32(num_value); }
    inline float    get_f16() const { return f16_bits_to_f32((uint16_t)(num_value & 0xFFFF)); }
    // get the numeric value as double (for arithmetic promotion)
    inline double get_num_sized_as_double() const {
        switch (num_type) {
            case NUM_INT8:    return (double)get_i8();
            case NUM_INT16:   return (double)get_i16();
            case NUM_INT32:   return (double)get_i32();
            case NUM_UINT8:   return (double)get_u8();
            case NUM_UINT16:  return (double)get_u16();
            case NUM_UINT32:  return (double)get_u32();
            case NUM_FLOAT16: return (double)get_f16();
            case NUM_FLOAT32: return (double)get_f32();
            default: return 0.0;
        }
    }
    // get the numeric value as int64 (for integer operations)
    inline int64_t get_num_sized_as_int64() const {
        switch (num_type) {
            case NUM_INT8:    return (int64_t)get_i8();
            case NUM_INT16:   return (int64_t)get_i16();
            case NUM_INT32:   return (int64_t)get_i32();
            case NUM_UINT8:   return (int64_t)get_u8();
            case NUM_UINT16:  return (int64_t)get_u16();
            case NUM_UINT32:  return (int64_t)get_u32();
            default: return 0;
        }
    }

    // get chars/len for string-like types (STRING, SYMBOL, BINARY)
    // Symbol has the same leading layout: len, then chars (with ns in between)
    inline const char* get_chars() const {
        if (this->_type_id == LMD_TYPE_STRING || this->_type_id == LMD_TYPE_BINARY) {
            return ((String*)this->string_ptr)->chars;
        }
        return ((Symbol*)this->symbol_ptr)->chars;
    }
    inline uint32_t get_len() const {
        if (this->_type_id == LMD_TYPE_STRING || this->_type_id == LMD_TYPE_BINARY) {
            return ((String*)this->string_ptr)->len;
        }
        return ((Symbol*)this->symbol_ptr)->len;
    }

    // get int56 value sign-extended to int64
    inline int64_t get_int56() const {
        uint64_t raw = item & 0x00FFFFFFFFFFFFFFULL;
        // sign extend from bit 55
        if (raw & 0x0080000000000000ULL) {
            return (int64_t)(raw | 0xFF00000000000000ULL);
        }
        return (int64_t)raw;
    }
} Item;

// const read-only item
// ConstItem, instead of const Item, to hide fields from Item
struct ConstItem {
    union {
        // raw 64-bit value
        const uint64_t item;

        // direct pointers to the container types
        const Container* container;
        const Range* range;
        const List* list;
        const Array* array;
        const ArrayNum* array_num;
        const ArrayNum* array_int;      // compat alias
        const ArrayNum* array_int64;    // compat alias
        const ArrayNum* array_float;    // compat alias
        const Map* map;
        const Element* element;
        const Object* object;
        const Type* type;
        const Function* function;
    };

    explicit ConstItem(): item(0) {}
    // ConstItem& operator=(ConstItem&&) = default;
    ConstItem& operator=(const ConstItem &) = default;

    inline TypeId type_id() const {
        return ((Item*)this)->type_id();
    }

    inline String* string() const {
        Item* itm = (Item*)this;
        return (itm->_type_id == LMD_TYPE_STRING) ? (String*)itm->string_ptr : nullptr;
    }
};

// define Item::to_const() after ConstItem is complete
inline ConstItem Item::to_const() const {
    return *(ConstItem*)this;
}

// get type_id from an Item
static inline TypeId get_type_id(Item value) { return value.type_id(); }

extern const Item ItemNull;
extern const Item ItemError;

// ============================================================================
// Error propagation guard macros (Phase 1 of error handling improvements)
// If any argument is an error Item, propagate it immediately.
// For Item-returning functions: returns the original error Item.
// For Bool-returning functions: returns BOOL_ERROR.
// ============================================================================
#define GUARD_ERROR1(a) \
    if (get_type_id(a) == LMD_TYPE_ERROR) return (a)
#define GUARD_ERROR2(a, b) \
    if (get_type_id(a) == LMD_TYPE_ERROR) return (a); \
    if (get_type_id(b) == LMD_TYPE_ERROR) return (b)
#define GUARD_ERROR3(a, b, c) \
    if (get_type_id(a) == LMD_TYPE_ERROR) return (a); \
    if (get_type_id(b) == LMD_TYPE_ERROR) return (b); \
    if (get_type_id(c) == LMD_TYPE_ERROR) return (c)

// RetItem-returning error guards: propagate error as RetItem
#define GUARD_ERROR_RI1(a) \
    if (get_type_id(a) == LMD_TYPE_ERROR) return item_to_ri(a)
#define GUARD_ERROR_RI2(a, b) \
    if (get_type_id(a) == LMD_TYPE_ERROR) return item_to_ri(a); \
    if (get_type_id(b) == LMD_TYPE_ERROR) return item_to_ri(b)

// Bool-returning function guards: propagate error as BOOL_ERROR
#define GUARD_BOOL_ERROR1(a) \
    if (get_type_id(a) == LMD_TYPE_ERROR) return BOOL_ERROR
#define GUARD_BOOL_ERROR2(a, b) \
    if (get_type_id(a) == LMD_TYPE_ERROR) return BOOL_ERROR; \
    if (get_type_id(b) == LMD_TYPE_ERROR) return BOOL_ERROR

// DateTime-returning function guards: propagate error as DATETIME_MAKE_ERROR()
#define GUARD_DATETIME_ERROR1(a) \
    if (get_type_id(a) == LMD_TYPE_ERROR) return DATETIME_MAKE_ERROR()
#define GUARD_DATETIME_ERROR2(a, b) \
    if (get_type_id(a) == LMD_TYPE_ERROR) return DATETIME_MAKE_ERROR(); \
    if (get_type_id(b) == LMD_TYPE_ERROR) return DATETIME_MAKE_ERROR()
#define GUARD_DATETIME_ERROR3(a, b, c) \
    if (get_type_id(a) == LMD_TYPE_ERROR) return DATETIME_MAKE_ERROR(); \
    if (get_type_id(b) == LMD_TYPE_ERROR) return DATETIME_MAKE_ERROR(); \
    if (get_type_id(c) == LMD_TYPE_ERROR) return DATETIME_MAKE_ERROR()

struct Range : Container {
    int64_t start;  // inclusive start
    int64_t end;    // inclusive end
    int64_t length;
};

struct List : Container {
    Item* items;
    int64_t length;
    int64_t extra;  // count of extra items stored at the end of the list
    int64_t capacity;

    ConstItem get(int index) const;
};

struct ArrayNum : Container {
    // Container::flags byte: upper 4 bits = elem_type, lower 4 bits = Container flags
    union {
        int64_t* items;        // for ELEM_INT, ELEM_INT64
        double* float_items;   // for ELEM_FLOAT
    };
    int64_t length;
    int64_t extra;  // count of extra items
    int64_t capacity;

    ArrayNumElemType get_elem_type() const { return (ArrayNumElemType)(flags & 0xF0); }
};

struct Map : Container {
    void* type;  // map type/shape
    void* data;  // packed data struct of the map
    int data_cap;  // capacity of the data struct

    ConstItem get(const Item key) const;
    ConstItem get(const char* key_str) const;
    bool has_field(const char* field_name) const;
};

struct Element : List {
    // attributes map
    void* type;  // attr type/shape
    void* data;  // packed data struct of the attrs
    int data_cap;  // capacity of the data struct
    // member functions
    bool has_attr(const char* attr_name);

    ConstItem get_attr(const Item attr_name) const;
    ConstItem get_attr(const char* attr_name) const;
};

// VMap: Virtual map with vtable dispatch
// Supports arbitrary key types and pluggable backends (HashMap, TreeMap, etc.)
// type(vmap) returns "map" — transparent to Lambda scripts
struct VMapVtable {
    Item    (*get)(void* data, Item key);                    // map[key]
    void    (*set)(void* data, Item key, Item value);        // in-place mutation (pn context)
    int64_t (*count)(void* data);                            // len(map)
    ArrayList* (*keys)(void* data);                          // item_keys() → ArrayList<String*>
    Item    (*key_at)(void* data, int64_t index);            // original key at insertion index
    Item    (*value_at)(void* data, int64_t index);          // value at insertion index
    void    (*destroy)(void* data);                          // free backing store
};

struct VMap : Container {
    void* data;            // opaque pointer to backing implementation (e.g. HashMapData*)
    VMapVtable* vtable;    // dispatch table
};

// Object: nominally-typed map with type name and methods
// Same memory layout as Map for field access compatibility
struct Object : Container {
    void* type;         // TypeObject* — shape + methods + type_name
    void* data;         // packed field data (same layout as Map)
    int data_cap;       // data buffer capacity

    ConstItem get(const Item key) const;
    ConstItem get(const char* key_str) const;
    bool has_field(const char* field_name) const;
};

// ============================================================================
// C++ versions of Item-using inline helpers (Item is fully defined here)
// C versions are in lambda.h (where Item = uint64_t)
// ============================================================================

// Container unboxing: Item → native pointer.
// Container Items store direct pointers (no type tag in the high bits),
// so just return the typed union field.

static inline Map*     it2map(Item item)   { return item.map; }
static inline List*    it2list(Item item)   { return item.list; }
static inline Element* it2elmt(Item item)   { return item.element; }
static inline Object*  it2obj(Item item)    { return item.object; }
static inline Array*   it2arr(Item item)    { return item.array; }
static inline Range*   it2range(Item item)  { return item.range; }
static inline Path*    it2path(Item item)   { return item.path; }
static inline void*    it2p(Item item)      { return (void*)item.container; }

static inline Item p2it(void* ptr) {
    if (!ptr) return ItemNull;
    return Item{.item = (uint64_t)(uintptr_t)ptr};
}

static inline Item err2it(LambdaError* err) {
    if (!err) return ItemNull;
    return Item{.item = ((uint64_t)LMD_TYPE_ERROR << 56) | (uint64_t)(uintptr_t)err};
}

static inline LambdaError* it2err(Item item) {
    if (item._type_id != LMD_TYPE_ERROR) return null;
    return (LambdaError*)(uintptr_t)(item.item & 0x00FFFFFFFFFFFFFFULL);
}

// RetItem — C++ version (Item is complete here)
typedef struct RetItem { Item value; LambdaError* err; } RetItem;

static inline RetItem ri_ok(Item value) {
    RetItem r; r.value = value; r.err = null; return r;
}
static inline RetItem ri_err(LambdaError* error) {
    RetItem r; r.value = ItemError; r.err = error; return r;
}

static inline RetItem item_to_ri(Item item) {
    RetItem r;
    r.value = item;
    r.err = (item._type_id == LMD_TYPE_ERROR) ? (LambdaError*)1 : nullptr;
    return r;
}

static inline Item ri_to_item(RetItem ri) {
    return ri.value;
}
