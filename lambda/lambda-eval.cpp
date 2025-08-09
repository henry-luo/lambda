#include "transpiler.hpp"
#include <stdarg.h>

extern __thread Context* context;

#define stack_alloc(size) alloca(size);

#define Malloc malloc
#define Realloc realloc

void expand_list(List *list);

Array* array() {
    Array *arr = (Array*)calloc(1, sizeof(Array));
    arr->type_id = LMD_TYPE_ARRAY;
    frame_start();
    return arr;
}

Array* array_pooled(VariableMemPool *pool) {
    Array *arr;
    MemPoolError err = pool_variable_alloc(pool, sizeof(Array), (void**)&arr);
    if (err != MEM_POOL_ERR_OK) return NULL;
    memset(arr, 0, sizeof(Array));
    arr->type_id = LMD_TYPE_ARRAY;
    // frame_start();
    return arr;
}

void array_set(Array* arr, int index, Item itm, VariableMemPool *pool) {
    arr->items[index] = itm;
    printf("array set item: type: %d, index: %d, length: %ld, extra: %ld\n", 
        itm.type_id, index, arr->length, arr->extra);
    // input files uses pool, instead of extra slots in the array
    if (pool) return;
    switch (itm.type_id) {
    case LMD_TYPE_FLOAT: {
        double* dval = (double*)(arr->items + (arr->capacity - arr->extra - 1));
        *dval = *(double*)itm.pointer;  arr->items[index] = {.item = d2it(dval)};
        arr->extra++;
        printf("array set float: %lf\n", *dval);
        break;
    }
    case LMD_TYPE_INT64: {
        long* ival = (long*)(arr->items + (arr->capacity - arr->extra - 1));
        *ival = *(long*)itm.pointer;  arr->items[index] = {.item = l2it(ival)};
        arr->extra++;
        break;
    }
    case LMD_TYPE_STRING:  case LMD_TYPE_SYMBOL:  case LMD_TYPE_DTIME:  case LMD_TYPE_BINARY: {
        String *str = (String*)itm.pointer;
        str->ref_cnt++;
        break;
    }
    case LMD_TYPE_RAW_POINTER: {
        TypeId type_id = *((uint8_t*)itm.raw_pointer);
        if (type_id >= LMD_TYPE_LIST && type_id <= LMD_TYPE_ELEMENT) {
            Container *container = itm.container;
            container->ref_cnt++;
        }
        break;
    }}
}

void array_append(Array* arr, Item itm, VariableMemPool *pool) {
    if (arr->length + arr->extra + 2 > arr->capacity) { expand_list((List*)arr); }
    array_set(arr, arr->length, itm, pool);
    arr->length++;
}

Array* array_fill(Array* arr, int count, ...) {
    if (count > 0) {
        va_list args;
        va_start(args, count);
        arr->capacity = count;
        arr->items = (Item*)Malloc(count * sizeof(Item));
        for (int i = 0; i < count; i++) {
            if (arr->length + arr->extra + 2 > arr->capacity) { expand_list((List*)arr); }
            array_set(arr, i, va_arg(args, Item), NULL);
            arr->length++;
        }
        va_end(args);
    }
    frame_end();
    StrBuf *strbuf = strbuf_new();
    print_item(strbuf, {.array = arr});
    printf("array_fill: %s\n", strbuf->str);
    return arr;
}

Item array_get(Array *array, int index) {
    if (index < 0 || index >= array->length) { return ItemNull; }
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
    default:
        return item;
    }    
}

ArrayLong* array_long_new(int count, ...) {
    if (count <= 0) { return NULL; }
    va_list args;
    va_start(args, count);
    ArrayLong *arr = (ArrayLong*)heap_alloc(sizeof(ArrayLong), LMD_TYPE_ARRAY_INT);
    arr->type_id = LMD_TYPE_ARRAY_INT;  arr->capacity = count;
    arr->items = (long*)Malloc(count * sizeof(long));
    arr->length = count;
    for (int i = 0; i < count; i++) {
        arr->items[i] = va_arg(args, long);
    }       
    va_end(args);
    return arr;
}

List* list() {
    List *list = (List *)heap_calloc(sizeof(List), LMD_TYPE_LIST);
    list->type_id = LMD_TYPE_LIST;
    frame_start();
    return list;
}

