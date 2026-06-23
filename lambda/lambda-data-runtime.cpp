
#include "transpiler.hpp"
#include "../lib/log.h"
#include "../lib/str.h"
#include "../lib/arraylist.hpp"
#include "../lib/checked_math.hpp"
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

struct LambdaSymbolKeyList {
    lam::ArrayList<Symbol*> keys;

    explicit LambdaSymbolKeyList(size_t initial_capacity)
        : keys(MEM_CAT_CONTAINER, initial_capacity) {
    }
};

extern "C" SymbolKeyList* symbol_key_list_new(int64_t initial_capacity) {
    if (initial_capacity < 0) initial_capacity = 0;
    void* raw = mem_alloc(sizeof(LambdaSymbolKeyList), MEM_CAT_CONTAINER);
    if (!raw) return nullptr;
    return new (raw) LambdaSymbolKeyList((size_t)initial_capacity); // NEW_DELETE_OK: single audited boundary for LambdaSymbolKeyList construction inside symbol_key_list_new factory.
}

extern "C" bool symbol_key_list_append(SymbolKeyList* keys_ptr, Symbol* symbol) {
    if (!keys_ptr || !symbol) return false;
    LambdaSymbolKeyList* keys = (LambdaSymbolKeyList*)keys_ptr;
    return keys->keys.append(symbol);
}

extern "C" int64_t symbol_key_list_len(void* keys_ptr) {
    if (!keys_ptr) return 0;
    LambdaSymbolKeyList* keys = (LambdaSymbolKeyList*)keys_ptr;
    return (int64_t)keys->keys.size();
}

extern "C" Symbol* symbol_key_list_at(void* keys_ptr, int64_t index) {
    if (!keys_ptr || index < 0) return nullptr;
    LambdaSymbolKeyList* keys = (LambdaSymbolKeyList*)keys_ptr;
    size_t key_index = (size_t)index;
    Symbol** symbol = keys->keys.try_get(key_index);
    return symbol ? *symbol : nullptr;
}

extern "C" void symbol_key_list_free(void* keys_ptr) {
    if (!keys_ptr) return;
    LambdaSymbolKeyList* keys = (LambdaSymbolKeyList*)keys_ptr;
    keys->~LambdaSymbolKeyList();
    mem_free(keys);
}

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
        size_t bytes;
        if (lam::checked_mul((size_t)count, sizeof(Item), &bytes)) {
            arr->items = (Item*)heap_data_alloc(bytes);
        }
        if (arr->items) {                     // skip on overflow/OOM — array stays empty
            arr->capacity = count;
            va_list args;
            va_start(args, count);
            for (int i = 0; i < count; i++) {
                array_push(arr, va_arg(args, Item));
            }
            va_end(args);
        }
    }
    log_item({.array = arr}, "array_filled");
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
    if (!arr) return NULL;
    arr->type_id = LMD_TYPE_ARRAY_NUM;
    arr->set_elem_type(elem_type);  // stored in map_kind byte (byte 2)
    int elem_size = ELEM_TYPE_SIZE[elem_type >> 4];
    size_t bytes;
    if (length > 0 && lam::checked_mul((size_t)length, (size_t)elem_size, &bytes)) {
        arr->data = heap_data_alloc(bytes);
    }
    if (arr->data) {                          // length/capacity stay 0 on overflow/OOM
        arr->length = length;  arr->capacity = length;
    }
    return arr;
}

// Allocate an N-D ArrayNum: data buffer sized for `total` elements, plus a
// shape side-table with C-contiguous strides computed from `dims[0..ndim-1]`.
// Used by both the C transpiler and the MIR transpiler to materialize nested
// numeric literals like [[1,2],[3,4]] as a single tensor.
ArrayNum* array_num_new_ndim(ArrayNumElemType elem_type, int64_t total, int ndim, int64_t* dims) {
    if (ndim < 1 || ndim > 32) return NULL;
    ArrayNum* arr = array_num_new(elem_type, total);
    if (!arr) return NULL;
    size_t shape_bytes = sizeof(ArrayNumShape) + 2 * (size_t)ndim * sizeof(int64_t);
    ArrayNumShape* s = (ArrayNumShape*)heap_data_calloc(shape_bytes);
    if (!s) return arr;  // best-effort: keep as 1-D
    s->ndim = (uint8_t)ndim;
    s->is_c_contig = 1;
    s->is_f_contig = (ndim == 1) ? 1 : 0;
    s->offset = 0;
    s->base = NULL;  // owned
    int64_t* sd = array_num_shape_dims(s);
    int64_t* ss = array_num_shape_strides(s);
    int64_t stride = 1;
    for (int i = ndim - 1; i >= 0; i--) {
        sd[i] = dims[i];
        ss[i] = stride;
        stride *= dims[i];
    }
    arr->is_ndim = 1;
    arr->extra = (int64_t)(uintptr_t)s;
    return arr;
}

