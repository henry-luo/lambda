#include "ast.hpp"
#include "lambda-decimal.hpp"
#include "../lib/log.h"
#include "../lib/mempool.h"
#include "../lib/memtrack.h"
#include "../lib/checked_math.hpp"
#include "../lib/arena.h"  // for arena_owns() and arena_realloc()
#include "js/js_exec_profile_weak.h"

#ifndef LAMBDA_STATIC
// ui_mode helper: copy GC-heap string into result arena as fat DomText node
// (defined in lambda-data-runtime.cpp which has access to DOM headers)
Item ui_copy_string_to_arena(Arena* arena, Item str_item);

// ui_mode helper: merge two strings into a new fat DomText on the result arena
// (defined in lambda-data-runtime.cpp which has access to DOM headers)
Item ui_merge_strings_to_arena(Arena* arena, String* prev, String* next);
#endif

// data zone allocation helper (defined in lambda-mem.cpp)
// weak fallback uses malloc — overridden by real GC implementation when linked
extern "C" {
    __attribute__((weak))
    void* heap_data_alloc(size_t size) {
        return raw_malloc(size);  // RAWALLOC_OK: GC heap allocation — managed by garbage collector, not memtrack
    }

    __attribute__((weak))
    void heap_register_gc_root(uint64_t* slot) {
        (void)slot;
    }

    __attribute__((weak))
    void heap_unregister_gc_root(uint64_t* slot) {
        (void)slot;
    }
}

Type TYPE_NULL = {.type_id = LMD_TYPE_NULL};
Type TYPE_UNDEFINED = {.type_id = LMD_TYPE_UNDEFINED};  // JavaScript undefined
Type TYPE_BOOL = {.type_id = LMD_TYPE_BOOL};
Type TYPE_INT = {.type_id = LMD_TYPE_INT};
Type TYPE_INT64 = {.type_id = LMD_TYPE_INT64};
Type TYPE_FLOAT = {.type_id = LMD_TYPE_FLOAT};
Type TYPE_FLOAT64 = {.type_id = LMD_TYPE_FLOAT};
Type TYPE_DECIMAL = {.type_id = LMD_TYPE_DECIMAL};
Type TYPE_INTEGER = {.type_id = LMD_TYPE_TYPE};
Type TYPE_NUMBER = {.type_id = LMD_TYPE_TYPE};
Type TYPE_STRING = {.type_id = LMD_TYPE_STRING};
Type TYPE_BINARY = {.type_id = LMD_TYPE_BINARY};
Type TYPE_SYMBOL = {.type_id = LMD_TYPE_SYMBOL};
Type TYPE_PATH = {.type_id = LMD_TYPE_PATH};
Type TYPE_NUM_SIZED = {.type_id = LMD_TYPE_NUM_SIZED};
Type TYPE_UINT64 = {.type_id = LMD_TYPE_UINT64};
// sub-type Type objects for sized numerics (used by LIT_TYPE_I8..LIT_TYPE_F32)
// kind field stores the NumSizedType discriminator
Type TYPE_I8  = {.type_id = LMD_TYPE_NUM_SIZED, .kind = NUM_INT8};
Type TYPE_I16 = {.type_id = LMD_TYPE_NUM_SIZED, .kind = NUM_INT16};
Type TYPE_I32 = {.type_id = LMD_TYPE_NUM_SIZED, .kind = NUM_INT32};
Type TYPE_U8  = {.type_id = LMD_TYPE_NUM_SIZED, .kind = NUM_UINT8};
Type TYPE_U16 = {.type_id = LMD_TYPE_NUM_SIZED, .kind = NUM_UINT16};
Type TYPE_U32 = {.type_id = LMD_TYPE_NUM_SIZED, .kind = NUM_UINT32};
Type TYPE_F16 = {.type_id = LMD_TYPE_NUM_SIZED, .kind = NUM_FLOAT16};
Type TYPE_F32 = {.type_id = LMD_TYPE_NUM_SIZED, .kind = NUM_FLOAT32};
Type TYPE_F64 = {.type_id = LMD_TYPE_FLOAT};
Type TYPE_DTIME = {.type_id = LMD_TYPE_DTIME};
Type TYPE_DATE = {.type_id = LMD_TYPE_DTIME};   // sub-type: date-only datetime
Type TYPE_TIME = {.type_id = LMD_TYPE_DTIME};   // sub-type: time-only datetime
Type TYPE_LIST = {.type_id = LMD_TYPE_ARRAY};
Type TYPE_RANGE = {.type_id = LMD_TYPE_RANGE};
TypeArray TYPE_ARRAY;
Type TYPE_MAP = {.type_id = LMD_TYPE_MAP};
Type TYPE_ELMT = {.type_id = LMD_TYPE_ELEMENT};
Type TYPE_OBJECT = {.type_id = LMD_TYPE_OBJECT};
Type TYPE_TYPE = {.type_id = LMD_TYPE_TYPE};
Type TYPE_FUNC = {.type_id = LMD_TYPE_FUNC};
Type TYPE_ANY = {.type_id = LMD_TYPE_ANY};
Type TYPE_ERROR = {.type_id = LMD_TYPE_ERROR};

Type CONST_BOOL = {.type_id = LMD_TYPE_BOOL, .is_const = 1};
Type CONST_INT = {.type_id = LMD_TYPE_INT, .is_const = 1};
Type CONST_FLOAT = {.type_id = LMD_TYPE_FLOAT, .is_const = 1};
Type CONST_STRING = {.type_id = LMD_TYPE_STRING, .is_const = 1};

Type LIT_NULL = {.type_id = LMD_TYPE_NULL, .is_literal = 1, .is_const = 1};
Type LIT_BOOL = {.type_id = LMD_TYPE_BOOL, .is_literal = 1, .is_const = 1};
Type LIT_INT = {.type_id = LMD_TYPE_INT, .is_literal = 1, .is_const = 1};
Type LIT_INT64 = {.type_id = LMD_TYPE_INT64, .is_literal = 1, .is_const = 1};
Type LIT_FLOAT = {.type_id = LMD_TYPE_FLOAT, .is_literal = 1, .is_const = 1};
Type LIT_DECIMAL = {.type_id = LMD_TYPE_DECIMAL, .is_literal = 1, .is_const = 1};
Type LIT_STRING = {.type_id = LMD_TYPE_STRING, .is_literal = 1, .is_const = 1};
Type LIT_DTIME = {.type_id = LMD_TYPE_DTIME, .is_literal = 1, .is_const = 1};
Type LIT_NUM_SIZED = {.type_id = LMD_TYPE_NUM_SIZED, .is_literal = 1, .is_const = 1};
Type LIT_UINT64 = {.type_id = LMD_TYPE_UINT64, .is_literal = 1, .is_const = 1};
Type LIT_TYPE = {.type_id = LMD_TYPE_TYPE, .is_literal = 1, .is_const = 1};

// get_type_name: human-readable name for a TypeId (for error messages)
// Moved from lambda.h static inline to reduce JIT-embedded header size (~30 lines saved).
extern "C" const char* get_type_name(TypeId type_id) {
    switch (type_id) {
        case LMD_TYPE_RAW_POINTER: return "raw_pointer";
        case LMD_TYPE_NULL: return "null";
        case LMD_TYPE_BOOL: return "bool";
        case LMD_TYPE_INT: return "int";
        case LMD_TYPE_INT64: return "int64";
        case LMD_TYPE_FLOAT: return "float";
        case LMD_TYPE_FLOAT64: return "float";
        case LMD_TYPE_DECIMAL: return "decimal";
        case LMD_TYPE_DTIME: return "datetime";
        case LMD_TYPE_SYMBOL: return "symbol";
        case LMD_TYPE_STRING: return "string";
        case LMD_TYPE_BINARY: return "binary";
        case LMD_TYPE_RANGE: return "range";
        case LMD_TYPE_ARRAY_NUM: return "array[num]";
        case LMD_TYPE_ARRAY: return "array";
        case LMD_TYPE_MAP: return "map";
        case LMD_TYPE_VMAP: return "map";  // VMap appears as "map" to Lambda scripts
        case LMD_TYPE_ELEMENT: return "element";
        case LMD_TYPE_OBJECT: return "object";
        case LMD_TYPE_TYPE: return "type";
        case LMD_TYPE_FUNC: return "function";
        case LMD_TYPE_ANY: return "any";
        case LMD_TYPE_ERROR: return "error";
        case LMD_TYPE_UNDEFINED: return "undefined";
        case LMD_TYPE_PATH: return "path";
        case LMD_TYPE_NUM_SIZED: return "num_sized";  // generic; use get_num_sized_type_name() for specific name
        case LMD_TYPE_UINT64: return "u64";
        default: return "unknown";
    }
}

extern "C" const char* get_num_sized_type_name(NumSizedType num_type) {
    switch (num_type) {
        case NUM_INT8:    return "i8";
        case NUM_INT16:   return "i16";
        case NUM_INT32:   return "i32";
        case NUM_UINT8:   return "u8";
        case NUM_UINT16:  return "u16";
        case NUM_UINT32:  return "u32";
        case NUM_FLOAT16: return "f16";
        case NUM_FLOAT32: return "f32";
        default: return "num_sized";
    }
}

