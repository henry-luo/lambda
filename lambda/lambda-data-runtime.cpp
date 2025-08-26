
#include "transpiler.hpp"

extern __thread Context* context;
void array_set(Array* arr, int index, Item itm, VariableMemPool *pool);
void set_fields(TypeMap *map_type, void* map_data, va_list args);

Item typeditem_to_item(TypedItem *titem) {
    switch (titem->type_id) {
    case LMD_TYPE_NULL:  return ItemNull;
    case LMD_TYPE_BOOL:
        return {.item = b2it(titem->bool_val)};
    case LMD_TYPE_INT:
        return {.item = i2it(titem->int_val)};
    case LMD_TYPE_INT64:
        return push_l(titem->long_val);
    case LMD_TYPE_FLOAT:
        return push_d(titem->double_val);
    case LMD_TYPE_DTIME:
        return push_k(titem->datetime_val);
    case LMD_TYPE_DECIMAL:
        return {.item = c2it(titem->decimal)};
    case LMD_TYPE_STRING:
        return {.item = s2it(titem->string)};
    case LMD_TYPE_SYMBOL:
        return {.item = y2it(titem->string)};
    case LMD_TYPE_BINARY:
        return {.item = x2it(titem->string)};
    case LMD_TYPE_ARRAY:  case LMD_TYPE_ARRAY_INT:  case LMD_TYPE_ARRAY_FLOAT:
    case LMD_TYPE_RANGE:  case LMD_TYPE_LIST:  case LMD_TYPE_MAP:  case LMD_TYPE_ELEMENT:
        return {.raw_pointer = titem->pointer};
    default:
        printf("map_get ANY type is UNKNOWN: %d\n", titem->type_id);
        return ItemError;
    }
}

Array* array() {
    Array *arr = (Array*)calloc(1, sizeof(Array));
    arr->type_id = LMD_TYPE_ARRAY;
    frame_start();
    return arr;
}

Array* array_fill(Array* arr, int count, ...) {
    if (count > 0) {
        va_list args;
        va_start(args, count);
        arr->capacity = count;
        arr->items = (Item*)malloc(count * sizeof(Item));
        for (int i = 0; i < count; i++) {
            if (arr->length + arr->extra + 2 > arr->capacity) { expand_list((List*)arr); }
            array_set(arr, i, va_arg(args, Item), NULL);
            arr->length++;
        }
        va_end(args);
    }
    frame_end();
    return arr;
}

Item array_get(Array *array, int index) {
    printf("array_get: index: %d, length: %ld\n", index, array->length);
    if (index < 0 || index >= array->length) {
        printf("array_get: index out of bounds: %d\n", index);
        return ItemNull;
    }
    Item item = array->items[index];
    switch (item.type_id) {
    case LMD_TYPE_INT64: {
        long lval = *(long*)item.pointer;
        return push_l(lval);
    }
    case LMD_TYPE_FLOAT: {
        double dval = *(double*)item.pointer;
        return push_d(dval);
    }
    case LMD_TYPE_DTIME: {
        DateTime dtval = *(DateTime*)item.pointer;
        return push_k(dtval);
    }
    default:
        return item;
    }    
}

List* list() {
    List *list = (List *)heap_calloc(sizeof(List), LMD_TYPE_LIST);
    list->type_id = LMD_TYPE_LIST;
    frame_start();
    return list;
}

Item list_fill(List *list, int count, ...) {
    printf("list_fill cnt: %d\n", count);
    va_list args;
    va_start(args, count);
    for (int i = 0; i < count; i++) {
        list_push(list, {.item = va_arg(args, uint64_t)});
    }
    va_end(args);
    frame_end();
    // printf("list_filled: %ld items\n", list->length);
    if (list->length == 0) {
        return ItemNull;
    } else if (list->length == 1 && list->type_id != LMD_TYPE_ELEMENT) {
        return list->items[0];
    } else {
        return {.list = list};
    }
}

Item list_get(List *list, int index) {
    if (index < 0 || index >= list->length) { return ItemNull; }
    Item item = list->items[index];
    switch (item.type_id) {
    case LMD_TYPE_INT64: {
        long lval = *(long*)item.pointer;
        return push_l(lval);
    }
    case LMD_TYPE_FLOAT: {
        double dval = *(double*)item.pointer;
        return push_d(dval);
    }
    default:
        return item;
    }
}

Map* map(int type_index) {
    printf("map with type %d\n", type_index);
    Map *map = (Map *)heap_calloc(sizeof(Map), LMD_TYPE_MAP);
    map->type_id = LMD_TYPE_MAP;
    ArrayList* type_list = (ArrayList*)context->type_list;
    TypeMap *map_type = (TypeMap*)(type_list->data[type_index]);
    map->type = map_type;    
    frame_start();
    return map;
}

// zig cc has problem compiling this function, it seems to align the pointers to 8 bytes
Map* map_fill(Map* map, ...) {
    TypeMap *map_type = (TypeMap*)map->type;
    map->data = calloc(1, map_type->byte_size);
    printf("map byte_size: %ld\n", map_type->byte_size);
    // set map fields
    va_list args;
    va_start(args, map_type->length);
    set_fields(map_type, map->data, args);
    va_end(args);
    frame_end();
    printf("map filled with type: %d, length: %ld\n", map_type->type_id, map_type->length);
    return map;
}

