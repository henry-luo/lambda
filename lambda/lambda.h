
#pragma once
// #include <math.h>  // MIR has problem parsing math.h
// #include <stdint.h>
typedef unsigned long long uint64_t;
typedef unsigned char uint8_t;
typedef int int32_t;
typedef long long int64_t;
#ifndef bool
#define bool uint8_t
#endif
#define true 1
#define false 0
#define null 0
#define infinity (1.0 / 0.0)
#define not_a_number (0.0 / 0.0)

enum TypeId {
    LMD_TYPE_RAW_POINTER = 0,
    LMD_TYPE_NULL,
    LMD_TYPE_BOOL,
    LMD_TYPE_IMP_INT,  // implicit int, 56-bit
    LMD_TYPE_INT,  // lambda: explicit int, 64-bit
    LMD_TYPE_FLOAT,  // lambda: explicit float, 64-bit
    LMD_TYPE_DECIMAL,
    LMD_TYPE_NUMBER,  // lambda: explicit number, which includes decimal
    LMD_TYPE_DTIME,
    LMD_TYPE_STRING,
    LMD_TYPE_SYMBOL,
    LMD_TYPE_BINARY,
    LMD_TYPE_ARRAY,
    LMD_TYPE_ARRAY_INT,
    LMD_TYPE_LIST,
    LMD_TYPE_MAP,
    LMD_TYPE_ELEMENT,
    LMD_TYPE_TYPE,
    LMD_TYPE_FUNC,
    LMD_TYPE_ANY,
    LMD_TYPE_ERROR,
};
typedef uint8_t TypeId;

typedef uint64_t Item;

// a fat string with prefixed length and flags
typedef struct String {
    int32_t len:30;  // int instead of uint, to align with default Lambda int literal type
    int32_t heap_owned:1;  // whether it is owned by the heap
    int32_t contained:1;  // whether it is a reference to a string in containers
    char chars[];
} String;

typedef struct Heap Heap;
typedef struct Pack Pack;

typedef struct Context {
    void* ast_pool;
    void** consts;
    void* type_list;
    Heap* heap;
    Pack* stack;  // eval stack
} Context;

// Array and List struct defintions needed for for-loop

typedef struct Array {
    uint64_t type_id:8;
    uint64_t capacity:56;    
    Item* items;
    long length;
} Array;

typedef struct ArrayLong {
    uint64_t type_id:8;
    uint64_t capacity:56;    
    long* items;
    long length;
} ArrayLong;

Array* array();
Array* array_fill(Array* arr, int count, ...);
ArrayLong* array_long_new(int count, ...);

typedef struct List {
    uint64_t type_id:8;
    uint64_t capacity:56;
    Item* items;
    long length;
} List;

List* list();  // constructs an empty list
List* list_fill(List *list, int cnt, ...);  // fill the list with the items
void list_push(List *list, Item item);
Item list_get(List *list, int index);

typedef struct Map Map;
Map* map();
Map* map_fill(Map* map, int type_index, ...);
Item map_get(Map* map, char *key);

typedef struct Element Element;
Element* elmt();
Element* elmt_fill(Element *elmt, int type_index, ...);

bool item_true(Item item);
Item v2it(List *list);

Item push_d(double dval);
Item push_l(long lval);

#define ITEM_NULL           ((uint64_t)LMD_TYPE_NULL << 56)
#define ITEM_IMP_INT        ((uint64_t)LMD_TYPE_IMP_INT << 56)
#define ITEM_ERROR          ((uint64_t)LMD_TYPE_ERROR << 56)

#define b2it(bool_val)       ((((uint64_t)LMD_TYPE_BOOL)<<56) | (uint8_t)(bool_val))
#define i2it(int_val)        (ITEM_IMP_INT | ((int64_t)(int_val) & 0x00FFFFFFFFFFFFFF))
#define l2it(long_ptr)       ((((uint64_t)LMD_TYPE_INT)<<56) | (uint64_t)(long_ptr))
#define d2it(double_ptr)     ((((uint64_t)LMD_TYPE_FLOAT)<<56) | (uint64_t)(double_ptr))
#define s2it(str_ptr)        ((((uint64_t)LMD_TYPE_STRING)<<56) | (uint64_t)(str_ptr))
#define y2it(sym_ptr)        ((((uint64_t)LMD_TYPE_SYMBOL)<<56) | (uint64_t)(sym_ptr))
#define x2it(bin_ptr)        ((((uint64_t)LMD_TYPE_BINARY)<<56) | (uint64_t)(bin_ptr))
#define k2it(dtime_ptr)      ((((uint64_t)LMD_TYPE_DTIME)<<56) | (uint64_t)(dtime_ptr))

#define const_d2it(index)    d2it((uint64_t)*(rt->consts + index))
#define const_l2it(index)    l2it((uint64_t)*(rt->consts + index))
#define const_s2it(index)    s2it((uint64_t)*(rt->consts + index))
#define const_y2it(index)    y2it((uint64_t)*(rt->consts + index))
#define const_k2it(index)    k2it((uint64_t)*(rt->consts + index))
#define const_x2it(index)    x2it((uint64_t)*(rt->consts + index))

#define const_s(index)      ((String*)*(rt->consts + index))

long it2l(Item item);
double it2d(Item item);

double pow(double x, double y);
Item add(Context *rt, Item a, Item b);
String *str_cat(String *left, String *right);

typedef void* (*fn_ptr)();
typedef struct Function {
    uint8_t type_id;
    void* fn;  // fn definition, LambdaTypeFunc
    fn_ptr ptr;
} Function;

Function* to_fn(fn_ptr ptr);

typedef struct LambdaType {
    TypeId type_id;
    uint8_t is_literal:1;  // is a literal value
    uint8_t is_const:1;  // is a constant expr
} LambdaType;

LambdaType *type_int();
LambdaType *type_float();
LambdaType *type_string();

bool is(Item a, Item b);
bool in(Item a, Item b);
String* string(Context *rt, Item item);
LambdaType* type(Context *rt, Item item);