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

// Name pool configuration
#define NAME_POOL_SYMBOL_LIMIT 32  // Max length for symbols in name_pool

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
    LMD_TYPE_ARRAY,  // array of Items
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
typedef struct Decimal Decimal;

/*
* The C verion of Lambda Item and data structures are defined primarily for MIR JIT ciompiler
*/

// only define DateTime if not already defined by lib/datetime.h
#ifndef __cplusplus
typedef uint64_t DateTime;
typedef uint64_t Item;
#else
#include "../lib/datetime.h"
typedef struct Item Item;
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

typedef String Symbol;  // Symbol is just a String
typedef String Binary;  // Binary is just a String

// Array and List struct defintions needed for for-loop
struct Container {
    TypeId type_id;
    union {
        uint8_t flags;
        struct {
            uint8_t is_content:1;  // whether it is a content list, or value list
            // uint8_t is_const:1;  // is a constant expr
            // uint8_t is_pooled:1; // is allocated from a memory pool
            uint8_t reserved:7;
        };
    };
    uint16_t ref_cnt;  // reference count
};

#ifndef __cplusplus
    struct Range {
        TypeId type_id;
        uint8_t flags;
        uint16_t ref_cnt;  // reference count
        //---------------------
        int64_t start;  // inclusive start
        int64_t end;    // inclusive end
        int64_t length;
    };

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

    struct ArrayInt {
        TypeId type_id;
        uint8_t flags;
        uint16_t ref_cnt;  // reference count
        //---------------------
        int64_t* items;  // pointer to int56 values (stored as int64)
        int64_t length;  // number of items
        int64_t extra;   // count of extra items stored at the end of the array
        int64_t capacity;  // allocated capacity
    };

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

#endif

Range* range();
long range_get(Range *range, int index);

List* list();  // constructs an empty list
Item list_fill(List *list, int cnt, ...);  // fill the list with the items
void list_push(List *list, Item item);
Item list_end(List *list);

typedef void* (*fn_ptr)();

// Function as first-class value
// Supports both direct function references and closures (future)
struct Function {
    uint8_t type_id;
    uint8_t arity;        // number of parameters (0-255)
    uint16_t ref_cnt;     // reference count for memory management
    void* fn_type;        // fn type definition (TypeFunc*)
    fn_ptr ptr;           // native function pointer
    void* closure_env;    // closure environment (NULL if no captures)
};

// Dynamic function invocation for first-class functions
Item fn_call(Function* fn, List* args);
Item fn_call0(Function* fn);
Item fn_call1(Function* fn, Item a);
Item fn_call2(Function* fn, Item a, Item b);
Item fn_call3(Function* fn, Item a, Item b, Item c);

#define INT64_ERROR           INT64_MAX
#define LAMBDA_INT64_MAX    (INT64_MAX - 1)

#define ITEM_UNDEFINED      0
#define ITEM_NULL           ((uint64_t)LMD_TYPE_NULL << 56)
#define ITEM_INT            ((uint64_t)LMD_TYPE_INT << 56)
#define ITEM_ERROR          ((uint64_t)LMD_TYPE_ERROR << 56)
#define ITEM_TRUE           ((uint64_t)LMD_TYPE_BOOL << 56) | (uint8_t)1
#define ITEM_FALSE          ((uint64_t)LMD_TYPE_BOOL << 56) | (uint8_t)0

// int56 limits: signed 56-bit integer range
#define INT56_MAX  ((int64_t)0x007FFFFFFFFFFFFF)   // +36,028,797,018,963,967
#define INT56_MIN  ((int64_t)0xFF80000000000000LL) // -36,028,797,018,963,968

inline uint64_t b2it(uint8_t bool_val) {
    return bool_val >= BOOL_ERROR ? ITEM_ERROR : ((((uint64_t)LMD_TYPE_BOOL)<<56) | bool_val);
}
// int56: check range and pack, return ITEM_ERROR on overflow
#ifndef __cplusplus
#define i2it(int_val)        (((int64_t)(int_val) <= INT56_MAX && (int64_t)(int_val) >= INT56_MIN) ? (ITEM_INT | ((uint64_t)(int_val) & 0x00FFFFFFFFFFFFFF)) : ITEM_ERROR)
#else
#define i2it(int_val)        (((int64_t)(int_val) <= INT56_MAX && (int64_t)(int_val) >= INT56_MIN) ? (ITEM_INT | ((uint64_t)(int_val) & 0x00FFFFFFFFFFFFFF)) : ITEM_ERROR)
#endif
#define l2it(long_ptr)       ((long_ptr)? ((((uint64_t)LMD_TYPE_INT64)<<56) | (uint64_t)(long_ptr)): null)
#define d2it(double_ptr)     ((double_ptr)? ((((uint64_t)LMD_TYPE_FLOAT)<<56) | (uint64_t)(double_ptr)): null)
#define c2it(decimal_ptr)    ((decimal_ptr)? ((((uint64_t)LMD_TYPE_DECIMAL)<<56) | (uint64_t)(decimal_ptr)): null)
#define s2it(str_ptr)        ((str_ptr)? ((((uint64_t)LMD_TYPE_STRING)<<56) | (uint64_t)(str_ptr)): null)
#define y2it(sym_ptr)        ((sym_ptr)? ((((uint64_t)LMD_TYPE_SYMBOL)<<56) | (uint64_t)(sym_ptr)): null)
#define x2it(bin_ptr)        ((bin_ptr)? ((((uint64_t)LMD_TYPE_BINARY)<<56) | (uint64_t)(bin_ptr)): null)
#define k2it(dtime_ptr)      ((dtime_ptr)? ((((uint64_t)LMD_TYPE_DTIME)<<56) | (uint64_t)(dtime_ptr)): null)

