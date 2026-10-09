#include "mvp_lmd_runtime.h"
#include "../../runtime/lambda-root-frame.hpp"
#include "../../runtime/runtime-state.h"
#include "../../../lib/sort.h"
#include "../../../lib/mem.h"
#include <math.h>

struct MvpLmdSort {
    Rooted<Item>* comparator;
    Rooted<Item>* values;
    Rooted<Item>* error;
};
static int array_sort_compare(const void* left, const void* right, void* opaque) {
    MvpLmdSort* sort = (MvpLmdSort*)opaque;
    int64_t a = *(const int64_t*)left, b = *(const int64_t*)right;
    int order = 0;
    if (!item_is_error(sort->error->get())) {
        Item arguments[] = {sort->values->get().array->items[a], sort->values->get().array->items[b]};
        if (sort->comparator->get().item == ITEM_JS_UNDEFINED) {
            RootFrame roots(2);
            if (!roots.valid()) { sort->error->set(ItemError); return 0; }
            Rooted<Item> x(roots, mvp_lmd_primitive_to_string(arguments[0]));
            if (item_is_error(x.get())) { sort->error->set(x.get()); return 0; }
            Rooted<Item> y(roots, mvp_lmd_primitive_to_string(sort->values->get().array->items[b]));
            if (item_is_error(y.get())) { sort->error->set(y.get()); return 0; }
            order = (int)mvp_lmd_string_compare(x.get(), y.get());
        } else {
            RootSpan roots(2);
            if (!roots.valid()) { sort->error->set(ItemError); return 0; }
            // callbacks can collect or grow arrays that own the borrowed numeric tails.
            uint64_t homes[2] = {};
            for (int i = 0; i < 2; i++)
                roots.items()[i] = lambda_item_adopt_scalar_home(arguments[i], &homes[i]);
            Item result = mvp_lmd_class_invoke(sort->comparator->get(),
                Item{.item = ITEM_JS_UNDEFINED}, roots.items(), 2, Item{.item = ITEM_JS_UNDEFINED});
            result = lambda_item_resolve_pending_slot(result);
            if (item_is_error(result)) { sort->error->set(result); return 0; }
            TypeId type = get_type_id(result);
            double number = type == LMD_TYPE_STRING ? mvp_lmd_string_to_number(result.get_string()) :
                type == LMD_TYPE_NULL ? 0 : type == LMD_TYPE_UNDEFINED ? NAN :
                type == LMD_TYPE_BOOL ? (double)(result.item & 1) :
                type == LMD_TYPE_INT || type == LMD_TYPE_FLOAT ? it2d(result) : NAN;
            if (type != LMD_TYPE_STRING && type != LMD_TYPE_NULL && type != LMD_TYPE_UNDEFINED &&
                    type != LMD_TYPE_BOOL && type != LMD_TYPE_INT && type != LMD_TYPE_FLOAT)
                sort->error->set(mvp_lmd_fail(LMD_MVP_CAPABILITY, 0));
            order = (number > 0) - (number < 0);
        }
    }
    // sorting the permutation by (comparison, original ordinal) makes shared heapsort stable.
    return order ? order : (a > b) - (a < b);
}
extern "C" Item mvp_lmd_array_sort(Item owner, Item comparator) {
    if (get_type_id(owner) != LMD_TYPE_ARRAY ||
            (comparator.item != ITEM_JS_UNDEFINED && get_type_id(comparator) != LMD_TYPE_FUNC))
        return mvp_lmd_fail(LMD_MVP_TYPE, 0);
    RootFrame roots(4);
    if (!roots.valid()) return ItemError;
    Rooted<Item> target(roots, owner), fn(roots, comparator), values(roots, mvp_lmd_array_new(0)), error(roots, ItemNull);
    if (item_is_error(values.get())) return values.get();
    int64_t length = target.get().array->length, undefined = 0;
    for (int64_t i = 0; i < length; i++) {
        Item value = target.get().array->items[i];
        if (value.item == ITEM_JS_DELETED_SENTINEL) continue;
        if (value.item == ITEM_JS_UNDEFINED) { undefined++; continue; }
        Item stored = mvp_lmd_array_store(values.get(), values.get().array->length, value);
        if (item_is_error(stored)) return stored;
    }
    int64_t count = values.get().array->length;
    int64_t* indices = (int64_t*)mem_alloc((size_t)(count ? count : 1) * sizeof(int64_t), MEM_CAT_JS_RUNTIME);
    if (!indices) return mvp_lmd_fail(LMD_MVP_MEMORY, 0);
    for (int64_t i = 0; i < count; i++) indices[i] = i;
    MvpLmdSort sort = {&fn, &values, &error};
    sort_qsort_r(indices, count, sizeof(int64_t), array_sort_compare, &sort);
    if (!item_is_error(error.get())) for (int64_t i = 0; i < count + undefined; i++) {
        Item value = i < count ? values.get().array->items[indices[i]] : Item{.item = ITEM_JS_UNDEFINED};
        Item result = mvp_lmd_array_store(target.get(), i, value);
        if (item_is_error(result)) { error.set(result); break; }
    }
    mem_free(indices);
    if (item_is_error(error.get())) return error.get();
    // callback mutations beyond the snapshotted length survive the final write/delete pass.
    for (int64_t i = count + undefined; i < length && i < target.get().array->length; i++)
        target.get().array->items[i].item = ITEM_JS_DELETED_SENTINEL;
    return target.get();
}
extern "C" Item mvp_lmd_array_spread(Item target, Item source, int64_t projection) {
    TypeId type = get_type_id(source);
    if (type != LMD_TYPE_ARRAY && (type != LMD_TYPE_MAP || source.map->map_kind != MAP_KIND_ORDERED))
        return mvp_lmd_fail(LMD_MVP_CAPABILITY, 0);
    RootFrame roots(3);
    if (!roots.valid()) return ItemError;
    Rooted<Item> output(roots, target), input(roots, source), element(roots, ItemNull);
    for (int64_t cursor = 0;; cursor++) {
        if (type == LMD_TYPE_ARRAY) {
            if (cursor >= input.get().array->length) break;
            Item value = input.get().array->items[cursor];
            element.set(value.item == ITEM_JS_DELETED_SENTINEL ? Item{.item = ITEM_JS_UNDEFINED} : value);
        } else {
            cursor = mvp_lmd_map_next(input.get(), cursor);
            if (cursor < 0) break;
            element.set(mvp_lmd_map_entry(input.get(), cursor, projection));
            // ordered entries occupy four slots; map_next expects a raw entry offset.
            cursor += 3;
        }
        if (item_is_error(element.get())) return element.get();
        if (output.get().array->length >= UINT32_MAX) return mvp_lmd_fail(LMD_MVP_RANGE, 0);
        Item result = mvp_lmd_array_store(output.get(), output.get().array->length, element.get());
        if (item_is_error(result)) return result;
    }
    return output.get();
}
