
#pragma once
#include "lambda.h"
#include "core/lambda-path.h"
#include "io/target.h"
#include "../lib/byte_storage.h"
#include "runtime/side_stack.h"

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
    TYPE_KIND_RANGE,        // TypeRange: value-level range membership contract
    // TypeParam keeps a compact carrier Type prefix plus its full source
    // contract. Mark it so identifier typing can safely recover that contract.
    TYPE_KIND_PARAM,
};

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
        Array* array;
        ArrayNum* array_num;
        ArrayNum* array_int;      // compat alias (elem_type == ELEM_INT)
        ArrayNum* array_int64;    // compat alias (elem_type == ELEM_INT64)
        ArrayNum* array_float;    // compat alias (elem_type == ELEM_FLOAT64)
        Map* map;
        VMap* vmap;
        Element* element;
        Object* object;
        Type* type;
        Function* function;
        Path* path;
    };

    inline TypeId type_id() const {
        // Inline double Items must be recognized before interpreting the word
        // as either a high-byte tag or a raw container header.
        if (this->item & ITEM_DBL_MASK) {
            // `inf` and `nan` are ONE value shared by `int` and `float` (formal
            // semantics 4), physically a double. This is the DECODER dispatch,
            // so it reports what the bits are; the C16 surface rule that
            // `type(nan)` is `int` lives in fn_type()/item_static_type_for_is().
            return LMD_TYPE_FLOAT;
        }
        // v5: a finite int is a 56-bit payload under the ordinary LMD_TYPE_INT
        // tag byte, so it needs no arm of its own -- the tag-byte path below
        // reports it. (v4 needed one: its rotation encoding put ints in the
        // `100` octant, outside the tag space entirely.)
        if (this->_type_id) {
            // RV4.1 tripwire: this is the choke point every accessor, formatter,
            // comparator and hash funnels through, so a pending return lane that
            // escaped its resolution point dies here rather than decoding as a
            // bogus TypeId. Debug builds only; in release the out-of-range tag
            // still trips per-tag table bounds.
            assert_item_not_pending(this->item);
            return this->_type_id;
        }
        // container types store TypeId at address pointed to by item
        if (this->item) {
            return *((TypeId*)this->item);
        }
        return LMD_TYPE_NULL;
    }

    inline ConstItem to_const() const;

    // get raw value out of an Item
    inline double get_double() const{
        if (this->item & ITEM_DBL_MASK) {
            double value;
            __builtin_memcpy(&value, &this->item, sizeof(value));
            return value;
        }
        if (this->_type_id == LMD_TYPE_FLOAT && this->double_ptr <= 1) {
            return this->double_ptr ? -0.0 : 0.0;
        }
        return *(double*)this->double_ptr;
    }
    inline int64_t get_int64() const {
        return *(int64_t*)this->int64_ptr;
    }
    inline uint64_t get_uint64() const { return *(uint64_t*)this->uint64_ptr; }
    inline DateTime* get_datetime_ptr() const { return (DateTime*)(uintptr_t)this->datetime_ptr; }
    inline DateTime get_datetime() const { return *(DateTime*)this->datetime_ptr; }
    inline Decimal* get_decimal() const { return (Decimal*)this->decimal_ptr; }
    inline Complex* get_complex() const { return (Complex*)(uintptr_t)this->item; }
    inline String* get_string() const { return (String*)this->string_ptr; }
    inline Symbol* get_symbol() const { return (Symbol*)this->symbol_ptr; }
    inline Binary* get_binary() const{ return (Binary*)this->binary_ptr; }
    inline String* get_safe_string() const { return type_id() == LMD_TYPE_STRING ? get_string() : nullptr; }
    inline Symbol* get_safe_symbol() const { return type_id() == LMD_TYPE_SYMBOL ? get_symbol() : nullptr; }
    inline Binary* get_safe_binary() const { return type_id() == LMD_TYPE_BINARY ? get_binary() : nullptr; }
    inline Array* get_safe_array() const { return type_id() == LMD_TYPE_ARRAY ? array : nullptr; }
    inline Map* get_safe_map() const { return type_id() == LMD_TYPE_MAP ? map : nullptr; }

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

    // get bytes/len for text and Binary values through their dedicated layouts
    inline const char* get_chars() const {
        if (this->_type_id == LMD_TYPE_BINARY) {
            return (const char*)binary_data(get_binary());
        }
        if (this->_type_id == LMD_TYPE_STRING) {
            return ((String*)this->string_ptr)->chars;
        }
        return ((Symbol*)this->symbol_ptr)->chars;
    }
    inline uint32_t get_len() const {
        if (this->_type_id == LMD_TYPE_BINARY) {
            return binary_length(get_binary());
        }
        if (this->_type_id == LMD_TYPE_STRING) {
            return ((String*)this->string_ptr)->len;
        }
        return ((Symbol*)this->symbol_ptr)->len;
    }

} Item;

