#include "transpiler.h"
#include "lambda.h"

Item ITEM_NULL = {.type_id = LMD_TYPE_NULL, .data = NULL};

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

Item* map_get(Context *rt, Map* map, char *key) {
    if (!rt || !map || !key) { return &ITEM_NULL; }
    ShapeEntry *field = ((LambdaTypeMap*)((AstMapNode*)map->ast)->type)->shape;
    while (field) {
        if (strncmp(field->name.str, key, field->name.length) == 0) {
            TypeId type_id = field->type->type_id;
            void* field_ptr = (char*)map->data + field->byte_offset;
            switch (type_id) {
                case LMD_TYPE_INT:
                    return &(Item){.type_id = LMD_TYPE_INT, .long_val = *(long*)field_ptr};
                case LMD_TYPE_FLOAT:
                    return &(Item){.type_id = LMD_TYPE_FLOAT, .double_val = *(double*)field_ptr};
                case LMD_TYPE_STRING:
                    return &(Item){.type_id = LMD_TYPE_STRING, .str = *(char**)field_ptr};
                case LMD_TYPE_BOOL:
                    return &(Item){.type_id = LMD_TYPE_BOOL, .bool_val = *(bool*)field_ptr};
                default:
                    printf("unknown type %d\n", type_id);
            }
        }
        field = field->next;
    }
    printf("key %s not found\n", key);
    return &ITEM_NULL;
}

bool item_true(Item* item) {
    if (item->type_id == LMD_TYPE_NULL) {
        return false;
    }
    if (item->type_id == LMD_TYPE_BOOL) {
        return item->data;
    }
    return true;
}