Item _map_get(TypeMap* map_type, void* map_data, char *key, bool *is_found) {
    ShapeEntry *field = map_type->shape;
    while (field) {
        if (!field->name) { // nested map, skip
            Map* nested_map = *(Map**)((char*)map_data + field->byte_offset);
            bool nested_is_found;
            Item result = _map_get((TypeMap*)nested_map->type, nested_map->data, key, &nested_is_found);
            if (nested_is_found) {
                *is_found = true;
                return result;
            }
            field = field->next;
            continue;
        }
        // printf("map_get compare field: %.*s\n", (int)field->name->length, field->name->str);
        if (strncmp(field->name->str, key, field->name->length) == 0 && 
            strlen(key) == field->name->length) {
            *is_found = true;
            TypeId type_id = field->type->type_id;
            void* field_ptr = (char*)map_data + field->byte_offset;
            printf("map_get found field: %.*s, type: %d, ptr: %p\n", 
                (int)field->name->length, field->name->str, type_id, field_ptr);
            switch (type_id) {
            case LMD_TYPE_NULL:
                return ItemNull;
            case LMD_TYPE_BOOL:
                return {.item = b2it(*(bool*)field_ptr)};
            case LMD_TYPE_INT:
                return {.item = i2it(*(int*)field_ptr)};
            case LMD_TYPE_INT64:
                return push_l(*(long*)field_ptr);
            case LMD_TYPE_FLOAT:
                return push_d(*(double*)field_ptr);
            case LMD_TYPE_DTIME: {
                DateTime dt = *(DateTime*)field_ptr;
                StrBuf *strbuf = strbuf_new();
                datetime_format_lambda(strbuf, &dt);
                printf("map_get datetime: %s\n", strbuf->str);
                return push_k(dt);
            }
            case LMD_TYPE_DECIMAL:
                return {.item = c2it(*(char**)field_ptr)};
            case LMD_TYPE_STRING:
                return {.item = s2it(*(char**)field_ptr)};
            case LMD_TYPE_SYMBOL:
                return {.item = y2it(*(char**)field_ptr)};
            case LMD_TYPE_BINARY:
                return {.item = x2it(*(char**)field_ptr)};
                
            case LMD_TYPE_RANGE:  case LMD_TYPE_ARRAY:  case LMD_TYPE_ARRAY_INT:  case LMD_TYPE_ARRAY_FLOAT:
            case LMD_TYPE_LIST:  case LMD_TYPE_MAP:  case LMD_TYPE_ELEMENT: {
                Container* container = *(Container**)field_ptr;
                printf("map_get container: %p, type_id: %d\n", container, container->type_id);
                // assert(container->type_id == type_id);
                return {.raw_pointer = container};
            }
            case LMD_TYPE_TYPE:  case LMD_TYPE_FUNC:
                return {.raw_pointer = *(void**)field_ptr};
            case LMD_TYPE_ANY: {
                printf("map_get ANY type, pointer: %p\n", field_ptr);
                return typeditem_to_item((TypedItem*)field_ptr);
            }
            default:
                printf("unknown map item type %d\n", type_id);
                return ItemError;
            }
        }
        field = field->next;
    }
    *is_found = false;
    printf("map_get: key %s not found\n", key);
    return ItemNull;
}

Item map_get(Map* map, Item key) {
    printf("map_get %p\n", map);
    if (!map || !key.item) { return ItemNull;}
    bool is_found;
    char *key_str = NULL;
    if (key.type_id == LMD_TYPE_STRING || key.type_id == LMD_TYPE_SYMBOL) {
        key_str = ((String*)key.pointer)->chars;
    } else {
        printf("map_get: key must be string or symbol, got type %d\n", key.type_id);
        return ItemNull;  // only string or symbol keys are supported
    }
    printf("map_get key: %s\n", key_str);
    return _map_get((TypeMap*)map->type, map->data, key_str, &is_found);
}

Element* elmt(int type_index) {
    printf("elmt with type %d\n", type_index);
    Element *elmt = (Element *)heap_calloc(sizeof(Element), LMD_TYPE_ELEMENT);
    elmt->type_id = LMD_TYPE_ELEMENT;
    ArrayList* type_list = (ArrayList*)context->type_list;
    TypeElmt *elmt_type = (TypeElmt*)(type_list->data[type_index]);
    elmt->type = elmt_type;
    if (elmt_type->length || elmt_type->content_length) {
        frame_start();
    }
    return elmt;
} 

Element* elmt_fill(Element* elmt, ...) {
    TypeElmt *elmt_type = (TypeElmt*)elmt->type;
    elmt->data = calloc(1, elmt_type->byte_size);  // heap_alloc(rt->heap, elmt_type->byte_size);
    printf("elmt byte_size: %ld\n", elmt_type->byte_size);
    // set attributes
    long count = elmt_type->length;
    printf("elmt length: %ld\n", count);
    va_list args;
    va_start(args, count);
    set_fields((TypeMap*)elmt_type, elmt->data, args);
    va_end(args);
    // no frame_end here, as there's still element body content
    return elmt;
}

Item elmt_get(Element* elmt, Item key) {
    if (!elmt || !key.item) { return ItemNull;}
    bool is_found;
    char *key_str = NULL;
    if (key.type_id == LMD_TYPE_STRING || key.type_id == LMD_TYPE_SYMBOL) {
        key_str = ((String*)key.pointer)->chars;
    } else {
        return ItemNull;  // only string or symbol keys are supported
    }
    return _map_get((TypeMap*)elmt->type, elmt->data, key_str, &is_found);
}