static_assert(sizeof(Item) == sizeof(uint64_t), "C++ Item must remain one word");

// const read-only item
// ConstItem, instead of const Item, to hide fields from Item
struct ConstItem {
    union {
        // raw 64-bit value
        const uint64_t item;

        // direct pointers to the container types
        const Container* container;
        const Range* range;
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
    ConstItem(const ConstItem &) = default;
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

// An in-band exception is identified by its ordinary Item tag; no side channel
// is needed by callers that already hold the helper's returned value.
static inline bool item_is_error(Item value) {
    return get_type_id(value) == LMD_TYPE_ERROR;
}

static inline bool lambda_item_uses_scalar_home(Item item) {
    // C16: `int` is NOT here. An int Item carries its own rotated IEEE bits at
    // every magnitude, so it never points at frame-scoped storage. The scalar
    // home protocol covers exactly int64, uint64 and cell-backed (tiny) floats.
    TypeId type = get_type_id(item);
    if (type == LMD_TYPE_INT64 || type == LMD_TYPE_UINT64) return true;
    return type == LMD_TYPE_FLOAT &&
        !(item.item & ITEM_DBL_MASK) && item.item != ITEM_FLOAT_P0 &&
        item.item != ITEM_FLOAT_N0;
}

// The two canonical ways to read an `int` Item. Every consumer goes through one
// of them, so the boxed representation is changeable in one place: `_value` is
// the C16 numeric view (int arithmetic is total in binary64, so this one stays
// exact at every magnitude), `_to_i64` is for index/count/i64-storage consumers,
// whose values are in-band by construction. Poison payloads come back as their
// sentinel magnitudes, so callers that care must classify with
// LAMBDA_INT_VALUE_IS_POISON first.
static inline double lambda_int_item_value(Item item) {
    return lambda_int_unbox_double(item.item);
}

static inline int64_t lambda_int_item_to_i64(Item item) {
    // Shared IEEE poison must re-enter the i64 lane as its sentinel; casting
    // inf/nan would lose the v5 closure value before the caller can classify it.
    return lambda_int_item_to_lane(item.item);
}

// Compatibility aggregate: the native root/GC helpers are provided by rt.
// Keep the include after Item so the Item-specialized root handles are complete.
#include "runtime/lambda-root-frame.hpp"

static inline Item lambda_float_ptr_to_item(const double* double_ptr) {
    if (!double_ptr) return {.item = ITEM_NULL};
    double value = *double_ptr;
    uint64_t bits;
    __builtin_memcpy(&bits, &value, sizeof(bits));
    if (value == 0.0) {
        return {.item = ITEM_FLOAT_P0 | ((bits >> 63) ? UINT64_C(1) : UINT64_C(0))};
    }
    if (bits & ITEM_DBL_MASK) {
        return {.item = bits};
    }
    return {.item = d2it(double_ptr)};
}

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
    bool is_char;    // true when bounds are Unicode codepoints materializing as strings
};

// D2.6.6v2: containers form ONE single-inheritance chain — Map -> Array ->
// Element. Map is the base because the attribute face is what every container
// shares, and a property bag is what a JS program calls an object (D2.6.9v3).
// Keeping it first puts `type`/`data`/`data_cap` at ONE offset in every
// container, so a cast to any ancestor is valid by construction and the
// kind-aware offset accessors the previous layout needed all retire.
struct Map : Container {
    void* type;  // map type/shape
    void* data;  // packed data struct of the map
    int data_cap;  // capacity of the data struct

