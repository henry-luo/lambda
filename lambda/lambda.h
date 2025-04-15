#include <stdio.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdarg.h>
#define null 0

typedef void* Item;
typedef uint64_t Handle;

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
    Item* items;
    int length;
    int capacity;
} List;
typedef struct ListLong {
    long* items;
    int length;
    int capacity;
} ListLong;
List* list();  // constructs an empty list
void list_push(List *list, Item item);
ListLong* list_long();  // construct an empty list
void list_long_push(ListLong *list, long item);

// script runtime context
typedef struct Context {
    void* ast_pool;
    void* type_list;
    void* heap;
} Context;

typedef struct Map {
    void* ast;  // ast node of the map
    void* data;  // packed data struct of the map
} Map;
Map* map();  // constructs an empty map
Map* map_new(Context *rt, int type_index, ...);

