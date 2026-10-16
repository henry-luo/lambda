#include "mvp_lmd_runtime.h"
#include "../../runtime/lambda-root-frame.hpp"
#include "../../runtime/runtime-state.h"
#include "../../runtime/heap_api.h"
#include "../../../lib/sort.h"
#include "../../../lib/mem.h"
#include "../../../lib/utf.h"
#include "../js_well_known_names.h"
#include "../js_props.h"
#include "../js_runtime.h"
#include "../js_builtin_catalog.hpp"
#include <math.h>

const JsArrayOps mvp_lmd_array_ops = {
    &mvp_lmd_buffer_ops,
    [](Item value) { return mvp_lmd_box_primitive(value); },
    [](Item owner, int64_t index, Item value, JsArrayIndexOp operation) {
        const int operations[] = {LMD_PROP_GET, LMD_PROP_HAS, LMD_PROP_SET | LMD_PROP_STRICT,
            LMD_PROP_DELETE | LMD_PROP_STRICT, LMD_PROP_CREATE_DATA};
        return mvp_lmd_array_index_property(owner, index, value, operations[operation]);
    },
    mvp_lmd_array_new,
    mvp_lmd_is_callable,
    mvp_lmd_is_constructor,
    [](Item constructor, Item* args, int argc) {
        return mvp_lmd_class_invoke(constructor, Item{.item = ITEM_JS_UNDEFINED}, args, argc, constructor);
    },
    mvp_lmd_array_sort
};

