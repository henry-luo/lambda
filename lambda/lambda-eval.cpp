#include "transpiler.hpp"
#include "unicode_config.h"
#if LAMBDA_UNICODE_LEVEL >= LAMBDA_UNICODE_UTF8PROC
#include "utf_string.h"
#elif LAMBDA_UNICODE_LEVEL >= LAMBDA_UNICODE_COMPACT
#include "unicode_string.h"
#endif
#include <stdarg.h>
#include <time.h>
#include <cstdlib>  // for abs function
#include <cmath>    // for pow function

extern __thread Context* context;

#define stack_alloc(size) alloca(size);

#define Malloc malloc
#define Realloc realloc

// Helper functions for decimal operations
Item push_decimal(mpd_t* dec_val) {
    if (!dec_val) return ItemError;
    
    Decimal* decimal = (Decimal*)heap_alloc(sizeof(Decimal), LMD_TYPE_DECIMAL);
    if (!decimal) {
        mpd_del(dec_val);
        return ItemError;
    }
    
    decimal->ref_cnt = 1;
    decimal->dec_val = dec_val;
    
    Item result;
    result.item = c2it(decimal);
    return result;
}

mpd_t* convert_to_decimal(Item item, mpd_context_t* ctx) {
    if (item.type_id == LMD_TYPE_DECIMAL) {
        Decimal* dec_ptr = (Decimal*)item.pointer;
        return dec_ptr->dec_val;
    }
    
    mpd_t* result = mpd_new(ctx);
    if (!result) return NULL;
    
    if (item.type_id == LMD_TYPE_INT) {
        long signed_val = (long)((int64_t)(item.long_val << 8) >> 8);
        mpd_set_ssize(result, signed_val, ctx);
    } else if (item.type_id == LMD_TYPE_INT64) {
        long val = *(long*)item.pointer;
        mpd_set_ssize(result, val, ctx);
    } else if (item.type_id == LMD_TYPE_FLOAT) {
        double val = *(double*)item.pointer;
        char str_buf[64];
        snprintf(str_buf, sizeof(str_buf), "%.17g", val);
        mpd_set_string(result, str_buf, ctx);
    } else {
        mpd_del(result);
        return NULL;
    }
    
    return result;
}

void cleanup_temp_decimal(mpd_t* dec_val, bool is_original_decimal) {
    // Only delete if this was a temporary decimal we created
    if (!is_original_decimal && dec_val) {
        mpd_del(dec_val);
    }
}

bool decimal_is_zero(mpd_t* dec_val) {
    return mpd_iszero(dec_val);
}

void expand_list(List *list);

Array* array() {
    Array *arr = (Array*)calloc(1, sizeof(Array));
    arr->type_id = LMD_TYPE_ARRAY;
    frame_start();
    return arr;
}