Array* array_fill(Array* arr, int count, ...);
ArrayInt* array_int_fill(ArrayInt* arr, int count, ...);
ArrayInt64* array_int64_fill(ArrayInt64* arr, int count, ...);
ArrayFloat* array_float_fill(ArrayFloat* arr, int count, ...);

typedef struct Map Map;
Map* map_fill(Map* map, ...);

typedef struct Element Element;
Element* elmt_fill(Element *elmt, ...);

typedef struct Url Url;
typedef struct _ArrayList ArrayList;
typedef struct Pool Pool;

typedef struct Context {
    Pool* pool;
    void** consts;
    Url* cwd;  // current working directory
    void* (*context_alloc)(int size, TypeId type_id);
    bool run_main; // whether to run main procedure on start
    bool disable_string_merging; // disable automatic string merging in list_push
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
    bool it2b(Item item);
    int it2i(Item item);
    String* it2s(Item item);

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

    // vector arithmetic operations (element-wise)
    Item vec_add(Item a, Item b);
    Item vec_sub(Item a, Item b);
    Item vec_mul(Item a, Item b);
    Item vec_div(Item a, Item b);
    Item vec_mod(Item a, Item b);
    Item vec_pow(Item a, Item b);
    
    // vector system functions
    Item fn_prod(Item a);
    Item fn_cumsum(Item a);
    Item fn_cumprod(Item a);
    Item fn_argmin(Item a);
    Item fn_argmax(Item a);
    Item fn_fill(Item n, Item value);
    Item fn_dot(Item a, Item b);
    Item fn_norm(Item a);
    // statistical functions
    Item fn_mean(Item a);
    Item fn_median(Item a);
    Item fn_variance(Item a);
    Item fn_deviation(Item a);
    // element-wise math functions
    Item fn_sqrt(Item a);
    Item fn_log(Item a);
    Item fn_log10(Item a);
    Item fn_exp(Item a);
    Item fn_sin(Item a);
    Item fn_cos(Item a);
    Item fn_tan(Item a);
    Item fn_sign(Item a);
    // vector manipulation functions
    Item fn_reverse(Item a);
    Item fn_sort1(Item a);
    Item fn_sort2(Item a, Item dir);
    Item fn_unique(Item a);
    Item fn_concat(Item a, Item b);
    Item fn_take(Item a, Item n);
    Item fn_drop(Item a, Item n);
    Item fn_slice(Item a, Item start, Item end);
    Item fn_zip(Item a, Item b);
    Item fn_range3(Item start, Item end, Item step);
    Item fn_quantile(Item a, Item p);

    Range* fn_to(Item a, Item b);
    String* fn_string(Item item);
    String *fn_strcat(String *left, String *right);
    Item fn_normalize(Item str, Item type);
    Item fn_substring(Item str, Item start, Item end);
    Item fn_contains(Item str, Item substr);
    Item fn_join(Item a, Item b);

    Function* to_fn(fn_ptr ptr);
    Function* to_fn_n(fn_ptr ptr, int arity);  // create function with arity info
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

    // variadic parameter access
    void set_vargs(List* vargs);  // set current variadic args
    Item fn_varg0();              // varg() - get all variadic args as list
    Item fn_varg1(Item index);    // varg(n) - get nth variadic arg

    // procedural functions
    Item pn_print(Item item);
    Item pn_cmd(Item cmd, Item args);
    Item pn_fetch(Item url, Item options);
    Item pn_output(Item source, Item url, Item format);   // internal: write formatted data to file
    Item pn_output2(Item source, Item url);               // output(source, url) - auto-detect format
    Item pn_output3(Item source, Item url, Item format);  // output(source, url, format) - explicit format

#endif
