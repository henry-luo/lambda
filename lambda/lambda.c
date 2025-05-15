#include "transpiler.h"
#include <stdarg.h>

Array* array_new(int count, ...) {
    if (count <= 0) { return NULL; }
    va_list args;
    va_start(args, count);
    Array *arr = malloc(sizeof(Array));
    arr->type_id = LMD_TYPE_ARRAY;  arr->capacity = count;
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
    arr->type_id = LMD_TYPE_ARRAY_INT;  arr->capacity = count;
    arr->items = malloc(count * sizeof(long));
    arr->length = count;
    for (int i = 0; i < count; i++) {
        arr->items[i] = va_arg(args, long);
        printf("array int: %ld\n", arr->items[i]);
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
    if (!item) { return; }  // NULL value
    LambdaItem itm = {.item = item};
    if (itm.type_id == LMD_TYPE_NULL) { 
        return;  // skip NULL value
    }
    // list is flattened
    if (itm.type_id == LMD_TYPE_RAW_POINTER && *((uint8_t*)itm.raw_pointer) == LMD_TYPE_LIST) {
        List *nest_list = (List*)itm.raw_pointer;
        for (int i = 0; i < nest_list->length; i++) {
            Item item = nest_list->items[i];
            list_push(list, item);
        }
        return;
    }  
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
        LambdaItem itm = {.item = va_arg(args, uint64_t)};
        if (itm.type_id == LMD_TYPE_NULL) { 
            continue;  // skip NULL value
        }
        list_push(list, itm.item);
    }
    va_end(args);
    return list;
}

ListLong* list_long() {
    printf("list_long");
    ListLong *list = malloc(sizeof(ListLong));
    list->items = NULL;
    list->length = 0;
    list->capacity = 0;
    return list;
}

void list_long_push(ListLong *list, long item) {
    printf("list_long_push: %ld", item);
    if (list->length >= list->capacity) {
        list->capacity = list->capacity ? list->capacity * 2 : 1;
        list->items = realloc(list->items, list->capacity * sizeof(long));
    }
    list->items[list->length++] = item;
}

