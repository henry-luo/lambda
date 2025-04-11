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

Array* array(int count, ...);
ArrayLong* array_long(int count, ...);

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
List* list();  // construct an empty list
void list_push(List *list, Item item);
ListLong* list_long();  // construct an empty list
void list_long_push(ListLong *list, long item);