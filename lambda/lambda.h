#pragma once
// #include <math.h>  // MIR has problem parsing math.h

// Include standard integer types from system
#include <stdint.h>

#if !defined(__cplusplus) && !defined(_STDBOOL_H) && !defined(_STDBOOL_H_) && !defined(__bool_true_false_are_defined)
#define bool uint8_t
#define true 1
#define false 0
#endif

#define null 0

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
    LMD_TYPE_ARRAY,
    LMD_TYPE_MAP,
    LMD_TYPE_ELEMENT,
    LMD_TYPE_TYPE,
    LMD_TYPE_FUNC,

    LMD_TYPE_ANY,
    LMD_TYPE_ERROR,
    LMD_CONTAINER_HEAP_START, // special value for container heap entry start
};
typedef uint8_t TypeId;

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
// Only define DateTime if not already defined by lib/datetime.h
#ifndef _DATETIME_DEFINED_
#ifdef __cplusplus
#include "../lib/datetime.h"
#else
typedef uint64_t DateTime;
#endif
#define _DATETIME_DEFINED_
#endif
typedef struct Decimal Decimal;

#ifndef __cplusplus
typedef uint64_t Item;
#else
// uses the high byte to tag the pointer, defined for little-endian
typedef union Item {
    struct {
        union {
            struct {
                int int_val: 32;
                uint32_t _24: 24;
                uint32_t _type: 8;
            };
            struct {
                uint64_t bool_val: 8;
                uint64_t _56: 56;
            };
            struct {
                uint64_t pointer : 56;  // tagged pointer for long, double, string, symbol, dtime, binary
                uint64_t type_id : 8;        
            };           
        };
    };
    uint64_t item;
    void* raw_pointer;

    // pointers to the container types
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
} Item;

extern Item ItemNull;
extern Item ItemError;
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

// Array and List struct defintions needed for for-loop
struct Container {
    TypeId type_id;
    uint8_t flags;
    uint16_t ref_cnt;  // reference count
};

#ifndef __cplusplus
    struct Range {
        TypeId type_id;
        uint8_t flags;
        uint16_t ref_cnt;  // reference count
        //---------------------    
        long start;  // inclusive start
        long end;    // inclusive end
        long length;
    };
#else
    struct Range : Container {
        long start;  // inclusive start
        long end;    // inclusive end
        long length;
    };
#endif

Range* range();
long range_get(Range *range, int index);

#ifndef __cplusplus
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
#else
    struct List : Container {
        Item* items;
        int64_t length;
        int64_t extra;  // count of extra items stored at the end of the list
        int64_t capacity;
    };
#endif

List* list();  // constructs an empty list
Item list_fill(List *list, int cnt, ...);  // fill the list with the items
void list_push(List *list, Item item);
Item list_end(List *list);

#ifndef __cplusplus
    struct ArrayInt {
        TypeId type_id;
        uint8_t flags;
        uint16_t ref_cnt;  // reference count
        //---------------------
        int* items;  // pointer to 32-bit integer items
        int64_t length;  // number of items
        int64_t extra;   // count of extra items stored at the end of the array
        int64_t capacity;  // allocated capacity
    };
#else
    struct ArrayInt : Container {
        int* items;  // 32-bit integer items
        int64_t length;
        int64_t extra;  // count of extra items
        int64_t capacity;
    };
#endif

#ifndef __cplusplus
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
#else
    struct ArrayInt64 : Container {
        int64_t* items;  // 64-bit integer items
        int64_t length;
        int64_t extra;  // count of extra items
        int64_t capacity;
    };
#endif

#ifndef __cplusplus
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
#else
    struct ArrayFloat : Container {
        double* items;
        int64_t length;
        int64_t extra;  // count of extra items
        int64_t capacity;
    };
#endif

typedef void* (*fn_ptr)();
struct Function {
    uint8_t type_id;
    void* fn;  // fn definition, TypeFunc
    fn_ptr ptr;
};

#define INT_ERROR           INT64_MAX
#define INT_MAX             INT_MAX
#define LAMBDA_INT64_MAX    (INT64_MAX - 1)

#define ITEM_UNDEFINED      0
#define ITEM_NULL           ((uint64_t)LMD_TYPE_NULL << 56)
#define ITEM_INT            ((uint64_t)LMD_TYPE_INT << 56)
#define ITEM_ERROR          ((uint64_t)LMD_TYPE_ERROR << 56)
#define ITEM_TRUE           ((uint64_t)LMD_TYPE_BOOL << 56) | (uint8_t)1
#define ITEM_FALSE          ((uint64_t)LMD_TYPE_BOOL << 56) | (uint8_t)0