TypeType LIT_TYPE_NULL;
TypeType LIT_TYPE_BOOL;
TypeType LIT_TYPE_INT;
TypeType LIT_TYPE_INT64;
TypeType LIT_TYPE_FLOAT;
TypeType LIT_TYPE_FLOAT64;
TypeType LIT_TYPE_DECIMAL;
TypeType LIT_TYPE_INTEGER;
TypeType LIT_TYPE_NUMBER;
TypeType LIT_TYPE_STRING;
TypeType LIT_TYPE_BINARY;
TypeType LIT_TYPE_SYMBOL;
TypeType LIT_TYPE_PATH;
TypeType LIT_TYPE_DTIME;
TypeType LIT_TYPE_DATE;
TypeType LIT_TYPE_TIME;
TypeType LIT_TYPE_LIST;
TypeType LIT_TYPE_RANGE;
TypeType LIT_TYPE_ARRAY;
TypeType LIT_TYPE_MAP;
TypeType LIT_TYPE_ELMT;
TypeType LIT_TYPE_OBJECT;
TypeType LIT_TYPE_FUNC;
TypeType LIT_TYPE_TYPE;
TypeType LIT_TYPE_ANY;
TypeType LIT_TYPE_ERROR;
// sized numeric type references (for `is` checks on specific sub-types)
TypeType LIT_TYPE_I8;
TypeType LIT_TYPE_I16;
TypeType LIT_TYPE_I32;
TypeType LIT_TYPE_U8;
TypeType LIT_TYPE_U16;
TypeType LIT_TYPE_U32;
TypeType LIT_TYPE_U64;
TypeType LIT_TYPE_F16;
TypeType LIT_TYPE_F32;
TypeType LIT_TYPE_F64;

TypeMap EmptyMap;
TypeElmt EmptyElmt;
TypeObject EmptyObject;

const Item ItemNull = {._type_id = LMD_TYPE_NULL};
const Item ItemError = {._type_id = LMD_TYPE_ERROR};

// Note: ConstItem has const members and cannot be assigned after initialization.
// These are zero-initialized and should be used via reinterpret_cast from appropriate Items.
alignas(ConstItem) static uint64_t error_result_storage = ITEM_ERROR;
alignas(ConstItem) static uint64_t null_result_storage = ITEM_NULL;

ConstItem& error_result = *reinterpret_cast<ConstItem*>(&error_result_storage);
ConstItem& null_result = *reinterpret_cast<ConstItem*>(&null_result_storage);

extern __thread Context* input_context;

void init_typetype() {
    *(Type*)&TYPE_ARRAY = {.type_id = LMD_TYPE_ARRAY};
    TYPE_ARRAY.nested = &TYPE_ANY;  // default nested type
    TYPE_ARRAY.length = 0;  TYPE_ARRAY.type_index = -1;
    *(Type*)(&LIT_TYPE_NULL) = LIT_TYPE;  LIT_TYPE_NULL.type = &TYPE_NULL;
    *(Type*)(&LIT_TYPE_BOOL) = LIT_TYPE;  LIT_TYPE_BOOL.type = &TYPE_BOOL;
    *(Type*)(&LIT_TYPE_INT) = LIT_TYPE;  LIT_TYPE_INT.type = &TYPE_INT;
    *(Type*)(&LIT_TYPE_INT64) = LIT_TYPE;  LIT_TYPE_INT64.type = &TYPE_INT64;
    *(Type*)(&LIT_TYPE_FLOAT) = LIT_TYPE;  LIT_TYPE_FLOAT.type = &TYPE_FLOAT;
    *(Type*)(&LIT_TYPE_FLOAT64) = LIT_TYPE;  LIT_TYPE_FLOAT64.type = &TYPE_FLOAT;
    *(Type*)(&LIT_TYPE_DECIMAL) = LIT_TYPE;  LIT_TYPE_DECIMAL.type = &TYPE_DECIMAL;
    *(Type*)(&LIT_TYPE_INTEGER) = LIT_TYPE;  LIT_TYPE_INTEGER.type = &TYPE_INTEGER;
    *(Type*)(&LIT_TYPE_NUMBER) = LIT_TYPE;  LIT_TYPE_NUMBER.type = &TYPE_NUMBER;
    *(Type*)(&LIT_TYPE_STRING) = LIT_TYPE;  LIT_TYPE_STRING.type = &TYPE_STRING;
    *(Type*)(&LIT_TYPE_BINARY) = LIT_TYPE;  LIT_TYPE_BINARY.type = &TYPE_BINARY;
    *(Type*)(&LIT_TYPE_SYMBOL) = LIT_TYPE;  LIT_TYPE_SYMBOL.type = &TYPE_SYMBOL;
    *(Type*)(&LIT_TYPE_PATH) = LIT_TYPE;  LIT_TYPE_PATH.type = &TYPE_PATH;
    *(Type*)(&LIT_TYPE_DTIME) = LIT_TYPE;  LIT_TYPE_DTIME.type = &TYPE_DTIME;
    *(Type*)(&LIT_TYPE_DATE) = LIT_TYPE;  LIT_TYPE_DATE.type = &TYPE_DATE;
    *(Type*)(&LIT_TYPE_TIME) = LIT_TYPE;  LIT_TYPE_TIME.type = &TYPE_TIME;
    *(Type*)(&LIT_TYPE_LIST) = LIT_TYPE;  LIT_TYPE_LIST.type = &TYPE_LIST;
    *(Type*)(&LIT_TYPE_RANGE) = LIT_TYPE;  LIT_TYPE_RANGE.type = &TYPE_RANGE;
    *(Type*)(&LIT_TYPE_ARRAY) = LIT_TYPE;  LIT_TYPE_ARRAY.type = (Type*)&TYPE_ARRAY;
    *(Type*)(&LIT_TYPE_MAP) = LIT_TYPE;  LIT_TYPE_MAP.type = &TYPE_MAP;
    *(Type*)(&LIT_TYPE_ELMT) = LIT_TYPE;  LIT_TYPE_ELMT.type = &TYPE_ELMT;
    *(Type*)(&LIT_TYPE_OBJECT) = LIT_TYPE;  LIT_TYPE_OBJECT.type = &TYPE_OBJECT;
    *(Type*)(&LIT_TYPE_FUNC) = LIT_TYPE;  LIT_TYPE_FUNC.type = &TYPE_FUNC;
    *(Type*)(&LIT_TYPE_TYPE) = LIT_TYPE;  LIT_TYPE_TYPE.type = &TYPE_TYPE;
    *(Type*)(&LIT_TYPE_ANY) = LIT_TYPE;  LIT_TYPE_ANY.type = &TYPE_ANY;
    *(Type*)(&LIT_TYPE_ERROR) = LIT_TYPE;  LIT_TYPE_ERROR.type = &TYPE_ERROR;
    // sized numeric type references
    *(Type*)(&LIT_TYPE_I8)  = LIT_TYPE;  LIT_TYPE_I8.type  = &TYPE_I8;
    *(Type*)(&LIT_TYPE_I16) = LIT_TYPE;  LIT_TYPE_I16.type = &TYPE_I16;
    *(Type*)(&LIT_TYPE_I32) = LIT_TYPE;  LIT_TYPE_I32.type = &TYPE_I32;
    *(Type*)(&LIT_TYPE_U8)  = LIT_TYPE;  LIT_TYPE_U8.type  = &TYPE_U8;
    *(Type*)(&LIT_TYPE_U16) = LIT_TYPE;  LIT_TYPE_U16.type = &TYPE_U16;
    *(Type*)(&LIT_TYPE_U32) = LIT_TYPE;  LIT_TYPE_U32.type = &TYPE_U32;
    *(Type*)(&LIT_TYPE_U64) = LIT_TYPE;  LIT_TYPE_U64.type = &TYPE_UINT64;
    *(Type*)(&LIT_TYPE_F16) = LIT_TYPE;  LIT_TYPE_F16.type = &TYPE_F16;
    *(Type*)(&LIT_TYPE_F32) = LIT_TYPE;  LIT_TYPE_F32.type = &TYPE_F32;
    *(Type*)(&LIT_TYPE_F64) = LIT_TYPE;  LIT_TYPE_F64.type = &TYPE_FLOAT;

    memset(&EmptyMap, 0, sizeof(TypeMap));
    EmptyMap.type_id = LMD_TYPE_MAP;  EmptyMap.type_index = -1;

    memset(&EmptyElmt, 0, sizeof(TypeElmt));
    EmptyElmt.type_id = LMD_TYPE_ELEMENT;  EmptyElmt.type_index = -1;  EmptyElmt.name = {0};

    memset(&EmptyObject, 0, sizeof(TypeObject));
    EmptyObject.type_id = LMD_TYPE_OBJECT;  EmptyObject.type_index = -1;
}

TypeInfo type_info[LMD_CONTAINER_HEAP_START + 1];

