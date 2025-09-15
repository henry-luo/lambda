
#include "transpiler.hpp"
#include "../lib/log.h"

extern __thread Context* context;
void array_set(Array* arr, int index, Item itm);
void array_push(Array* arr, Item itm);
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
    case LMD_TYPE_ARRAY:  case LMD_TYPE_ARRAY_INT:  case LMD_TYPE_ARRAY_INT64: case LMD_TYPE_ARRAY_FLOAT:
    case LMD_TYPE_RANGE:  case LMD_TYPE_LIST:  case LMD_TYPE_MAP:  case LMD_TYPE_ELEMENT:
        return {.raw_pointer = titem->pointer};
    default:
        log_error("map_get ANY type is UNKNOWN: %d", titem->type_id);
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
            array_push(arr, va_arg(args, Item));
        }
        va_end(args);
    }
    log_debug("array_filled");
    frame_end();
    log_item({.list = arr}, "array_filled");
    return arr;
}

Item array_get(Array *array, int index) {
    log_debug("array_get: index: %d, length: %ld", index, array->length);
    if (index < 0 || index >= array->length) {
        log_warn("array_get: index out of bounds: %d", index);
        return ItemNull;  // return null instead of error
    }
    Item item = array->items[index];
    switch (item.type_id) {
    case LMD_TYPE_INT64: {
        long lval = *(long*)item.pointer;
        return push_l(lval); // need to push to num_stack, as long values are not ref counted
    }
    case LMD_TYPE_FLOAT: {
        double dval = *(double*)item.pointer;
        return push_d(dval); // need to push to num_stack, as float values are not ref counted
    }
    case LMD_TYPE_DTIME: {
        DateTime dtval = *(DateTime*)item.pointer;
        return push_k(dtval); // need to push to num_stack, as datetime values are not ref counted
    }
    default:
        log_debug("array_get returning: type: %d, item: %p", item.type_id, item.raw_pointer);
        return item;
    }    
}

ArrayInt* array_int() {
    ArrayInt *arr = (ArrayInt*)heap_calloc(sizeof(ArrayInt), LMD_TYPE_ARRAY_INT);
    arr->type_id = LMD_TYPE_ARRAY_INT;
    frame_start();
    return arr;
}

// used when there's no interleaving with transpiled code
ArrayInt* array_int_new(int length) {
    ArrayInt *arr = (ArrayInt*)heap_calloc(sizeof(ArrayInt), LMD_TYPE_ARRAY_INT);
    arr->type_id = LMD_TYPE_ARRAY_INT;
    arr->length = length;  arr->capacity = length;
    arr->items = (int32_t*)malloc(length * sizeof(int32_t));
    return arr;
}

ArrayInt* array_int_fill(ArrayInt *arr, int count, ...) {
    if (count > 0) {
        va_list args;
        va_start(args, count);
        arr->items = (int32_t*)malloc(count * sizeof(int32_t));
        arr->length = count;  arr->capacity = count;
        for (int i = 0; i < count; i++) {
            arr->items[i] = va_arg(args, int32_t);
        }       
        va_end(args);
    }
    log_debug("array_int_filled");
    frame_end();
    return arr;
}

Item array_int_get(ArrayInt *array, int index) {
    log_debug("array_int_get: index: %d, length: %ld", index, array->length);
    if (index < 0 || index >= array->length) {
        log_warn("array_int_get: index out of bounds: %d", index);
        return ItemNull;  // return null instead of error
    }
    int val = array->items[index];
    Item item = (Item){.int_val = val, ._type = LMD_TYPE_INT};
    log_debug("array_int_get returning: type: %d, int_val: %d", item.type_id, item.int_val);
    return item;
}

ArrayInt64* array_int64() {
    ArrayInt64 *arr = (ArrayInt64*)heap_calloc(sizeof(ArrayInt64), LMD_TYPE_ARRAY_INT64);
    arr->type_id = LMD_TYPE_ARRAY_INT64;
    frame_start();
    return arr;
}

