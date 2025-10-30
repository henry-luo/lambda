#include "ast.hpp"
#include "../lib/log.h"
#include "../lib/mempool.h"

Type TYPE_NULL = {.type_id = LMD_TYPE_NULL};
Type TYPE_BOOL = {.type_id = LMD_TYPE_BOOL};
Type TYPE_INT = {.type_id = LMD_TYPE_INT};
Type TYPE_INT64 = {.type_id = LMD_TYPE_INT64};
Type TYPE_FLOAT = {.type_id = LMD_TYPE_FLOAT};
Type TYPE_DECIMAL = {.type_id = LMD_TYPE_DECIMAL};
Type TYPE_NUMBER = {.type_id = LMD_TYPE_NUMBER};
Type TYPE_STRING = {.type_id = LMD_TYPE_STRING};
Type TYPE_BINARY = {.type_id = LMD_TYPE_BINARY};
Type TYPE_SYMBOL = {.type_id = LMD_TYPE_SYMBOL};
Type TYPE_DTIME = {.type_id = LMD_TYPE_DTIME};
Type TYPE_LIST = {.type_id = LMD_TYPE_LIST};
Type TYPE_RANGE = {.type_id = LMD_TYPE_RANGE};
TypeArray TYPE_ARRAY;
Type TYPE_MAP = {.type_id = LMD_TYPE_MAP};
Type TYPE_ELMT = {.type_id = LMD_TYPE_ELEMENT};
Type TYPE_TYPE = {.type_id = LMD_TYPE_TYPE};
Type TYPE_FUNC = {.type_id = LMD_TYPE_FUNC};
Type TYPE_ANY = {.type_id = LMD_TYPE_ANY};
Type TYPE_ERROR = {.type_id = LMD_TYPE_ERROR};

Type CONST_BOOL = {.type_id = LMD_TYPE_BOOL, .is_const = 1};
Type CONST_INT = {.type_id = LMD_TYPE_INT, .is_const = 1};
Type CONST_FLOAT = {.type_id = LMD_TYPE_FLOAT, .is_const = 1};
Type CONST_STRING = {.type_id = LMD_TYPE_STRING, .is_const = 1};

Type LIT_NULL = {.type_id = LMD_TYPE_NULL, .is_literal = 1, .is_const = 1};
Type LIT_BOOL = {.type_id = LMD_TYPE_BOOL, .is_literal = 1, .is_const = 1};
Type LIT_INT = {.type_id = LMD_TYPE_INT, .is_literal = 1, .is_const = 1};
Type LIT_INT64 = {.type_id = LMD_TYPE_INT64, .is_literal = 1, .is_const = 1};
Type LIT_FLOAT = {.type_id = LMD_TYPE_FLOAT, .is_literal = 1, .is_const = 1};
Type LIT_DECIMAL = {.type_id = LMD_TYPE_DECIMAL, .is_literal = 1, .is_const = 1};
Type LIT_STRING = {.type_id = LMD_TYPE_STRING, .is_literal = 1, .is_const = 1};
Type LIT_DTIME = {.type_id = LMD_TYPE_DTIME, .is_literal = 1, .is_const = 1};
Type LIT_TYPE = {.type_id = LMD_TYPE_TYPE, .is_literal = 1, .is_const = 1};

TypeType LIT_TYPE_NULL;
TypeType LIT_TYPE_BOOL;
TypeType LIT_TYPE_INT;
TypeType LIT_TYPE_FLOAT;
TypeType LIT_TYPE_DECIMAL;
TypeType LIT_TYPE_NUMBER;
TypeType LIT_TYPE_STRING;
TypeType LIT_TYPE_BINARY;
TypeType LIT_TYPE_SYMBOL;
TypeType LIT_TYPE_DTIME;
TypeType LIT_TYPE_LIST;
TypeType LIT_TYPE_RANGE;
TypeType LIT_TYPE_ARRAY;
TypeType LIT_TYPE_MAP;
TypeType LIT_TYPE_ELMT;
TypeType LIT_TYPE_FUNC;
TypeType LIT_TYPE_TYPE;
TypeType LIT_TYPE_ANY;
TypeType LIT_TYPE_ERROR;

TypeMap EmptyMap;
TypeElmt EmptyElmt;

Item ItemNull = {.type_id = LMD_TYPE_NULL};
Item ItemError = {.type_id = LMD_TYPE_ERROR};
String EMPTY_STRING = {.len = sizeof("lambda.nil") - 1, .ref_cnt = 0, .chars = "lambda.nil"};

