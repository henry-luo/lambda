#include "lambda.hpp"
#include "transpiler.hpp"

static volatile uint64_t scalar_heap_rehome_counts[3];

static int scalar_heap_rehome_type_index(TypeId type_id) {
    switch (type_id) {
    case LMD_TYPE_INT64: return 0;
    case LMD_TYPE_UINT64: return 1;
    case LMD_TYPE_FLOAT:
    case LMD_TYPE_FLOAT64: return 2;
    default: return -1;
    }
}

static void scalar_heap_rehome_note(TypeId type_id) {
    int index = scalar_heap_rehome_type_index(type_id);
    if (index >= 0) {
        // This boundary has no destination-owned scalar slot, so the copy is
        // deliberately observable until that ABI can donate a retired home.
        __atomic_fetch_add(&scalar_heap_rehome_counts[index], 1,
            __ATOMIC_RELAXED);
    }
}

extern "C" uint64_t lambda_scalar_heap_rehome_count(TypeId type_id) {
    int index = scalar_heap_rehome_type_index(type_id);
    return index < 0 ? 0 : __atomic_load_n(&scalar_heap_rehome_counts[index],
        __ATOMIC_RELAXED);
}

extern "C" Item lambda_item_heap_rehome(Item item) {
    switch (get_type_id(item)) {
    case LMD_TYPE_INT64: {
        int64_t* value = (int64_t*)heap_alloc(sizeof(int64_t), LMD_TYPE_INT64);
        if (!value) return ItemError;
        *value = item.get_int64();
        scalar_heap_rehome_note(LMD_TYPE_INT64);
        return {.item = l2it(value)};
    }
    case LMD_TYPE_UINT64: {
        uint64_t* value = (uint64_t*)heap_alloc(sizeof(uint64_t), LMD_TYPE_UINT64);
        if (!value) return ItemError;
        *value = item.get_uint64();
        scalar_heap_rehome_note(LMD_TYPE_UINT64);
        return {.item = u2it(value)};
    }
    case LMD_TYPE_FLOAT:
    case LMD_TYPE_FLOAT64: {
        if ((item.item & ITEM_DBL_MASK) || item.item == ITEM_FLOAT_P0 ||
                item.item == ITEM_FLOAT_N0) return item;
        double* value = (double*)heap_alloc(sizeof(double), LMD_TYPE_FLOAT);
        if (!value) return ItemError;
        *value = item.get_double();
        scalar_heap_rehome_note(LMD_TYPE_FLOAT);
        return {.item = d2it(value)};
    }
    default:
        return item;
    }
}