    ConstItem get(const Item key) const;
    ConstItem get(const char* key_str) const;
    bool has_field(const char* field_name) const;
};

// `Array` is a typedef of List (lambda.h). Extending Map costs every array a
// 24-byte attribute prefix — the accepted price of the reconciliation — and
// buys arrays their own property face, which is what retires the reserved-tail
// JS-props companion that used to live in `extra`.
struct List : Map {
    Item* items;
    int64_t length;
    int64_t extra;  // count of reserved tail items (wide scalars)
    int64_t capacity;

    ConstItem get(int index) const;
};

#define ARRAY_NATIVE_LANE_KIND_MASK 0x07u
#define ARRAY_NATIVE_LANE_NULLABLE  0x08u

static inline Item lambda_num_sized_lane_to_item(int64_t lane, NumSizedType num_type) {
    switch (num_type) {
    case NUM_INT8: return {.item = i8_to_item((int8_t)lane)};
    case NUM_INT16: return {.item = i16_to_item((int16_t)lane)};
    case NUM_INT32: return {.item = i32_to_item((int32_t)lane)};
    case NUM_UINT8: return {.item = u8_to_item((uint8_t)lane)};
    case NUM_UINT16: return {.item = u16_to_item((uint16_t)lane)};
    case NUM_UINT32: return {.item = u32_to_item((uint32_t)lane)};
    default: return ItemError;
    }
}

static inline bool lambda_pointer_lane_accepts_item(TypeId base_type, TypeId value_type) {
    if (base_type == LMD_TYPE_ARRAY) {
        return value_type == LMD_TYPE_ARRAY || value_type == LMD_TYPE_ARRAY_NUM;
    }
    if (base_type == LMD_TYPE_MAP) {
        return value_type == LMD_TYPE_MAP || value_type == LMD_TYPE_VMAP;
    }
    return value_type == base_type;
}

static inline Item lambda_pointer_lane_to_item(uint64_t word, TypeId base_type) {
    if (word == 0) return ItemNull;
    void* ptr = (void*)(uintptr_t)word;
    switch (base_type) {
    case LMD_TYPE_DECIMAL: return {.item = c2it((Decimal*)ptr)};
    case LMD_TYPE_DTIME: return {.item = k2it((DateTime*)ptr)};
    case LMD_TYPE_SYMBOL: return {.item = y2it((Symbol*)ptr)};
    case LMD_TYPE_STRING: return {.item = s2it((String*)ptr)};
    case LMD_TYPE_BINARY: return {.item = x2it((Binary*)ptr)};
    case LMD_TYPE_COMPLEX: return {.item = word};
    default:
        // Container, function, path, and Type Items are already raw pointers.
        return {.item = word};
    }
}

static inline bool lambda_pointer_lane_store(void* storage, TypeId base_type, bool nullable,
        Item value) {
    if (!storage || !lambda_type_id_has_pointer_lane(base_type)) return false;
    TypeId value_type = get_type_id(value);
    if (value_type == LMD_TYPE_NULL) {
        if (!nullable) return false;
        *(uint64_t*)storage = 0;
        return true;
    }
    if (!lambda_pointer_lane_accepts_item(base_type, value_type)) return false;

    uint64_t word = 0;
    switch (base_type) {
    case LMD_TYPE_DECIMAL: word = (uint64_t)(uintptr_t)value.get_decimal(); break;
    case LMD_TYPE_DTIME: word = (uint64_t)(uintptr_t)value.get_datetime_ptr(); break;
    case LMD_TYPE_SYMBOL: word = (uint64_t)(uintptr_t)value.get_safe_symbol(); break;
    case LMD_TYPE_STRING: word = (uint64_t)(uintptr_t)value.get_safe_string(); break;
    case LMD_TYPE_BINARY: word = (uint64_t)(uintptr_t)value.get_safe_binary(); break;
    case LMD_TYPE_COMPLEX: word = (uint64_t)(uintptr_t)value.get_complex(); break;
    default: word = value.item; break;
    }
    *(uint64_t*)storage = word;
    return true;
}

static inline LaneStorageKind array_native_lane_kind(const Array* array) {
    return array ? (LaneStorageKind)(array->map_kind & ARRAY_NATIVE_LANE_KIND_MASK) :
        LANE_STORAGE_INVALID;
}

static inline bool array_native_lane_nullable(const Array* array) {
    return array && (array->map_kind & ARRAY_NATIVE_LANE_NULLABLE) != 0;
}

static inline NumSizedType array_native_lane_num_sized_type(const Array* array) {
    return array ? (NumSizedType)array->reserved_state : NUM_SIZED_COUNT;
}

static inline TypeId array_native_lane_pointer_type(const Array* array) {
    return array ? (TypeId)array->reserved_state : LMD_TYPE_NULL;
}

static inline void array_native_lane_configure(Array* array, const LaneStorageDesc* desc) {
    if (!array || !desc) return;
    // Generic Arrays otherwise do not consume map_kind. Keep the compact lane
    // contract here so List and DOM/Element layouts remain unchanged.
    array->is_native_lane_array = 1;
    array->map_kind = (uint8_t)desc->kind |
        (desc->nullable ? ARRAY_NATIVE_LANE_NULLABLE : 0);
    if (desc->kind == LANE_STORAGE_SIZED_I64 && desc->base_contract) {
        array->reserved_state = desc->base_contract->kind;
    } else if (desc->kind == LANE_STORAGE_POINTER && desc->base_contract) {
        array->reserved_state = desc->base_contract->type_id;
    } else {
        array->reserved_state = 0;
    }
}

static inline bool array_has_native_lane(const Array* array) {
    return array && array->is_native_lane_array &&
        array_native_lane_kind(array) != LANE_STORAGE_INVALID;
}

static inline bool array_native_lane_matches_desc(const Array* array,
        const LaneStorageDesc* desc) {
    if (!array_has_native_lane(array) || !desc ||
            array_native_lane_kind(array) != desc->kind ||
            array_native_lane_nullable(array) != (desc->nullable != 0)) {
        return false;
    }
    if (desc->kind == LANE_STORAGE_SIZED_I64) {
        return desc->base_contract && array_native_lane_num_sized_type(array) ==
            (NumSizedType)desc->base_contract->kind;
    }
    if (desc->kind == LANE_STORAGE_POINTER) {
        return desc->base_contract && array_native_lane_pointer_type(array) ==
            desc->base_contract->type_id;
    }
    return true;
}

static inline Item array_native_lane_read(const Array* array, int64_t index) {
    if (!array_has_native_lane(array) || index < 0 || index >= array->length) return ItemNull;
    uint64_t word = array->items[index].item;
    switch (array_native_lane_kind(array)) {
    case LANE_STORAGE_INT:
        return {.item = lambda_int_box_lane((int64_t)word)};
    case LANE_STORAGE_BOOL:
        if (array_native_lane_nullable(array) && word == 2) return ItemNull;
        return {.item = b2it(word ? BOOL_TRUE : BOOL_FALSE)};
    case LANE_STORAGE_FLOAT64:
        if (array_native_lane_nullable(array) && lambda_float_lane_is_null(word)) {
            return ItemNull;
        }
        // The lane word itself is stable array-owned double storage. Use the
        // existing pointer boxer here because lambda.hpp is also compiled by
        // input-only targets that do not link the runtime push_d helper.
        return lambda_float_ptr_to_item((const double*)&array->items[index]);
    case LANE_STORAGE_ITEM:
        return array->items[index];
    case LANE_STORAGE_SIZED_I64: {
        int64_t lane = (int64_t)word;
        if (array_native_lane_nullable(array) && lane == SIZED_LANE_NULL) return ItemNull;
        NumSizedType num_type = array_native_lane_num_sized_type(array);
        return lambda_num_sized_is_integer(num_type)
            ? lambda_num_sized_lane_to_item(lane, num_type) : ItemError;
    }
    case LANE_STORAGE_POINTER:
        return lambda_pointer_lane_to_item(word, array_native_lane_pointer_type(array));
    default:
        return ItemNull;
    }
}

static inline bool array_native_lane_store(Array* array, int64_t index, Item value) {
    if (!array_has_native_lane(array) || index < 0 || index >= array->capacity) return false;
    TypeId value_type = get_type_id(value);
    switch (array_native_lane_kind(array)) {
    case LANE_STORAGE_INT:
        if (value.item == ITEM_NULL) {
            if (!array_native_lane_nullable(array)) return false;
            array->items[index] = {.item = (uint64_t)INT_LANE_NULL};
            return true;
        }
        // int poison values are stored as Float Items but belong to IntLane.
        if (value_type != LMD_TYPE_INT && !lambda_item_is_merged_poison(value.item)) return false;
        array->items[index] = {.item = (uint64_t)lambda_int_item_to_lane(value.item)};
        return true;
    case LANE_STORAGE_BOOL:
        if (value.item == ITEM_NULL) {
            if (!array_native_lane_nullable(array)) return false;
            array->items[index] = {.item = 2};
            return true;
        }
        if (value_type != LMD_TYPE_BOOL) return false;
        array->items[index] = {.item = value.bool_val == BOOL_TRUE ? 1u : 0u};
        return true;
    case LANE_STORAGE_FLOAT64:
        if (value.item == ITEM_NULL) {
            if (!array_native_lane_nullable(array)) return false;
            array->items[index] = {.item = FLOAT_LANE_NULL_BITS};
            return true;
        }
        if (value_type != LMD_TYPE_FLOAT) return false;
        array->items[index] = {.item = lambda_float_lane_from_double(value.get_double())};
        return true;
    case LANE_STORAGE_ITEM:
        array->items[index] = value;
        return true;
    case LANE_STORAGE_SIZED_I64:
        if (value.item == ITEM_NULL) {
            if (!array_native_lane_nullable(array)) return false;
            array->items[index] = {.item = (uint64_t)SIZED_LANE_NULL};
            return true;
        }
        if (value_type != LMD_TYPE_NUM_SIZED ||
                value.get_num_type() != array_native_lane_num_sized_type(array)) return false;
        array->items[index] = {.item = (uint64_t)lambda_num_sized_item_to_lane(value.item)};
        return true;
    case LANE_STORAGE_POINTER:
        return lambda_pointer_lane_store(&array->items[index],
            array_native_lane_pointer_type(array), array_native_lane_nullable(array), value);
    default:
        return false;
    }
}

struct ArrayNum : Map {
    // Container::map_kind byte holds the elem_type for ArrayNum.
    // Container::array_flags stores layout flags (is_ndim/is_view/is_pinned).
    union {
        int64_t* items;        // for ELEM_INT, ELEM_INT64
        double* float_items;   // for ELEM_FLOAT64
        void* data;            // for compact types (ELEM_INT8, ELEM_UINT8, etc.)
    };
    int64_t length;
    int64_t extra;  // for is_ndim/is_view: ArrayNumShape*; else count of extra elements
    int64_t capacity;