TypedItem error_result = {.type_id = LMD_TYPE_ERROR};
TypedItem null_result = {.type_id = LMD_TYPE_NULL};

extern __thread Context* input_context;

void init_typetype() {
    *(Type*)&TYPE_ARRAY = {.type_id = LMD_TYPE_ARRAY};
    TYPE_ARRAY.nested = &TYPE_ANY;  // default nested type
    TYPE_ARRAY.length = 0;  TYPE_ARRAY.type_index = -1;
    *(Type*)(&LIT_TYPE_NULL) = LIT_TYPE;  LIT_TYPE_NULL.type = &TYPE_NULL;
    *(Type*)(&LIT_TYPE_BOOL) = LIT_TYPE;  LIT_TYPE_BOOL.type = &TYPE_BOOL;
    *(Type*)(&LIT_TYPE_INT) = LIT_TYPE;  LIT_TYPE_INT.type = &TYPE_INT64;
    *(Type*)(&LIT_TYPE_FLOAT) = LIT_TYPE;  LIT_TYPE_FLOAT.type = &TYPE_FLOAT;
    *(Type*)(&LIT_TYPE_DECIMAL) = LIT_TYPE;  LIT_TYPE_DECIMAL.type = &TYPE_DECIMAL;
    *(Type*)(&LIT_TYPE_NUMBER) = LIT_TYPE;  LIT_TYPE_NUMBER.type = &TYPE_NUMBER;
    *(Type*)(&LIT_TYPE_STRING) = LIT_TYPE;  LIT_TYPE_STRING.type = &TYPE_STRING;
    *(Type*)(&LIT_TYPE_BINARY) = LIT_TYPE;  LIT_TYPE_BINARY.type = &TYPE_BINARY;
    *(Type*)(&LIT_TYPE_SYMBOL) = LIT_TYPE;  LIT_TYPE_SYMBOL.type = &TYPE_SYMBOL;
    *(Type*)(&LIT_TYPE_DTIME) = LIT_TYPE;  LIT_TYPE_DTIME.type = &TYPE_DTIME;
    *(Type*)(&LIT_TYPE_LIST) = LIT_TYPE;  LIT_TYPE_LIST.type = &TYPE_LIST;
    *(Type*)(&LIT_TYPE_RANGE) = LIT_TYPE;  LIT_TYPE_RANGE.type = &TYPE_RANGE;
    *(Type*)(&LIT_TYPE_ARRAY) = LIT_TYPE;  LIT_TYPE_ARRAY.type = (Type*)&TYPE_ARRAY;
    *(Type*)(&LIT_TYPE_MAP) = LIT_TYPE;  LIT_TYPE_MAP.type = &TYPE_MAP;
    *(Type*)(&LIT_TYPE_ELMT) = LIT_TYPE;  LIT_TYPE_ELMT.type = &TYPE_ELMT;
    *(Type*)(&LIT_TYPE_FUNC) = LIT_TYPE;  LIT_TYPE_FUNC.type = &TYPE_FUNC;
    *(Type*)(&LIT_TYPE_TYPE) = LIT_TYPE;  LIT_TYPE_TYPE.type = &TYPE_TYPE;
    *(Type*)(&LIT_TYPE_ANY) = LIT_TYPE;  LIT_TYPE_ANY.type = &TYPE_ANY;
    *(Type*)(&LIT_TYPE_ERROR) = LIT_TYPE;  LIT_TYPE_ERROR.type = &TYPE_ERROR;

    memset(&EmptyMap, 0, sizeof(TypeMap));
    EmptyMap.type_id = LMD_TYPE_MAP;  EmptyMap.type_index = -1;

    memset(&EmptyElmt, 0, sizeof(TypeElmt));
    EmptyElmt.type_id = LMD_TYPE_ELEMENT;  EmptyElmt.type_index = -1;  EmptyElmt.name = {0};
}

TypeInfo type_info[32];