// Returns the iteration count for for-in / index loops:
//   - 1-D / non-ndim arrays: total element count (length)
//   - N-D arrays: shape[0] (leading axis), so for-in yields leading-axis slices
int64_t array_num_iter_count(ArrayNum* arr) {
    if (!arr) return 0;
    if (arr->is_ndim && arr->extra) {
        ArrayNumShape* s = (ArrayNumShape*)(uintptr_t)arr->extra;
        if (s && s->ndim >= 1) return array_num_shape_dims(s)[0];
    }
    return arr->length;
}

// Build a leading-axis view of an N-D ArrayNum at `row_idx`.
// Result is ndim-1 dimensional; for ndim==2 it's 1-D, for ndim==3 it's 2-D, etc.
// The row view aliases the parent's data; its data pointer is adjusted so
// element 0 of the row is element row_idx along axis 0 of the parent.
static Item make_leading_axis_view(ArrayNum* parent, int64_t row_idx) {
    ArrayNumShape* pshape = (ArrayNumShape*)(uintptr_t)parent->extra;
    if (!pshape || pshape->ndim < 2) return ItemNull;
    int64_t* pdims = array_num_shape_dims(pshape);
    int64_t* pstrs = array_num_shape_strides(pshape);
    if (row_idx < 0 || row_idx >= pdims[0]) return ItemNull;

    int new_ndim = pshape->ndim - 1;
    int64_t row_len = 1;
    for (int i = 1; i < pshape->ndim; i++) row_len *= pdims[i];

    ArrayNumElemType etype = parent->get_elem_type();
    int elem_size = ELEM_TYPE_SIZE[etype >> 4];

    ArrayNum* view = (ArrayNum*)heap_calloc(sizeof(ArrayNum), LMD_TYPE_ARRAY_NUM);
    if (!view) return ItemNull;
    view->type_id = LMD_TYPE_ARRAY_NUM;
    view->set_elem_type(etype);
    view->is_ndim = 1;
    view->is_view = 1;
    int64_t base_elem_offset = row_idx * pstrs[0];
    view->data = (void*)((char*)parent->data + base_elem_offset * (size_t)elem_size);
    view->length = row_len;
    view->capacity = row_len;

    // shape side-table for the row view
    size_t shape_bytes = sizeof(ArrayNumShape) + 2 * (size_t)new_ndim * sizeof(int64_t);
    ArrayNumShape* rshape = (ArrayNumShape*)heap_data_calloc(shape_bytes);
    if (!rshape) return ItemNull;
    rshape->ndim = (uint8_t)new_ndim;
    rshape->is_c_contig = pshape->is_c_contig;  // inherits contig if parent was
    rshape->is_f_contig = 0;
    rshape->offset = base_elem_offset;
    // base ref: prefer the parent's own base (if it's itself a view), else the parent.
    // This keeps the original data owner alive across nested views.
    rshape->base = pshape->base ? pshape->base : (void*)parent;
    int64_t* rdims = array_num_shape_dims(rshape);
    int64_t* rstrs = array_num_shape_strides(rshape);
    for (int i = 0; i < new_ndim; i++) {
        rdims[i] = pdims[i + 1];
        rstrs[i] = pstrs[i + 1];
    }
    view->extra = (int64_t)(uintptr_t)rshape;

    // pin the actual data owner
    Container* owner = (Container*)rshape->base;
    if (owner) owner->is_pinned = 1;
    return { .array_num = view };
}

// Unified ArrayNum getter — dispatches on elem_type
Item array_num_get(ArrayNum *array, int64_t index) {
    if (!array || ((uintptr_t)array >> 56)) { return ItemNull; }
    if (array->type_id != LMD_TYPE_ARRAY_NUM)
        return array_get((Array*)array, index);

    // N-D arrays: return a leading-axis view instead of a scalar element
    if (array->is_ndim && array->extra) {
        ArrayNumShape* s = (ArrayNumShape*)(uintptr_t)array->extra;
        if (s && s->ndim >= 2) {
            return make_leading_axis_view(array, index);
        }
        // ndim==1: continue to scalar dispatch below, using shape[0] for bounds
    }

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
    case ELEM_BOOL:
        return (Item){.item = b2it(((uint8_t*)array->data)[index] ? BOOL_TRUE : BOOL_FALSE)};
    default:
        return ItemNull;
    }
}