    ArrayNumElemType get_elem_type() const { return (ArrayNumElemType)map_kind; }
    void set_elem_type(ArrayNumElemType e) { map_kind = (uint8_t)e; }
};

// Tune5 P5 gate: a List and ArrayNum share the same managed header and tail
// offsets, but their element buffers still have different semantic contracts.
// Retagging is legal only after this physical proof is true on every build.
// D2.6.6v2: the single chain Map -> List/Array -> Element. These pin the one
// property the whole design rests on: the attribute face sits at the SAME
// offset in every container, so an ancestor cast is always valid.
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Winvalid-offsetof"
static_assert(offsetof(Container, type_id) == LAMBDA_GC_OFF_CONTAINER_TYPE_ID &&
              offsetof(Container, flags) == LAMBDA_GC_OFF_CONTAINER_FLAGS &&
              offsetof(Container, array_flags) == LAMBDA_GC_OFF_CONTAINER_ARRAY_FLAGS &&
              offsetof(Container, map_kind) == LAMBDA_GC_OFF_CONTAINER_MAP_KIND,
              "Container must match the GC ABI");
static_assert(offsetof(Map, type) == LAMBDA_GC_OFF_MAP_TYPE &&
              offsetof(Map, data) == LAMBDA_GC_OFF_MAP_DATA &&
              offsetof(Map, data_cap) == LAMBDA_GC_OFF_MAP_DATA_CAP,
              "Map must match the GC ABI");
static_assert(offsetof(List, type) == LAMBDA_GC_OFF_MAP_TYPE &&
              offsetof(List, data) == LAMBDA_GC_OFF_MAP_DATA &&
              offsetof(List, data_cap) == LAMBDA_GC_OFF_MAP_DATA_CAP,
              "List must share Map's GC ABI face");
static_assert(offsetof(List, items) == LAMBDA_GC_OFF_LIST_ITEMS &&
              offsetof(List, length) == LAMBDA_GC_OFF_LIST_LENGTH &&
              offsetof(List, extra) == LAMBDA_GC_OFF_LIST_EXTRA &&
              offsetof(List, capacity) == LAMBDA_GC_OFF_LIST_CAPACITY,
              "List must match the GC ABI content face");
static_assert(offsetof(ArrayNum, items) == LAMBDA_GC_OFF_LIST_ITEMS &&
              offsetof(ArrayNum, length) == LAMBDA_GC_OFF_LIST_LENGTH &&
              offsetof(ArrayNum, extra) == LAMBDA_GC_OFF_LIST_EXTRA &&
              offsetof(ArrayNum, capacity) == LAMBDA_GC_OFF_LIST_CAPACITY,
              "ArrayNum must share the GC ABI content face");
#pragma clang diagnostic pop

static_assert(sizeof(List) == sizeof(ArrayNum),
              "JS numeric promotion requires identical container sizes");

static inline bool array_num_init_external_view(ArrayNum* view, ArrayNumShape* shape,
        Container* base, void* data_base, ArrayNumElemType elem_type,
        int64_t byte_offset, int64_t length, bool mutable_view) {
    if (!view || !shape || byte_offset < 0 || length < 0) return false;
    if (!data_base && (byte_offset != 0 || length != 0)) return false;
    uint8_t elem_size = ELEM_TYPE_SIZE[elem_type >> 4];
    if (!elem_size || (byte_offset % elem_size) != 0) return false;

    view->type_id = LMD_TYPE_ARRAY_NUM;
    view->array_flags = 0;
    view->is_ndim = 1;
    view->is_view = 1;
    view->is_mutable_view = mutable_view ? 1 : 0;
    view->set_elem_type(elem_type);
    view->data = data_base ? (void*)((uint8_t*)data_base + byte_offset) : NULL;
    view->length = length;
    view->capacity = length;

    shape->ndim = 1;
    shape->is_c_contig = 1;
    shape->is_f_contig = 1;
    // Borrowed external views are valid only while gc_base keeps a stable,
    // non-moving data pointer alive; replaceable storage requires a handle.
    shape->backing_kind = ARRAY_NUM_BACKING_EXTERNAL_BORROWED;
    shape->offset = byte_offset / elem_size;
    shape->base = (void*)base;
    array_num_shape_dims(shape)[0] = length;
    array_num_shape_strides(shape)[0] = 1;
    view->extra = (int64_t)(uintptr_t)shape;
    return true;
}

static inline bool array_num_init_storage_view(ArrayNum* view, ArrayNumShape* shape,
        ByteStorage* storage, ArrayNumElemType elem_type,
        int64_t byte_offset, int64_t length, bool mutable_view) {
    if (!storage || byte_offset < 0 || length < 0 ||
        (mutable_view && (storage->flags & BYTE_STORAGE_FLAG_READ_ONLY))) {
        return false;
    }
    uint8_t elem_size = ELEM_TYPE_SIZE[elem_type >> 4];
    size_t offset = (size_t)byte_offset;
    if (!elem_size || (offset % elem_size) != 0 || offset > storage->capacity ||
        (size_t)length > (storage->capacity - offset) / elem_size) {
        return false;
    }
    ByteStorage* retained = byte_storage_retain(storage);
    if (!retained) return false;
    if (!array_num_init_external_view(view, shape, NULL, storage->data,
            elem_type, byte_offset, length, mutable_view)) {
        byte_storage_release(retained);
        return false;
    }
    // Retained storage, unlike a borrowed external pointer, remains stable even
    // when the FFI/mmap owner that supplied it is otherwise unreachable.
    shape->backing_kind = ARRAY_NUM_BACKING_BYTE_STORAGE;
    shape->backing = retained;
    return true;
}


// Constructor shapes reserve at most 16 fixed-width slots before executing the
// body. The named mask bytes preserve the public Map layout and keep the state
// per instance rather than on TypeMap.
static inline uint16_t map_ctor_reserved_mask(const Map* map) {
    if (!map || !map->has_ctor_reserved) return 0;
    return (uint16_t)map->ctor_reserved_mask_lo |
        (uint16_t)((uint16_t)map->ctor_reserved_mask_hi << 8);
}

static inline void map_ctor_set_reserved_mask(Map* map, uint16_t mask) {
    if (!map) return;
    map->ctor_reserved_mask_lo = (uint8_t)(mask & 0xffu);
    map->ctor_reserved_mask_hi = (uint8_t)(mask >> 8);
    map->has_ctor_reserved = mask != 0;
}

static inline bool map_ctor_offset_is_reserved(const Map* map,
                                                int64_t byte_offset) {
    // Ordinary maps dominate property traffic, so reject them before doing
    // offset arithmetic for the constructor-only reservation mechanism.
    if (!map || !map->has_ctor_reserved || byte_offset < 0 ||
            (byte_offset % (int64_t)sizeof(void*)) != 0) return false;
    int64_t slot = byte_offset / (int64_t)sizeof(void*);
    return slot < 16 && (map_ctor_reserved_mask(map) & (uint16_t)(1u << slot));
}

static inline void map_ctor_initialize_offset(Map* map, int64_t byte_offset) {
    if (!map_ctor_offset_is_reserved(map, byte_offset)) return;
    int64_t slot = byte_offset / (int64_t)sizeof(void*);
    map_ctor_set_reserved_mask(map,
        (uint16_t)(map_ctor_reserved_mask(map) & (uint16_t)~(1u << slot)));
}

struct SparseArrayMap : Map {
    struct hashmap* sparse_indices;  // numeric sparse array data entries
    int64_t sparse_version;          // increments on numeric sparse mutations
};

// D2.6.6v2: Element adds NO fields — its attribute face is Map's, inherited
// through List. `Object` is a typedef of Element (lambda.h).
struct Element : List {
    // member functions
    bool has_attr(const char* attr_name);

