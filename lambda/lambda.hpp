
#pragma once
#include "lambda.h"

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
        // raw 64-bit value
        uint64_t item;

        // direct pointers to the container types
        Container* container;
        Range* range;
        List* list;
        Array* array;
        ArrayInt* array_int;      // Renamed from array_long
        ArrayInt64* array_int64;  // New: 64-bit integer arrays
        ArrayFloat* array_float;
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
    inline DateTime get_datetime() const { return *(DateTime*)this->datetime_ptr; }
    inline Decimal* get_decimal() const { return (Decimal*)this->decimal_ptr; }
    inline String* get_string() const { return (String*)this->string_ptr; }
    inline Symbol* get_symbol() const { return (Symbol*)this->symbol_ptr; }
    inline String* get_binary() const{ return (String*)this->binary_ptr; }

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
        const ArrayInt* array_int;      // Renamed from array_long
        const ArrayInt64* array_int64;  // New: 64-bit integer arrays
        const ArrayFloat* array_float;
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

extern Item ItemNull;
extern Item ItemError;

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

struct ArrayInt : Container {
    int64_t* items;  // int56 values (stored as int64)
    int64_t length;
    int64_t extra;  // count of extra items
    int64_t capacity;
};

struct ArrayInt64 : Container {
    int64_t* items;  // 64-bit integer items
    int64_t length;
    int64_t extra;  // count of extra items
    int64_t capacity;
};

struct ArrayFloat : Container {
    double* items;
    int64_t length;
    int64_t extra;  // count of extra items
    int64_t capacity;
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
