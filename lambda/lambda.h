#include <stdio.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdarg.h>
#define null 0

typedef enum TypeId {
    LMD_RAW_POINTER = 0,
    LMD_TYPE_NULL,
    LMD_TYPE_ANY,
    LMD_TYPE_ERROR,
    LMD_TYPE_BOOL,
    LMD_TYPE_INT,
    LMD_TYPE_FLOAT,
    LMD_TYPE_DOUBLE,
    LMD_TYPE_STRING,
    LMD_TYPE_ARRAY,
    LMD_TYPE_LIST,
    LMD_TYPE_MAP,
    LMD_TYPE_ELEMENT,
    LMD_TYPE_FUNC,
} TypeId;

typedef uint64_t Item;

// script runtime context
typedef struct Heap Heap;
typedef struct Context {
    void* ast_pool;
    void** consts;
    void* type_list;
    Heap* heap;
} Context;

typedef struct Array {
    Item* items;
    int length;
} Array;

typedef struct ArrayLong {
    long* items;
    int length;
} ArrayLong;

Array* array_new(int count, ...);
ArrayLong* array_long_new(int count, ...);

// for for-expr result
typedef struct List {
    uint64_t type_id:8;
    uint64_t capacity:56;
    Item* items;
    int length;
} List;

typedef struct ListLong {
    long* items;
    int length;
    int capacity;
} ListLong;
List* list();  // constructs an empty list
List* list_new(Context *rt, int cnt, ...);  // constructs an empty list
void list_push(List *list, Item item);
ListLong* list_long();  // construct an empty list
void list_long_push(ListLong *list, long item);

typedef struct Map {
    void* ast;  // ast node of the map
    void* data;  // packed data struct of the map
} Map;
Map* map();  // constructs an empty map
Map* map_new(Context *rt, int type_index, ...);
Item map_get(Context *rt, Map* map, char *key);

bool item_true(Item item);
Item v2x(List *list);

#define b2x(bool_val)       ((((uint64_t)LMD_TYPE_BOOL)<<56) | (bool_val))
#define i2x(int_val)        ((((uint64_t)LMD_TYPE_INT)<<56) | (int_val))
#define s2x(str_ptr)        ((((uint64_t)LMD_TYPE_STRING)<<56) | (str_ptr))
#define d2x(double_ptr)     ((((uint64_t)LMD_TYPE_DOUBLE)<<56) | (double_ptr))
#define const_d2x(index)    d2x((uint64_t)*(rt->consts + index))