inline uint64_t b2it(uint8_t bool_val) {
    return bool_val >= BOOL_ERROR ? ITEM_ERROR : ((((uint64_t)LMD_TYPE_BOOL)<<56) | bool_val);
}
// int overflow check and promotion to double
#ifndef __cplusplus
#define i2it(int_val)        ((int_val) <= INT32_MAX && (int_val) >= INT32_MIN ? (ITEM_INT | ((int64_t)(int_val) & 0x00FFFFFFFFFFFFFF)) : push_d(int_val))
#else
#define i2it(int_val)        ((int_val) <= INT32_MAX && (int_val) >= INT32_MIN ? (ITEM_INT | ((int64_t)(int_val) & 0x00FFFFFFFFFFFFFF)) : push_d(int_val).item)
#endif
#define l2it(long_ptr)       ((long_ptr)? ((((uint64_t)LMD_TYPE_INT64)<<56) | (uint64_t)(long_ptr)): null)
#define d2it(double_ptr)     ((double_ptr)? ((((uint64_t)LMD_TYPE_FLOAT)<<56) | (uint64_t)(double_ptr)): null)
#define c2it(decimal_ptr)    ((decimal_ptr)? ((((uint64_t)LMD_TYPE_DECIMAL)<<56) | (uint64_t)(decimal_ptr)): null)
#define s2it(str_ptr)        ((str_ptr)? ((((uint64_t)LMD_TYPE_STRING)<<56) | (uint64_t)(str_ptr)): null)
#define y2it(sym_ptr)        ((sym_ptr)? ((((uint64_t)LMD_TYPE_SYMBOL)<<56) | (uint64_t)(sym_ptr)): null)
#define x2it(bin_ptr)        ((bin_ptr)? ((((uint64_t)LMD_TYPE_BINARY)<<56) | (uint64_t)(bin_ptr)): null)
#define k2it(dtime_ptr)      ((dtime_ptr)? ((((uint64_t)LMD_TYPE_DTIME)<<56) | (uint64_t)(dtime_ptr)): null)
#define r2it(range_ptr)      ((range_ptr)? ((((uint64_t)LMD_TYPE_RANGE)<<56) | (uint64_t)(range_ptr)): null)

Array* array_fill(Array* arr, int count, ...);
ArrayInt* array_int_fill(ArrayInt* arr, int count, ...);
ArrayInt64* array_int64_fill(ArrayInt64* arr, int count, ...);
ArrayFloat* array_float_fill(ArrayFloat* arr, int count, ...);

typedef struct Map Map;
Map* map_fill(Map* map, ...);

typedef struct Element Element;
Element* elmt_fill(Element *elmt, ...);

typedef struct Heap Heap;
typedef struct Pack Pack;
typedef struct mpd_context_t mpd_context_t;
typedef struct num_stack_t num_stack_t;
typedef struct Url Url;
typedef struct _ArrayList ArrayList;
typedef struct VariableMemPool VariableMemPool;

typedef struct Context {
    Heap* heap;   
    VariableMemPool* ast_pool;
    void** consts;
    ArrayList* type_list;
    num_stack_t* num_stack;  // for long and double pointers
    void* type_info;  // meta info for the base types
    Url* cwd;  // current working directory
    Item result; // final exec result
    mpd_context_t* decimal_ctx; // libmpdec context for decimal operations
    bool run_main; // whether to run main procedure on start
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

Bool is_truthy(Item item);
Item v2it(List *list);

Item push_d(double dval);
Item push_l(int64_t lval);
Item push_k(DateTime dtval);
Item push_c(int64_t cval);

#define const_d2it(index)    d2it((uint64_t)*(rt->consts + index))
#define const_l2it(index)    l2it((uint64_t)*(rt->consts + index))
#define const_c2it(index)    c2it((uint64_t)*(rt->consts + index))
#define const_s2it(index)    s2it((uint64_t)*(rt->consts + index))
#define const_y2it(index)    y2it((uint64_t)*(rt->consts + index))
#define const_k2it(index)    k2it((uint64_t)*(rt->consts + index))
#define const_x2it(index)    x2it((uint64_t)*(rt->consts + index))

#define const_s(index)      ((String*)rt->consts[index])
#define const_c(index)      ((Decimal*)rt->consts[index])
#define const_k(index)      (*(DateTime*)rt->consts[index])

// item unboxing
int64_t it2l(Item item);
double it2d(Item item);

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

Range* fn_to(Item a, Item b);
String* fn_string(Item item);
String *fn_strcat(String *left, String *right);
Item fn_normalize(Item str, Item type);
Item fn_substring(Item str, Item start, Item end);
Item fn_contains(Item str, Item substr);

Function* to_fn(fn_ptr ptr);
Type* base_type(TypeId type_id);
Type* const_type(int type_index);

// returns the type of the item
Type* fn_type(Item item);

Item fn_input1(Item url);
Item fn_input2(Item url, Item options);
String* fn_format1(Item item);
String* fn_format2(Item item, Item options);
Item fn_fetch(Item url, Item options);

Item fn_typeset_latex(Item input_file, Item output_file, Item options);
DateTime fn_datetime();

// procedural functions
Item pn_print(Item item);
Item pn_cmd(Item cmd, Item args);
Item pn_fetch(Item url, Item options);

#endif
