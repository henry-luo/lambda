#pragma once
// #include <math.h>  // MIR has problem parsing math.h
// #include <stdint.h>

#if !defined(_STDINT_H) && !defined(_STDINT_H_) && !defined(_STDINT) && !defined(__STDINT_H_INCLUDED)
typedef unsigned char uint8_t;
typedef unsigned short uint16_t;
typedef unsigned int uint32_t;
typedef unsigned long long uint64_t;
typedef signed char int8_t;
typedef short int16_t;
typedef int int32_t;
typedef long long int64_t;
#define INT32_MAX  2147483647
#define INT32_MIN  (-2147483647 - 1)
#endif

#if !defined(__cplusplus) && !defined(_STDBOOL_H) && !defined(_STDBOOL_H_) && !defined(__bool_true_false_are_defined)
#define bool uint8_t
#define true 1
#define false 0
#endif

#define null 0
// #define infinity (1.0 / 0.0)
// #define not_a_number (0.0 / 0.0)

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

// Comparison result enum: 0=false, 1=true, 2=error
typedef enum {
    COMP_FALSE = 0,
    COMP_TRUE = 1,
    COMP_ERROR = 2
} CompResult;

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
        long length;  // number of items
        long extra;   // count of extra items stored at the end of the list
        long capacity;  // allocated capacity
    };
#else
    struct List : Container {
        Item* items;
        long length;
        long extra;  // count of extra items stored at the end of the list
        long capacity;
    };
#endif

List* list();  // constructs an empty list
Item list_fill(List *list, int cnt, ...);  // fill the list with the items
void list_push(List *list, Item item);

#ifndef __cplusplus
    struct ArrayInt {
        TypeId type_id;
        uint8_t flags;
        uint16_t ref_cnt;  // reference count
        //---------------------
        int* items;  // pointer to 32-bit integer items
        long length;  // number of items
        long extra;   // count of extra items stored at the end of the array
        long capacity;  // allocated capacity
    };
#else
    struct ArrayInt : Container {
        int* items;  // 32-bit integer items
        long length;
        long extra;  // count of extra items
        long capacity;
    };
#endif

#ifndef __cplusplus
    struct ArrayInt64 {
        TypeId type_id;
        uint8_t flags;
        uint16_t ref_cnt;  // reference count
        //---------------------
        int64_t* items;  // pointer to 64-bit integer items
        long length;  // number of items
        long extra;   // count of extra items stored at the end of the array
        long capacity;  // allocated capacity
    };
#else
    struct ArrayInt64 : Container {
        int64_t* items;  // 64-bit integer items
        long length;
        long extra;  // count of extra items
        long capacity;
    };
#endif

#ifndef __cplusplus
    struct ArrayFloat {
        TypeId type_id;
        uint8_t flags;
        uint16_t ref_cnt;  // reference count
        //---------------------
        double* items;  // pointer to items
        long length;  // number of items
        long extra;   // count of extra items stored at the end of the array
        long capacity;  // allocated capacity
    };
