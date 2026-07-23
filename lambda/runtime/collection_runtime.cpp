#include "../lambda-data.hpp"
#include "lambda-number-runtime.hpp"
#include "../core/collection_storage.h"
#include "../io/input-allocation-context.h"
#include "heap_api.h"
#include "../js/js_exec_profile_weak.h"
#include "../../lib/checked_math.hpp"
#include "../../lib/log.h"

extern __thread EvalContext* context;
extern __thread Context* input_context;

// These UI conversion helpers require the DOM representation and remain in the
// runtime data implementation. Collection growth calls them only for an
// explicitly input-owned UI result, never as an allocation fallback.
Item ui_copy_string_to_arena(Arena* arena, Item str_item);
Item ui_merge_strings_to_arena(Arena* arena, String* previous, String* next);

void expand_list(List* list, Arena* arena) {
    if (!list) return;
    JS_WEAK_PROPERTY_SET_BRANCH("expand_list_total");
    int64_t previous_capacity = list->capacity;
    int64_t new_capacity = previous_capacity ? previous_capacity * 2 : 8;
    Item* old_items = list->items;

    if (arena && (old_items == nullptr || arena_owns(arena, old_items))) {
        Item* new_items = (Item*)arena_alloc(arena, (size_t)new_capacity * sizeof(Item));
        if (!new_items) return;
        if (old_items && previous_capacity > 0) {
            memcpy(new_items, old_items, (size_t)previous_capacity * sizeof(Item));
        }
        list->items = new_items;
        list->capacity = new_capacity;
        list_relocate_owned_tail(list, old_items, previous_capacity, new_items, new_capacity);
        return;
    }

    size_t new_size;
    if (!lam::checked_mul((size_t)new_capacity, sizeof(Item), &new_size)) return;
    RootFrame roots((Context*)context, 1);
    Rooted<List*> rooted_list(roots, list);
    Item* new_items = (Item*)heap_data_alloc(new_size);
    list = rooted_list.get();
    if (!new_items) return;

    // Collection may compact the old buffer. Reload the rooted owner before
    // copying so a runtime growth path never dereferences nursery storage.
    old_items = list->items;
    if (old_items && previous_capacity > 0) {
        memcpy(new_items, old_items, (size_t)previous_capacity * sizeof(Item));
    }
    list->items = new_items;
    list->capacity = new_capacity;
    list_relocate_owned_tail(list, old_items, previous_capacity, new_items, new_capacity);
}

bool js_array_has_props(const Array* arr) {
    return arr && arr->has_js_props;
}

Map* js_array_props(const Array* arr) {
    if (!js_array_has_props(arr) || !arr->items || arr->capacity <= 0) return NULL;
    Item props_item = arr->items[arr->capacity - 1];
    return get_type_id(props_item) == LMD_TYPE_MAP ? props_item.map : NULL;
}

int64_t container_tail_reserved(const Array* arr) {
    return js_array_has_props(arr) ? 1 : 0;
}

int64_t container_dense_capacity(const Array* arr) {
    return arr && arr->capacity > arr->extra ? arr->capacity - arr->extra : 0;
}

void js_array_set_props(Array* arr, Map* props) {
    if (!arr || !props) return;
    if (js_array_has_props(arr)) {
        arr->items[arr->capacity - 1] = {.map = props};
        return;
    }

    RootFrame roots((Context*)context, 1);
    Rooted<Map*> rooted_props(roots, props);
    int64_t dense_capacity = arr->capacity >= arr->extra ? arr->capacity - arr->extra : 0;
    int64_t dense_required = arr->length < dense_capacity ? arr->length : dense_capacity;
    while (!arr->items || dense_required + arr->extra + 2 > arr->capacity) {
        int64_t old_capacity = arr->capacity;
        expand_list((List*)arr, nullptr);
        if (arr->capacity <= old_capacity) return;
    }
    props = rooted_props.get();

    // The props reservation is the high tail slot. Shift any existing scalar
    // payloads down one slot and rebase only logical Items that point into them.
    int64_t old_tail_start = arr->capacity - arr->extra;
    if (arr->extra > 0) {
        memmove(arr->items + old_tail_start - 1, arr->items + old_tail_start,
            (size_t)arr->extra * sizeof(Item));
        int64_t dense_count = arr->length < old_tail_start ? arr->length : old_tail_start;
        for (int64_t i = 0; i < dense_count; i++) {
            Item item = arr->items[i];
            if (!(((item._type_id == LMD_TYPE_FLOAT && item.double_ptr > 1) ||
                        item._type_id == LMD_TYPE_FLOAT64) ||
                    item._type_id == LMD_TYPE_INT64 || item._type_id == LMD_TYPE_UINT64)) continue;
            Item* pointer = (Item*)item.double_ptr;
            if (pointer < arr->items + old_tail_start ||
                    pointer >= arr->items + arr->capacity) continue;
            void* shifted = pointer - 1;
            arr->items[i] = {.item = is_float_type_id(item._type_id) ? d2it(shifted) :
                item._type_id == LMD_TYPE_INT64 ? l2it(shifted) : u2it(shifted)};
        }
    }
    arr->items[arr->capacity - 1] = {.map = props};
    arr->extra++;
    arr->has_js_props = 1;
    // Sparse arrays can have a spec length beyond their physical dense prefix.
    // Promotion may allocate fresh slots after the last sparse-hole stamp;
    // mark those slots as holes so iteration never exposes zeroed words as null.
    int64_t promoted_dense_capacity = arr->capacity - arr->extra;
    for (int64_t i = dense_required; i < promoted_dense_capacity; i++) {
        arr->items[i] = {.item = ITEM_JS_DELETED_SENTINEL};
    }
}