void init_type_info() {
    type_info[LMD_TYPE_RAW_POINTER] = {sizeof(void*), "pointer", &TYPE_NULL, (Type*)&LIT_TYPE_NULL};
    type_info[LMD_TYPE_NULL] = {sizeof(bool), "null", &TYPE_NULL, (Type*)&LIT_TYPE_NULL};
    type_info[LMD_TYPE_BOOL] = {sizeof(bool), "bool", &TYPE_BOOL, (Type*)&LIT_TYPE_BOOL};
    type_info[LMD_TYPE_INT] = {sizeof(int), "int", &TYPE_INT, (Type*)&LIT_TYPE_INT};
    type_info[LMD_TYPE_INT64] = {sizeof(int64_t), "int", &TYPE_INT, (Type*)&LIT_TYPE_INT};
    type_info[LMD_TYPE_FLOAT] = {sizeof(double), "float", &TYPE_FLOAT, (Type*)&LIT_TYPE_FLOAT};
    type_info[LMD_TYPE_DECIMAL] = {sizeof(void*), "decimal", &TYPE_DECIMAL, (Type*)&LIT_TYPE_DECIMAL};
    type_info[LMD_TYPE_NUMBER] = {sizeof(double), "number", &TYPE_NUMBER, (Type*)&LIT_TYPE_NUMBER};
    type_info[LMD_TYPE_DTIME] = {sizeof(DateTime), "datetime", &TYPE_DTIME, (Type*)&LIT_TYPE_DTIME};
    type_info[LMD_TYPE_STRING] = {sizeof(char*), "string", &TYPE_STRING, (Type*)&LIT_TYPE_STRING};
    type_info[LMD_TYPE_SYMBOL] = {sizeof(char*), "symbol", &TYPE_SYMBOL, (Type*)&LIT_TYPE_SYMBOL};
    type_info[LMD_TYPE_BINARY] = {sizeof(char*), "binary", &TYPE_BINARY, (Type*)&LIT_TYPE_BINARY};
    type_info[LMD_TYPE_LIST] = {sizeof(void*), "list", &TYPE_LIST, (Type*)&LIT_TYPE_LIST};
    type_info[LMD_TYPE_RANGE] = {sizeof(void*), "array", &TYPE_RANGE, (Type*)&LIT_TYPE_RANGE};
    type_info[LMD_TYPE_ARRAY] = {sizeof(void*), "array", (Type*)&TYPE_ARRAY, (Type*)&LIT_TYPE_ARRAY};
    type_info[LMD_TYPE_ARRAY_INT] = {sizeof(void*), "array", (Type*)&TYPE_ARRAY, (Type*)&LIT_TYPE_ARRAY};
    type_info[LMD_TYPE_ARRAY_FLOAT] = {sizeof(void*), "array", (Type*)&TYPE_ARRAY, (Type*)&LIT_TYPE_ARRAY};
    type_info[LMD_TYPE_ARRAY_INT64] = {sizeof(void*), "array", (Type*)&TYPE_ARRAY, (Type*)&LIT_TYPE_ARRAY};
    type_info[LMD_TYPE_MAP] = {sizeof(void*), "map", &TYPE_MAP, (Type*)&LIT_TYPE_MAP};
    type_info[LMD_TYPE_ELEMENT] = {sizeof(void*), "element", &TYPE_ELMT, (Type*)&LIT_TYPE_ELMT};
    type_info[LMD_TYPE_TYPE] = {sizeof(void*), "type", &TYPE_TYPE, (Type*)&LIT_TYPE_TYPE};
    type_info[LMD_TYPE_FUNC] = {sizeof(void*), "function", &TYPE_FUNC, (Type*)&LIT_TYPE_FUNC};
    type_info[LMD_TYPE_ANY] = {sizeof(TypedItem), "any", &TYPE_ANY, (Type*)&LIT_TYPE_ANY};
    type_info[LMD_TYPE_ERROR] = {sizeof(void*), "error", &TYPE_ERROR, (Type*)&LIT_TYPE_ERROR};
    type_info[LMD_CONTAINER_HEAP_START] = {0, "container_start", &TYPE_NULL, (Type*)&LIT_TYPE_NULL};
}

struct Initializer {
    Initializer() {
        init_typetype();
        init_type_info();
    }
};
static Initializer initializer;

Type* alloc_type(Pool* pool, TypeId type, size_t size) {
    Type* t;
    t = (Type*)pool_calloc(pool, size);
    memset(t, 0, size);
    t->type_id = type;
    // Defensive check: verify the type was properly initialized
    if (t->is_const != 0) {
        log_warn("Warning: alloc_type - is_const flag was not zeroed properly");
        t->is_const = 0; // Force correction
    }
    return t;
}

