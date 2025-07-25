#include "transpiler.h"
#include <stdarg.h>

extern __thread Context* context;

#define stack_alloc(size) alloca(size);

#define Malloc malloc
#define Realloc realloc

void expand_list(List *list);

Array* array() {
    Array *arr = calloc(1, sizeof(Array));
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

void array_set(Array* arr, int index, LambdaItem itm, VariableMemPool *pool) {
    arr->items[index] = itm.item;
    printf("array set item: type: %d, index: %d, length: %ld, extra: %ld\n", 
        itm.type_id, index, arr->length, arr->extra);
    // input files uses pool, instead of extra slots in the array
    if (pool) return;
    switch (itm.type_id) {
    case LMD_TYPE_FLOAT:
        double* dval = (double*)(arr->items + (arr->capacity - arr->extra - 1));
        *dval = *(double*)itm.pointer;  arr->items[index] = d2it(dval);        
        arr->extra++;
        printf("array set float: %lf\n", *dval);
        break;
    case LMD_TYPE_INT64:
        long* ival = (long*)(arr->items + (arr->capacity - arr->extra - 1));
        *ival = *(long*)itm.pointer;  arr->items[index] = l2it(ival);
        arr->extra++;
        break;
    case LMD_TYPE_STRING:  case LMD_TYPE_SYMBOL:  case LMD_TYPE_DTIME:  case LMD_TYPE_BINARY:
        String *str = (String*)itm.pointer;
        str->ref_cnt++;
        break;
    case LMD_TYPE_RAW_POINTER: 
        TypeId type_id = *((uint8_t*)itm.raw_pointer);
        if (type_id >= LMD_TYPE_LIST && type_id <= LMD_TYPE_ELEMENT) {
            Container *container = (Container*)itm.raw_pointer;
            container->ref_cnt++;
        }
        break;
    }
}

void array_append(Array* arr, LambdaItem itm, VariableMemPool *pool) {
    if (arr->length + arr->extra + 2 > arr->capacity) { expand_list((List*)arr); }
    array_set(arr, arr->length, itm, pool);
    arr->length++;
}

Array* array_fill(Array* arr, int count, ...) {
    if (count > 0) {
        va_list args;
        va_start(args, count);
        arr->capacity = count;
        arr->items = Malloc(count * sizeof(Item));
        for (int i = 0; i < count; i++) {
            if (arr->length + arr->extra + 2 > arr->capacity) { expand_list((List*)arr); }
            array_set(arr, i, va_arg(args, LambdaItem), NULL);
            arr->length++;
        }
        va_end(args);
    }
    frame_end();
    StrBuf *strbuf = strbuf_new();
    print_item(strbuf, (Item)arr);
    printf("array_fill: %s\n", strbuf->str);
    return arr;
}

Item array_get(Array *array, int index) {
    if (index < 0 || index >= array->length) { return ITEM_NULL; }
    LambdaItem item = (LambdaItem)array->items[index];
    switch (item.type_id) {
    case LMD_TYPE_INT64:
        long lval = *(long*)item.pointer;
        return push_l(lval);
    case LMD_TYPE_FLOAT:
        double dval = *(double*)item.pointer;
        return push_d(dval);
    default:
        return item.item;
    }    
}

ArrayLong* array_long_new(int count, ...) {
    if (count <= 0) { return NULL; }
    va_list args;
    va_start(args, count);
    ArrayLong *arr = heap_alloc(sizeof(ArrayLong), LMD_TYPE_ARRAY_INT);
    arr->type_id = LMD_TYPE_ARRAY_INT;  arr->capacity = count;
    arr->items = Malloc(count * sizeof(long));
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
    printf("list expand: old capacity %ld, length %ld, extra %ld\n", 
        list->capacity, list->length, list->extra);
    list->capacity = list->capacity ? list->capacity * 2 : 8;
    // list items are allocated from C heap, instead of Lambda heap
    // to consider: could also alloc directly from Lambda heap without the heap entry
    // need to profile to see which is faster
    Item* old_items = list->items;
    list->items = Realloc(list->items, list->capacity * sizeof(Item));
    // copy extra items to the end of the list
    if (list->extra) {
        memcpy(list->items + (list->capacity - list->extra), 
            list->items + (list->capacity/2 - list->extra), list->extra * sizeof(Item));
        // scan the list, if the item is long/double,
        // and is stored in the list extra slots, need to update the pointer
        for (int i = 0; i < list->length; i++) {
            LambdaItem itm = (LambdaItem)list->items[i];
            if (itm.type_id == LMD_TYPE_FLOAT || itm.type_id == LMD_TYPE_INT64) {
                Item* old_pointer = (Item*)itm.pointer;
                // Only update pointers that are in the old list buffer's extra space
                if (old_items <= old_pointer && old_pointer < old_items + list->capacity/2) {
                    printf("list expand: item %d, old pointer %p\n", i, old_pointer);
                    int offset = old_items + list->capacity/2 - old_pointer;
                    void* new_pointer = list->items + list->capacity - offset;
                    list->items[i] = itm.type_id == LMD_TYPE_FLOAT ? d2it(new_pointer) : l2it(new_pointer);
                }
                // if the pointer is not in the old buffer, it should not be updated
            }
        }
    }
}

void list_push(List *list, Item item) {
    if (!item) { return; }  // NULL value
    LambdaItem itm = {.item = item};
    if (itm.type_id == LMD_TYPE_NULL) { 
        return;  // skip NULL value
    }
    if (itm.type_id == LMD_TYPE_RAW_POINTER) {
        TypeId type_id = *((uint8_t*)itm.raw_pointer);
        // nest list is flattened
        if (type_id == LMD_TYPE_LIST) {
            // copy over the items
            List *nest_list = (List*)itm.raw_pointer;
            for (int i = 0; i < nest_list->length; i++) {
                Item nest_item = nest_list->items[i];
                list_push(list, nest_item);
            }
            return;
        }
        else if (type_id == LMD_TYPE_RANGE) {
            // copy over the items
            Range *range = (Range*)itm.raw_pointer;
            for (int i = range->start; i <= range->end; i++) {
                // todo: handle value > 32-bit
                Item ival = i2it(i);
                list_push(list, ival);
            }
            return;
        }
        else if (type_id == LMD_TYPE_ARRAY || type_id == LMD_TYPE_ARRAY_INT || 
            type_id == LMD_TYPE_MAP || type_id == LMD_TYPE_ELEMENT) {
            Container *container = (Container*)itm.raw_pointer;
            container->ref_cnt++;
        }
    }
    // store the value in the list (and we may need two slots for long/double)
    if (list->length + list->extra + 2 > list->capacity) { expand_list(list); }
    list->items[list->length++] = item;
    // printf("list push item: type: %d, length: %ld\n", itm.type_id, list->length);
    switch (itm.type_id) {
    case LMD_TYPE_STRING:  case LMD_TYPE_SYMBOL:  case LMD_TYPE_DTIME:  case LMD_TYPE_BINARY:
        String *str = (String*)itm.pointer;
        str->ref_cnt++;
        break;
    case LMD_TYPE_FLOAT:
        double* dval = (double*)(list->items + (list->capacity - list->extra - 1));
        *dval = *(double*)itm.pointer;  list->items[list->length-1] = d2it(dval);
        list->extra++;
        printf("list push float: %f, extra: %ld\n", *dval, list->extra);
        break;
    case LMD_TYPE_INT64:
        long* ival = (long*)(list->items + (list->capacity - list->extra - 1));
        *ival = *(long*)itm.pointer;  list->items[list->length-1] = l2it(ival);
        list->extra++;
        break;
    }
}

Item list_fill(List *list, int count, ...) {
    printf("list_fill cnt: %d\n", count);
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
    frame_end();
    return list->length ? (list->length == 1 && list->type_id != LMD_TYPE_ELEMENT 
        ? list->items[0] : (Item)list) : ITEM_NULL;
}

Item list_get(List *list, int index) {
    if (index < 0 || index >= list->length) { return ITEM_NULL; }
    LambdaItem item = (LambdaItem)list->items[index];
    switch (item.type_id) {
    case LMD_TYPE_INT64:
        long lval = *(long*)item.pointer;
        return push_l(lval);
    case LMD_TYPE_FLOAT:
        double dval = *(double*)item.pointer;
        return push_d(dval);
    default:
        return item.item;
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
            LambdaItem itm = {.item = va_arg(args, uint64_t)};
            if (itm.type_id == LMD_TYPE_RAW_POINTER && *((TypeId*)itm.raw_pointer) == LMD_TYPE_MAP) {
                Map* nested_map = (Map*)itm.raw_pointer;
                nested_map->ref_cnt++;
                *(Map**)field_ptr = nested_map;
            } else {
                printf("expected a map, got type %d\n", itm.type_id );
            }
        } else {
            switch (field->type->type_id) {
            case LMD_TYPE_NULL:
                *(bool*)field_ptr = va_arg(args, bool);
                break;
            case LMD_TYPE_BOOL:
                *(bool*)field_ptr = va_arg(args, bool);
                printf("field bool value: %s\n", *(bool*)field_ptr ? "true" : "false");
                break;                
            case LMD_TYPE_INT:  case LMD_TYPE_INT64:
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
                printf("field string value: %s\n", str->chars);
                *(String**)field_ptr = str;
                str->ref_cnt++;
                break;
            case LMD_TYPE_ARRAY:  case LMD_TYPE_ARRAY_INT:
            case LMD_TYPE_LIST:  case LMD_TYPE_MAP:  case LMD_TYPE_ELEMENT:
                Container *container = va_arg(args, Container*);
                *(Container**)field_ptr = container;
                container->ref_cnt++;
                break;            
            case LMD_TYPE_FUNC:  case LMD_TYPE_ANY:
                void *arr = va_arg(args, void*);
                *(void**)field_ptr = arr;
                break;
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

TypeMap EmptyMap = {
    .type_id = LMD_TYPE_MAP,
    .type_index = -1,
};

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

Item _map_get(TypeMap* map_type,void* map_data, char *key, bool *is_found) {
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
        if (strncmp(field->name->str, key, field->name->length) == 0) {
            *is_found = true;
            TypeId type_id = field->type->type_id;
            void* field_ptr = (char*)map_data + field->byte_offset;
            switch (type_id) {
                case LMD_TYPE_NULL:
                    return ITEM_NULL;
                case LMD_TYPE_BOOL:
                    return b2it(*(bool*)field_ptr);
                case LMD_TYPE_INT:
                    return i2it(*(int*)field_ptr);
                case LMD_TYPE_INT64:
                    long lval = *(long*)field_ptr;
                    return push_l(lval);
                case LMD_TYPE_FLOAT:
                    double dval = *(double*)field_ptr;
                    return push_d(dval);
                case LMD_TYPE_DTIME:
                    //hashmap_set(context->data_owners, &(DataOwner){.data = field_ptr, .owner = map});
                    return k2it(*(char**)field_ptr);
                case LMD_TYPE_STRING:
                    //hashmap_set(context->data_owners, &(DataOwner){.data = field_ptr, .owner = map});
                    return s2it(*(char**)field_ptr);
                case LMD_TYPE_SYMBOL:
                    //hashmap_set(context->data_owners, &(DataOwner){.data = field_ptr, .owner = map});
                    return y2it(*(char**)field_ptr);
                case LMD_TYPE_BINARY:
                    //hashmap_set(context->data_owners, &(DataOwner){.data = field_ptr, .owner = map});
                    return x2it(*(char**)field_ptr);
                case LMD_TYPE_ARRAY:  case LMD_TYPE_ARRAY_INT:
                case LMD_TYPE_LIST:  case LMD_TYPE_MAP:
                    return (Item)*(Map**)field_ptr;
                default:
                    printf("unknown type %d\n", type_id);
                    return ITEM_ERROR;
            }
        }
        field = field->next;
    }
    printf("key %s not found\n", key);
    *is_found = false;
    return ITEM_NULL;
}

Item map_get(Map* map, Item key) {
    printf("map_get %p\n", map);
    if (!map || !key) { return ITEM_NULL;}
    bool is_found;
    char *key_str = NULL;
    LambdaItem key_item = {.item = key};
    if (key_item.type_id == LMD_TYPE_STRING || key_item.type_id == LMD_TYPE_SYMBOL) {
        key_str = ((String*)key_item.pointer)->chars;
    } else {
        printf("map_get: key must be string or symbol, got type %d\n", key_item.type_id);
        return ITEM_NULL;  // only string or symbol keys are supported
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

TypeElmt EmptyElmt = {
    .type_id = LMD_TYPE_ELEMENT,
    .type_index = -1,
    .name = {0},
};

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
    printf("elmt_get %p\n", elmt);
    if (!elmt || !key) { return ITEM_NULL;}
    bool is_found;
    char *key_str = NULL;
    LambdaItem key_item = {.item = key};
    if (key_item.type_id == LMD_TYPE_STRING || key_item.type_id == LMD_TYPE_SYMBOL) {
        key_str = ((String*)key_item.pointer)->chars;
    } else {
        printf("elmt_get: key must be string or symbol, got type %d\n", key_item.type_id);
        return ITEM_NULL;  // only string or symbol keys are supported
    }
    printf("elmt_get key: %s\n", key_str);
    return _map_get((TypeMap*)elmt->type, elmt->data, key_str, &is_found);
}

bool item_true(Item itm) {
    LambdaItem item = {.item = itm};
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

Item push_d(double dval) {
    printf("push_d: %g\n", dval);
    double *dptr = num_stack_push_double((num_stack_t *)context->num_stack, dval); // stack_alloc(sizeof(double));
    return (Item) d2it(dptr);
}

Item push_l(long lval) {
    printf("push_l: %ld\n", lval);
    long *lptr = num_stack_push_long((num_stack_t *)context->num_stack, lval); // stack_alloc(sizeof(long));
    return (Item) l2it(lptr);
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

Item add(Item a, Item b) {
    LambdaItem item_a = {.item = a};  LambdaItem item_b = {.item = b};
    // todo: join binary, list, array, map
    if (item_a.type_id == LMD_TYPE_STRING && item_b.type_id == LMD_TYPE_STRING) {
        String *str_a = (String*)item_a.pointer;  String *str_b = (String*)item_b.pointer;
        String *result = str_cat(str_a, str_b);
        return s2it(result);
    }
    else if (item_a.type_id == LMD_TYPE_INT && item_b.type_id == LMD_TYPE_INT) {
        return i2it(item_a.long_val + item_b.long_val);
    }
    else if (item_a.type_id == LMD_TYPE_INT64 && item_b.type_id == LMD_TYPE_INT64) {
        return l2it(push_l(*(long*)item_a.pointer + *(long*)item_b.pointer));
    }
    else if (item_a.type_id == LMD_TYPE_FLOAT && item_b.type_id == LMD_TYPE_FLOAT) {
        printf("add float: %g + %g\n", *(double*)item_a.pointer, *(double*)item_b.pointer);
        return d2it(push_d(*(double*)item_a.pointer + *(double*)item_b.pointer));
    }
    else if (item_a.type_id == LMD_TYPE_INT && item_b.type_id == LMD_TYPE_FLOAT) {
        return d2it(push_d((double)item_a.long_val + *(double*)item_b.pointer));
    }
    else if (item_a.type_id == LMD_TYPE_FLOAT && item_b.type_id == LMD_TYPE_INT) {
        return d2it(push_d(*(double*)item_a.pointer + (double)item_b.long_val));
    }
    else {
        printf("unknown add type: %d, %d\n", item_a.type_id, item_b.type_id);
    }
    return ITEM_ERROR;
}

Range* fn_to(Item a, Item b) {
    LambdaItem item_a = (LambdaItem)a;  LambdaItem item_b = (LambdaItem)b;
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

long it2l(Item item) {
    LambdaItem itm = {.item = item};
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

double it2d(Item item) {
    LambdaItem itm = {.item = item};
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
    Function *fn = calloc(1, sizeof(Function));
    fn->type_id = LMD_TYPE_FUNC;
    fn->ptr = ptr;
    return fn;
}

bool fn_is(Item a, Item b) {
    printf("is expr\n");
    LambdaItem a_item = (LambdaItem)a, b_item = (LambdaItem)b;
    if (b_item.type_id || *((uint8_t*)b) != LMD_TYPE_TYPE) {
        return false;
    }
    TypeType *type_b = (TypeType *)b;
    TypeId a_type_id = a_item.type_id ? a_item.type_id : *((uint8_t*)a);
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

bool equal(Item a, Item b) {
    printf("equal expr\n");
    LambdaItem a_item = {.item = a};
    LambdaItem b_item = {.item = b};
    if (a_item.type_id != b_item.type_id) {
        // number promotion
        if (LMD_TYPE_INT <= a_item.type_id && a_item.type_id <= LMD_TYPE_NUMBER && 
            LMD_TYPE_INT <= b_item.type_id && b_item.type_id <= LMD_TYPE_NUMBER) {
            double a_val = it2d(a_item.item);
            double b_val = it2d(b_item.item);
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

bool fn_in(Item a, Item b) {
    printf("in expr\n");
    LambdaItem a_item = {.item = a};
    LambdaItem b_item = {.item = b};
    if (b_item.type_id) { // b is scalar
        if (b_item.type_id == LMD_TYPE_STRING && a_item.type_id == LMD_TYPE_STRING) {
            String *str_a = (String*)a_item.pointer;  String *str_b = (String*)b_item.pointer;
            return str_a->len <= str_b->len && strstr(str_b->chars, str_a->chars) != NULL;
        }
    }
    else { // b is container
        TypeId b_type = *((uint8_t*)b);
        if (b_type == LMD_TYPE_LIST) {
            List *list = (List*)b;
            for (int i = 0; i < list->length; i++) {
                if (equal(list->items[i], a)) {
                    return true;
                }
            }
            return false;
        }
        else if (b_type == LMD_TYPE_RANGE) {
            Range *range = (Range*)b;
            long a_val = it2l(a);
            return range->start <= a_val && a_val <= range->end;
        }
        else if (b_type == LMD_TYPE_ARRAY) {
            Array *arr = (Array*)b;
            for (int i = 0; i < arr->length; i++) {
                if (equal(arr->items[i], a)) {
                    return true;
                }
            }
            return false;
        }
        else if (b_type == LMD_TYPE_ARRAY_INT) {
            ArrayLong *arr = (ArrayLong*)b;
            for (int i = 0; i < arr->length; i++) {
                if (arr->items[i] == it2l(a)) {
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

String* fn_string(Item item) {
    LambdaItem itm = {.item = item};
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
    LambdaItem itm = {.item = item};
    TypeType *type = calloc(1, sizeof(TypeType) + sizeof(Type)); 
    Type *item_type = (Type *)((uint8_t *)type + sizeof(TypeType));
    type->type = item_type;  type->type_id = LMD_TYPE_TYPE;
    if (itm.type_id) {
        item_type->type_id = itm.type_id;
    }
    else if (itm.type_id == LMD_TYPE_RAW_POINTER) {
        item_type->type_id = *((uint8_t*)item);
    }
    return (Type*)type;
}

Input* input_data(Context* ctx, String* url, String* type);

Item fn_input(Item url, Item type) {
    String* url_str;
    LambdaItem url_item = {.item = url};
    if (url_item.type_id != LMD_TYPE_STRING && url_item.type_id != LMD_TYPE_SYMBOL) {
        printf("input url must be a string or symbol, got type %d\n", url_item.type_id);
        return ITEM_NULL;  // todo: push error
    }
    else {
        url_str = (String*)url_item.pointer;
    }
    LambdaItem type_item = {.item = type};
    String* type_str;
    if (type_item.type_id != LMD_TYPE_NULL && type_item.type_id != LMD_TYPE_STRING && type_item.type_id != LMD_TYPE_SYMBOL) {
        printf("input type must be a string or symbol, got type %d\n", type_item.type_id);
        return ITEM_NULL;  // todo: push error
    }
    else {
        type_str = (type_item.type_id == LMD_TYPE_NULL) ? NULL : (String*)type_item.pointer;
    }
    Input *input = input_data(context, url_str, type_str);
    // todo: input should be cached in context
    return (input && input->root) ? input->root : ITEM_NULL;
}

void fn_print(Item item) {
    String *str = fn_string(item);
    if (str) {
        printf("%s\n", str->chars);
    }
}

String* format_data(Context* ctx, Item item, String* type);

String* fn_format(Item item, Item type) {
    LambdaItem type_item = {.item = type};
    String* type_str;
    if (type_item.type_id != LMD_TYPE_NULL && type_item.type_id != LMD_TYPE_STRING && type_item.type_id != LMD_TYPE_SYMBOL) {
        printf("format type must be a string or symbol, got type %d\n", type_item.type_id);
        return NULL;  // todo: push error
    }
    else {
        type_str = (type_item.type_id == LMD_TYPE_NULL) ? NULL : (String*)type_item.pointer;
    }
    // printf("format item type: %s\n", type_str ? type_str->chars : "null");
    return format_data(context, item, type_str);
}

int utf8_char_count(const char* utf8_string);

// generic field access function for any type
Item fn_index(Item item, long index) {
    // Determine the type and delegate to appropriate getter
    LambdaItem litem = (LambdaItem)item;
    TypeId type_id = get_type_id(litem);

    switch (type_id) {
    case LMD_TYPE_RANGE: {
        Range *range = (Range*)litem.raw_pointer;
        if (index < range->start || index > range->end) { return ITEM_NULL; }
        long value = range->start + index;
        return i2it(value);
    }
    case LMD_TYPE_ARRAY:
        return array_get((Array*)litem.raw_pointer, (int)index);
    case LMD_TYPE_ARRAY_INT: {
        ArrayLong *arr = (ArrayLong*)litem.raw_pointer;
        if (index < 0 || index >= arr->length) { return ITEM_NULL; }
        return i2it(arr->items[index]);
    }
    case LMD_TYPE_LIST:
        return list_get((List*)litem.raw_pointer, (int)index);
    case LMD_TYPE_ELEMENT:
        // treat element as list for index access
        return list_get((List*)litem.raw_pointer, (int)index);
    case LMD_TYPE_MAP:
        // to consider: should we return ITEM_NULL or ITEM_ERROR? 
        return ITEM_NULL;
    // todo: string, symbol, dtime, binary, etc.
    default:
        return ITEM_NULL;
    }
}

Item fn_member(Item item, Item key) {
    LambdaItem litem = (LambdaItem)item;
    TypeId type_id = get_type_id(litem);
    switch (type_id) {
    case LMD_TYPE_MAP:
        Map *map = (Map*)litem.raw_pointer;
        return map_get(map, key);
    case LMD_TYPE_ELEMENT:
        Element *elmt = (Element*)litem.raw_pointer;
        return elmt_get(elmt, key);
    case LMD_TYPE_LIST: {
        // Handle built-in properties for List type
        LambdaItem key_item = (LambdaItem)key;
        if (key_item.type_id == LMD_TYPE_STRING || key_item.type_id == LMD_TYPE_SYMBOL) {
            String *key_str = (String*)key_item.pointer;
            if (key_str && strcmp(key_str->chars, "length") == 0) {
                List *list = (List*)litem.raw_pointer;
                return i2it(list->length);
            }
        }
        return ITEM_NULL;
    }
    // todo: built-in properties for other types
    default:
        return ITEM_NULL;
    }
}

// length function for item
Item fn_len(Item item) {
    LambdaItem litem = (LambdaItem)item;
    TypeId type_id = get_type_id(litem);
    printf("fn_len item: %d\n", type_id);
    long size = 0;
    switch (type_id) {
    case LMD_TYPE_LIST:
        size = ((List*)litem.raw_pointer)->length;
        break;
    case LMD_TYPE_RANGE:
        size = ((Range*)litem.raw_pointer)->length;
        break;
    case LMD_TYPE_ARRAY:
        size = ((Array*)litem.raw_pointer)->length;
        break;
    case LMD_TYPE_ARRAY_INT:
        size = ((ArrayLong*)litem.raw_pointer)->length;
        break;
    case LMD_TYPE_MAP:
        TypeMap *map_type = (TypeMap*)((Map*)litem.raw_pointer)->type;
        // returns the num of fields in the map
        size = map_type ? map_type->length : 0;
        break;
    case LMD_TYPE_ELEMENT: {
        Element *elmt = (Element*)litem.raw_pointer;
        TypeElmt *elmt_type = (TypeElmt*)elmt->type;
        // returns the num of fields in the element
        size = (elmt_type ? elmt_type->length : 0) + elmt->length;
        break;
    }
    case LMD_TYPE_STRING:  case LMD_TYPE_SYMBOL:  case LMD_TYPE_BINARY: {
        // returns the length of the string
        // todo: binary length
        String *str = (String*)litem.pointer;  // todo:: should return char length
        size = str ? utf8_char_count(str->chars) : 0;
        break;
    }
    case LMD_TYPE_ERROR:
        return ITEM_ERROR;
    default: // NULL and scalar types
        size = 0;
        break;
    }
    return i2it(size);
}