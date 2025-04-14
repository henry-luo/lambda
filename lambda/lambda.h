#include <stdio.h>
#include <stdbool.h>
#include <stdlib.h>
#include <stdarg.h>
#define null 0
typedef void* Item;

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

typedef struct Map {
    void* shape;  // data shape of the map
    void* data;  // packed data struct of the map
} Map;
Map* map();  // constructs an empty list
Map* map_new(int count, ...);