// used when there's no interleaving with transpiled code
ArrayInt64* array_int64_new(int length) {
    ArrayInt64 *arr = (ArrayInt64*)heap_calloc(sizeof(ArrayInt64), LMD_TYPE_ARRAY_INT64);
    arr->type_id = LMD_TYPE_ARRAY_INT64;
    arr->length = length;  arr->capacity = length;
    arr->items = (int64_t*)malloc(length * sizeof(int64_t));
    return arr;
}

ArrayInt64* array_int64_fill(ArrayInt64 *arr, int count, ...) {
    if (count > 0) { 
        va_list args;
        va_start(args, count);
        arr->items = (int64_t*)malloc(count * sizeof(int64_t));
        arr->length = count;  arr->capacity = count;
        for (int i = 0; i < count; i++) {
            arr->items[i] = va_arg(args, int64_t);
        }       
        va_end(args);
    }
    log_debug("array_int64_filled");
    frame_end();
    return arr;
}

Item array_int64_get(ArrayInt64* array, int index) {
    if (index < 0 || index >= array->length) {
        log_warn("array_int64_get: index out of bounds: %d", index);
        return ItemNull;
    }
    return push_l(array->items[index]);
}

ArrayFloat* array_float() {
    ArrayFloat *arr = (ArrayFloat*)heap_calloc(sizeof(ArrayFloat), LMD_TYPE_ARRAY_FLOAT);
    arr->type_id = LMD_TYPE_ARRAY_FLOAT;
    log_debug("array_float_start");
    frame_start();
    return arr;
}

// used when there's no interleaving with transpiled code
ArrayFloat* array_float_new(int length) {
    ArrayFloat *arr = (ArrayFloat*)heap_calloc(sizeof(ArrayFloat), LMD_TYPE_ARRAY_FLOAT);
    arr->type_id = LMD_TYPE_ARRAY_FLOAT;
    arr->length = length;  arr->capacity = length;
    arr->items = (double*)malloc(length * sizeof(double));
    return arr;
}

ArrayFloat* array_float_fill(ArrayFloat *arr, int count, ...) {
    if (count > 0) {
        va_list args;
        va_start(args, count);
        arr->type_id = LMD_TYPE_ARRAY_FLOAT;  
        arr->items = (double*)malloc(count * sizeof(double));
        arr->length = count;  arr->capacity = count;
        for (int i = 0; i < count; i++) {
            arr->items[i] = va_arg(args, double);
        }       
        va_end(args);
    }
    log_debug("array_float_filled");
    frame_end();
    return arr;
}

Item array_float_get(ArrayFloat* array, int index) {
    if (index < 0 || index >= array->length) {
        log_warn("array_float_get: index out of bounds: %d", index);
        return ItemNull;
    }
    log_debug("array_float_get: %d, val: %f", index, array->items[index]);
    return push_d(array->items[index]);
}

// fast path
double array_float_get_value(ArrayFloat *arr, int index) {
    if (index < 0 || index >= arr->length) {
        return NAN;  // Return NaN for invalid access
    }
    return arr->items[index];
}

// Wrapper function to return an Item instead of raw ArrayFloat*
// Item array_float_item(int count, ...) {
//     if (count <= 0) { 
//         ArrayFloat *arr = (ArrayFloat*)heap_alloc(sizeof(ArrayFloat), LMD_TYPE_ARRAY_FLOAT);
//         arr->type_id = LMD_TYPE_ARRAY_FLOAT;
//         arr->capacity = 0;
//         arr->items = NULL;
//         arr->length = 0;
//         return (Item){.array_float = arr};
//     }
//     va_list args;
//     va_start(args, count);
//     ArrayFloat *arr = (ArrayFloat*)heap_alloc(sizeof(ArrayFloat), LMD_TYPE_ARRAY_FLOAT);
//     arr->type_id = LMD_TYPE_ARRAY_FLOAT;  
//     arr->capacity = count;
//     arr->items = (double*)Malloc(count * sizeof(double));
//     arr->length = count;
//     for (int i = 0; i < count; i++) {
//         arr->items[i] = va_arg(args, double);
//     }       
//     va_end(args);
//     return (Item){.array_float = arr};
// }