// zig cc has problem compiling this function, it seems to align the pointers to 8 bytes
Map* map_new(Context *rt, int type_index, ...) {
    printf("map_new with shape %d\n", type_index);
    ArrayList* type_list = (ArrayList*)rt->type_list;
    AstMapNode* node = (AstMapNode*)((AstNode*)type_list->data[type_index]);
    LambdaTypeMap *map_type = (LambdaTypeMap*)node->type;
    Map *map = calloc(1, sizeof(Map));
    map->type_id = LMD_TYPE_MAP;  map->type = map_type;
    map->data = calloc(1, map_type->byte_size);  // heap_alloc(rt->heap, map_type->byte_size);
    printf("map byte_size: %d\n", map_type->byte_size);
    printf("map data: %d\n", *(uint8_t*)(((uint8_t*)map->data) + map_type->byte_size - 1));

    // set the fields
    int count = map_type->length;
    printf("map length: %d\n", count);
    va_list args;  ShapeEntry *field = map_type->shape;
    va_start(args, count);
    for (int i = 0; i < count; i++) {
        printf("field type: %d, offset: %d\n", field->type->type_id, field->byte_offset);
        void* field_ptr = ((uint8_t*)map->data) + field->byte_offset;
        switch (field->type->type_id) {
            case LMD_TYPE_NULL:
                *(bool*)field_ptr = va_arg(args, bool);
                break;
            case LMD_TYPE_BOOL:
                *(bool*)field_ptr = va_arg(args, bool);
                break;                
            case LMD_TYPE_IMP_INT:  case LMD_TYPE_INT:
                *(long*)field_ptr = va_arg(args, long);
                printf("field int value: %ld\n", *(long*)field_ptr);
                break;
            case LMD_TYPE_FLOAT:
                *(double*)field_ptr = va_arg(args, double);
                printf("field float value: %f\n", *(double*)field_ptr);
                break;
            case LMD_TYPE_STRING:  case LMD_TYPE_SYMBOL:  
            case LMD_TYPE_DTIME:  case LMD_TYPE_BINARY:
                String *str = va_arg(args, String*);
                printf("field string value: %s\n", str->str);
                *(String**)field_ptr = str;
                break;
            case LMD_TYPE_ARRAY:  case LMD_TYPE_ARRAY_INT:
            case LMD_TYPE_LIST:  case LMD_TYPE_MAP:
                Array *arr = va_arg(args, Array*);
                printf("field array value: %p\n", arr);
                *(Array**)field_ptr = arr;
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
    ShapeEntry *field = ((LambdaTypeMap*)map->type)->shape;
    while (field) {
        if (strncmp(field->name.str, key, field->name.length) == 0) {
            TypeId type_id = field->type->type_id;
            void* field_ptr = (char*)map->data + field->byte_offset;
            switch (type_id) {
                case LMD_TYPE_NULL:
                    return ITEM_NULL;
                case LMD_TYPE_BOOL:
                    return b2it(*(bool*)field_ptr);
                case LMD_TYPE_IMP_INT:
                    return i2it(*(int*)field_ptr);
                case LMD_TYPE_FLOAT:
                    double dval = *(double*)field_ptr;
                    return push_d(rt, dval);
                case LMD_TYPE_DTIME:
                    return k2it(*(char**)field_ptr);
                case LMD_TYPE_STRING:
                    return s2it(*(char**)field_ptr);
                case LMD_TYPE_SYMBOL:
                    return y2it(*(char**)field_ptr);
                case LMD_TYPE_BINARY:
                    return x2it(*(char**)field_ptr);
                default:
                    printf("unknown type %d\n", type_id);
                    return ITEM_ERROR;
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
Item v2it(List* list) {
    if (!list) { return ITEM_NULL; }
    printf("v2it %p, length: %ld\n", list, list->length);
    if (list->length == 0) { return ITEM_NULL; }
    if (list->length == 1) { return list->items[0]; }
    return (Item)list;
}

Item push_d(Context *rt, double dval) {
    printf("push_d: %g\n", dval);
    double *dptr = pack_alloc(rt->stack, sizeof(double));
    *dptr = dval;
    return (Item) d2it(dptr);
}

Item push_l(Context *rt, long lval) {
    printf("push_l: %ld\n", lval);
    long *lptr = pack_alloc(rt->stack, sizeof(long));
    *lptr = lval;
    return (Item) l2it(lptr);
}

String *str_cat(String *left, String *right) {
    printf("str_cat %p, %p\n", left, right);
    size_t left_len = left->len;
    size_t right_len = right->len;
    printf("left len %zu, right len %zu\n", left_len, right_len);
    String *result = malloc(sizeof(String) + left_len + right_len + 1);
    result->len = left_len + right_len;
    memcpy(result->str, left->str, left_len);
    // copy the string and '\0'
    memcpy(result->str + left_len, right->str, right_len + 1);
    return result;
}

Item add(Context *rt, Item a, Item b) {
    LambdaItem item_a = {.item = a};  LambdaItem item_b = {.item = b};
    if (item_a.type_id == LMD_TYPE_STRING && item_b.type_id == LMD_TYPE_STRING) {
        String *str_a = (String*)item_a.pointer;  String *str_b = (String*)item_b.pointer;
        String *result = str_cat(str_a, str_b);
        return s2it(result);
    }
    else if (item_a.type_id == LMD_TYPE_IMP_INT && item_b.type_id == LMD_TYPE_IMP_INT) {
        return i2it(item_a.long_val + item_b.long_val);
    }
    else if (item_a.type_id == LMD_TYPE_INT && item_b.type_id == LMD_TYPE_INT) {
        return l2it(push_l(rt, *(long*)item_a.pointer + *(long*)item_b.pointer));
    }
    else if (item_a.type_id == LMD_TYPE_FLOAT && item_b.type_id == LMD_TYPE_FLOAT) {
        printf("add float: %g + %g\n", *(double*)item_a.pointer, *(double*)item_b.pointer);
        return d2it(push_d(rt, *(double*)item_a.pointer + *(double*)item_b.pointer));
    }
    else if (item_a.type_id == LMD_TYPE_IMP_INT && item_b.type_id == LMD_TYPE_FLOAT) {
        return d2it(push_d(rt, (double)item_a.long_val + *(double*)item_b.pointer));
    }
    else if (item_a.type_id == LMD_TYPE_FLOAT && item_b.type_id == LMD_TYPE_IMP_INT) {
        return d2it(push_d(rt, *(double*)item_a.pointer + (double)item_b.long_val));
    }
    else {
        printf("unknown add type: %d, %d\n", item_a.type_id, item_b.type_id);
    }
    return ITEM_ERROR;
}

long it2l(Item item) {
    LambdaItem itm = {.item = item};
    if (itm.type_id == LMD_TYPE_IMP_INT) {
        return itm.long_val;
    }
    else if (itm.type_id == LMD_TYPE_INT) {
        return *(long*)itm.pointer;
    }
    else if (itm.type_id == LMD_TYPE_FLOAT) {
        return (long)*(double*)itm.pointer;
    }
    printf("invalid type %d\n", itm.type_id);
    // todo: push error
    return 0;
}

double it2d(Item item) {
    LambdaItem itm = {.item = item};
    if (itm.type_id == LMD_TYPE_IMP_INT) {
        return (double)itm.long_val;
    }
    else if (itm.type_id == LMD_TYPE_INT) {
        return (double)*(long*)itm.pointer;
    }
    else if (itm.type_id == LMD_TYPE_FLOAT) {
        return *(double*)itm.pointer;
    }
    printf("invalid type %d\n", itm.type_id);
    // todo: push error
    return 0;
}