void init_type_info() {
    type_info[LMD_TYPE_RAW_POINTER] = {sizeof(void*), "pointer", &TYPE_NULL, (Type*)&LIT_TYPE_NULL};
    type_info[LMD_TYPE_NULL] = {sizeof(void*), "null", &TYPE_NULL, (Type*)&LIT_TYPE_NULL};  // pointer-sized for NULL↔container transitions
    type_info[LMD_TYPE_UNDEFINED] = {sizeof(bool), "undefined", &TYPE_UNDEFINED, (Type*)&LIT_TYPE_NULL};  // JS undefined
    type_info[LMD_TYPE_BOOL] = {sizeof(bool), "bool", &TYPE_BOOL, (Type*)&LIT_TYPE_BOOL};
    type_info[LMD_TYPE_INT] = {sizeof(int64_t), "int", &TYPE_INT, (Type*)&LIT_TYPE_INT};  // 64-bit to store 56-bit value
    type_info[LMD_TYPE_INT64] = {sizeof(int64_t), "int64", &TYPE_INT64, (Type*)&LIT_TYPE_INT64};
    type_info[LMD_TYPE_FLOAT] = {sizeof(double), "float", &TYPE_FLOAT, (Type*)&LIT_TYPE_FLOAT};
    type_info[LMD_TYPE_FLOAT64] = {sizeof(double), "float", &TYPE_FLOAT, (Type*)&LIT_TYPE_FLOAT};
    type_info[LMD_TYPE_DECIMAL] = {sizeof(void*), "decimal", &TYPE_DECIMAL, (Type*)&LIT_TYPE_DECIMAL};
    type_info[LMD_TYPE_DTIME] = {sizeof(DateTime), "datetime", &TYPE_DTIME, (Type*)&LIT_TYPE_DTIME};
    type_info[LMD_TYPE_SYMBOL] = {sizeof(char*), "symbol", &TYPE_SYMBOL, (Type*)&LIT_TYPE_SYMBOL};
    type_info[LMD_TYPE_STRING] = {sizeof(char*), "string", &TYPE_STRING, (Type*)&LIT_TYPE_STRING};
    type_info[LMD_TYPE_BINARY] = {sizeof(char*), "binary", &TYPE_BINARY, (Type*)&LIT_TYPE_BINARY};
    type_info[LMD_TYPE_RANGE] = {sizeof(void*), "range", &TYPE_RANGE, (Type*)&LIT_TYPE_RANGE};
    type_info[LMD_TYPE_ARRAY] = {sizeof(void*), "array", (Type*)&TYPE_ARRAY, (Type*)&LIT_TYPE_ARRAY};
    type_info[LMD_TYPE_ARRAY_NUM] = {sizeof(void*), "array", (Type*)&TYPE_ARRAY, (Type*)&LIT_TYPE_ARRAY};
    type_info[LMD_TYPE_MAP] = {sizeof(void*), "map", &TYPE_MAP, (Type*)&LIT_TYPE_MAP};
    // VMap values are container pointers even though they present as Lambda maps;
    // shape fields need pointer-sized metadata or later map rebuilds pack them as
    // zero-byte unknown fields and corrupt ordinary maps that store VMaps.
    type_info[LMD_TYPE_VMAP] = {sizeof(void*), "map", &TYPE_MAP, (Type*)&LIT_TYPE_MAP};
    type_info[LMD_TYPE_ELEMENT] = {sizeof(void*), "element", &TYPE_ELMT, (Type*)&LIT_TYPE_ELMT};
    type_info[LMD_TYPE_OBJECT] = {sizeof(void*), "object", &TYPE_OBJECT, (Type*)&LIT_TYPE_OBJECT};
    type_info[LMD_TYPE_TYPE] = {sizeof(void*), "type", &TYPE_TYPE, (Type*)&LIT_TYPE_TYPE};
    type_info[LMD_TYPE_FUNC] = {sizeof(void*), "function", &TYPE_FUNC, (Type*)&LIT_TYPE_FUNC};
    type_info[LMD_TYPE_ANY] = {sizeof(TypedItem), "any", &TYPE_ANY, (Type*)&LIT_TYPE_ANY};
    type_info[LMD_TYPE_ERROR] = {sizeof(void*), "error", &TYPE_ERROR, (Type*)&LIT_TYPE_ERROR};
    type_info[LMD_TYPE_PATH] = {sizeof(void*), "path", &TYPE_PATH, (Type*)&LIT_TYPE_PATH};
    type_info[LMD_TYPE_NUM_SIZED] = {sizeof(uint64_t), "num_sized", &TYPE_NUM_SIZED, (Type*)&LIT_TYPE_INT};  // inline packed
    type_info[LMD_TYPE_UINT64] = {sizeof(uint64_t), "u64", &TYPE_UINT64, (Type*)&LIT_TYPE_U64};
    type_info[LMD_CONTAINER_HEAP_START] = {0, "container_start", &TYPE_NULL, (Type*)&LIT_TYPE_NULL};
}

struct Initializer {
    Initializer() {
        init_typetype();
        init_type_info();
    }
};
static Initializer initializer;

Type* alloc_type(Pool* pool, TypeId type, size_t size) {
    Type* t;
    t = (Type*)pool_calloc(pool, size);
    memset(t, 0, size);
    t->type_id = type;
    // Defensive check: verify the type was properly initialized
    if (t->is_const != 0) {
        log_warn("Warning: alloc_type - is_const flag was not zeroed properly");
        t->is_const = 0; // Force correction
    }
    return t;
}

// allocate a Type with type_id = LMD_TYPE_TYPE and a specific TypeKind
Type* alloc_type_kind(Pool* pool, uint8_t kind, size_t size) {
    Type* t = alloc_type(pool, LMD_TYPE_TYPE, size);
    t->kind = kind;
    return t;
}

extern "C" {

// Old it2l - redirects to get_int56() for int type
// Note: The main it2l function is defined below it2i

double it2d(Item itm) {
    TypeId type_id = get_type_id(itm);
    if (type_id == LMD_TYPE_INT) {
        return (double)itm.get_int56();
    }
    else if (type_id == LMD_TYPE_INT64) {
        return (double)itm.get_int64();
    }
    else if (is_float_type_id(type_id)) {
        return itm.get_double();
    }
    else if (type_id == LMD_TYPE_DECIMAL) {
        return decimal_to_double(itm);
    }
    else if (type_id == LMD_TYPE_NUM_SIZED) {
        return itm.get_num_sized_as_double();
    }
    else if (type_id == LMD_TYPE_UINT64) {
        return (double)itm.get_uint64();
    }
    else if (type_id == LMD_TYPE_ERROR) {
        return NAN;  // poison NaN — auto-propagates through all downstream arithmetic
    }
    log_debug("it2d: cannot convert type %s to double", get_type_name(type_id));
    return NAN;  // NaN for unrecognized types (was 0.0 — silent data corruption)
}

bool it2b(Item itm) {
    TypeId type_id = get_type_id(itm);
    if (type_id == LMD_TYPE_BOOL) {
        return itm.bool_val != 0;
    }
    // Convert other types to boolean following JavaScript rules
    else if (type_id == LMD_TYPE_NULL || type_id == LMD_TYPE_ERROR) {
        return false;  // errors are falsy
    }
    else if (type_id == LMD_TYPE_INT) {
        return itm.get_int56() != 0;
    }
    else if (is_float_type_id(type_id)) {
        // Lambda truthiness treats every number as truthy; inline floats must not
        // inherit JS-style zero/NaN falsiness from their raw double payload.
        return true;
    }
    else if (type_id == LMD_TYPE_STRING) {
        String* str = itm.get_safe_string();
        return str && str->len > 0;
    }
    // Objects are truthy
    return true;
}

int64_t it2i(Item itm) {
    TypeId type_id = get_type_id(itm);
    if (type_id == LMD_TYPE_INT) {
        return itm.get_int56();
    }
    else if (type_id == LMD_TYPE_INT64) {
        return itm.get_int64();
    }
    else if (is_float_type_id(type_id)) {
        return (int64_t)itm.get_double();
    }
    else if (type_id == LMD_TYPE_BOOL) {
        return itm.bool_val ? 1 : 0;
    }
    else if (type_id == LMD_TYPE_NUM_SIZED) {
        NumSizedType st = itm.get_num_type();
        if (st == NUM_FLOAT16 || st == NUM_FLOAT32) return (int64_t)itm.get_num_sized_as_double();
        return itm.get_num_sized_as_int64();
    }
    else if (type_id == LMD_TYPE_UINT64) {
        return (int64_t)itm.get_uint64();
    }
    else if (type_id == LMD_TYPE_ERROR) {
        return 0;  // error items return 0 (callers should check type before calling it2i)
    }
    return 0;  // unrecognized type
}

// MIR JIT workaround: opaque store functions to prevent SSA optimizer from
// reordering swap-pattern assignments inside while loops.
void _store_i64(int64_t* dst, int64_t val) { *dst = val; }
void _store_f64(double* dst, double val) { *dst = val; }

// extract int56 as int64 (full precision)
int64_t it2l(Item itm) {
    TypeId type_id = get_type_id(itm);
    if (type_id == LMD_TYPE_INT) {
        return itm.get_int56();
    }
    else if (type_id == LMD_TYPE_INT64) {
        return itm.get_int64();
    }
    else if (is_float_type_id(type_id)) {
        return (int64_t)itm.get_double();
    }
    else if (type_id == LMD_TYPE_BOOL) {
        return itm.bool_val ? 1 : 0;
    }
    else if (type_id == LMD_TYPE_NUM_SIZED) {
        NumSizedType st = itm.get_num_type();
        if (st == NUM_FLOAT16 || st == NUM_FLOAT32) return (int64_t)itm.get_num_sized_as_double();
        return itm.get_num_sized_as_int64();
    }
    else if (type_id == LMD_TYPE_UINT64) {
        return (int64_t)itm.get_uint64();
    }
    return INT64_MAX;  // error sentinel
}

String* it2s(Item itm) {
    if (itm._type_id == LMD_TYPE_STRING) {
        return itm.get_safe_string();
    }
    if (itm._type_id == LMD_TYPE_ERROR) {
        static struct { uint32_t len; uint8_t is_ascii; char chars[8]; } str_err_buf = {7, 1, "<error>"};
        return (String*)&str_err_buf;
    }
    // For other types, we'd need to convert to string
    // For now, return a default string
    return nullptr;
}

Binary* it2x(Item itm) {
    return itm.get_safe_binary();
}

// Convert item to C string (for use in path segment names)
// Returns the chars pointer from a string/symbol
// For other types, returns empty string (path segments must be strings)
const char* fn_to_cstr(Item itm) {
    TypeId type_id = itm._type_id;
    if (is_text_type_id(type_id)) {
        return itm.get_chars();
    }
    if (type_id == LMD_TYPE_ERROR) {
        log_debug("fn_to_cstr: received error item");
        return "";  // error items return empty string for path segments
    }
    // For non-string types in path segments, return empty string
    // The calling code should ensure the expression evaluates to a string
    log_debug("fn_to_cstr: expected string type for path segment, got type %s", get_type_name(type_id));
    return "";
}

} // extern "C"

