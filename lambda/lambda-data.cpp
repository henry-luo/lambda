#include "ast.hpp"
#include "../lib/log.h"
#include "../lib/mempool.h"
#include "../lib/arena.h"  // for arena_owns() and arena_realloc()

Type TYPE_NULL = {.type_id = LMD_TYPE_NULL};
Type TYPE_UNDEFINED = {.type_id = LMD_TYPE_UNDEFINED};  // JavaScript undefined
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
TypeType LIT_TYPE_INT64;
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

Item ItemNull = {._type_id = LMD_TYPE_NULL};
Item ItemError = {._type_id = LMD_TYPE_ERROR};

// Note: ConstItem has const members and cannot be assigned after initialization.
// These are zero-initialized and should be used via reinterpret_cast from appropriate Items.
alignas(ConstItem) static uint64_t error_result_storage = ITEM_ERROR;
alignas(ConstItem) static uint64_t null_result_storage = ITEM_NULL;

ConstItem& error_result = *reinterpret_cast<ConstItem*>(&error_result_storage);
ConstItem& null_result = *reinterpret_cast<ConstItem*>(&null_result_storage);

extern __thread Context* input_context;

void init_typetype() {
    *(Type*)&TYPE_ARRAY = {.type_id = LMD_TYPE_ARRAY};
    TYPE_ARRAY.nested = &TYPE_ANY;  // default nested type
    TYPE_ARRAY.length = 0;  TYPE_ARRAY.type_index = -1;
    *(Type*)(&LIT_TYPE_NULL) = LIT_TYPE;  LIT_TYPE_NULL.type = &TYPE_NULL;
    *(Type*)(&LIT_TYPE_BOOL) = LIT_TYPE;  LIT_TYPE_BOOL.type = &TYPE_BOOL;
    *(Type*)(&LIT_TYPE_INT) = LIT_TYPE;  LIT_TYPE_INT.type = &TYPE_INT;
    *(Type*)(&LIT_TYPE_INT64) = LIT_TYPE;  LIT_TYPE_INT64.type = &TYPE_INT64;
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
    type_info[LMD_TYPE_UNDEFINED] = {sizeof(bool), "undefined", &TYPE_UNDEFINED, (Type*)&LIT_TYPE_NULL};  // JS undefined
    type_info[LMD_TYPE_BOOL] = {sizeof(bool), "bool", &TYPE_BOOL, (Type*)&LIT_TYPE_BOOL};
    type_info[LMD_TYPE_INT] = {sizeof(int64_t), "int", &TYPE_INT, (Type*)&LIT_TYPE_INT};  // 64-bit to store 56-bit value
    type_info[LMD_TYPE_INT64] = {sizeof(int64_t), "int64", &TYPE_INT64, (Type*)&LIT_TYPE_INT64};
    type_info[LMD_TYPE_FLOAT] = {sizeof(double), "float", &TYPE_FLOAT, (Type*)&LIT_TYPE_FLOAT};
    type_info[LMD_TYPE_DECIMAL] = {sizeof(void*), "decimal", &TYPE_DECIMAL, (Type*)&LIT_TYPE_DECIMAL};
    type_info[LMD_TYPE_NUMBER] = {sizeof(double), "number", &TYPE_NUMBER, (Type*)&LIT_TYPE_NUMBER};
    type_info[LMD_TYPE_DTIME] = {sizeof(DateTime), "datetime", &TYPE_DTIME, (Type*)&LIT_TYPE_DTIME};
    type_info[LMD_TYPE_SYMBOL] = {sizeof(char*), "symbol", &TYPE_SYMBOL, (Type*)&LIT_TYPE_SYMBOL};
    type_info[LMD_TYPE_STRING] = {sizeof(char*), "string", &TYPE_STRING, (Type*)&LIT_TYPE_STRING};
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

extern "C" {

// Old it2l - redirects to get_int56() for int type
// Note: The main it2l function is defined below it2i

double it2d(Item itm) {
    if (itm._type_id == LMD_TYPE_INT) {
        return (double)itm.get_int56();
    }
    else if (itm._type_id == LMD_TYPE_INT64) {
        return (double)itm.get_int64();
    }
    else if (itm._type_id == LMD_TYPE_FLOAT) {
        return itm.get_double();
    }
    else if (itm._type_id == LMD_TYPE_DECIMAL) {
        Decimal* dec = itm.get_decimal();
        char* endptr;
        char* dec_str = mpd_to_sci(dec->dec_val, 0);
        double val = strtod(dec_str, &endptr);
        if (!dec_str || endptr == dec_str) {
            log_error("it2d: failed to convert decimal to double");
            return NAN; // conversion error
        }
        return val;
    }
    log_debug("invalid type %d", itm._type_id);
    // todo: push error
    return 0;
}

bool it2b(Item itm) {
    if (itm._type_id == LMD_TYPE_BOOL) {
        return itm.bool_val != 0;
    }
    // Convert other types to boolean following JavaScript rules
    else if (itm._type_id == LMD_TYPE_NULL) {
        return false;
    }
    else if (itm._type_id == LMD_TYPE_INT) {
        return itm.get_int56() != 0;
    }
    else if (itm._type_id == LMD_TYPE_FLOAT) {
        double d = itm.get_double();
        return !isnan(d) && d != 0.0;
    }
    else if (itm._type_id == LMD_TYPE_STRING) {
        String* str = itm.get_string();
        return str && str->len > 0;
    }
    // Objects are truthy
    return true;
}

int it2i(Item itm) {
    if (itm._type_id == LMD_TYPE_INT) {
        // extract int56 sign-extended to int64, truncate to int32 for legacy compatibility
        return (int)itm.get_int56();
    }
    else if (itm._type_id == LMD_TYPE_INT64) {
        return (int)itm.get_int64();
    }
    else if (itm._type_id == LMD_TYPE_FLOAT) {
        return (int)itm.get_double();
    }
    else if (itm._type_id == LMD_TYPE_BOOL) { // should bool be convertible to int?
        return itm.bool_val ? 1 : 0;
    }
    return ITEM_ERROR;
}

// extract int56 as int64 (full precision)
int64_t it2l(Item itm) {
    if (itm._type_id == LMD_TYPE_INT) {
        return itm.get_int56();
    }
    else if (itm._type_id == LMD_TYPE_INT64) {
        return itm.get_int64();
    }
    else if (itm._type_id == LMD_TYPE_FLOAT) {
        return (int64_t)itm.get_double();
    }
    else if (itm._type_id == LMD_TYPE_BOOL) {
        return itm.bool_val ? 1 : 0;
    }
    return INT64_MAX;  // error sentinel
}

String* it2s(Item itm) {
    if (itm._type_id == LMD_TYPE_STRING) {
        return itm.get_string();
    }
    // For other types, we'd need to convert to string
    // For now, return a default string
    return nullptr;
}

} // extern "C"

void expand_list(List *list, Arena* arena = nullptr) {
    log_debug("expand list:: %p, length: %ld, extra: %ld, capacity: %ld", list, list->length, list->extra, list->capacity);
    log_item({.list = list}, "list to expand");
    list->capacity = list->capacity ? list->capacity * 2 : 8;

    // Determine which allocator to use
    Item* old_items = list->items;
    bool use_arena = (arena != nullptr && old_items != nullptr && arena_owns(arena, old_items));

    if (use_arena) {
        // Use arena realloc for arena-allocated buffers (MarkBuilder path)
        list->items = (Item*)arena_realloc(arena, list->items,
                                           (list->capacity/2) * sizeof(Item),
                                           list->capacity * sizeof(Item));
        log_debug("arena_realloc used for list expansion");
    } else {
        // Use C heap realloc for pool-allocated/runtime containers
        list->items = (Item*)realloc(list->items, list->capacity * sizeof(Item));
        log_debug("C heap realloc used for list expansion");
    }

    // copy extra items to the end of the list
    if (list->extra) {
        memcpy(list->items + (list->capacity - list->extra),
            list->items + (list->capacity/2 - list->extra), list->extra * sizeof(Item));
        // scan the list, if the item is long/double,
        // and is stored in the list extra slots, need to update the pointer
        for (int i = 0; i < list->length; i++) {
            Item itm = list->items[i];
            if (itm._type_id == LMD_TYPE_FLOAT || itm._type_id == LMD_TYPE_INT64 || itm._type_id == LMD_TYPE_DTIME) {
                Item* old_pointer = (Item*)itm.double_ptr;
                // Only update pointers that are in the old list buffer's extra space
                if (old_items <= old_pointer && old_pointer < old_items + list->capacity/2) {
                    int offset = old_items + list->capacity/2 - old_pointer;
                    void* new_pointer = list->items + list->capacity - offset;
                    list->items[i] = {.item = itm._type_id == LMD_TYPE_FLOAT ? d2it(new_pointer) :
                        itm._type_id == LMD_TYPE_INT64 ? l2it(new_pointer) : k2it(new_pointer)};
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

// Arena-based allocation for MarkBuilder
Array* array_arena(Arena* arena) {
    Array* arr = (Array*)arena_alloc(arena, sizeof(Array));
    if (arr == NULL) return NULL;
    memset(arr, 0, sizeof(Array));
    arr->type_id = LMD_TYPE_ARRAY;
    return arr;
}

List* list_arena(Arena* arena) {
    List* list = (List*)arena_alloc(arena, sizeof(List));
    if (list == NULL) return NULL;
    memset(list, 0, sizeof(List));
    list->type_id = LMD_TYPE_LIST;
    return list;
}

void array_set(Array* arr, int index, Item itm) {
    arr->items[index] = itm;
    TypeId type_id = get_type_id(itm);
    log_debug("array set item: type: %d, index: %d, length: %ld, extra: %ld",
        type_id, index, arr->length, arr->extra);
    switch (type_id) {
    case LMD_TYPE_FLOAT: {
        double* dval = (double*)(arr->items + (arr->capacity - arr->extra - 1));
        *dval = itm.get_double();  arr->items[index] = {.item = d2it(dval)};
        arr->extra++;
        log_debug("array set float: %lf", *dval);
        break;
    }
    case LMD_TYPE_INT64: {
        int64_t* ival = (int64_t*)(arr->items + (arr->capacity - arr->extra - 1));
        *ival = itm.get_int64();  arr->items[index] = {.item = l2it(ival)};
        arr->extra++;
        break;
    }
    case LMD_TYPE_DTIME:  {
        DateTime* dtval = (DateTime*)(arr->items + (arr->capacity - arr->extra - 1));
        *dtval = itm.get_datetime();  arr->items[index] = {.item = k2it(dtval)};
        arr->extra++;
        break;
    }
    case LMD_TYPE_STRING:  case LMD_TYPE_SYMBOL:  case LMD_TYPE_BINARY: {
        String *str = itm.get_string();
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

void array_append(Array* arr, Item itm, Pool *pool, Arena* arena) {
    if (arr->length + arr->extra + 2 > arr->capacity) { expand_list((List*)arr, arena); }
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
    log_debug("list_push: pushing item: type_id: %d, item.item: %lx", type_id, item.item);
    // 1. skip NULL value
    if (type_id == LMD_TYPE_NULL) { return; }

    // 2. nest list is flattened
    if (type_id == LMD_TYPE_LIST) {
        log_debug("list_push: pushing nested list: %p, type_id: %d, length: %ld", item.list, type_id, item.list->length);
        // copy over the items
        List *nest_list = item.list;
        if (nest_list == NULL || (uintptr_t)nest_list < 0x1000) {
            log_error("list_push: nested list pointer is invalid! type_id=%d, item.item=%016lx", type_id, item.item);
            return;
        }
        if (nest_list->items == NULL) {
            log_error("list_push: nested list has NULL items array! length=%ld, list=%p", nest_list->length, (void*)nest_list);
            return;
        }
        for (int i = 0; i < nest_list->length; i++) {
            Item nest_item = nest_list->items[i];
            list_push(list, nest_item);
        }
        return;
    }

    // 3. need to merge with previous string if any (unless disabled)
    if (type_id == LMD_TYPE_STRING) {
        // Only attempt string merging if input_context is available and merging is enabled
        bool should_merge = input_context != NULL &&
                           !input_context->disable_string_merging &&
                           list->length > 0 &&
                           list->items != NULL;

        if (should_merge) {
            log_debug("list_push: checking for string merging, list length: %ld", list->length);
            Item prev_item = list->items[list->length - 1];
            if (get_type_id(prev_item) == LMD_TYPE_STRING) {
                log_debug("list_push: merging strings");
                String *prev_str = prev_item.get_string();
                String *new_str = item.get_string();
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
                merged_str->ref_cnt = prev_str->ref_cnt;
                prev_str->ref_cnt = 0;  // to be freed later
                // replace previous string with new merged string, in the list directly,
                // assuming the list is still being constructed/mutable
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
    switch (item._type_id) {
    case LMD_TYPE_STRING:  case LMD_TYPE_SYMBOL:  case LMD_TYPE_BINARY: {
        String *str = (String*)item.get_string();
        str->ref_cnt++;
        break;
    }
    case LMD_TYPE_DECIMAL: {
        Decimal *dval = item.get_decimal();
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
        *dval = item.get_double();
        list->items[list->length-1] = {.item = d2it(dval)};
        list->extra++;
        log_debug("list_push: float value: %f", *dval);
        break;
    }
    case LMD_TYPE_INT64: {
        int64_t* ival = (int64_t*)(list->items + (list->capacity - list->extra - 1));
        *ival = item.get_int64();  list->items[list->length-1] = {.item = l2it(ival)};
        list->extra++;
        log_debug("list_push: int64 value: %ld", *ival);
        break;
    }
    case LMD_TYPE_DTIME:  {
        DateTime* dtval = (DateTime*)(list->items + (list->capacity - list->extra - 1));
        DateTime dt = *dtval = item.get_datetime();  list->items[list->length-1] = {.item = k2it(dtval)};
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

// push item to list, spreading spreadable arrays inline
void list_push_spread(List *list, Item item) {
    TypeId type_id = get_type_id(item);
    // check if this is a spreadable array
    if (type_id == LMD_TYPE_ARRAY) {
        Array* arr = item.array;
        if (arr && arr->is_spreadable) {
            log_debug("list_push_spread: spreading array of length %ld", arr->length);
            for (int i = 0; i < arr->length; i++) {
                list_push(list, arr->items[i]);
            }
            return;
        }
    }
    // not spreadable, push as-is
    list_push(list, item);
}

ConstItem List::get(int index) const {
    log_debug("list_get_const %p, index: %d", this, index);
    if (!this) { return null_result; }
    if (index < 0 || index >= this->length) {
        log_error("list_get_const: index out of bounds: %d", index);
        return null_result;
    }
    return this->items[index].to_const();
}

void set_fields(TypeMap *map_type, void* map_data, va_list args) {
    long count = map_type->length;
    log_debug("map length: %ld", count);
    ShapeEntry *field = map_type->shape;
    for (long i = 0; i < count; i++) {
        // printf("set field of type: %d, offset: %ld, name:%.*s\n", field->type->type_id, field->byte_offset,
        //     field->name ? (int)field->name->length:4, field->name ? field->name->str : "null");
        void* field_ptr = ((uint8_t*)map_data) + field->byte_offset;
        // always read an Item (uint64_t) from varargs - transpiler now passes Items via box functions like i2it()
        Item item = {.item = va_arg(args, uint64_t)};
        if (!field->name) { // nested map
            log_debug("set nested map field of type: %d", field->type->type_id);
            TypeId type_id = get_type_id(item);
            if (type_id == LMD_TYPE_MAP) {
                Map* nested_map = item.map;
                nested_map->ref_cnt++;
                *(Map**)field_ptr = nested_map;
            } else {
                log_error("expected a map, got data of type %d", type_id);
                *(Map**)field_ptr = nullptr;
            }
        } else {
            log_debug("map set field: %.*s, type: %d, at offset: %d", (int)field->name->length,
                field->name->str, field->type->type_id, (int)field->byte_offset);
            switch (field->type->type_id) {
            case LMD_TYPE_NULL: {
                // item is ITEM_NULL, nothing to store
                break;
            }
            case LMD_TYPE_BOOL: {
                *(bool*)field_ptr = item.bool_val;
                break;
            }
            case LMD_TYPE_INT: {
                int64_t val = item.get_int56();
                *(int64_t*)field_ptr = val;  // store full 64-bit to preserve 56-bit value
                log_debug("set field of int type to val: %lld", (long long)val);
                break;
            }
            case LMD_TYPE_INT64: {
                *(int64_t*)field_ptr = item.get_int64();
                break;
            }
            case LMD_TYPE_FLOAT: {
                *(double*)field_ptr = item.get_double();
                break;
            }
            case LMD_TYPE_DTIME:  {
                DateTime dtval = item.get_datetime();
                // StrBuf *strbuf = strbuf_new();
                // datetime_format_lambda(strbuf, &dtval);
                // log_debug("set field of datetime type to: %s", strbuf->str);
                *(DateTime*)field_ptr = dtval;
                break;
            }
            case LMD_TYPE_STRING:  case LMD_TYPE_SYMBOL:  case LMD_TYPE_BINARY: {
                String *str = item.get_string();
                *(String**)field_ptr = str;
                str->ref_cnt++;
                break;
            }
            case LMD_TYPE_ARRAY:  case LMD_TYPE_ARRAY_INT:  case LMD_TYPE_ARRAY_INT64:  case LMD_TYPE_ARRAY_FLOAT:
            case LMD_TYPE_RANGE:  case LMD_TYPE_LIST:  case LMD_TYPE_MAP:  case LMD_TYPE_ELEMENT: {
                Container *container = item.container;
                *(Container**)field_ptr = container;
                container->ref_cnt++;
                break;
            }
            case LMD_TYPE_TYPE:  case LMD_TYPE_FUNC: {
                *(void**)field_ptr = (void*)item.function;  // use function pointer accessor
                break;
            }
            case LMD_TYPE_ANY: { // a special case
                TypeId type_id = get_type_id(item);
                log_debug("set field of ANY type to type: %d", type_id);
                TypedItem titem = {.type_id = type_id, .item = item.item};
                switch (type_id) {
                case LMD_TYPE_NULL: ;
                    break; // no extra work needed
                case LMD_TYPE_BOOL:
                    titem.bool_val = item.bool_val;  break;
                case LMD_TYPE_INT:
                    titem.int_val = item.int_val;  break;
                case LMD_TYPE_INT64:
                    titem.long_val = item.get_int64();  break;
                case LMD_TYPE_FLOAT:
                    titem.double_val = item.get_double();  break;
                case LMD_TYPE_DTIME:
                    titem.datetime_val = item.get_datetime();  break;
                case LMD_TYPE_STRING:  case LMD_TYPE_SYMBOL:  case LMD_TYPE_BINARY: {
                    String *str = item.get_string();
                    titem.string = str;  str->ref_cnt++;
                    break;
                }
                case LMD_TYPE_ARRAY:  case LMD_TYPE_ARRAY_INT:  case LMD_TYPE_ARRAY_FLOAT:
                case LMD_TYPE_LIST:  case LMD_TYPE_MAP:  case LMD_TYPE_ELEMENT: {
                    Container *container = item.container;
                    titem.container = container;  container->ref_cnt++;
                    break;
                }
                case LMD_TYPE_TYPE:
                    titem.type = item.type;
                    break;
                case LMD_TYPE_FUNC:
                    titem.function = item.function;
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

// Arena-based allocation for MarkBuilder
Map* map_arena(Arena* arena) {
    Map *map = (Map *)arena_alloc(arena, sizeof(Map));
    if (map == NULL) return NULL;
    memset(map, 0, sizeof(Map));
    map->type_id = LMD_TYPE_MAP;
    map->type = &EmptyMap;
    return map;
}

Item typeditem_to_item(TypedItem *titem) {
    switch (titem->type_id) {
    case LMD_TYPE_NULL:  return ItemNull;
    case LMD_TYPE_BOOL:
        return {.item = b2it(titem->bool_val)};
    case LMD_TYPE_INT:
        return {.item = i2it(titem->int_val)};
    case LMD_TYPE_INT64:
        return {.item = l2it(&titem->long_val)};
    case LMD_TYPE_FLOAT:
        return {.item = d2it(&titem->double_val)};
    case LMD_TYPE_DTIME:
        return {.item = k2it(&titem->item)};
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
        return {.item = titem->item};
    default:
        log_error("map_get ANY type is UNKNOWN: %d", titem->type_id);
        return ItemError;
    }
}

Item _map_field_to_item(void* field_ptr, TypeId type_id) {
    Item result = (Item){._type_id = type_id};
    switch (type_id) {
    case LMD_TYPE_NULL:
        return ItemNull;
    case LMD_TYPE_BOOL:
        result.bool_val = *(bool*)field_ptr;
        break;
    case LMD_TYPE_INT:
        result = {.item = i2it(*(int64_t*)field_ptr)};  // read full int64 to preserve 56-bit value
        break;
    case LMD_TYPE_INT64:
        result = {.item = l2it(field_ptr)};  // points to long directly
        break;
    case LMD_TYPE_FLOAT:
        result = {.item = d2it(field_ptr)};  // points to double directly
        break;
    case LMD_TYPE_DTIME:
        result = {.item = k2it(field_ptr)};  // points to datetime directly
        break;
    case LMD_TYPE_DECIMAL:
        result = {.item = c2it(*(Decimal**)field_ptr)};
        break;
    case LMD_TYPE_STRING:
        result = {.item = s2it(*(String**)field_ptr)};
        break;
    case LMD_TYPE_SYMBOL:
        result = {.item = y2it(*(String**)field_ptr)};
        break;
    case LMD_TYPE_BINARY:
        result = {.item = x2it(*(String**)field_ptr)};
        break;

    case LMD_TYPE_RANGE:  case LMD_TYPE_ARRAY:  case LMD_TYPE_ARRAY_INT:
    case LMD_TYPE_ARRAY_INT64:  case LMD_TYPE_ARRAY_FLOAT:  case LMD_TYPE_LIST:
    case LMD_TYPE_MAP:  case LMD_TYPE_ELEMENT:  case LMD_TYPE_TYPE:  case LMD_TYPE_FUNC:
        result.container = *(Container**)field_ptr;
        break;
    case LMD_TYPE_ANY: {
        log_debug("_map_get_const ANY type, pointer: %p", field_ptr);
        result = typeditem_to_item((TypedItem*)field_ptr);
        break;
    }
    default:
        log_error("unknown map item type %d", type_id);
        return ItemError;
    }
    return result;
}

ConstItem _map_get_const(TypeMap* map_type, void* map_data, const char *key, bool *is_found) {
    ShapeEntry *field = map_type->shape;
    while (field) {
        if (!field->name) { // nested map, skip
            Map* nested_map = *(Map**)((char*)map_data + field->byte_offset);
            bool nested_is_found;
            ConstItem result = _map_get_const((TypeMap*)nested_map->type, nested_map->data, key, &nested_is_found);
            if (nested_is_found) {
                *is_found = true;
                return result;
            }
            field = field->next;
            continue;
        }
        log_debug("_map_get_const compare field: %.*s", (int)field->name->length, field->name->str);
        if (strncmp(field->name->str, key, field->name->length) == 0 && strlen(key) == field->name->length) {
            *is_found = true;
            TypeId type_id = field->type->type_id;
            void* field_ptr = (char*)map_data + field->byte_offset;
            log_debug("_map_get_const found field: %.*s, type: %d, ptr: %p",
                (int)field->name->length, field->name->str, type_id, field_ptr);
            Item result = _map_field_to_item(field_ptr, type_id);
            return *(ConstItem*)&result;
        }
        field = field->next;
    }
    *is_found = false;
    log_debug("_map_get_const: key %s not found", key);
    return null_result;
}

ConstItem Map::get(const Item key) const {
    log_debug("map_get_const %p", this);
    if (!this || !key.item) { return null_result; }

    bool is_found;
    char *key_str = NULL;
    if (key._type_id == LMD_TYPE_STRING || key._type_id == LMD_TYPE_SYMBOL) {
        key_str = key.get_string()->chars;
    } else {
        log_error("map_get_const: key must be string or symbol, got type %d", key._type_id);
        return null_result;  // only string or symbol keys are supported
    }
    log_debug("map_get_const key: %s", key_str);
    return _map_get_const((TypeMap*)this->type, this->data, key_str, &is_found);
}

ConstItem Map::get(const char* key_str) const {
    log_debug("map_get_const %p", this);
    if (!this || !key_str) { return null_result; }
    bool is_found;
    return _map_get_const((TypeMap*)this->type, this->data, (char*)key_str, &is_found);
}

bool Map::has_field(const char* field_name) const {
    if (!this || !this->type) return false;

    TypeMap* type = (TypeMap*)this->type;
    if (!type->shape) return false;

    ShapeEntry* shape = type->shape;
    // Iterate through the shape to find the field
    while (shape) {
        if (shape->name && strview_equal(shape->name, field_name)) {
            return true;
        }
        shape = shape->next;
    }
    return false;
}

Element* elmt_pooled(Pool *pool) {
    Element *elmt = (Element *)pool_calloc(pool, sizeof(Element));
    elmt->type_id = LMD_TYPE_ELEMENT;
    elmt->type = &EmptyElmt;
    return elmt;
}

// Arena-based allocation for MarkBuilder
Element* elmt_arena(Arena* arena) {
    Element *elmt = (Element *)arena_alloc(arena, sizeof(Element));
    if (elmt == NULL) return NULL;
    memset(elmt, 0, sizeof(Element));
    elmt->type_id = LMD_TYPE_ELEMENT;
    elmt->type = &EmptyElmt;
    return elmt;
}

ConstItem Element::get_attr(const Item key) const {
    if (!this || !key.item) { return null_result;}
    bool is_found;
    char *key_str = NULL;
    if (key._type_id == LMD_TYPE_STRING || key._type_id == LMD_TYPE_SYMBOL) {
        key_str = key.get_string()->chars;
    } else {
        return null_result;  // only string or symbol keys are supported
    }
    return _map_get_const((TypeMap*)this->type, this->data, key_str, &is_found);
}

ConstItem Element::get_attr(const char* attr_name) const {
    if (!this || !attr_name) { return null_result;}
    bool is_found;
    return _map_get_const((TypeMap*)this->type, this->data, attr_name, &is_found);
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