Item array_to_item(Array* arr) {
    if (!arr) {
        return ItemError;
    }
    Item item;
    item.type_id = LMD_TYPE_ARRAY;
    item.array = arr;
    return item;
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
    case LMD_TYPE_DTIME:  {
        DateTime* dtval = (DateTime*)(arr->items + (arr->capacity - arr->extra - 1));
        *dtval = *(DateTime*)itm.pointer;  arr->items[index] = {.item = k2it(dtval)};
        arr->extra++;
        break;
    }
    case LMD_TYPE_STRING:  case LMD_TYPE_SYMBOL:  case LMD_TYPE_BINARY: {
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
        // DateTime dtval = *(DateTime*)item.pointer;
        long dtval = *(long*)item.pointer;
        return push_k(dtval);
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

// Wrapper function to return an Item instead of raw ArrayLong*
Item array_long_new_item(int count, ...) {
    if (count <= 0) { 
        ArrayLong *arr = (ArrayLong*)heap_alloc(sizeof(ArrayLong), LMD_TYPE_ARRAY_INT);
        arr->type_id = LMD_TYPE_ARRAY_INT;
        arr->capacity = 0;
        arr->items = NULL;
        arr->length = 0;
        return (Item){.array_long = arr};
    }
    va_list args;
    va_start(args, count);
    ArrayLong *arr = (ArrayLong*)heap_alloc(sizeof(ArrayLong), LMD_TYPE_ARRAY_INT);
    arr->type_id = LMD_TYPE_ARRAY_INT;  
    arr->capacity = count;
    arr->items = (long*)Malloc(count * sizeof(long));
    arr->length = count;
    for (int i = 0; i < count; i++) {
        arr->items[i] = va_arg(args, long);
    }       
    va_end(args);
    return (Item){.array_long = arr};
}

List* list() {
    List *list = (List *)heap_calloc(sizeof(List), LMD_TYPE_LIST);
    list->type_id = LMD_TYPE_LIST;
    frame_start();
    return list;
}

void list_push(List *list, Item item) {
    if (item.item <= 1024) {
        if (!item.item) { return; }  // NULL value
        // treat as invalid pointer
        printf("list_push: invalid raw pointer: %p\n", item.raw_pointer);
        return;
    }
    TypeId type_id = get_type_id(item);
    // printf("list_push: pushing item: type_id: %d\n", type_id);
    if (type_id == LMD_TYPE_NULL) { return; } // skip NULL value
    if (type_id == LMD_TYPE_LIST) { // nest list is flattened
        // printf("list_push: pushing nested list: %p, type_id: %d\n", item.list, type_id);
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
    
    // store the value in the list (and we may need two slots for long/double)
    //printf("list push item: type: %d, length: %ld\n", item.type_id, list->length);
    if (list->length + list->extra + 2 > list->capacity) { expand_list(list); }
    list->items[list->length++] = item;
    switch (item.type_id) {
    case LMD_TYPE_STRING:  case LMD_TYPE_SYMBOL:  case LMD_TYPE_BINARY: {
        String *str = (String*)item.pointer;
        printf("DEBUG list_push: string/symbol/binary value: %s\n", str->chars);
        str->ref_cnt++;
        break;
    }
    case LMD_TYPE_DECIMAL: {
        Decimal *dval = (Decimal*)item.pointer;
        if (dval && dval->dec_val) {
            char *buf = mpd_to_sci(dval->dec_val, 1);
            printf("DEBUG list_push: pushed decimal value: %s\n", buf ? buf : "null");
            if (buf) free(buf);
        } else {
            printf("DEBUG list_push: pushed null decimal value\n");
        }
        dval->ref_cnt++;
        break;
    }
    case LMD_TYPE_FLOAT: {
        double* dval = (double*)(list->items + (list->capacity - list->extra - 1));
        *dval = *(double*)item.pointer;
        printf("DEBUG list_push: Pushed float value: %f\n", *dval);
        list->items[list->length-1] = {.item = d2it(dval)};
        list->extra++;
        break;
    }
    case LMD_TYPE_INT64: {
        long* ival = (long*)(list->items + (list->capacity - list->extra - 1));
        *ival = *(long*)item.pointer;  list->items[list->length-1] = {.item = l2it(ival)};
        list->extra++;
        break;
    }
    case LMD_TYPE_DTIME:  {
        DateTime* dtval = (DateTime*)(list->items + (list->capacity - list->extra - 1));
        *dtval = *(DateTime*)item.pointer;  list->items[list->length-1] = {.item = k2it(dtval)};
        list->extra++;
        break;
    }
    }
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
    printf("list_filled: %ld items\n", list->length);
    if (list->length == 0) {
        return ItemNull;
    } else if (list->length == 1 && list->type_id != LMD_TYPE_ELEMENT) {
        return list->items[0];
    } else {
        Item result = {.list = list};
        result.type_id = LMD_TYPE_LIST;
        return result;
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
            case LMD_TYPE_DTIME:  {
                DateTime* dtval = va_arg(args, DateTime*);
                *(DateTime**)field_ptr = dtval;
                printf("field datetime value\n");
                break;
            }
            case LMD_TYPE_STRING:  case LMD_TYPE_SYMBOL:  case LMD_TYPE_BINARY: {
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
            case LMD_TYPE_TYPE:  case LMD_TYPE_FUNC: {
                void *arr = va_arg(args, void*);
                *(void**)field_ptr = arr;
                break;
            }
            case LMD_TYPE_ANY: {
                Item item = va_arg(args, Item);
                *(Item*)field_ptr = item;
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
            case LMD_TYPE_ANY:
                return *(Item*)field_ptr;
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

// Convert Item to boolean Item, preserving errors
Item safe_b2it(Item item) {
    switch (item.type_id) {
    case LMD_TYPE_ERROR:
        // Preserve error - don't convert to boolean
        return item;
    case LMD_TYPE_BOOL:
        // Already boolean, return as-is
        return item;
    case LMD_TYPE_NULL:
        return {.item = b2it(false)};
    default:
        // Convert to boolean based on truthiness
        return {.item = b2it(item_true(item))};
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
    printf("TRACE: push_d: %g\n", dval);
    double *dptr = num_stack_push_double((num_stack_t *)context->num_stack, dval); // stack_alloc(sizeof(double));
    Item result = {.item = d2it(dptr)};
    printf("TRACE: push_d result item: %llu, type_id: %d\n", result.item, result.type_id);
    return result;
}

Item push_l(long lval) {
    printf("TRACE: push_l: %ld\n", lval);
    long *lptr = num_stack_push_long((num_stack_t *)context->num_stack, lval); // stack_alloc(sizeof(long));
    Item result = {.item = l2it(lptr)};
    printf("TRACE: push_l result item: %llu, type_id: %d\n", result.item, result.type_id);
    return result;
}

Item push_k(long val) {
    DateTime dtval = *(DateTime*)&val;
    DateTime *dtptr = num_stack_push_datetime((num_stack_t *)context->num_stack, dtval); // stack_alloc(sizeof(DateTime));
    return {.item = k2it(dtptr)};
}

String *str_cat(String *left, String *right) {
    printf("str_cat %p, %p\n", left, right);
    if (!left || !right) {
        printf("Error: null pointer in str_cat: left=%p, right=%p\n", left, right);
        return NULL;
    }
    size_t left_len = left->len;
    size_t right_len = right->len;
    printf("left len %zu, right len %zu\n", left_len, right_len);
    String *result = (String *)heap_alloc(sizeof(String) + left_len + right_len + 1, LMD_TYPE_STRING);
    if (!result) {
        printf("Error: failed to allocate memory for str_cat result\n");
        return NULL;
    }
    printf("str result %p\n", result);
    result->ref_cnt = 0;  result->len = left_len + right_len;
    memcpy(result->chars, left->chars, left_len);
    // copy the string and '\0'
    memcpy(result->chars + left_len, right->chars, right_len + 1);
    printf("str_cat result: %s\n", result->chars);
    return result;
}

Item fn_add(Item item_a, Item item_b) {
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
    // Add libmpdec decimal support
    else if (item_a.type_id == LMD_TYPE_DECIMAL || item_b.type_id == LMD_TYPE_DECIMAL) {
        mpd_t* a_dec = convert_to_decimal(item_a, &context->decimal_ctx);
        mpd_t* b_dec = convert_to_decimal(item_b, &context->decimal_ctx);
        
        if (!a_dec || !b_dec) {
            if (a_dec) cleanup_temp_decimal(a_dec, item_a.type_id);
            if (b_dec) cleanup_temp_decimal(b_dec, item_b.type_id);
            printf("decimal conversion failed in fn_add\n");
            return ItemError;
        }
        
        mpd_t* result = mpd_new(&context->decimal_ctx);
        if (!result) {
            cleanup_temp_decimal(a_dec, item_a.type_id == LMD_TYPE_DECIMAL);
            cleanup_temp_decimal(b_dec, item_b.type_id == LMD_TYPE_DECIMAL);
            return ItemError;
        }
        
        mpd_add(result, a_dec, b_dec, &context->decimal_ctx);
        
        cleanup_temp_decimal(a_dec, item_a.type_id == LMD_TYPE_DECIMAL);
        cleanup_temp_decimal(b_dec, item_b.type_id == LMD_TYPE_DECIMAL);
        
        if (mpd_isnan(result) || mpd_isinfinite(result)) {
            mpd_del(result);
            printf("decimal addition failed\n");
            return ItemError;
        }
        
        return push_decimal(result);
    }
    else {
        printf("unknown add type: %d, %d\n", item_a.type_id, item_b.type_id);
    }
    return ItemError;
}

String *str_repeat(String *str, long times) {
    if (times <= 0) {
        // Return empty string
        String *result = (String *)heap_alloc(sizeof(String) + 1, LMD_TYPE_STRING);
        result->ref_cnt = 0;
        result->len = 0;
        result->chars[0] = '\0';
        return result;
    }
    
    size_t str_len = str->len;
    size_t total_len = str_len * times;
    String *result = (String *)heap_alloc(sizeof(String) + total_len + 1, LMD_TYPE_STRING);
    result->ref_cnt = 0;
    result->len = total_len;
    
    for (long i = 0; i < times; i++) {
        memcpy(result->chars + (i * str_len), str->chars, str_len);
    }
    result->chars[total_len] = '\0';
    
    return result;
}

Item fn_mul(Item item_a, Item item_b) {
    if (item_a.type_id == LMD_TYPE_INT && item_b.type_id == LMD_TYPE_INT) {
        return {.item = i2it(item_a.long_val * item_b.long_val)};
    }
    else if (item_a.type_id == LMD_TYPE_INT64 && item_b.type_id == LMD_TYPE_INT64) {
        return push_l(*(long*)item_a.pointer * *(long*)item_b.pointer);
    }
    else if (item_a.type_id == LMD_TYPE_FLOAT && item_b.type_id == LMD_TYPE_FLOAT) {
        printf("mul float: %g * %g\n", *(double*)item_a.pointer, *(double*)item_b.pointer);
        return push_d(*(double*)item_a.pointer * *(double*)item_b.pointer);
    }
    else if (item_a.type_id == LMD_TYPE_INT && item_b.type_id == LMD_TYPE_FLOAT) {
        return push_d((double)item_a.long_val * *(double*)item_b.pointer);
    }
    else if (item_a.type_id == LMD_TYPE_FLOAT && item_b.type_id == LMD_TYPE_INT) {
        return push_d(*(double*)item_a.pointer * (double)item_b.long_val);
    }
    else if (item_a.type_id == LMD_TYPE_STRING && item_b.type_id == LMD_TYPE_INT) {
        String *str_a = (String*)item_a.pointer;
        String *result = str_repeat(str_a, item_b.long_val);
        return {.item = s2it(result)};
    }
    else if (item_a.type_id == LMD_TYPE_INT && item_b.type_id == LMD_TYPE_STRING) {
        String *str_b = (String*)item_b.pointer;
        String *result = str_repeat(str_b, item_a.long_val);
        return {.item = s2it(result)};
    }
    // Add libmpdec decimal support
    else if (item_a.type_id == LMD_TYPE_DECIMAL || item_b.type_id == LMD_TYPE_DECIMAL) {
        mpd_t* a_dec = convert_to_decimal(item_a, &context->decimal_ctx);
        mpd_t* b_dec = convert_to_decimal(item_b, &context->decimal_ctx);
        
        if (!a_dec || !b_dec) {
            if (a_dec) cleanup_temp_decimal(a_dec, item_a.type_id);
            if (b_dec) cleanup_temp_decimal(b_dec, item_b.type_id);
            printf("decimal conversion failed in fn_mul\n");
            return ItemError;
        }
        
        mpd_t* result = mpd_new(&context->decimal_ctx);
        if (!result) {
            cleanup_temp_decimal(a_dec, item_a.type_id == LMD_TYPE_DECIMAL);
            cleanup_temp_decimal(b_dec, item_b.type_id == LMD_TYPE_DECIMAL);
            return ItemError;
        }
        
        mpd_mul(result, a_dec, b_dec, &context->decimal_ctx);
        
        cleanup_temp_decimal(a_dec, item_a.type_id == LMD_TYPE_DECIMAL);
        cleanup_temp_decimal(b_dec, item_b.type_id == LMD_TYPE_DECIMAL);
        
        if (mpd_isnan(result) || mpd_isinfinite(result)) {
            mpd_del(result);
            printf("decimal multiplication failed\n");
            return ItemError;
        }
        
        return push_decimal(result);
    }
    else {
        printf("unknown mul type: %d, %d\n", item_a.type_id, item_b.type_id);
    }
    return ItemError;
}

Item fn_sub(Item item_a, Item item_b) {
    if (item_a.type_id == LMD_TYPE_INT && item_b.type_id == LMD_TYPE_INT) {
        return {.item = i2it(item_a.long_val - item_b.long_val)};
    }
    else if (item_a.type_id == LMD_TYPE_INT64 && item_b.type_id == LMD_TYPE_INT64) {
        return push_l(*(long*)item_a.pointer - *(long*)item_b.pointer);
    }
    else if (item_a.type_id == LMD_TYPE_FLOAT && item_b.type_id == LMD_TYPE_FLOAT) {
        printf("sub float: %g - %g\n", *(double*)item_a.pointer, *(double*)item_b.pointer);
        return push_d(*(double*)item_a.pointer - *(double*)item_b.pointer);
    }
    else if (item_a.type_id == LMD_TYPE_INT && item_b.type_id == LMD_TYPE_FLOAT) {
        return push_d((double)item_a.long_val - *(double*)item_b.pointer);
    }
    else if (item_a.type_id == LMD_TYPE_FLOAT && item_b.type_id == LMD_TYPE_INT) {
        return push_d(*(double*)item_a.pointer - (double)item_b.long_val);
    }
    // Add libmpdec decimal support
    else if (item_a.type_id == LMD_TYPE_DECIMAL || item_b.type_id == LMD_TYPE_DECIMAL) {
        mpd_t* a_dec = convert_to_decimal(item_a, &context->decimal_ctx);
        mpd_t* b_dec = convert_to_decimal(item_b, &context->decimal_ctx);
        
        if (!a_dec || !b_dec) {
            if (a_dec) cleanup_temp_decimal(a_dec, item_a.type_id);
            if (b_dec) cleanup_temp_decimal(b_dec, item_b.type_id);
            printf("decimal conversion failed in fn_sub\n");
            return ItemError;
        }
        
        mpd_t* result = mpd_new(&context->decimal_ctx);
        if (!result) {
            cleanup_temp_decimal(a_dec, item_a.type_id == LMD_TYPE_DECIMAL);
            cleanup_temp_decimal(b_dec, item_b.type_id == LMD_TYPE_DECIMAL);
            return ItemError;
        }
        
        mpd_sub(result, a_dec, b_dec, &context->decimal_ctx);
        
        cleanup_temp_decimal(a_dec, item_a.type_id == LMD_TYPE_DECIMAL);
        cleanup_temp_decimal(b_dec, item_b.type_id == LMD_TYPE_DECIMAL);
        
        if (mpd_isnan(result) || mpd_isinfinite(result)) {
            mpd_del(result);
            printf("decimal subtraction failed\n");
            return ItemError;
        }
        
        return push_decimal(result);
    }
    else {
        printf("unknown sub type: %d, %d\n", item_a.type_id, item_b.type_id);
    }
    return ItemError;
}

Item fn_div(Item item_a, Item item_b) {
    if (item_a.type_id == LMD_TYPE_INT && item_b.type_id == LMD_TYPE_INT) {
        if (item_b.long_val == 0) {
            printf("integer division by zero error\n");
            return ItemError;
        }
        return push_d((double)item_a.long_val / (double)item_b.long_val);
    }
    else if (item_a.type_id == LMD_TYPE_INT64 && item_b.type_id == LMD_TYPE_INT64) {
        if (*(long*)item_b.pointer == 0) {
            printf("integer division by zero error\n");
            return ItemError;
        }
        return push_d((double)*(long*)item_a.pointer / (double)*(long*)item_b.pointer);
    }
    else if (item_a.type_id == LMD_TYPE_FLOAT && item_b.type_id == LMD_TYPE_FLOAT) {
        if (*(double*)item_b.pointer == 0.0) {
            printf("float division by zero error\n");
            return ItemError;
        }
        printf("div float: %g / %g\n", *(double*)item_a.pointer, *(double*)item_b.pointer);
        return push_d(*(double*)item_a.pointer / *(double*)item_b.pointer);
    }
    else if (item_a.type_id == LMD_TYPE_INT && item_b.type_id == LMD_TYPE_FLOAT) {
        if (*(double*)item_b.pointer == 0.0) {
            printf("float division by zero error\n");
            return ItemError;
        }
        return push_d((double)item_a.long_val / *(double*)item_b.pointer);
    }
    else if (item_a.type_id == LMD_TYPE_FLOAT && item_b.type_id == LMD_TYPE_INT) {
        if (item_b.long_val == 0) {
            printf("integer division by zero error\n");
            return ItemError;
        }
        return push_d(*(double*)item_a.pointer / (double)item_b.long_val);
    }
    // Add libmpdec decimal support
    else if (item_a.type_id == LMD_TYPE_DECIMAL || item_b.type_id == LMD_TYPE_DECIMAL) {
        mpd_t* a_dec = convert_to_decimal(item_a, &context->decimal_ctx);
        mpd_t* b_dec = convert_to_decimal(item_b, &context->decimal_ctx);
        
        if (!a_dec || !b_dec) {
            if (a_dec) cleanup_temp_decimal(a_dec, item_a.type_id);
            if (b_dec) cleanup_temp_decimal(b_dec, item_b.type_id);
            printf("decimal conversion failed in fn_div\n");
            return ItemError;
        }
        
        // Check for division by zero
        printf("DEBUG: Checking division by zero, b_dec=%p\n", b_dec);
        if (b_dec) {
            char *b_str = mpd_to_sci(b_dec, 1);
            printf("DEBUG: b_dec value as string: '%s'\n", b_str ? b_str : "NULL");
            printf("DEBUG: mpd_iszero result: %d\n", mpd_iszero(b_dec));
            if (b_str) mpd_free(b_str);
        }
        if (b_dec && decimal_is_zero(b_dec)) {
            cleanup_temp_decimal(a_dec, item_a.type_id == LMD_TYPE_DECIMAL);
            cleanup_temp_decimal(b_dec, item_b.type_id == LMD_TYPE_DECIMAL);
            printf("decimal division by zero error\n");
            return ItemError;
        }
        printf("DEBUG: Division by zero check passed\n");
        
        // Debug the values and context before division
        if (a_dec && b_dec) {
            char *a_str = mpd_to_sci(a_dec, 1);
            char *b_str = mpd_to_sci(b_dec, 1);
            printf("DEBUG: About to divide '%s' / '%s'\n", a_str ? a_str : "NULL", b_str ? b_str : "NULL");
            printf("DEBUG: Context prec=%lld, emax=%lld, emin=%lld\n", (long long)context->decimal_ctx.prec, (long long)context->decimal_ctx.emax, (long long)context->decimal_ctx.emin);
            if (a_str) mpd_free(a_str);
            if (b_str) mpd_free(b_str);
        }
        
        mpd_t* result = mpd_new(&context->decimal_ctx);
        if (!result) {
            cleanup_temp_decimal(a_dec, item_a.type_id == LMD_TYPE_DECIMAL);
            cleanup_temp_decimal(b_dec, item_b.type_id == LMD_TYPE_DECIMAL);
            return ItemError;
        }
        
        printf("DEBUG: Calling mpd_div...\n");
        mpd_div(result, a_dec, b_dec, &context->decimal_ctx);
        printf("DEBUG: mpd_div completed\n");
        
        cleanup_temp_decimal(a_dec, item_a.type_id == LMD_TYPE_DECIMAL);
        cleanup_temp_decimal(b_dec, item_b.type_id == LMD_TYPE_DECIMAL);
        
        if (mpd_isnan(result) || mpd_isinfinite(result)) {
            mpd_del(result);
            printf("decimal division failed\n");
            return ItemError;
        }
        
        return push_decimal(result);
    }
    else {
        printf("unknown div type: %d, %d\n", item_a.type_id, item_b.type_id);
    }
    return ItemError;
}

Item fn_idiv(Item item_a, Item item_b) {
    // Check for division by zero
    bool is_zero = false;
    if (item_b.type_id == LMD_TYPE_INT) {
        // Sign extend the 56-bit long_val to a proper signed long
        long signed_val = (long)((int64_t)(item_b.long_val << 8) >> 8);
        is_zero = (signed_val == 0);
    }
    else if (item_b.type_id == LMD_TYPE_INT64 && *(long*)item_b.pointer == 0) {
        is_zero = true;
    }
    
    if (is_zero) {
        printf("integer division by zero error\n");
        return ItemError;
    }

    if (item_a.type_id == LMD_TYPE_INT && item_b.type_id == LMD_TYPE_INT) {
        // Sign extend both values to proper signed longs
        long a_val = (long)((int64_t)(item_a.long_val << 8) >> 8);
        long b_val = (long)((int64_t)(item_b.long_val << 8) >> 8);
        return (Item){.item = i2it(a_val / b_val)};
    }
    else if (item_a.type_id == LMD_TYPE_INT64 && item_b.type_id == LMD_TYPE_INT64) {
        return push_l(*(long*)item_a.pointer / *(long*)item_b.pointer);
    }
    else if (item_a.type_id == LMD_TYPE_INT && item_b.type_id == LMD_TYPE_INT64) {
        long a_val = (long)((int64_t)(item_a.long_val << 8) >> 8);
        return push_l(a_val / *(long*)item_b.pointer);
    }
    else if (item_a.type_id == LMD_TYPE_INT64 && item_b.type_id == LMD_TYPE_INT) {
        long b_val = (long)((int64_t)(item_b.long_val << 8) >> 8);
        return push_l(*(long*)item_a.pointer / b_val);
    }
    else {
        printf("unknown idiv type: %d, %d\n", item_a.type_id, item_b.type_id);
    }
    return ItemError;
}

Item fn_pow(Item item_a, Item item_b) {
    // Handle decimal types first
    if (item_a.type_id == LMD_TYPE_DECIMAL || item_b.type_id == LMD_TYPE_DECIMAL) {
        // For now, convert decimals to double for power operations
        // This is a limitation of libmpdec - it doesn't have general power operations
        double base = 0.0, exponent = 0.0;
        
        if (item_a.type_id == LMD_TYPE_DECIMAL) {
            Decimal* dec_ptr = (Decimal*)item_a.pointer;
            // Convert decimal to string then to double (preserves precision better)
            char* str = mpd_to_sci(dec_ptr->dec_val, 1);
            base = strtod(str, NULL);
            free(str);
        } else {
            // Convert non-decimal to double
            if (item_a.type_id == LMD_TYPE_INT) {
                long signed_val = (long)((int64_t)(item_a.long_val << 8) >> 8);
                base = (double)signed_val;
            } else if (item_a.type_id == LMD_TYPE_INT64) {
                base = (double)(*(long*)item_a.pointer);
            } else if (item_a.type_id == LMD_TYPE_FLOAT) {
                base = *(double*)item_a.pointer;
            } else {
                printf("unsupported pow base type with decimal: %d\n", item_a.type_id);
                return ItemError;
            }
        }
        
        if (item_b.type_id == LMD_TYPE_DECIMAL) {
            Decimal* dec_ptr = (Decimal*)item_b.pointer;
            char* str = mpd_to_sci(dec_ptr->dec_val, 1);
            exponent = strtod(str, NULL);
            free(str);
        } else {
            // Convert non-decimal to double
            if (item_b.type_id == LMD_TYPE_INT) {
                long signed_val = (long)((int64_t)(item_b.long_val << 8) >> 8);
                exponent = (double)signed_val;
            } else if (item_b.type_id == LMD_TYPE_INT64) {
                exponent = (double)(*(long*)item_b.pointer);
            } else if (item_b.type_id == LMD_TYPE_FLOAT) {
                exponent = *(double*)item_b.pointer;
            } else {
                printf("unsupported pow exponent type with decimal: %d\n", item_b.type_id);
                return ItemError;
            }
        }
        
        // For decimal operations, return a decimal result
        double result_val = lambda_pow(base, exponent);
        
        mpd_t* result = mpd_new(&context->decimal_ctx);
        if (!result) return ItemError;
        
        char str_buf[64];
        snprintf(str_buf, sizeof(str_buf), "%.17g", result_val);
        mpd_set_string(result, str_buf, &context->decimal_ctx);
        
        if (mpd_isnan(result) || mpd_isinfinite(result)) {
            mpd_del(result);
            printf("decimal power operation failed\n");
            return ItemError;
        }
        
        return push_decimal(result);
    }
    
    // Original non-decimal logic
    double base = 0.0, exponent = 0.0;
    
    // Convert first argument to double
    if (item_a.type_id == LMD_TYPE_INT) {
        // Sign extend the 56-bit long_val to a proper signed long
        long signed_val = (long)((int64_t)(item_a.long_val << 8) >> 8);
        base = (double)signed_val;
    }
    else if (item_a.type_id == LMD_TYPE_INT64) {
        base = (double)(*(long*)item_a.pointer);
    }
    else if (item_a.type_id == LMD_TYPE_FLOAT) {
        base = *(double*)item_a.pointer;
    }
    else {
        printf("unknown pow base type: %d\n", item_a.type_id);
        return ItemError;
    }
    
    // Convert second argument to double
    if (item_b.type_id == LMD_TYPE_INT) {
        // Sign extend the 56-bit long_val to a proper signed long
        long signed_val = (long)((int64_t)(item_b.long_val << 8) >> 8);
        exponent = (double)signed_val;
    }
    else if (item_b.type_id == LMD_TYPE_INT64) {
        exponent = (double)(*(long*)item_b.pointer);
    }
    else if (item_b.type_id == LMD_TYPE_FLOAT) {
        exponent = *(double*)item_b.pointer;
    }
    else {
        printf("unknown pow exponent type: %d\n", item_b.type_id);
        return ItemError;
    }
    
    return push_d(lambda_pow(base, exponent));
}

// Implementation of lambda_pow function
double lambda_pow(double x, double y) {
    return pow(x, y);
}

Item fn_mod(Item item_a, Item item_b) {
    // Handle decimal types first
    if (item_a.type_id == LMD_TYPE_DECIMAL || item_b.type_id == LMD_TYPE_DECIMAL) {
        mpd_t* val_a = convert_to_decimal(item_a, &context->decimal_ctx);
        if (!val_a) return ItemError;
        
        mpd_t* val_b = convert_to_decimal(item_b, &context->decimal_ctx);
        if (!val_b) {
            cleanup_temp_decimal(val_a, item_a.type_id == LMD_TYPE_DECIMAL);
            return ItemError;
        }
        
        // Check for division by zero
        if (decimal_is_zero(val_b)) {
            printf("modulo by zero error\n");
            cleanup_temp_decimal(val_a, item_a.type_id == LMD_TYPE_DECIMAL);
            cleanup_temp_decimal(val_b, item_b.type_id == LMD_TYPE_DECIMAL);
            return ItemError;
        }
        
        mpd_t* result = mpd_new(&context->decimal_ctx);
        if (!result) {
            cleanup_temp_decimal(val_a, item_a.type_id == LMD_TYPE_DECIMAL);
            cleanup_temp_decimal(val_b, item_b.type_id == LMD_TYPE_DECIMAL);
            return ItemError;
        }
        
        mpd_rem(result, val_a, val_b, &context->decimal_ctx);
        
        // Clean up temporary decimals
        cleanup_temp_decimal(val_a, item_a.type_id == LMD_TYPE_DECIMAL);
        cleanup_temp_decimal(val_b, item_b.type_id == LMD_TYPE_DECIMAL);
        
        if (mpd_isnan(result) || mpd_isinfinite(result)) {
            mpd_del(result);
            printf("decimal modulo operation failed\n");
            return ItemError;
        }
        
        return push_decimal(result);
    }
    
    // Original non-decimal logic for integer mod
    if (item_a.type_id == LMD_TYPE_INT && item_b.type_id == LMD_TYPE_INT) {
        // Sign extend both values to proper signed longs
        long a_val = (long)((int64_t)(item_a.long_val << 8) >> 8);
        long b_val = (long)((int64_t)(item_b.long_val << 8) >> 8);
        
        if (b_val == 0) {
            printf("modulo by zero error\n");
            return ItemError;
        }
        
        return (Item){.item = i2it(a_val % b_val)};
    }
    else if (item_a.type_id == LMD_TYPE_INT64 && item_b.type_id == LMD_TYPE_INT64) {
        long a_val = *(long*)item_a.pointer;
        long b_val = *(long*)item_b.pointer;
        
        if (b_val == 0) {
            printf("modulo by zero error\n");
            return ItemError;
        }
        
        return push_l(a_val % b_val);
    }
    else if (item_a.type_id == LMD_TYPE_INT && item_b.type_id == LMD_TYPE_INT64) {
        long a_val = (long)((int64_t)(item_a.long_val << 8) >> 8);
        long b_val = *(long*)item_b.pointer;
        
        if (b_val == 0) {
            printf("modulo by zero error\n");
            return ItemError;
        }
        
        return push_l(a_val % b_val);
    }
    else if (item_a.type_id == LMD_TYPE_INT64 && item_b.type_id == LMD_TYPE_INT) {
        long a_val = *(long*)item_a.pointer;
        long b_val = (long)((int64_t)(item_b.long_val << 8) >> 8);
        
        if (b_val == 0) {
            printf("modulo by zero error\n");
            return ItemError;
        }
        
        return push_l(a_val % b_val);
    }
    else if (item_a.type_id == LMD_TYPE_FLOAT || item_b.type_id == LMD_TYPE_FLOAT) {
        printf("modulo not supported for float types\n");
        return ItemError;
    }
    else {
        printf("unknown mod type: %d, %d\n", item_a.type_id, item_b.type_id);
        return ItemError;
    }
}

// Numeric system functions implementation

Item fn_abs(Item item) {
    // abs() - absolute value of a number
    if (item.type_id == LMD_TYPE_INT) {
        // Sign extend the 56-bit long_val to a proper signed long
        long val = (long)((int64_t)(item.long_val << 8) >> 8);
        return (Item){.item = i2it(val < 0 ? -val : val)};
    }
    else if (item.type_id == LMD_TYPE_INT64) {
        long val = *(long*)item.pointer;
        return push_l(val < 0 ? -val : val);
    }
    else if (item.type_id == LMD_TYPE_FLOAT) {
        double val = *(double*)item.pointer;
        return push_d(fabs(val));
    }
    else {
        printf("abs not supported for type: %d\n", item.type_id);
        return ItemError;
    }
}

Item fn_round(Item item) {
    // round() - round to nearest integer
    if (item.type_id == LMD_TYPE_INT || item.type_id == LMD_TYPE_INT64) {
        // Already an integer, return as-is
        return item;
    }
    else if (item.type_id == LMD_TYPE_FLOAT) {
        double val = *(double*)item.pointer;
        return push_d(round(val));
    }
    else {
        printf("round not supported for type: %d\n", item.type_id);
        return ItemError;
    }
}

Item fn_floor(Item item) {
    // floor() - round down to nearest integer
    if (item.type_id == LMD_TYPE_INT || item.type_id == LMD_TYPE_INT64) {
        // Already an integer, return as-is
        return item;
    }
    else if (item.type_id == LMD_TYPE_FLOAT) {
        double val = *(double*)item.pointer;
        return push_d(floor(val));
    }
    else {
        printf("floor not supported for type: %d\n", item.type_id);
        return ItemError;
    }
}

Item fn_ceil(Item item) {
    // ceil() - round up to nearest integer
    if (item.type_id == LMD_TYPE_INT || item.type_id == LMD_TYPE_INT64) {
        // Already an integer, return as-is
        return item;
    }
    else if (item.type_id == LMD_TYPE_FLOAT) {
        double val = *(double*)item.pointer;
        return push_d(ceil(val));
    }
    else {
        printf("ceil not supported for type: %d\n", item.type_id);
        return ItemError;
    }
}

Item fn_min(Item item_a, Item item_b) {
    // min() - minimum of two numbers
    double a_val = 0.0, b_val = 0.0;
    bool is_float = false;
    
    // Convert first argument
    if (item_a.type_id == LMD_TYPE_INT) {
        long signed_val = (long)((int64_t)(item_a.long_val << 8) >> 8);
        a_val = (double)signed_val;
    }
    else if (item_a.type_id == LMD_TYPE_INT64) {
        a_val = (double)(*(long*)item_a.pointer);
    }
    else if (item_a.type_id == LMD_TYPE_FLOAT) {
        a_val = *(double*)item_a.pointer;
        is_float = true;
    }
    else {
        printf("min not supported for type: %d\n", item_a.type_id);
        return ItemError;
    }
    
    // Convert second argument
    if (item_b.type_id == LMD_TYPE_INT) {
        long signed_val = (long)((int64_t)(item_b.long_val << 8) >> 8);
        b_val = (double)signed_val;
    }
    else if (item_b.type_id == LMD_TYPE_INT64) {
        b_val = (double)(*(long*)item_b.pointer);
    }
    else if (item_b.type_id == LMD_TYPE_FLOAT) {
        b_val = *(double*)item_b.pointer;
        is_float = true;
    }
    else {
        printf("min not supported for type: %d\n", item_b.type_id);
        return ItemError;
    }
    
    double result = a_val < b_val ? a_val : b_val;
    
    // Return as integer if both inputs were integers
    if (!is_float) {
        // Convert back to Item int directly to match input type
        return {.item = i2it((long)result)};
    } else {
        return push_d(result);
    }
}

Item fn_max(Item item_a, Item item_b) {
    // max() - maximum of two numbers
    double a_val = 0.0, b_val = 0.0;
    bool is_float = false;
    
    // Convert first argument
    if (item_a.type_id == LMD_TYPE_INT) {
        long signed_val = (long)((int64_t)(item_a.long_val << 8) >> 8);
        a_val = (double)signed_val;
    }
    else if (item_a.type_id == LMD_TYPE_INT64) {
        a_val = (double)(*(long*)item_a.pointer);
    }
    else if (item_a.type_id == LMD_TYPE_FLOAT) {
        a_val = *(double*)item_a.pointer;
        is_float = true;
    }
    else {
        printf("max not supported for type: %d\n", item_a.type_id);
        return ItemError;
    }
    
    // Convert second argument
    if (item_b.type_id == LMD_TYPE_INT) {
        long signed_val = (long)((int64_t)(item_b.long_val << 8) >> 8);
        b_val = (double)signed_val;
    }
    else if (item_b.type_id == LMD_TYPE_INT64) {
        b_val = (double)(*(long*)item_b.pointer);
    }
    else if (item_b.type_id == LMD_TYPE_FLOAT) {
        b_val = *(double*)item_b.pointer;
        is_float = true;
    }
    else {
        printf("max not supported for type: %d\n", item_b.type_id);
        return ItemError;
    }
    
    double result = a_val > b_val ? a_val : b_val;
    
    // Return as integer if both inputs were integers
    if (!is_float) {
        // Convert back to Item int directly to match input type
        return {.item = i2it((long)result)};
    } else {
        return push_d(result);
    }
}

Item fn_sum(Item item) {
    // sum() - sum of all elements in an array or list
    TypeId type_id = get_type_id(item);
    printf("DEBUG fn_sum: called with type_id: %d, pointer: %p\n", type_id, item.raw_pointer);
    if (type_id == LMD_TYPE_ARRAY) {
        printf("DEBUG fn_sum: Processing LMD_TYPE_ARRAY\n");
        Array* arr = item.array;  // Use item.array, not item.pointer
        printf("DEBUG fn_sum: Array pointer: %p, length: %ld\n", arr, arr ? arr->length : -1);
        if (!arr || arr->length == 0) {
            printf("DEBUG fn_sum: Empty array, returning 0\n");
            return (Item){.item = i2it(0)};  // Empty array sums to 0
        }
        double sum = 0.0;
        bool has_float = false;
        for (size_t i = 0; i < arr->length; i++) {
            Item elem_item = array_get(arr, i);
            if (elem_item.type_id == LMD_TYPE_INT) {
                long val = elem_item.int_val;
                printf("DEBUG fn_sum: Adding int value: %ld\n", val);
                sum += (double)val;
            }
            else if (elem_item.type_id == LMD_TYPE_INT64) {
                long val = *(long*)elem_item.pointer;
                printf("DEBUG fn_sum: Adding int64 value: %ld\n", val);
                sum += (double)val;
            }
            else if (elem_item.type_id == LMD_TYPE_FLOAT) {
                double val = *(double*)elem_item.pointer;
                printf("DEBUG fn_sum: Adding float value: %f\n", val);
                sum += val;
                has_float = true;
            }
            else {
                printf("DEBUG fn_sum: sum: non-numeric element at index %zu, type: %d\n", i, elem_item.type_id);
                return ItemError;
            }
        }
        if (has_float) {
            return push_d(sum);
        } else {
            return push_l((long)sum);
        }
    }
    else if (type_id == LMD_TYPE_ARRAY_INT) {
        ArrayLong* arr = item.array_long;  // Use the correct field
        if (!arr || arr->length == 0) {
            return (Item){.item = i2it(0)};  // Empty array sums to 0
        }
        long sum = 0;
        for (size_t i = 0; i < arr->length; i++) {
            sum += arr->items[i];
        }
        return (Item){.item = i2it(sum)};
    }
    else if (type_id == LMD_TYPE_LIST) {
        printf("DEBUG fn_sum: Processing LMD_TYPE_LIST\n");
        List* list = item.list;
        printf("DEBUG fn_sum: List pointer: %p, length: %ld\n", list, list ? list->length : -1);
        if (!list || list->length == 0) {
            printf("DEBUG fn_sum: Empty list, returning 0\n");
            return (Item){.item = i2it(0)};  // Empty list sums to 0
        }
        double sum = 0.0;
        bool has_float = false;
        for (size_t i = 0; i < list->length; i++) {
            Item elem_item = list_get(list, i);
            if (elem_item.type_id == LMD_TYPE_INT) {
                long val = elem_item.int_val;
                printf("DEBUG fn_sum: Adding int value: %ld\n", val);
                sum += (double)val;
            }
            else if (elem_item.type_id == LMD_TYPE_INT64) {
                long val = *(long*)elem_item.pointer;
                printf("DEBUG fn_sum: Adding int64 value: %ld\n", val);
                sum += (double)val;
            }
            else if (elem_item.type_id == LMD_TYPE_FLOAT) {
                double val = *(double*)elem_item.pointer;
                printf("DEBUG fn_sum: Adding float value: %f\n", val);
                sum += val;
                has_float = true;
            }
            else {
                printf("DEBUG fn_sum: sum: non-numeric element at index %zu, type: %d\n", i, elem_item.type_id);
                return ItemError;
            }
        }
        if (has_float) {
            printf("DEBUG fn_sum: Returning sum as double: %f\n", sum);
            return push_d(sum);
        } else {
            printf("DEBUG fn_sum: Returning sum as long: %ld\n", (long)sum);
            return push_l((long)sum);
        }
    }
    else {
        printf("DEBUG fn_sum: sum not supported for type: %d\n", type_id);
        return ItemError;
    }
}

Item fn_avg(Item item) {
    // avg() - average of all elements in an array or list
    TypeId type_id = get_type_id(item);
    if (type_id == LMD_TYPE_ARRAY) {
        Array* arr = item.array;  // Use item.array, not item.pointer
        if (!arr || arr->length == 0) {
            return ItemError;
        }
        double sum = 0.0;
        for (size_t i = 0; i < arr->length; i++) {
            Item elem_item = array_get(arr, i);
            if (elem_item.type_id == LMD_TYPE_INT) {
                long val = elem_item.int_val;
                sum += (double)val;
            }
            else if (elem_item.type_id == LMD_TYPE_INT64) {
                sum += (double)(*(long*)elem_item.pointer);
            }
            else if (elem_item.type_id == LMD_TYPE_FLOAT) {
                sum += *(double*)elem_item.pointer;
            }
            else {
                printf("avg: non-numeric element at index %zu, type: %d\n", i, elem_item.type_id);
                return ItemError;
            }
        }
        return push_d(sum / (double)arr->length);
    }
    else if (type_id == LMD_TYPE_ARRAY_INT) {
        ArrayLong* arr = item.array_long;  // Use the correct field
        if (!arr || arr->length == 0) {
            return ItemError;
        }
        double sum = 0.0;
        for (size_t i = 0; i < arr->length; i++) {
            sum += (double)arr->items[i];
        }
        return push_d(sum / (double)arr->length);
    }
    else if (type_id == LMD_TYPE_LIST) {
        List* list = item.list;
        if (!list || list->length == 0) {
            return ItemError;
        }
        double sum = 0.0;
        for (size_t i = 0; i < list->length; i++) {
            Item elem_item = list_get(list, i);
            if (elem_item.type_id == LMD_TYPE_INT) {
                long val = (long)elem_item.int_val;
                sum += (double)val;
            }
            else if (elem_item.type_id == LMD_TYPE_INT64) {
                sum += (double)(*(long*)elem_item.pointer);
            }
            else if (elem_item.type_id == LMD_TYPE_FLOAT) {
                sum += *(double*)elem_item.pointer;
            }
            else {
                printf("avg: non-numeric element at index %zu, type: %d\n", i, elem_item.type_id);
                return ItemError;
            }
        }
        return push_d(sum / (double)list->length);
    }
    else {
        printf("avg not supported for type: %d\n", type_id);
        return ItemError;
    }
}

Item fn_pos(Item item) {
    // Unary + operator - return the item as-is for numeric types, or cast strings/symbols to numbers
    if (item.type_id == LMD_TYPE_INT) {
        return item;  // Already in correct format
    }
    else if (item.type_id == LMD_TYPE_INT64) {
        return item;  // Already in correct format
    }
    else if (item.type_id == LMD_TYPE_FLOAT) {
        return item;  // Already in correct format
    }
    else if (item.type_id == LMD_TYPE_DECIMAL) {
        return item;  // For decimal, unary + returns the same value
    }
    else if (item.type_id == LMD_TYPE_STRING || item.type_id == LMD_TYPE_SYMBOL) {
        // Cast string/symbol to number
        String* str = (String*)item.pointer;
        if (!str || str->len == 0) {
            printf("unary + error: empty string/symbol\n");
            return ItemError;
        }
        
        // Try to parse as integer first
        char* endptr;
        long long_val = strtol(str->chars, &endptr, 10);
        
        // If entire string was consumed and no overflow, it's an integer
        if (*endptr == '\0' && endptr == str->chars + str->len) {
            return (Item){.item = i2it(long_val)};
        }
        
        // Try to parse as float
        double double_val = strtod(str->chars, &endptr);
        if (*endptr == '\0' && endptr == str->chars + str->len) {
            return push_d(double_val);
        }
        
        // Not a valid number
        printf("unary + error: cannot convert '%.*s' to number\n", (int)str->len, str->chars);
        return ItemError;
    }
    else {
        // For other types (bool, datetime, etc.), unary + is an error
        printf("unary + not supported for type: %d\n", item.type_id);
        return ItemError;
    }
}

Item fn_neg(Item item) {
    // Unary - operator - negate numeric values or cast and negate strings/symbols
    if (item.type_id == LMD_TYPE_INT) {
        // Sign extend the 56-bit long_val to a proper signed long, then negate
        long val = (long)((int64_t)(item.long_val << 8) >> 8);
        return (Item){.item = i2it(-val)};
    }
    else if (item.type_id == LMD_TYPE_INT64) {
        long val = *(long*)item.pointer;
        return push_l(-val);
    }
    else if (item.type_id == LMD_TYPE_FLOAT) {
        double val = *(double*)item.pointer;
        return push_d(-val);
    }
    else if (item.type_id == LMD_TYPE_DECIMAL) {
        // For decimal types, we'd need to negate the libmpdec value
        // This would require more complex decimal arithmetic with libmpdec
        printf("unary - for decimal type not yet implemented\n");
        return ItemError;
    }
    else if (item.type_id == LMD_TYPE_STRING || item.type_id == LMD_TYPE_SYMBOL) {
        // Cast string/symbol to number, then negate
        String* str = (String*)item.pointer;
        if (!str || str->len == 0) {
            printf("unary - error: empty string/symbol\n");
            return ItemError;
        }
        
        // Try to parse as integer first
        char* endptr;
        long long_val = strtol(str->chars, &endptr, 10);
        
        // If entire string was consumed and no overflow, it's an integer
        if (*endptr == '\0' && endptr == str->chars + str->len) {
            return (Item){.item = i2it(-long_val)};
        }
        
        // Try to parse as float
        double double_val = strtod(str->chars, &endptr);
        if (*endptr == '\0' && endptr == str->chars + str->len) {
            return push_d(-double_val);
        }
        
        // Not a valid number
        printf("unary - error: cannot convert '%.*s' to number\n", (int)str->len, str->chars);
        return ItemError;
    }
    else {
        // For other types (bool, datetime, etc.), unary - is an error
        printf("unary - not supported for type: %d\n", item.type_id);
        return ItemError;
    }
}

// Unicode string normalization function
Item fn_normalize(Item str_item, Item type_item) {
    // normalize(string, 'nfc'|'nfd'|'nfkc'|'nfkd') - Unicode normalization
    if (str_item.type_id != LMD_TYPE_STRING) {
        printf("normalize: first argument must be a string, got type: %d\n", str_item.type_id);
        return ItemError;
    }
    
    if (type_item.type_id != LMD_TYPE_SYMBOL) {
        printf("normalize: second argument must be a symbol (normalization type), got type: %d\n", type_item.type_id);
        return ItemError;
    }
    
    String* input_str = (String*)str_item.pointer;
    String* norm_type = (String*)type_item.pointer;
    
    if (!input_str || !norm_type) {
        printf("normalize: null string arguments\n");
        return ItemError;
    }
    
#if LAMBDA_UNICODE_LEVEL >= LAMBDA_UNICODE_UTF8PROC
    // Use utf8proc for normalization
    int result_len = 0;
    char* result = NULL;
    
    if (strncmp(norm_type->chars, "nfc", norm_type->len) == 0) {
        result = normalize_utf8proc_nfc(input_str->chars, input_str->len, &result_len);
    } else if (strncmp(norm_type->chars, "nfd", norm_type->len) == 0) {
        result = normalize_utf8proc_nfd(input_str->chars, input_str->len, &result_len);
    } else if (strncmp(norm_type->chars, "nfkc", norm_type->len) == 0) {
        result = normalize_utf8proc_nfkc(input_str->chars, input_str->len, &result_len);
    } else if (strncmp(norm_type->chars, "nfkd", norm_type->len) == 0) {
        result = normalize_utf8proc_nfkd(input_str->chars, input_str->len, &result_len);
    } else {
        printf("normalize: unknown normalization type '%.*s', supported: nfc, nfd, nfkc, nfkd\n", 
               (int)norm_type->len, norm_type->chars);
        return ItemError;
    }
    
    if (!result || result_len == 0) {
        printf("normalize: normalization failed\n");
        return ItemError;
    }
    
    // Create a new string from the normalized result
    String* output_str = (String *)heap_alloc(sizeof(String) + result_len + 1, LMD_TYPE_STRING);
    if (!output_str) {
        free(result);
        printf("normalize: failed to allocate output string\n");
        return ItemError;
    }
    
    memcpy(output_str->chars, result, result_len);
    output_str->len = result_len;
    free(result);  // utf8proc allocates with malloc, so we need to free
    
    return (Item){.item = s2it(output_str)};
    
#elif LAMBDA_UNICODE_LEVEL >= LAMBDA_UNICODE_COMPACT
    // Use ICU for normalization (fallback for compatibility)
    printf("normalize: ICU normalization not implemented yet\n");
    return ItemError;
    
#else
    // ASCII-only mode: return original string (no normalization)
    printf("normalize: Unicode normalization disabled (ASCII-only mode)\n");
    return str_item;  // Return original string unchanged
#endif
}

Range* fn_to(Item item_a, Item item_b) {
    // todo: join binary, list, array, map
    if ((item_a.type_id == LMD_TYPE_INT || item_a.type_id == LMD_TYPE_INT64) && 
        (item_b.type_id == LMD_TYPE_INT || item_b.type_id == LMD_TYPE_INT64)) {
        long start = item_a.type_id == LMD_TYPE_INT ? item_a.int_val : *(long*)item_a.pointer;
        long end = item_b.type_id == LMD_TYPE_INT ? item_b.int_val : *(long*)item_b.pointer;
        if (start > end) {
            // todo: should raise error
            printf("Error: start of range is greater than end: %ld > %ld\n", start, end);
            return NULL;
        }
        Range *range = (Range *)heap_alloc(sizeof(Range), LMD_TYPE_RANGE);
        range->type_id = LMD_TYPE_RANGE;
        range->start = start;  range->end = end;
        range->length = end - start + 1;
        printf("create range: %ld to %ld, length: %ld\n", range->start, range->end, range->length);
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
    CompResult result = equal_comp(a_item, b_item);
    return result == COMP_TRUE;
}

CompResult equal_comp(Item a_item, Item b_item) {
#if LAMBDA_UNICODE_LEVEL >= LAMBDA_UNICODE_COMPACT
    // Use Unicode-enhanced comparison when available
    return equal_comp_unicode(a_item, b_item);
#else
    // Original ASCII-only implementation
    printf("equal_comp expr\n");
    if (a_item.type_id != b_item.type_id) {
        // number promotion - only for int/float types
        if (LMD_TYPE_INT <= a_item.type_id && a_item.type_id <= LMD_TYPE_NUMBER && 
            LMD_TYPE_INT <= b_item.type_id && b_item.type_id <= LMD_TYPE_NUMBER) {
            double a_val = it2d(a_item), b_val = it2d(b_item);
            return (a_val == b_val) ? COMP_TRUE : COMP_FALSE;
        }
        // Type mismatch error for equality comparisons (e.g., true == 1, "test" != null)
        // Note: null can only be compared to null, any other comparison is a type error
        return COMP_ERROR;
    }
    
    if (a_item.type_id == LMD_TYPE_NULL) {
        return COMP_TRUE; // null == null
    }    
    else if (a_item.type_id == LMD_TYPE_BOOL) {
        return (a_item.bool_val == b_item.bool_val) ? COMP_TRUE : COMP_FALSE;
    }
    else if (a_item.type_id == LMD_TYPE_INT) {
        return (a_item.long_val == b_item.long_val) ? COMP_TRUE : COMP_FALSE;
    }
    else if (a_item.type_id == LMD_TYPE_INT64) {
        return (*(long*)a_item.pointer == *(long*)b_item.pointer) ? COMP_TRUE : COMP_FALSE;
    }
    else if (a_item.type_id == LMD_TYPE_FLOAT) {
        return (*(double*)a_item.pointer == *(double*)b_item.pointer) ? COMP_TRUE : COMP_FALSE;
    }
    else if (a_item.type_id == LMD_TYPE_STRING || a_item.type_id == LMD_TYPE_SYMBOL || 
        a_item.type_id == LMD_TYPE_BINARY || a_item.type_id == LMD_TYPE_DTIME) {
        String *str_a = (String*)a_item.pointer;  String *str_b = (String*)b_item.pointer;
        bool result = (str_a->len == str_b->len && strncmp(str_a->chars, str_b->chars, str_a->len) == 0);
        return result ? COMP_TRUE : COMP_FALSE;
    }
    printf("unknown comparing type %d\n", a_item.type_id);
    return COMP_ERROR;
#endif
}

// Comparison functions with fast path for int/float and fallback for other types

Item fn_eq(Item a_item, Item b_item) {
#if LAMBDA_UNICODE_LEVEL >= LAMBDA_UNICODE_COMPACT
    // Use Unicode-enhanced equality comparison
    return fn_eq_unicode(a_item, b_item);
#else
    // Fast path for numeric types
    if (a_item.type_id == LMD_TYPE_INT && b_item.type_id == LMD_TYPE_INT) {
        return {.item = b2it(a_item.long_val == b_item.long_val)};
    }
    else if (a_item.type_id == LMD_TYPE_FLOAT && b_item.type_id == LMD_TYPE_FLOAT) {
        return {.item = b2it(*(double*)a_item.pointer == *(double*)b_item.pointer)};
    }
    else if ((a_item.type_id == LMD_TYPE_INT && b_item.type_id == LMD_TYPE_FLOAT) ||
             (a_item.type_id == LMD_TYPE_FLOAT && b_item.type_id == LMD_TYPE_INT)) {
        double a_val = (a_item.type_id == LMD_TYPE_INT) ? (double)a_item.long_val : *(double*)a_item.pointer;
        double b_val = (b_item.type_id == LMD_TYPE_INT) ? (double)b_item.long_val : *(double*)b_item.pointer;
        return {.item = b2it(a_val == b_val)};
    }
    else if (a_item.type_id == LMD_TYPE_BOOL && b_item.type_id == LMD_TYPE_BOOL) {
        return {.item = b2it(a_item.bool_val == b_item.bool_val)};
    }
    
    // Fallback to 3-state comparison function
    printf("fn_eq fallback\n");
    CompResult result = equal_comp(a_item, b_item);
    if (result == COMP_ERROR) {
        printf("equality type error for types: %d, %d\n", a_item.type_id, b_item.type_id);
        return ItemError;
    }
    return {.item = b2it(result == COMP_TRUE)};
#endif
}

Item fn_ne(Item a_item, Item b_item) {
#if LAMBDA_UNICODE_LEVEL >= LAMBDA_UNICODE_COMPACT
    // Use Unicode-enhanced inequality comparison
    return fn_ne_unicode(a_item, b_item);
#else
    // Fast path for numeric types
    if (a_item.type_id == LMD_TYPE_INT && b_item.type_id == LMD_TYPE_INT) {
        return {.item = b2it(a_item.long_val != b_item.long_val)};
    }
    else if (a_item.type_id == LMD_TYPE_FLOAT && b_item.type_id == LMD_TYPE_FLOAT) {
        return {.item = b2it(*(double*)a_item.pointer != *(double*)b_item.pointer)};
    }
    else if ((a_item.type_id == LMD_TYPE_INT && b_item.type_id == LMD_TYPE_FLOAT) ||
             (a_item.type_id == LMD_TYPE_FLOAT && b_item.type_id == LMD_TYPE_INT)) {
        double a_val = (a_item.type_id == LMD_TYPE_INT) ? (double)a_item.long_val : *(double*)a_item.pointer;
        double b_val = (b_item.type_id == LMD_TYPE_INT) ? (double)b_item.long_val : *(double*)b_item.pointer;
        return {.item = b2it(a_val != b_val)};
    }
    else if (a_item.type_id == LMD_TYPE_BOOL && b_item.type_id == LMD_TYPE_BOOL) {
        return {.item = b2it(a_item.bool_val != b_item.bool_val)};
    }
    
    // Fallback to 3-state comparison function
    printf("fn_ne fallback\n");
    CompResult result = equal_comp(a_item, b_item);
    if (result == COMP_ERROR) {
        printf("inequality type error for types: %d, %d\n", a_item.type_id, b_item.type_id);
        return ItemError;
    }
    return {.item = b2it(result == COMP_FALSE)};
#endif
}

Item fn_lt(Item a_item, Item b_item) {
#if LAMBDA_UNICODE_LEVEL >= LAMBDA_UNICODE_COMPACT
    // Use Unicode-enhanced less-than comparison (supports string comparison)
    return fn_lt_unicode(a_item, b_item);
#else
    // Fast path for numeric types
    if (a_item.type_id == LMD_TYPE_INT && b_item.type_id == LMD_TYPE_INT) {
        long a_val = (long)((int64_t)(a_item.long_val << 8) >> 8);
        long b_val = (long)((int64_t)(b_item.long_val << 8) >> 8);
        return {.item = b2it(a_val < b_val)};
    }
    else if (a_item.type_id == LMD_TYPE_FLOAT && b_item.type_id == LMD_TYPE_FLOAT) {
        return {.item = b2it(*(double*)a_item.pointer < *(double*)b_item.pointer)};
    }
    else if ((a_item.type_id == LMD_TYPE_INT && b_item.type_id == LMD_TYPE_FLOAT) ||
             (a_item.type_id == LMD_TYPE_FLOAT && b_item.type_id == LMD_TYPE_INT)) {
        double a_val = (a_item.type_id == LMD_TYPE_INT) ? (double)((long)((int64_t)(a_item.long_val << 8) >> 8)) : *(double*)a_item.pointer;
        double b_val = (b_item.type_id == LMD_TYPE_INT) ? (double)((long)((int64_t)(b_item.long_val << 8) >> 8)) : *(double*)b_item.pointer;
        return {.item = b2it(a_val < b_val)};
    }
    // Error for non-numeric types - relational comparisons not supported
    if (a_item.type_id == LMD_TYPE_BOOL || b_item.type_id == LMD_TYPE_BOOL ||
        a_item.type_id == LMD_TYPE_STRING || b_item.type_id == LMD_TYPE_STRING ||
        a_item.type_id == LMD_TYPE_NULL || b_item.type_id == LMD_TYPE_NULL) {
        printf("less than not supported for types: %d, %d\n", a_item.type_id, b_item.type_id);
        return ItemError;
    }
    // Fallback error for any other type combination
    printf("less than not supported for types: %d, %d\n", a_item.type_id, b_item.type_id);
    return ItemError;
#endif
}

Item fn_gt(Item a_item, Item b_item) {
#if LAMBDA_UNICODE_LEVEL >= LAMBDA_UNICODE_COMPACT
    // Use Unicode-enhanced greater-than comparison (supports string comparison)
    return fn_gt_unicode(a_item, b_item);
#else
    // Fast path for numeric types
    if (a_item.type_id == LMD_TYPE_INT && b_item.type_id == LMD_TYPE_INT) {
        long a_val = (long)((int64_t)(a_item.long_val << 8) >> 8);
        long b_val = (long)((int64_t)(b_item.long_val << 8) >> 8);
        return {.item = b2it(a_val > b_val)};
    }
    else if (a_item.type_id == LMD_TYPE_FLOAT && b_item.type_id == LMD_TYPE_FLOAT) {
        return {.item = b2it(*(double*)a_item.pointer > *(double*)b_item.pointer)};
    }
    else if ((a_item.type_id == LMD_TYPE_INT && b_item.type_id == LMD_TYPE_FLOAT) ||
             (a_item.type_id == LMD_TYPE_FLOAT && b_item.type_id == LMD_TYPE_INT)) {
        double a_val = (a_item.type_id == LMD_TYPE_INT) ? (double)((long)((int64_t)(a_item.long_val << 8) >> 8)) : *(double*)a_item.pointer;
        double b_val = (b_item.type_id == LMD_TYPE_INT) ? (double)((long)((int64_t)(b_item.long_val << 8) >> 8)) : *(double*)b_item.pointer;
        return {.item = b2it(a_val > b_val)};
    }
    // Error for non-numeric types - relational comparisons not supported
    if (a_item.type_id == LMD_TYPE_BOOL || b_item.type_id == LMD_TYPE_BOOL ||
        a_item.type_id == LMD_TYPE_STRING || b_item.type_id == LMD_TYPE_STRING ||
        a_item.type_id == LMD_TYPE_NULL || b_item.type_id == LMD_TYPE_NULL) {
        printf("greater than not supported for types: %d, %d\n", a_item.type_id, b_item.type_id);
        return ItemError;
    }
    // Fallback error for any other type combination
    printf("greater than not supported for types: %d, %d\n", a_item.type_id, b_item.type_id);
    return ItemError;
#endif
}

Item fn_le(Item a_item, Item b_item) {
#if LAMBDA_UNICODE_LEVEL >= LAMBDA_UNICODE_COMPACT
    // Use Unicode-enhanced less-than-or-equal comparison (supports string comparison)
    return fn_le_unicode(a_item, b_item);
#else
    // Fast path for numeric types
    if (a_item.type_id == LMD_TYPE_INT && b_item.type_id == LMD_TYPE_INT) {
        long a_val = (long)((int64_t)(a_item.long_val << 8) >> 8);
        long b_val = (long)((int64_t)(b_item.long_val << 8) >> 8);
        return {.item = b2it(a_val <= b_val)};
    }
    else if (a_item.type_id == LMD_TYPE_FLOAT && b_item.type_id == LMD_TYPE_FLOAT) {
        return {.item = b2it(*(double*)a_item.pointer <= *(double*)b_item.pointer)};
    }
    else if ((a_item.type_id == LMD_TYPE_INT && b_item.type_id == LMD_TYPE_FLOAT) ||
             (a_item.type_id == LMD_TYPE_FLOAT && b_item.type_id == LMD_TYPE_INT)) {
        double a_val = (a_item.type_id == LMD_TYPE_INT) ? (double)((long)((int64_t)(a_item.long_val << 8) >> 8)) : *(double*)a_item.pointer;
        double b_val = (b_item.type_id == LMD_TYPE_INT) ? (double)((long)((int64_t)(b_item.long_val << 8) >> 8)) : *(double*)b_item.pointer;
        return {.item = b2it(a_val <= b_val)};
    }
    // Error for non-numeric types - relational comparisons not supported
    if (a_item.type_id == LMD_TYPE_BOOL || b_item.type_id == LMD_TYPE_BOOL ||
        a_item.type_id == LMD_TYPE_STRING || b_item.type_id == LMD_TYPE_STRING ||
        a_item.type_id == LMD_TYPE_NULL || b_item.type_id == LMD_TYPE_NULL) {
        printf("less than or equal not supported for types: %d, %d\n", a_item.type_id, b_item.type_id);
        return ItemError;
    }
    // Fallback error for any other type combination
    printf("less than or equal not supported for types: %d, %d\n", a_item.type_id, b_item.type_id);
    return ItemError;
#endif
}

Item fn_ge(Item a_item, Item b_item) {
#if LAMBDA_UNICODE_LEVEL >= LAMBDA_UNICODE_COMPACT
    // Use Unicode-enhanced greater-than-or-equal comparison (supports string comparison)
    return fn_ge_unicode(a_item, b_item);
#else
    // Fast path for numeric types
    if (a_item.type_id == LMD_TYPE_INT && b_item.type_id == LMD_TYPE_INT) {
        long a_val = (long)((int64_t)(a_item.long_val << 8) >> 8);
        long b_val = (long)((int64_t)(b_item.long_val << 8) >> 8);
        return {.item = b2it(a_val >= b_val)};
    }
    else if (a_item.type_id == LMD_TYPE_FLOAT && b_item.type_id == LMD_TYPE_FLOAT) {
        return {.item = b2it(*(double*)a_item.pointer >= *(double*)b_item.pointer)};
    }
    else if ((a_item.type_id == LMD_TYPE_INT && b_item.type_id == LMD_TYPE_FLOAT) ||
             (a_item.type_id == LMD_TYPE_FLOAT && b_item.type_id == LMD_TYPE_INT)) {
        double a_val = (a_item.type_id == LMD_TYPE_INT) ? (double)((long)((int64_t)(a_item.long_val << 8) >> 8)) : *(double*)a_item.pointer;
        double b_val = (b_item.type_id == LMD_TYPE_INT) ? (double)((long)((int64_t)(b_item.long_val << 8) >> 8)) : *(double*)b_item.pointer;
        return {.item = b2it(a_val >= b_val)};
    }
    // Error for non-numeric types - relational comparisons not supported
    if (a_item.type_id == LMD_TYPE_BOOL || b_item.type_id == LMD_TYPE_BOOL ||
        a_item.type_id == LMD_TYPE_STRING || b_item.type_id == LMD_TYPE_STRING ||
        a_item.type_id == LMD_TYPE_NULL || b_item.type_id == LMD_TYPE_NULL) {
        printf("greater than or equal not supported for types: %d, %d\n", a_item.type_id, b_item.type_id);
        return ItemError;
    }
    // Fallback error for any other type combination
    printf("greater than or equal not supported for types: %d, %d\n", a_item.type_id, b_item.type_id);
    return ItemError;
#endif
}

Item fn_not(Item item) {
    // Logical NOT - invert the truthiness of the item
    printf("fn_not expr\n");
    return {.item = b2it(!item_true(item))};
}

Item fn_and(Item a_item, Item b_item) {
    printf("fn_and called with types: %d, %d\n", a_item.type_id, b_item.type_id);
    
    // Fast path for boolean types
    if (a_item.type_id == LMD_TYPE_BOOL && b_item.type_id == LMD_TYPE_BOOL) {
        printf("fn_and: bool fast path\n");
        return {.item = b2it(a_item.bool_val && b_item.bool_val)};
    }
    
    // Type error for string operands in logical operations
    if (a_item.type_id == LMD_TYPE_STRING || b_item.type_id == LMD_TYPE_STRING) {
        printf("logical AND not supported with string types: %d, %d\n", a_item.type_id, b_item.type_id);
        return ItemError;
    }
    
    // Type error for null operands in logical operations
    if (a_item.type_id == LMD_TYPE_NULL || b_item.type_id == LMD_TYPE_NULL) {
        printf("logical AND not supported with null types: %d, %d\n", a_item.type_id, b_item.type_id);
        return ItemError;
    }
    
    // Fallback to generic truthiness evaluation for numeric and boolean combinations
    printf("fn_and: generic truthiness fallback\n");
    bool a_truth = item_true(a_item);
    bool b_truth = item_true(b_item);
    return {.item = b2it(a_truth && b_truth)};
}

Item fn_or(Item a_item, Item b_item) {
    // Fast path for boolean types
    if (a_item.type_id == LMD_TYPE_BOOL && b_item.type_id == LMD_TYPE_BOOL) {
        return {.item = b2it(a_item.bool_val || b_item.bool_val)};
    }
    
    // Type error for string operands in logical operations
    if (a_item.type_id == LMD_TYPE_STRING || b_item.type_id == LMD_TYPE_STRING) {
        printf("logical OR not supported with string types: %d, %d\n", a_item.type_id, b_item.type_id);
        return ItemError;
    }
    
    // Type error for null operands in logical operations
    if (a_item.type_id == LMD_TYPE_NULL || b_item.type_id == LMD_TYPE_NULL) {
        printf("logical OR not supported with null types: %d, %d\n", a_item.type_id, b_item.type_id);
        return ItemError;
    }
    
    // Fallback to generic truthiness evaluation for numeric and boolean combinations
    bool a_truth = item_true(a_item);
    bool b_truth = item_true(b_item);
    return {.item = b2it(a_truth || b_truth)};
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
        itm.type_id == LMD_TYPE_BINARY) {
        return (String*)itm.pointer;
    }
    else if (itm.type_id == LMD_TYPE_DTIME) {
        DateTime *dt = (DateTime*)itm.pointer;
        if (dt) {
            // Debug: Print the datetime precision and basic info
            printf("fn_string debug: DateTime precision=%d, hour=%d, minute=%d, second=%d, ms=%d\n", 
                   dt->precision, dt->hour, dt->minute, dt->second, dt->millisecond);
            
            // Format datetime in Lambda format based on precision
            char buf[64];
            int len = 0;
            
            switch (dt->precision) {
                case DATETIME_PRECISION_YEAR_ONLY:
                    len = snprintf(buf, sizeof(buf), "t'%04d'", DATETIME_GET_YEAR(dt));
                    break;
                    
                case DATETIME_PRECISION_DATE_ONLY:
                    len = snprintf(buf, sizeof(buf), "t'%04d-%02d-%02d'", 
                        DATETIME_GET_YEAR(dt), DATETIME_GET_MONTH(dt), dt->day);
                    break;
                    
                case DATETIME_PRECISION_TIME_ONLY: {
                    // Debug: Print the datetime values we're formatting
                    printf("fn_string debug: formatting time-only: %02d:%02d:%02d.%03d, tz_offset=%d\n", 
                           dt->hour, dt->minute, dt->second, dt->millisecond, 
                           DATETIME_HAS_TIMEZONE(dt) ? DATETIME_GET_TZ_OFFSET(dt) : -999);
                    
                    // Format time-only without 'T' prefix
                    len = snprintf(buf, sizeof(buf), "t'%02d:%02d:%02d", 
                        dt->hour, dt->minute, dt->second);
                    
                    // Add milliseconds if non-zero
                    if (dt->millisecond > 0) {
                        len += snprintf(buf + len, sizeof(buf) - len, ".%03d", dt->millisecond);
                    }
                    
                    // Add timezone - use 'z' for UTC (+00:00)
                    if (DATETIME_HAS_TIMEZONE(dt)) {
                        int tz_offset = DATETIME_GET_TZ_OFFSET(dt);
                        if (tz_offset == 0) {
                            len += snprintf(buf + len, sizeof(buf) - len, "z");
                        } else {
                            int hours = abs(tz_offset) / 60;
                            int minutes = abs(tz_offset) % 60;
                            len += snprintf(buf + len, sizeof(buf) - len, "%+03d:%02d", 
                                tz_offset >= 0 ? hours : -hours, minutes);
                        }
                    }
                    
                    len += snprintf(buf + len, sizeof(buf) - len, "'");
                    break;
                }
                    
                case DATETIME_PRECISION_DATE_TIME:
                default: {
                    // Format full datetime with 'T' separator
                    len = snprintf(buf, sizeof(buf), "t'%04d-%02d-%02dT%02d:%02d:%02d", 
                        DATETIME_GET_YEAR(dt), DATETIME_GET_MONTH(dt), dt->day,
                        dt->hour, dt->minute, dt->second);
                    
                    // Add milliseconds if non-zero
                    if (dt->millisecond > 0) {
                        len += snprintf(buf + len, sizeof(buf) - len, ".%03d", dt->millisecond);
                    }
                    
                    // Add timezone - use 'z' for UTC (+00:00)
                    if (DATETIME_HAS_TIMEZONE(dt)) {
                        int tz_offset = DATETIME_GET_TZ_OFFSET(dt);
                        if (tz_offset == 0) {
                            len += snprintf(buf + len, sizeof(buf) - len, "z");
                        } else {
                            int hours = abs(tz_offset) / 60;
                            int minutes = abs(tz_offset) % 60;
                            len += snprintf(buf + len, sizeof(buf) - len, "%+03d:%02d", 
                                tz_offset >= 0 ? hours : -hours, minutes);
                        }
                    }
                    
                    len += snprintf(buf + len, sizeof(buf) - len, "'");
                    break;
                }
            }
            
            String *str = (String *)heap_alloc(len + 1 + sizeof(String), LMD_TYPE_STRING);
            strcpy(str->chars, buf);
            str->len = len;  str->ref_cnt = 0;
            return str;
        } else {
            return &STR_NULL;
        }
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

// Single-argument format function wrapper - uses default formatting
extern "C" String* fn_format_simple(Item item) {
    return fn_format(item, ItemNull);
}

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

    printf("fn_index item index: %ld\n", index);
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

// Static DateTime instance to avoid dynamic allocation issues
static DateTime static_dt;
static bool static_dt_initialized = false;

// DateTime system function - creates a current DateTime
DateTime* fn_datetime() {
    // Use a static DateTime to avoid heap allocation issues - this is not roubust, not thread-safe
    if (!static_dt_initialized) {
        memset(&static_dt, 0, sizeof(DateTime));
        static_dt_initialized = true;
    }
    
    // Get current time
    time_t now = time(NULL);
    struct tm* tm_utc = gmtime(&now);
    if (!tm_utc) { return NULL; }

    // Set date and time from current UTC time
    DATETIME_SET_YEAR_MONTH(&static_dt, tm_utc->tm_year + 1900, tm_utc->tm_mon + 1);
    static_dt.day = tm_utc->tm_mday;
    static_dt.hour = tm_utc->tm_hour;
    static_dt.minute = tm_utc->tm_min;
    static_dt.second = tm_utc->tm_sec;
    static_dt.millisecond = 0;
    
    // Set as UTC timezone
    DATETIME_SET_TZ_OFFSET(&static_dt, 0);
    static_dt.precision = DATETIME_PRECISION_DATE_TIME;
    static_dt.format_hint = DATETIME_FORMAT_ISO8601_UTC;
    
    return &static_dt;
}