void array_push(Array* arr, Item item) {
    TypeId type_id = get_type_id(item);
    if (type_id == LMD_TYPE_ARRAY) {
        List* nested = item.array;
        if (nested && nested->is_content) {
            RootFrame roots((Context*)context, 2);
            Rooted<Array*> rooted_array(roots, arr);
            Rooted<Item> rooted_source(roots, item);
            for (int64_t i = 0; i < nested->length; i++) {
                arr = rooted_array.get();
                nested = rooted_source.get().array;
                array_push(arr, nested->items[i]);
            }
            return;
        }
    }
    if (arr->length + arr->extra + 2 > arr->capacity) {
        // A collection allocation is a safepoint. Both the destination and
        // the incoming value must survive it before their storage is read.
        RootFrame roots((Context*)context, 2);
        Rooted<Array*> rooted_array(roots, arr);
        Rooted<Item> rooted_item(roots, item);
        expand_list((List*)rooted_array.get(), nullptr);
        arr = rooted_array.get();
        item = rooted_item.get();
    }
    array_set(arr, arr->length, item);
    arr->length++;
}

Item pn_push(Item arr_item, Item value) {
    TypeId tid = get_type_id(arr_item);
    if (tid != LMD_TYPE_ARRAY) {
        log_error("push: expected a growable array, got %s", get_type_name(tid));
        return arr_item;
    }
    array_push(arr_item.array, value);
    return arr_item;
}

Item pn_push_cow(Item owner, Item value) {
    Item replacement = cow_prepare_write(owner);
    if (get_type_id(replacement) == LMD_TYPE_ERROR) return replacement;
    return pn_push(replacement, value);
}

Item pn_splice(Item arr_item, Item start_item, Item count_item) {
    TypeId tid = get_type_id(arr_item);
    if (tid != LMD_TYPE_ARRAY && tid != LMD_TYPE_ARRAY_NUM) {
        log_error("splice: expected a growable array, got %s", get_type_name(tid));
        return arr_item;
    }
    int64_t start = 0;
    int64_t count = 0;
    if (!lambda_item_to_int64_exact(start_item, &start) ||
            !lambda_item_to_int64_exact(count_item, &count)) {
        log_error("splice: start and count must be integer-valued numbers");
        return arr_item;
    }

    if (tid == LMD_TYPE_ARRAY_NUM) {
        ArrayNum* arr = arr_item.array_num;
        if (arr->is_view || arr->is_ndim) {
            log_error("splice: cannot splice a view or N-D array; copy()/ravel() first");
            return arr_item;
        }
        int64_t length = arr->length;
        if (start < 0) start += length;
        if (start < 0) start = 0;
        if (start > length) start = length;
        if (count < 0) count = 0;
        if (count > length - start) count = length - start;
        if (count == 0) return arr_item;
        size_t element_size = ELEM_TYPE_SIZE[arr->get_elem_type() >> 4];
        char* base = (char*)arr->data;
        memmove(base + (size_t)start * element_size,
            base + (size_t)(start + count) * element_size,
            (size_t)(length - start - count) * element_size);
        arr->length = length - count;
        return arr_item;
    }

    Array* arr = arr_item.array;
    int64_t length = arr->length;
    if (start < 0) start += length;
    if (start < 0) start = 0;
    if (start > length) start = length;
    if (count < 0) count = 0;
    if (count > length - start) count = length - start;
    if (count == 0) return arr_item;
    for (int64_t i = start + count; i < length; i++) {
        arr->items[i - count] = arr->items[i];
    }
    arr->length = length - count;
    return arr_item;
}

Item pn_splice_cow(Item owner, Item start_item, Item count_item) {
    Item replacement = cow_prepare_write(owner);
    if (get_type_id(replacement) == LMD_TYPE_ERROR) return replacement;
    return pn_splice(replacement, start_item, count_item);
}