void expand_list(List *list, Arena* arena = nullptr) {
    JS_WEAK_PROPERTY_SET_BRANCH("expand_list_total");
    // P8 fix: do NOT bump list->capacity until the new buffer has been allocated
    // and populated. If heap_data_alloc triggers GC, gc_compact_data reads
    // (capacity, items) as a pair to memcpy `capacity * sizeof(Item)` bytes out
    // of the items buffer into the tenured zone. Bumping capacity first while
    // items still points at the old (half-sized) buffer makes the compactor
    // read `new_capacity * 8` bytes from a `(new_capacity/2) * 8`-byte buffer,
    // copying garbage past the buffer end into tenured. That garbage then
    // surfaces as "non-hole" slots in iteration paths (Array.every/some on
    // sparse arrays trip on this — items[6..N] read as zeros instead of the
    // hole sentinel that was written there).
    int64_t prev_capacity = list->capacity;
    int64_t new_capacity = list->capacity ? list->capacity * 2 : 8;

    // Determine which allocator to use
    Item* old_items = list->items;

    // If no arena explicitly passed, check input_context for arena fallback
    // (input parsing path where GC heap is not initialized)
    if (!arena && input_context && input_context->arena) {
        arena = input_context->arena;
    }

    bool use_arena = (arena != nullptr && (old_items == nullptr || arena_owns(arena, old_items)));

    if (use_arena) {
        // Use arena alloc for arena-allocated buffers (MarkBuilder/Input path).
        // Always allocate fresh — do NOT arena_realloc (which frees the old
        // buffer to the arena's free-list).  In ui_mode the old items buffer
        // may still be pointed-to by other Element structs in the retained
        // tree (siblings of a retransformed subtree).  Freeing it allows the
        // arena to recycle that memory for new DomElement allocations, which
        // overwrites the items data and causes use-after-free crashes.
        Item* new_items;
        {
            JS_WEAK_PROPERTY_SET_BRANCH("expand_list_arena_alloc");
            new_items = (Item*)arena_alloc(arena, new_capacity * sizeof(Item));
        }
        if (new_items && old_items) {
            JS_WEAK_PROPERTY_SET_BRANCH("expand_list_arena_copy");
            memcpy(new_items, old_items, prev_capacity * sizeof(Item));
        }
        list->items = new_items;
        list->capacity = new_capacity;
    } else {
        // Use data zone allocation for GC-managed runtime containers.
        // Allocate new buffer; old buffer is abandoned in the data zone
        // and will be reclaimed on next GC compaction/reset.
        size_t old_size = prev_capacity * sizeof(Item);
        size_t new_size;
        if (!lam::checked_mul((size_t)new_capacity, sizeof(Item), &new_size)) {
            return;   // overflow: growth not possible, leave capacity untouched
        }
        uint64_t list_root = (uint64_t)(uintptr_t)list;
        heap_register_gc_root(&list_root);
        Item* new_items;
        {
            JS_WEAK_PROPERTY_SET_BRANCH("expand_list_heap_alloc");
            new_items = (Item*)heap_data_alloc(new_size);
        }
        list = (List*)(uintptr_t)list_root;
        if (!new_items) {
            heap_unregister_gc_root(&list_root);
            // OOM: leave capacity untouched (old buffer still valid).
            return;
        }
        // Re-read old_items after allocation: GC may have fired during
        // heap_data_alloc, compacting list->items from nursery to tenured.
        // The local old_items would then point to freed nursery memory. The
        // compactor only ever sees the OLD (consistent) capacity here because
        // we haven't bumped list->capacity yet.
        old_items = list->items;
        if (old_items && old_size > 0) {
            JS_WEAK_PROPERTY_SET_BRANCH("expand_list_heap_copy");
            memcpy(new_items, old_items, old_size);
        }
        // Publish both the new buffer pointer and the new capacity atomically
        // from the compactor's perspective: it sees old/old or new/new but
        // never the new_capacity/old_items combination that overruns the
        // source buffer.
        list->items = new_items;
        list->capacity = new_capacity;
        heap_unregister_gc_root(&list_root);
    }

    // copy extra items to the end of the list
    if (list->extra) {
        memcpy(list->items + (list->capacity - list->extra),
            list->items + (list->capacity/2 - list->extra), list->extra * sizeof(Item));
        // scan the list, if the item is long/double,
        // and is stored in the list extra slots, need to update the pointer
        for (int i = 0; i < list->length; i++) {
            Item itm = list->items[i];
            if (((itm._type_id == LMD_TYPE_FLOAT && itm.double_ptr > 1) || itm._type_id == LMD_TYPE_FLOAT64) ||
                    (itm._type_id == LMD_TYPE_INT64 && !itm.is_inline_int64()) ||
                    itm._type_id == LMD_TYPE_DTIME) {
                Item* old_pointer = (Item*)itm.double_ptr;
                // Only update pointers that are in the old list buffer's extra space
                if (old_items <= old_pointer && old_pointer < old_items + list->capacity/2) {
                    int offset = old_items + list->capacity/2 - old_pointer;
                    void* new_pointer = list->items + list->capacity - offset;
                    list->items[i] = {.item = is_float_type_id(itm._type_id) ? d2it(new_pointer) :
                        itm._type_id == LMD_TYPE_INT64 ? l2it(new_pointer) : k2it(new_pointer)};
                }
                // if the pointer is not in the old buffer, it should not be updated
            }
        }
    }
}

Array* array_pooled(Pool *pool) {
    Array* arr = (Array*)pool_calloc(pool, sizeof(Array));
    if (arr == NULL) return NULL;
    memset(arr, 0, sizeof(Array));
    arr->type_id = LMD_TYPE_ARRAY;
    arr->is_immortal = 1;
    return arr;
}

// Arena-based allocation for MarkBuilder
Array* array_arena(Arena* arena) {
    Array* arr = (Array*)arena_alloc(arena, sizeof(Array));
    if (arr == NULL) return NULL;
    memset(arr, 0, sizeof(Array));
    arr->type_id = LMD_TYPE_ARRAY;
    arr->is_immortal = 1;
    return arr;
}

List* list_arena(Arena* arena) {
    List* list = (List*)arena_alloc(arena, sizeof(List));
    if (list == NULL) return NULL;
    memset(list, 0, sizeof(List));
    list->type_id = LMD_TYPE_ARRAY;
    list->is_immortal = 1;
    return list;
}

void array_set(Array* arr, int64_t index, Item itm) {
    arr->items[index] = itm;
    TypeId type_id = get_type_id(itm);
    switch (type_id) {
    case LMD_TYPE_FLOAT:
    case LMD_TYPE_FLOAT64: {
        if ((itm.item & ITEM_DBL_MASK) || itm.item == ITEM_FLOAT_P0 || itm.item == ITEM_FLOAT_N0) {
            break;
        }
        double* dval = (double*)(arr->items + (arr->capacity - arr->extra - 1));
        *dval = itm.get_double();
        Item encoded = lambda_float_ptr_to_item(dval);
        arr->items[index] = encoded;
        if (encoded.item == d2it(dval)) arr->extra++;
        break;
    }
    case LMD_TYPE_INT64: {
        if (itm.is_inline_int64()) break;
        int64_t* ival = (int64_t*)(arr->items + (arr->capacity - arr->extra - 1));
        *ival = itm.get_int64();  arr->items[index] = {.item = l2it(ival)};
        arr->extra++;
        break;
    }
    case LMD_TYPE_DTIME:  {
        DateTime* dtval = (DateTime*)(arr->items + (arr->capacity - arr->extra - 1));
        *dtval = itm.get_datetime();  arr->items[index] = {.item = k2it(dtval)};
        arr->extra++;
        break;
    }
    case LMD_TYPE_STRING:  case LMD_TYPE_BINARY: {
        break;
    }
    case LMD_TYPE_SYMBOL: {
        break;
    }
    default:
        if (LMD_TYPE_CONTAINER <= type_id && type_id <= LMD_TYPE_OBJECT) {
        }
    }
}

void array_copy_owned_items(Array* destination, int64_t destination_index,
                            const Item* source, int64_t count) {
    if (!destination || !source || count <= 0) return;
    // Scalar tail pointers belong to their source container. Route every copy
    // through array_set so the destination owns and rebases wide payloads.
    for (int64_t i = 0; i < count; i++) {
        array_set(destination, destination_index + i, source[i]);
    }
}