ArrayNum* array_int() {
    ArrayNum *arr = (ArrayNum*)heap_calloc(sizeof(ArrayNum), LMD_TYPE_ARRAY_NUM);
    arr->type_id = LMD_TYPE_ARRAY_NUM;
    arr->set_elem_type(ELEM_INT);
    return arr;
}

// used when there's no interleaving with transpiled code
ArrayNum* array_int_new(int64_t length) {
    return array_num_new(ELEM_INT, length);
}

ArrayNum* array_int_fill(ArrayNum *arr, int count, ...) {
    if (count > 0) {
        size_t bytes;
        if (lam::checked_mul((size_t)count, sizeof(int64_t), &bytes)) {
            arr->items = (int64_t*)heap_data_alloc(bytes);
        }
        if (arr->items) {                     // skip on overflow/OOM — array stays empty
            arr->length = count;  arr->capacity = count;
            va_list args;
            va_start(args, count);
            for (int i = 0; i < count; i++) {
                arr->items[i] = va_arg(args, int64_t);
            }
            va_end(args);
        }
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
    arr->set_elem_type(ELEM_INT64);
    return arr;
}

// used when there's no interleaving with transpiled code
ArrayNum* array_int64_new(int64_t length) {
    return array_num_new(ELEM_INT64, length);
}

ArrayNum* array_int64_fill(ArrayNum *arr, int count, ...) {
    if (count > 0) {
        size_t bytes;
        if (lam::checked_mul((size_t)count, sizeof(int64_t), &bytes)) {
            arr->items = (int64_t*)heap_data_alloc(bytes);
        }
        if (arr->items) {                     // skip on overflow/OOM — array stays empty
            arr->length = count;  arr->capacity = count;
            va_list args;
            va_start(args, count);
            for (int i = 0; i < count; i++) {
                arr->items[i] = va_arg(args, int64_t);
            }
            va_end(args);
        }
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
    arr->set_elem_type(ELEM_FLOAT);
    return arr;
}

// used when there's no interleaving with transpiled code
ArrayNum* array_float_new(int64_t length) {
    return array_num_new(ELEM_FLOAT, length);
}

ArrayNum* array_float_fill(ArrayNum *arr, int count, ...) {
    if (count > 0) {
        arr->type_id = LMD_TYPE_ARRAY_NUM;
        arr->set_elem_type(ELEM_FLOAT);
        size_t bytes;
        if (lam::checked_mul((size_t)count, sizeof(double), &bytes)) {
            arr->float_items = (double*)heap_data_alloc(bytes);
        }
        if (arr->float_items) {               // skip on overflow/OOM — array stays empty
            arr->length = count;  arr->capacity = count;
            va_list args;
            va_start(args, count);
            for (int i = 0; i < count; i++) {
                arr->float_items[i] = va_arg(args, double);
            }
            va_end(args);
        }
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
    if (arr->is_view) {
        log_error("array_float_set: cannot mutate a view; copy() first");
        return;
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
    if (arr->is_view) {
        log_error("array_int_set: cannot mutate a view; copy() first");
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
    if (arr->is_view) {
        log_error("array_float_set_item: cannot mutate a view; copy() first");
        return;
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
    if (arr->is_view) {
        log_error("array_num_set_item: cannot mutate a view; copy() first");
        return;
    }
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
    case ELEM_BOOL: {
        TypeId vt = get_type_id(value);
        uint8_t b;
        if (vt == LMD_TYPE_BOOL) {
            b = (value.bool_val == BOOL_TRUE) ? 1 : 0;
        } else if (vt == LMD_TYPE_INT || vt == LMD_TYPE_INT64) {
            b = item_to_int_value(value) ? 1 : 0;
        } else if (vt == LMD_TYPE_FLOAT) {
            b = item_to_float_value(value) != 0.0 ? 1 : 0;
        } else {
            b = 0;
        }
        ((uint8_t*)arr->data)[index] = b;
        break;
    }
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
        log_item({.array = list}, "elmt_end");
        return {.array = list};
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
            return {.array = list};
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

// Dynamic N-D promotion: when every element of `arr` is an ArrayNum of the
// same elem_type and length, promote the whole structure to a single N-D
// ArrayNum.  Returns the new ArrayNum on success, or NULL if promotion isn't
// applicable (heterogeneous children, jagged, mixed types, empty, etc.).
//
// Used by array_end to catch nested-literal cases the static AST detector
// missed (let-bound rows, function-returned rows, etc.).
// Determine whether the child item is a "row-like" numeric sequence (ArrayNum
// or generic Array of homogeneous numerics) and extract its length / etype.
// Returns true on success with rt_length set; etype defaults to LMD_TYPE_INT
// (caller may widen later when scanning siblings).
static bool row_summary(Item it, ArrayNumElemType* etype_out, int64_t* len_out, bool* is_arr_num) {
    TypeId tid = get_type_id(it);
    if (tid == LMD_TYPE_ARRAY_NUM) {
        ArrayNum* a = it.array_num;
        if (!a || a->is_view) return false;
        *etype_out = a->get_elem_type();
        *len_out = a->length;
        *is_arr_num = true;
        return true;
    }
    if (tid == LMD_TYPE_ARRAY) {
        Array* a = it.array;
        if (!a || a->is_spreadable || a->is_content) return false;
        if (a->length == 0) return false;
        // Scan items: all must be numeric; if any is float, etype = ELEM_FLOAT, else ELEM_INT64
        bool any_float = false;
        for (int64_t i = 0; i < a->length; i++) {
            TypeId it_tid = get_type_id(a->items[i]);
            if (it_tid == LMD_TYPE_FLOAT) any_float = true;
            else if (it_tid != LMD_TYPE_INT && it_tid != LMD_TYPE_INT64) return false;
        }
        *etype_out = any_float ? ELEM_FLOAT : ELEM_INT64;
        *len_out = a->length;
        *is_arr_num = false;
        return true;
    }
    return false;
}

// Write one element of the source row to the promoted N-D ArrayNum's data buffer
// at position flat_idx.  Handles both ArrayNum source (typed read) and generic
// Array source (Item-unboxed read).
static void write_row_into_ndim(ArrayNum* dst, ArrayNumElemType etype, int64_t flat_idx,
                                 Item src_item) {
    TypeId tid = get_type_id(src_item);
    if (tid == LMD_TYPE_ARRAY_NUM) {
        ArrayNum* src = src_item.array_num;
        int elem_size = ELEM_TYPE_SIZE[etype >> 4];
        memcpy((char*)dst->data + flat_idx * elem_size, src->data, (size_t)src->length * elem_size);
    } else if (tid == LMD_TYPE_ARRAY) {
        Array* src = src_item.array;
        for (int64_t j = 0; j < src->length; j++) {
            array_num_set_item(dst, flat_idx + j, src->items[j]);
        }
    }
}

static ArrayNum* try_promote_to_ndim(Array* arr) {
    if (!arr || arr->length < 2) return NULL;
    if (arr->is_spreadable || arr->is_content) return NULL;

    // First child sets the shape/etype; subsequent must match
    ArrayNumElemType etype;
    int64_t inner_len;
    bool first_is_arr_num;
    if (!row_summary(arr->items[0], &etype, &inner_len, &first_is_arr_num)) return NULL;
    if (inner_len == 0) return NULL;

    // For now we only fold 1-D rows.  If the first row is N-D ArrayNum we require
    // all rows to share the same multi-dim shape; this catches the common
    // [tensor1, tensor2] → (N+1)-D case but stays conservative.
    int64_t shape_stack[32];
    int out_ndim;
    shape_stack[0] = arr->length;

    if (first_is_arr_num && arr->items[0].array_num->is_ndim && arr->items[0].array_num->extra) {
        ArrayNum* fa = arr->items[0].array_num;
        ArrayNumShape* fs = (ArrayNumShape*)(uintptr_t)fa->extra;
        if (!fs || fs->ndim < 1 || fs->ndim > 30) return NULL;
        int64_t* fd = array_num_shape_dims(fs);
        for (int i = 0; i < fs->ndim; i++) shape_stack[1 + i] = fd[i];
        out_ndim = 1 + fs->ndim;
        // Verify all siblings match shape and elem_type
        for (int64_t i = 1; i < arr->length; i++) {
            ArrayNumElemType e2; int64_t l2; bool an2;
            if (!row_summary(arr->items[i], &e2, &l2, &an2)) return NULL;
            if (e2 != etype) return NULL;
            if (!an2) return NULL;  // need full N-D match, generic Array can't carry N-D
            ArrayNum* sa = arr->items[i].array_num;
            if (!sa->is_ndim || !sa->extra) return NULL;
            ArrayNumShape* ss = (ArrayNumShape*)(uintptr_t)sa->extra;
            if (!ss || ss->ndim != fs->ndim) return NULL;
            int64_t* sd = array_num_shape_dims(ss);
            for (int j = 0; j < fs->ndim; j++) if (sd[j] != fd[j]) return NULL;
        }
    } else {
        // 1-D rows: each sibling must be the same length, same (or compatible) elem_type
        shape_stack[1] = inner_len;
        out_ndim = 2;
        bool any_float = (etype == ELEM_FLOAT || etype == ELEM_FLOAT64);
        for (int64_t i = 1; i < arr->length; i++) {
            ArrayNumElemType e2; int64_t l2; bool an2;
            if (!row_summary(arr->items[i], &e2, &l2, &an2)) return NULL;
            if (l2 != inner_len) return NULL;
            // widen to float if any row is float
            if (e2 == ELEM_FLOAT || e2 == ELEM_FLOAT64) any_float = true;
            else if (e2 != etype && e2 != ELEM_INT && e2 != ELEM_INT64) {
                // refuse exotic compact mixes for simplicity
                return NULL;
            }
        }
        if (any_float) etype = ELEM_FLOAT;
        else if (etype == ELEM_INT) etype = ELEM_INT64;  // standardize int promotion to INT64 for storage
    }

    // Allocate promoted N-D ArrayNum
    int64_t total = 1;
    for (int i = 0; i < out_ndim; i++) total *= shape_stack[i];
    ArrayNum* promoted = array_num_new_ndim(etype, total, out_ndim, shape_stack);
    if (!promoted) return NULL;

    // Fill: each row writes inner_len elements at offset i*inner_len
    int64_t flat_idx = 0;
    for (int64_t i = 0; i < arr->length; i++) {
        write_row_into_ndim(promoted, etype, flat_idx, arr->items[i]);
        flat_idx += inner_len;
    }
    return promoted;
}

// finalize spreadable array - returns array as Item (no flattening)
// returns spreadable null for empty arrays so they can be skipped when spreading
Item array_end(Array* arr) {
    if (arr->length == 0) {
        // return spreadable null - will be skipped when added to collections
        return {.item = ITEM_NULL_SPREADABLE};
    }
    // Dynamic N-D promotion: when children are uniform ArrayNums, fold into a tensor
    ArrayNum* nd = try_promote_to_ndim(arr);
    if (nd) return {.array_num = nd};
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
        List* inner = item.array;
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
        List* list = item.array;
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
    va_start(args, map);
    set_fields(map_type, map->data, args);
    va_end(args);
    return map;
}

// extract field value from a named shape entry's storage
Item _map_read_field(ShapeEntry* field, void* map_data) {
    TypeId type_id = field->type->type_id;
    void* field_ptr = (char*)map_data + field->byte_offset;
    void* ptr_val = nullptr;
    switch (type_id) {
    case LMD_TYPE_NULL: {
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
        memcpy(&ptr_val, field_ptr, sizeof(void*));
        return {.item = c2it(ptr_val)};
    case LMD_TYPE_STRING:
        memcpy(&ptr_val, field_ptr, sizeof(void*));
        return {.item = s2it(ptr_val)};
    case LMD_TYPE_SYMBOL:
        memcpy(&ptr_val, field_ptr, sizeof(void*));
        return {.item = y2it(ptr_val)};
    case LMD_TYPE_BINARY:
        memcpy(&ptr_val, field_ptr, sizeof(void*));
        return {.item = x2it(ptr_val)};
    case LMD_TYPE_RANGE:  case LMD_TYPE_ARRAY:  case LMD_TYPE_ARRAY_NUM:
    case LMD_TYPE_MAP:  case LMD_TYPE_VMAP:
    case LMD_TYPE_ELEMENT:  case LMD_TYPE_OBJECT: {
        memcpy(&ptr_val, field_ptr, sizeof(void*));
        if (((uintptr_t)ptr_val >> 56) != 0) return ItemNull;
        Container* container = (Container*)ptr_val;
        if (!container) return ItemNull;
        if (container->type_id == LMD_TYPE_RAW_POINTER) {
            container->type_id = type_id;
        }
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
Item _map_get(TypeMap* map_type, void* map_data, const char *key, bool *is_found) {
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
    String* src = str_item.get_safe_string();
    if (!src) return str_item;
    size_t total = sizeof(DomText) + sizeof(String) + src->len + 1;
    DomText* dt = (DomText*)arena_calloc(arena, total);
    dt->node_type = DOM_NODE_TEXT;
    dt->content_type = DOM_TEXT_STRING;
    String* dst = dom_text_to_string(dt);
    dst->len = src->len;
    dst->is_ascii = src->is_ascii;
    memcpy(dst->chars, src->chars, src->len + 1);
    dt->native_string = dst;
    dt->text = dst->chars;
    dt->length = dst->len;
    return {.item = s2it(dst)};
}

// ui_mode helper: merge two strings into a new fat DomText on the result arena.
// Called by list_push() string merge path in ui_mode.
Item ui_merge_strings_to_arena(Arena* arena, String* prev, String* next) {
    size_t new_len = prev->len + next->len;
    size_t total = sizeof(DomText) + sizeof(String) + new_len + 1;
    DomText* dt = (DomText*)arena_calloc(arena, total);
    dt->node_type = DOM_NODE_TEXT;
    dt->content_type = DOM_TEXT_STRING;
    String* merged = dom_text_to_string(dt);
    merged->len = new_len;
    merged->is_ascii = prev->is_ascii && next->is_ascii;
    memcpy(merged->chars, prev->chars, prev->len);
    memcpy(merged->chars + prev->len, next->chars, next->len);
    merged->chars[new_len] = '\0';
    dt->native_string = merged;
    dt->text = merged->chars;
    dt->length = merged->len;
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
    va_start(args, obj);
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
    va_list args;
    va_start(args, elmt);
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
        bool is_ascii = true;
        if (type_id == LMD_TYPE_STRING) {
            String* str = data.get_safe_string();
            if (!str) { return ItemNull; }
            is_ascii = str->is_ascii != 0;
        }

        // ASCII fast-path: byte index == char index, O(1)
        if (is_ascii) {
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
SymbolKeyList* item_keys(Item data) {
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
        SymbolKeyList* keys = symbol_key_list_new(8);
        ShapeEntry* field = map_type->shape;
        while (field) {
            if (field->name) {
                // Convert StrView to Symbol for the transpiled code
                StrView* sv = field->name;
                Symbol* sym = heap_create_symbol(sv->str, sv->length);
                symbol_key_list_append(keys, sym);
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
        SymbolKeyList* keys = symbol_key_list_new(8);
        ShapeEntry* field = elmt_type->shape;
        while (field) {
            if (field->name) {
                // Convert StrView to Symbol for the transpiled code
                StrView* sv = field->name;
                Symbol* sym = heap_create_symbol(sv->str, sv->length);
                symbol_key_list_append(keys, sym);
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
    if (type_id == LMD_TYPE_ERROR || type_id == LMD_TYPE_NULL) return 0;
    int64_t key_count = symbol_key_list_len(keys_ptr);

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
    int64_t key_count = symbol_key_list_len(keys_ptr);

    if (type_id == LMD_TYPE_ELEMENT) {
        if (key_filter == 2) {
            // SYMBOL: attrs only
            if (idx < key_count) {
                Symbol* key_sym = symbol_key_list_at(keys_ptr, idx);
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
            Symbol* key_sym = symbol_key_list_at(keys_ptr, idx);
            return {.item = y2it(key_sym)};
        }
        return {.item = i2it(idx - key_count)};
    }
    if (type_id == LMD_TYPE_MAP || type_id == LMD_TYPE_VMAP || type_id == LMD_TYPE_OBJECT) {
        if (idx < key_count) {
            Symbol* key_sym = symbol_key_list_at(keys_ptr, idx);
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
    int64_t key_count = symbol_key_list_len(keys_ptr);

    if (type_id == LMD_TYPE_ELEMENT) {
        if (key_filter == 2) {
            // SYMBOL: attrs only
            if (idx < key_count) {
                Symbol* key_sym = symbol_key_list_at(keys_ptr, idx);
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
            Symbol* key_sym = symbol_key_list_at(keys_ptr, idx);
            return item_attr(data, key_sym->chars);
        }
        return list_get(data.element, idx - key_count);
    }
    if (type_id == LMD_TYPE_MAP || type_id == LMD_TYPE_VMAP || type_id == LMD_TYPE_OBJECT) {
        if (idx < key_count) {
            Symbol* key_sym = symbol_key_list_at(keys_ptr, idx);
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
    if (item_tid == LMD_TYPE_ARRAY) {
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
