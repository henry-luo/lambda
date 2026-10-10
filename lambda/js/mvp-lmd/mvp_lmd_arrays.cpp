#include "mvp_lmd_runtime.h"
#include "../../runtime/lambda-root-frame.hpp"
#include "../../runtime/runtime-state.h"
#include "../../runtime/heap_api.h"
#include "../../../lib/sort.h"
#include "../../../lib/mem.h"
#include "../../../lib/utf.h"
#include <math.h>

struct MvpLmdSort {
    Rooted<Item>* comparator;
    Rooted<Item>* values;
    Rooted<Item>* error;
};
extern "C" Item mvp_lmd_array_visit(Item owner, Item* args, int64_t count, int64_t method) {
    if (owner.item == ITEM_NULL || owner.item == ITEM_JS_UNDEFINED || !count || get_type_id(args[0]) != LMD_TYPE_FUNC)
        return mvp_lmd_fail(LMD_MVP_TYPE, 0);
    TypeId type = get_type_id(owner);
    bool reduce = method == LMD_METHOD_REDUCE, map = method == LMD_METHOD_ARRAY_MAP, filter = method == LMD_METHOD_FILTER;
    RootFrame roots(8);
    if (!roots.valid()) return ItemError;
    Rooted<Item> source(roots, owner), callback(roots, args[0]), self(roots, !reduce && count > 1 ? args[1] : Item{.item = ITEM_JS_UNDEFINED}),
        output(roots, reduce && count > 1 ? args[1] : Item{.item = ITEM_JS_UNDEFINED}), value(roots, ItemNull),
        key(roots, ItemNull), mapped(roots, ItemNull), length_value(roots, ItemNull);
    int64_t length = type == LMD_TYPE_ARRAY ? owner.array->length : type == LMD_TYPE_STRING ?
        utf8_to_utf16_length(owner.get_string()->chars, owner.get_string()->len) : 0;
    if (type == LMD_TYPE_MAP) {
        String* name = heap_create_name("length", 6);
        if (!name) return ItemError;
        length_value.set(mvp_lmd_class_property(source.get(), Item{.item = s2it(name)}, ItemNull, LMD_PROP_GET, NULL));
        if (item_is_error(length_value.get())) return length_value.get();
        length_value.set(mvp_lmd_primitive_to_number(length_value.get()));
        if (item_is_error(length_value.get())) return length_value.get();
        double amount = it2d(length_value.get());
        if (amount > UINT32_MAX) return mvp_lmd_fail(LMD_MVP_RANGE, 0);
        length = isnan(amount) || amount < 0 ? 0 : (int64_t)trunc(amount);
    }
    if (map || filter) output.set(mvp_lmd_array_new(map ? length : 0));
    if (item_is_error(output.get())) return output.get();
    bool initialized = count > 1;
    uint64_t value_home = 0, result_home = 0;
    for (int64_t index = 0; index < length; index++) {
        if (type == LMD_TYPE_ARRAY) {
            if (index >= source.get().array->length || source.get().array->items[index].item == ITEM_JS_DELETED_SENTINEL) continue;
            value.set(source.get().array->items[index]);
        } else if (type == LMD_TYPE_STRING) value.set(mvp_lmd_string_at(source.get(), index, LMD_STRING_CHAR));
        else {
            key.set(mvp_lmd_number_to_string(index));
            if (item_is_error(key.get())) return key.get();
            Item present = mvp_lmd_class_property(source.get(), key.get(), ItemNull, LMD_PROP_HAS, NULL);
            if (item_is_error(present)) return present;
            if (!mvp_lmd_truthy(present)) continue;
            value.set(mvp_lmd_class_property(source.get(), key.get(), ItemNull, LMD_PROP_GET, NULL));
        }
        value.set(lambda_item_adopt_scalar_home(value.get(), &value_home));
        if (item_is_error(value.get())) return value.get();
        if (reduce && !initialized) {
            output.set(lambda_item_adopt_scalar_home(value.get(), &result_home)); initialized = true; continue;
        }
        Item arguments[4]; int argc = 0;
        if (reduce) arguments[argc++] = output.get();
        arguments[argc++] = value.get(); arguments[argc++] = Item{.item = i2it(index)}; arguments[argc++] = source.get();
        mapped.set(mvp_lmd_class_invoke(callback.get(), self.get(), arguments, argc, Item{.item = ITEM_JS_UNDEFINED}));
        if (item_is_error(mapped.get())) return mapped.get();
        if (method == LMD_METHOD_SOME && mvp_lmd_truthy(mapped.get())) return Item{.item = ITEM_TRUE};
        if (reduce) output.set(lambda_item_adopt_scalar_home(mapped.get(), &result_home));
        if (map || (filter && mvp_lmd_truthy(mapped.get()))) {
            Item stored = mvp_lmd_array_store(output.get(), map ? index : output.get().array->length, map ? mapped.get() : value.get());
            if (item_is_error(stored)) return stored;
        }
    }
    if (reduce && !initialized) return mvp_lmd_fail(LMD_MVP_TYPE, 0);
    return reduce ? lambda_item_uses_scalar_home(output.get()) ? push_d(it2d(output.get())) : output.get() :
        method == LMD_METHOD_SOME ? Item{.item = ITEM_FALSE} : output.get();
}
extern "C" Item mvp_lmd_array_edit(Item owner, Item* args, int64_t count, int64_t method) {
    if (get_type_id(owner) != LMD_TYPE_ARRAY) return mvp_lmd_fail(LMD_MVP_TYPE, 0);
    RootFrame roots(3);
    if (!roots.valid()) return ItemError;
    Rooted<Item> target(roots, owner), result(roots, mvp_lmd_array_new(0)), current(roots, ItemNull);
    if (item_is_error(result.get())) return result.get();
    if (method == LMD_METHOD_AT) {
        int64_t length = target.get().array->length;
        current.set(count ? mvp_lmd_primitive_to_number(args[0]) : Item{.item = i2it(0)});
        if (item_is_error(current.get())) return current.get();
        double index = it2d(current.get()); index = isnan(index) ? 0 : trunc(index);
        if (index < 0) index += length;
        if (!(index >= 0 && index < length && index < target.get().array->length)) return Item{.item = ITEM_JS_UNDEFINED};
        Item value = target.get().array->items[(int64_t)index];
        return value.item == ITEM_JS_DELETED_SENTINEL ? Item{.item = ITEM_JS_UNDEFINED} :
            lambda_item_uses_scalar_home(value) ? push_d(it2d(value)) : value;
    }
    if (method == LMD_METHOD_CONCAT || method == LMD_METHOD_TO_REVERSED) {
        if (method == LMD_METHOD_TO_REVERSED) count = 0;
        for (int64_t i = -1; i < count; i++) {
            current.set(i < 0 ? target.get() : args[i]);
            bool spread = get_type_id(current.get()) == LMD_TYPE_ARRAY;
            int64_t length = spread ? current.get().array->length : 1;
            if (length > UINT32_MAX - result.get().array->length) return mvp_lmd_fail(LMD_MVP_RANGE, 0);
            for (int64_t j = 0; j < length; j++) {
                Item value = spread ? current.get().array->items[j] : current.get();
                if (method == LMD_METHOD_TO_REVERSED && value.item == ITEM_JS_DELETED_SENTINEL)
                    value.item = ITEM_JS_UNDEFINED;
                Item stored = mvp_lmd_array_store(result.get(), result.get().array->length, value);
                if (item_is_error(stored)) return stored;
            }
        }
        if (method == LMD_METHOD_TO_REVERSED) array_reverse_in_place(result.get().array);
        return result.get();
    }
    int64_t length = target.get().array->length, start = method == LMD_METHOD_PUSH ? length : 0, removed = 0;
    int64_t inserted = method != LMD_METHOD_SPLICE ? count : count > 2 ? count - 2 : 0;
    if (method == LMD_METHOD_SHIFT) { inserted = 0; removed = length ? 1 : 0; }
    if (method == LMD_METHOD_SPLICE && count) {
        current.set(mvp_lmd_primitive_to_number(args[0]));
        if (item_is_error(current.get())) return current.get();
        double index = it2d(current.get()); index = isnan(index) ? 0 : trunc(index);
        start = index < 0 ? (int64_t)fmax(length + index, 0) : (int64_t)fmin(index, length);
        removed = length - start;
        if (count > 1) {
            current.set(mvp_lmd_primitive_to_number(args[1]));
            if (item_is_error(current.get())) return current.get();
            double amount = it2d(current.get());
            removed = isnan(amount) ? 0 : (int64_t)fmin(fmax(trunc(amount), 0), removed);
        }
    }
    if (inserted > UINT32_MAX - (length - removed)) return mvp_lmd_fail(LMD_MVP_RANGE, 0);
    for (int64_t i = 0; i < removed; i++) {
        Item stored = mvp_lmd_array_store(result.get(), i, target.get().array->items[start + i]);
        if (item_is_error(stored)) return stored;
    }
    if (inserted > removed) {
        for (int64_t i = length; i > start + removed;) {
            i--;
            Item stored = mvp_lmd_array_store(target.get(), i + inserted - removed, target.get().array->items[i]);
            if (item_is_error(stored)) return stored;
        }
    } else if (inserted < removed) {
        for (int64_t i = start + removed; i < length; i++) {
            Item stored = mvp_lmd_array_store(target.get(), i + inserted - removed, target.get().array->items[i]);
            if (item_is_error(stored)) return stored;
        }
    }
    for (int64_t i = 0; i < inserted; i++) {
        Item stored = mvp_lmd_array_store(target.get(), start + i, args[i + (method == LMD_METHOD_SPLICE ? 2 : 0)]);
        if (item_is_error(stored)) return stored;
    }
    Item resized = mvp_lmd_array_resize(target.get(), length - removed + inserted);
    if (item_is_error(resized)) return resized;
    if (method == LMD_METHOD_SHIFT) {
        Item value = removed ? result.get().array->items[0] : Item{.item = ITEM_JS_UNDEFINED};
        // the removed array owns numeric homes; publish the scalar before its root is released.
        return value.item == ITEM_JS_DELETED_SENTINEL ? Item{.item = ITEM_JS_UNDEFINED} :
            lambda_item_uses_scalar_home(value) ? push_d(it2d(value)) : value;
    }
    return method != LMD_METHOD_SPLICE ? Item{.item = i2it(target.get().array->length)} : result.get();
}
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
            result = mvp_lmd_primitive_to_number(result);
            if (item_is_error(result)) { sort->error->set(result); return 0; }
            double number = it2d(result);
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