void expand_list(List *list) {
    log_debug("expand list:: %p, length: %ld, extra: %ld, capacity: %ld", list, list->length, list->extra, list->capacity);
    log_item({.list = list}, "list to expand");
    list->capacity = list->capacity ? list->capacity * 2 : 8;
    // list items are allocated from C heap, instead of Lambda heap
    // to consider: could also alloc directly from Lambda heap without the heap entry
    // need to profile to see which is faster
    Item* old_items = list->items;
    list->items = (Item*)realloc(list->items, list->capacity * sizeof(Item));
    // copy extra items to the end of the list
    if (list->extra) {
        memcpy(list->items + (list->capacity - list->extra),
            list->items + (list->capacity/2 - list->extra), list->extra * sizeof(Item));
        // scan the list, if the item is long/double,
        // and is stored in the list extra slots, need to update the pointer
        for (int i = 0; i < list->length; i++) {
            Item itm = list->items[i];
            if (itm.type_id == LMD_TYPE_FLOAT || itm.type_id == LMD_TYPE_INT64 ||
                itm.type_id == LMD_TYPE_DTIME) {
                Item* old_pointer = (Item*)itm.pointer;
                // Only update pointers that are in the old list buffer's extra space
                if (old_items <= old_pointer && old_pointer < old_items + list->capacity/2) {
                    int offset = old_items + list->capacity/2 - old_pointer;
                    void* new_pointer = list->items + list->capacity - offset;
                    list->items[i] = {.item = itm.type_id == LMD_TYPE_FLOAT ? d2it(new_pointer) :
                        itm.type_id == LMD_TYPE_INT64 ? l2it(new_pointer) : k2it(new_pointer)};
                }
                // if the pointer is not in the old buffer, it should not be updated
            }
        }
    }
    log_item({.list = list}, "list_expanded");
    log_debug("list expanded: %d, capacity: %ld", list->type_id, list->capacity);
}

Array* array_pooled(Pool *pool) {
    Array* arr = (Array*)pool_calloc(pool, sizeof(Array));
    if (arr == NULL) return NULL;
    memset(arr, 0, sizeof(Array));
    arr->type_id = LMD_TYPE_ARRAY;
    return arr;
}

Pool* variable_mem_pool_create() {
    return pool_create();
}

void variable_mem_pool_destroy(Pool* pool) {
    if (pool) {
        pool_destroy(pool);
    }
}

