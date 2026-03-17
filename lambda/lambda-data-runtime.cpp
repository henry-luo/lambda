
#include "transpiler.hpp"
#include "../lib/log.h"
#include "../lib/str.h"

// data zone allocation helpers (defined in lambda-mem.cpp)
extern "C" void* heap_data_alloc(size_t size);
extern "C" void* heap_data_calloc(size_t size);

extern __thread EvalContext* context;
void array_set(Array* arr, int index, Item itm);
void array_push(Array* arr, Item itm);
void set_fields(TypeMap *map_type, void* map_data, va_list args);
Item typeditem_to_item(TypedItem *titem);
RetItem fn_input1(Item url);

// External: path resolution for iteration (implemented in path.c)
extern "C" Item path_resolve_for_iteration(Path* path);
extern "C" void path_load_metadata(Path* path);

// External: interned ASCII char table (implemented in lambda-mem.cpp)
extern "C" String* get_ascii_char_string(unsigned char ch);

// VMap access helpers (implemented in vmap.cpp)
Item vmap_get_by_str(VMap* vm, const char* key);
Item vmap_get_by_item(VMap* vm, Item key);

// Internal helper: resolve path content and cache it
// Uses the new path_resolve_for_iteration which handles directories and files properly
static Item resolve_path_content(Path* path) {
    if (!path) return ItemNull;

    // Check if already resolved
    if (path->result != 0) {
        return {.item = path->result};
    }

    // Use the new path resolution which handles:
    // - Directories: returns list of child paths
    // - Files: returns parsed content
    // - Wildcards: expands to list of matching paths
    // - Non-existent: returns null
    // - Access errors: returns error
    return path_resolve_for_iteration(path);
}

Array* array() {
    Array *arr = (Array*)heap_calloc(sizeof(Array), LMD_TYPE_ARRAY);
    arr->type_id = LMD_TYPE_ARRAY;
    return arr;
}

Array* array_fill(Array* arr, int count, ...) {
    if (count > 0) {
        va_list args;
        va_start(args, count);
        arr->capacity = count;
        arr->items = (Item*)heap_data_alloc(count * sizeof(Item));
        for (int i = 0; i < count; i++) {
            array_push(arr, va_arg(args, Item));
        }
        va_end(args);
    }
    log_item({.list = arr}, "array_filled");
    return arr;
}

Item array_get(Array *array, int index) {
    if (!array || ((uintptr_t)array >> 56)) { return ItemNull; }
    if (index < 0 || index >= array->length) {
        return ItemNull;  // return null instead of error
    }
    Item item = array->items[index];
    switch (item._type_id) {
    case LMD_TYPE_INT64: {
        int64_t lval = item.get_int64();
        return push_l(lval); // need to push to num_stack, as long values are not ref counted
    }
    case LMD_TYPE_FLOAT: {
        double dval = item.get_double();
        return push_d(dval); // need to push to num_stack, as float values are not ref counted
    }
    case LMD_TYPE_DTIME: {
        DateTime dtval = item.get_datetime();
        return push_k(dtval); // need to push to num_stack, as datetime values are not ref counted
    }
    default:
        return item;
    }
}

ArrayInt* array_int() {
    ArrayInt *arr = (ArrayInt*)heap_calloc(sizeof(ArrayInt), LMD_TYPE_ARRAY_INT);
    arr->type_id = LMD_TYPE_ARRAY_INT;
    return arr;
}

// used when there's no interleaving with transpiled code
ArrayInt* array_int_new(int length) {
    ArrayInt *arr = (ArrayInt*)heap_calloc(sizeof(ArrayInt), LMD_TYPE_ARRAY_INT);
    arr->type_id = LMD_TYPE_ARRAY_INT;
    arr->length = length;  arr->capacity = length;
    arr->items = (int64_t*)heap_data_alloc(length * sizeof(int64_t));
    return arr;
}

ArrayInt* array_int_fill(ArrayInt *arr, int count, ...) {
    if (count > 0) {
        va_list args;
        va_start(args, count);
        arr->items = (int64_t*)heap_data_alloc(count * sizeof(int64_t));
        arr->length = count;  arr->capacity = count;
        for (int i = 0; i < count; i++) {
            arr->items[i] = va_arg(args, int64_t);
        }
        va_end(args);
    }
    return arr;
}

Item array_int_get(ArrayInt *array, int index) {
    if (!array || ((uintptr_t)array >> 56)) { return ItemNull; }
    // runtime type check: array may have been converted to generic by fn_array_set
    if (array->type_id != LMD_TYPE_ARRAY_INT)
        return array_get((Array*)array, index);
    if (index < 0 || index >= array->length) {
        log_debug("array_int_get: index out of bounds: %d", index);
        return ItemNull;  // return null instead of error
    }
    int64_t val = array->items[index];
    Item item = (Item){.item = i2it(val)};
    return item;
}

// Fast path: return raw int64_t without boxing — for use when result type is known int
// Only used for immutable arrays (let) where type widening cannot occur.
int64_t array_int_get_raw(ArrayInt *array, int index) {
    if (!array || (unsigned)index >= (unsigned)array->length) return 0;
    return array->items[index];
}

ArrayInt64* array_int64() {
    ArrayInt64 *arr = (ArrayInt64*)heap_calloc(sizeof(ArrayInt64), LMD_TYPE_ARRAY_INT64);
    arr->type_id = LMD_TYPE_ARRAY_INT64;
    return arr;
}

// used when there's no interleaving with transpiled code
ArrayInt64* array_int64_new(int length) {
    ArrayInt64 *arr = (ArrayInt64*)heap_calloc(sizeof(ArrayInt64), LMD_TYPE_ARRAY_INT64);
    arr->type_id = LMD_TYPE_ARRAY_INT64;
    arr->length = length;  arr->capacity = length;
    arr->items = (int64_t*)heap_data_alloc(length * sizeof(int64_t));
    return arr;
}

