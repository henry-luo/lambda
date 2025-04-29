
#include <stdint.h>
// #include <math.h>  // MIR has problem parsing math.h
#ifndef bool
#define bool uint8_t
#endif
#define true 1
#define false 0
#define null 0

enum TypeId {
    LMD_TYPE_RAW_POINTER = 0,
    LMD_TYPE_NULL,
    LMD_TYPE_ANY,
    LMD_TYPE_ERROR,
    LMD_TYPE_BOOL,
    LMD_TYPE_INT,
    LMD_TYPE_LONG,  // lambda: int
    LMD_TYPE_FLOAT,
    LMD_TYPE_DOUBLE,  // lambda: float
    LMD_TYPE_NUMBER,
    LMD_TYPE_DATE,
    LMD_TYPE_TIME,
    LMD_TYPE_DTIME,
    LMD_TYPE_STRING,
    LMD_TYPE_SYMBOL,
    LMD_TYPE_ARRAY,
    LMD_TYPE_LIST,
    LMD_TYPE_MAP,
    LMD_TYPE_ELEMENT,
    LMD_TYPE_FUNC,
};
typedef uint8_t TypeId;

typedef uint64_t Item;

// a FAT string: null-terminated and prefixed with length
typedef struct String {
    int32_t len;  // int instead of uint, to align with default Lambda int literal type
    char str[];
} String;

String *str_cat(String *left, String *right);

typedef struct Heap Heap;
typedef struct Pack Pack;

// script runtime context
typedef struct Context {
    void* ast_pool;
    void** consts;
    void* type_list;
    Heap* heap;
    Pack* stack;  // eval stack
} Context;

typedef struct Array {
    Item* items;
    int length;
} Array;

typedef struct ArrayInt {
    int* items;
    int length;
} ArrayInt;

Array* array_new(int count, ...);
ArrayInt* array_int_new(int count, ...);

// for for-expr result
typedef struct List {
    uint64_t type_id:8;
    uint64_t capacity:56;
    Item* items;
    int length;
} List;

typedef struct ListInt {
    int* items;
    int length;
    int capacity;
} ListInt;
List* list();  // constructs an empty list
List* list_new(Context *rt, int cnt, ...);  // constructs an empty list
void list_push(List *list, Item item);
ListInt* list_int();  // construct an empty list
void list_int_push(ListInt *list, int item);

typedef struct Map {
    void* ast;  // ast node of the map
    void* data;  // packed data struct of the map
} Map;
Map* map();  // constructs an empty map
Map* map_new(Context *rt, int type_index, ...);
Item map_get(Context *rt, Map* map, char *key);

bool item_true(Item item);
Item v2it(List *list);

Item push_d(Context *rt, double dval);

#define ITEM_NULL           ((uint64_t)LMD_TYPE_NULL << 56)
#define ITEM_INT            ((uint64_t)LMD_TYPE_INT << 56)
#define ITEM_ERROR          ((uint64_t)LMD_TYPE_ERROR << 56)

#define b2it(bool_val)       ((((uint64_t)LMD_TYPE_BOOL)<<56) | (uint8_t)(bool_val))
#define i2it(int_val)        (ITEM_INT | (uint32_t)(int_val))
#define s2it(str_ptr)        ((((uint64_t)LMD_TYPE_STRING)<<56) | (uint64_t)(str_ptr))
#define y2it(sym_ptr)        ((((uint64_t)LMD_TYPE_SYMBOL)<<56) | (uint64_t)(sym_ptr))
#define d2it(double_ptr)     ((((uint64_t)LMD_TYPE_DOUBLE)<<56) | (uint64_t)(double_ptr))
#define k2it(dtime_ptr)      ((((uint64_t)LMD_TYPE_DTIME)<<56) | (uint64_t)(dtime_ptr))

#define const_d2it(index)    d2it((uint64_t)*(rt->consts + index))
#define const_s2it(index)    s2it((uint64_t)*(rt->consts + index))
#define const_y2it(index)    y2it((uint64_t)*(rt->consts + index))
#define const_k2it(index)    k2it((uint64_t)*(rt->consts + index))

#define const_s(index)      ((String*)*(rt->consts + index))

double pow(double x, double y);
Item add(Item a, Item b);