void expand_list(List *list) {
    list->capacity = list->capacity ? list->capacity * 2 : 8;
    // list items are allocated from C heap, instead of Lambda heap
    // to consider: could also alloc directly from Lambda heap without the heap entry
    // need to profile to see which is faster
    Item* old_items = list->items;
    list->items = (Item*)Realloc(list->items, list->capacity * sizeof(Item));
    // copy extra items to the end of the list
    if (list->extra) {
        memcpy(list->items + (list->capacity - list->extra), 
            list->items + (list->capacity/2 - list->extra), list->extra * sizeof(Item));
        // scan the list, if the item is long/double,
        // and is stored in the list extra slots, need to update the pointer
        for (int i = 0; i < list->length; i++) {
            Item itm = list->items[i];
            if (itm.type_id == LMD_TYPE_FLOAT || itm.type_id == LMD_TYPE_INT64) {
                Item* old_pointer = (Item*)itm.pointer;
                // Only update pointers that are in the old list buffer's extra space
                if (old_items <= old_pointer && old_pointer < old_items + list->capacity/2) {
                    int offset = old_items + list->capacity/2 - old_pointer;
                    void* new_pointer = list->items + list->capacity - offset;
                    list->items[i] = {.item = itm.type_id == LMD_TYPE_FLOAT ? d2it(new_pointer) : l2it(new_pointer)};
                }
                // if the pointer is not in the old buffer, it should not be updated
            }
        }
    }
}

void list_push(List *list, Item item) {
    if (!item.item) { return; }  // NULL value
    if (item.type_id == LMD_TYPE_NULL) { return; } // skip NULL value
    if (item.type_id == LMD_TYPE_RAW_POINTER) {
        TypeId type_id = *((uint8_t*)item.raw_pointer);
        // nest list is flattened
        if (type_id == LMD_TYPE_LIST) {
            // copy over the items
            List *nest_list = item.list;
            for (int i = 0; i < nest_list->length; i++) {
                Item nest_item = nest_list->items[i];
                list_push(list, nest_item);
            }
            return;
        }
        else if (type_id == LMD_TYPE_RANGE) {
            // copy over the items
            Range *range = item.range;
            for (int i = range->start; i <= range->end; i++) {
                // todo: handle value > 32-bit
                list_push(list, {.item = i2it(i)});
            }
            return;
        }
        else if (type_id == LMD_TYPE_ARRAY || type_id == LMD_TYPE_ARRAY_INT || 
            type_id == LMD_TYPE_MAP || type_id == LMD_TYPE_ELEMENT) {
            Container *container = item.container;
            container->ref_cnt++;
        }
    }
    // store the value in the list (and we may need two slots for long/double)
    if (list->length + list->extra + 2 > list->capacity) { expand_list(list); }
    list->items[list->length++] = item;
    // printf("list push item: type: %d, length: %ld\n", itm.type_id, list->length);
    switch (item.type_id) {
    case LMD_TYPE_STRING:  case LMD_TYPE_SYMBOL:  case LMD_TYPE_DTIME:  case LMD_TYPE_BINARY: {
        String *str = (String*)item.pointer;
        str->ref_cnt++;
        break;
    }
    case LMD_TYPE_FLOAT: {
        double* dval = (double*)(list->items + (list->capacity - list->extra - 1));
        *dval = *(double*)item.pointer;  list->items[list->length-1] = {.item = d2it(dval)};
        list->extra++;
        printf("list push float: %f, extra: %ld\n", *dval, list->extra);
        break;
    }
    case LMD_TYPE_INT64: {
        long* ival = (long*)(list->items + (list->capacity - list->extra - 1));
        *ival = *(long*)item.pointer;  list->items[list->length-1] = {.item = l2it(ival)};
        list->extra++;
        break;
    }}
}