void array_set(Array* arr, int index, Item itm) {
    arr->items[index] = itm;
    TypeId type_id = get_type_id(itm);
    log_debug("array set item: type: %d, index: %d, length: %ld, extra: %ld",
        type_id, index, arr->length, arr->extra);
    switch (type_id) {
    case LMD_TYPE_FLOAT: {
        double* dval = (double*)(arr->items + (arr->capacity - arr->extra - 1));
        *dval = *(double*)itm.pointer;  arr->items[index] = {.item = d2it(dval)};
        arr->extra++;
        log_debug("array set float: %lf", *dval);
        break;
    }
    case LMD_TYPE_INT64: {
        int64_t* ival = (int64_t*)(arr->items + (arr->capacity - arr->extra - 1));
        *ival = *(int64_t*)itm.pointer;  arr->items[index] = {.item = l2it(ival)};
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
    default:
        if (LMD_TYPE_LIST <= type_id && type_id <= LMD_TYPE_ELEMENT) {
            Container *container = itm.container;
            container->ref_cnt++;
        }
    }
}

void array_append(Array* arr, Item itm, Pool *pool) {
    if (arr->length + arr->extra + 2 > arr->capacity) { expand_list((List*)arr); }
    // no need to call array_set() as the item data is pooled
    // array_set(arr, arr->length, itm, pool);
    arr->items[arr->length] = itm;
    arr->length++;
}

void array_push(Array* arr, Item item) {
    TypeId type_id = get_type_id(item);
    if (type_id == LMD_TYPE_LIST) { // nest list is flattened
        log_debug("list_push: pushing nested list: %p, type_id: %d", item.list, type_id);
        // copy over the items
        List *nest_list = item.list;
        for (int i = 0; i < nest_list->length; i++) {
            Item nest_item = nest_list->items[i];
            array_push(arr, nest_item);
        }
        return;
    }
    if (arr->length + arr->extra + 2 > arr->capacity) { expand_list((List*)arr); }
    array_set(arr, arr->length, item);
    arr->length++;
}

void list_push(List *list, Item item) {
    TypeId type_id = get_type_id(item);
    // Safety check: if type_id says LIST but bitfield says STRING, trust the bitfield
    if (type_id == LMD_TYPE_LIST && item.type_id == LMD_TYPE_STRING) {
        fprintf(stderr, "WARNING: get_type_id returned LIST but bitfield is STRING! Treating as STRING. item.item=%016lx\n", item.item);
        fflush(stderr);
        type_id = LMD_TYPE_STRING;
    }
    log_debug("list_push: pushing item: type_id: %d, item.item: %lx", type_id, item.item);
    if (type_id == LMD_TYPE_NULL) { return; } // skip NULL value
    if (type_id == LMD_TYPE_LIST) { // nest list is flattened
        log_debug("list_push: pushing nested list: %p, type_id: %d, length: %ld", item.list, type_id, item.list->length);
        // copy over the items
        List *nest_list = item.list;
        fprintf(stderr, "DEBUG: nest_list pointer = %p, type_id=%d, item.item=%016lx\n", (void*)nest_list, type_id, item.item);
        fflush(stderr);
        if (nest_list == NULL || (uintptr_t)nest_list < 0x1000) {
            fprintf(stderr, "CRITICAL: Nested list pointer is invalid! type_id=%d, item.item=%016lx\n", type_id, item.item);
            fflush(stderr);
            return;
        }
        if (nest_list->items == NULL) {
            fprintf(stderr, "CRITICAL: Nested list has NULL items array! length=%ld, list=%p\n", nest_list->length, (void*)nest_list);
            fflush(stderr);
            return;
        }
        for (int i = 0; i < nest_list->length; i++) {
            Item nest_item = nest_list->items[i];
            list_push(list, nest_item);
        }
        return;
    }
    else if (type_id == LMD_TYPE_STRING) {
        // need to merge with previous string if any (unless disabled)
        if (list->length > 0 && list->items != NULL && !input_context->disable_string_merging) {
            log_debug("list_push: checking for string merging, list length: %ld", list->length);
            Item prev_item = list->items[list->length - 1];
            if (get_type_id(prev_item) == LMD_TYPE_STRING) {
                log_debug("list_push: merging strings");
                String *prev_str = (String*)prev_item.pointer;
                String *new_str = (String*)item.pointer;
                // merge the two strings
                size_t new_len = prev_str->len + new_str->len;
                String *merged_str;
                if (input_context->consts) { // dynamic runtime context
                    merged_str = (String *)input_context->context_alloc(sizeof(String) + new_len + 1, LMD_TYPE_STRING);
                } else {  // static (input) context
                    merged_str = (String*)pool_calloc(input_context->pool, sizeof(String) + new_len + 1);
                }
                memcpy(merged_str->chars, prev_str->chars, prev_str->len);
                memcpy(merged_str->chars + prev_str->len, new_str->chars, new_str->len);
                merged_str->chars[new_len] = '\0';  merged_str->len = new_len;
                // replace previous string with new merged string
                merged_str->ref_cnt = prev_str->ref_cnt;
                prev_str->ref_cnt = 0;  // to be freed later
                list->items[list->length - 1] = (Item){.item = s2it(merged_str)};
                return;
            }
        }
    }
    else if (LMD_TYPE_RANGE <= type_id && type_id <= LMD_TYPE_ELEMENT) {
        Container *container = item.container;
        container->ref_cnt++;
    }

    // store the value in the list (and we may need two slots for long/double)
    log_debug("list pushing item: type: %d, length: %ld", type_id, list->length);
    if (list->length + list->extra + 2 > list->capacity) { expand_list(list); }
    // Safety check: ensure items array was allocated
    if (list->items == NULL) {
        log_error("CRITICAL: list->items is NULL after expand_list! length=%ld, capacity=%ld", list->length, list->capacity);
        return;  // Prevent crash
    }
    // Note: TYPE_ERROR will be stored as it is
    list->items[list->length++] = item;
    switch (item.type_id) {
    case LMD_TYPE_STRING:  case LMD_TYPE_SYMBOL:  case LMD_TYPE_BINARY: {
        String *str = (String*)item.pointer;
        str->ref_cnt++;
        break;
    }
    case LMD_TYPE_DECIMAL: {
        Decimal *dval = (Decimal*)item.pointer;
        if (dval && dval->dec_val) {
            char *buf = mpd_to_sci(dval->dec_val, 1);
            if (buf) free(buf);
        } else {
        log_debug("DEBUG list_push: pushed null decimal value");
        }
        dval->ref_cnt++;
        break;
    }
    case LMD_TYPE_FLOAT: {
        double* dval = (double*)(list->items + (list->capacity - list->extra - 1));
        *dval = *(double*)item.pointer;
        list->items[list->length-1] = {.item = d2it(dval)};
        list->extra++;
        log_debug("list_push: float value: %f", *dval);
        break;
    }
    case LMD_TYPE_INT64: {
        int64_t* ival = (int64_t*)(list->items + (list->capacity - list->extra - 1));
        *ival = *(int64_t*)item.pointer;  list->items[list->length-1] = {.item = l2it(ival)};
        list->extra++;
        log_debug("list_push: int64 value: %ld", *ival);
        break;
    }
    case LMD_TYPE_DTIME:  {
        DateTime* dtval = (DateTime*)(list->items + (list->capacity - list->extra - 1));
        DateTime dt = *dtval = *(DateTime*)item.pointer;  list->items[list->length-1] = {.item = k2it(dtval)};
        StrBuf *strbuf = strbuf_new();
        datetime_format_lambda(strbuf, &dt);
        log_debug("list_push: pushed datetime value: %s", strbuf->str);
        strbuf_free(strbuf);
        list->extra++;
        break;
    }
    }
    // log_item({.list = list}, "list_after_push");
}

TypedItem to_typed(Item item) {
    TypeId type_id = get_type_id(item);
    TypedItem result = {.type_id = type_id};

    switch (type_id) {
    case LMD_TYPE_NULL:
        return null_result;
    case LMD_TYPE_BOOL:
        result.bool_val = *(bool*)&item.item;
        return result;
    case LMD_TYPE_INT:
        result.int_val = *(int*)&item.item;
        return result;
    case LMD_TYPE_INT64: {
        int64_t lval = *(int64_t*)item.pointer;
        result.long_val = lval;
        return result;
    }
    case LMD_TYPE_FLOAT: {
        double dval = *(double*)item.pointer;
        result.double_val = dval;
        return result;
    }
    case LMD_TYPE_DTIME: {
        DateTime dtval = *(DateTime*)item.pointer;
        result.datetime_val = dtval;
        return result;
    }
    case LMD_TYPE_DECIMAL:
        result.decimal = (Decimal*)item.pointer;
        return result;
    case LMD_TYPE_STRING:
        result.string = (String*)item.pointer;
        return result;
    case LMD_TYPE_SYMBOL:
        result.string = (String*)item.pointer;
        return result;
    case LMD_TYPE_BINARY:
        result.string = (String*)item.pointer;
        return result;
    case LMD_TYPE_RANGE:
        result.range = item.range;
        return result;
    case LMD_TYPE_ARRAY:  case LMD_TYPE_ARRAY_INT:  case LMD_TYPE_ARRAY_INT64: case LMD_TYPE_ARRAY_FLOAT:
        result.array = item.array;
        return result;
    case LMD_TYPE_LIST:
        result.list = item.list;
        return result;
    case LMD_TYPE_MAP:
        result.map = item.map;
        return result;
    case LMD_TYPE_ELEMENT:
        result.element = item.element;
        return result;
    case LMD_TYPE_TYPE:  case LMD_TYPE_FUNC:
        result.pointer = item.raw_pointer;
        return result;
    default:
        log_error("unknown list item type %d", type_id);
        return error_result;
    }
}

TypedItem list_get_typed(List* list, int index) {
    log_debug("list_get_typed %p, index: %d", list, index);
    if (!list) { return null_result; }
    if (index < 0 || index >= list->length) {
        log_error("list_get_typed: index out of bounds: %d", index);
        return null_result;
    }
    return to_typed(list->items[index]);
}

void set_fields(TypeMap *map_type, void* map_data, va_list args) {
    long count = map_type->length;
    log_debug("map length: %ld", count);
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
                log_error("expected a map, got type %d", itm.type_id );
            }
        } else {
            log_debug("map set field: %.*s, type: %d", (int)field->name->length, field->name->str, field->type->type_id);
            switch (field->type->type_id) {
            case LMD_TYPE_NULL: {
                *(bool*)field_ptr = (bool)va_arg(args, int);
                break;
            }
            case LMD_TYPE_BOOL: {
                *(bool*)field_ptr = (bool)va_arg(args, int);
                break;
            }
            case LMD_TYPE_INT: {
                *(int*)field_ptr = va_arg(args, int);
                log_debug("set field of int type to: %d", *(int*)field_ptr);
                break;
            }
            case LMD_TYPE_INT64: {
                *(int64_t*)field_ptr = va_arg(args, int64_t);
                break;
            }
            case LMD_TYPE_FLOAT: {
                *(double*)field_ptr = va_arg(args, double);
                break;
            }
            case LMD_TYPE_DTIME:  {
                DateTime dtval = va_arg(args, DateTime);
                StrBuf *strbuf = strbuf_new();
                datetime_format_lambda(strbuf, &dtval);
                log_debug("set field of datetime type to: %s", strbuf->str);
                *(DateTime*)field_ptr = dtval;
                break;
            }
            case LMD_TYPE_STRING:  case LMD_TYPE_SYMBOL:  case LMD_TYPE_BINARY: {
                String *str = va_arg(args, String*);
                *(String**)field_ptr = str;
                str->ref_cnt++;
                break;
            }
            case LMD_TYPE_ARRAY:  case LMD_TYPE_ARRAY_INT:  case LMD_TYPE_ARRAY_INT64:  case LMD_TYPE_ARRAY_FLOAT:
            case LMD_TYPE_RANGE:  case LMD_TYPE_LIST:  case LMD_TYPE_MAP:  case LMD_TYPE_ELEMENT: {
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
            case LMD_TYPE_ANY: { // a special case
                Item item = va_arg(args, Item);
                TypeId type_id = get_type_id(item);
                log_debug("set field of ANY type to: %d", type_id);
                TypedItem titem = {.type_id =type_id, .pointer = item.raw_pointer};
                switch (type_id) {
                case LMD_TYPE_NULL: ;
                    break; // no extra work needed
                case LMD_TYPE_BOOL:
                    titem.bool_val = item.bool_val;  break;
                case LMD_TYPE_INT:
                    titem.int_val = item.int_val;  break;
                case LMD_TYPE_INT64:
                    titem.long_val = *(int64_t*)item.pointer;  break;
                case LMD_TYPE_FLOAT:
                    titem.double_val = *(double*)item.pointer;  break;
                case LMD_TYPE_DTIME:
                    titem.datetime_val = *(DateTime*)item.pointer;  break;
                case LMD_TYPE_STRING:  case LMD_TYPE_SYMBOL:  case LMD_TYPE_BINARY: {
                    String *str = (String*)item.pointer;
                    titem.string = str;  str->ref_cnt++;
                    break;
                }
                case LMD_TYPE_ARRAY:  case LMD_TYPE_ARRAY_INT:  case LMD_TYPE_ARRAY_FLOAT:
                case LMD_TYPE_LIST:  case LMD_TYPE_MAP:  case LMD_TYPE_ELEMENT: {
                    Container *container = item.container;
                    titem.pointer = container;  container->ref_cnt++;
                    break;
                }
                case LMD_TYPE_TYPE:  case LMD_TYPE_FUNC:
                    titem.pointer = item.raw_pointer;  // just a pointer
                    break;
                default:
                    log_error("unknown type %d in set_fields", type_id);
                    // set as ERROR
                    titem = {.type_id = LMD_TYPE_ERROR};
                }
                // set in map
                *(TypedItem*)field_ptr = titem;
                break;
            }
            default:
                log_error("unknown type %d", field->type->type_id);
            }
        }
        field = field->next;
    }
}