void owned_item_slot_store(Item* storage, int64_t item_count,
                           int64_t index, Item item) {
    if (!storage || item_count <= 0 || index < 0 || index >= item_count) return;
    storage[index] = item;
    Item* payload = &storage[item_count + index];
    switch (get_type_id(item)) {
    case LMD_TYPE_INT64:
        if (!item.is_inline_int64()) {
            *(int64_t*)payload = item.get_int64();
            storage[index] = {.item = l2it(payload)};
        }
        break;
    case LMD_TYPE_FLOAT:
    case LMD_TYPE_FLOAT64:
        if (!(item.item & ITEM_DBL_MASK) && item.item != ITEM_FLOAT_P0 &&
                item.item != ITEM_FLOAT_N0) {
            *(double*)payload = item.get_double();
            storage[index] = lambda_float_ptr_to_item((double*)payload);
        }
        break;
    case LMD_TYPE_DTIME:
        *(DateTime*)payload = item.get_datetime();
        storage[index] = {.item = k2it(payload)};
        break;
    default:
        break;
    }
}

Item owned_item_slot_read(Item* storage, int64_t item_count,
                          int64_t index, bool immortal) {
    if (!storage || item_count <= 0 || index < 0 || index >= item_count) return ItemNull;
    return scalar_storage_read(storage[index], immortal);
}

Item scalar_storage_read(Item item, bool immortal) {
    if (immortal) return item;
#ifdef LAMBDA_STATIC
    // The standalone input library has no execution side stack; all of its
    // container storage is owned by the live input pool, so the reference is
    // already stable for the reader's lifetime.
    return item;
#else
    // Owned storage may move or die independently of the reader. Re-home its
    // interior scalar references in the current number frame before escape.
    switch (get_type_id(item)) {
    case LMD_TYPE_INT64:
        return item.is_inline_int64() ? item : box_int64_value(item.get_int64());
    case LMD_TYPE_FLOAT:
    case LMD_TYPE_FLOAT64:
        return push_d(item.get_double());
    case LMD_TYPE_DTIME:
        return push_k(item.get_datetime());
    default:
        return item;
    }
#endif
}

void array_append(Array* arr, Item itm, Pool *pool, Arena* arena) {
    if (arr->length + arr->extra + 2 > arr->capacity) { expand_list((List*)arr, arena); }
    (void)pool;
    // Even arena-backed builders can receive a runtime-boxed scalar. Rebase it
    // into this array so no container Item ever owns another frame's payload.
    array_set(arr, arr->length, itm);
    arr->length++;
}

void array_push(Array* arr, Item item) {
    TypeId type_id = get_type_id(item);
    if (type_id == LMD_TYPE_ARRAY) { // flatten only content lists (from list_end)
        List *nest_list = item.array;
        if (nest_list && nest_list->is_content) {
            for (int i = 0; i < nest_list->length; i++) {
                Item nest_item = nest_list->items[i];
                array_push(arr, nest_item);
            }
            return;
        }
    }
    if (arr->length + arr->extra + 2 > arr->capacity) { expand_list((List*)arr); }
    array_set(arr, arr->length, item);
    arr->length++;
}

// push(arr, val) — Lambda-script builtin: append val to a growable generic array
// in place (amortized O(1) via expand_list's doubling). Mutates arr and returns it,
// so len(arr) reflects the new size and arr[i] indexes directly. This is the
// idiomatic replacement for the chunked-vector + `.sz` size-tracking workaround.
Item pn_push(Item arr_item, Item value) {
    TypeId tid = get_type_id(arr_item);
    if (tid != LMD_TYPE_ARRAY) {
        log_error("push: expected a growable array, got %s", get_type_name(tid));
        return arr_item;
    }
    // array_push grows via expand_list (GC-aware, registers the array as a root
    // across the allocation). It only flattens nested *content* lists; ordinary
    // `[...]` values are appended as a single element.
    array_push(arr_item.array, value);
    return arr_item;
}

// splice(arr, start, count) — Lambda-script builtin: remove `count` elements starting
// at index `start` from a growable generic array, IN PLACE — shift the tail down over
// the gap and shrink `length` (no reallocation). Mutates arr and returns it, so callers
// holding the same array (incl. through a map field) see the removal. Together with
// push/len this gives pop / dequeue / middle-removal without a chunked + `.sz` wrapper.
//   pop:     splice(v, len(v) - 1, 1)
//   dequeue: splice(v, 0, 1)
//   remove:  splice(v, idx, 1)
// `start` may be negative (counts from the end, like slice); `start`/`count` are clamped
// to the valid range.
Item pn_splice(Item arr_item, Item start_item, Item count_item) {
    TypeId tid = get_type_id(arr_item);
    if (tid != LMD_TYPE_ARRAY && tid != LMD_TYPE_ARRAY_NUM) {
        log_error("splice: expected a growable array, got %s", get_type_name(tid));
        return arr_item;
    }
    TypeId st = get_type_id(start_item), ct = get_type_id(count_item);
    if (!is_integer_type_id(st) || !is_integer_type_id(ct)) {
        log_error("splice: start and count must be integers");
        return arr_item;
    }
    int64_t start = (st == LMD_TYPE_INT) ? start_item.get_int56() : start_item.get_int64();
    int64_t count = (ct == LMD_TYPE_INT) ? count_item.get_int56() : count_item.get_int64();

    if (tid == LMD_TYPE_ARRAY_NUM) {
        ArrayNum* arr = arr_item.array_num;
        if (arr->is_view || arr->is_ndim) {
            log_error("splice: cannot splice a view or N-D array; copy()/ravel() first");
            return arr_item;
        }
        int64_t len = arr->length;
        if (start < 0) start += len;
        if (start < 0) start = 0;
        if (start > len) start = len;
        if (count < 0) count = 0;
        if (count > len - start) count = len - start;
        if (count == 0) return arr_item;
        // shift the typed elements [start+count, len) down to [start, len-count) — a raw
        // byte move on the contiguous buffer (`data` aliases items/float_items in the union;
        // ELEM_TYPE_SIZE gives bytes/element). No reallocation; just shrink the length.
        size_t esize = ELEM_TYPE_SIZE[arr->get_elem_type() >> 4];
        char* base = (char*)arr->data;
        memmove(base + (size_t)start * esize, base + (size_t)(start + count) * esize,
                (size_t)(len - start - count) * esize);
        arr->length = len - count;
        return arr_item;
    }

    // generic Array (Item* items)
    Array* arr = arr_item.array;
    int64_t len = arr->length;
    if (start < 0) start += len;       // negative start counts from the end
    if (start < 0) start = 0;
    if (start > len) start = len;
    if (count < 0) count = 0;
    if (count > len - start) count = len - start;
    if (count == 0) return arr_item;

    // shift the tail [start+count, len) down into [start, len-count)
    for (int64_t i = start + count; i < len; i++) {
        arr->items[i - count] = arr->items[i];
    }
    arr->length = len - count;
    return arr_item;
}