ArrayInt64* array_int64_fill(ArrayInt64 *arr, int count, ...) {
    if (count > 0) {
        va_list args;
        va_start(args, count);
        arr->items = (int64_t*)heap_data_alloc(count * sizeof(int64_t));
        arr->length = count;  arr->capacity = count;
        for (int i = 0; i < count; i++) {
            arr->items[i] = va_arg(args, int64_t);
        }
        va_end(args);
    }
    return arr;
}

Item array_int64_get(ArrayInt64* array, int index) {
    if (!array || ((uintptr_t)array >> 56)) { return ItemNull; }
    // runtime type check: array may have been converted to generic by fn_array_set
    if (array->type_id != LMD_TYPE_ARRAY_INT64)
        return array_get((Array*)array, index);
    if (index < 0 || index >= array->length) {
        log_debug("array_int64_get: index out of bounds: %d", index);
        return ItemNull;
    }
    return push_l(array->items[index]);
}

// Fast path: return raw int64_t without boxing
// Only used for immutable arrays (let) where type widening cannot occur.
int64_t array_int64_get_raw(ArrayInt64 *array, int index) {
    if (!array || (unsigned)index >= (unsigned)array->length) return 0;
    return array->items[index];
}

ArrayFloat* array_float() {
    ArrayFloat *arr = (ArrayFloat*)heap_calloc(sizeof(ArrayFloat), LMD_TYPE_ARRAY_FLOAT);
    arr->type_id = LMD_TYPE_ARRAY_FLOAT;
    return arr;
}

// used when there's no interleaving with transpiled code
ArrayFloat* array_float_new(int length) {
    ArrayFloat *arr = (ArrayFloat*)heap_calloc(sizeof(ArrayFloat), LMD_TYPE_ARRAY_FLOAT);
    arr->type_id = LMD_TYPE_ARRAY_FLOAT;
    arr->length = length;  arr->capacity = length;
    arr->items = (double*)heap_data_alloc(length * sizeof(double));
    return arr;
}

ArrayFloat* array_float_fill(ArrayFloat *arr, int count, ...) {
    if (count > 0) {
        va_list args;
        va_start(args, count);
        arr->type_id = LMD_TYPE_ARRAY_FLOAT;
        arr->items = (double*)heap_data_alloc(count * sizeof(double));
        arr->length = count;  arr->capacity = count;
        for (int i = 0; i < count; i++) {
            arr->items[i] = va_arg(args, double);
        }
        va_end(args);
    }
    return arr;
}

Item array_float_get(ArrayFloat* array, int index) {
    if (!array || ((uintptr_t)array >> 56)) { return ItemNull; }
    // runtime type check: array may have been converted to generic by fn_array_set
    if (array->type_id != LMD_TYPE_ARRAY_FLOAT)
        return array_get((Array*)array, index);
    if (index < 0 || index >= array->length) {
        log_debug("array_float_get: index out of bounds: %d", index);
        return ItemNull;
    }
    return push_d(array->items[index]);
}

// fast path — only used for immutable arrays where type widening cannot occur
double array_float_get_value(ArrayFloat *arr, int index) {
    if (!arr || index < 0 || index >= arr->length) {
        return NAN;  // Return NaN for invalid access
    }
    return arr->items[index];
}

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

void array_int_set(ArrayInt *arr, int index, int64_t value) {
    if (!arr || index < 0 || index >= arr->capacity) {
        return;
    }
    arr->items[index] = value;
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
            dval = value.get_double();
            break;
        case LMD_TYPE_INT64:
            dval = (double)(value.get_int64());
            break;
        case LMD_TYPE_INT:
            dval = (double)(value.get_int56());
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
    return list;
}