extern TypeMap EmptyMap;

Map* map_pooled(Pool *pool) {
    Map *map = (Map *)pool_calloc(pool, sizeof(Map));
    map->type_id = LMD_TYPE_MAP;
    map->type = &EmptyMap;
    return map;
}

TypedItem _map_get_typed(TypeMap* map_type, void* map_data, char *key, bool *is_found) {
    ShapeEntry *field = map_type->shape;
    while (field) {
        if (!field->name) { // nested map, skip
            Map* nested_map = *(Map**)((char*)map_data + field->byte_offset);
            bool nested_is_found;
            TypedItem result = _map_get_typed((TypeMap*)nested_map->type, nested_map->data, key, &nested_is_found);
            if (nested_is_found) {
                *is_found = true;
                return result;
            }
            field = field->next;
            continue;
        }
        log_debug("map_get_typed compare field: %.*s", (int)field->name->length, field->name->str);
        if (strncmp(field->name->str, key, field->name->length) == 0 &&
            strlen(key) == field->name->length) {
            *is_found = true;
            TypeId type_id = field->type->type_id;
            void* field_ptr = (char*)map_data + field->byte_offset;
            log_debug("map_get_typed found field: %.*s, type: %d, ptr: %p",
                (int)field->name->length, field->name->str, type_id, field_ptr);

            TypedItem result = {.type_id = type_id};
            switch (type_id) {
            case LMD_TYPE_NULL:
                return null_result;
            case LMD_TYPE_BOOL:
                result.bool_val = *(bool*)field_ptr;
                return result;
            case LMD_TYPE_INT:
                result.int_val = *(int*)field_ptr;
                return result;
            case LMD_TYPE_INT64:
                result.long_val = *(int64_t*)field_ptr;
                return result;
            case LMD_TYPE_FLOAT:
                result.double_val = *(double*)field_ptr;
                return result;
            case LMD_TYPE_DTIME: {
                result.datetime_val = *(DateTime*)field_ptr;
                StrBuf *strbuf = strbuf_new();
                datetime_format_lambda(strbuf, &result.datetime_val);
                log_debug("map_get_typed datetime: %s", strbuf->str);
                strbuf_free(strbuf);
                return result;
            }
            case LMD_TYPE_DECIMAL:
                result.decimal = *(Decimal**)field_ptr;
                return result;
            case LMD_TYPE_STRING:
                result.string = *(String**)field_ptr;
                return result;
            case LMD_TYPE_SYMBOL:
                result.string = *(String**)field_ptr;
                return result;
            case LMD_TYPE_BINARY:
                result.string = *(String**)field_ptr;
                return result;

            case LMD_TYPE_RANGE:
                result.range = *(Range**)field_ptr;
                return result;
            case LMD_TYPE_ARRAY:  case LMD_TYPE_ARRAY_INT:  case LMD_TYPE_ARRAY_INT64:  case LMD_TYPE_ARRAY_FLOAT:
                result.array = *(Array**)field_ptr;
                return result;
            case LMD_TYPE_LIST:
                result.list = *(List**)field_ptr;
                return result;
            case LMD_TYPE_MAP:
                result.map = *(Map**)field_ptr;
                return result;
            case LMD_TYPE_ELEMENT:
                result.element = *(Element**)field_ptr;
                return result;
            case LMD_TYPE_TYPE:  case LMD_TYPE_FUNC:
                result.pointer = *(void**)field_ptr;
                return result;
            case LMD_TYPE_ANY: {
                log_debug("map_get_typed ANY type, pointer: %p", field_ptr);
                return *(TypedItem*)field_ptr;
            }
            default:
                log_error("unknown map item type %d", type_id);
                return error_result;
            }
        }
        field = field->next;
    }
    *is_found = false;
    log_debug("map_get_typed: key %s not found", key);
    return null_result;
}