void list_push(List *list, Item item) {
    TypeId type_id = get_type_id(item);
    // 1. skip NULL value
    if (type_id == LMD_TYPE_NULL) { return; }

    // 2. nest content list is flattened (only arrays with is_content flag from list_end)
    if (type_id == LMD_TYPE_ARRAY) {
        List *nest_list = item.array;
        if (nest_list && (uintptr_t)nest_list >= 0x1000 && nest_list->is_content) {
            // content list: flatten by copying over the items
            if (nest_list->items == NULL) {
                // empty content lists are valid no-op results from vector helpers.
                if (nest_list->length == 0) return;
                log_error("list_push: nested list has NULL items array! length=%ld, list=%p", nest_list->length, (void*)nest_list);
                return;
            }
            for (int i = 0; i < nest_list->length; i++) {
                Item nest_item = nest_list->items[i];
                list_push(list, nest_item);
            }
            return;
        }
    }

    // 3. need to merge with previous string if any (unless disabled)
    if (type_id == LMD_TYPE_STRING) {
#ifndef LAMBDA_STATIC
        // ui_mode: copy GC-heap string to result arena as fat DomText when adding to element content
        bool is_ui = input_context && input_context->ui_mode && input_context->arena;
        if (is_ui && list->is_content) {
            item = ui_copy_string_to_arena(input_context->arena, item);
        }
#else
        bool is_ui = false;
#endif

        // Only attempt string merging if input_context is available and merging is enabled
        bool should_merge = input_context != NULL &&
                           !input_context->disable_string_merging &&
                           list->length > 0 &&
                           list->items != NULL;

        if (should_merge) {
            Item prev_item = list->items[list->length - 1];
            if (get_type_id(prev_item) == LMD_TYPE_STRING) {
                String *prev_str = prev_item.get_safe_string();
                String *new_str = item.get_safe_string();
                if (prev_str && new_str) {
#ifndef LAMBDA_STATIC
                    if (is_ui) {
                        // ui_mode: merge into a new fat DomText on the arena
                        list->items[list->length - 1] = ui_merge_strings_to_arena(
                            input_context->arena, prev_str, new_str);
                        return;
                    }
#endif
                    // merge the two strings
                    size_t new_len = prev_str->len + new_str->len;
                    String *merged_str;
                    if (input_context->consts) { // dynamic runtime context
                        merged_str = (String *)input_context->context_alloc(sizeof(String) + new_len + 1, LMD_TYPE_STRING);
                    } else {  // static (input) context
                        merged_str = (String*)pool_calloc(input_context->pool, sizeof(String) + new_len + 1);
                    }
                    memcpy(merged_str->chars, prev_str->chars, prev_str->len);
                    memcpy(merged_str->chars + prev_str->len, new_str->chars, new_str->len);
                    merged_str->chars[new_len] = '\0';  merged_str->len = new_len;
                    // replace previous string with new merged string, in the list directly,
                    // assuming the list is still being constructed/mutable
                    list->items[list->length - 1] = (Item){.item = s2it(merged_str)};
                    return;
                }
            }
        }
    }
    else if (LMD_TYPE_RANGE <= type_id && type_id <= LMD_TYPE_OBJECT) {
    }
    else if (type_id == LMD_TYPE_FUNC) {
    }

    // store the value in the list (and we may need two slots for long/double)
    if (list->length + list->extra + 2 > list->capacity) { expand_list(list); }
    // Safety check: ensure items array was allocated
    if (list->items == NULL) {
        log_error("CRITICAL: list->items is NULL after expand_list! length=%ld, capacity=%ld", list->length, list->capacity);
        return;  // Prevent crash
    }
    // Note: TYPE_ERROR will be stored as it is
    list->items[list->length++] = item;
    switch (item._type_id) {
    case LMD_TYPE_STRING:  case LMD_TYPE_BINARY: {
        break;
    }
    case LMD_TYPE_SYMBOL: {
        break;
    }
    case LMD_TYPE_DECIMAL: {
        Decimal *dval = item.get_decimal();
        if (dval) {
        } else {
        }
        break;
    }
    case LMD_TYPE_FLOAT:
    case LMD_TYPE_FLOAT64: {
        if (item.item == ITEM_FLOAT_P0 || item.item == ITEM_FLOAT_N0) {
            break;
        }
        double* dval = (double*)(list->items + (list->capacity - list->extra - 1));
        *dval = item.get_double();
        Item encoded = lambda_float_ptr_to_item(dval);
        list->items[list->length-1] = encoded;
        if (encoded.item == d2it(dval)) list->extra++;
        break;
    }
    case LMD_TYPE_INT64: {
        if (item.is_inline_int64()) break;
        int64_t* ival = (int64_t*)(list->items + (list->capacity - list->extra - 1));
        *ival = item.get_int64();  list->items[list->length-1] = {.item = l2it(ival)};
        list->extra++;
        break;
    }
    case LMD_TYPE_DTIME:  {
        DateTime* dtval = (DateTime*)(list->items + (list->capacity - list->extra - 1));
        DateTime dt = *dtval = item.get_datetime();  list->items[list->length-1] = {.item = k2it(dtval)};
        StrBuf *strbuf = strbuf_new();
        datetime_format_lambda(strbuf, &dt);
        strbuf_free(strbuf);
        list->extra++;
        break;
    }
    }
    // log_item({.array = list}, "list_after_push");
}

// push item to list, spreading spreadable arrays or lists inline
// skips spreadable nulls (from empty for-expressions)
void list_push_spread(List *list, Item item) {
    TypeId type_id = get_type_id(item);
    // skip spreadable null (empty for-expression result)
    if (item.item == ITEM_NULL_SPREADABLE) {
        return;
    }
    // check if this is a spreadable array
    if (type_id == LMD_TYPE_ARRAY) {
        Array* arr = item.array;
        if (arr && arr->is_spreadable) {
            for (int i = 0; i < arr->length; i++) {
                list_push(list, arr->items[i]);
            }
            return;
        }
    }
    // check if this is a spreadable list
    if (type_id == LMD_TYPE_ARRAY) {
        List* inner = item.array;
        if (inner && inner->is_spreadable) {
            for (int i = 0; i < inner->length; i++) {
                list_push(list, inner->items[i]);
            }
            return;
        }
    }
    // check if this is a spreadable ArrayNum
    if (type_id == LMD_TYPE_ARRAY_NUM) {
        ArrayNum* arr = item.array_num;
        if (arr && arr->is_spreadable) {
            switch (arr->get_elem_type()) {
            case ELEM_INT:
                for (int64_t i = 0; i < arr->length; i++)
                    list_push(list, {.item = i2it(arr->items[i])});
                break;
            case ELEM_INT64:
                for (int64_t i = 0; i < arr->length; i++)
                    list_push(list, {.item = l2it(&arr->items[i])});
                break;
            case ELEM_FLOAT64:
                for (int64_t i = 0; i < arr->length; i++)
                    list_push(list, lambda_float_ptr_to_item(&arr->float_items[i]));
                break;
            default: break;
            }
            return;
        }
    }
    // not spreadable, push as-is
    list_push(list, item);
}

Item array_num_read_borrowed_item(ArrayNum* array, int64_t offset) {
    if (!array || offset < 0 || offset >= array->length) return ItemNull;
    switch (array->get_elem_type()) {
        case ELEM_INT:     return (Item){.item = i2it(array->items[offset])};
        case ELEM_INT64:   return (Item){.item = l2it(&array->items[offset])};
        case ELEM_FLOAT64: return lambda_float_ptr_to_item(&array->float_items[offset]);
        case ELEM_INT8:    return (Item){.item = i8_to_item(((int8_t*)array->data)[offset])};
        case ELEM_INT16:   return (Item){.item = i16_to_item(((int16_t*)array->data)[offset])};
        case ELEM_INT32:   return (Item){.item = i32_to_item(((int32_t*)array->data)[offset])};
        case ELEM_UINT8:   return (Item){.item = u8_to_item(((uint8_t*)array->data)[offset])};
        case ELEM_UINT8_CLAMPED: return (Item){.item = u8_to_item(((uint8_t*)array->data)[offset])};
        case ELEM_UINT16:  return (Item){.item = u16_to_item(((uint16_t*)array->data)[offset])};
        case ELEM_UINT32:  return (Item){.item = u32_to_item(((uint32_t*)array->data)[offset])};
        case ELEM_FLOAT16: return (Item){.item = f16_to_item(f16_bits_to_f32(((uint16_t*)array->data)[offset]))};
        case ELEM_FLOAT32: return (Item){.item = f32_to_item(((float*)array->data)[offset])};
        case ELEM_UINT64:  return (Item){.item = u64_to_item(&((uint64_t*)array->data)[offset])};
        case ELEM_BOOL:    return (Item){.item = b2it(((uint8_t*)array->data)[offset] ? BOOL_TRUE : BOOL_FALSE)};
        default:           return ItemError;
    }
}

ConstItem List::get(int index) const {
    if (!this) { return null_result; }
    if (index < 0 || index >= this->length) {
        log_error("list_get_const: index out of bounds: %d", index);
        return null_result;
    }
    return this->items[index].to_const();
}