Item list_fill(List *list, int count, ...) {
    // printf("list_fill cnt: %d\n", count);
    va_list args;
    va_start(args, count);
    for (int i = 0; i < count; i++) {
        Item itm = {.item = va_arg(args, uint64_t)};
        list_push(list, itm);
    }
    va_end(args);
    frame_end();
    return list->length ? (list->length == 1 && list->type_id != LMD_TYPE_ELEMENT
        ? list->items[0] : (Item){.list = list}) : ItemNull;
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

void set_fields(TypeMap *map_type, void* map_data, va_list args) {
    long count = map_type->length;
    printf("map length: %ld\n", count);
    ShapeEntry *field = map_type->shape;
    for (long i = 0; i < count; i++) {
        // printf("set field of type: %d, offset: %ld, name:%.*s\n", field->type->type_id, field->byte_offset, 
        //     field->name ? (int)field->name->length:4, field->name ? field->name->str : "null");
        void* field_ptr = ((uint8_t*)map_data) + field->byte_offset;
        if (!field->name) { // nested map
            Item itm = {.item = va_arg(args, uint64_t)};
            if (itm.type_id == LMD_TYPE_RAW_POINTER && *((TypeId*)itm.raw_pointer) == LMD_TYPE_MAP) {
                Map* nested_map = itm.map;
                nested_map->ref_cnt++;
                *(Map**)field_ptr = nested_map;
            } else {
                printf("expected a map, got type %d\n", itm.type_id );
            }
        } else {
            switch (field->type->type_id) {
            case LMD_TYPE_NULL: {
                *(bool*)field_ptr = va_arg(args, bool);
                break;
            }
            case LMD_TYPE_BOOL: {
                *(bool*)field_ptr = va_arg(args, bool);
                printf("field bool value: %s\n", *(bool*)field_ptr ? "true" : "false");
                break;
            }
            case LMD_TYPE_INT:  case LMD_TYPE_INT64: {
                *(long*)field_ptr = va_arg(args, long);
                printf("field int value: %ld\n", *(long*)field_ptr);
                break;
            }
            case LMD_TYPE_FLOAT: {
                *(double*)field_ptr = va_arg(args, double);
                printf("field float value: %f\n", *(double*)field_ptr);
                break;
            }
            case LMD_TYPE_STRING:  case LMD_TYPE_SYMBOL:  case LMD_TYPE_DTIME:  case LMD_TYPE_BINARY: {
                String *str = va_arg(args, String*);
                printf("field string value: %s\n", str->chars);
                *(String**)field_ptr = str;
                str->ref_cnt++;
                break;
            }
            case LMD_TYPE_ARRAY:  case LMD_TYPE_ARRAY_INT:
            case LMD_TYPE_LIST:  case LMD_TYPE_MAP:  case LMD_TYPE_ELEMENT: {
                Container *container = va_arg(args, Container*);
                *(Container**)field_ptr = container;
                container->ref_cnt++;
                break;
            }
            case LMD_TYPE_TYPE:  case LMD_TYPE_FUNC:  case LMD_TYPE_ANY: {
                void *arr = va_arg(args, void*);
                *(void**)field_ptr = arr;
                break;
            }
            default:
                printf("unknown type %d\n", field->type->type_id);
            }
        }
        field = field->next;
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

extern TypeMap EmptyMap;

Map* map_pooled(VariableMemPool *pool) {
    Map *map = (Map *)pool_calloc(pool, sizeof(Map));
    map->type_id = LMD_TYPE_MAP;
    map->type = &EmptyMap;
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
            case LMD_TYPE_INT64: {
                long lval = *(long*)field_ptr;
                return push_l(lval);
            }
            case LMD_TYPE_FLOAT: {
                double dval = *(double*)field_ptr;
                return push_d(dval);
            }
            case LMD_TYPE_DECIMAL: {
                printf("decimal not supported yet\n");
                return ItemError;
            }
            case LMD_TYPE_STRING:
                return {.item = s2it(*(char**)field_ptr)};
            case LMD_TYPE_SYMBOL:
                return {.item = y2it(*(char**)field_ptr)};
            case LMD_TYPE_DTIME:
                return {.item = k2it(*(char**)field_ptr)};
            case LMD_TYPE_BINARY:
                return {.item = x2it(*(char**)field_ptr)};
                
            case LMD_TYPE_RANGE:  case LMD_TYPE_ARRAY:  case LMD_TYPE_ARRAY_INT:
            case LMD_TYPE_LIST:  case LMD_TYPE_MAP:  case LMD_TYPE_ELEMENT: {
                Container* container = *(Container**)field_ptr;
                printf("map_get container: %p, type_id: %d\n", container, container->type_id);
                // assert(container->type_id == type_id);
                return {.raw_pointer = container};
            }
            case LMD_TYPE_TYPE:  case LMD_TYPE_FUNC:
                return {.raw_pointer = *(void**)field_ptr};
            default:
                printf("unknown type %d\n", type_id);
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

extern TypeElmt EmptyElmt;

Element* elmt_pooled(VariableMemPool *pool) {
    Element *elmt = (Element *)pool_calloc(pool, sizeof(Element));
    elmt->type_id = LMD_TYPE_ELEMENT;
    elmt->type = &EmptyElmt;
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

bool item_true(Item item) {
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
    if (!list) { return ItemNull; }
    printf("v2it %p, length: %ld\n", list, list->length);
    if (list->length == 0) { return ItemNull; }
    if (list->length == 1) { return list->items[0]; }
    return {.list = list};
}

Item push_d(double dval) {
    printf("push_d: %g\n", dval);
    double *dptr = num_stack_push_double((num_stack_t *)context->num_stack, dval); // stack_alloc(sizeof(double));
    return {.item = d2it(dptr)};
}

Item push_l(long lval) {
    printf("push_l: %ld\n", lval);
    long *lptr = num_stack_push_long((num_stack_t *)context->num_stack, lval); // stack_alloc(sizeof(long));
    return {.item = l2it(lptr)};
}

String *str_cat(String *left, String *right) {
    printf("str_cat %p, %p\n", left, right);
    size_t left_len = left->len;
    size_t right_len = right->len;
    printf("left len %zu, right len %zu\n", left_len, right_len);
    String *result = (String *)heap_alloc(sizeof(String) + left_len + right_len + 1, LMD_TYPE_STRING);
    printf("str result %p\n", result);
    result->ref_cnt = 0;  result->len = left_len + right_len;
    memcpy(result->chars, left->chars, left_len);
    // copy the string and '\0'
    memcpy(result->chars + left_len, right->chars, right_len + 1);
    printf("str_cat result: %s\n", result->chars);
    return result;
}

Item add(Item item_a, Item item_b) {
    // todo: join binary, list, array, map
    if (item_a.type_id == LMD_TYPE_STRING && item_b.type_id == LMD_TYPE_STRING) {
        String *str_a = (String*)item_a.pointer;  String *str_b = (String*)item_b.pointer;
        String *result = str_cat(str_a, str_b);
        return {.item = s2it(result)};
    }
    else if (item_a.type_id == LMD_TYPE_INT && item_b.type_id == LMD_TYPE_INT) {
        return {.item = i2it(item_a.long_val + item_b.long_val)};
    }
    else if (item_a.type_id == LMD_TYPE_INT64 && item_b.type_id == LMD_TYPE_INT64) {
        return push_l(*(long*)item_a.pointer + *(long*)item_b.pointer);
    }
    else if (item_a.type_id == LMD_TYPE_FLOAT && item_b.type_id == LMD_TYPE_FLOAT) {
        printf("add float: %g + %g\n", *(double*)item_a.pointer, *(double*)item_b.pointer);
        return push_d(*(double*)item_a.pointer + *(double*)item_b.pointer);
    }
    else if (item_a.type_id == LMD_TYPE_INT && item_b.type_id == LMD_TYPE_FLOAT) {
        return push_d((double)item_a.long_val + *(double*)item_b.pointer);
    }
    else if (item_a.type_id == LMD_TYPE_FLOAT && item_b.type_id == LMD_TYPE_INT) {
        return push_d(*(double*)item_a.pointer + (double)item_b.long_val);
    }
    else {
        printf("unknown add type: %d, %d\n", item_a.type_id, item_b.type_id);
    }
    return ItemError;
}

Range* fn_to(Item item_a, Item item_b) {
    // todo: join binary, list, array, map
    if ((item_a.type_id == LMD_TYPE_INT || item_a.type_id == LMD_TYPE_INT64) && 
        (item_b.type_id == LMD_TYPE_INT || item_b.type_id == LMD_TYPE_INT64)) {
        if (item_a.long_val > item_b.long_val) {
            // todo: should raise error
            return NULL;
        }
        Range *range = (Range *)heap_alloc(sizeof(Range), LMD_TYPE_RANGE);
        range->type_id = LMD_TYPE_RANGE;
        range->start = item_a.long_val;  range->end = item_b.long_val;
        range->length = item_b.long_val - item_a.long_val + 1;
        return range;
    }
    else {
        printf("unknown range type: %d, %d\n", item_a.type_id, item_b.type_id);
        return NULL;
    }
}

long it2l(Item itm) {
    if (itm.type_id == LMD_TYPE_INT) {
        return itm.long_val;
    }
    else if (itm.type_id == LMD_TYPE_INT64) {
        return *(long*)itm.pointer;
    }
    else if (itm.type_id == LMD_TYPE_FLOAT) {
        return (long)*(double*)itm.pointer;
    }
    printf("invalid type %d\n", itm.type_id);
    // todo: push error
    return 0;
}

double it2d(Item itm) {
    if (itm.type_id == LMD_TYPE_INT) {
        return (double)itm.long_val;
    }
    else if (itm.type_id == LMD_TYPE_INT64) {
        return (double)*(long*)itm.pointer;
    }
    else if (itm.type_id == LMD_TYPE_FLOAT) {
        return *(double*)itm.pointer;
    }
    printf("invalid type %d\n", itm.type_id);
    // todo: push error
    return 0;
}

Function* to_fn(fn_ptr ptr) {
    printf("create fn %p\n", ptr);
    Function *fn = (Function*)calloc(1, sizeof(Function));
    fn->type_id = LMD_TYPE_FUNC;
    fn->ptr = ptr;
    return fn;
}

bool fn_is(Item a, Item b) {
    printf("is expr\n");
    TypeId b_type_id = get_type_id(b);
    if (b_type_id != LMD_TYPE_TYPE) {
        return false;
    }
    TypeType *type_b = (TypeType *)b.type;
    TypeId a_type_id = get_type_id(a);
    printf("is type %d, %d\n", a_type_id, type_b->type->type_id);
    switch (type_b->type->type_id) {
    case LMD_TYPE_ANY:
        return a_type_id != LMD_TYPE_ERROR;
    case LMD_TYPE_INT:  case LMD_TYPE_INT64:  case LMD_TYPE_FLOAT:  case LMD_TYPE_NUMBER:
        return LMD_TYPE_INT <= a_type_id && a_type_id <= type_b->type->type_id;
    case LMD_TYPE_RANGE:  case LMD_TYPE_ARRAY:  case LMD_TYPE_ARRAY_INT:
        printf("is array type: %d, %d\n", a_type_id, type_b->type->type_id);
        return a_type_id == LMD_TYPE_RANGE || a_type_id == LMD_TYPE_ARRAY || a_type_id == LMD_TYPE_ARRAY_INT;
    default:
        return a_type_id == type_b->type->type_id;
    }
}

bool equal(Item a_item, Item b_item) {
    printf("equal expr\n");
    if (a_item.type_id != b_item.type_id) {
        // number promotion
        if (LMD_TYPE_INT <= a_item.type_id && a_item.type_id <= LMD_TYPE_NUMBER && 
            LMD_TYPE_INT <= b_item.type_id && b_item.type_id <= LMD_TYPE_NUMBER) {
            double a_val = it2d(a_item), b_val = it2d(b_item);
            return a_val == b_val;
        }
        return false;
    }
    if (a_item.type_id == LMD_TYPE_NULL) {
        return true;
    }    
    else if (a_item.type_id == LMD_TYPE_INT) {
        return a_item.long_val == b_item.long_val;
    }
    else if (a_item.type_id == LMD_TYPE_INT64) {
        return *(long*)a_item.pointer == *(long*)b_item.pointer;
    }
    else if (a_item.type_id == LMD_TYPE_FLOAT) {
        return *(double*)a_item.pointer == *(double*)b_item.pointer;
    }
    else if (a_item.type_id == LMD_TYPE_STRING || a_item.type_id == LMD_TYPE_SYMBOL || 
        a_item.type_id == LMD_TYPE_BINARY || a_item.type_id == LMD_TYPE_DTIME) {
        String *str_a = (String*)a_item.pointer;  String *str_b = (String*)b_item.pointer;
        return str_a->len == str_b->len && strncmp(str_a->chars, str_b->chars, str_a->len) == 0;
    }
    printf("unknown comparing type %d\n", a_item.type_id);
    return false;
}

bool fn_in(Item a_item, Item b_item) {
    printf("in expr\n");
    if (b_item.type_id) { // b is scalar
        if (b_item.type_id == LMD_TYPE_STRING && a_item.type_id == LMD_TYPE_STRING) {
            String *str_a = (String*)a_item.pointer;  String *str_b = (String*)b_item.pointer;
            return str_a->len <= str_b->len && strstr(str_b->chars, str_a->chars) != NULL;
        }
    }
    else { // b is container
        TypeId b_type = b_item.container->type_id;
        if (b_type == LMD_TYPE_LIST) {
            List *list = b_item.list;
            for (int i = 0; i < list->length; i++) {
                if (equal(list->items[i], a_item)) {
                    return true;
                }
            }
            return false;
        }
        else if (b_type == LMD_TYPE_RANGE) {
            Range *range = b_item.range;
            long a_val = it2l(a_item);
            return range->start <= a_val && a_val <= range->end;
        }
        else if (b_type == LMD_TYPE_ARRAY) {
            Array *arr = b_item.array;
            for (int i = 0; i < arr->length; i++) {
                if (equal(arr->items[i], a_item)) {
                    return true;
                }
            }
            return false;
        }
        else if (b_type == LMD_TYPE_ARRAY_INT) {
            ArrayLong *arr = b_item.array_long;
            for (int i = 0; i < arr->length; i++) {
                if (arr->items[i] == it2l(a_item)) {
                    return true;
                }
            }
            return false;
        }
        else if (b_type == LMD_TYPE_MAP) {
            // check if a is in map
        }
        else if (b_type == LMD_TYPE_ELEMENT) {
            // check if a is in element
        }
        else {
            printf("invalid type %d\n", b_type);
        }
    }
    return false;
}

String STR_NULL = {.chars = "null", .len = 4};
String STR_TRUE = {.chars = "true", .len = 4};
String STR_FALSE = {.chars = "false", .len = 5};

String* fn_string(Item itm) {
    if (itm.type_id == LMD_TYPE_NULL) {
        return &STR_NULL;
    }
    else if (itm.type_id == LMD_TYPE_BOOL) {
        return itm.bool_val ? &STR_TRUE : &STR_FALSE;
    }    
    else if (itm.type_id == LMD_TYPE_STRING || itm.type_id == LMD_TYPE_SYMBOL || 
        itm.type_id == LMD_TYPE_BINARY || itm.type_id == LMD_TYPE_DTIME) {
        return (String*)itm.pointer;
    }
    else if (itm.type_id == LMD_TYPE_INT) {
        char buf[32];
        int int_val = (int32_t)itm.long_val;
        snprintf(buf, sizeof(buf), "%d", int_val);
        int len = strlen(buf);
        String *str = (String *)heap_alloc(len + 1 + sizeof(String), LMD_TYPE_STRING);
        strcpy(str->chars, buf);
        str->len = len;  str->ref_cnt = 0;
        return (String*)str;
    }
    else if (itm.type_id == LMD_TYPE_INT64) {
        char buf[32];
        long long_val = *(long*)itm.pointer;
        snprintf(buf, sizeof(buf), "%ld", long_val);
        int len = strlen(buf);
        String *str = (String *)heap_alloc(len + 1 + sizeof(String), LMD_TYPE_STRING);
        strcpy(str->chars, buf);
        str->len = len;  str->ref_cnt = 0;
        return (String*)str;
    }
    else if (itm.type_id == LMD_TYPE_FLOAT) {
        char buf[32];
        double dval = *(double*)itm.pointer;
        snprintf(buf, sizeof(buf), "%g", dval);
        int len = strlen(buf);
        String *str = (String *)heap_alloc(len + 1 + sizeof(String), LMD_TYPE_STRING);
        strcpy(str->chars, buf);
        str->len = len;  str->ref_cnt = 0;
        return (String*)str;
    }
    printf("unhandled type %d\n", itm.type_id);
    return NULL;
}

extern Type LIT_TYPE_ERROR;

Type* base_type(TypeId type_id) {
    return (type_id <= 0 || type_id > LMD_TYPE_ERROR) ? 
        &LIT_TYPE_ERROR : ((TypeInfo*)context->type_info)[type_id].lit_type;
}

Type* const_type(int type_index) {
    ArrayList* type_list = (ArrayList*)context->type_list;
    if (type_index < 0 || type_index >= type_list->length) {
        return &LIT_TYPE_ERROR;
    }    
    Type* type = (Type*)(type_list->data[type_index]);
    printf("const_type %d, %d, %p\n", type_index, type->type_id, type);
    return type;
}

Type* fn_type(Item item) {
    TypeType *type = (TypeType *)calloc(1, sizeof(TypeType) + sizeof(Type)); 
    Type *item_type = (Type *)((uint8_t *)type + sizeof(TypeType));
    type->type = item_type;  type->type_id = LMD_TYPE_TYPE;
    if (item.type_id) {
        item_type->type_id = item.type_id;
    }
    else if (item.type_id == LMD_TYPE_RAW_POINTER) {
        item_type->type_id = item.container->type_id;
    }
    return (Type*)type;
}

Input* input_data(Context* ctx, String* url, String* type, String* flavor);

Item fn_input(Item url, Item type) {
    String* url_str;
    if (url.type_id != LMD_TYPE_STRING && url.type_id != LMD_TYPE_SYMBOL) {
        printf("input url must be a string or symbol, got type %d\n", url.type_id);
        return ItemNull;  // todo: push error
    }
    else {
        url_str = (String*)url.pointer;
    }
    
    String* type_str = NULL;
    String* flavor_str = NULL;
    
    TypeId type_id = get_type_id(type);
    if (type_id == LMD_TYPE_NULL) {
        // No type specified
        type_str = NULL;
    }
    else if (type_id == LMD_TYPE_STRING || type_id == LMD_TYPE_SYMBOL) {
        // Legacy behavior: type is a simple string/symbol
        type_str = (String*)type.pointer;
    }
    else if (type_id == LMD_TYPE_MAP) {
        printf("input type is a map\n");
        // New behavior: type is a map with options
        Map* options_map = type.map;
        
        // Extract 'type' from map
        bool is_found;
        Item input_type = _map_get((TypeMap*)options_map->type, options_map->data, "type", &is_found);
        if (!is_found || !input_type.item || input_type.type_id == LMD_TYPE_NULL) { // missing 'type' key
            type_str = NULL;
        } else {
            TypeId type_value_type = get_type_id(input_type);
            if (type_value_type == LMD_TYPE_STRING || type_value_type == LMD_TYPE_SYMBOL) {
                type_str = (String*)input_type.pointer;
            }
            else {
                printf("input type must be a string or symbol, got type %d\n", type_value_type);
                // todo: push error
                type_str = NULL;  // input type ignored
            }
        }

        // Extract 'flavor' from map
        Item input_flavor = _map_get((TypeMap*)options_map->type, options_map->data, "flavor", &is_found);
        if (!is_found || !input_flavor.item || input_flavor.type_id == LMD_TYPE_NULL) { // missing 'flavor' key
            flavor_str = NULL;
        } else {
            TypeId flavor_value_type = get_type_id(input_flavor);
            if (flavor_value_type == LMD_TYPE_STRING || flavor_value_type == LMD_TYPE_SYMBOL) {
                flavor_str = (String*)input_flavor.pointer;
            }
            else {
                printf("input flavor must be a string or symbol, got type %d\n", flavor_value_type);
                // todo: push error
                flavor_str = NULL;  // input flavor ignored
            }
        }
    }
    else {
        printf("input type must be a string, symbol, or map, got type %d\n", type_id);
        return ItemNull;  // todo: push error
    }
    
    Input *input = input_data(context, url_str, type_str, flavor_str);
    // todo: input should be cached in context
    return (input && input->root.item) ? input->root : ItemNull;
}

void fn_print(Item item) {
    String *str = fn_string(item);
    if (str) {
        printf("%s\n", str->chars);
    }
}

extern "C" String* format_data(Item item, String* type, String* flavor, VariableMemPool *pool);

String* fn_format(Item item, Item type) {
    TypeId type_id = get_type_id(type);
    String* type_str = NULL;
    String* flavor_str = NULL;

    if (type_id == LMD_TYPE_NULL) {
        type_str = NULL;  // use default
    }
    else if (type_id == LMD_TYPE_STRING || type_id == LMD_TYPE_SYMBOL) {
        // Legacy behavior: type is a simple string or symbol
        type_str = (String*)type.pointer;
    }
    else if (type_id == LMD_TYPE_MAP) {
        printf("format type is a map\n");
        // New behavior: type is a map with options
        Map* options_map = (Map*)type.pointer;
        
        // Extract 'type' from map
        bool is_found;
        Item format_type = _map_get((TypeMap*)options_map->type, options_map->data, "type", &is_found);
        if (!is_found || !format_type.item || format_type.type_id == LMD_TYPE_NULL) { // missing 'type' key
            type_str = NULL;
        } else {
            TypeId type_value_type = get_type_id(format_type);
            if (type_value_type == LMD_TYPE_STRING || type_value_type == LMD_TYPE_SYMBOL) {
                type_str = (String*)format_type.pointer;
            }
            else {
                printf("format type must be a string or symbol, got type %d\n", type_value_type);
                // todo: push error
                type_str = NULL;  // format type ignored
            }
        }

        // Extract 'flavor' from map
        Item format_flavor = _map_get((TypeMap*)options_map->type, options_map->data, "flavor", &is_found);
        if (!is_found || !format_flavor.item || format_flavor.type_id == LMD_TYPE_NULL) { // missing 'flavor' key
            flavor_str = NULL;
        } else {
            TypeId flavor_value_type = get_type_id(format_flavor);
            if (flavor_value_type == LMD_TYPE_STRING || flavor_value_type == LMD_TYPE_SYMBOL) {
                flavor_str = (String*)format_flavor.pointer;
            }
            else {
                printf("format flavor must be a string or symbol, got type %d\n", flavor_value_type);
                // todo: push error
                flavor_str = NULL;  // format flavor ignored
            }
        }
    }
    else {
        printf("format type must be a string, symbol, or map, got type %d\n", type_id);
        return NULL;  // todo: push error
    }
    
    // printf("format item type: %s, flavor: %s\n", type_str ? type_str->chars : "null", flavor_str ? flavor_str->chars : "null");
    String* result = format_data(item, type_str, flavor_str, context->heap->pool);
    if (result) {
         arraylist_append(context->heap->entries, (void*)s2it(result));
    }
    return result;
}

#include "../lib/utf.h"

// generic field access function for any type
Item fn_index(Item item, Item index_item) {
    // Determine the type and delegate to appropriate getter
    long index = -1;
    switch (index_item.type_id) {
    case LMD_TYPE_INT:
        index = index_item.int_val;
        break;
    case LMD_TYPE_INT64:
        index = *(long*)index_item.pointer;
        break;
    case LMD_TYPE_FLOAT: {
        double dval = *(double*)index_item.pointer;
        // check dval is an integer
        if (dval == (long)dval) {
            index = (long)dval;
        } else {
            printf("index must be an integer, got float %g\n", dval);
            return ItemNull;  // todo: push error
        }
        break;
    }
    case LMD_TYPE_STRING:  case LMD_TYPE_SYMBOL:
        return fn_member(item, index_item);
    default:
        printf("invalid index type %d\n", index_item.type_id);
        return ItemNull;
    }

    TypeId type_id = get_type_id(item);
    switch (type_id) {
    case LMD_TYPE_RANGE: {
        Range *range = item.range;
        if (index < range->start || index > range->end) { return ItemNull; }
        long value = range->start + index;
        return {.item = i2it(value)};
    }
    case LMD_TYPE_ARRAY:
        return array_get(item.array, (int)index);
    case LMD_TYPE_ARRAY_INT: {
        ArrayLong *arr = item.array_long;
        if (index < 0 || index >= arr->length) { return ItemNull; }
        return {.item = i2it(arr->items[index])};
    }
    case LMD_TYPE_LIST:
        return list_get(item.list, (int)index);
    case LMD_TYPE_ELEMENT:
        // treat element as list for index access
        return list_get(item.list, (int)index);
    case LMD_TYPE_MAP:
        // to consider: should we return ITEM_NULL or ITEM_ERROR? 
        return ItemNull;
    // todo: string, symbol, dtime, binary, etc.
    default:
        return ItemNull;
    }
}

Item fn_member(Item item, Item key) {
    TypeId type_id = get_type_id(item);
    switch (type_id) {
    case LMD_TYPE_MAP: {
        Map *map = item.map;
        return map_get(map, key);
    }
    case LMD_TYPE_ELEMENT: {
        Element *elmt = item.element;
        return elmt_get(elmt, key);
    }
    case LMD_TYPE_LIST: {
        // Handle built-in properties for List type
        if (key.type_id == LMD_TYPE_STRING || key.type_id == LMD_TYPE_SYMBOL) {
            String *key_str = (String*)key.pointer;
            if (key_str && strcmp(key_str->chars, "length") == 0) {
                List *list = item.list;
                return {.item = i2it(list->length)};
            }
        }
        return ItemNull;
    }
    // todo: built-in properties for other types
    default:
        return ItemNull;
    }
}

// length of an item's content, relates to indexed access, i.e. item[index] 
Item fn_len(Item item) {
    TypeId type_id = get_type_id(item);
    printf("fn_len item: %d\n", type_id);
    long size = 0;
    switch (type_id) {
    case LMD_TYPE_LIST:
        size = item.list->length;
        break;
    case LMD_TYPE_RANGE:
        size = item.range->length;
        break;
    case LMD_TYPE_ARRAY:
        size = item.array->length;
        break;
    case LMD_TYPE_ARRAY_INT:
        size = item.array_long->length;
        break;
    case LMD_TYPE_MAP: {
        size = 0;
        break;
    }
    case LMD_TYPE_ELEMENT: {
        Element *elmt = item.element;
        size = elmt->length;
        break;
    }
    case LMD_TYPE_STRING:  case LMD_TYPE_SYMBOL:  case LMD_TYPE_BINARY: {
        // returns the length of the string
        // todo: binary length
        String *str = (String*)item.pointer;  // todo:: should return char length
        size = str ? utf8_char_count(str->chars) : 0;
        break;
    }
    case LMD_TYPE_ERROR:
        return ItemError;
    default: // NULL and scalar types
        size = 0;
        break;
    }
    return {.item = i2it(size)};
}