TypedItem map_get_typed(Map* map, Item key) {
    log_debug("map_get_typed %p", map);
    if (!map || !key.item) { return null_result; }

    bool is_found;
    char *key_str = NULL;
    if (key.type_id == LMD_TYPE_STRING || key.type_id == LMD_TYPE_SYMBOL) {
        key_str = ((String*)key.pointer)->chars;
    } else {
        log_error("map_get_typed: key must be string or symbol, got type %d", key.type_id);
        return null_result;  // only string or symbol keys are supported
    }
    log_debug("map_get_typed key: %s", key_str);
    return _map_get_typed((TypeMap*)map->type, map->data, key_str, &is_found);
}

Element* elmt_pooled(Pool *pool) {
    Element *elmt = (Element *)pool_calloc(pool, sizeof(Element));
    elmt->type_id = LMD_TYPE_ELEMENT;
    elmt->type = &EmptyElmt;
    return elmt;
}

TypedItem elmt_get_typed(Element* elmt, Item key) {
    if (!elmt || !key.item) { return null_result;}
    bool is_found;
    char *key_str = NULL;
    if (key.type_id == LMD_TYPE_STRING || key.type_id == LMD_TYPE_SYMBOL) {
        key_str = ((String*)key.pointer)->chars;
    } else {
        return null_result;  // only string or symbol keys are supported
    }
    return _map_get_typed((TypeMap*)elmt->type, elmt->data, key_str, &is_found);
}

bool Element::has_attr(const char* attr_name) {
    if (!this || !this->type) return false;

    TypeElmt* type = (TypeElmt*)this->type;
    if (!type->shape) return false;

    ShapeEntry* shape = type->shape;
    // Iterate through the shape to find the attribute
    while (shape) {
        if (shape->name && strview_equal(shape->name, attr_name)) {
            return true;
        }
        shape = shape->next;
    }
    return false;
}

/*
TypedItem Element::get_attr(const char* attr_name) {
    if (!this || !this->type) return null_result;

    TypeElmt* type = (TypeElmt*)this->type;
    if (!type->shape) return null_result;

    ShapeEntry* shape = type->shape;
    // Iterate through the shape to find the attribute
    while (shape) {
        if (shape->name && strview_equal(shape->name, attr_name)) {
            return shape->value;
        }
        shape = shape->next;
    }
    return null_result;
}
*/