void set_fields(TypeMap *map_type, void* map_data, va_list args) {
    long count = map_type->length;
    ShapeEntry *field = map_type->shape;
    for (long i = 0; i < count; i++) {
        // printf("set field of type: %d, offset: %ld, name:%.*s\n", field->type->type_id, field->byte_offset,
        //     field->name ? (int)field->name->length:4, field->name ? field->name->str : "null");
        void* field_ptr = map_field_ptr(map_data, field);
        // always read an Item (uint64_t) from varargs - transpiler now passes Items via box functions like i2it()
        Item item = {.item = va_arg(args, uint64_t)};
        if (!field->name) { // nested map
            TypeId type_id = get_type_id(item);
            if (type_id == LMD_TYPE_MAP) {
                Map* nested_map = item.map;
                *(Map**)field_ptr = nested_map;
            } else {
                log_error("expected a map, got data of type %s", get_type_name(type_id));
                *(Map**)field_ptr = nullptr;
            }
        } else {
            switch (field->type->type_id) {
            case LMD_TYPE_NULL: {
                // For dynamically-typed fields (e.g. state-bound element attributes),
                // store non-null values as raw tagged Items. The compiler sets shape type
                // to LMD_TYPE_NULL when it can't resolve the type at compile time.
                if (item.item != ITEM_NULL && get_type_id(item) != LMD_TYPE_NULL) {
                    *(Item*)field_ptr = item;
                }
                break;
            }
            case LMD_TYPE_BOOL: {
                *(bool*)field_ptr = item.bool_val;
                break;
            }
            case LMD_TYPE_INT: {
                // handle type coercion: float → int, bool → int
                TypeId item_type = get_type_id(item);
                int64_t val;
                if (is_float_type_id(item_type)) {
                    val = (int64_t)item.get_double();
                } else if (item_type == LMD_TYPE_BOOL) {
                    val = item.bool_val ? 1 : 0;
                } else {
                    val = item.get_int56();
                }
                *(int64_t*)field_ptr = val;
                break;
            }
            case LMD_TYPE_INT64: {
                *(int64_t*)field_ptr = item.get_int64();
                break;
            }
            case LMD_TYPE_FLOAT:
            case LMD_TYPE_FLOAT64: {
                // handle type coercion: int → float, int64 → float
                TypeId item_type = get_type_id(item);
                double val;
                if (item_type == LMD_TYPE_INT) {
                    val = (double)item.get_int56();
                } else if (item_type == LMD_TYPE_INT64) {
                    val = (double)item.get_int64();
                } else {
                    val = item.get_double();
                }
                *(double*)field_ptr = val;
                break;
            }
            case LMD_TYPE_DTIME:  {
                DateTime dtval = item.get_datetime();
                // StrBuf *strbuf = strbuf_new();
                // datetime_format_lambda(strbuf, &dtval);
                // log_debug("set field of datetime type to: %s", strbuf->str);
                *(DateTime*)field_ptr = dtval;
                break;
            }
            case LMD_TYPE_STRING: {
                *(String**)field_ptr = item.get_safe_string();
                break;
            }
            case LMD_TYPE_BINARY: {
                *(Binary**)field_ptr = item.get_safe_binary();
                break;
            }
            case LMD_TYPE_SYMBOL: {
                Symbol *sym = item.get_safe_symbol();
                *(Symbol**)field_ptr = sym;
                break;
            }
            case LMD_TYPE_ARRAY:  case LMD_TYPE_ARRAY_NUM:
            case LMD_TYPE_RANGE:  case LMD_TYPE_MAP:  case LMD_TYPE_VMAP:
            case LMD_TYPE_ELEMENT:  case LMD_TYPE_OBJECT: {
                TypeId item_type = get_type_id(item);
                if (item_type >= LMD_TYPE_RANGE && item_type <= LMD_TYPE_OBJECT) {
                    *(Container**)field_ptr = item.container;
                } else {
                    *(Container**)field_ptr = nullptr;
                }
                break;
            }
            case LMD_TYPE_TYPE: {
                if (get_type_id(item) == LMD_TYPE_NULL) {
                    *(void**)field_ptr = nullptr;
                } else {
                    *(void**)field_ptr = (void*)item.type;
                }
                break;
            }
            case LMD_TYPE_FUNC: {
                if (get_type_id(item) == LMD_TYPE_NULL) {
                    *(Function**)field_ptr = nullptr;
                } else {
                    *(Function**)field_ptr = item.function;
                }
                break;
            }
            case LMD_TYPE_PATH: {
                if (get_type_id(item) == LMD_TYPE_NULL) {
                    *(Path**)field_ptr = nullptr;
                } else {
                    *(Path**)field_ptr = item.path;
                }
                break;
            }
            case LMD_TYPE_ANY: { // a special case
                TypeId type_id = get_type_id(item);
                TypedItem titem = {.type_id = type_id, .item = item.item};
                switch (type_id) {
                case LMD_TYPE_NULL: ;
                    break; // no extra work needed
                case LMD_TYPE_BOOL:
                    titem.bool_val = item.bool_val;  break;
                case LMD_TYPE_INT:
                    titem.int_val = item.int_val;  break;
                case LMD_TYPE_INT64:
                    titem.long_val = item.get_int64();  break;
                case LMD_TYPE_FLOAT:
                case LMD_TYPE_FLOAT64:
                    titem.double_val = item.get_double();  break;
                case LMD_TYPE_DTIME:
                    titem.datetime_val = item.get_datetime();  break;
                case LMD_TYPE_STRING:
                    titem.string = item.get_safe_string();
                    break;
                case LMD_TYPE_BINARY:
                    titem.binary = item.get_safe_binary();
                    break;
                case LMD_TYPE_SYMBOL:
                    titem.symbol = item.get_safe_symbol();
                    break;
                case LMD_TYPE_ARRAY:  case LMD_TYPE_ARRAY_NUM:
                case LMD_TYPE_MAP:  case LMD_TYPE_VMAP:
                case LMD_TYPE_ELEMENT:  case LMD_TYPE_OBJECT: {
                    Container *container = item.container;
                    titem.container = container;
                    break;
                }
                case LMD_TYPE_TYPE:
                    titem.type = item.type;
                    break;
                case LMD_TYPE_FUNC: {
                    Function* fn = item.function;
                    titem.function = fn;
                    break;
                }
                case LMD_TYPE_PATH:
                    titem.path = item.path;
                    break;
                case LMD_TYPE_ERROR:
                case LMD_TYPE_UNDEFINED:
                    // store sentinel — the type_id alone is enough to round-trip
                    // through typeditem_to_item. No payload to copy.
                    break;
                default:
                    log_error("unknown type %s in set_fields", get_type_name(type_id));
                    // set as ERROR
                    titem = {.type_id = LMD_TYPE_ERROR};
                }
                // set in map
                *(TypedItem*)field_ptr = titem;
                break;
            }
            default:
                log_error("unknown type %s", get_type_name(field->type->type_id));
            }
        }
        field = field->next;
    }
}

extern TypeMap EmptyMap;

Map* map_pooled(Pool *pool) {
    Map *map = (Map *)pool_calloc(pool, sizeof(Map));
    map->type_id = LMD_TYPE_MAP;
    map->is_immortal = 1;
    map->type = &EmptyMap;
    return map;
}

// Arena-based allocation for MarkBuilder
Map* map_arena(Arena* arena) {
    Map *map = (Map *)arena_alloc(arena, sizeof(Map));
    if (map == NULL) return NULL;
    memset(map, 0, sizeof(Map));
    map->type_id = LMD_TYPE_MAP;
    map->is_immortal = 1;
    map->type = &EmptyMap;
    return map;
}

Item typeditem_to_item(TypedItem *titem) {
    void* ptr_val = nullptr;
    uint64_t item_val = 0;
    switch (titem->type_id) {
    case LMD_TYPE_NULL:  return ItemNull;
    case LMD_TYPE_BOOL:
        return {.item = b2it(titem->bool_val)};
    case LMD_TYPE_INT:
        return {.item = i2it(titem->int_val)};
    case LMD_TYPE_INT64:
        return {.item = l2it(&titem->long_val)};
    case LMD_TYPE_FLOAT:
        return lambda_float_ptr_to_item(&titem->double_val);
    case LMD_TYPE_FLOAT64:
        return lambda_float_ptr_to_item(&titem->double_val);
    case LMD_TYPE_DTIME:
        return {.item = k2it(&titem->item)};
    case LMD_TYPE_DECIMAL:
        memcpy(&ptr_val, ((char*)titem) + 1, sizeof(void*));
        return {.item = c2it((Decimal*)ptr_val)};
    case LMD_TYPE_STRING:
        memcpy(&ptr_val, ((char*)titem) + 1, sizeof(void*));
        return {.item = s2it((String*)ptr_val)};
    case LMD_TYPE_SYMBOL:
        memcpy(&ptr_val, ((char*)titem) + 1, sizeof(void*));
        return {.item = y2it((Symbol*)ptr_val)};
    case LMD_TYPE_BINARY:
        memcpy(&ptr_val, ((char*)titem) + 1, sizeof(void*));
        return {.item = x2it((String*)ptr_val)};
    case LMD_TYPE_ARRAY:  case LMD_TYPE_ARRAY_NUM:
    case LMD_TYPE_RANGE:  case LMD_TYPE_MAP:  case LMD_TYPE_VMAP:
    case LMD_TYPE_ELEMENT:  case LMD_TYPE_OBJECT:
        memcpy(&item_val, ((char*)titem) + 1, sizeof(uint64_t));
        if (item_val) {
            Container* container = (Container*)item_val;
            if (container->type_id == LMD_TYPE_RAW_POINTER) {
                container->type_id = titem->type_id;
            }
        }
        return {.item = item_val};
    case LMD_TYPE_ERROR:
        // a typed slot may legitimately hold a stored error value; surface it
        // as an Error Item rather than logging an "unknown type" warning.
        return {.item = ITEM_ERROR};
    case LMD_TYPE_UNDEFINED:
        return {.item = ITEM_JS_UNDEFINED};
    default:
        log_error("map_get ANY type is UNKNOWN: %d", titem->type_id);
        return ItemError;
    }
}

Item map_field_to_item(void* field_ptr, TypeId type_id) {
    Item result = (Item){._type_id = type_id};
    void* ptr_val = nullptr;
    switch (type_id) {
    case LMD_TYPE_NULL: {
        // For dynamically-typed fields, a non-null value may have been stored as a raw
        // tagged Item by set_fields(). Read it back; return ItemNull if field is empty.
        uint64_t raw = 0;
        memcpy(&raw, field_ptr, sizeof(uint64_t));
        if (raw == 0) return ItemNull;
        Item item_val;
        memcpy(&item_val, field_ptr, sizeof(Item));
        return item_val;
    }
    case LMD_TYPE_BOOL:
        result.bool_val = *(bool*)field_ptr;
        break;
    case LMD_TYPE_INT:
        result = {.item = i2it(*(int64_t*)field_ptr)};  // read full int64 to preserve 56-bit value
        break;
    case LMD_TYPE_INT64:
        result = {.item = l2it(field_ptr)};  // points to long directly
        break;
    case LMD_TYPE_UINT64:
        result = {.item = u64_to_item((uint64_t*)field_ptr)};
        break;
    case LMD_TYPE_FLOAT:
        result = lambda_float_ptr_to_item((const double*)field_ptr);
        break;
    case LMD_TYPE_FLOAT64:
        result = lambda_float_ptr_to_item((const double*)field_ptr);
        break;
    case LMD_TYPE_DTIME:
        result = {.item = k2it(field_ptr)};  // points to datetime directly
        break;
    case LMD_TYPE_DECIMAL:
        memcpy(&ptr_val, field_ptr, sizeof(void*));
        result = {.item = c2it((Decimal*)ptr_val)};
        break;
    case LMD_TYPE_STRING:
        memcpy(&ptr_val, field_ptr, sizeof(void*));
        result = {.item = s2it((String*)ptr_val)};
        break;
    case LMD_TYPE_SYMBOL:
        memcpy(&ptr_val, field_ptr, sizeof(void*));
        result = {.item = y2it((Symbol*)ptr_val)};
        break;
    case LMD_TYPE_BINARY:
        memcpy(&ptr_val, field_ptr, sizeof(void*));
        result = {.item = x2it((String*)ptr_val)};
        break;

    case LMD_TYPE_RANGE:  case LMD_TYPE_ARRAY:  case LMD_TYPE_ARRAY_NUM:
    case LMD_TYPE_MAP:  case LMD_TYPE_ELEMENT:  case LMD_TYPE_OBJECT:  case LMD_TYPE_TYPE:  case LMD_TYPE_FUNC:
    case LMD_TYPE_PATH:
        memcpy(&ptr_val, field_ptr, sizeof(void*));
        result.container = (Container*)ptr_val;
        if (result.container && result.container->type_id == LMD_TYPE_RAW_POINTER) {
            result.container->type_id = type_id;
        }
        break;
    case LMD_TYPE_VMAP:
        // VMap is a host-object pointer, not a Container; reading it through
        // the generic container arm turns named DOM collection slots into null.
        memcpy(&ptr_val, field_ptr, sizeof(void*));
        result.vmap = (VMap*)ptr_val;
        break;
    case LMD_TYPE_ANY: {
        result = typeditem_to_item((TypedItem*)field_ptr);
        break;
    }
    case LMD_TYPE_ERROR:
        // field was stored with error type — return null
        return ItemNull;
    default:
        log_error("unknown map item type %s", get_type_name(type_id));
        return ItemError;
    }
    return result;
}

