#include "transpiler.h"
#include "lambda.h"

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
    map->ast = NULL;
    map->data = NULL;
    return map;
}
Map* map_new(Context *rt, int type_index, ...) {
    if (!rt) { return NULL; }
    printf("map_new %p at %d\n", rt, type_index);
    ArrayList* type_list = (ArrayList*)rt->type_list;
    AstMapNode* node = (AstMapNode*)((AstNode*)type_list->data[type_index]);
    printf("map node: %p, type: %p, node_type: %d\n", node, node->type, node->node_type);
    Map *map = malloc(sizeof(Map));
    map->ast = node;
    VariableMemPool *pool = (VariableMemPool*)rt->heap;
    pool_variable_alloc(pool, ((LambdaTypeMap*)node->type)->byte_size, (void**)&map->data);
    printf("map data: %p, byte_size: %d\n", map->data, ((LambdaTypeMap*)node->type)->byte_size);

    // set the fields
    LambdaTypeMap *map_type = (LambdaTypeMap*)node->type;
    printf("map type: %d\n", map_type->type_id);
    int count = map_type->length;
    printf("map length: %d\n", count);
    va_list args;  AstNamedNode *field = node->item;
    printf("map field: %p\n", field);
    va_start(args, count);
    for (int i = 0; i < count; i++) {
        LambdaTypeField *field_type = (LambdaTypeField*)field->type;
        printf("field type: %d, offset: %d\n", field_type->type_id, field_type->byte_offset);
        void* field_ptr = (char*)map->data + field_type->byte_offset;
        switch (field_type->type_id) {
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
                printf("unknown type %d\n", field_type->type_id);
        }
        field = (AstNamedNode*)field->next;
    }
    va_end(args);
    return map;
}

void* map_get(Context *rt, char *key) {
    if (!rt || !key) {
        return NULL;
    }
    return NULL;
}