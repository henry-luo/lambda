#include "transpiler.h"
#include <stdarg.h>

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

ArrayInt* array_int_new(int count, ...) {
    if (count <= 0) { return NULL; }
    va_list args;
    va_start(args, count);
    ArrayInt *arr = malloc(sizeof(ArrayInt));
    arr->items = malloc(count * sizeof(int));
    arr->length = count;
    for (int i = 0; i < count; i++) {
        arr->items[i] = va_arg(args, int);
    }       
    va_end(args);
    return arr;
}

List* list() {
    // todo: alloc from heap
    List *list = calloc(1, sizeof(List));
    list->type_id = LMD_TYPE_LIST;
    return list;
}
void list_push(List *list, Item item) {
    printf("push: list %p, item: %llu\n", list, item);
    if (list->length >= list->capacity) {
        list->capacity = list->capacity ? list->capacity * 2 : 1;
        list->items = realloc(list->items, list->capacity * sizeof(Item));
    }
    list->items[list->length++] = item;
}

List* list_new(Context *rt, int count, ...) {
    printf("list_new cnt: %d\n", count);
    List *list = calloc(1, sizeof(List));
    list->type_id = LMD_TYPE_LIST;
    va_list args;
    va_start(args, count);
    for (int i = 0; i < count; i++) {
        Item item = va_arg(args, uint64_t);
        list_push(list, item);
    }
    va_end(args);
    return list;
}

ListInt* list_int() {
    ListInt *list = malloc(sizeof(ListInt));
    list->items = NULL;
    list->length = 0;
    list->capacity = 0;
    return list;
}
void list_int_push(ListInt *list, int item) {
    if (list->length >= list->capacity) {
        list->capacity = list->capacity ? list->capacity * 2 : 1;
        list->items = realloc(list->items, list->capacity * sizeof(long));
    }
    list->items[list->length++] = item;
}

Map* map() {
    Map *map = malloc(sizeof(Map));
    map->ast = NULL;
    map->data = NULL;
    return map;
}
Map* map_new(Context *rt, int type_index, ...) {
    printf("map_new %p at %d\n", rt, type_index);
    ArrayList* type_list = (ArrayList*)rt->type_list;
    AstMapNode* node = (AstMapNode*)((AstNode*)type_list->data[type_index]);
    printf("map node: %p, type: %p, node_type: %d\n", node, node->type, node->node_type);
    Map *map = malloc(sizeof(Map));
    map->ast = node;
    map->data = heap_alloc(rt->heap, ((LambdaTypeMap*)node->type)->byte_size);
    printf("map data: %p, byte_size: %d\n", map->data, ((LambdaTypeMap*)node->type)->byte_size);

    // set the fields
    LambdaTypeMap *map_type = (LambdaTypeMap*)node->type;
    printf("map type: %d\n", map_type->type_id);
    int count = map_type->length;
    printf("map length: %d\n", count);
    va_list args;  ShapeEntry *field = map_type->shape;
    printf("map field: %p\n", field);
    va_start(args, count);
    for (int i = 0; i < count; i++) {
        printf("field type: %d, offset: %d\n", field->type->type_id, field->byte_offset);
        void* field_ptr = (char*)map->data + field->byte_offset;
        switch (field->type->type_id) {
            case LMD_TYPE_INT:
                *(long*)field_ptr = va_arg(args, long);
                break;
            case LMD_TYPE_FLOAT:
                *(double*)field_ptr = va_arg(args, double);
                break;
            case LMD_TYPE_STRING:
                *(char**)field_ptr = va_arg(args, char*);
                break;
            case LMD_TYPE_BOOL:
                *(bool*)field_ptr = va_arg(args, bool);
                break;
            default:
                printf("unknown type %d\n", field->type->type_id);
        }
        field = field->next;
    }
    va_end(args);
    return map;
}

Item map_get(Context *rt, Map* map, char *key) {
    if (!rt || !map || !key) { return ITEM_NULL; }
    ShapeEntry *field = ((LambdaTypeMap*)((AstMapNode*)map->ast)->type)->shape;
    while (field) {
        if (strncmp(field->name.str, key, field->name.length) == 0) {
            TypeId type_id = field->type->type_id;
            void* field_ptr = (char*)map->data + field->byte_offset;
            switch (type_id) {
                case LMD_TYPE_BOOL:
                    return b2x(*(bool*)field_ptr);
                case LMD_TYPE_INT:
                    return i2x(*(int*)field_ptr);
                // case LMD_TYPE_FLOAT:
                //     return &(Item){.type_id = LMD_TYPE_FLOAT, .double_val = *(double*)field_ptr};
                case LMD_TYPE_STRING:
                    return s2x(*(char*)field_ptr);
                default:
                    printf("unknown type %d\n", type_id);
            }
        }
        field = field->next;
    }
    printf("key %s not found\n", key);
    return ITEM_NULL;
}

bool item_true(Item itm) {
    LambdaItem item = {.item = itm};
    printf("item type: %d, val: %llu\n", item.type_id, item.pointer);
    switch (item.type_id) {
    case LMD_TYPE_NULL:
        return false;
    case LMD_TYPE_ERROR:
        return false;
    case LMD_TYPE_BOOL:
        return item.bool_val;
    default:
        return true;
    }
}

// list to item
Item v2x(List* list) {
    printf("v2x %p, length: %d\n", list, list->length);
    if (list->length == 0) { return ITEM_NULL; }
    if (list->length == 1) { return list->items[0]; }
    return (Item)list;
}

Item push_d(Context *rt, double dval) {
    printf("push_d: %g\n", dval);
    double *dptr = pack_alloc(rt->stack, sizeof(double));
    *dptr = dval;
    return (Item) d2x(dptr);
}

const char *str_cat(const char *left, const char *right) {
    size_t left_len = strlen(left);
    size_t right_len = strlen(right);
    char *result = malloc(left_len + right_len + 1);
    memcpy(result, left, left_len);
    memcpy(result + left_len, right, right_len + 1);
    return result;
}