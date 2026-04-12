
#include "transpiler.hpp"
#include "../lib/log.h"
#include "../lib/str.h"
#include "input/css/dom_element.hpp"  // DomElement, dom_element_to_element, element_to_dom_element
#include "input/css/dom_node.hpp"     // DomText, dom_text_to_string, string_to_dom_text

// data zone allocation helpers (defined in lambda-mem.cpp)
extern "C" void* heap_data_alloc(size_t size);
extern "C" void* heap_data_calloc(size_t size);

extern __thread EvalContext* context;
void array_set(Array* arr, int64_t index, Item itm);
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

Item array_get(Array *array, int64_t index) {
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

// ============================================================================
// Unified ArrayNum factory
// ============================================================================
ArrayNum* array_num_new(ArrayNumElemType elem_type, int64_t length) {
    ArrayNum *arr = (ArrayNum*)heap_calloc(sizeof(ArrayNum), LMD_TYPE_ARRAY_NUM);
    arr->type_id = LMD_TYPE_ARRAY_NUM;
    arr->flags = elem_type;  // elem_type stored in Container::flags byte
    arr->length = length;  arr->capacity = length;
    int elem_size = ELEM_TYPE_SIZE[elem_type >> 4];
    arr->data = heap_data_alloc(length * elem_size);
    return arr;
}

// Unified ArrayNum getter — dispatches on elem_type
Item array_num_get(ArrayNum *array, int64_t index) {
    if (!array || ((uintptr_t)array >> 56)) { return ItemNull; }
    if (array->type_id != LMD_TYPE_ARRAY_NUM)
        return array_get((Array*)array, index);
    if (index < 0 || index >= array->length) {
        return ItemNull;
    }
    switch (array->get_elem_type()) {
    case ELEM_INT: {
        int64_t val = array->items[index];
        Item item = (Item){.item = i2it(val)};
        return item;
    }
    case ELEM_INT64:
        return push_l(array->items[index]);
    case ELEM_FLOAT:
        return push_d(array->float_items[index]);
    // compact sized types
    case ELEM_INT8:    return (Item){.item = i8_to_item(((int8_t*)array->data)[index])};
    case ELEM_INT16:   return (Item){.item = i16_to_item(((int16_t*)array->data)[index])};
    case ELEM_INT32:   return (Item){.item = i32_to_item(((int32_t*)array->data)[index])};
    case ELEM_UINT8:   return (Item){.item = u8_to_item(((uint8_t*)array->data)[index])};
    case ELEM_UINT16:  return (Item){.item = u16_to_item(((uint16_t*)array->data)[index])};
    case ELEM_UINT32:  return (Item){.item = u32_to_item(((uint32_t*)array->data)[index])};
    case ELEM_FLOAT16: return (Item){.item = f16_to_item(f16_bits_to_f32(((uint16_t*)array->data)[index]))};
    case ELEM_FLOAT32: return (Item){.item = f32_to_item(((float*)array->data)[index])};
    case ELEM_UINT64: {
        uint64_t val = ((uint64_t*)array->data)[index];
        uint64_t* heap_val = (uint64_t*)heap_calloc(sizeof(uint64_t), LMD_TYPE_UINT64);
        *heap_val = val;
        return (Item){.item = u64_to_item(heap_val)};
    }
    case ELEM_FLOAT64:
        return push_d(((double*)array->data)[index]);
    default:
        return ItemNull;
    }
}

ArrayNum* array_int() {
    ArrayNum *arr = (ArrayNum*)heap_calloc(sizeof(ArrayNum), LMD_TYPE_ARRAY_NUM);
    arr->type_id = LMD_TYPE_ARRAY_NUM;
    arr->flags = ELEM_INT;
    return arr;
}

// used when there's no interleaving with transpiled code
ArrayNum* array_int_new(int64_t length) {
    return array_num_new(ELEM_INT, length);
}

ArrayNum* array_int_fill(ArrayNum *arr, int count, ...) {
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

Item array_int_get(ArrayNum *array, int64_t index) {
    if (!array || ((uintptr_t)array >> 56)) { return ItemNull; }
    // runtime type check: array may have been converted to generic by fn_array_set
    if (array->type_id != LMD_TYPE_ARRAY_NUM)
        return array_get((Array*)array, index);
    if (index < 0 || index >= array->length) {
        log_debug("array_int_get: index out of bounds: %lld", (long long)index);
        return ItemNull;  // return null instead of error
    }
    int64_t val = array->items[index];
    Item item = (Item){.item = i2it(val)};
    return item;
}

// Fast path: return raw int64_t without boxing — for use when result type is known int
// Only used for immutable arrays (let) where type widening cannot occur.
int64_t array_int_get_raw(ArrayNum *array, int64_t index) {
    if (!array || (uint64_t)index >= (uint64_t)array->length) return 0;
    return array->items[index];
}

ArrayNum* array_int64() {
    ArrayNum *arr = (ArrayNum*)heap_calloc(sizeof(ArrayNum), LMD_TYPE_ARRAY_NUM);
    arr->type_id = LMD_TYPE_ARRAY_NUM;
    arr->flags = ELEM_INT64;
    return arr;
}

// used when there's no interleaving with transpiled code
ArrayNum* array_int64_new(int64_t length) {
    return array_num_new(ELEM_INT64, length);
}

ArrayNum* array_int64_fill(ArrayNum *arr, int count, ...) {
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

Item array_int64_get(ArrayNum* array, int64_t index) {
    if (!array || ((uintptr_t)array >> 56)) { return ItemNull; }
    // runtime type check: array may have been converted to generic by fn_array_set
    if (array->type_id != LMD_TYPE_ARRAY_NUM)
        return array_get((Array*)array, index);
    if (index < 0 || index >= array->length) {
        log_debug("array_int64_get: index out of bounds: %lld", (long long)index);
        return ItemNull;
    }
    return push_l(array->items[index]);
}

// Fast path: return raw int64_t without boxing
// Only used for immutable arrays (let) where type widening cannot occur.
int64_t array_int64_get_raw(ArrayNum *array, int64_t index) {
    if (!array || (uint64_t)index >= (uint64_t)array->length) return 0;
    return array->items[index];
}

ArrayNum* array_float() {
    ArrayNum *arr = (ArrayNum*)heap_calloc(sizeof(ArrayNum), LMD_TYPE_ARRAY_NUM);
    arr->type_id = LMD_TYPE_ARRAY_NUM;
    arr->flags = ELEM_FLOAT;
    return arr;
}

// used when there's no interleaving with transpiled code
ArrayNum* array_float_new(int64_t length) {
    return array_num_new(ELEM_FLOAT, length);
}

ArrayNum* array_float_fill(ArrayNum *arr, int count, ...) {
    if (count > 0) {
        va_list args;
        va_start(args, count);
        arr->type_id = LMD_TYPE_ARRAY_NUM;
        arr->flags = ELEM_FLOAT;
        arr->float_items = (double*)heap_data_alloc(count * sizeof(double));
        arr->length = count;  arr->capacity = count;
        for (int i = 0; i < count; i++) {
            arr->float_items[i] = va_arg(args, double);
        }
        va_end(args);
    }
    return arr;
}

Item array_float_get(ArrayNum* array, int64_t index) {
    if (!array || ((uintptr_t)array >> 56)) { return ItemNull; }
    // runtime type check: array may have been converted to generic by fn_array_set
    if (array->type_id != LMD_TYPE_ARRAY_NUM)
        return array_get((Array*)array, index);
    if (index < 0 || index >= array->length) {
        log_debug("array_float_get: index out of bounds: %lld", (long long)index);
        return ItemNull;
    }
    return push_d(array->float_items[index]);
}

// fast path — only used for immutable arrays where type widening cannot occur
double array_float_get_value(ArrayNum *arr, int64_t index) {
    if (!arr || index < 0 || index >= arr->length) {
        return NAN;  // Return NaN for invalid access
    }
    return arr->float_items[index];
}

void array_float_set(ArrayNum *arr, int64_t index, double value) {
    if (!arr || index < 0 || index >= arr->capacity) {
        return;  // Invalid access, do nothing
    }
    arr->float_items[index] = value;
    // Update length if we're setting beyond current length
    if (index >= arr->length) {
        arr->length = index + 1;
    }
}

void array_int_set(ArrayNum *arr, int64_t index, int64_t value) {
    if (!arr || index < 0 || index >= arr->capacity) {
        return;
    }
    arr->items[index] = value;
    if (index >= arr->length) {
        arr->length = index + 1;
    }
}

// set with item value
void array_float_set_item(ArrayNum *arr, int64_t index, Item value) {
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

    arr->float_items[index] = dval;
    // Update length if we're setting beyond current length
    if (index >= arr->length) {
        arr->length = index + 1;
    }
}

// helper: extract any numeric Item as int64_t for compact integer store
static int64_t item_to_int_value(Item value) {
    TypeId tid = get_type_id(value);
    switch (tid) {
    case LMD_TYPE_INT:       return value.get_int56();
    case LMD_TYPE_INT64:     return value.get_int64();
    case LMD_TYPE_FLOAT:     return (int64_t)value.get_double();
    case LMD_TYPE_NUM_SIZED: {
        NumSizedType st = (NumSizedType)NUM_SIZED_SUBTYPE(value.item);
        switch (st) {
        case NUM_INT8:    return item_to_i8(value.item);
        case NUM_INT16:   return item_to_i16(value.item);
        case NUM_INT32:   return item_to_i32(value.item);
        case NUM_UINT8:   return item_to_u8(value.item);
        case NUM_UINT16:  return item_to_u16(value.item);
        case NUM_UINT32:  return item_to_u32(value.item);
        case NUM_FLOAT16: return (int64_t)item_to_f16(value.item);
        case NUM_FLOAT32: return (int64_t)item_to_f32(value.item);
        default:          return 0;
        }
    }
    case LMD_TYPE_UINT64:    return (int64_t)value.get_uint64();
    default:                 return 0;
    }
}

// helper: extract any numeric Item as double for compact float store
static double item_to_float_value(Item value) {
    TypeId tid = get_type_id(value);
    switch (tid) {
    case LMD_TYPE_FLOAT:     return value.get_double();
    case LMD_TYPE_INT:       return (double)value.get_int56();
    case LMD_TYPE_INT64:     return (double)value.get_int64();
    case LMD_TYPE_NUM_SIZED: {
        NumSizedType st = (NumSizedType)NUM_SIZED_SUBTYPE(value.item);
        switch (st) {
        case NUM_FLOAT32: return (double)item_to_f32(value.item);
        case NUM_FLOAT16: return (double)item_to_f16(value.item);
        case NUM_INT8:    return (double)item_to_i8(value.item);
        case NUM_INT16:   return (double)item_to_i16(value.item);
        case NUM_INT32:   return (double)item_to_i32(value.item);
        case NUM_UINT8:   return (double)item_to_u8(value.item);
        case NUM_UINT16:  return (double)item_to_u16(value.item);
        case NUM_UINT32:  return (double)item_to_u32(value.item);
        default:          return 0.0;
        }
    }
    case LMD_TYPE_UINT64:    return (double)value.get_uint64();
    default:                 return 0.0;
    }
}

// Generic setter for all ArrayNum elem_types, dispatches on elem_type
void array_num_set_item(ArrayNum *arr, int64_t index, Item value) {
    if (!arr || index < 0 || index >= arr->capacity) return;
    switch (arr->get_elem_type()) {
    case ELEM_INT:
        arr->items[index] = item_to_int_value(value);
        break;
    case ELEM_INT64:
        arr->items[index] = item_to_int_value(value);
        break;
    case ELEM_FLOAT:
        arr->float_items[index] = item_to_float_value(value);
        break;
    case ELEM_INT8:
        ((int8_t*)arr->data)[index] = (int8_t)item_to_int_value(value);
        break;
    case ELEM_INT16:
        ((int16_t*)arr->data)[index] = (int16_t)item_to_int_value(value);
        break;
    case ELEM_INT32:
        ((int32_t*)arr->data)[index] = (int32_t)item_to_int_value(value);
        break;
    case ELEM_UINT8:
        ((uint8_t*)arr->data)[index] = (uint8_t)item_to_int_value(value);
        break;
    case ELEM_UINT16:
        ((uint16_t*)arr->data)[index] = (uint16_t)item_to_int_value(value);
        break;
    case ELEM_UINT32:
        ((uint32_t*)arr->data)[index] = (uint32_t)item_to_int_value(value);
        break;
    case ELEM_FLOAT16:
        ((uint16_t*)arr->data)[index] = f32_to_f16_bits((float)item_to_float_value(value));
        break;
    case ELEM_FLOAT32:
        ((float*)arr->data)[index] = (float)item_to_float_value(value);
        break;
    case ELEM_UINT64:
        ((uint64_t*)arr->data)[index] = (uint64_t)item_to_int_value(value);
        break;
    case ELEM_FLOAT64:
        ((double*)arr->data)[index] = item_to_float_value(value);
        break;
    default:
        return;
    }
    if (index >= arr->length) {
        arr->length = index + 1;
    }
}

List* list() {
    log_enter();
    List *list = (List *)heap_calloc(sizeof(List), LMD_TYPE_ARRAY);
    list->type_id = LMD_TYPE_ARRAY;
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
            list->is_content = 1;
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
    if (type_id == LMD_TYPE_ARRAY) {
        List* inner = item.list;
        if (inner && inner->is_spreadable) {
            for (int i = 0; i < inner->length; i++) {
                array_push(arr, inner->items[i]);
            }
            return;
        }
    }
    // check if this is a spreadable ArrayNum
    if (type_id == LMD_TYPE_ARRAY_NUM) {
        ArrayNum* inner = item.array_num;
        if (inner && inner->is_spreadable) {
            for (int64_t i = 0; i < inner->length; i++) {
                array_push(arr, array_num_get(inner, i));
            }
            return;
        }
    }
    // not spreadable, push as-is
    array_push(arr, item);
}

// push item to array, spreading any array type unconditionally (regardless of is_spreadable flag)
// used for pipe expression results in array literals: [a, pipe_expr | ~, b]
void array_push_spread_all(Array* arr, Item item) {
    if (item.item == ITEM_NULL_SPREADABLE) return;
    TypeId type_id = get_type_id(item);
    if (type_id == LMD_TYPE_ARRAY) {
        Array* inner = item.array;
        if (inner) {
            for (int i = 0; i < inner->length; i++) array_push(arr, inner->items[i]);
            return;
        }
    }
    if (type_id == LMD_TYPE_ARRAY_NUM) {
        ArrayNum* inner = item.array_num;
        if (inner) {
            for (int64_t i = 0; i < inner->length; i++) array_push(arr, array_num_get(inner, i));
            return;
        }
    }
    // non-array types are pushed as single items (maps, elements, scalars, etc.)
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
    } else if (type_id == LMD_TYPE_ARRAY) {
        List* list = item.list;
        if (list) list->is_spreadable = true;
    } else if (type_id == LMD_TYPE_ARRAY_NUM) {
        ArrayNum* arr = item.array_num;
        if (arr) arr->is_spreadable = true;
    }
    // for other types, just return as-is (they will be pushed normally)
    return item;
}

Item list_fill(List *list, int count, ...) {
    va_list args;
    va_start(args, count);
    for (int i = 0; i < count; i++) {
        list_push_spread(list, {.item = va_arg(args, uint64_t)});
    }
    va_end(args);
    return list_end(list);
}

Item list_get(List *list, int64_t index) {
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

Map* map(int64_t type_index) {
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
Map* map_with_data(int64_t type_index) {
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

// MIR Direct module-type-list-aware wrapper: saves/restores context->type_list around
// map_with_data so cross-module calls use this module's own type_list.
Map* map_with_tl(int64_t type_index, void* type_list_ptr) {
    void* saved = context->type_list;
    context->type_list = type_list_ptr;
    Map* r = map_with_data(type_index);
    context->type_list = saved;
    return r;
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
    // DEBUG: trace reads of "number" field  
    if (field->name && field->name->length == 6 && strncmp(field->name->str, "number", 6) == 0) {
        int64_t raw_val = *(int64_t*)field_ptr;
        log_debug("TRACE _map_read_field: name=number type=%d raw_val=%lld field=%p entry=%p",
                  (int)type_id, (long long)raw_val, field_ptr, (void*)field);
    }
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
    case LMD_TYPE_RANGE:  case LMD_TYPE_ARRAY:  case LMD_TYPE_ARRAY_NUM:
    case LMD_TYPE_MAP:  case LMD_TYPE_ELEMENT:  case LMD_TYPE_OBJECT: {
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

Element* elmt(int64_t type_index) {
    ArrayList* type_list = (ArrayList*)context->type_list;
    TypeElmt *elmt_type = (TypeElmt*)(type_list->data[type_index]);

    if (context->ui_mode && context->arena) {
        // ui_mode: allocate fat DomElement on result arena
        DomElement* dom = (DomElement*)arena_calloc(context->arena, sizeof(DomElement));
        dom->node_type = DOM_NODE_ELEMENT;
        Element* e = dom_element_to_element(dom);
        e->type_id = LMD_TYPE_ELEMENT;
        e->type = elmt_type;
        return e;
    }

    // non-UI: allocate plain Element on GC heap
    Element *e = (Element *)heap_calloc(sizeof(Element), LMD_TYPE_ELEMENT);
    e->type_id = LMD_TYPE_ELEMENT;
    e->type = elmt_type;
    return e;
}

// MIR Direct module-type-list-aware wrapper for elmt.
Element* elmt_with_tl(int64_t type_index, void* type_list_ptr) {
    void* saved = context->type_list;
    context->type_list = type_list_ptr;
    Element* r = elmt(type_index);
    context->type_list = saved;
    return r;
}

// ui_mode helper: copy a GC-heap string into the result arena as a fat DomText node.
// Returns the new String* (embedded in [DomText][String][chars]) on the arena.
// Called by list_push() when adding a string to an element's content list in ui_mode.
Item ui_copy_string_to_arena(Arena* arena, Item str_item) {
    String* src = str_item.get_string();
    if (!src) return str_item;
    size_t total = sizeof(DomText) + sizeof(String) + src->len + 1;
    DomText* dt = (DomText*)arena_calloc(arena, total);
    dt->node_type = DOM_NODE_TEXT;
    String* dst = dom_text_to_string(dt);
    dst->len = src->len;
    dst->is_ascii = src->is_ascii;
    memcpy(dst->chars, src->chars, src->len + 1);
    return {.item = s2it(dst)};
}

// ui_mode helper: merge two strings into a new fat DomText on the result arena.
// Called by list_push() string merge path in ui_mode.
Item ui_merge_strings_to_arena(Arena* arena, String* prev, String* next) {
    size_t new_len = prev->len + next->len;
    size_t total = sizeof(DomText) + sizeof(String) + new_len + 1;
    DomText* dt = (DomText*)arena_calloc(arena, total);
    dt->node_type = DOM_NODE_TEXT;
    String* merged = dom_text_to_string(dt);
    merged->len = new_len;
    memcpy(merged->chars, prev->chars, prev->len);
    memcpy(merged->chars + prev->len, next->chars, next->len);
    merged->chars[new_len] = '\0';
    return {.item = s2it(merged)};
}

Object* object(int64_t type_index) {
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
Object* object_with_data(int64_t type_index) {
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

// MIR Direct module-type-list-aware wrapper for object_with_data.
Object* object_with_tl(int64_t type_index, void* type_list_ptr) {
    void* saved = context->type_list;
    context->type_list = type_list_ptr;
    Object* r = object_with_data(type_index);
    context->type_list = saved;
    return r;
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
        if (context->ui_mode && context->arena) {
            elmt->data = arena_calloc(context->arena, elmt_type->byte_size);
        } else {
            elmt->data = heap_data_calloc(elmt_type->byte_size);
        }
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

Item item_at(Item data, int64_t index) {
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
    case LMD_TYPE_ARRAY_NUM:
        return array_num_get(data.array_num, index);
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
        String *ch_str = heap_strcpy((char*)(chars + byte_offset), ch_len);
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
            return list_get(data.element, idx);
        }
        // ALL: attrs first, then children
        if (idx < key_count) {
            Symbol* key_sym = (Symbol*)keys->data[idx];
            return item_attr(data, key_sym->chars);
        }
        return list_get(data.element, idx - key_count);
    }
    if (type_id == LMD_TYPE_MAP || type_id == LMD_TYPE_VMAP || type_id == LMD_TYPE_OBJECT) {
        if (idx < key_count) {
            Symbol* key_sym = (Symbol*)keys->data[idx];
            return item_attr(data, key_sym->chars);
        }
        return ItemNull;
    }
    // array/list/range: use item_at
    return item_at(data, idx);
}

// ============================================================================
// Runtime typed array coercion for type annotations (int[], float[], etc.)
// Converts generic Array/List to typed array, or validates existing typed array.
// Returns a pointer to the coerced typed array, or NULL if elements are incompatible.
// ============================================================================

void* ensure_typed_array(Item item, TypeId element_type_id) {
    TypeId item_tid = get_type_id(item);

    // already the correct typed array — pass through (container types are direct pointers)
    // already the correct typed array — pass through
    if (item_tid == LMD_TYPE_ARRAY_NUM) {
        ArrayNum* arr = item.array_num;
        ArrayNumElemType et = arr->get_elem_type();
        if ((element_type_id == LMD_TYPE_INT && et == ELEM_INT) ||
            (element_type_id == LMD_TYPE_FLOAT && et == ELEM_FLOAT) ||
            (element_type_id == LMD_TYPE_INT64 && et == ELEM_INT64)) {
            return (void*)arr;
        }
    }
    if (element_type_id == LMD_TYPE_INT && item_tid == LMD_TYPE_RANGE) {
        return (void*)item.container;
    }

    // null/empty → return NULL (caller checks)
    if (item_tid == LMD_TYPE_NULL) return NULL;

    // -----------------------------------------------------------------------
    // Cross-convert between ArrayNum elem_types
    // -----------------------------------------------------------------------
    if (item_tid == LMD_TYPE_ARRAY_NUM) {
        ArrayNum* src = item.array_num;
        int64_t length = src->length;
        ArrayNumElemType src_et = src->get_elem_type();

        if (element_type_id == LMD_TYPE_INT) {
            ArrayNum* typed = array_int_new(length);
            for (int64_t i = 0; i < length; i++) {
                if (src_et == ELEM_INT || src_et == ELEM_INT64)
                    typed->items[i] = src->items[i];
                else
                    typed->items[i] = (int64_t)src->float_items[i];
            }
            return typed;
        }
        else if (element_type_id == LMD_TYPE_FLOAT) {
            ArrayNum* typed = array_float_new(length);
            for (int64_t i = 0; i < length; i++) {
                if (src_et == ELEM_FLOAT)
                    typed->float_items[i] = src->float_items[i];
                else
                    typed->float_items[i] = (double)src->items[i];
            }
            return typed;
        }
        else if (element_type_id == LMD_TYPE_INT64) {
            ArrayNum* typed = array_int64_new(length);
            for (int64_t i = 0; i < length; i++) {
                if (src_et == ELEM_INT || src_et == ELEM_INT64)
                    typed->items[i] = src->items[i];
                else
                    typed->items[i] = (int64_t)src->float_items[i];
            }
            return typed;
        }
    }

    // convert generic Array/List to typed array (Array and List are the same struct)
    if (item_tid == LMD_TYPE_ARRAY || item_tid == LMD_TYPE_ARRAY) {
        Array* arr = item.array;
        Item* items = arr->items;
        int64_t length = arr->length;

        if (element_type_id == LMD_TYPE_INT) {
            ArrayNum* typed = array_int_new(length);
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
            ArrayNum* typed = array_float_new(length);
            for (int64_t i = 0; i < length; i++) {
                TypeId elem_tid = get_type_id(items[i]);
                if (elem_tid != LMD_TYPE_FLOAT && elem_tid != LMD_TYPE_INT &&
                    elem_tid != LMD_TYPE_INT64 && elem_tid != LMD_TYPE_DECIMAL &&
                    elem_tid != LMD_TYPE_BOOL) {
                    log_error("ensure_typed_array: element %lld has type %s, expected float", i, get_type_name(elem_tid));
                    return NULL;
                }
                if (elem_tid == LMD_TYPE_BOOL)
                    typed->float_items[i] = items[i].bool_val ? 1.0 : 0.0;
                else
                    typed->float_items[i] = it2d(items[i]);
            }
            return typed;
        }
        else if (element_type_id == LMD_TYPE_INT64) {
            ArrayNum* typed = array_int64_new(length);
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

// Runtime coercion for compact sized typed arrays (u8[], i16[], f32[], etc.)
// Converts generic Array/List or ARRAY_NUM → compact sized ARRAY_NUM.
void* ensure_sized_array(Item item, int64_t elem_type_int) {
    ArrayNumElemType target_et = (ArrayNumElemType)(int)elem_type_int;
    TypeId item_tid = get_type_id(item);

    // already the correct compact typed array — pass through
    if (item_tid == LMD_TYPE_ARRAY_NUM) {
        ArrayNum* arr = item.array_num;
        if (arr->get_elem_type() == target_et) {
            return (void*)arr;
        }
        // cross-convert from different ARRAY_NUM elem_type
        int64_t length = arr->length;
        ArrayNum* typed = array_num_new(target_et, length);
        for (int64_t i = 0; i < length; i++) {
            Item val = array_num_get(arr, i);
            array_num_set_item(typed, i, val);
        }
        return typed;
    }

    if (item_tid == LMD_TYPE_NULL) return NULL;

    // convert generic Array/List to compact sized array
    if (item_tid == LMD_TYPE_ARRAY) {
        Array* arr = item.array;
        int64_t length = arr->length;
        ArrayNum* typed = array_num_new(target_et, length);
        for (int64_t i = 0; i < length; i++) {
            array_num_set_item(typed, i, arr->items[i]);
        }
        return typed;
    }

    log_error("ensure_sized_array: cannot coerce %s to compact typed array", get_type_name(item_tid));
    return NULL;
}