void list_push(List* list, Item item) {
    TypeId type_id = get_type_id(item);
    if (type_id == LMD_TYPE_NULL) return;

    if (type_id == LMD_TYPE_ARRAY) {
        List* nested = item.array;
        if (nested && (uintptr_t)nested >= 0x1000 && nested->is_content) {
            if (!nested->items) {
                if (nested->length == 0) return;
                log_error("list_push: nested list has no backing storage");
                return;
            }
            RootFrame roots((Context*)context, 2);
            Rooted<List*> rooted_list(roots, list);
            Rooted<Item> rooted_source(roots, item);
            for (int64_t i = 0; i < nested->length; i++) {
                list = rooted_list.get();
                nested = rooted_source.get().array;
                list_push(list, nested->items[i]);
            }
            return;
        }
    }

    if (type_id == LMD_TYPE_STRING) {
        bool is_ui = input_allocation_context && input_allocation_context->ui_mode &&
            input_allocation_context->arena;
        if (is_ui && list->is_content) {
            item = ui_copy_string_to_arena(input_allocation_context->arena, item);
        }

        bool should_merge = (input_context || input_allocation_context) &&
            !(input_allocation_context ? input_allocation_context->disable_string_merging :
                input_context->disable_string_merging) &&
            list->length > 0 && list->items;
        if (should_merge) {
            Item previous_item = list->items[list->length - 1];
            if (get_type_id(previous_item) == LMD_TYPE_STRING) {
                String* previous = previous_item.get_safe_string();
                String* next = item.get_safe_string();
                if (previous && next) {
                    if (is_ui) {
                        list->items[list->length - 1] = ui_merge_strings_to_arena(
                            input_allocation_context->arena, previous, next);
                        return;
                    }
                    size_t new_length = previous->len + next->len;
                    String* merged;
                    if (input_context && input_context->consts) {
                        // This allocation may compact the list and the incoming
                        // string. Root both owners before reading either again.
                        RootFrame roots((Context*)context, 2);
                        Rooted<List*> rooted_list(roots, list);
                        Rooted<Item> rooted_item(roots, item);
                        merged = (String*)context->context_alloc(
                            sizeof(String) + new_length + 1, LMD_TYPE_STRING);
                        list = rooted_list.get();
                        item = rooted_item.get();
                        previous = list->items[list->length - 1].get_safe_string();
                        next = item.get_safe_string();
                    } else if (input_allocation_context) {
                        merged = (String*)pool_calloc(input_allocation_context->pool,
                            sizeof(String) + new_length + 1);
                    } else {
                        merged = (String*)pool_calloc(input_context->pool,
                            sizeof(String) + new_length + 1);
                    }
                    if (!merged || !previous || !next) return;
                    memcpy(merged->chars, previous->chars, previous->len);
                    memcpy(merged->chars + previous->len, next->chars, next->len);
                    merged->chars[new_length] = '\0';
                    merged->len = new_length;
                    list->items[list->length - 1] = {.item = s2it(merged)};
                    return;
                }
            }
        }
    }

    if (list->length + list->extra + 2 > list->capacity) {
        // expand_list is a safepoint; the incoming value is not owned by the
        // list yet, so retain it independently across the allocation.
        RootFrame roots((Context*)context, 1);
        Rooted<Item> rooted_item(roots, item);
        expand_list(list, nullptr);
        item = rooted_item.get();
    }
    if (!list->items) {
        log_error("list_push: collection growth did not provide backing storage");
        return;
    }
    array_set((Array*)list, list->length, item);
    list->length++;
}

void list_push_spread(List* list, Item item) {
    TypeId type_id = get_type_id(item);
    if (item.item == ITEM_NULL_SPREADABLE) return;
    if (type_id == LMD_TYPE_ARRAY) {
        Array* arr = item.array;
        if (arr && arr->is_spreadable) {
            RootFrame roots((Context*)context, 2);
            Rooted<List*> rooted_list(roots, list);
            Rooted<Item> rooted_source(roots, item);
            for (int64_t i = 0; i < arr->length; i++) {
                list = rooted_list.get();
                arr = rooted_source.get().array;
                list_push(list, arr->items[i]);
            }
            return;
        }
    }
    if (type_id == LMD_TYPE_ARRAY_NUM) {
        ArrayNum* arr = item.array_num;
        if (arr && arr->is_spreadable) {
            RootFrame roots((Context*)context, 2);
            Rooted<List*> rooted_list(roots, list);
            Rooted<Item> rooted_source(roots, item);
            for (int64_t i = 0; i < arr->length; i++) {
                list = rooted_list.get();
                arr = rooted_source.get().array_num;
                list_push(list, array_num_read_borrowed_item(arr, i));
            }
            return;
        }
    }
    list_push(list, item);
}