ConstItem _map_get_const(TypeMap* map_type, void* map_data, const char *key, bool *is_found, Target* key_ns) {
    ShapeEntry *field = map_type->shape;
    while (field) {
        if (!field->name) { // nested map, skip
            Map* nested_map = map_shape_field_to_map(map_data, field);
            bool nested_is_found;
            ConstItem result = _map_get_const((TypeMap*)nested_map->type, nested_map->data, key, &nested_is_found, key_ns);
            if (nested_is_found) {
                *is_found = true;
                return result;
            }
            field = field->next;
            continue;
        }
        // compare both name AND namespace
        if (field->name->str &&
            strncmp(field->name->str, key, field->name->length) == 0 &&
            strlen(key) == field->name->length &&
            target_equal(field->ns, key_ns)) {
            *is_found = true;
            TypeId type_id = field->type->type_id;
            void* field_ptr = map_field_ptr(map_data, field);
            log_debug("_map_get_const: key='%s' type_id=%d byte_offset=%d field_ptr=%p raw_8bytes=0x%016lx map_type=%p map_data=%p",
                key, type_id, field->byte_offset, field_ptr, *(uint64_t*)field_ptr, map_type, map_data);
            Item result = map_field_to_item(field_ptr, type_id);
            return *(ConstItem*)&result;
        }
        field = field->next;
    }
    *is_found = false;
    return null_result;
}

ConstItem Map::get(const Item key) const {
    if (!this || !key.item) { return null_result; }

    bool is_found;
    char *key_str = NULL;
    Target* key_ns = NULL;
    if (key._type_id == LMD_TYPE_STRING) {
        String* str = (String*)key.string_ptr;
        key_str = str->chars;
        // strings don't have namespace
    } else if (key._type_id == LMD_TYPE_SYMBOL) {
        Symbol* sym = (Symbol*)key.symbol_ptr;
        key_str = sym->chars;
        key_ns = sym->ns;
    } else {
        log_error("map_get_const: key must be string or symbol, got type %s", get_type_name(key._type_id));
        return null_result;  // only string or symbol keys are supported
    }
    return _map_get_const((TypeMap*)this->type, this->data, key_str, &is_found, key_ns);
}

ConstItem Map::get(const char* key_str) const {
    if (!this || !key_str) { return null_result; }
    bool is_found;
    // unqualified string lookup - only matches keys with NULL namespace
    return _map_get_const((TypeMap*)this->type, this->data, (char*)key_str, &is_found, NULL);
}

bool Map::has_field(const char* field_name) const {
    if (!this || !this->type) return false;

    TypeMap* type = (TypeMap*)this->type;
    if (!type->shape) return false;

    ShapeEntry* shape = type->shape;
    // Iterate through the shape to find the field
    while (shape) {
        if (shape->name && strview_equal(shape->name, field_name)) {
            return true;
        }
        shape = shape->next;
    }
    return false;
}

Element* elmt_pooled(Pool *pool) {
    Element *elmt = (Element *)pool_calloc(pool, sizeof(Element));
    elmt->type_id = LMD_TYPE_ELEMENT;
    elmt->is_immortal = 1;
    elmt->type = &EmptyElmt;
    return elmt;
}

// Arena-based allocation for MarkBuilder
Element* elmt_arena(Arena* arena) {
    Element *elmt = (Element *)arena_alloc(arena, sizeof(Element));
    if (elmt == NULL) return NULL;
    memset(elmt, 0, sizeof(Element));
    elmt->type_id = LMD_TYPE_ELEMENT;
    elmt->is_immortal = 1;
    elmt->type = &EmptyElmt;
    return elmt;
}

ConstItem Element::get_attr(const Item key) const {
    if (!this || !key.item) { return null_result;}
    bool is_found;
    char *key_str = NULL;
    Target* key_ns = NULL;
    if (key._type_id == LMD_TYPE_STRING) {
        String* str = (String*)key.string_ptr;
        key_str = str->chars;
        // strings don't have namespace
    } else if (key._type_id == LMD_TYPE_SYMBOL) {
        Symbol* sym = (Symbol*)key.symbol_ptr;
        key_str = sym->chars;
        key_ns = sym->ns;
    } else {
        return null_result;  // only string or symbol keys are supported
    }
    return _map_get_const((TypeMap*)this->type, this->data, key_str, &is_found, key_ns);
}

ConstItem Element::get_attr(const char* attr_name) const {
    if (!this || !attr_name) { return null_result;}
    bool is_found;
    // unqualified string lookup - only matches attributes with NULL namespace
    return _map_get_const((TypeMap*)this->type, this->data, attr_name, &is_found, NULL);
}

bool Element::has_attr(const char* attr_name) {
    if (!this || !this->type) return false;

    TypeElmt* type = (TypeElmt*)this->type;
    if (!type->shape) return false;

    ShapeEntry* shape = type->shape;
    // Iterate through the shape to find the attribute
    while (shape) {
        if (shape->name && strview_equal(shape->name, attr_name)) {
            return true;
        }
        shape = shape->next;
    }
    return false;
}

// Phase 14: Deep structural equality for Items
// Used by no-op elision to detect when retransformed output is identical
bool item_deep_equal(Item a, Item b) {
    // identical Item value (same pointer or same packed scalar)
    if (a.item == b.item) return true;

    TypeId ta = get_type_id(a);
    TypeId tb = get_type_id(b);
    if (ta != tb) return false;

    switch (ta) {
        case LMD_TYPE_NULL:
            return true;
        case LMD_TYPE_BOOL:
        case LMD_TYPE_INT:
            // packed in-line — already compared by a.item == b.item
            return false;
        case LMD_TYPE_INT64:
            return a.get_int64() == b.get_int64();
        case LMD_TYPE_FLOAT:
        case LMD_TYPE_FLOAT64:
            return a.get_double() == b.get_double();
        case LMD_TYPE_STRING: {
            String* sa = a.get_safe_string();
            String* sb = b.get_safe_string();
            if (sa == sb) return true;
            if (!sa || !sb) return false;
            if (sa->len != sb->len) return false;
            return memcmp(sa->chars, sb->chars, sa->len) == 0;
        }
        case LMD_TYPE_SYMBOL: {
            Symbol* sa = a.get_safe_symbol();
            Symbol* sb = b.get_safe_symbol();
            if (sa == sb) return true;
            if (!sa || !sb) return false;
            if (sa->len != sb->len) return false;
            return memcmp(sa->chars, sb->chars, sa->len) == 0;
        }
        case LMD_TYPE_ELEMENT: {
            Element* ea = a.element;
            Element* eb = b.element;
            if (ea == eb) return true;
            if (!ea || !eb) return false;

            // compare element tag (TypeElmt name)
            TypeElmt* ta_e = (TypeElmt*)ea->type;
            TypeElmt* tb_e = (TypeElmt*)eb->type;
            if (!ta_e || !tb_e) return false;
            if (ta_e->name.length != tb_e->name.length) return false;
            if (ta_e->name.str != tb_e->name.str &&
                memcmp(ta_e->name.str, tb_e->name.str, ta_e->name.length) != 0) return false;

            // compare shape (field structure)
            if (ta_e->length != tb_e->length) return false;

            // compare attribute data bytes
            if (ta_e->byte_size > 0) {
                if (!ea->data || !eb->data) return false;
                if (memcmp(ea->data, eb->data, ta_e->byte_size) != 0) return false;
            }

            // compare children count
            if (ea->length != eb->length) return false;

            // recursively compare children
            for (int64_t i = 0; i < ea->length; i++) {
                if (!item_deep_equal(ea->items[i], eb->items[i])) return false;
            }
            return true;
        }
        case LMD_TYPE_ARRAY: {
            Array* la = a.array;
            Array* lb = b.array;
            if (la == lb) return true;
            if (!la || !lb) return false;
            if (la->length != lb->length) return false;
            for (int64_t i = 0; i < la->length; i++) {
                if (!item_deep_equal(la->items[i], lb->items[i])) return false;
            }
            return true;
        }
        default:
            // for other types (map, function, etc.), fall back to pointer equality
            return false;
    }
}
