
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
        Element* element;
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
