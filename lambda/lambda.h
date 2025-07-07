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
#endif

#ifndef bool
#define bool uint8_t
#endif

#define true 1
#define false 0
#define null 0
#define infinity (1.0 / 0.0)
#define not_a_number (0.0 / 0.0)

enum EnumTypeId {
    LMD_TYPE_RAW_POINTER = 0,
    LMD_TYPE_NULL,

    // scalar types
    LMD_TYPE_BOOL,
    LMD_TYPE_INT,  // implicit int literal, store value up to 32-bit
    LMD_TYPE_INT64,  // explicit int, 64-bit
    LMD_TYPE_FLOAT,  // explicit float, 64-bit
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

# define  LMD_TYPE_CONTAINER LMD_TYPE_LIST

typedef struct Type {
    TypeId type_id;
    uint8_t is_literal:1;  // is a literal value
    uint8_t is_const:1;  // is a constant expr
} Type;

typedef uint64_t Item;

// a fat string with prefixed length and flags
typedef struct String {
    uint32_t len:22;  // string len , up to 4MB;
    uint32_t ref_cnt:10;  // ref_cnt, up to 1024 refs
    char chars[];
} String;

typedef struct Heap Heap;
typedef struct Pack Pack;

typedef struct Context {
    Heap* heap;   
    void* ast_pool;
    void** consts;
    void* type_list;
    void* num_stack;  // for long and double pointers
    void* type_info;  // meta info for the base types
    void* cwd;  // current working directory
    Item result; // final exec result
} Context;

// Array and List struct defintions needed for for-loop
typedef struct Container {
    uint8_t type_id;
    uint8_t flags;
    uint16_t ref_cnt;  // reference count
} Container;

typedef struct Range {
    uint8_t type_id;
    uint8_t flags;
    uint16_t ref_cnt;  // reference count
    // --------
    long start;  // inclusive start
    long end;    // inclusive end
    long length;
} Range;

Range* range();
long range_get(Range *range, int index);

typedef struct List {
    uint8_t type_id;
    uint8_t flags;
    uint16_t ref_cnt;  // reference count
    // --------
    Item* items;
    long length;
    long extra;  // count of extra items stored at the end of the list
    long capacity;
} List;

List* list();  // constructs an empty list
Item list_fill(List *list, int cnt, ...);  // fill the list with the items
void list_push(List *list, Item item);
Item list_get(List *list, int index);

typedef struct List Array;

typedef struct ArrayLong {
    uint8_t type_id;
    uint8_t flags;
    uint16_t ref_cnt;  // reference count
    // --------
    long* items;
    long length;
    long extra;  // count of extra items
    long capacity;
} ArrayLong;

Array* array();
ArrayLong* array_long_new(int count, ...);
Array* array_fill(Array* arr, int count, ...);
// void array_push(Array *array, Item item);
Item array_get(Array *array, int index);

typedef struct Map Map;
Map* map(int type_index);
Map* map_fill(Map* map, ...);
Item map_get(Map* map, char *key);

// Generic field access function
Item field(Item item, long index);

typedef struct Element Element;
Element* elmt(int type_index);
Element* elmt_fill(Element *elmt, ...);

bool item_true(Item item);
Item v2it(List *list);

Item push_d(double dval);
Item push_l(long lval);

#define ITEM_UNDEFINED      0
#define ITEM_NULL           ((uint64_t)LMD_TYPE_NULL << 56)
#define ITEM_INT            ((uint64_t)LMD_TYPE_INT << 56)
#define ITEM_ERROR          ((uint64_t)LMD_TYPE_ERROR << 56)

#define b2it(bool_val)       ((((uint64_t)LMD_TYPE_BOOL)<<56) | (uint8_t)(bool_val))
#define i2it(int_val)        (ITEM_INT | ((int64_t)(int_val) & 0x00FFFFFFFFFFFFFF))
#define l2it(long_ptr)       ((((uint64_t)LMD_TYPE_INT64)<<56) | (uint64_t)(long_ptr))
#define d2it(double_ptr)     ((((uint64_t)LMD_TYPE_FLOAT)<<56) | (uint64_t)(double_ptr))
#define c2it(decimal_ptr)    ((((uint64_t)LMD_TYPE_DECIMAL)<<56) | (uint64_t)(decimal_ptr))
#define s2it(str_ptr)        ((((uint64_t)LMD_TYPE_STRING)<<56) | (uint64_t)(str_ptr))
#define y2it(sym_ptr)        ((((uint64_t)LMD_TYPE_SYMBOL)<<56) | (uint64_t)(sym_ptr))
#define x2it(bin_ptr)        ((((uint64_t)LMD_TYPE_BINARY)<<56) | (uint64_t)(bin_ptr))
#define k2it(dtime_ptr)      ((((uint64_t)LMD_TYPE_DTIME)<<56) | (uint64_t)(dtime_ptr))
#define r2it(range_ptr)      ((((uint64_t)LMD_TYPE_RANGE)<<56) | (uint64_t)(range_ptr))

#define const_d2it(index)    d2it((uint64_t)*(rt->consts + index))
#define const_l2it(index)    l2it((uint64_t)*(rt->consts + index))
#define const_c2it(index)    c2it((uint64_t)*(rt->consts + index))
#define const_s2it(index)    s2it((uint64_t)*(rt->consts + index))
#define const_y2it(index)    y2it((uint64_t)*(rt->consts + index))
#define const_k2it(index)    k2it((uint64_t)*(rt->consts + index))
#define const_x2it(index)    x2it((uint64_t)*(rt->consts + index))

#define const_s(index)      ((String*)*(rt->consts + index))

// item unboxing
long it2l(Item item);
double it2d(Item item);

double pow(double x, double y);
Item add(Item a, Item b);
String *str_cat(String *left, String *right);

typedef void* (*fn_ptr)();
typedef struct Function {
    uint8_t type_id;
    void* fn;  // fn definition, TypeFunc
    fn_ptr ptr;
} Function;

Function* to_fn(fn_ptr ptr);

bool fn_is(Item a, Item b);
bool fn_in(Item a, Item b);
Range* fn_to(Item a, Item b);
String* string(Item item);

Type* base_type(TypeId type_id);
Type* const_type(int type_index);

// returns the type of the item
Type* type(Item item);

Item input(Item url, Item type);
void print(Item item);
String* format(Item item, Item type);