#else
    struct ArrayFloat : Container {
        double* items;
        long length;
        long extra;  // count of extra items
        long capacity;
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

#define b2it(bool_val)       ((((uint64_t)LMD_TYPE_BOOL)<<56) | (uint8_t)(bool_val))
#define l2it(long_ptr)       ((((uint64_t)LMD_TYPE_INT64)<<56) | (uint64_t)(long_ptr))
#define d2it(double_ptr)     ((((uint64_t)LMD_TYPE_FLOAT)<<56) | (uint64_t)(double_ptr))
#define c2it(decimal_ptr)    ((((uint64_t)LMD_TYPE_DECIMAL)<<56) | (uint64_t)(decimal_ptr))
#define s2it(str_ptr)        ((((uint64_t)LMD_TYPE_STRING)<<56) | (uint64_t)(str_ptr))
#define y2it(sym_ptr)        ((((uint64_t)LMD_TYPE_SYMBOL)<<56) | (uint64_t)(sym_ptr))
#define x2it(bin_ptr)        ((((uint64_t)LMD_TYPE_BINARY)<<56) | (uint64_t)(bin_ptr))
#define k2it(dtime_ptr)      ((((uint64_t)LMD_TYPE_DTIME)<<56) | (uint64_t)(dtime_ptr))
#define r2it(range_ptr)      ((((uint64_t)LMD_TYPE_RANGE)<<56) | (uint64_t)(range_ptr))

Array* array_fill(Array* arr, int count, ...);
ArrayInt* array_int_fill(ArrayInt* arr, int count, ...);
ArrayInt64* array_int64_fill(ArrayInt64* arr, int count, ...);
ArrayFloat* array_float_fill(ArrayFloat* arr, int count, ...);

// void array_push(Array *array, Item item);

typedef struct Map Map;
Map* map_fill(Map* map, ...);

typedef struct Element Element;
Element* elmt_fill(Element *elmt, ...);

typedef struct Heap Heap;
typedef struct Pack Pack;
typedef struct mpd_context_t mpd_context_t;

typedef struct Context {
    Heap* heap;   
    void* ast_pool;
    void** consts;
    void* type_list;
    void* num_stack;  // for long and double pointers
    void* type_info;  // meta info for the base types
    void* cwd;  // current working directory
    Item result; // final exec result
    mpd_context_t* decimal_ctx; // libmpdec context for decimal operations
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
Item list_get(List *list, int index);
Item map_get(Map* map, Item key);
Item elmt_get(Element *elmt, Item key);

bool item_true(Item item);
Item v2it(List *list);

Item push_d(double dval);
Item push_l(long lval);
Item push_k(DateTime dtval);
Item push_c(long cval);

Item safe_b2it(Item item);  // Convert Item to boolean Item, preserving errors
// int overflow check and promotion to decimal
#ifndef __cplusplus
// int overflow check and promotion to decimal
#define i2it(int_val)        ((int_val) <= INT32_MAX && (int_val) >= INT32_MIN ? (ITEM_INT | ((int64_t)(int_val) & 0x00FFFFFFFFFFFFFF)) : push_c(int_val))
#else
#define i2it(int_val)        ((int_val) <= INT32_MAX && (int_val) >= INT32_MIN ? (ITEM_INT | ((int64_t)(int_val) & 0x00FFFFFFFFFFFFFF)) : push_c(int_val).item)
#endif

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
Item fn_min(Item a, Item b);
Item fn_max(Item a, Item b);
Item fn_sum(Item a);
Item fn_avg(Item a);
Item fn_pos(Item a);
Item fn_neg(Item a);
Item fn_eq(Item a, Item b);
Item fn_ne(Item a, Item b);
Item fn_lt(Item a, Item b);
Item fn_gt(Item a, Item b);
Item fn_le(Item a, Item b);
Item fn_ge(Item a, Item b);
Item fn_not(Item a);
Item fn_and(Item a, Item b);
Item fn_or(Item a, Item b);
bool fn_is(Item a, Item b);
bool fn_in(Item a, Item b);
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

Item fn_input(Item url, Item type);
String* fn_format(Item item, Item type);
DateTime fn_datetime();

// procedural functions
void fn_print(Item item);

#endif

Context *rt;

Item main(Context *runtime){
 rt = runtime;
 return ({
 List* ls = list();
 Array* _large_arr64=({ArrayInt64* arr = array_int64(); array_int64_fill(arr,4,fn_int64(const_c2it(0)),fn_int64(const_c2it(1)),fn_int64(const_c2it(2)),fn_int64(const_c2it(3))); });
 Array* _empty64=({Array* arr = array(); array_fill(arr,0); });
 Array* _single64=({ArrayInt64* arr = array_int64(); array_int64_fill(arr,1,fn_int64(const_c2it(6))); });
 Array* _neg_arr64=({ArrayInt64* arr = array_int64(); array_int64_fill(arr,3,fn_int64(i2it(-100)),fn_int64(i2it(-200)),fn_int64(i2it(-300))); });
 list_fill(ls,25,push_l(fn_len(_large_arr64)), fn_sum(_large_arr64), fn_avg(_large_arr64), fn_min(_large_arr64, ITEM_NULL), fn_max(_large_arr64, ITEM_NULL), const_s2it(4), fn_add(({ArrayInt64* arr = array_int64(); array_int64_fill(arr,2,fn_int64(i2it(100)),fn_int64(i2it(200))); }),({ArrayInt64* arr = array_int64(); array_int64_fill(arr,2,fn_int64(i2it(300)),fn_int64(i2it(400))); })), fn_sub(({ArrayInt64* arr = array_int64(); array_int64_fill(arr,2,fn_int64(i2it(500)),fn_int64(i2it(600))); }),({ArrayInt64* arr = array_int64(); array_int64_fill(arr,2,fn_int64(i2it(100)),fn_int64(i2it(200))); })), fn_mul(({ArrayInt64* arr = array_int64(); array_int64_fill(arr,2,fn_int64(i2it(10)),fn_int64(i2it(20))); }),({ArrayInt64* arr = array_int64(); array_int64_fill(arr,2,fn_int64(i2it(5)),fn_int64(i2it(3))); })), const_s2it(5), push_l(fn_len(_empty64)), push_l(fn_len(_single64)), fn_sum(_single64), fn_min(_single64, ITEM_NULL), fn_max(_single64, ITEM_NULL), push_l(fn_int64(i2it(0))), ({ArrayInt64* arr = array_int64(); array_int64_fill(arr,3,fn_int64(i2it(0)),fn_int64(i2it(0)),fn_int64(i2it(0))); }), fn_sum(({ArrayInt64* arr = array_int64(); array_int64_fill(arr,3,fn_int64(i2it(0)),fn_int64(i2it(0)),fn_int64(i2it(0))); })), fn_sum(_neg_arr64), fn_min(_neg_arr64, ITEM_NULL), fn_max(_neg_arr64, ITEM_NULL), const_s2it(7), push_l(((fn_int64(i2it(1000))+500)*fn_int64(i2it(2)))), fn_add(fn_sum(({ArrayInt* arr = array_int(); array_int_fill(arr,3,10,20,30); })),fn_sum(({ArrayInt64* arr = array_int64(); array_int64_fill(arr,2,fn_int64(const_c2it(8)),fn_int64(const_c2it(9))); }))), fn_max(({Array* arr = array(); array_fill(arr,2,fn_min(({ArrayInt64* arr = array_int64(); array_int64_fill(arr,2,fn_int64(i2it(101)),fn_int64(i2it(200))); }), ITEM_NULL), fn_max(({ArrayInt64* arr = array_int64(); array_int64_fill(arr,2,fn_int64(i2it(50)),fn_int64(i2it(151))); }), ITEM_NULL)); }), ITEM_NULL));});
}