void array_float_set(ArrayFloat *arr, int index, double value) {
    if (!arr || index < 0 || index >= arr->capacity) {
        return;  // Invalid access, do nothing
    }
    arr->items[index] = value;
    // Update length if we're setting beyond current length
    if (index >= arr->length) {
        arr->length = index + 1;
    }
}

// set with item value
void array_float_set_item(ArrayFloat *arr, int index, Item value) {
    if (!arr || index < 0 || index >= arr->capacity) {
        return;  // Invalid access, do nothing
    }
    
    double dval = 0.0;
    TypeId type_id = get_type_id(value);
    
    // Convert item to double based on its type
    switch (type_id) {
        case LMD_TYPE_FLOAT:
            dval = *(double*)value.pointer;
            break;
        case LMD_TYPE_INT64:
            dval = (double)(*(long*)value.pointer);
            break;
        case LMD_TYPE_INT:
            dval = (double)(value.int_val);
            break;
        default:
            return;  // Unsupported type, do nothing
    }
    
    arr->items[index] = dval;
    // Update length if we're setting beyond current length
    if (index >= arr->length) {
        arr->length = index + 1;
    }
}

List* list() {
    log_enter();
    List *list = (List *)heap_calloc(sizeof(List), LMD_TYPE_LIST);
    list->type_id = LMD_TYPE_LIST;
    frame_start();
    return list;
}

Item list_end(List *list) {
    frame_end();  log_leave();
    if (list->type_id == LMD_TYPE_ELEMENT) {
        log_debug("elmt_end!");
        log_item({.list = list}, "elmt_end");
        return {.list = list};        
    }
    else {
        log_debug("list_ended");
        if (list->length == 0) {
            return ItemNull;
        } 
        // flatten list, not element
        else if (list->length == 1) {
            return list->items[0];
        } else {
            log_item({.list = list}, "list_end");
            return {.list = list};
        }        
    }
}

Item list_fill(List *list, int count, ...) {
    log_debug("list_fill cnt: %d", count);
    va_list args;
    va_start(args, count);
    for (int i = 0; i < count; i++) {
        list_push(list, {.item = va_arg(args, uint64_t)});
    }
    va_end(args);
    return list_end(list);
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
    log_debug("map with type %d", type_index);
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
    log_debug("map byte_size: %ld", map_type->byte_size);
    // set map fields
    va_list args;
    va_start(args, map_type->length);
    set_fields(map_type, map->data, args);
    va_end(args);
    log_debug("map_filled");
    frame_end();
    log_debug("map filled with type: %d, length: %ld", map_type->type_id, map_type->length);
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
            log_debug("map_get found field: %.*s, type: %d, ptr: %p", 
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
                log_debug("map_get datetime: %s", strbuf->str);
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
                
            case LMD_TYPE_RANGE:  case LMD_TYPE_ARRAY:  case LMD_TYPE_ARRAY_INT:  case LMD_TYPE_ARRAY_INT64:  case LMD_TYPE_ARRAY_FLOAT:
            case LMD_TYPE_LIST:  case LMD_TYPE_MAP:  case LMD_TYPE_ELEMENT: {
                Container* container = *(Container**)field_ptr;
                log_debug("map_get container: %p, type_id: %d", container, container->type_id);
                // assert(container->type_id == type_id);
                return {.raw_pointer = container};
            }
            case LMD_TYPE_TYPE:  case LMD_TYPE_FUNC:
                return {.raw_pointer = *(void**)field_ptr};
            case LMD_TYPE_ANY: {
                log_debug("map_get ANY type, pointer: %p", field_ptr);
                return typeditem_to_item((TypedItem*)field_ptr);
            }
            default:
                log_error("unknown map item type %d", type_id);
                return ItemError;
            }
        }
        field = field->next;
    }
    *is_found = false;
    log_debug("map_get: key %s not found", key);
    return ItemNull;
}