    ConstItem get_attr(const Item attr_name) const;
    ConstItem get_attr(const char* attr_name) const;
};

// D2.6.6v2: Element inherits BOTH faces and adds nothing of its own.
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Winvalid-offsetof"
static_assert(offsetof(Element, type) == offsetof(Map, type) &&
              offsetof(Element, data) == offsetof(Map, data) &&
              offsetof(Element, data_cap) == offsetof(Map, data_cap),
              "Element must share Map's attribute face");
static_assert(offsetof(Element, items) == offsetof(List, items) &&
              offsetof(Element, length) == offsetof(List, length) &&
              offsetof(Element, extra) == offsetof(List, extra) &&
              offsetof(Element, capacity) == offsetof(List, capacity),
              "Element must share List's content face");
static_assert(sizeof(Element) == sizeof(List), "Element adds no fields");
#pragma clang diagnostic pop

// VMap: Virtual map with vtable dispatch
// Supports arbitrary key types and pluggable backends (HashMap, TreeMap, etc.)
// type(vmap) returns "map" — transparent to Lambda scripts
struct gc_heap;
struct VMapVtable {
    Item    (*get)(void* data, Item key);                    // map[key]
    void    (*set)(void* data, Item key, Item value);        // in-place mutation (pn context)
    int64_t (*count)(void* data);                            // len(map)
    SymbolKeyList* (*keys)(void* data);                      // item_keys() → SymbolKeyList
    Item    (*key_at)(void* data, int64_t index);            // original key at insertion index
    Item    (*value_at)(void* data, int64_t index);          // value at insertion index
    void    (*destroy)(void* data);                          // free backing store
    void    (*trace)(void* data, gc_heap* gc);               // precise Item-edge tracing
};

struct VMap : Container {
    void* data;            // opaque pointer to backing implementation (e.g. HashMapData*)
    VMapVtable* vtable;    // dispatch table
    const void* host_type;  // optional branded native host type; NULL for ordinary VMaps
    void* host_data;        // optional native payload for host-object adapters
};

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Winvalid-offsetof"
static_assert(offsetof(VMap, data) == LAMBDA_GC_OFF_VMAP_DATA &&
              offsetof(VMap, vtable) == LAMBDA_GC_OFF_VMAP_VTABLE,
              "VMap must match the GC ABI");
static_assert(offsetof(VMapVtable, trace) == LAMBDA_GC_OFF_VMAP_VTABLE_TRACE,
              "VMap trace hook must match the GC ABI");
#pragma clang diagnostic pop

// Object: nominally-typed map with type name and methods
// Same memory layout as Map for field access compatibility

// S2.1.3: the one content-face accessor. An object shares Element's layout, so
// both kinds ARE Lists and the content face is the value itself. Kept as a
// named accessor so content-face sites read the same way and the kind check
// lives in one place. Returns NULL for anything without a content face.
static inline List* lambda_content_list(TypeId type_id, const void* container) {
    if (!container || !is_element_family_type_id(type_id)) return nullptr;
    return (List*)(Element*)(void*)container;
}

// Content item count for either kind; 0 when absent.
static inline int64_t lambda_content_count(TypeId type_id, const void* container) {
    List* content = lambda_content_list(type_id, container);
    return content ? content->length : 0;
}

// TypeMap is defined in lambda-data.hpp, which some translation units include
// after this header; only the pointer is needed here.
struct TypeMap;

// D2.6.6: the packed attribute shape and buffer sit at DIFFERENT offsets in a
// map (right after the header) than in an element or object (after the list
// fields). These two accessors are the only correct way to reach the attribute
// face of a value whose kind is not statically fixed — reading `.map->type` on
// an object silently returns its `items` pointer.
// Which kinds answer with an attribute face. Arrays inherit the face from Map
// too, but theirs is the JS-props companion (`ArrayPropsShape`), not Lambda
// attributes, so they stay out of this predicate.
static inline bool lambda_type_id_has_attr_face(TypeId type_id) {
    return is_element_family_type_id(type_id) || type_id == LMD_TYPE_MAP;
}

// D2.6.6v2: every container extends Map, so the shape and its buffer sit at ONE
// offset and a single cast to the shared base reaches them. The kind test that
// remains selects WHICH kinds have attributes, not where they live.
static inline struct TypeMap* lambda_attr_shape(TypeId type_id, const void* container) {
    if (!container || !lambda_type_id_has_attr_face(type_id)) return nullptr;
    return (struct TypeMap*)((const Map*)container)->type;
}

static inline void* lambda_attr_data(TypeId type_id, const void* container) {
    if (!container || !lambda_type_id_has_attr_face(type_id)) return nullptr;
    return ((const Map*)container)->data;
}

// ============================================================================
// C++ versions of Item-using inline helpers (Item is fully defined here)
// C versions are in lambda.h (where Item = uint64_t)
// ============================================================================

// Container unboxing: Item → native pointer.
// Container Items store direct pointers (no type tag in the high bits),
// so just return the typed union field.

static inline Map*     it2map(Item item)   { return item.map; }
static inline List*    it2list(Item item)   { return item.array; }
static inline Element* it2elmt(Item item)   { return item.element; }
static inline Object*  it2obj(Item item)    { return item.object; }
static inline Array*   it2arr(Item item)    { return item.array; }
static inline Range*   it2range(Item item)  { return item.range; }
static inline Path*    it2path(Item item)   { return item.path; }
static inline void*    it2p(Item item)      { return (void*)item.container; }

static inline Item p2it(void* ptr) {
    if (!ptr) return Item{.item = ITEM_NULL};
    assert_raw_item_pointer(ptr);
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
    if (item._type_id == LMD_TYPE_ERROR) {
        LambdaError* err = it2err(item);
        r.err = err ? err : (LambdaError*)1;
    } else {
        r.err = nullptr;
    }
    return r;
}

static inline Item ri_to_item(RetItem ri) {
    return ri.value;
}
