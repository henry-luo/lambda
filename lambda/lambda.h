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

#ifndef __cplusplus
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
typedef struct ArrayLong ArrayLong;
typedef struct Map Map;
typedef struct Element Element;
typedef struct Function Function;
typedef struct DateTime DateTime;

/*
# Lambda Runtime Item

Lambda runtime uses the following to represent its runtime data:
- for simple scalar types: LMD_TYPE_NULL, LMD_TYPE_BOOL, LMD_TYPE_INT
	- they are packed into Item, with high bits set to TypeId;
- for compound scalar types: LMD_TYPE_INT64, LMD_TYPE_FLOAT, LMD_TYPE_DECIMAL, LMD_TYPE_DTIME, LMD_TYPE_SYMBOL, LMD_TYPE_STRING, LMD_TYPE_BINARY
	- they are packed into item as a tagged pointer. It's a pointer to the actual data, with high bits set to TypeId.
- for container types: LMD_TYPE_LIST, LMD_TYPE_RANGE, LMD_TYPE_ARRAY_INT, LMD_TYPE_ARRAY, LMD_TYPE_MAP, LMD_TYPE_ELEMENT
	- they are direct/raw pointers to the container data.
	- all containers extends struct Container, that starts with field TypeId;
- can use get_type_id() function to get the TypeId of an Item in a general manner;
- Lambda map/LMD_TYPE_MAP, uses a packed struct:
	- its list of fields are defined as a linked list of ShapeEntry;
	- and the actual data are stored as a packed struct;
- Lambda element/LMD_TYPE_ELEMENT, extends Lambda list/LMD_TYPE_LIST, and it's also a map/LMD_TYPE_MAP at the same time;
	- note that it can be casted as List directly, but not as Map directly;
*/

#ifndef __cplusplus
typedef uint64_t Item;
#else
// uses the high byte to tag the pointer, defined for little-endian
typedef union Item {
    struct {
        union {
            struct {
                int int_val: 32;
                uint32_t _32: 32;
            };            
            struct {
                uint64_t long_val: 56;
                uint64_t _8: 8;
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
    ArrayLong* array_long;
    Map* map;
    Element* element;
    Type* type;
    Function* function;
} Item;

extern Item ItemNull;
extern Item ItemError;

#endif

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
Item list_get(List *list, int index);

#ifndef __cplusplus
    struct ArrayLong {
        TypeId type_id;
        uint8_t flags;
        uint16_t ref_cnt;  // reference count
        //---------------------
        long* items;  // pointer to items
        long length;  // number of items
        long extra;   // count of extra items stored at the end of the array
        long capacity;  // allocated capacity
    };
#else
    struct ArrayLong : Container {
        long* items;
        long length;
        long extra;  // count of extra items
        long capacity;
    };
#endif

Array* array();
ArrayLong* array_long_new(int count, ...);
Array* array_fill(Array* arr, int count, ...);
// void array_push(Array *array, Item item);
Item array_get(Array *array, int index);

typedef struct Map Map;
Map* map(int type_index);
Map* map_fill(Map* map, ...);
Item map_get(Map* map, Item key);

// generic field access function
Item fn_index(Item item, Item index);
Item fn_member(Item item, Item key);

// length function
Item fn_len(Item item);

typedef struct Element Element;
Element* elmt(int type_index);
Element* elmt_fill(Element *elmt, ...);
Item elmt_get(Element *elmt, Item key);

bool item_true(Item item);
Item v2it(List *list);

Item push_d(double dval);
Item push_l(long lval);

#define ITEM_UNDEFINED      0
#define ITEM_NULL           ((uint64_t)LMD_TYPE_NULL << 56)
#define ITEM_INT            ((uint64_t)LMD_TYPE_INT << 56)
#define ITEM_ERROR          ((uint64_t)LMD_TYPE_ERROR << 56)

#define b2it(bool_val)       ((((uint64_t)LMD_TYPE_BOOL)<<56) | (uint8_t)(bool_val))
// todo: int overflow check and promotion to decimal
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
#define const_k(index)      ((DateTime*)*(rt->consts + index))

// item unboxing
long it2l(Item item);
double it2d(Item item);

double pow(double x, double y);
Item add(Item a, Item b);
String *str_cat(String *left, String *right);

typedef void* (*fn_ptr)();
struct Function {
    uint8_t type_id;
    void* fn;  // fn definition, TypeFunc
    fn_ptr ptr;
};

Function* to_fn(fn_ptr ptr);

bool fn_is(Item a, Item b);
bool fn_in(Item a, Item b);
Range* fn_to(Item a, Item b);
String* fn_string(Item item);

Type* base_type(TypeId type_id);
Type* const_type(int type_index);

// returns the type of the item
Type* fn_type(Item item);

Item fn_input(Item url, Item type);
void fn_print(Item item);
String* fn_format(Item item, Item type);