Item map_get(Map* map, Item key) {
    log_debug("map_get %p", map);
    if (!map || !key.item) { return ItemNull;}
    bool is_found;
    char *key_str = NULL;
    if (key.type_id == LMD_TYPE_STRING || key.type_id == LMD_TYPE_SYMBOL) {
        key_str = ((String*)key.pointer)->chars;
    } else {
        log_error("map_get: key must be string or symbol, got type %d", key.type_id);
        return ItemNull;  // only string or symbol keys are supported
    }
        log_debug("map_get key: %s", key_str);
    return _map_get((TypeMap*)map->type, map->data, key_str, &is_found);
}

Element* elmt(int type_index) {
    log_debug("elmt with type index: %d", type_index);
    Element *elmt = (Element *)heap_calloc(sizeof(Element), LMD_TYPE_ELEMENT);
    elmt->type_id = LMD_TYPE_ELEMENT;
    ArrayList* type_list = (ArrayList*)context->type_list;
    TypeElmt *elmt_type = (TypeElmt*)(type_list->data[type_index]);
    elmt->type = elmt_type;
    if (elmt_type->length || elmt_type->content_length) {
        frame_start();
    }
    // else - bare element
    return elmt;
} 

Element* elmt_fill(Element* elmt, ...) {
    TypeElmt *elmt_type = (TypeElmt*)elmt->type;
    elmt->data = calloc(1, elmt_type->byte_size);  // heap_alloc(rt->heap, elmt_type->byte_size);
        log_debug("elmt byte_size: %ld", elmt_type->byte_size);
    // set attributes
    long count = elmt_type->length;
        log_debug("elmt length: %ld", count);
    va_list args;
    va_start(args, count);
    set_fields((TypeMap*)elmt_type, elmt->data, args);
    va_end(args);
    // no frame_end here, as there's still element body content
    return elmt;
}

// get element attribute by key
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

Item item_at(Item data, int index) {
    if (!data.item) { return ItemNull; }

    // note: for out of bound access, we return null instead of error
    TypeId type_id = get_type_id(data);
    switch (type_id) {
    case LMD_TYPE_ARRAY:
        return array_get(data.array, index);
    case LMD_TYPE_ARRAY_INT:
        return array_int_get(data.array_int, index);
    case LMD_TYPE_ARRAY_INT64:
        return array_int64_get(data.array_int64, index);
    case LMD_TYPE_ARRAY_FLOAT:
        return array_float_get(data.array_float, index);
    case LMD_TYPE_LIST:
        return list_get(data.list, index);
    case LMD_TYPE_RANGE: {
        Range *range = data.range;
        if (index < range->start || index > range->end) { return ItemNull; }
        long value = range->start + index;
        return {.item = i2it(value)};
    }
    case LMD_TYPE_ELEMENT: {
        // treat element as list
        return list_get(data.element, index);
    }
    case LMD_TYPE_STRING:  case LMD_TYPE_SYMBOL: {
        String *str = (String*)data.pointer;
        if (index < 0 || index >= str->len) { return ItemNull; }
        // return a single character string
        char buf[2] = {str->chars[index], '\0'};
        String *ch_str = heap_string(buf, 1);
        if (type_id == LMD_TYPE_SYMBOL) return {.item = y2it(ch_str)};
        else return {.item = s2it(ch_str)};
    }
    // case LMD_TYPE_BINARY: todo - proper binary data access
    default:
        log_error("item_at: unsupported item_at type: %d", type_id);
        return ItemNull;
    }
}