Item list_end(List *list) {
    log_leave();
    if (list->type_id == LMD_TYPE_ELEMENT) {
        log_item({.list = list}, "elmt_end");
        return {.list = list};
    }
    else {
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

// create a plain array without frame management (for auxiliary arrays like sort keys)
Array* array_plain() {
    Array* arr = (Array*)heap_calloc(sizeof(Array), LMD_TYPE_ARRAY);
    arr->type_id = LMD_TYPE_ARRAY;
    return arr;
}

// drop first n items from array in-place (for order by + offset)
void array_drop_inplace(Array* arr, int64_t n) {
    if (n <= 0) return;
    if (n >= arr->length) { arr->length = 0; return; }
    for (int64_t i = 0; i < arr->length - n; i++) {
        arr->items[i] = arr->items[i + n];
    }
    arr->length -= n;
}

// limit array to first n items in-place (for order by + limit)
void array_limit_inplace(Array* arr, int64_t n) {
    if (n < 0) n = 0;
    if (n < arr->length) arr->length = n;
}

// create a spreadable array for for-expression results
Array* array_spreadable() {
    Array* arr = (Array*)heap_calloc(sizeof(Array), LMD_TYPE_ARRAY);
    arr->type_id = LMD_TYPE_ARRAY;
    arr->is_spreadable = true;  // mark as spreadable
    return arr;
}

// finalize spreadable array - returns array as Item (no flattening)
// returns spreadable null for empty arrays so they can be skipped when spreading
Item array_end(Array* arr) {
    if (arr->length == 0) {
        // return spreadable null - will be skipped when added to collections
        return {.item = ITEM_NULL_SPREADABLE};
    }
    return {.array = arr};
}

// push item to array, spreading if the item is a spreadable array
// skips spreadable nulls (from empty for-expressions)
void array_push_spread(Array* arr, Item item) {
    TypeId type_id = get_type_id(item);
    // skip spreadable null (empty for-expression result)
    if (item.item == ITEM_NULL_SPREADABLE) {
        return;
    }
    if (type_id == LMD_TYPE_ARRAY) {
        Array* inner = item.array;
        if (inner && inner->is_spreadable) {
            for (int i = 0; i < inner->length; i++) {
                array_push(arr, inner->items[i]);
            }
            return;
        }
    }
    // check if this is a spreadable list
    if (type_id == LMD_TYPE_LIST) {
        List* inner = item.list;
        if (inner && inner->is_spreadable) {
            for (int i = 0; i < inner->length; i++) {
                array_push(arr, inner->items[i]);
            }
            return;
        }
    }
    // check if this is a spreadable ArrayInt
    if (type_id == LMD_TYPE_ARRAY_INT) {
        ArrayInt* inner = item.array_int;
        if (inner && inner->is_spreadable) {
            for (int i = 0; i < inner->length; i++) {
                array_push(arr, {.item = i2it(inner->items[i])});
            }
            return;
        }
    }
    // check if this is a spreadable ArrayInt64
    if (type_id == LMD_TYPE_ARRAY_INT64) {
        ArrayInt64* inner = item.array_int64;
        if (inner && inner->is_spreadable) {
            for (int i = 0; i < inner->length; i++) {
                array_push(arr, {.item = l2it(inner->items[i])});
            }
            return;
        }
    }
    // check if this is a spreadable ArrayFloat
    if (type_id == LMD_TYPE_ARRAY_FLOAT) {
        ArrayFloat* inner = item.array_float;
        if (inner && inner->is_spreadable) {
            for (int i = 0; i < inner->length; i++) {
                array_push(arr, {.item = d2it(inner->items[i])});
            }
            return;
        }
    }
    // not spreadable, push as-is
    array_push(arr, item);
}

// mark an item as spreadable (for spread operator *expr)
// works on arrays and lists - marks the is_spreadable flag
// returns the item unchanged for spreading when used with push_spread functions
Item item_spread(Item item) {
    TypeId type_id = get_type_id(item);
    if (type_id == LMD_TYPE_ARRAY) {
        Array* arr = item.array;
        if (arr) arr->is_spreadable = true;
    } else if (type_id == LMD_TYPE_LIST) {
        List* list = item.list;
        if (list) list->is_spreadable = true;
    } else if (type_id == LMD_TYPE_ARRAY_INT) {
        ArrayInt* arr = item.array_int;
        if (arr) arr->is_spreadable = true;
    } else if (type_id == LMD_TYPE_ARRAY_INT64) {
        ArrayInt64* arr = item.array_int64;
        if (arr) arr->is_spreadable = true;
    } else if (type_id == LMD_TYPE_ARRAY_FLOAT) {
        ArrayFloat* arr = item.array_float;
        if (arr) arr->is_spreadable = true;
    }
    // for other types, just return as-is (they will be pushed normally)
    return item;
}

Item list_fill(List *list, int count, ...) {
    va_list args;
    va_start(args, count);
    for (int i = 0; i < count; i++) {
        list_push(list, {.item = va_arg(args, uint64_t)});
    }
    va_end(args);
    return list_end(list);
}

Item list_get(List *list, int index) {
    if (!list || ((uintptr_t)list >> 56)) { return ItemNull; }
    if (index < 0 || index >= list->length) { return ItemNull; }
    Item item = list->items[index];
    switch (item._type_id) {
    case LMD_TYPE_INT64: {
        int64_t lval = item.get_int64();
        return push_l(lval);
    }
    case LMD_TYPE_FLOAT: {
        double dval = item.get_double();
        return push_d(dval);
    }
    default:
        return item;
    }
}

Map* map(int type_index) {
    Map *map = (Map *)heap_calloc(sizeof(Map), LMD_TYPE_MAP);
    map->type_id = LMD_TYPE_MAP;
    ArrayList* type_list = (ArrayList*)context->type_list;
    TypeMap *map_type = (TypeMap*)(type_list->data[type_index]);
    map->type = map_type;
    return map;
}

// Allocate map struct + data buffer in a single GC allocation.
// The data buffer is placed immediately after the Map struct,
// eliminating the separate heap_data_calloc call.
// This is used for all static maps where byte_size is known at transpile time.
Map* map_with_data(int type_index) {
    ArrayList* type_list = (ArrayList*)context->type_list;
    TypeMap *map_type = (TypeMap*)(type_list->data[type_index]);
    int64_t byte_size = map_type->byte_size;
    size_t total_size = sizeof(Map) + (byte_size > 0 ? (size_t)byte_size : 0);
    Map *m = (Map *)heap_calloc(total_size, LMD_TYPE_MAP);
    m->type_id = LMD_TYPE_MAP;
    m->type = map_type;
    if (byte_size > 0) {
        m->data = (char*)m + sizeof(Map);
        m->data_cap = (int)byte_size;
    }
    return m;
}

// zig cc has problem compiling this function, it seems to align the pointers to 8 bytes
Map* map_fill(Map* map, ...) {
    TypeMap *map_type = (TypeMap*)map->type;
    // skip data allocation if already set (combined allocation via map_with_data)
    if (!map->data) {
        map->data = heap_data_calloc(map_type->byte_size);
    }
    // set map fields
    va_list args;
    va_start(args, map_type->length);
    set_fields(map_type, map->data, args);
    va_end(args);
    return map;
}

// extract field value from a named shape entry's storage
Item _map_read_field(ShapeEntry* field, void* map_data) {
    TypeId type_id = field->type->type_id;
    void* field_ptr = (char*)map_data + field->byte_offset;
    switch (type_id) {
    case LMD_TYPE_NULL: {
        void* ptr = *(void**)field_ptr;
        if (ptr) {
            Container* container = (Container*)ptr;
            return {.container = container};
        }
        return ItemNull;
    }
    case LMD_TYPE_BOOL:
        return {.item = b2it(*(bool*)field_ptr)};
    case LMD_TYPE_UNDEFINED:
        return {.item = ((uint64_t)LMD_TYPE_UNDEFINED << 56)};
    case LMD_TYPE_INT:
        return {.item = i2it(*(int64_t*)field_ptr)};
    case LMD_TYPE_INT64:
        return push_l(*(int64_t*)field_ptr);
    case LMD_TYPE_FLOAT:
        return push_d(*(double*)field_ptr);
    case LMD_TYPE_DTIME: {
        DateTime dt = *(DateTime*)field_ptr;
        StrBuf *strbuf = strbuf_new();
        datetime_format_lambda(strbuf, &dt);
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
    case LMD_TYPE_LIST:  case LMD_TYPE_MAP:  case LMD_TYPE_ELEMENT:  case LMD_TYPE_OBJECT: {
        Container* container = *(Container**)field_ptr;
        if (!container) return ItemNull;
        return {.container = container};
    }
    case LMD_TYPE_TYPE:
        return {.type = *(Type**)field_ptr};
    case LMD_TYPE_FUNC:
        return {.function = *(Function**)field_ptr};
    case LMD_TYPE_PATH:
        return {.path = *(Path**)field_ptr};
    case LMD_TYPE_ANY: {
        return typeditem_to_item((TypedItem*)field_ptr);
    }
    case LMD_TYPE_ERROR:
        // field was stored with error type (e.g., from failed arithmetic) — return null
        return ItemNull;
    default:
        log_error("unknown map item type %s", get_type_name(type_id));
        return ItemError;
    }
}

// last-writer-wins: scan all entries in declaration order, keep the last match
Item _map_get(TypeMap* map_type, void* map_data, char *key, bool *is_found) {
    Item result = ItemNull;
    *is_found = false;
    ShapeEntry *field = map_type->shape;
    while (field) {
        if (!field->name) {
            // spread/nested map — search recursively
            Map* nested_map = *(Map**)((char*)map_data + field->byte_offset);
            if (nested_map && nested_map->type_id == LMD_TYPE_MAP) {
                bool nested_found;
                Item nested_result = _map_get((TypeMap*)nested_map->type, nested_map->data, key, &nested_found);
                if (nested_found) {
                    *is_found = true;
                    result = nested_result;
                    // don't return — later entries may override
                }
            }
        } else {
            // named field — direct match
            if (strncmp(field->name->str, key, field->name->length) == 0 &&
                strlen(key) == field->name->length) {
                *is_found = true;
                result = _map_read_field(field, map_data);
                // don't return — later entries may override
            }
        }
        field = field->next;
    }
    if (!*is_found) {
    }
    return result;
}

Item map_get(Map* map, Item key) {
    if (!map || !key.item) { return ItemNull;}
    bool is_found;
    char *key_str = NULL;
    if (key._type_id == LMD_TYPE_STRING || key._type_id == LMD_TYPE_SYMBOL) {
        key_str = (char*)key.get_chars();
    } else {
        log_error("map_get: key must be string or symbol, got type %s", get_type_name(key._type_id));
        return ItemNull;  // only string or symbol keys are supported
    }
    return _map_get((TypeMap*)map->type, map->data, key_str, &is_found);
}

Element* elmt(int type_index) {
    Element *elmt = (Element *)heap_calloc(sizeof(Element), LMD_TYPE_ELEMENT);
    elmt->type_id = LMD_TYPE_ELEMENT;
    ArrayList* type_list = (ArrayList*)context->type_list;
    TypeElmt *elmt_type = (TypeElmt*)(type_list->data[type_index]);
    elmt->type = elmt_type;
    return elmt;
}

Object* object(int type_index) {
    Object *obj = (Object *)heap_calloc(sizeof(Object), LMD_TYPE_OBJECT);
    obj->type_id = LMD_TYPE_OBJECT;
    ArrayList* type_list = (ArrayList*)context->type_list;
    // type_list stores TypeType wrapper; unwrap to get TypeObject
    Type* stored = (Type*)(type_list->data[type_index]);
    TypeObject *obj_type = (stored->type_id == LMD_TYPE_TYPE)
        ? (TypeObject*)((TypeType*)stored)->type
        : (TypeObject*)stored;
    obj->type = obj_type;
    return obj;
}

// Allocate object struct + data buffer in a single GC allocation.
// Same approach as map_with_data — data placed immediately after the struct.
Object* object_with_data(int type_index) {
    ArrayList* type_list = (ArrayList*)context->type_list;
    Type* stored = (Type*)(type_list->data[type_index]);
    TypeObject *obj_type = (stored->type_id == LMD_TYPE_TYPE)
        ? (TypeObject*)((TypeType*)stored)->type
        : (TypeObject*)stored;
    int64_t byte_size = obj_type->byte_size;
    size_t total_size = sizeof(Object) + (byte_size > 0 ? (size_t)byte_size : 0);
    Object *obj = (Object *)heap_calloc(total_size, LMD_TYPE_OBJECT);
    obj->type_id = LMD_TYPE_OBJECT;
    obj->type = obj_type;
    if (byte_size > 0) {
        obj->data = (char*)obj + sizeof(Object);
        obj->data_cap = (int)byte_size;
    }
    return obj;
}

Object* object_fill(Object* obj, ...) {
    TypeObject *obj_type = (TypeObject*)obj->type;
    // skip data allocation if already set (combined allocation via object_with_data)
    if (!obj->data) {
        obj->data = heap_data_calloc(obj_type->byte_size);
    }
    // set object fields (same layout as map)
    va_list args;
    va_start(args, obj_type->length);
    set_fields((TypeMap*)obj_type, obj->data, args);
    va_end(args);
    return obj;
}

Item object_get(Object* obj, Item key) {
    if (!obj || !key.item) { return ItemNull; }
    bool is_found;
    char *key_str = NULL;
    if (key._type_id == LMD_TYPE_STRING || key._type_id == LMD_TYPE_SYMBOL) {
        key_str = (char*)key.get_chars();
    } else {
        log_error("object_get: key must be string or symbol, got type %s", get_type_name(key._type_id));
        return ItemNull;
    }
    return _map_get((TypeMap*)obj->type, obj->data, key_str, &is_found);
}

// Register a compiled method function pointer on a TypeObject's method table
void object_type_set_method(int64_t type_index, const char* method_name,
                            fn_ptr func_ptr, int64_t arity, int64_t is_proc) {
    log_debug("object_type_set_method: type_index=%ld, method='%s', arity=%ld",
        type_index, method_name, arity);
    ArrayList* type_list = (ArrayList*)context->type_list;
    // type_list stores TypeType wrapper; unwrap to get TypeObject
    Type* stored = (Type*)(type_list->data[type_index]);
    TypeObject* obj_type = (stored->type_id == LMD_TYPE_TYPE)
        ? (TypeObject*)((TypeType*)stored)->type
        : (TypeObject*)stored;

    // Walk the TypeMethod linked list to find matching method
    TypeMethod* method = obj_type->methods;
    while (method) {
        if (strcmp(method->name->str, method_name) == 0) {
            // Create Function* with the compiled function pointer
            Function* fn = to_fn_named(func_ptr, (int)arity, method_name);
            method->fn = fn;
            log_debug("object_type_set_method: registered '%s' on '%.*s' fn=%p",
                method_name, (int)obj_type->type_name.length, obj_type->type_name.str, fn);
            return;
        }
        method = method->next;
    }
    log_error("object_type_set_method: method '%s' not found in type '%.*s'",
        method_name, (int)obj_type->type_name.length, obj_type->type_name.str);
}

// Register a compiled constraint function on a TypeObject
void object_type_set_constraint(int64_t type_index, fn_ptr constraint_func) {
    log_debug("object_type_set_constraint: type_index=%ld", type_index);
    ArrayList* type_list = (ArrayList*)context->type_list;
    Type* stored = (Type*)(type_list->data[type_index]);
    TypeObject* obj_type = (stored->type_id == LMD_TYPE_TYPE)
        ? (TypeObject*)((TypeType*)stored)->type
        : (TypeObject*)stored;
    obj_type->constraint_fn = (ConstraintFn)constraint_func;
    log_debug("object_type_set_constraint: registered constraint on '%.*s'",
        (int)obj_type->type_name.length, obj_type->type_name.str);
}

Element* elmt_fill(Element* elmt, ...) {
    TypeElmt *elmt_type = (TypeElmt*)elmt->type;
    // skip data allocation if already set (combined allocation via elmt_with_data)
    if (!elmt->data) {
        elmt->data = heap_data_calloc(elmt_type->byte_size);
    }
    // set attributes
    long count = elmt_type->length;
    va_list args;
    va_start(args, count);
    set_fields((TypeMap*)elmt_type, elmt->data, args);
    va_end(args);
    return elmt;
}

// get element attribute by key
Item elmt_get(Element* elmt, Item key) {
    if (!elmt || !key.item) { return ItemNull;}
    bool is_found;
    char *key_str = NULL;
    if (key._type_id == LMD_TYPE_STRING || key._type_id == LMD_TYPE_SYMBOL) {
        key_str = (char*)key.get_chars();
    } else {
        return ItemNull;  // only string or symbol keys are supported
    }

    // PRIORITY 1: First try to get user-defined attribute
    Item result = _map_get((TypeMap*)elmt->type, elmt->data, key_str, &is_found);
    if (is_found) {
        return result;
    }

    // PRIORITY 2: Fall back to system/built-in properties
    // handle special 'name' property - returns the element's tag name
    if (strcmp(key_str, "name") == 0) {
        TypeElmt* elmt_type = (TypeElmt*)elmt->type;
        if (elmt_type && elmt_type->name.str) {
            Symbol* tag_sym = heap_create_symbol(elmt_type->name.str, elmt_type->name.length);
            return {.item = y2it(tag_sym)};
        }
        return ItemNull;
    }

    return ItemNull;
}

Item item_at(Item data, int index) {
    if (!data.item) { return ItemNull; }

    // note: for out of bound access, we return null instead of error
    TypeId type_id = get_type_id(data);
    switch (type_id) {
    case LMD_TYPE_NULL:
    case LMD_TYPE_BOOL:
    case LMD_TYPE_ERROR:
        return ItemNull;  // indexing scalars returns null silently
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
        if (index < 0 || index >= range->length) { return ItemNull; }
        int64_t value = range->start + index;
        return {.item = i2it(value)};
    }
    case LMD_TYPE_ELEMENT: {
        // treat element as list
        return list_get(data.element, index);
    }
    case LMD_TYPE_STRING:  case LMD_TYPE_SYMBOL: {
        const char* chars = data.get_chars();
        uint32_t byte_len = data.get_len();
        if (index < 0) { return ItemNull; }
        String* str = data.get_string();

        // ASCII fast-path: byte index == char index, O(1)
        if (str->is_ascii) {
            if ((uint32_t)index >= byte_len) { return ItemNull; }
            if (type_id == LMD_TYPE_SYMBOL) {
                Symbol* ch_sym = heap_create_symbol(chars + index, 1);
                return {.item = y2it(ch_sym)};
            }
            // return interned single-char string if available
            unsigned char ch = (unsigned char)chars[index];
            String* interned = get_ascii_char_string(ch);
            if (interned) return {.item = s2it(interned)};
            String *ch_str = heap_strcpy((char*)(chars + index), 1);
            return {.item = s2it(ch_str)};
        }

        // UTF-8 path: combined bounds check + char-to-byte in a single pass
        // str_utf8_char_to_byte returns STR_NPOS if index is out of range
        size_t byte_offset = str_utf8_char_to_byte(chars, byte_len, (size_t)index);
        if (byte_offset == STR_NPOS) { return ItemNull; }
        // get the UTF-8 character length (1-4 bytes)
        size_t ch_len = str_utf8_char_len((unsigned char)chars[byte_offset]);
        if (ch_len == 0) ch_len = 1; // fallback for invalid UTF-8
        if (byte_offset + ch_len > byte_len) { return ItemNull; }
        // return a single character string/symbol
        if (type_id == LMD_TYPE_SYMBOL) {
            Symbol* ch_sym = heap_create_symbol(chars + byte_offset, ch_len);
            return {.item = y2it(ch_sym)};
        }
        // for single-byte ASCII chars in a UTF-8 string, use interned table
        if (ch_len == 1 && (unsigned char)chars[byte_offset] < 128) {
            String* interned = get_ascii_char_string((unsigned char)chars[byte_offset]);
            if (interned) return {.item = s2it(interned)};
        }
        String *ch_str = heap_strcpy((char*)(chars + byte_offset), (int)ch_len);
        return {.item = s2it(ch_str)};
    }
    case LMD_TYPE_PATH: {
        // Lazy evaluation: resolve path content and delegate to it
        Item resolved = resolve_path_content(data.path);
        return item_at(resolved, index);
    }
    case LMD_TYPE_VMAP: {
        VMap* vm = data.vmap;
        if (vm && vm->vtable) return vm->vtable->value_at(vm->data, (int64_t)index);
        return ItemNull;
    }
    // case LMD_TYPE_BINARY: todo - proper binary data access
    default:
        log_error("item_at: unsupported item_at type: %d", type_id);
        return ItemNull;
    }
}
// Get attribute by name from an Item (for map/element attribute access)
Item item_attr(Item data, const char* key) {
    if (!data.item || !key) { return ItemNull; }
    TypeId type_id = get_type_id(data);
    switch (type_id) {
    case LMD_TYPE_DTIME: {
        // datetime member properties - delegate to fn_member for consistency
        Item key_item = {.item = s2it(heap_create_name(key))};
        return fn_member(data, key_item);
    }
    case LMD_TYPE_MAP: {
        Map* map = data.map;
        bool is_found;
        return _map_get((TypeMap*)map->type, map->data, (char*)key, &is_found);
    }
    case LMD_TYPE_VMAP: {
        VMap* vm = data.vmap;
        return vmap_get_by_str(vm, key);
    }
    case LMD_TYPE_ELEMENT: {
        Element* elmt = data.element;
        // fall through to attribute lookup
        bool is_found;
        return _map_get((TypeMap*)elmt->type, elmt->data, (char*)key, &is_found);
    }
    case LMD_TYPE_PATH: {
        Path* path = data.path;
        if (!path) return ItemNull;

        // First, try to resolve the path and access the attribute from resolved content
        // This is needed for sys.* paths which resolve to Maps
        if (path->result == 0) {
            Item resolved = path_resolve_for_iteration(path);
            if (resolved.item == ItemError.item) return ItemNull;
        }
        if (path->result != 0) {
            Item resolved = {.item = path->result};
            TypeId resolved_type = get_type_id(resolved);
            if (resolved_type == LMD_TYPE_MAP || resolved_type == LMD_TYPE_ELEMENT || resolved_type == LMD_TYPE_OBJECT) {
                // Access attribute from resolved content
                return item_attr(resolved, key);
            }
        }

        // path.name - returns the leaf segment name as a string
        if (strcmp(key, "name") == 0) {
            if (!path->name) return ItemNull;
            String* name_str = heap_strcpy((char*)path->name, strlen(path->name));
            return (Item){.item = s2it(name_str)};
        }

        // path.parent - returns the parent path (one level up)
        if (strcmp(key, "parent") == 0) {
            if (path->parent && path->parent->parent) {
                return {.path = path->parent};
            }
            return ItemNull;
        }

        // Metadata-based properties - require loading metadata first
        if (strcmp(key, "is_dir") == 0 || strcmp(key, "is_file") == 0 || strcmp(key, "is_link") == 0 ||
            strcmp(key, "size") == 0 || strcmp(key, "modified") == 0) {
            // Load metadata if not already loaded
            if (!(path->flags & PATH_FLAG_META_LOADED)) {
                path_load_metadata(path);
            }

            PathMeta* meta = path->meta;
            if (!meta) {
                // Path doesn't exist or couldn't be stat'd
                if (strcmp(key, "size") == 0) return (Item){.item = i2it(-1)};  // -1 for unknown/error
                if (strcmp(key, "modified") == 0) return ItemNull;  // null for unknown
                return (Item){.item = b2it(false)};  // false for boolean flags
            }

            if (strcmp(key, "is_dir") == 0) {
                return (Item){.item = b2it((meta->flags & PATH_META_IS_DIR) != 0)};
            }
            if (strcmp(key, "is_file") == 0) {
                return (Item){.item = b2it((meta->flags & PATH_META_IS_DIR) == 0)};
            }
            if (strcmp(key, "is_link") == 0) {
                return (Item){.item = b2it((meta->flags & PATH_META_IS_LINK) != 0)};
            }
            if (strcmp(key, "size") == 0) {
                return push_l(meta->size);  // int64_t size
            }
            if (strcmp(key, "modified") == 0) {
                return push_k(meta->modified);  // DateTime
            }
        }

        // Unknown property
        log_debug("item_attr: unknown path property '%s'", key);
        return ItemNull;
    }
    default:
        log_debug("item_attr: unsupported type %d", type_id);
        return ItemNull;
    }
}

// Get list of attribute/field names from an Item
ArrayList* item_keys(Item data) {
    if (!data.item) { return NULL; }
    TypeId type_id = get_type_id(data);

    // Handle Path: resolve first, then get keys from resolved content
    if (type_id == LMD_TYPE_PATH) {
        Path* path = data.path;
        if (!path) return NULL;
        // Resolve the path if not already resolved
        if (path->result == 0) {
            Item resolved = path_resolve_for_iteration(path);
            if (resolved.item == ItemError.item) return NULL;
        }
        // Get keys from resolved content
        data = {.item = path->result};
        type_id = get_type_id(data);
    }

    switch (type_id) {
    case LMD_TYPE_MAP: {
        Map* map = data.map;
        TypeMap* map_type = (TypeMap*)map->type;
        ArrayList* keys = arraylist_new(8);
        ShapeEntry* field = map_type->shape;
        while (field) {
            if (field->name) {
                // Convert StrView to Symbol for the transpiled code
                StrView* sv = field->name;
                Symbol* sym = heap_create_symbol(sv->str, sv->length);
                arraylist_append(keys, (void*)sym);
            }
            field = field->next;
        }
        return keys;
    }
    case LMD_TYPE_VMAP: {
        VMap* vm = data.vmap;
        if (vm && vm->vtable && vm->data) {
            return vm->vtable->keys(vm->data);
        }
        return NULL;
    }
    case LMD_TYPE_ELEMENT: {
        Element* elmt = data.element;
        TypeMap* elmt_type = (TypeMap*)elmt->type;
        ArrayList* keys = arraylist_new(8);
        ShapeEntry* field = elmt_type->shape;
        while (field) {
            if (field->name) {
                // Convert StrView to Symbol for the transpiled code
                StrView* sv = field->name;
                Symbol* sym = heap_create_symbol(sv->str, sv->length);
                arraylist_append(keys, (void*)sym);
            }
            field = field->next;
        }
        return keys;
    }
    default:
        log_debug("item_keys: unsupported type %d", type_id);
        return NULL;
    }
}

// Unified for-loop iteration helpers.
// For element: attrs first (keyed), then children (indexed).
// key_filter: 0=ALL, 1=INT only, 2=SYMBOL only

// Get total iteration length for unified for-loop
int64_t iter_len(Item data, void* keys_ptr, int key_filter) {
    TypeId type_id = get_type_id(data);
    ArrayList* keys = (ArrayList*)keys_ptr;
    int64_t key_count = keys ? (int64_t)keys->length : 0;

    if (type_id == LMD_TYPE_ELEMENT) {
        int64_t child_count = (int64_t)data.element->length;
        if (key_filter == 1) return child_count;        // INT: children only
        if (key_filter == 2) return key_count;           // SYMBOL: attrs only
        return key_count + child_count;                  // ALL: attrs + children
    }
    if (type_id == LMD_TYPE_MAP || type_id == LMD_TYPE_VMAP || type_id == LMD_TYPE_OBJECT) {
        if (key_filter == 1) return 0;  // INT filter on map: no indexed entries
        return key_count;
    }
    // array/list/range: indexed entries
    if (key_filter == 2) return 0;  // SYMBOL filter on array: no keyed entries
    return fn_len(data);
}

// Get key at iteration index for unified for-loop
// Returns: int (boxed) for indexed entries, symbol string (boxed) for keyed entries
Item iter_key_at(Item data, void* keys_ptr, int64_t idx, int key_filter) {
    TypeId type_id = get_type_id(data);
    ArrayList* keys = (ArrayList*)keys_ptr;
    int64_t key_count = keys ? (int64_t)keys->length : 0;

    if (type_id == LMD_TYPE_ELEMENT) {
        if (key_filter == 2) {
            // SYMBOL: attrs only
            if (idx < key_count) {
                Symbol* key_sym = (Symbol*)keys->data[idx];
                return {.item = y2it(key_sym)};
            }
            return ItemNull;
        }
        if (key_filter == 1) {
            // INT: children only
            return {.item = i2it(idx)};
        }
        // ALL: attrs first, then children
        if (idx < key_count) {
            Symbol* key_sym = (Symbol*)keys->data[idx];
            return {.item = y2it(key_sym)};
        }
        return {.item = i2it(idx - key_count)};
    }
    if (type_id == LMD_TYPE_MAP || type_id == LMD_TYPE_VMAP || type_id == LMD_TYPE_OBJECT) {
        if (idx < key_count) {
            Symbol* key_sym = (Symbol*)keys->data[idx];
            return {.item = y2it(key_sym)};
        }
        return ItemNull;
    }
    // array/list/range: key is the index
    return {.item = i2it(idx)};
}

// Get value at iteration index for unified for-loop
Item iter_val_at(Item data, void* keys_ptr, int64_t idx, int key_filter) {
    TypeId type_id = get_type_id(data);
    ArrayList* keys = (ArrayList*)keys_ptr;
    int64_t key_count = keys ? (int64_t)keys->length : 0;

    if (type_id == LMD_TYPE_ELEMENT) {
        if (key_filter == 2) {
            // SYMBOL: attrs only
            if (idx < key_count) {
                Symbol* key_sym = (Symbol*)keys->data[idx];
                return item_attr(data, key_sym->chars);
            }
            return ItemNull;
        }
        if (key_filter == 1) {
            // INT: children only
            return list_get(data.element, (int)idx);
        }
        // ALL: attrs first, then children
        if (idx < key_count) {
            Symbol* key_sym = (Symbol*)keys->data[idx];
            return item_attr(data, key_sym->chars);
        }
        return list_get(data.element, (int)(idx - key_count));
    }
    if (type_id == LMD_TYPE_MAP || type_id == LMD_TYPE_VMAP || type_id == LMD_TYPE_OBJECT) {
        if (idx < key_count) {
            Symbol* key_sym = (Symbol*)keys->data[idx];
            return item_attr(data, key_sym->chars);
        }
        return ItemNull;
    }
    // array/list/range: use item_at
    return item_at(data, (int)idx);
}

// ============================================================================
// Runtime typed array coercion for type annotations (int[], float[], etc.)
// Converts generic Array/List to typed array, or validates existing typed array.
// Returns a pointer to the coerced typed array, or NULL if elements are incompatible.
// ============================================================================

void* ensure_typed_array(Item item, TypeId element_type_id) {
    TypeId item_tid = get_type_id(item);

    // already the correct typed array — pass through (container types are direct pointers)
    if (element_type_id == LMD_TYPE_INT &&
        (item_tid == LMD_TYPE_ARRAY_INT || item_tid == LMD_TYPE_RANGE)) {
        return (void*)item.container;
    }
    if (element_type_id == LMD_TYPE_FLOAT && item_tid == LMD_TYPE_ARRAY_FLOAT) {
        return (void*)item.container;
    }
    if (element_type_id == LMD_TYPE_INT64 && item_tid == LMD_TYPE_ARRAY_INT64) {
        return (void*)item.container;
    }

    // null/empty → return NULL (caller checks)
    if (item_tid == LMD_TYPE_NULL) return NULL;

    // -----------------------------------------------------------------------
    // Cross-convert any typed array (ArrayInt, ArrayInt64, ArrayFloat) to the
    // target typed array.  Handles every mismatch combination generically:
    //   e.g.  var arr:int[]   = fill(10, 0.0)   — ArrayFloat  → ArrayInt
    //         var arr:float[] = fill(10, 0)     — ArrayInt    → ArrayFloat
    //         var arr:int[]   = some_int64_arr  — ArrayInt64  → ArrayInt
    // -----------------------------------------------------------------------
    if (item_tid == LMD_TYPE_ARRAY_INT || item_tid == LMD_TYPE_ARRAY_INT64 ||
        item_tid == LMD_TYPE_ARRAY_FLOAT) {
        // extract source length and per-element accessor
        int64_t length;
        if (item_tid == LMD_TYPE_ARRAY_INT)        length = item.array_int->length;
        else if (item_tid == LMD_TYPE_ARRAY_INT64)  length = item.array_int64->length;
        else                                        length = item.array_float->length;

        if (element_type_id == LMD_TYPE_INT) {
            ArrayInt* typed = array_int_new((int)length);
            for (int64_t i = 0; i < length; i++) {
                if (item_tid == LMD_TYPE_ARRAY_INT)        typed->items[i] = item.array_int->items[i];
                else if (item_tid == LMD_TYPE_ARRAY_INT64) typed->items[i] = item.array_int64->items[i];
                else                                       typed->items[i] = (int64_t)item.array_float->items[i];
            }
            return typed;
        }
        else if (element_type_id == LMD_TYPE_FLOAT) {
            ArrayFloat* typed = array_float_new((int)length);
            for (int64_t i = 0; i < length; i++) {
                if (item_tid == LMD_TYPE_ARRAY_FLOAT)      typed->items[i] = item.array_float->items[i];
                else if (item_tid == LMD_TYPE_ARRAY_INT)    typed->items[i] = (double)item.array_int->items[i];
                else                                        typed->items[i] = (double)item.array_int64->items[i];
            }
            return typed;
        }
        else if (element_type_id == LMD_TYPE_INT64) {
            ArrayInt64* typed = array_int64_new((int)length);
            for (int64_t i = 0; i < length; i++) {
                if (item_tid == LMD_TYPE_ARRAY_INT64)      typed->items[i] = item.array_int64->items[i];
                else if (item_tid == LMD_TYPE_ARRAY_INT)    typed->items[i] = item.array_int->items[i];
                else                                        typed->items[i] = (int64_t)item.array_float->items[i];
            }
            return typed;
        }
    }

    // convert generic Array/List to typed array (Array and List are the same struct)
    if (item_tid == LMD_TYPE_ARRAY || item_tid == LMD_TYPE_LIST) {
        Array* arr = item.array;
        Item* items = arr->items;
        int64_t length = arr->length;

        if (element_type_id == LMD_TYPE_INT) {
            ArrayInt* typed = array_int_new((int)length);
            for (int64_t i = 0; i < length; i++) {
                TypeId elem_tid = get_type_id(items[i]);
                if (elem_tid != LMD_TYPE_INT && elem_tid != LMD_TYPE_INT64 && elem_tid != LMD_TYPE_BOOL) {
                    log_error("ensure_typed_array: element %lld has type %s, expected int", i, get_type_name(elem_tid));
                    return NULL;
                }
                typed->items[i] = it2i(items[i]);
            }
            return typed;
        }
        else if (element_type_id == LMD_TYPE_FLOAT) {
            ArrayFloat* typed = array_float_new((int)length);
            for (int64_t i = 0; i < length; i++) {
                TypeId elem_tid = get_type_id(items[i]);
                if (elem_tid != LMD_TYPE_FLOAT && elem_tid != LMD_TYPE_INT &&
                    elem_tid != LMD_TYPE_INT64 && elem_tid != LMD_TYPE_DECIMAL &&
                    elem_tid != LMD_TYPE_BOOL) {
                    log_error("ensure_typed_array: element %lld has type %s, expected float", i, get_type_name(elem_tid));
                    return NULL;
                }
                if (elem_tid == LMD_TYPE_BOOL)
                    typed->items[i] = items[i].bool_val ? 1.0 : 0.0;
                else
                    typed->items[i] = it2d(items[i]);
            }
            return typed;
        }
        else if (element_type_id == LMD_TYPE_INT64) {
            ArrayInt64* typed = array_int64_new((int)length);
            for (int64_t i = 0; i < length; i++) {
                TypeId elem_tid = get_type_id(items[i]);
                if (elem_tid != LMD_TYPE_INT && elem_tid != LMD_TYPE_INT64 && elem_tid != LMD_TYPE_BOOL) {
                    log_error("ensure_typed_array: element %lld has type %s, expected int64", i, get_type_name(elem_tid));
                    return NULL;
                }
                typed->items[i] = it2l(items[i]);
            }
            return typed;
        }
    }

    // incompatible type (e.g., int[] but got a string)
    log_error("ensure_typed_array: cannot coerce %s to %s[]", get_type_name(item_tid), get_type_name(element_type_id));
    return NULL;
}
