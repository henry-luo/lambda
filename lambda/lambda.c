#ifndef JIT_MODE
#include "lambda.h"
#endif

Array* array_new(int count, ...) {
    if (count <= 0) { return NULL; }
    va_list args;
    va_start(args, count);
    Array *arr = malloc(sizeof(Array));
    arr->items = malloc(count * sizeof(Item));
    for (int i = 0; i < count; i++) {
        arr->items[i] = va_arg(args, Item);
    }
    arr->length = count;
    va_end(args);
    return arr;
}

ArrayLong* array_long_new(int count, ...) {
    if (count <= 0) { return NULL; }
    va_list args;
    va_start(args, count);
    ArrayLong *arr = malloc(sizeof(ArrayLong));
    arr->items = malloc(count * sizeof(long));
    arr->length = count;
    for (int i = 0; i < count; i++) {
        arr->items[i] = va_arg(args, long);
    }       
    va_end(args);
    return arr;
}

List* list() {
    List *list = malloc(sizeof(List));
    list->items = NULL;
    list->length = 0;
    list->capacity = 0;
    return list;
}
void list_push(List *list, Item item) {
    if (list->length >= list->capacity) {
        list->capacity = list->capacity ? list->capacity * 2 : 1;
        list->items = realloc(list->items, list->capacity * sizeof(Item));
    }
    list->items[list->length++] = item;
}
ListLong* list_long() {
    ListLong *list = malloc(sizeof(ListLong));
    list->items = NULL;
    list->length = 0;
    list->capacity = 0;
    return list;
}
void list_long_push(ListLong *list, long item) {
    if (list->length >= list->capacity) {
        list->capacity = list->capacity ? list->capacity * 2 : 1;
        list->items = realloc(list->items, list->capacity * sizeof(long));
    }
    list->items[list->length++] = item;
}

Map* map() {
    Map *map = malloc(sizeof(Map));
    map->shape = NULL;
    map->data = NULL;
    return map;
}
Map* map_new(int count, ...) {
    if (count <= 0) { return NULL; }
    va_list args;
    va_start(args, count);
    Map *map = malloc(sizeof(Map));
    // map->shape = malloc(count * sizeof(void*));
    // for (int i = 0; i < count; i++) {
    //     map->shape[i] = va_arg(args, void*);
    // }
    map->shape = NULL;  // Placeholder for shape
    map->data = NULL;
    va_end(args);
    return map;
}