struct MvpLmdSort {
    Rooted<Item>* comparator;
    Rooted<Item>* values;
    Rooted<Item>* error;
};
extern "C" int64_t mvp_lmd_array_indices_use_own_storage(Item owner) {
    if (get_type_id(owner) != LMD_TYPE_ARRAY ||
            container_js_elements_kind(owner.array) == JS_ELEMENTS_SPARSE_TAGGED) return false;
    MvpLmdLibraryState* library = mvp_lmd_program_library();
    if (!library || !library->mutable_prototypes) return true;
    for (bool ancestor = false;; ancestor = true) {
        TypeId kind = get_type_id(owner);
        // inspect only passive map/array links; no proxy traps or lazy function preparation.
        if ((kind != LMD_TYPE_MAP && kind != LMD_TYPE_ARRAY) || mvp_lmd_is_proxy(owner)) return false;
        Map* map = mvp_lmd_property_map(owner, !ancestor);
        if (!map || (ancestor && kind == LMD_TYPE_ARRAY && owner.array->length)) return false;
        TypeMap* shape = (TypeMap*)map->type;
        if (!ancestor && shape) {
            if (library && typemap_hash_lookup_key(shape, library->extensibility_key)) return false;
            ShapeEntry* length = typemap_hash_lookup(shape, "length", 6);
            if (length && (length->flags & JSPD_NON_WRITABLE)) return false;
        }
        // D8.4.1v2: the field index summarizes names only; links and policy stay live.
        if (shape && shape->digit_key_length && (int64_t)shape->digit_key_length == shape->length + 1) {
            if (shape->has_digit_key) return false;
        } else if (shape) FOR_EACH_MAP_FIELD(shape, field) {
            // a digit prefix conservatively rejects every canonical array index without interning keys.
            if (field->key_kind == NAME_KEY_STRING && field->name && field->name->length &&
                    field->name->str[0] >= '0' && field->name->str[0] <= '9') return false;
        }
        if (!library && !mvp_lmd_class_record(owner)) return true;
        owner = mvp_lmd_get_prototype_with_library(owner, library);
        if (owner.item == ITEM_NULL) return true;
        if (item_is_error(owner)) return false;
    }
}
Item mvp_lmd_sequence_length(Item source) {
    TypeId type = get_type_id(source);
    if (type == LMD_TYPE_ARRAY) return Item{.item = i2it(source.array->length)};
    if (type == LMD_TYPE_STRING) return Item{.item = i2it(utf8_to_utf16_length(source.get_string()->chars, source.get_string()->len))};
    if (!mvp_lmd_is_object(source)) return mvp_lmd_fail(LMD_MVP_TYPE, 0);
    RootFrame roots(2);
    if (!roots.valid()) return ItemError;
    Rooted<Item> object(roots, source), value(roots, ItemNull);
    String* key = heap_create_name("length", 6);
    if (!key) return mvp_lmd_fail(LMD_MVP_MEMORY, 0);
    value.set(mvp_lmd_property_with_receiver(object.get(), Item{.item = s2it(key)}, ItemNull, LMD_PROP_GET, object.get()));
    if (item_is_error(value.get())) return value.get();
    value.set(mvp_lmd_primitive_to_number(value.get()));
    if (item_is_error(value.get())) return value.get();
    double length = it2d(value.get());
    // LengthOfArrayLike uses ToLength; only actual Array allocation is limited to uint32.
    return Item{.item = i2it(isnan(length) || length < 0 ? 0 : (int64_t)fmin(trunc(length), 9007199254740991.0))};
}
Item mvp_lmd_array_species_create(Item source, int64_t length, bool* fresh) {
    // an intrinsic result is private until return; user species can publish or reuse their result.
    if (fresh) *fresh = true;
    MvpLmdLibraryState* state = mvp_lmd_program_library();
    if (!state || !state->mutable_prototypes) return mvp_lmd_array_new(length);
    if (get_type_id(source) == LMD_TYPE_ARRAY &&
            mvp_lmd_get_prototype(source).item == state->values[LMD_LIBRARY_ARRAY_PROTOTYPE].item) {
        String* name = heap_create_name("constructor", 11);
        if (!name) return mvp_lmd_fail(LMD_MVP_MEMORY, 0);
        Item key = {.item = s2it(name)};
        JsPropertyDescriptor descriptor = {};
        // these three own inspections are passive: arrays and the initialized
        // native constructor need neither traps nor lazy function preparation.
        if (mvp_lmd_get_own_property(source, key, &descriptor).item == ITEM_FALSE &&
                mvp_lmd_get_own_property(state->values[LMD_LIBRARY_ARRAY_PROTOTYPE], key, &descriptor).item == ITEM_TRUE &&
                (descriptor.flags & JS_PD_HAS_VALUE) && descriptor.value.item == state->values[LMD_LIBRARY_ARRAY].item &&
                mvp_lmd_get_own_property(state->values[LMD_LIBRARY_ARRAY],
                    Item{.item = s2it(well_known_name_ref(JS_SYMBOL_SPECIES))}, &descriptor).item == ITEM_TRUE &&
                (descriptor.flags & JS_PD_HAS_GET) && get_type_id(descriptor.getter) == LMD_TYPE_FUNC &&
                descriptor.getter.function->ptr == (fn_ptr)mvp_lmd_receiver_identity)
            return mvp_lmd_array_new(length);
    }
    RootFrame roots(2);
    if (!roots.valid()) return ItemError;
    Rooted<Item> owner(roots, source), constructor(roots, ItemNull);
    Item array = mvp_lmd_array_is_array(owner.get());
    if (item_is_error(array)) return array;
    if (array.item == ITEM_TRUE) {
        Item key = {.item = s2it(heap_create_name("constructor", 11))};
        constructor.set(mvp_lmd_property_with_receiver(owner.get(), key, ItemNull, LMD_PROP_GET, owner.get()));
        if (item_is_error(constructor.get())) return constructor.get();
        if (mvp_lmd_is_constructor(constructor.get())) {
            MvpLmdProgram* program = NULL;
            Item found = mvp_lmd_function_realm(constructor.get(), &program);
            if (item_is_error(found)) return found;
            MvpLmdLibraryState* foreign = mvp_lmd_program_library(program);
            // ArraySpeciesCreate ignores a different realm's intrinsic Array before @@species.
            if (foreign != state && constructor.get().item == foreign->values[LMD_LIBRARY_ARRAY].item)
                constructor.set(Item{.item = ITEM_JS_UNDEFINED});
        }
        if (mvp_lmd_is_object(constructor.get())) {
            key.item = s2it(well_known_name_ref(JS_SYMBOL_SPECIES));
            constructor.set(mvp_lmd_property_with_receiver(constructor.get(), key, ItemNull, LMD_PROP_GET, constructor.get()));
            if (item_is_error(constructor.get())) return constructor.get();
            if (constructor.get().item == ITEM_NULL) constructor.set(Item{.item = ITEM_JS_UNDEFINED});
        }
        if (constructor.get().item != ITEM_JS_UNDEFINED && constructor.get().item != state->values[LMD_LIBRARY_ARRAY].item) {
            if (!mvp_lmd_is_constructor(constructor.get())) return mvp_lmd_fail(LMD_MVP_TYPE, 0);
            Item argument = {.item = i2it(length)};
            if (fresh) *fresh = false;
            return mvp_lmd_class_invoke(constructor.get(), Item{.item = ITEM_JS_UNDEFINED}, &argument, 1, constructor.get());
        }
    }
    return mvp_lmd_array_new(length);
}
extern "C" Item mvp_lmd_array_visit(Item owner, Item* args, int64_t count, int64_t method) {
    if (owner.item == ITEM_NULL || owner.item == ITEM_JS_UNDEFINED)
        return mvp_lmd_fail(LMD_MVP_TYPE, 0);
    bool backwards = method == LMD_METHOD_REDUCE_RIGHT ||
        method == LMD_METHOD_FIND_LAST || method == LMD_METHOD_FIND_LAST_INDEX;
    bool reduce = method == LMD_METHOD_REDUCE || method == LMD_METHOD_REDUCE_RIGHT;
    bool map = method == LMD_METHOD_ARRAY_MAP, filter = method == LMD_METHOD_FILTER;
    bool find_index = method == LMD_METHOD_FIND_INDEX || method == LMD_METHOD_FIND_LAST_INDEX;
    bool find = method == LMD_METHOD_FIND || method == LMD_METHOD_FIND_LAST || find_index;
    RootFrame roots(8);
    if (!roots.valid()) return ItemError;
    Rooted<Item> source(roots, owner), callback(roots, count ? args[0] : Item{.item = ITEM_JS_UNDEFINED}), self(roots, !reduce && count > 1 ? args[1] : Item{.item = ITEM_JS_UNDEFINED}),
        output(roots, reduce && count > 1 ? args[1] : Item{.item = ITEM_JS_UNDEFINED}), value(roots, ItemNull),
        key(roots, ItemNull), mapped(roots, ItemNull), length_value(roots, ItemNull);
    if (!mvp_lmd_is_object(source.get())) source.set(mvp_lmd_box_primitive(source.get()));
    if (item_is_error(source.get())) return source.get();
    length_value.set(mvp_lmd_sequence_length(source.get()));
    if (item_is_error(length_value.get())) return length_value.get();
    int64_t length = it2i(length_value.get());
    if (!mvp_lmd_is_callable(callback.get())) return mvp_lmd_fail(LMD_MVP_TYPE, 0);
    if (method == LMD_METHOD_FLAT_MAP) {
        // the visitor's roots retain this borrowed pair through species and mapper reentry.
        Item mapping[] = {callback.get(), self.get()};
        return mvp_lmd_array_flatten(source.get(), 1, length, mapping);
    }
    bool fresh = false;
    if (map || filter) output.set(mvp_lmd_array_species_create(source.get(), map ? length : 0, &fresh));
    if (item_is_error(output.get())) return output.get();
    bool initialized = count > 1;
    uint64_t value_home = 0, result_home = 0;
    int64_t destination = 0;
    for (int64_t visited = 0; visited < length; visited++) {
        int64_t index = backwards ? length - visited - 1 : visited;
        value.set(mvp_lmd_array_index_property(source.get(), index, ItemNull, find ? LMD_PROP_GET : LMD_PROP_PRESENT));
        if (value.get().item == ITEM_JS_DELETED_SENTINEL) continue;
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
        if (method == LMD_METHOD_EVERY && !mvp_lmd_truthy(mapped.get())) return Item{.item = ITEM_FALSE};
        if (find && mvp_lmd_truthy(mapped.get())) return find_index ? Item{.item = i2it(index)} :
            lambda_item_uses_scalar_home(value.get()) ? push_d(it2d(value.get())) : value.get();
        if (reduce) output.set(lambda_item_adopt_scalar_home(mapped.get(), &result_home));
        if (map || (filter && mvp_lmd_truthy(mapped.get()))) {
            // species may return an ordinary object or a proxy; create an own data property.
            Item stored = mvp_lmd_array_index_property(output.get(), map ? index : destination++,
                map ? mapped.get() : value.get(), LMD_PROP_CREATE_DATA | (fresh ? LMD_PROP_OWN_STORAGE : 0));
            if (item_is_error(stored)) return stored;
        }
    }
    if (reduce && !initialized) return mvp_lmd_fail(LMD_MVP_TYPE, 0);
    return reduce ? lambda_item_uses_scalar_home(output.get()) ? push_d(it2d(output.get())) : output.get() :
        method == LMD_METHOD_SOME ? Item{.item = ITEM_FALSE} : method == LMD_METHOD_EVERY ? Item{.item = ITEM_TRUE} :
        find_index ? Item{.item = i2it(-1)} : output.get();
}
extern "C" Item mvp_lmd_array_edit(Item owner, Item* args, int64_t count, int64_t method) {
    if (method >= LMD_METHOD_COPY_WITHIN && method <= LMD_METHOD_WITH) {
        const int methods[] = {JS_BUILTIN_ARR_COPY_WITHIN, JS_BUILTIN_ARR_TO_SORTED,
            JS_BUILTIN_ARR_TO_SPLICED, JS_BUILTIN_ARR_WITH};
        return js_array_method_with_ops(methods[method - LMD_METHOD_COPY_WITHIN], owner, args, count, &mvp_lmd_array_ops);
    }
    RootFrame roots(3);
    if (!roots.valid()) return ItemError;
    // methods allocate an output only when their result needs one.
    Rooted<Item> target(roots, owner), result(roots, ItemNull), current(roots, ItemNull);
    bool fresh = false;
    if (!mvp_lmd_is_object(target.get())) target.set(mvp_lmd_box_primitive(target.get()));
    if (item_is_error(target.get())) return target.get();
    if (method == LMD_METHOD_ARRAY_STRING) {
        if (!mvp_lmd_program_library()) return mvp_lmd_array_to_string(target.get(), Item{.item = ITEM_JS_UNDEFINED});
        Item key = {.item = s2it(heap_create_name("join", 4))};
        current.set(mvp_lmd_property_with_receiver(target.get(), key, ItemNull, LMD_PROP_GET, target.get()));
        if (item_is_error(current.get())) return current.get();
        if (mvp_lmd_is_callable(current.get()))
            return mvp_lmd_class_invoke(current.get(), target.get(), NULL, 0, Item{.item = ITEM_JS_UNDEFINED});
        return mvp_lmd_object_string(target.get());
    }
    if (method == LMD_METHOD_JOIN)
        return mvp_lmd_array_to_string(target.get(), count ? args[0] : Item{.item = ITEM_JS_UNDEFINED});
    if (method == LMD_METHOD_SORT) return mvp_lmd_array_sort(target.get(), count ? args[0] : Item{.item = ITEM_JS_UNDEFINED});
    current.set(method == LMD_METHOD_CONCAT ? Item{.item = i2it(0)} : mvp_lmd_sequence_length(target.get()));
    if (item_is_error(current.get())) return current.get();
    int64_t length = it2i(current.get());
    int64_t storage = 0;
    auto index_property = [&](int64_t index, Item value, int64_t operation) {
        return mvp_lmd_array_index_property(target.get(), index, value, operation | storage);
    };
    if (method == LMD_METHOD_FLAT) {
        current.set(count && args[0].item != ITEM_JS_UNDEFINED ? mvp_lmd_primitive_to_number(args[0]) : Item{.item = i2it(1)});
        return item_is_error(current.get()) ? current.get() : mvp_lmd_array_flatten(target.get(), it2d(current.get()), length);
    }
    if (method == LMD_METHOD_POP) {
        storage = mvp_lmd_array_indices_use_own_storage(target.get()) ? LMD_PROP_OWN_STORAGE : 0;
        uint64_t home = 0;
        current.set(length ? lambda_item_adopt_scalar_home(index_property(length - 1,
            ItemNull, LMD_PROP_GET), &home) : Item{.item = ITEM_JS_UNDEFINED});
        if (item_is_error(current.get())) return current.get();
        if (length) {
            Item deleted = index_property(length - 1, ItemNull, LMD_PROP_DELETE | LMD_PROP_STRICT);
            if (item_is_error(deleted)) return deleted;
        }
        Item resized = index_property(-1, Item{.item = i2it(length ? length - 1 : 0)}, LMD_PROP_SET | LMD_PROP_STRICT);
        return item_is_error(resized) ? resized : lambda_item_uses_scalar_home(current.get()) ? push_d(it2d(current.get())) : current.get();
    }
    if (method == LMD_METHOD_FILL || method == LMD_METHOD_SLICE || method == LMD_METHOD_ARRAY_INDEX_OF ||
            method == LMD_METHOD_INCLUDES || method == LMD_METHOD_LAST_INDEX_OF) {
        bool backwards = method == LMD_METHOD_LAST_INDEX_OF;
        if (!length && (method == LMD_METHOD_ARRAY_INDEX_OF || method == LMD_METHOD_INCLUDES || backwards))
            return Item{.item = method == LMD_METHOD_INCLUDES ? ITEM_FALSE : i2it(-1)};
        int64_t first = backwards ? length - 1 : 0, last = length;
        int start_arg = method == LMD_METHOD_SLICE ? 0 : 1, end_arg = method == LMD_METHOD_FILL ? 2 : 1;
        for (int i = 0; i < 2; i++) {
            if (i && method != LMD_METHOD_FILL && method != LMD_METHOD_SLICE) break;
            int arg = i ? end_arg : start_arg;
            if (arg >= count || (i && args[arg].item == ITEM_JS_UNDEFINED)) continue;
            current.set(mvp_lmd_primitive_to_number(args[arg]));
            if (item_is_error(current.get())) return current.get();
            double position = it2d(current.get()); position = isnan(position) ? 0 : trunc(position);
            int64_t relative = backwards ? position < 0 ? (int64_t)fmax(length + position, -1) :
                (int64_t)fmin(position, length - 1) :
                position < 0 ? (int64_t)fmax(length + position, 0) : (int64_t)fmin(position, length);
            if (i) last = relative; else first = relative;
        }
        if (method == LMD_METHOD_SLICE) {
            result.set(mvp_lmd_array_species_create(target.get(), last > first ? last - first : 0, &fresh));
            if (item_is_error(result.get())) return result.get();
        }
        // only writes need a chain proof; reads use the existing present-element fast path.
        storage = method == LMD_METHOD_FILL && mvp_lmd_array_indices_use_own_storage(target.get()) ? LMD_PROP_OWN_STORAGE : 0;
        for (int64_t i = first; backwards ? i >= 0 : i < last; i += backwards ? -1 : 1) {
            if (method == LMD_METHOD_FILL) {
                Item status = index_property(i, count ? args[0] : Item{.item = ITEM_JS_UNDEFINED}, LMD_PROP_SET | LMD_PROP_STRICT);
                if (item_is_error(status)) return status;
                continue;
            }
            current.set(index_property(i, ItemNull,
                method == LMD_METHOD_INCLUDES ? LMD_PROP_GET : LMD_PROP_PRESENT));
            if (item_is_error(current.get())) return current.get();
            if (current.get().item == ITEM_JS_DELETED_SENTINEL) continue;
            if (method == LMD_METHOD_SLICE) {
                Item stored = mvp_lmd_array_index_property(result.get(), i - first, current.get(),
                    LMD_PROP_CREATE_DATA | (fresh ? LMD_PROP_OWN_STORAGE : 0));
                if (item_is_error(stored)) return stored;
            } else {
                Item sought = count ? args[0] : Item{.item = ITEM_JS_UNDEFINED};
                bool equal = mvp_lmd_same_value_zero(current.get(), sought);
                if (method != LMD_METHOD_INCLUDES && get_type_id(current.get()) == LMD_TYPE_FLOAT)
                    equal &= !isnan(it2d(current.get()));
                if (equal) return Item{.item = method == LMD_METHOD_INCLUDES ? ITEM_TRUE : i2it(i)};
            }
        }
        if (method == LMD_METHOD_SLICE) {
            Item resized = mvp_lmd_array_index_property(result.get(), -1, Item{.item = i2it(last > first ? last - first : 0)},
                LMD_PROP_SET | LMD_PROP_STRICT);
            if (item_is_error(resized)) return resized;
        }
        return method == LMD_METHOD_FILL ? target.get() : method == LMD_METHOD_SLICE ? result.get() :
            Item{.item = method == LMD_METHOD_INCLUDES ? ITEM_FALSE : i2it(-1)};
    }
    if (method == LMD_METHOD_REVERSE) {
        if (mvp_lmd_array_indices_use_own_storage(target.get())) {
            array_reverse_in_place(target.get().array);
            return target.get();
        }
        RootFrame pair_roots(2);
        if (!pair_roots.valid()) return ItemError;
        Rooted<Item> lower(pair_roots, ItemNull), upper(pair_roots, ItemNull);
        uint64_t homes[2] = {};
        for (int64_t lo = 0; lo < length / 2; lo++) {
            int64_t hi = length - lo - 1;
            Item has_lower = index_property(lo, ItemNull, LMD_PROP_HAS);
            if (item_is_error(has_lower)) return has_lower;
            lower.set(has_lower.item == ITEM_TRUE ? lambda_item_adopt_scalar_home(index_property(lo, ItemNull, LMD_PROP_GET), &homes[0]) : ItemNull);
            if (item_is_error(lower.get())) return lower.get();
            Item has_upper = index_property(hi, ItemNull, LMD_PROP_HAS);
            if (item_is_error(has_upper)) return has_upper;
            upper.set(has_upper.item == ITEM_TRUE ? lambda_item_adopt_scalar_home(index_property(hi, ItemNull, LMD_PROP_GET), &homes[1]) : ItemNull);
            if (item_is_error(upper.get())) return upper.get();
            if (has_lower.item == ITEM_FALSE && has_upper.item == ITEM_FALSE) continue;
            Item status = index_property(lo, upper.get(),
                (has_upper.item == ITEM_TRUE ? LMD_PROP_SET : LMD_PROP_DELETE) | LMD_PROP_STRICT);
            if (item_is_error(status)) return status;
            status = index_property(hi, lower.get(),
                (has_lower.item == ITEM_TRUE ? LMD_PROP_SET : LMD_PROP_DELETE) | LMD_PROP_STRICT);
            if (item_is_error(status)) return status;
        }
        return target.get();
    }
    if (method == LMD_METHOD_AT) {
        current.set(count ? mvp_lmd_primitive_to_number(args[0]) : Item{.item = i2it(0)});
        if (item_is_error(current.get())) return current.get();
        double index = it2d(current.get()); index = isnan(index) ? 0 : trunc(index);
        if (index < 0) index += length;
        if (!(index >= 0 && index < length)) return Item{.item = ITEM_JS_UNDEFINED};
        Item value = index_property((int64_t)index, ItemNull, LMD_PROP_GET);
        return value.item == ITEM_JS_DELETED_SENTINEL ? Item{.item = ITEM_JS_UNDEFINED} :
            lambda_item_uses_scalar_home(value) ? push_d(it2d(value)) : value;
    }
    if (method == LMD_METHOD_CONCAT || method == LMD_METHOD_TO_REVERSED) {
        fresh = method == LMD_METHOD_TO_REVERSED;
        result.set(method == LMD_METHOD_CONCAT ? mvp_lmd_array_species_create(target.get(), 0, &fresh) : mvp_lmd_array_new(length));
        if (item_is_error(result.get())) return result.get();
        if (method == LMD_METHOD_TO_REVERSED) count = 0;
        int64_t destination = 0;
        for (int64_t i = -1; i < count; i++) {
            current.set(i < 0 ? target.get() : args[i]);
            Item array = {.item = ITEM_JS_UNDEFINED};
            MvpLmdLibraryState* state = mvp_lmd_program_library();
            if (method == LMD_METHOD_CONCAT && state && state->mutable_prototypes && mvp_lmd_is_object(current.get())) {
                Item key = {.item = s2it(well_known_name_ref(JS_SYMBOL_IS_CONCAT_SPREADABLE))};
                array = mvp_lmd_property_with_receiver(current.get(), key, ItemNull, LMD_PROP_GET, current.get());
                if (item_is_error(array)) return array;
            }
            if (method == LMD_METHOD_CONCAT && array.item == ITEM_JS_UNDEFINED) array = mvp_lmd_array_is_array(current.get());
            if (item_is_error(array)) return array;
            bool spread = method == LMD_METHOD_TO_REVERSED || mvp_lmd_truthy(array);
            Item extent = method == LMD_METHOD_TO_REVERSED ? Item{.item = i2it(length)} :
                spread ? mvp_lmd_sequence_length(current.get()) : Item{.item = i2it(1)};
            if (item_is_error(extent)) return extent;
            int64_t length = it2i(extent);
            if (length > 9007199254740991LL - destination) return mvp_lmd_fail(LMD_MVP_TYPE, 0);
            for (int64_t j = 0; j < length; j++) {
                int64_t index = method == LMD_METHOD_TO_REVERSED ? length - j - 1 : j;
                Item value = spread ? mvp_lmd_array_index_property(current.get(), index, ItemNull,
                    method == LMD_METHOD_TO_REVERSED ? LMD_PROP_GET : LMD_PROP_PRESENT) : current.get();
                if (item_is_error(value)) return value;
                if (method == LMD_METHOD_TO_REVERSED && value.item == ITEM_JS_DELETED_SENTINEL)
                    value.item = ITEM_JS_UNDEFINED;
                Item stored = value.item == ITEM_JS_DELETED_SENTINEL ? ItemNull :
                    mvp_lmd_array_index_property(result.get(), destination, value,
                        LMD_PROP_CREATE_DATA | (fresh ? LMD_PROP_OWN_STORAGE : 0));
                if (item_is_error(stored)) return stored;
                destination++;
            }
        }
        if (method == LMD_METHOD_CONCAT) {
            Item resized = mvp_lmd_array_index_property(result.get(), -1, Item{.item = i2it(destination)},
                LMD_PROP_SET | LMD_PROP_STRICT | (fresh ? LMD_PROP_OWN_STORAGE : 0));
            if (item_is_error(resized)) return resized;
        }
        return result.get();
    }
    int64_t start = method == LMD_METHOD_PUSH ? length : 0, removed = 0;
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
    if (inserted > 9007199254740991LL - (length - removed)) return mvp_lmd_fail(LMD_MVP_TYPE, 0);
    if (method == LMD_METHOD_SPLICE) {
        result.set(mvp_lmd_array_species_create(target.get(), removed, &fresh));
        if (item_is_error(result.get())) return result.get();
    } else if (method == LMD_METHOD_SHIFT && removed) {
        fresh = true;
        result.set(mvp_lmd_array_new(removed));
        if (item_is_error(result.get())) return result.get();
    }
    // no guest callbacks remain after this proof; physical stores retain scalar ownership.
    storage = mvp_lmd_array_indices_use_own_storage(target.get()) ? LMD_PROP_OWN_STORAGE : 0;
    for (int64_t i = 0; i < removed; i++) {
        Item value = index_property(start + i, ItemNull,
            method == LMD_METHOD_SHIFT ? LMD_PROP_GET : LMD_PROP_PRESENT);
        if (item_is_error(value)) return value;
        Item stored = value.item == ITEM_JS_DELETED_SENTINEL ? ItemNull :
            mvp_lmd_array_index_property(result.get(), i, value, LMD_PROP_CREATE_DATA | (fresh ? LMD_PROP_OWN_STORAGE : 0));
        if (item_is_error(stored)) return stored;
    }
    if (method == LMD_METHOD_SPLICE) {
        Item resized = mvp_lmd_array_index_property(result.get(), -1, Item{.item = i2it(removed)},
            LMD_PROP_SET | LMD_PROP_STRICT | (fresh ? LMD_PROP_OWN_STORAGE : 0));
        if (item_is_error(resized)) return resized;
    }
    if (inserted > removed) {
        for (int64_t i = length; i > start + removed;) {
            i--;
            Item value = index_property(i, ItemNull, LMD_PROP_PRESENT);
            if (item_is_error(value)) return value;
            Item stored = index_property(i + inserted - removed, value,
                (value.item == ITEM_JS_DELETED_SENTINEL ? LMD_PROP_DELETE : LMD_PROP_SET) | LMD_PROP_STRICT);
            if (item_is_error(stored)) return stored;
        }
    } else if (inserted < removed) {
        for (int64_t i = start + removed; i < length; i++) {
            Item value = index_property(i, ItemNull, LMD_PROP_PRESENT);
            if (item_is_error(value)) return value;
            Item stored = index_property(i + inserted - removed, value,
                (value.item == ITEM_JS_DELETED_SENTINEL ? LMD_PROP_DELETE : LMD_PROP_SET) | LMD_PROP_STRICT);
            if (item_is_error(stored)) return stored;
        }
    }
    for (int64_t i = 0; i < inserted; i++) {
        Item stored = index_property(start + i, args[i + (method == LMD_METHOD_SPLICE ? 2 : 0)], LMD_PROP_SET | LMD_PROP_STRICT);
        if (item_is_error(stored)) return stored;
    }
    for (int64_t i = length; i > length - removed + inserted;) {
        Item deleted = index_property(--i, ItemNull, LMD_PROP_DELETE | LMD_PROP_STRICT);
        if (item_is_error(deleted)) return deleted;
    }
    Item resized = index_property(-1, Item{.item = i2it(length - removed + inserted)}, LMD_PROP_SET | LMD_PROP_STRICT);
    if (item_is_error(resized)) return resized;
    if (method == LMD_METHOD_SHIFT) {
        Item value = removed ? result.get().array->items[0] : Item{.item = ITEM_JS_UNDEFINED};
        // the removed array owns numeric homes; publish the scalar before its root is released.
        return value.item == ITEM_JS_DELETED_SENTINEL ? Item{.item = ITEM_JS_UNDEFINED} :
            lambda_item_uses_scalar_home(value) ? push_d(it2d(value)) : value;
    }
    return method != LMD_METHOD_SPLICE ? Item{.item = i2it(length - removed + inserted)} : result.get();
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
    if (!mvp_lmd_is_object(owner) ||
            (comparator.item != ITEM_JS_UNDEFINED && !mvp_lmd_is_callable(comparator)))
        return mvp_lmd_fail(LMD_MVP_TYPE, 0);
    RootFrame roots(4);
    if (!roots.valid()) return ItemError;
    Rooted<Item> target(roots, owner), fn(roots, comparator), values(roots, mvp_lmd_array_new(0)), error(roots, ItemNull);
    if (item_is_error(values.get())) return values.get();
    Item extent = mvp_lmd_sequence_length(target.get());
    if (item_is_error(extent)) return extent;
    int64_t length = it2i(extent), undefined = 0;
    for (int64_t i = 0; i < length; i++) {
        Item value = mvp_lmd_array_index_property(target.get(), i, ItemNull, LMD_PROP_PRESENT);
        if (item_is_error(value)) return value;
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
        Item result = mvp_lmd_array_index_property(target.get(), i, value, LMD_PROP_SET | LMD_PROP_STRICT);
        if (item_is_error(result)) { error.set(result); break; }
    }
    mem_free(indices);
    if (item_is_error(error.get())) return error.get();
    // callback mutations beyond the snapshotted length survive the final write/delete pass.
    for (int64_t i = count + undefined; i < length; i++) {
        Item deleted = mvp_lmd_array_index_property(target.get(), i, ItemNull, LMD_PROP_DELETE | LMD_PROP_STRICT);
        if (item_is_error(deleted)) return deleted;
    }
    return target.get();
}
extern "C" Item mvp_lmd_array_spread(Item target, Item source, int64_t projection) {
    TypeId type = get_type_id(source);
    if (type != LMD_TYPE_ARRAY && !mvp_lmd_is_ordered_map(source))
        return mvp_lmd_fail(LMD_MVP_CAPABILITY, 0);
    RootFrame roots(3);
    if (!roots.valid()) return ItemError;
    Rooted<Item> output(roots, target), input(roots, source), element(roots, ItemNull);
    for (int64_t cursor = 0;; cursor++) {
        if (type == LMD_TYPE_ARRAY) {
            if (cursor >= input.get().array->length) break;
            Item value = mvp_lmd_array_index_property(input.get(), cursor, ItemNull, LMD_PROP_GET);
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
